#include "blorb/appearance.h"

#include <algorithm>

namespace blorb {
namespace {

constexpr uint8_t kHatchlingScalePct = 55;
constexpr int kSizeLocusPct = 10;        // the size locus at 1 adds this much
constexpr int kHueShiftSteps = 64;       // the hue_shift locus at 1 turns every region a quarter
constexpr int kBoundValSteps = 127;      // a fully bound chemical nearly doubles its region's value
constexpr Fx kHintAt = Fx::ratio(1, 2);  // a drive this high is a need the care hint names
constexpr Fx kHalf = Fx::ratio(1, 2);

int scaled(Fx level, int steps) { return int((int64_t(clamp01(level).raw) * steps + Fx::kOne / 2) >> Fx::kFrac); }

uint8_t clampByte(int v) { return uint8_t(std::clamp(v, 0, 255)); }

// Hatchlings draw small; an adult is the art's size moved by its size gene
// (base 128 = x1, each step 1/256) and the size locus; the elder is the art.
uint8_t scaleFor(Stage stage, const Phenotype& p, Fx sizeLocus) {
  switch (stage) {
    case Stage::Baby:
    case Stage::Child: return kHatchlingScalePct;
    case Stage::Elder: return 100;
    case Stage::Adult: break;
  }
  uint8_t base = p.size.base ? p.size.base : 128;
  return clampByte(100 * (128 + int(base)) / 256 + scaled(sizeLocus, kSizeLocusPct));
}

// Region tints and marks from the genes; the later gene for a region wins, so
// a woken dormant colour shows. `chem` binds tints to chemical levels; a
// portrait has none.
void look(const Phenotype& p, const Chemistry* chem, Appearance& a) {
  for (const Phenotype::Paint& paint : p.palette) {
    Tint t = paint.tint;
    if (chem && paint.hasBound) t.val = clampByte(t.val + scaled(chem->chem[paint.bound.v] * paint.gain, kBoundValSteps));
    a.regions[paint.region.v % kRegionCount] = t;
  }
  if (chem) {
    int shift = scaled(chem->locus[locus::hue_shift.v], kHueShiftSteps);
    for (Tint& t : a.regions) t.hue = int8_t(((int(t.hue) + shift + 128) & 255) - 128);
  }
  for (const Phenotype::Mark& m : p.marks) {
    if (a.markCount >= std::size(a.marks)) break;
    a.marks[a.markCount++] = Appearance::Mark{m.layer, m.variant, Tint{int8_t(int(m.tint) - 128), 128, 128}};
  }
}

// Recent loci are set to 1 by a stimulus and halve every tick, so the
// strongest one is the latest stimulus and its halvings are its age.
void lastStimulus(const Chemistry& c, Appearance& a) {
  a.lastStim = stim::none;
  a.ticksSinceStim = UINT16_MAX;
  int best = -1;
  for (int l = kRecentBase; l < kRecentBase + 64; ++l)
    if (c.locus[l].raw > 0 && (best < 0 || c.locus[l] > c.locus[best])) best = l;
  if (best < 0) return;
  int halvings = 0;
  for (int32_t raw = Fx::kOne; raw > c.locus[best].raw && halvings < 32; raw >>= 1) ++halvings;
  a.lastStim = StimId{uint8_t(best - kRecentBase)};
  a.ticksSinceStim = uint16_t(halvings);
}

// The gesture for the most pressing need, while he is awake to be helped.
void careHint(const Creature& c, Appearance& a) {
  if (c.body().asleep) return;
  for (const CareInfo& care : CARES) {
    Fx level = c.chemistry().drive(care.drive);
    if (level >= kHintAt && (!a.hasHint || level > a.hintUrgency)) {
      a.hasHint = true;
      a.hint = care.id;
      a.hintUrgency = level;
    }
  }
}

void dishItems(const Habitat& h, const HabitatRules* rules, uint32_t tick, Appearance& a) {
  for (const Pellet& p : h.pellets) {
    if (!p.present) continue;
    bool rotten = rules && tick - p.droppedTick > rules->rotTicks;
    a.items[a.itemCount++] = Appearance::Item{rotten ? Appearance::Item::What::RottenPellet : Appearance::Item::What::Pellet, p.at};
  }
  a.items[a.itemCount++] = Appearance::Item{Appearance::Item::What::Marble, h.marble.at};
  a.pantry = h.pantry;
}

void presentCreature(const Creature& c, uint32_t tick, Appearance& a) {
  const Chemistry& chem = c.chemistry();
  const Body& body = c.body();
  a.kind = Appearance::Kind::Creature;
  a.generation = c.generation();
  a.stage = c.stage();
  a.lifeSeed = c.genome().hash();
  a.at = body.at;
  a.facing = body.facing;
  a.scalePct = scaleFor(c.stage(), c.phenotype(), chem.locus[locus::size.v]);
  a.pose = body.pose;
  a.poseTick = body.poseTick;
  a.expression = c.face().current;
  a.previous = c.face().previous;
  a.intensity = c.face().intensity;
  a.exprTicks = uint16_t(std::min<uint32_t>(UINT16_MAX, tick - c.face().sinceTick));
  if (body.reflex) {
    a.reflexActive = true;
    a.reflex = body.reflex->kind;
    a.reflexPhase = body.reflex->ticks ? Fx::ratio(body.reflex->tick, body.reflex->ticks) : Fx::one();
    a.reflexStrength = body.reflex->strength;
  }
  look(c.phenotype(), &chem, a);
  a.foreseeing = c.action() == action::foresee;
  a.glow = clamp01(chem.locus[locus::glow.v]);
  if (a.foreseeing) a.glow = fxMax(a.glow, kForeseeGlowFloor);
  a.asleep = body.asleep;
  a.dreaming = body.dreaming;
  a.calling = c.action() == action::call;
  a.eating = chem.locus[locus::eating.v] >= kHalf;
  a.injury = chem.chem[chem::injury.v];
  a.wobble = chem.locus[locus::wobble.v];
  careHint(c, a);
  lastStimulus(chem, a);
}

void presentEgg(const Egg& e, Appearance& a) {
  a.kind = Appearance::Kind::Egg;
  a.generation = e.generation();
  a.lifeSeed = e.genome().hash();
  a.eggProgress = e.progress();
  Phenotype p{};
  expressStage(e.genome(), Stage::Baby, 0, p);
  look(p, nullptr, a);
}

void presentClutch(const Clutch& k, Appearance& a) {
  a.lifeSeed = k.parent.hash();
  if (k.sinceDeath < Clutch::kVigilTicks) {
    a.kind = Appearance::Kind::Remains;
    a.generation = uint16_t(k.generation - 1);
    a.stage = k.reached;
    a.remainsFade = Fx::ratio(int32_t(k.sinceDeath), int32_t(Clutch::kVigilTicks));
    Phenotype p{};
    for (uint8_t s = 0; s <= uint8_t(k.reached); ++s) expressStage(k.parent, Stage(s), 0, p);
    look(p, nullptr, a);
  } else {
    a.kind = Appearance::Kind::Clutch;
    a.generation = k.generation;
  }
  a.eggCount = k.previewed;
  std::copy(k.previews, k.previews + k.previewed, a.eggs);
  a.cursor = k.cursor;
}

}  // namespace

Appearance present(const Occupant& occ, const Habitat& habitat, const PetClock& clock, uint32_t tick) {
  Appearance a;
  Fx fromNoon = clock.dayFraction() + clock.dayFraction() - Fx::one();   // -1 at midnight, 0 at noon, 1 at midnight
  a.night = fromNoon >= Fx::zero() ? fromNoon : -fromNoon;
  const HabitatRules* rules = nullptr;
  if (const Creature* c = std::get_if<Creature>(&occ)) {
    presentCreature(*c, tick, a);
    rules = &c->phenotype().habitat;
  } else if (const Egg* e = std::get_if<Egg>(&occ)) {
    presentEgg(*e, a);
  } else {
    presentClutch(std::get<Clutch>(occ), a);
  }
  dishItems(habitat, rules, tick, a);
  return a;
}

Appearance portrait(const Genome& g, Stage stage, uint16_t generation, uint32_t legacyFeats) {
  Phenotype p{};
  for (uint8_t s = 0; s <= uint8_t(stage); ++s) expressStage(g, Stage(s), legacyFeats, p);
  Appearance a;
  a.kind = Appearance::Kind::Creature;
  a.generation = generation;
  a.stage = stage;
  a.lifeSeed = g.hash();
  a.scalePct = scaleFor(stage, p, Fx::zero());
  a.pose = pose::idle;
  a.expression = a.previous = expr::neutral;
  a.lastStim = stim::none;
  look(p, nullptr, a);
  return a;
}

}  // namespace blorb
