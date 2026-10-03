#include "blorb/chemistry.h"

#include <algorithm>

namespace blorb {
namespace {

// Act loci and the free gene-to-gene band are written only by receptors, so
// both are cleared each step and an effect stops when its receptor stops.
constexpr uint16_t kReceptorOwnedFrom = 128;

// How far the input is past the threshold in the gene's direction; a gene
// fires only when this is positive.
Fx excess(Fx input, Fx threshold, bool invert) { return invert ? threshold - input : input - threshold; }

Fx emitted(const Emitter& e, Fx past) { return e.digital ? e.gain : e.gain * past; }

// Converts `rate` of the limiting reagent: q units of each reagent per unit of
// reaction, q units of each product, so consumed and produced amounts keep
// the gene's ratios. Chem id 0 or quantity 0 marks an unused slot.
void react(Fx (&chem)[kChemSlots], const Reaction& r, Fx rate) {
  const ChemId ids[4] = {r.a, r.b, r.c, r.d};
  const uint8_t qs[4] = {r.qa, r.qb, r.qc, r.qd};
  int32_t limit = INT32_MAX;
  for (int i = 0; i < 2; ++i)
    if (ids[i].v != 0 && qs[i] != 0) limit = std::min(limit, chem[ids[i].v].raw / qs[i]);
  if (limit == INT32_MAX) return;
  int64_t x = (Fx{limit} * rate).raw;
  if (x <= 0) return;
  for (int i = 0; i < 4; ++i) {
    if (ids[i].v == 0 || qs[i] == 0) continue;
    int64_t delta = x * qs[i];
    chem[ids[i].v] = clamp01(Fx::sat(chem[ids[i].v].raw + (i < 2 ? -delta : delta)));
  }
}

void receive(Chemistry& c, const ChemRules& rules) {
  for (uint16_t l = kReceptorOwnedFrom; l < kLocusSlots; ++l) c.locus[l] = Fx::zero();
  for (const Receptor& r : rules.receptors) {
    Fx past = excess(c.chem[r.chem.v], r.threshold, r.invert);
    if (past <= Fx::zero()) continue;
    c.locus[r.locus.v] += r.nominal + (r.digital ? r.gain : r.gain * past);
  }
  for (Fx& l : c.locus) l = clamp01(l);
}

// Stepping decays on every tick that is a multiple of 2^shift, so the closed
// form gets exactly as many applications as [tick, tick + stride) holds.
uint32_t decayTicks(Decay d, uint32_t tick, uint32_t stride) {
  uint64_t period = uint64_t(1) << d.shift;
  auto multiplesBelow = [&](uint64_t x) { return (x + period - 1) / period; };
  uint64_t n = multiplesBelow(uint64_t(tick) + stride) - multiplesBelow(tick);
  return uint32_t(std::min<uint64_t>(n << d.shift, UINT32_MAX));
}

}  // namespace

void Chemistry::add(ChemId c, Fx delta) {
  if (c.v != 0) chem[c.v] = clamp01(chem[c.v] + delta);
}

void Chemistry::set(LocusId l, Fx v) { locus[l.v] = clamp01(v); }

void Chemistry::step(const ChemRules& rules, uint32_t tick) {
  for (const Emitter& e : rules.emitters) {
    uint32_t mask = e.period >= 32 ? UINT32_MAX : (uint32_t(1) << e.period) - 1;
    if ((tick & mask) != 0) continue;
    Fx past = excess(locus[e.locus.v], e.threshold, e.invert);
    if (past <= Fx::zero()) continue;
    add(e.chem, emitted(e, past));
    if (e.clear) locus[e.locus.v] = Fx::zero();
  }
  for (const Reaction& r : rules.reactions) react(chem, r, r.rate);
  for (uint16_t i = 0; i < kChemSlots; ++i) chem[i] = applyDecay(chem[i], rules.decay[i], tick);
  receive(*this, rules);
}

// One step stands for `stride` ticks. Emission is split around the decay
// (half before, half after), the trapezoid rule, so a chemical held up by an
// emitter settles where stepping would put it instead of 1 / (1 - keep^n) off.
void Chemistry::stepCoarse(const ChemRules& rules, uint32_t stride, uint32_t tick) {
  Fx pending[kChemSlots]{};
  for (const Emitter& e : rules.emitters) {
    if (e.chem.v == 0) continue;
    Fx past = excess(locus[e.locus.v], e.threshold, e.invert);
    if (past <= Fx::zero()) continue;
    uint32_t period = e.period >= 32 ? 31 : e.period;
    Fx total = Fx::sat(int64_t(emitted(e, past).raw) * stride / (int64_t(1) << period));
    Fx half = Fx{total.raw / 2};
    add(e.chem, half);
    pending[e.chem.v] += total - half;
    if (e.clear) locus[e.locus.v] = Fx::zero();
  }
  for (const Reaction& r : rules.reactions) react(chem, r, fxMin(Fx::one(), Fx::sat(int64_t(r.rate.raw) * stride)));
  for (uint16_t i = 0; i < kChemSlots; ++i) {
    chem[i] = applyDecayTicks(chem[i], rules.decay[i], decayTicks(rules.decay[i], tick, stride));
    if (i != 0) chem[i] = clamp01(chem[i] + pending[i]);
  }
  receive(*this, rules);
}

uint32_t Chemistry::hash() const {
  return fnv1a(locus, sizeof locus, fnv1a(chem, sizeof chem));
}

}  // namespace blorb
