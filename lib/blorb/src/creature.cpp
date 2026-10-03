#include "blorb/creature.h"

#include <algorithm>

namespace blorb {
namespace {

constexpr Fx kHalf = Fx::ratio(1, 2);
// A new face must beat the one shown by this much, so faces do not flicker.
constexpr Fx kFaceHysteresis = Fx::ratio(1, 10);
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
    case stim::marble_hit.v:
    case stim::played.v: s.played = bump(s.played); break;
    case stim::dawn.v: s.nights = bump(s.nights); break;
    default: break;
  }
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

}  // namespace

Creature Creature::hatch(const Egg& egg, uint32_t legacyFeats, uint32_t tick) {
  Creature c{};
  c.genome_ = egg.genome();
  c.generation_ = egg.generation();
  c.legacyFeats_ = legacyFeats;
  c.rng_ = Rng::seeded(c.genome_.hash());
  express(Stage::Baby, c.genome_, legacyFeats, c.pheno_, c.chem_, c.brain_);
  for (uint8_t i = 0; i < c.pheno_.egg.hatchBurstDreams; ++i) c.brain_.dream(c.pheno_.temperament, c.rng_);
  c.actionDone_ = true;   // the brain decides at its first think
  c.face_.sinceTick = tick;
  c.pendingSelf_.fire(stim::hatched);
  return c;
}

void Creature::tick(const SenseOut& senses, Habitat& habitat, Behaviours& behaviours, uint32_t tick) {
  if (dead()) return;
  const SenseOut self = pendingSelf_;
  pendingSelf_.clear();

  // 1 sense loci
  for (LocusId l : kBehaviourLoci) chem_.set(l, Fx::zero());
  chem_.set(locus::always, Fx::one());
  chem_.set(locus::asleep, body_.asleep ? Fx::one() : Fx::zero());
  chem_.set(locus::age, Fx::one() - chem_.chem[chem::life.v]);
  for (const SenseOut* in : {&senses, &self})
    for (uint8_t i = 0; i < in->lociCount; ++i) chem_.set(in->loci[i].locus, in->loci[i].value);

  ActionCtx ctx{habitat, pheno_.habitat, chem_.locus[locus::tilt_x.v], chem_.locus[locus::tilt_y.v], tick, rng_,
                pendingSelf_};
  auto finish = [&] {
    behaviours.stop(action_, body_, ctx);
    actionDone_ = true;
  };
  auto switchTo = [&](ActionId a) {
    if (!actionDone_) behaviours.stop(action_, body_, ctx);
    action_ = a;
    actionStart_ = stats_.ageTicks;
    actionDone_ = false;
    behaviours.start(a, body_, ctx);
    const ActionInfo* row = rowOf(ACTIONS, a);
    if (row && row->selfStim != stim::none) pendingSelf_.fire(row->selfStim);
    if (a == action::foresee) stats_.foresights = bump(stats_.foresights);
  };

  // 2 stimuli through this creature's stimulus genes
  bool woken = false;
  for (const SenseOut* in : {&senses, &self})
    for (uint8_t i = 0; i < in->stimCount; ++i) {
      StimId s = in->stimuli[i];
      chem_.set(locus::recent(s), Fx::one());
      count(stats_, s);
      for (const Phenotype::StimResponse& r : pheno_.stimuli) {
        if (r.stim != s || (body_.asleep && !r.whenAsleep)) continue;
        for (int k = 0; k < 3; ++k) chem_.add(r.chem[k], r.amount[k]);
        woken = woken || r.wakes;
      }
    }
  if (woken && body_.asleep && !actionDone_) finish();

  // 3 chemistry
  Fx before[kReflexCount];
  for (size_t r = 0; r < kReflexCount; ++r) before[r] = chem_.locus[REFLEXES[r].trigger.v];
  chem_.step(pheno_.chem, tick);

  // 4 lifecycle
  for (const StageStep& s : kStageSteps)
    if (stage_ == s.from && fires(chem_.locus[s.trigger.v])) {
      express(s.to, genome_, legacyFeats_, pheno_, chem_, brain_);
      stage_ = s.to;
      stats_.reached = s.to;
      break;
    }
  if (dead()) return;
  for (size_t r = 0; r < kReflexCount; ++r) {
    Fx now = chem_.locus[REFLEXES[r].trigger.v];
    if (before[r] > kHalf || now <= kHalf) continue;
    body_.reflex = ActiveReflex{REFLEXES[r].id, 0, REFLEXES[r].ticks, now};
    if (REFLEXES[r].id == reflex::hop) stats_.hops = bump(stats_.hops);
  }

  // 5 brain
  const Temperament& t = pheno_.temperament;
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
  } else if (tick % kBrainEvery == 0) {
    Fx features[kFeatureCount], drives[kDriveCount];
    for (size_t f = 0; f < kFeatureCount; ++f) features[f] = chem_.locus[FEATURES[f].v];
    for (size_t d = 0; d < kDriveCount; ++d) drives[d] = chem_.drive(DRIVES[d].id);
    Decision d = brain_.think(features, drives, chem_.locus[locus::arousal.v], t, actionDone_, rng_);
    bool held = !actionDone_ && stats_.ageTicks - actionStart_ < minTicksOf(action_);
    if (!held && (actionDone_ || d.action != action_)) switchTo(d.action);
  }

  // 6 behaviour, unless a reflex owns the body
  PoseId pose = body_.pose;
  if (body_.reflex) {
    ActiveReflex& r = *body_.reflex;
    if (const ReflexInfo* row = rowOf(REFLEXES, r.kind)) body_.pose = row->pose;
    if (++r.tick >= r.ticks) body_.reflex.reset();
  } else if (!actionDone_) {
    Status s = behaviours.step(action_, body_, ctx);
    if (action_ == action::sleep && !fires(chem_.locus[locus::sleep_gate.v])) s = Status::Done;
    if (s == Status::Done) finish();
  }
  chem_.add(chem::food, ctx.foodEaten);
  body_.poseTick = body_.pose == pose ? uint16_t(body_.poseTick + 1) : 0;

  // 7 face, stats, recent loci
  if (body_.reflex) {
    const ReflexInfo* row = rowOf(REFLEXES, body_.reflex->kind);
    showFace(face_, row ? row->face : face_.current, body_.reflex->strength, tick);
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
  ++stats_.ageTicks;
  for (uint8_t l = kRecentBase; l < kRecentEnd; ++l) chem_.locus[l] = Fx{chem_.locus[l].raw >> 1};
}

bool Creature::dead() const { return fires(chem_.locus[locus::die.v]); }

DeathCause Creature::cause() const { return causeOf(chem_.locus[locus::cause.v]); }

void Creature::inject(ChemId c, Fx level) {
  if (c.v != 0) chem_.chem[c.v] = clamp01(level);
}

void Creature::force(ActionId a) { forced_ = a; }

void Creature::fire(StimId s) { pendingSelf_.fire(s); }

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
  mix(uint32_t(body_.asleep) | uint32_t(body_.dreaming) << 1 | uint32_t(body_.reflex.has_value()) << 2);
  if (body_.reflex) {
    mix(body_.reflex->kind.v);
    mix(body_.reflex->tick);
    mix(body_.reflex->ticks);
    mix(uint32_t(body_.reflex->strength.raw));
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
  return h;
}

}  // namespace blorb
