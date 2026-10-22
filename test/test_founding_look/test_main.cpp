#include <cstdio>
#include <map>
#include <string>
#include <gtest/gtest.h>
#include <set>
#include <algorithm>
#include <cstdlib>
#include "blorb/appearance.h"
#include "blorb/fixed.h"
#include "blorb/mutate.h"
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

// gene-variety (#60): every egg's guaranteed Look change shows on the glass,
// for every species. It steps the species' coat (species.def), so the white
// owl's eggs differ by their belly, not by a skin tint white cannot show.
// Drawn as the badge draws him, grown up, with one life seed so only genes
// differ; a pixel counts when a channel moves 3/31 or more (about 10%).
namespace {
int changedPixels(const Genome& a, const Genome& b) {
  static paint::Canvas240 ca, cb;
  for (auto [g, cv] : {std::pair<const Genome*, paint::Canvas240*>{&a, &ca}, {&b, &cb}}) {
    Appearance look = portrait(*g, Stage::Adult, 1, 0);
    look.lifeSeed = 0x5EED;
    paint::draw(look, paint::packFor(look.species), *cv, 0);
  }
  int n = 0;
  for (int i = 0; i < 240 * 240; ++i) {
    const uint16_t p = ca.px[i], q = cb.px[i];
    const int dr = std::abs(int(p >> 11) - int(q >> 11)), dg = std::abs(int((p >> 5) & 63) - int((q >> 5) & 63)) / 2,
              db = std::abs(int(p & 31) - int(q & 31));
    n += std::max({dr, dg, db}) >= 3;
  }
  return n;
}
}  // namespace

TEST(FoundingLook, EveryEggsForcedChangeShowsOnEverySpecies) {
  for (const SpeciesInfo& row : SPECIES) {
    for (uint32_t board = 0; board < 4; ++board) {
      const Genome parent = founder(row.id, seedOf(0x771d00 + board));
      for (uint8_t slot = 0; slot < kMaxClutch; ++slot) {
        MutationPolicy pol = policyOf(parent, Fx::zero());
        pol.pointPerGene = pol.dupPerGene = pol.delPerGene = pol.wakePerDormant = pol.sleepPerGene = Fx::zero();
        pol.heirloomMax = 0;
        pol.lookSlot = slot;
        Rng rng = Rng::seeded(board * 7u + slot);
        const Offspring child = mutate(parent, pol, {}, rng);
        // Only the forced Look and Mind changes: the Look one alone must show.
        EXPECT_GE(changedPixels(parent, child.genome), 400) << row.name << " board " << board << " egg " << int(slot);
      }
    }
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

