#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include "blorb/appearance.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "paint/sprite_pack.h"
#include "grungo_pack.h"

using namespace blorb;

// What the user asked of heredity, seen through grungo at the dish's scale:
// siblings look different from each other and from their parent.
namespace {

constexpr int kSide = 240;
constexpr int kChannelTol = 24;    // tests/film.py's: a pixel has visibly moved past this
constexpr int kVisiblePx = 400;    // pixels that must move for two looks to read as different
constexpr int kParents = 16;
constexpr uint32_t kWanderers = 200;

std::unique_ptr<paint::Canvas240> render(const Appearance& a) {
  auto cv = std::make_unique<paint::Canvas240>();
  paint::draw(a, grungoPack(), *cv);
  return cv;
}

int moved(const paint::Canvas240& a, const paint::Canvas240& b) {
  auto ch = [](uint16_t p, int c) {
    return c == 0 ? ((p >> 11) & 31) * 255 / 31 : c == 1 ? ((p >> 5) & 63) * 255 / 63 : (p & 31) * 255 / 31;
  };
  int n = 0;
  for (int i = 0; i < kSide * kSide; ++i) {
    int worst = 0;
    for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(ch(a.px[i], c) - ch(b.px[i], c)));
    n += worst > kChannelTol;
  }
  return n;
}

Clutch clutchOf(const Genome& parent, uint32_t parentSeed) {
  Clutch k;
  k.parent = parent;
  k.generation = 1;
  k.count = kMaxClutch;
  Rng rng = Rng::seeded(parentSeed * 7919u + 1);
  for (uint64_t& s : k.seeds) s = uint64_t(rng.next()) << 32 | rng.next();
  for (int i = 0; i < kMaxClutch; ++i) k.derivePreviewStep();
  return k;
}

// One egg alone in the middle of the dish, as the clutch draws it, not chosen.
std::unique_ptr<paint::Canvas240> egg(const EggPreview& e) {
  Appearance a;
  a.kind = Appearance::Kind::Clutch;
  a.eggs[0] = e;
  a.eggCount = 1;
  a.cursor = 1;
  return render(a);
}

// A parent a clutch could come from: the starter some generations on, or the
// starter with palette bytes set anywhere, as a phone edit may leave them.
Genome wanderer(uint32_t seed) {
  Rng rng = Rng::seeded(seed * 2654435761u);
  Genome g = starterGenome(rng.next());
  if (seed % 2) {
    GenomeBuilder b = GenomeBuilder::from(g);
    g.forEach([&](const GeneView& v) {
      if (v.header.type != GeneKindOf<PaletteGene>::value) return;
      for (uint8_t at = 1; at <= 3; ++at) if (rng.below(2)) b.setByte(v.header.uid, at, uint8_t(rng.below(256)));
    });
    return *b.build();
  }
  for (uint32_t gen = 1 + rng.below(12); gen > 0; --gen) {
    MutationPolicy pol = policyOf(g, Fx::zero());
    pol.lookSlot = uint8_t(rng.below(kMaxClutch));
    g = mutate(g, pol, {}, rng).genome;
  }
  return g;
}

}  // namespace

TEST(Siblings, EveryPairOfEggsInAClutchLooksDifferent) {
  int least = kSide * kSide;
  for (uint32_t parent = 1; parent <= kParents; ++parent) {
    Clutch k = clutchOf(starterGenome(parent), parent);
    ASSERT_EQ(k.previewed, kMaxClutch);
    for (int i = 0; i < kMaxClutch; ++i)
      for (int j = i + 1; j < kMaxClutch; ++j) {
        int n = moved(*egg(k.previews[i]), *egg(k.previews[j]));
        least = std::min(least, n);
        EXPECT_GE(n, kVisiblePx) << "parent " << parent << ": eggs " << i << " and " << j;
      }
  }
  std::printf("fewest pixels between two sibling eggs: %d\n", least);
}

TEST(Siblings, EveryChildLooksDifferentFromItsParent) {
  int least = kSide * kSide;
  for (uint32_t parent = 1; parent <= kParents; ++parent) {
    Clutch k = clutchOf(starterGenome(parent), parent);
    auto was = render(portrait(k.parent, Stage::Baby, 0, 0));
    for (uint8_t i = 0; i < kMaxClutch; ++i) {
      int n = moved(*was, *render(portrait(k.child(i).genome, Stage::Baby, 1, 0)));
      least = std::min(least, n);
      EXPECT_GE(n, kVisiblePx) << "parent " << parent << ": child " << int(i);
    }
  }
  std::printf("fewest pixels between a hatchling and its parent: %d\n", least);
}

TEST(Siblings, EveryChildOfAnyParentLooksDifferentFromIt) {
  int least = kSide * kSide;
  for (uint32_t seed = 1; seed <= kWanderers; ++seed) {
    Clutch k = clutchOf(wanderer(seed), seed);
    auto was = render(portrait(k.parent, Stage::Baby, 0, 0));
    for (uint8_t i = 0; i < kMaxClutch; ++i) {
      int n = moved(*was, *render(portrait(k.child(i).genome, Stage::Baby, 1, 0)));
      least = std::min(least, n);
      EXPECT_GE(n, kVisiblePx) << "wanderer " << seed << ": child " << int(i);
    }
  }
  std::printf("fewest pixels between any parent's hatchling and its child: %d\n", least);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
