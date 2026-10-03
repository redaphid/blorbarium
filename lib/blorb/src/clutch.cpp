#include "blorb/creature.h"

#include <utility>

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
