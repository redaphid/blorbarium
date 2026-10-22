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

// 0.4.0's recipe numbers where today's starter differs: sleepiness made
// around the clock (its chem's half-life and initial level, and the emitter's
// locus, gain, fill and flags), a fuller first energy, a dimmer happy face
// and a 30-minute egg. One body byte each, the same for every seed; read off
// 0.4.0's starter before the recipe changed.
struct Byte040 { uint16_t uid; uint8_t at, value; };
inline constexpr Byte040 kRecipe040[] = {
    {9, 1, 0x9b},  {9, 2, 0x00},  {10, 0, 0x00}, {10, 2, 0x00}, {10, 3, 0xc9},
    {10, 4, 0x01}, {24, 2, 0xcc}, {82, 1, 0x8c}, {105, 0, 0x1e},
};

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
  GenomeBuilder b0 = GenomeBuilder::from(starterGenome(speciesSeed));
  for (const Byte040& r : kRecipe040) b0.setByte(GeneUid{r.uid}, r.at, r.value);
  // 0.4.0's recipe still carried the marble: a stimulus gene at uid 63 (a
  // marble hit eases boredom) and the chase instinct at uid 77. The current
  // recipe retired both -- its lines stand empty there (skip) so every other
  // uid is unchanged -- but a field badge's founder has them, byte for byte
  // as 0.4.0 wrote them. They are inert in this firmware: no def row names
  // stimulus 19, locus 16 or action 9 any more. Headers and bodies are the
  // old starter's own, read off 0.4.0's starter before the cut.
  {
    constexpr uint8_t kStimulusBody[] = {0x13, 0x00, 0x03, 0x00, 0x00, 0x5a, 0x80, 0x80};
    constexpr uint8_t kInstinctBody[] = {0x10, 0xff, 0xff, 0x09, 0x02, 0x33, 0x8c};
    b0.insertAfter(GeneUid{62}, GeneHeader{GeneKindOf<StimulusGene>::value, uint8_t(sizeof(StimulusGene)),
                                           GeneFlags::Mutable | GeneFlags::Dupable | GeneFlags::Delable, Stage::Baby,
                                           0, 64, GeneUid{63}},
                   kStimulusBody);
    b0.insertAfter(GeneUid{76}, GeneHeader{GeneKindOf<InstinctGene>::value, uint8_t(sizeof(InstinctGene)),
                                           GeneFlags::Mutable | GeneFlags::Dupable | GeneFlags::Delable, Stage::Child,
                                           0, 64, GeneUid{77}},
                   kInstinctBody);
  }
  const Genome starterWithMarble = *b0.build();
  // 0.4.0's hop_circles was action 10; the current table renumbered it to 9
  // when chase retired (action ids stay dense). The founder's hop instinct
  // (uid 79) still names 10, so the byte goes back after the build below --
  // see the setByte with the palette rolls.
  Rng rng = Rng::seeded(uint64_t(speciesSeed) << 32 | 0x666f756eu);
  auto pick = [&](uint8_t lo, uint8_t hi) { return uint8_t(lo + rng.below(uint32_t(hi - lo) + 1u)); };
  GenomeBuilder b = GenomeBuilder::from(starterWithMarble);
  b.setByte(GeneUid{79}, 3, 10);   // uid 79's action byte: hop_circles as 0.4.0 numbered it
  starterWithMarble.forEach([&](const GeneView& v) {
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
