#include "blorb/stakes.h"

#include <algorithm>
#include "blorb/creature.h"
#include "blorb/lineage.h"

namespace blorb {
namespace {

// Fine enough that a receptor's threshold is found within 1/512, far inside
// kWoundMargin; coarse enough to cost a few thousand steps once per turn.
constexpr int kLethalSteps = 512;

// The die locus his receptors would write with injury at `injury` and every
// other chemical where it is now (the sum chemistry.cpp's receptor step makes).
Fx dieAt(const Creature& c, Fx injury) {
  Fx die{};
  for (const Receptor& r : c.phenotype().chem.receptors) {
    if (r.locus != locus::die) continue;
    const Fx level = r.chem == chem::injury ? injury : c.chemistry().chem[r.chem.v];
    const Fx past = r.invert ? r.threshold - level : level - r.threshold;
    if (past > Fx::zero()) die += r.nominal + (r.digital ? r.gain : r.gain * past);
  }
  return die;
}

// The highest injury below the lowest at which he would die, or 1 when no
// injury kills him. A step short of the first lethal step, so it never rounds
// up into death.
Fx lethalInjury(const Creature& c) {
  for (int k = 0; k <= kLethalSteps; ++k)
    if (dieAt(c, Fx::ratio(k, kLethalSteps)) >= Fx::ratio(1, 2)) return Fx::ratio(std::max(0, k - 1), kLethalSteps);
  return Fx::one();
}

// What a drop does to him by his own genes: every dropped gene's injury rise.
Fx dropInjury(const Creature& c) {
  Fx sum{};
  for (const Phenotype::StimResponse& r : c.phenotype().stimuli)
    if (r.stim == stim::dropped)
      for (int k = 0; k < 3; ++k)
        if (r.chem[k] == chem::injury && r.amount[k] > Fx::zero()) sum += r.amount[k];
  return sum;
}

}  // namespace

Fx woundRoom(const Creature& c) {
  const Fx now = c.chemistry().chem[chem::injury.v];
  return fxMax(Fx::zero(), lethalInjury(c) - dropInjury(c) - kWoundMargin - now);
}

TurnWound woundFor(Stakes s, const TurnHit* hits, uint8_t turn, Fx roomNow) {
  const StakesRule& r = rule(s);
  Fx before{};
  uint8_t scarred = 0;
  for (uint8_t t = 0; t < turn; ++t) {
    before += r.injuryPerHeart * Fx::ratio(hits[t].hearts, 1);
    if (hits[t].hearts > 0 && scarred < 2) ++scarred;
  }
  const TurnHit& hit = hits[turn];
  const Fx capLeft = fxMax(Fx::zero(), r.injuryCapPerDuel - before);
  TurnWound w{};
  w.injury = fxMin(fxMin(r.injuryPerHeart * Fx::ratio(hit.hearts, 1), capLeft), fxMax(roomNow, Fx::zero()));
  w.scar = r.scars && hit.hearts > 0 && scarred < 2;
  w.mark = Scar{hit.kind, uint8_t(hit.luck), uint8_t(hit.luck >> 8), 255};
  return w;
}

// woundFor never adds more than the tier's cap in one duel, so the cap is
// "every heart lost" without knowing how many hearts he will have (duel.h).
Danger forecast(const Creature& c, Stakes s) {
  const Fx now = c.chemistry().chem[chem::injury.v];
  const Fx room = woundRoom(c);
  const Fx added = fxMin(rule(s).injuryCapPerDuel, room);
  const Fx ceiling = now + room;
  Danger d{now + added + dropInjury(c), 255};
  if (ceiling > Fx::zero())
    d.skull = uint8_t(std::min<int64_t>(255, int64_t((now + added).raw) * 255 / ceiling.raw));
  return d;
}

// The slayer feat is awarded at death from duels.log (the Dish ORs the book's
// feats into the Death entry), so no LifeStats ever earns it.
bool feat_slayer(const LifeStats&, uint16_t) { return false; }

}  // namespace blorb
