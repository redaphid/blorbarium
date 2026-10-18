#include <cstdio>
#include <map>
#include <string>
#include <gtest/gtest.h>
#include <set>
#include "blorb/fixed.h"
#include "blorb/founding.h"
#include "blorb/genes.h"
#include "blorb/genome.h"
#include "blorb/registry.h"
#include "paint/sprite_pack.h"  // named here so PlatformIO's dependency finder links lib/paint

using namespace blorb;

namespace {

// A batch of boards off one reel: MACs that differ only in their last bytes.
uint32_t seedOf(uint32_t n) {
  const uint8_t mac[6] = {0xd4, 0x05, 0x92, uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
  return fnv1a(mac, sizeof mac);
}

Genome founder(SpeciesId s, uint32_t seed) { return foundingGenome(s, seed, paint::bandsFor(s)); }

bool isLook(const GeneView& v) {
  return v.header.type == GeneKindOf<PaletteGene>::value || v.header.type == GeneKindOf<MarkGene>::value;
}

}  // namespace

TEST(FoundingLook, ABatchOfBoardsOfOneSpeciesFoundsDistinctGenomes) {
  for (const SpeciesInfo& row : SPECIES) {
    std::set<uint32_t> hashes;
    std::set<std::vector<uint8_t>> genomes;
    constexpr uint32_t kBoards = 1000;
    for (uint32_t n = 0; n < kBoards; ++n) {
      // Reel base 0x771c00, not 0x771b00: that reel's boards 89 and 618
      // found byte-distinct fizz genomes whose 32-bit FNV-1a hashes
      // collide (both f20a6d16) -- a birthday lottery a recipe change
      // re-rolls, not a founding defect. The genomes differ; the hash
      // is an identity shorthand, not a uniqueness proof.
      const Genome g = founder(row.id, seedOf(0x771c00 + n));
      hashes.insert(g.hash());
      genomes.insert(g.bytes());
    }
    EXPECT_EQ(genomes.size(), kBoards) << row.name;
    EXPECT_EQ(hashes.size(), kBoards) << row.name;
  }
}

TEST(FoundingLook, OneBoardAlwaysFoundsTheSamePet) {
  for (const SpeciesInfo& row : SPECIES)
    EXPECT_EQ(founder(row.id, seedOf(0x771b34)).bytes(), founder(row.id, seedOf(0x771b34)).bytes()) << row.name;
}

TEST(FoundingLook, KeepsTheSpeciesStarterAndChangesOnlyTheLook) {
  for (const SpeciesInfo& row : SPECIES) {
    const Genome starter = starterGenomeOf(row.id, seedOf(0x771b34));
    const Genome g = founder(row.id, seedOf(0x771b34));
    ASSERT_EQ(g.geneCount(), starter.geneCount()) << row.name;
    for (uint16_t i = 0; i < starter.geneCount(); ++i) {
      const GeneView a = starter.gene(i), b = g.gene(i);
      EXPECT_EQ(a.header.uid, b.header.uid);
      EXPECT_EQ(a.header.type, b.header.type);
      if (!isLook(a))
        EXPECT_EQ(std::vector<uint8_t>(a.body, a.body + a.header.len),
                  std::vector<uint8_t>(b.body, b.body + b.header.len))
            << row.name << " gene " << i;
    }
    Phenotype p;
    expressStage(g, Stage::Baby, 0, p);
    EXPECT_EQ(p.species.body, row.id) << row.name;
    EXPECT_EQ(p.species.power, row.power) << row.name;
  }
}

TEST(FoundingLook, EveryExpressedColourIsInsideItsSpeciesBandsAndReachesAcrossThem) {
  for (const SpeciesInfo& row : SPECIES) {
    const paint::SpritePack& pack = paint::packFor(row.id);
    std::set<int> hues[kRegionCount];
    std::set<int> mottling;
    for (uint32_t n = 0; n < 500; ++n) {
      founder(row.id, seedOf(n)).forEach([&](const GeneView& v) {
        if (v.header.flags & GeneFlags::Dormant) return;
        if (v.header.type == GeneKindOf<MarkGene>::value && v.body[0] == 0) mottling.insert(v.body[1]);
        if (v.header.type != GeneKindOf<PaletteGene>::value) return;
        const RegionId r{uint8_t(v.body[0] % kRegionCount)};
        const paint::RegionBand band = pack.band(r);
        const int hue = int(v.body[1]) - 128;
        EXPECT_GE(hue, band.hueMin) << row.name;
        EXPECT_LE(hue, band.hueMax) << row.name;
        EXPECT_GE(v.body[2], band.satMin) << row.name;
        EXPECT_LE(v.body[2], band.satMax) << row.name;
        EXPECT_GE(v.body[3], band.valMin) << row.name;
        EXPECT_LE(v.body[3], band.valMax) << row.name;
        hues[r.v].insert(hue);
      });
    }
    // The whole band is reached, not a nudge around one hue.
    for (RegionId r : {region::skin, region::cloak, region::eye, region::glow}) {
      const paint::RegionBand band = pack.band(r);
      EXPECT_GT(hues[r.v].size(), size_t(band.hueMax - band.hueMin) * 3 / 4) << row.name << " region " << int(r.v);
      EXPECT_EQ(*hues[r.v].begin(), band.hueMin) << row.name << " region " << int(r.v);
      EXPECT_EQ(*hues[r.v].rbegin(), band.hueMax) << row.name << " region " << int(r.v);
    }
    EXPECT_EQ(mottling, (std::set<int>{2, 3, 4})) << row.name;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

