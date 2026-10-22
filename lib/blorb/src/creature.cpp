#include "blorb/creature.h"

#include <algorithm>

namespace blorb {
namespace {

constexpr Fx kHalf = Fx::ratio(1, 2);
// A new face must beat the one shown by this much, so faces do not flicker.
constexpr Fx kFaceHysteresis = Fx::ratio(1, 10);
// A sleep he chooses starts only with sleep_gate open this far, and runs
// until it falls below kHalf. The gap is the nap: without it a pet hovering
// at the gate dozes off and wakes every minute (docs/notes/first-hours.md).
constexpr Fx kSleepOnset = Fx::ratio(75, 100);
// A kid's hand wakes him, whatever his genes make of the gesture (owner,
// 2026-10-21: a pet that slept through every knock read as asleep all the
// time). Roused, his sleepiness drops to under the nap's onset (sleep_gate
// 0.75 is about 0.475), so he stays up a while; where it is light the night's
// melatonin goes too, or it would make him sleepy again within minutes. In
// the wall's night (no light) it does not, and he dozes off again.
constexpr Fx kRousedSleepiness = Fx::ratio(45, 100);
// Not a knock knock: that is a press, a pellet for him, and feeding a sleeper
// leaves him asleep (test_feeding KnockKnock.AsleepAPelletStillDrops).
constexpr StimId kRousing[] = {stim::knock,   stim::shake,   stim::picked_up, stim::dropped,
                               stim::flipped, stim::righted, stim::lid_up,    stim::touched};
bool rouses(StimId s) {
  for (StimId r : kRousing) if (r == s) return true;
  return false;
}

// Play (an idle, idle.h, over a rest) only when nothing presses: past these he would
// rather eat, hide, rest or sleep, and a play already going stops for it.
constexpr Fx kPlayHunger = Fx::ratio(55, 100), kPlayFear = Fx::ratio(3, 10), kPlayHurt = Fx::ratio(3, 10),
             kPlaySleepy = Fx::ratio(5, 10);
constexpr Fx kIdleFace = Fx::ratio(8, 10);
constexpr Fx kLevel = Fx::ratio(3, 100);       // a tilt locus this near 0.5 is a dish on a desk (habitat.cpp's dead zone)
constexpr Fx kCalmFear = Fx::ratio(2, 10);     // a curl with less fear than this, and no knock or shake
constexpr Fx kLong = Fx::ratio(1, 1000);       // just now (a recent locus halves each tick), is the brain's noise, not hiding
Fx absFx(Fx v) { return v < Fx::zero() ? -v : v; }   // the idle's face, shown whatever his genes score
// Sense loci only a behaviour writes; cleared each tick so they stop with it.
constexpr LocusId kBehaviourLoci[] = {locus::eating, locus::foreseeing};
constexpr uint8_t kRecentEnd = kRecentBase + 64;

struct StageStep { Stage from; LocusId trigger; Stage to; };
constexpr StageStep kStageSteps[] = {
    {Stage::Baby, locus::become_child, Stage::Child},
    {Stage::Child, locus::become_adult, Stage::Adult},
    {Stage::Adult, locus::become_elder, Stage::Elder},
};

// Stage and die loci fire at 0.5 or more; a reflex starts as its locus rises past 0.5.
bool fires(Fx level) { return level >= kHalf; }

// Each cause receptor adds its own power of two in sixteenths (starter_genome.cpp),
// so the sum is unique; the highest set bit wins: Poisoned > Injured > Starved > OldAge.
DeathCause causeOf(Fx level) {
  int code = std::min(15, (level.raw + (Fx::kOne >> 5)) >> (Fx::kFrac - 4));
  for (int bit = 3; bit >= 0; --bit)
    if (code & (1 << bit)) return DeathCause(bit);
  return DeathCause::Unknown;
}

uint16_t bump(uint16_t v) { return v == UINT16_MAX ? v : uint16_t(v + 1); }

template <class Row, size_t N, class IdT>
const Row* rowOf(const Row (&rows)[N], IdT id) {
  for (const Row& r : rows) if (r.id == id) return &r;
  return nullptr;
}

uint16_t minTicksOf(ActionId a) {
  const ActionInfo* row = rowOf(ACTIONS, a);
  return row ? row->minTicks : 0;
}

void count(LifeStats& s, StimId id) {
  switch (id.v) {
    case stim::shake.v: s.shaken = bump(s.shaken); break;
    case stim::knock.v:
    case stim::double_knock.v: s.knocked = bump(s.knocked); break;
    case stim::cradle.v: s.cradled = bump(s.cradled); break;
    case stim::dropped.v: s.dropped = bump(s.dropped); break;
    case stim::fed.v: s.fed = bump(s.fed); break;
    case stim::played.v: s.played = bump(s.played); break;
    case stim::dawn.v: s.nights = bump(s.nights); break;
    default: break;
  }
}

StimSource sourceOf(StimId s) {
  const StimInfo* row = rowOf(STIMULI, s);
  return row ? row->source : StimSource::Body;
}

Fx driveLevel(const Chemistry& c, DriveId d) { return d.v < kDriveCount ? c.drive(d) : Fx::zero(); }

Fx faceScore(const Phenotype::Face& f, const Chemistry& c) {
  Fx s = f.weight;
  for (int i = 0; i < 3; ++i) s += f.amount[i] * driveLevel(c, f.drive[i]);
  return clamp01(s);
}

// The best score among the genes for this face; a duplicated gene can only help.
Fx scoreOf(ExprId face, const Phenotype& p, const Chemistry& c) {
  Fx best{};
  for (const Phenotype::Face& f : p.faces) if (f.face == face) best = fxMax(best, faceScore(f, c));
  return best;
}

void showFace(FaceState& face, ExprId want, Fx intensity, uint32_t tick) {
  if (want != face.current) {
    face.previous = face.current;
    face.current = want;
    face.sinceTick = tick;
  }
  face.intensity = intensity;
}

// Whoever runs expressStage seeds the chemicals and queues the instincts it appended, once.
void express(Stage s, const Genome& g, uint32_t feats, Phenotype& p, Chemistry& c, Brain& b) {
  size_t seeds = p.chem.seeds.size(), instincts = p.instincts.size();
  expressStage(g, s, feats, p);
  for (size_t i = seeds; i < p.chem.seeds.size(); ++i) c.chem[p.chem.seeds[i].chem.v] = clamp01(p.chem.seeds[i].level);
  for (size_t i = instincts; i < p.instincts.size(); ++i) b.queueInstinct(p.instincts[i]);
}

// Each stage locus past 0.5 expresses the next stage, once.
void growUp(Stage& stage, LifeStats& stats, const Genome& g, uint32_t feats, Phenotype& p, Chemistry& c, Brain& b) {
  for (const StageStep& s : kStageSteps)
    if (stage == s.from && fires(c.locus[s.trigger.v])) {
      express(s.to, g, feats, p, c, b);
      stage = s.to;
      stats.reached = s.to;
      return;
    }
}

// The sense loci the creature writes for itself before the senses land.
void ownLoci(Chemistry& c, const Body& body) {
  for (LocusId l : kBehaviourLoci) c.set(l, Fx::zero());
  c.set(locus::always, Fx::one());
  c.set(locus::asleep, body.asleep ? Fx::one() : Fx::zero());
  c.set(locus::age, Fx::one() - c.chem[chem::life.v]);
}

}  // namespace

Creature::Creature(const Egg& egg, uint32_t legacyFeats, uint32_t tick)
    : genome_(egg.genome()), generation_(egg.generation()), legacyFeats_(legacyFeats), rng_(Rng::seeded(genome_.hash())) {
  express(Stage::Baby, genome_, legacyFeats, pheno_, chem_, brain_);
  for (uint8_t i = 0; i < pheno_.egg.hatchBurstDreams; ++i) brain_.dream(pheno_.temperament, rng_);
  actionDone_ = true;   // the brain decides at its first think
  face_.sinceTick = tick;
  pendingSelf_.fire(stim::hatched);
}

void Creature::tick(const SenseOut& senses, Habitat& habitat, Behaviours& behaviours, uint32_t tick) {
  if (dead()) return;
  const SenseOut self = pendingSelf_;
  pendingSelf_.clear();

  // 1 sense loci
  ownLoci(chem_, body_);
  for (const SenseOut* in : {&senses, &self})
    for (uint8_t i = 0; i < in->lociCount; ++i) chem_.set(in->loci[i].locus, in->loci[i].value);

  ActionCtx ctx{habitat, pheno_.habitat, chem_.locus[locus::tilt_x.v], chem_.locus[locus::tilt_y.v], tick, rng_,
                pendingSelf_};
  ctx.idle = &idle_;
  ctx.rotIsFood = chem_.drive(drive::hunger) >= kEatsRotAt;
  ctx.picky = &picky_;
  ctx.age = stats_.ageTicks;
  auto calm = [&] {
    return !body_.asleep && chem_.drive(drive::hunger) < kPlayHunger && chem_.drive(drive::fear) < kPlayFear &&
           chem_.drive(drive::pain) < kPlayHurt && chem_.drive(drive::discomfort) < kPlayHurt &&
           chem_.drive(drive::sleepiness) < kPlaySleepy;
  };
  auto mood = [&] {
    IdleMood m;
    m.species = pheno_.species.body;
    m.genomeHash = genome_.hash();
    m.boredom = chem_.drive(drive::boredom);
    m.loneliness = chem_.drive(drive::loneliness);
    m.needTouch = chem_.drive(drive::need_touch);
    m.sleepiness = chem_.drive(drive::sleepiness);
    m.explore = pheno_.temperament.explore;
    m.dark = !fires(chem_.locus[locus::light.v]);
    m.age = stats_.ageTicks;
    // #61: his day, how he is, his age and his genes.
    m.part = dayPart();
    m.hunger = chem_.drive(drive::hunger);
    m.pain = chem_.drive(drive::pain);
    m.discomfort = chem_.drive(drive::discomfort);
    m.injury = chem_.chem[chem::injury.v];
    m.toxin = chem_.chem[chem::toxin.v];
    Fx most{};
    for (const DriveInfo& row : DRIVES) most = fxMax(most, chem_.drive(row.id));
    // As Appearance::moods' content: 1 with no drive past 0.15, down to 0 as the most pressing nears 1.
    const Fx mark = Fx::ratio(15, 100);
    m.content = most <= mark ? Fx::one()
                             : clamp01(Fx::one() - Fx{int32_t(int64_t(most.raw - mark.raw) * Fx::kOne / (Fx::kOne - mark.raw))});
    m.stage = stage_;
    m.habituation = pheno_.temperament.habituation;
    m.dreamLenTicks = pheno_.temperament.dreamLenTicks;
    m.bulk = pheno_.size.base ? pheno_.size.base : 128;
    m.pressed = !calm();
    return m;
  };
  // Pressed (not calm), he may still show how he feels: awake, not afraid.
  auto mayFeel = [&] { return !body_.asleep && chem_.drive(drive::fear) < kPlayFear; };
  // A play goes on while he is calm; one that shows a feeling, while he still feels it.
  auto playOn = [&] {
    if (calm()) return true;
    if (!mayFeel()) return false;
    return feels(idle_.now, mood()) > 0;
  };
  // He slept long enough to have woken (#61): his day starts again.
  const bool sleptLong = body_.asleep && action_ == action::sleep && stats_.ageTicks - actionStart_ >= kNapTicks;
  const bool sleptNight = sleptLong && stats_.ageTicks - actionStart_ >= kNightSleepTicks;
  auto finish = [&] {
    behaviours.stop(action_, body_, ctx);
    actionDone_ = true;
    idle_.now = idles::none;
  };
  auto switchTo = [&](ActionId a) {
    if (!actionDone_) behaviours.stop(action_, body_, ctx);
    idle_.now = idles::none;
    action_ = a;
    actionStart_ = stats_.ageTicks;
    actionDone_ = false;
    behaviours.start(a, body_, ctx);
    const ActionInfo* row = rowOf(ACTIONS, a);
    if (row && row->selfStim != stim::none) pendingSelf_.fire(row->selfStim);
    if (a == action::foresee) stats_.foresights = bump(stats_.foresights);
  };

  // 2 stimuli through this creature's stimulus genes
  bool woken = false, roused = rouseAsked_;
  rouseAsked_ = false;
  // A play stops for anything from outside him: a knock, a pellet, the phone.
  if (playing() && senses.stimCount) finish();
  for (const SenseOut* in : {&senses, &self})
    for (uint8_t i = 0; i < in->stimCount; ++i) {
      StimId s = in->stimuli[i];
      roused = roused || (in == &senses && rouses(s));
      if (in == &senses && rouses(s) && !body_.asleep) idle_.touched(stats_.ageTicks);   // a hand: say hello
      chem_.set(locus::recent(s), Fx::one());
      count(stats_, s);
      const bool duel = sourceOf(s) == StimSource::Duel;
      for (const Phenotype::StimResponse& r : pheno_.stimuli) {
        if (r.stim != s || (body_.asleep && !r.whenAsleep)) continue;
        for (int k = 0; k < 3; ++k)
          chem_.add(r.chem[k], duel ? duelFilter(duelStakes_, r.chem[k], r.amount[k]) : r.amount[k]);
        woken = woken || r.wakes;
      }
    }
  if (roused && body_.asleep && !keepAsleep_ && !actionDone_) {
    finish();
    const ChemId sleepy = driveChem(drive::sleepiness);
    chem_.chem[sleepy.v] = fxMin(chem_.chem[sleepy.v], kRousedSleepiness);
    if (fires(chem_.locus[locus::light.v])) chem_.chem[chem::melatonin.v] = Fx::zero();
  }
  if ((woken || wakeAsked_) && body_.asleep && !actionDone_) finish();
  wakeAsked_ = false;

  // 3 chemistry
  Fx before[kReflexCount];
  for (size_t r = 0; r < kReflexCount; ++r) before[r] = chem_.locus[REFLEXES[r].trigger.v];
  chem_.step(pheno_.chem, tick);

  // 4 lifecycle
  growUp(stage_, stats_, genome_, legacyFeats_, pheno_, chem_, brain_);
  if (dead()) return;
  for (size_t r = 0; r < kReflexCount; ++r) {
    Fx now = chem_.locus[REFLEXES[r].trigger.v];
    if (before[r] > kHalf || now <= kHalf) continue;
    body_.reflex = ActiveReflex{REFLEXES[r].id, 0, REFLEXES[r].ticks, now};
    if (REFLEXES[r].id == reflex::hop) stats_.hops = bump(stats_.hops);
  }

  // 5 brain
  const Temperament& t = pheno_.temperament;
  bool choseSleep = false;
  if (forced_) {
    switchTo(*forced_);
    forced_.reset();
  } else if (tick % kBrainEvery == 0 && body_.asleep) {
    uint32_t every = std::max<uint32_t>(kBrainEvery, t.dreamEveryTicks);
    uint32_t asleepFor = stats_.ageTicks - actionStart_;
    if (asleepFor >= every && asleepFor / every != (asleepFor - kBrainEvery) / every) {
      body_.dreaming = brain_.dream(t, rng_);
      if (body_.dreaming) stats_.dreams = bump(stats_.dreams);
    } else if (asleepFor % every >= t.dreamLenTicks) {
      body_.dreaming = false;
    }
  } else if (tick % kBrainEvery == 0 && playing() && playOn()) {
    // An idle runs to its end (idles.def lengths) unless something presses;
    // the brain learns what it did once it is over.
  } else if (tick % kBrainEvery == 0) {
    Fx features[kFeatureCount], drives[kDriveCount];
    for (size_t f = 0; f < kFeatureCount; ++f) features[f] = chem_.locus[FEATURES[f].v];
    for (size_t d = 0; d < kDriveCount; ++d) drives[d] = chem_.drive(DRIVES[d].id);
    Decision d = brain_.think(features, drives, chem_.locus[locus::arousal.v], t, actionDone_, rng_);
    bool held = !actionDone_ && stats_.ageTicks - actionStart_ < minTicksOf(action_);
    if (!held && (actionDone_ || d.action != action_)) {
      ActionId next = d.action;
      // Hungry, with only rot in the dish and not starving: he goes to sniff
      // it and turn it down (actions.h Picky), now and then, so a kid sees why.
      const bool loose = next == action::rest || next == action::wander || next == action::foresee ||
                         next == action::hop_circles || next == action::call;
      const Fx hunger = chem_.drive(drive::hunger);
      if (loose && hunger >= kHalf && hunger < kEatsRotAt && chem_.drive(drive::fear) < kPlayFear &&
          picky_.mayRefuse(stats_.ageTicks) && !habitat.nearestFood(pheno_.habitat, body_.at, tick, false) &&
          habitat.nearestRotten(pheno_.habitat, body_.at, tick)) {
        next = action::eat;
        brain_.relabel(next);
      }
      // A rest or a wander, awake with nothing pressing, is often play: his
      // species' liveliness, more when bored (idle.h). He plays an idle, or
      // rests when every one is cooling down.
      // Picks that would only show him standing about: a rest, a wander, a
      // tilt-follow or -flee on a level dish, a curl with nothing to fear.
      const bool level = absFx(chem_.locus[locus::tilt_x.v] - kHalf) < kLevel && absFx(chem_.locus[locus::tilt_y.v] - kHalf) < kLevel;
      const bool idleish = next == action::rest || next == action::wander ||
                           ((next == action::follow_tilt || next == action::flee_tilt) && level) ||
                           (next == action::curl && chem_.drive(drive::fear) < kCalmFear &&
                            chem_.locus[locus::recent(stim::knock).v] < kLong && chem_.locus[locus::recent(stim::shake).v] < kLong);
      // A kid's hand just now: unless he would rather eat, hide or sleep, he
      // greets him (a Show idle or a dance), whatever its cooldown.
      const bool greets = idle_.greet && calm() && (idleish || next == action::foresee ||
                                                    next == action::hop_circles || next == action::call);
      idle_.greet = false;
      IdleId pick = idles::none;
      if (greets) {
        IdleMood m = mood();
        m.greeting = true;
        pick = pickIdle(idle_, m, rng_);
        if (pick != idles::none) idle_.greetedAt = stats_.ageTicks + 1;
      } else if (idleish && calm() && rng_.chance(idleLiveliness(mood()))) {
        pick = pickIdle(idle_, mood(), rng_);   // none when every one is cooling down: he rests
      } else if (idleish && !calm() && mayFeel() && rng_.chance(idleLiveliness(mood()))) {
        pick = pickIdle(idle_, mood(), rng_);   // something presses: he shows it (a Feel act), or rests
      }
      if (pick != idles::none && next != action::rest) {
        next = action::rest;   // he plays over a rest
        brain_.relabel(next);
      }
      if (next != action_ || actionDone_) switchTo(next);
      if (pick != idles::none) beginIdle(idle_, pick, body_.at, stats_.ageTicks, rng_, idlePace(mood()));
      choseSleep = next == action::sleep;
    }
  }

  // 6 behaviour, unless a reflex owns the body
  PoseId pose = body_.pose;
  if (body_.reflex) {
    ActiveReflex& r = *body_.reflex;
    if (const ReflexInfo* row = rowOf(REFLEXES, r.kind)) body_.pose = row->pose;
    if (++r.tick >= r.ticks) body_.reflex.reset();
  } else if (playing() && !playOn()) {
    finish();   // something presses: the brain picks again at its next think
  } else if (playing()) {
    // His idle in place of the rest's step, to its end; then the brain picks again.
    if (behave::Play{}.step(body_, ctx) == Status::Done) {
      finish();
    } else {
      IdleEffect fx[3];
      for (uint8_t i = 0, n = idleEffects(idle_.now, idle_.length, fx); i < n; ++i) chem_.add(driveChem(fx[i].drive), fx[i].perTick);
    }
  } else if (!actionDone_) {
    Status s = behaviours.step(action_, body_, ctx);
    const Fx gate = chem_.locus[locus::sleep_gate.v];
    if (action_ == action::sleep && !keepAsleep_ && (!fires(gate) || (choseSleep && gate < kSleepOnset))) s = Status::Done;
    if (s == Status::Done) finish();
  }
  chem_.add(chem::food, ctx.foodEaten);
  body_.poseTick = body_.pose == pose ? uint16_t(body_.poseTick + 1) : 0;
  if (body_.pose != pose::eat) body_.mouth = Mouthful::Nothing;   // a reflex or another action ended the chew

  // 7 face, stats, recent loci
  if (body_.reflex) {
    const ReflexInfo* row = rowOf(REFLEXES, body_.reflex->kind);
    showFace(face_, row ? row->face : face_.current, body_.reflex->strength, tick);
  } else if (picky_.showing(stats_.ageTicks)) {
    showFace(face_, expr::annoyed, kIdleFace, tick);   // yuck
  } else if (playing()) {
    showFace(face_, idleFace(idle_.now, idle_.ticks, idle_.length), kIdleFace, tick);
  } else {
    ExprId best = face_.current;
    Fx bestScore = scoreOf(face_.current, pheno_, chem_), shown = bestScore;
    for (const Phenotype::Face& f : pheno_.faces) {
      Fx s = faceScore(f, chem_);
      if (s > bestScore) best = f.face, bestScore = s;
    }
    if (bestScore > shown + kFaceHysteresis) showFace(face_, best, bestScore, tick);
    else showFace(face_, face_.current, shown, tick);
  }
  for (size_t a = 0; a < kActionCount; ++a)
    if (ACTIONS[a].id == action_) stats_.actionTicks[a] = bump(stats_.actionTicks[a]);
  if (sleptLong && !body_.asleep) idle_.wokeAt = stats_.ageTicks + 1;   // up: groggy a while (DayPart::Waking)
  if (sleptNight && !body_.asleep) idle_.dayAt = stats_.ageTicks + 1;   // up from a night's sleep: his morning
  ++stats_.ageTicks;
  for (uint8_t l = kRecentBase; l < kRecentEnd; ++l) chem_.locus[l] = Fx{chem_.locus[l].raw >> 1};
}

void Creature::tickCoarse(const SenseOut& senses, uint32_t ticks, uint32_t tick) {
  if (dead()) return;
  ownLoci(chem_, body_);
  for (uint8_t i = 0; i < senses.lociCount; ++i) chem_.set(senses.loci[i].locus, senses.loci[i].value);
  chem_.stepCoarse(pheno_.chem, ticks, tick);
  growUp(stage_, stats_, genome_, legacyFeats_, pheno_, chem_, brain_);
  stats_.ageTicks += ticks;
  body_.at = DishPos{};   // a stretch with nobody there is lived at rest, so he is home when it ends
  // Recent loci halve every tick, so a coarse step leaves nothing of them.
  for (uint8_t l = kRecentBase; l < kRecentEnd; ++l) chem_.locus[l] = Fx::zero();
}

bool Creature::dead() const { return fires(chem_.locus[locus::die.v]); }

DeathCause Creature::cause() const { return causeOf(chem_.locus[locus::cause.v]); }

void Creature::inject(ChemId c, Fx level) {
  if (c.v != 0) chem_.chem[c.v] = clamp01(level);
}

void Creature::force(ActionId a) { forced_ = a; }

void Creature::fire(StimId s) { pendingSelf_.fire(s); }

bool Creature::editGene(GeneUid uid, uint8_t offset, uint8_t value) {
  std::optional<GeneView> v = genome_.find(uid);
  if (!v || offset >= v->header.len) return false;
  std::optional<Genome> edited = GenomeBuilder::from(genome_).setByte(uid, offset, value).build();
  if (!edited) return false;
  genome_ = std::move(*edited);
  // Levels and the instinct queue are live state, so nothing is reseeded or requeued.
  pheno_ = Phenotype{};
  for (uint8_t s = 0; s <= uint8_t(stage_); ++s) expressStage(genome_, Stage(s), legacyFeats_, pheno_);
  return true;
}

bool Creature::extendGenome(Genome grown) {
  const std::vector<uint8_t>& mine = genome_.bytes();
  const std::vector<uint8_t>& next = grown.bytes();
  if (next.size() <= mine.size() || !std::equal(mine.begin(), mine.end(), next.begin())) return false;
  genome_ = std::move(grown);
  pheno_ = Phenotype{};
  for (uint8_t s = 0; s <= uint8_t(stage_); ++s) expressStage(genome_, Stage(s), legacyFeats_, pheno_);
  return true;
}

Fx Creature::wound(Fx injury) {
  const Fx added = fxMin(fxMax(injury, Fx::zero()), woundRoom(*this));
  chem_.add(chem::injury, added);
  return added;
}

bool Creature::addScar(Scar scar) {
  if (scars_.count < kMaxScars) {
    scars_.mark[scars_.count++] = scar;
    return true;
  }
  Scar* faintest = &scars_.mark[0];
  for (Scar& m : scars_.mark) if (m.fade < faintest->fade) faintest = &m;
  if (scar.fade < faintest->fade) return false;
  *faintest = scar;
  return true;
}

// Both loci are receptor-owned and cleared at each chemistry step, but a dead
// creature never steps again, so the Dish's next tick reads them and runs
// onDeath once.
void Creature::fall() {
  if (dead()) return;
  chem_.set(locus::die, Fx::one());
  chem_.set(locus::cause, Fx::ratio(1 << uint8_t(DeathCause::Injured), 16));
}

bool Creature::prophesy(LocusId feature, ActionId a, DriveId d, Fx effect) {
  return brain_.setWeight(feature, a, d, effect);
}

// Field by field, so struct padding never reaches the hash.
uint32_t Creature::hash() const {
  uint32_t h = fnv1a(nullptr, 0);
  auto mix = [&h](uint32_t v) { h = fnv1a(&v, sizeof v, h); };
  mix(genome_.hash());
  mix(chem_.hash());
  mix(brain_.hash());
  mix(uint32_t(body_.at.x.raw));
  mix(uint32_t(body_.at.y.raw));
  mix(uint32_t(body_.facing.raw));
  mix(body_.pose.v);
  mix(body_.poseTick);
  mix(uint32_t(body_.asleep) | uint32_t(body_.dreaming) << 1 | uint32_t(body_.reflex.has_value()) << 2 |
      uint32_t(body_.mouth) << 3);
  if (body_.reflex) {
    mix(body_.reflex->kind.v);
    mix(body_.reflex->tick);
    mix(body_.reflex->ticks);
    mix(uint32_t(body_.reflex->strength.raw));
  }
  // Only once he has one, so every hash from before scars stands.
  for (uint8_t i = 0; i < scars_.count; ++i) {
    const Scar& m = scars_.mark[i];
    mix(uint32_t(m.kind) | uint32_t(m.x) << 8 | uint32_t(m.y) << 16 | uint32_t(m.fade) << 24);
  }
  mix(uint32_t(stage_));
  mix(generation_);
  mix(legacyFeats_);
  mix(action_.v);
  mix(actionStart_);
  mix(uint32_t(actionDone_) | uint32_t(forced_.has_value()) << 1 | uint32_t(forced_ ? forced_->v : 0) << 2);
  mix(stats_.ageTicks);
  mix(face_.current.v);
  mix(face_.previous.v);
  mix(uint32_t(face_.intensity.raw));
  mix(face_.sinceTick);
  for (uint32_t w : rng_.s) mix(w);
  mix(uint32_t(idle_.now.v) | uint32_t(idle_.last.v) << 8 | uint32_t(idle_.ticks) << 16);
  mix(idle_.length);
  mix(uint32_t(idle_.anchor.x.raw)), mix(uint32_t(idle_.anchor.y.raw));
  for (uint32_t at : idle_.startedAt) mix(at);
  mix(uint32_t(idle_.greet)), mix(idle_.greetedAt);
  mix(picky_.refusedAt), mix(picky_.sniff);
  // Only once set (#61), so every hash from before his day stands.
  if (idle_.wokeAt || idle_.dayAt || idle_.pace != 100) mix(idle_.wokeAt), mix(idle_.dayAt), mix(idle_.pace);
  return h;
}

DayPart Creature::dayPart() const {
  DayCues c;
  c.hour = hourOfDayLoci(chem_.locus[locus::day_sin.v], chem_.locus[locus::day_cos.v]);
  c.dark = !fires(chem_.locus[locus::light.v]);
  const uint32_t age = stats_.ageTicks;
  c.upTicks = idle_.wokeAt && idle_.wokeAt <= age + 1 ? age + 1 - idle_.wokeAt : age;
  c.dayTicks = idle_.dayAt && idle_.dayAt <= age + 1 ? age + 1 - idle_.dayAt : age;
  c.sleepiness = chem_.drive(drive::sleepiness);
  return dayPartOf(c);
}

const char* Creature::doing() const {
  if (playing()) return idleName(idle_.now);
  const ActionInfo* row = rowOf(ACTIONS, action_);
  return row ? row->name : "?";
}

}  // namespace blorb
