#pragma once
// What firmware 0.4.0 (main aee2f23) founded on a fresh badge: grungo's
// starter with the look rolled across the grungo pack's bands, and no
// SpeciesGene. The eight badges flashed on 2026-10-15 hold these pets, so the
// tests that migrate them (test_adopt_species) start from this, frozen here
// byte for byte; the firmware no longer founds it.
#include <cstdint>
#include "blorb/fixed.h"
#include "blorb/genes.h"
#include "blorb/genome.h"
#include "blorb/registry.h"

namespace blorbtest {

inline blorb::Genome foundingGenome040(uint32_t speciesSeed) {
  using namespace blorb;
  // Byte ranges inside the grungo pack's bands (hue byte = 128 + band hue), so
  // he is always a grungo: skin from ochre olive to blue-green, cloak from rust
  // to moss, eyes from red-brown to gold.
  struct Roll { uint8_t region, hue0, hue1, sat0, sat1, val0, val1; };
  constexpr Roll kRolls[] = {
      {region::skin.v, 104, 168, 104, 168, 108, 148},
      {region::belly.v, 124, 132, 112, 144, 120, 144},
      {region::cloak.v, 112, 164, 96, 176, 96, 160},
      {region::eye.v, 120, 158, 112, 160, 112, 176},
  };
  const Genome starter = starterGenome(speciesSeed);
  Rng rng = Rng::seeded(uint64_t(speciesSeed) << 32 | 0x666f756eu);
  auto pick = [&](uint8_t lo, uint8_t hi) { return uint8_t(lo + rng.below(uint32_t(hi - lo) + 1u)); };
  GenomeBuilder b = GenomeBuilder::from(starter);
  starter.forEach([&](const GeneView& v) {
    if (v.header.flags & GeneFlags::Dormant) return;
    if (v.header.type == GeneKindOf<PaletteGene>::value) {
      for (const Roll& r : kRolls)
        if (v.body[0] % kRegionCount == r.region)
          b.setByte(v.header.uid, 1, pick(r.hue0, r.hue1))
              .setByte(v.header.uid, 2, pick(r.sat0, r.sat1))
              .setByte(v.header.uid, 3, pick(r.val0, r.val1));
    } else if (v.header.type == GeneKindOf<MarkGene>::value && v.body[0] == 0) {
      b.setByte(v.header.uid, 1, pick(2, 4));   // mottling density
    }
  });
  return *b.build();
}

}  // namespace blorbtest
