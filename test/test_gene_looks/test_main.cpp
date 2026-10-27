// A family looks like a family (gene-looks): a child's look (colour, size,
// build, spots) differs from his parent's, stays inside his species' ranges,
// is nearer his parent than a stranger, drifts down a line, blends in a cross,
// and shows on the glass.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#include "blorb/appearance.h"
#include "blorb/creature.h"
#include "blorb/founding.h"
#include "blorb/friend_egg.h"
#include "blorb/genes.h"
#include "blorb/look_genes.h"
#include "blorb/mutate.h"
#include "paint/sprite_pack.h"

using namespace blorb;

namespace {

uint32_t seedOf(uint32_t n) {
  const uint8_t mac[6] = {0xd4, 0x05, 0x92, uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
  return fnv1a(mac, sizeof mac);
}
Genome founder(SpeciesId s, uint32_t n) { return foundingGenome(s, seedOf(n), paint::bandsFor(s)); }

// The real egg path: hatched, a clutch of three laid, each egg Clutch::child.
std::vector<Genome> clutchOf(const Genome& parent, uint16_t generation) {
  Egg egg(Offspring{parent, {}}, generation, 0);
  auto c = std::make_unique<Creature>(egg, 0, 0);
  Clutch k = c->layClutch(3, Fx::zero(), 0);
  std::vector<Genome> out;
  for (uint8_t i = 0; i < k.count; ++i) out.push_back(k.child(i).genome);
  return out;
}

// What the renderer is handed for his look, as an adult portrait.
struct Look {
  Tint regions[4];   // skin, belly, cloak, eye
  int scale, build, spots, spotHue;
};
Look lookOf(const Genome& g) {
  const Appearance a = portrait(g, Stage::Adult, 1, 0);
  Look l{};
  // Each tint as paint draws it: clamped to the pack's band (a woken
  // dormant gene or a wild point mutation can carry a byte past it).
  for (int r = 0; r < 4; ++r) {
    const RegionBand& b = kLookBands[r];
    l.regions[r] = Tint{int8_t(std::clamp<int>(a.regions[r].hue, b.hueMin, b.hueMax)),
                        uint8_t(std::clamp<int>(a.regions[r].sat, b.satMin, b.satMax)),
                        uint8_t(std::clamp<int>(a.regions[r].val, b.valMin, b.valMax))};
  }
  l.scale = a.scalePct;
  l.build = a.build;
  for (int m = 0; m < a.markCount; ++m)
    if (a.marks[m].layer == 0) l.spots = a.marks[m].variant, l.spotHue = a.marks[m].tint.hue;
  return l;
}
// A look distance in gene steps: every visible byte counts its difference.
double distance(const Look& a, const Look& b) {
  double d = 0;
  for (int r = 0; r < 4; ++r)
    d += std::abs(a.regions[r].hue - b.regions[r].hue) + std::abs(a.regions[r].sat - b.regions[r].sat) +
         std::abs(a.regions[r].val - b.regions[r].val);
  return d + 2 * std::abs(a.scale - b.scale) + std::abs(a.build - b.build) + std::abs(a.spots - b.spots) +
         std::abs(a.spotHue - b.spotHue);
}

const SpeciesId kSpecies[] = {SpeciesId{0}, SpeciesId{2}, SpeciesId{7}, SpeciesId{9}};   // grungo, puffkin, hushwing, octopus

}  // namespace

TEST(GeneLooks, EveryPackCarriesTheBandsInheritanceKeepsTo) {
  for (const SpeciesInfo& s : SPECIES) {
    const LookBands bands = paint::bandsFor(s.id);
    for (size_t r = 0; r < kRegionCount; ++r) {
      EXPECT_EQ(bands[r].hueMin, kLookBands[r].hueMin) << s.name << " region " << r;
      EXPECT_EQ(bands[r].hueMax, kLookBands[r].hueMax) << s.name << " region " << r;
      EXPECT_EQ(bands[r].satMin, kLookBands[r].satMin) << s.name << " region " << r;
      EXPECT_EQ(bands[r].satMax, kLookBands[r].satMax) << s.name << " region " << r;
      EXPECT_EQ(bands[r].valMin, kLookBands[r].valMin) << s.name << " region " << r;
      EXPECT_EQ(bands[r].valMax, kLookBands[r].valMax) << s.name << " region " << r;
    }
  }
}

TEST(GeneLooks, FoundersDifferInSizeBuildAndSpots) {
  for (SpeciesId s : kSpecies) {
    int smallest = 255, biggest = 0, leanest = 255, stockiest = 0, fewest = 255, most = 0;
    for (uint32_t n = 0; n < 40; ++n) {
      const Look l = lookOf(founder(s, 0x771c00 + n));
      smallest = std::min(smallest, l.scale), biggest = std::max(biggest, l.scale);
      leanest = std::min(leanest, l.build), stockiest = std::max(stockiest, l.build);
      fewest = std::min(fewest, l.spots), most = std::max(most, l.spots);
    }
    EXPECT_GE(biggest - smallest, 10) << "size, percent, species " << int(s.v);
    EXPECT_GE(stockiest - leanest, 30) << "build byte, species " << int(s.v);
    EXPECT_GE(most - fewest, 30) << "spots, species " << int(s.v);
  }
}

TEST(GeneLooks, EveryChildDiffersFromHisParentInsideHisSpeciesRanges) {
  for (SpeciesId s : kSpecies)
    for (uint32_t n = 0; n < 6; ++n) {
      Genome parent = founder(s, 0x771c00 + n);
      for (uint16_t gen = 1; gen <= 6; ++gen) {
        const Look p = lookOf(parent);
        const std::vector<Genome> kids = clutchOf(parent, gen);
        for (const Genome& kid : kids) {
          const Look c = lookOf(kid);
          EXPECT_GE(distance(c, p), 24) << "a child must look different, species " << int(s.v) << " gen " << gen;
          EXPECT_GE(c.scale, 88);
          EXPECT_LE(c.scale, 112);
          EXPECT_GE(c.build, kBuildRange.lo);
          EXPECT_LE(c.build, kBuildRange.hi);
          EXPECT_GE(c.spots, kSpotsRange.lo);
          EXPECT_LE(c.spots, kSpotsRange.hi);
        }
        parent = kids[gen % kids.size()];
      }
    }
}

TEST(GeneLooks, AChildIsNearerHisParentThanAStrangerAndSiblingsAreKin) {
  for (SpeciesId s : kSpecies) {
    double toParent = 0, toStranger = 0, sibling = 0;
    int n = 0, pairs = 0;
    for (uint32_t i = 0; i < 24; ++i) {
      const Genome parent = founder(s, 0x771c00 + i), stranger = founder(s, 0x5a0000 + i);
      const std::vector<Genome> kids = clutchOf(parent, 1);
      for (size_t k = 0; k < kids.size(); ++k) {
        const Look c = lookOf(kids[k]);
        toParent += distance(c, lookOf(parent));
        toStranger += distance(c, lookOf(stranger));
        ++n;
        for (size_t j = k + 1; j < kids.size(); ++j) sibling += distance(c, lookOf(kids[j])), ++pairs;
      }
    }
    toParent /= n, toStranger /= n, sibling /= pairs;
    EXPECT_LT(toParent * 2, toStranger) << "inherited: species " << int(s.v) << " parent " << toParent << " stranger " << toStranger;
    EXPECT_LT(sibling, toStranger) << "kin: species " << int(s.v) << " siblings " << sibling << " stranger " << toStranger;
  }
}

TEST(GeneLooks, ALineDriftsFurtherFromItsFounderOverGenerations) {
  for (SpeciesId s : kSpecies) {
    double at1 = 0, at12 = 0;
    for (uint32_t i = 0; i < 8; ++i) {
      const Genome root = founder(s, 0x771c00 + i);
      Genome line = root;
      for (uint16_t gen = 1; gen <= 12; ++gen) {
        const std::vector<Genome> kids = clutchOf(line, gen);
        line = kids[(gen + i) % kids.size()];
        if (gen == 1) at1 += distance(lookOf(line), lookOf(root));
      }
      at12 += distance(lookOf(line), lookOf(root));
    }
    EXPECT_GT(at12, at1 * 1.5) << "species " << int(s.v) << " gen1 " << at1 / 8 << " gen12 " << at12 / 8;
  }
}

TEST(GeneLooks, ACrossBlendsTheParentsLooks) {
  constexpr uint8_t kPalette = GeneKindOf<PaletteGene>::value, kSize = GeneKindOf<SizeGene>::value,
                    kMark = GeneKindOf<MarkGene>::value;
  for (SpeciesId s : kSpecies) {
    const Genome a = founder(s, 0x771c00), b = founder(s, 0x22b0b);
    Rng rng = Rng::seeded(0xF00D);
    const std::optional<Genome> crossed = apply(a, crossDiff(a, b, rng));
    ASSERT_TRUE(crossed);
    int blended = 0;
    crossed->forEach([&](const GeneView& v) {
      if (v.header.type != kPalette && v.header.type != kSize && v.header.type != kMark) return;
      const GeneView x = *a.find(v.header.uid), y = *b.find(v.header.uid);
      for (uint8_t i = v.header.type == kSize ? 0 : 1; i < v.header.len; ++i) {
        EXPECT_EQ(v.body[i], (x.body[i] + y.body[i] + 1) / 2) << "species " << int(s.v) << " gene " << v.header.uid.v;
        blended += x.body[i] != y.body[i];
      }
    });
    EXPECT_GT(blended, 4) << "the two founders differ in several look bytes";
  }
}

TEST(GeneLooks, AChildsDifferenceShowsOnTheGlass) {
  auto a = std::make_unique<paint::Canvas240>(), b = std::make_unique<paint::Canvas240>();
  for (SpeciesId s : kSpecies)
    for (Stage stage : {Stage::Child, Stage::Adult}) {
      const Genome parent = founder(s, 0x771c00);
      Appearance pa = portrait(parent, stage, 0, 0);
      paint::draw(pa, paint::packFor(s), *a, 0);
      for (const Genome& kid : clutchOf(parent, 1)) {
        Appearance ka = portrait(kid, stage, 1, 0);
        ka.lifeSeed = pa.lifeSeed;   // the same blink and spot layout: only the genes differ
        paint::draw(ka, paint::packFor(s), *b, 0);
        int moved = 0, body = 0;
        for (int i = 0; i < 240 * 240; ++i) {
          const uint16_t p = a->px[i], q = b->px[i];
          const int d = std::abs(int(p >> 11) - int(q >> 11)) * 2 + std::abs(int((p >> 5) & 63) - int((q >> 5) & 63)) +
                        std::abs(int(p & 31) - int(q & 31)) * 2;
          body += p != q;
          moved += d >= 8;   // about 12% of a channel: a step a kid sees
        }
        EXPECT_GE(moved, 1500) << "species " << int(s.v) << " stage " << int(stage) << " changed " << body;
      }
    }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
