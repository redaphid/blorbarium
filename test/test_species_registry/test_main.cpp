#include <gtest/gtest.h>
#include <cstring>
#include <set>
#include "blorb/species.h"

using namespace blorb;

// Two badges duel only if their beacons carry the same hash. This number is
// the wire contract: a change to species.def's wheel columns or powers.def's
// names and costs moves it, and that must be a decision, not an accident.
// tools/species_neg_check.sh checks the ESP32-S3 compiler agrees with the host.
TEST(RosterHash, IsPinned) {
  EXPECT_EQ(rosterHash(), 47322u);   // lantern 4 to 3 for the queued special (unit D4)
}

// Written out by hand from roster/roster.json (origin/roster-art d98055e), the
// roster's id order and each creature's power. roster.json's ids are stable and
// the battle code keys on them, so a renumbered or swapped row shows up here.
TEST(Roster, RowsAreRosterJsonInIdOrder) {
  const char* const slugs[] = {"grungo", "mossby", "puffkin", "gilly", "mothwick",
                               "mudge", "fizz", "hushwing", "tinker"};
  const char* const powers[] = {"foresee", "shell", "spores", "mend", "lantern",
                                "dash", "zap", "hush", "gadget"};
  ASSERT_EQ(kSpeciesCount, countOf(slugs));
  ASSERT_EQ(kPowerCount, countOf(powers));
  for (size_t i = 0; i < kSpeciesCount; ++i) {
    EXPECT_STREQ(SPECIES[i].name, slugs[i]) << "species " << i;
    EXPECT_STREQ(powerInfo(SPECIES[i].power).name, powers[i]) << SPECIES[i].name;
  }
}

TEST(Roster, AnUnnamedByteReadsAsGrungo) {
  EXPECT_STREQ(speciesInfo(SpeciesId{9}).name, "grungo");
  EXPECT_STREQ(speciesInfo(SpeciesId{255}).name, "grungo");
  EXPECT_STREQ(powerInfo(PowerId{200}).name, "foresee");
}

TEST(Wheel, EveryPairHasExactlyOneWinnerOrIsEven) {
  for (uint8_t a = 0; a < kCycle; ++a)
    for (uint8_t b = 0; b < kCycle; ++b) {
      const int wins = int(beats(Stance(a), Stance(b))) + int(beats(Stance(b), Stance(a)));
      EXPECT_EQ(wins, a == b ? 0 : 1) << int(a) << " vs " << int(b);
      EXPECT_EQ(beats(Element(a), Element(b)), beats(Stance(a), Stance(b)));
    }
  for (uint8_t s = 0; s < kCycle; ++s) EXPECT_TRUE(beats(counterOf(Stance(s)), Stance(s)));
}

TEST(Wheel, EverySpeciesHasThreeGoodThreeBadThreeEvenMatchups) {
  for (const SpeciesInfo& me : SPECIES) {
    int good = 0, bad = 0, even = 0;
    for (const SpeciesInfo& them : SPECIES) {
      if (beats(me.element, them.element)) ++good;
      else if (beats(them.element, me.element)) ++bad;
      else ++even;
    }
    EXPECT_EQ(good, 3) << me.name;
    EXPECT_EQ(bad, 3) << me.name;
    EXPECT_EQ(even, 3) << me.name;
  }
}

TEST(FoundingNest, OneEggPerElementAndEverySpeciesCanBeOffered) {
  std::set<uint8_t> offered;
  for (uint32_t splash = 0; splash < 256; splash += 85)
    for (uint32_t sprout = 0; sprout < 256; sprout += 85)
      for (uint32_t spark = 0; spark < 256; spark += 85) {
        const uint32_t seed = splash | (sprout << 8) | (spark << 16) | 0xA5000000u;
        const FoundingNest n = foundingNest(seed);
        for (uint8_t e = 0; e < kCycle; ++e) {
          EXPECT_EQ(uint8_t(speciesInfo(n.eggs[e]).element), e) << "seed " << seed;
          offered.insert(n.eggs[e].v);
        }
        const FoundingNest again = foundingNest(seed);
        EXPECT_EQ(0, std::memcmp(&n, &again, sizeof n)) << "a nest replays from its seed";
      }
  EXPECT_EQ(offered.size(), kSpeciesCount);
}

TEST(SpeciesTraits, AHybridCarriesAPowerItsBodyDoesNot) {
  EXPECT_FALSE(SpeciesTraits{}.hybrid());
  EXPECT_FALSE((SpeciesTraits{SpeciesId{6}, power::zap, 0}.hybrid()));
  EXPECT_TRUE((SpeciesTraits{kGrungo, power::zap, 0}.hybrid()));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
