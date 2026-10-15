#pragma once
// One number for everything a phenotype holds except its species traits, so a
// pinned value proves a genome expresses exactly as it did when it was pinned.
// Species traits are left out on purpose: nothing set them before species
// genes existed, so they are asserted on their own.
#include <cstdint>
#include "blorb/creature.h"
#include "blorb/genes.h"

namespace blorb {

struct PhenotypePrint {
  uint32_t h = 2166136261u;
  void add(int64_t v) {
    for (int i = 0; i < 8; ++i) h = (h ^ uint8_t(uint64_t(v) >> (8 * i))) * 16777619u;
  }
  void fx(Fx v) { add(v.raw); }
};

inline uint32_t phenotypePrint(const Phenotype& p) {
  PhenotypePrint f;
  for (const Decay& d : p.chem.decay) f.add(d.keep), f.add(d.shift);
  for (const Reaction& r : p.chem.reactions) {
    f.add(r.a.v), f.add(r.b.v), f.add(r.c.v), f.add(r.d.v);
    f.add(r.qa), f.add(r.qb), f.add(r.qc), f.add(r.qd), f.fx(r.rate);
  }
  for (const Emitter& e : p.chem.emitters) {
    f.add(e.locus.v), f.add(e.chem.v), f.fx(e.threshold), f.fx(e.gain);
    f.add(e.digital), f.add(e.invert), f.add(e.clear), f.add(e.period);
  }
  for (const Receptor& r : p.chem.receptors) {
    f.add(r.chem.v), f.add(r.locus.v), f.fx(r.threshold), f.fx(r.nominal), f.fx(r.gain);
    f.add(r.digital), f.add(r.invert);
  }
  for (const Seed& s : p.chem.seeds) f.add(s.chem.v), f.fx(s.level);
  for (const auto& s : p.stimuli) {
    f.add(s.stim.v), f.add(s.whenAsleep), f.add(s.wakes);
    for (int i = 0; i < 3; ++i) f.add(s.chem[i].v), f.fx(s.amount[i]);
  }
  for (const Instinct& in : p.instincts) {
    for (int i = 0; i < in.cueCount; ++i) f.add(in.cue[i].v);
    f.add(in.cueCount), f.add(in.action.v), f.add(in.drive.v), f.fx(in.level), f.fx(in.strength);
  }
  for (const auto& x : p.faces) {
    f.add(x.face.v), f.fx(x.weight);
    for (int i = 0; i < 3; ++i) f.add(x.drive[i].v), f.fx(x.amount[i]);
  }
  for (const auto& x : p.palette) {
    f.add(x.region.v), f.add(x.tint.hue), f.add(x.tint.sat), f.add(x.tint.val);
    f.add(x.bound.v), f.fx(x.gain), f.add(x.hasBound);
  }
  for (const auto& x : p.marks) f.add(x.layer), f.add(x.variant), f.add(x.tint), f.add(x.bound.v), f.add(x.hasBound);
  const Temperament& t = p.temperament;
  f.fx(t.learnRate), f.fx(t.forgetRate), f.fx(t.explore), f.fx(t.habituation);
  f.add(t.dreamEveryTicks), f.add(t.dreamLenTicks), f.add(t.trace.keep), f.add(t.trace.shift);
  f.add(p.habitat.pantrySize), f.add(p.habitat.refillTicks), f.add(p.habitat.rotTicks), f.fx(p.habitat.biteSize);
  f.add(p.egg.incubateTicks), f.fx(p.egg.warmthBoost), f.add(p.egg.hatchBurstDreams);
  f.add(p.size.base), f.add(p.size.growth), f.add(p.size.squash);
  f.add(p.oracle.voice.v);
  for (uint8_t topic : p.oracle.topics) f.add(topic);
  f.add(p.expressedStages);
  return f.h;
}

// The whole life of a genome: every stage expressed, in order.
inline uint32_t lifePrint(const Genome& g, uint32_t legacyFeats) {
  Phenotype p{};
  for (uint8_t s = 0; s < kStageCount; ++s) expressStage(g, Stage(s), legacyFeats, p);
  return phenotypePrint(p);
}

}  // namespace blorb
