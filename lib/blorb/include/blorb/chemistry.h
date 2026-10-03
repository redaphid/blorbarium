#pragma once
// Chemistry: 256 anonymous chemical slots, 256 loci, and the gene-built rules
// that connect them. The engine knows what a drive is (ids.h) and nothing else.
//
// Order inside step(), fixed so replays agree:
//   emitters (locus -> chem); reactions; decay; act loci cleared; receptors (chem -> locus)
// Senses write sense loci before step(); code reads act loci after it.
#include <cstdint>
#include <vector>
#include "blorb/fixed.h"
#include "blorb/ids.h"

namespace blorb {

struct Reaction { ChemId a, b, c, d; uint8_t qa, qb, qc, qd; Fx rate; };
struct Emitter  { LocusId locus; ChemId chem; Fx threshold, gain; bool digital, invert, clear; uint8_t period; };
struct Receptor { ChemId chem; LocusId locus; Fx threshold, nominal, gain; bool digital, invert; };

// Built by gene expression; appended at each stage, never shrunk in a life.
struct ChemRules {
  Decay decay[kChemSlots];       // default: never decays
  std::vector<Reaction> reactions;
  std::vector<Emitter> emitters;
  std::vector<Receptor> receptors;
};

class Chemistry {
 public:
  Fx chem[kChemSlots]{};
  Fx locus[kLocusSlots]{};

  void add(ChemId, Fx delta);    // clamped to [0, 1]
  void set(LocusId, Fx);         // clamped to [0, 1]
  Fx drive(DriveId d) const { return chem[driveChem(d).v]; }

  // One body tick. Receptor outputs on one locus sum, then clamp. Act loci
  // are cleared before receptors write, so an effect stops when its receptor
  // stops firing (derived, never synced).
  void step(const ChemRules&, uint32_t tick);

  // The same rules at a coarse cadence (one step per `stride` ticks, decay in
  // closed form) for viability()'s dry run. Never used for the live creature.
  void stepCoarse(const ChemRules&, uint32_t stride, uint32_t tick);

  uint32_t hash() const;
};

}  // namespace blorb
