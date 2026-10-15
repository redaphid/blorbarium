#include <gtest/gtest.h>
#include <set>
#include "blorb/fixed.h"
#include "blorb/genes.h"
#include "blorb/genome.h"
#include "blorb/registry.h"
#include "paint/sprite_pack.h"  // named here so PlatformIO's dependency finder links lib/paint
#include "grungo_pack.h"

using namespace blorb;

namespace {

// A batch of boards off one reel: MACs that differ only in their last bytes.
uint32_t seedOf(uint32_t n) {
  const uint8_t mac[6] = {0xd4, 0x05, 0x92, uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
  return fnv1a(mac, sizeof mac);
}

}  // namespace

TEST(Founding, ABatchOfBoardsFoundsDistinctGenomes) {
  std::set<uint32_t> hashes;
  std::set<std::vector<uint8_t>> genomes;
  constexpr uint32_t kBoards = 5000;
  for (uint32_t n = 0; n < kBoards; ++n) {
    const Genome g = foundingGenome(seedOf(0x771b00 + n));
    hashes.insert(g.hash());
    genomes.insert(g.bytes());
  }
  EXPECT_EQ(genomes.size(), kBoards);
  EXPECT_EQ(hashes.size(), kBoards);
}

TEST(Founding, OneBoardAlwaysFoundsTheSamePet) {
  EXPECT_EQ(foundingGenome(seedOf(0x771b34)).bytes(), foundingGenome(seedOf(0x771b34)).bytes());
}

TEST(Founding, KeepsTheStarterGenesAndChangesOnlyTheLook) {
  const Genome starter = starterGenome(seedOf(0x771b34));
  const Genome founder = foundingGenome(seedOf(0x771b34));
  ASSERT_EQ(founder.geneCount(), starter.geneCount());
  for (uint16_t i = 0; i < starter.geneCount(); ++i) {
    const GeneView a = starter.gene(i), b = founder.gene(i);
    EXPECT_EQ(a.header.uid, b.header.uid);
    EXPECT_EQ(a.header.type, b.header.type);
    const bool look = a.header.type == GeneKindOf<PaletteGene>::value || a.header.type == GeneKindOf<MarkGene>::value;
    if (!look) EXPECT_EQ(std::vector<uint8_t>(a.body, a.body + a.header.len),
                         std::vector<uint8_t>(b.body, b.body + b.header.len));
  }
}

TEST(Founding, EveryExpressedColourIsInsideGrungosBands) {
  const paint::SpritePack& pack = grungoPack();
  std::set<int> skinHues;
  for (uint32_t n = 0; n < 500; ++n) {
    foundingGenome(seedOf(n)).forEach([&](const GeneView& v) {
      if (v.header.type != GeneKindOf<PaletteGene>::value || (v.header.flags & GeneFlags::Dormant)) return;
      const RegionId r{uint8_t(v.body[0] % kRegionCount)};
      const paint::RegionBand band = pack.band(r);
      const int hue = int(v.body[1]) - 128;
      EXPECT_GE(hue, band.hueMin);
      EXPECT_LE(hue, band.hueMax);
      EXPECT_GE(v.body[2], band.satMin);
      EXPECT_LE(v.body[2], band.satMax);
      EXPECT_GE(v.body[3], band.valMin);
      EXPECT_LE(v.body[3], band.valMax);
      if (r.v == region::skin.v) skinHues.insert(hue);
    });
  }
  EXPECT_GT(skinHues.size(), 50u);   // the whole skin band is reached, not a nudge around one hue
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
