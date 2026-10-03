#include "blorb/creature.h"

#include <algorithm>
#include <type_traits>
#include <utility>
#include "blorb/appearance.h"

namespace blorb {
namespace {

// incubated_ counts in 1/256 ticks, so a fractional warmth boost adds up.
constexpr uint32_t kIncubateUnit = 256;
constexpr Fx kHalf = Fx::ratio(1, 2);

EggRules eggRulesOf(const Genome& g) {
  Phenotype p{};
  expressStage(g, Stage::Baby, 0, p);
  return p.egg;
}

Fx lastWrite(const SenseOut& s, LocusId l) {
  for (uint8_t i = 0; i < s.lociCount; ++i) if (s.loci[i].locus == l) return s.loci[i].value;
  return Fx::zero();
}

}  // namespace

Egg::Egg(Offspring child, uint16_t generation, uint32_t laidTick)
    : child_(std::move(child)), generation_(generation), laidTick_(laidTick), rules_(eggRulesOf(child_.genome)) {}

// A hand's warmth, or just holding him, adds warmthBoost of a tick per tick.
bool Egg::tick(const SenseOut& senses) {
  bool warm = lastWrite(senses, locus::held) >= kHalf || lastWrite(senses, locus::warmth) >= kHalf;
  incubated_ += kIncubateUnit + (warm ? uint32_t(rules_.warmthBoost.raw >> (Fx::kFrac - 8)) : 0);
  return incubated_ >= rules_.incubateTicks * kIncubateUnit;
}

Fx Egg::progress() const {
  uint64_t total = uint64_t(rules_.incubateTicks) * kIncubateUnit;
  if (total == 0) return Fx::one();
  return clamp01(Fx::sat((int64_t(incubated_) << Fx::kFrac) / int64_t(total)));
}

// ---- the clutch -----------------------------------------------------------------------

Clutch Creature::layClutch(uint8_t clutchSize, Fx wildBonus, uint32_t) {
  Clutch c;
  c.parent = genome_;
  c.generation = uint16_t(generation_ + 1);
  const MutationPolicy pol = policyOf(genome_, wildBonus);
  c.heirlooms = brain_.lessons(pheno_.instincts, pol.heirloomMinConfidence, pol.heirloomMax);
  c.wildBonus = wildBonus;
  for (uint64_t& s : c.seeds) {
    uint64_t hi = rng_.next();   // two statements: the order of the draws must not depend on the compiler
    s = hi << 32 | rng_.next();
  }
  c.count = uint8_t(std::clamp<int>(clutchSize, 1, kMaxClutch));
  c.cause = cause();
  c.reached = stage_;
  return c;
}

Offspring Clutch::child(uint8_t i) const {
  Rng rng = Rng::seeded(seeds[i < kMaxClutch ? i : 0]);
  MutationPolicy pol = policyOf(parent, wildBonus);
  pol.lookSlot = i;
  return mutate(parent, pol, heirlooms, rng);
}

// The vigil first; then Button or Knock moves the cursor, ButtonHold or
// DoubleKnock picks, and the timeout picks the cursor. One egg needs no choice.
std::optional<uint8_t> Clutch::tick(const SenseOut& senses) {
  ++sinceDeath;
  if (sinceDeath < kVigilTicks) return std::nullopt;
  if (count <= 1) return uint8_t(0);
  for (uint8_t i = 0; i < senses.stimCount; ++i) {
    StimId s = senses.stimuli[i];
    if (s == stim::button_hold || s == stim::double_knock) return cursor;
    if (s == stim::button || s == stim::knock) cursor = uint8_t((cursor + 1) % count);
  }
  if (sinceDeath >= kVigilTicks + kPickTimeoutTicks) return cursor;
  return std::nullopt;
}

void Clutch::derivePreviewStep() {
  if (previewed >= count) return;
  Offspring o = child(previewed);
  const Appearance born = portrait(o.genome, Stage::Baby, generation, 0);
  EggPreview& e = previews[previewed];
  e = EggPreview{born.regions[region::skin.v], born.regions[region::cloak.v], born.regions[region::shell.v], 0, 0};
  for (const MutationOp& op : o.diff.ops) {
    GeneUid uid = std::visit([](const auto& x) -> GeneUid {
      if constexpr (std::is_same_v<std::decay_t<decltype(x)>, MutHeirloom>) return x.after;
      else return x.gene;
    }, op);
    std::optional<GeneView> v = o.genome.find(uid);
    if (!v) v = parent.find(uid);
    const GeneTypeInfo* info = v ? geneType(v->header.type) : nullptr;
    bool mind = std::holds_alternative<MutHeirloom>(op) || (info && info->cls == GeneClass::Mind);
    e.mindChanges = uint8_t(e.mindChanges + mind);
    e.lookChanges = uint8_t(e.lookChanges + (!mind && info && info->cls == GeneClass::Look));
  }
  ++previewed;
}

uint32_t Egg::hash() const {
  uint32_t h = fnv1a(nullptr, 0);
  auto mix = [&h](uint32_t v) { h = fnv1a(&v, sizeof v, h); };
  mix(child_.genome.hash());
  mix(generation_);
  mix(laidTick_);
  mix(incubated_);
  return h;
}

}  // namespace blorb
