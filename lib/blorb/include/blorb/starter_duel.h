#pragma once
// The duel feeling genes every starter carries (BATTLES.md, "What the pets
// feel"): how a duel stirs him, never what it does to him. Wounds, scars and a
// fall are the engine's (stakes.h); these only feel, behind duelFilter.
//
// Each gene sits at a fixed uid past every recipe uid and clear of the species
// genes (species.h), so a friend egg's cross lines them up on every device.
// starterGenomeOf (unit D3) appends them all; the one-time graft for a lineage
// founded before duels (unit D9) appends the missing ones at nextUid() instead,
// because a grafted gene never crosses.
#include <cstddef>
#include <cstdint>
#include "blorb/genome.h"

namespace blorb {

struct DuelGene {
  GeneHeader header;   // header.uid is the fixed uid a starter carries it at
  uint8_t body[8];     // header.len bytes
};
extern const DuelGene kDuelGenes[];
extern const size_t kDuelGeneCount;
inline constexpr GeneUid kFirstDuelGeneUid{0x0F10};

GenomeBuilder& appendDuelGenes(GenomeBuilder&);

}  // namespace blorb
