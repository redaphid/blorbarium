#include <gtest/gtest.h>
#include <variant>
#include "blorb/lineage.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "mem_storage.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr const char* kLog = "lineage.log";

Creature deadCreature(uint16_t generation = 0) {
  Creature c = Creature::hatch(Egg(Offspring{starterGenome(7), {}}, generation, 0), 0, 0);
  Habitat h;
  Behaviours b;
  c.inject(chem::life, Fx::ratio(5, 100));
  c.tick(SenseOut{}, h, b, 1);
  return c;
}

Clutch clutchOf(const Genome& parent, uint16_t generation, uint64_t seed) {
  Clutch k;
  k.parent = parent;
  k.generation = generation;
  k.count = 1;
  k.seeds[0] = seed;
  return k;
}

struct Census { int foundings = 0, births = 0, checkpoints = 0, deaths = 0; };
Census census(const Lineage& l) {
  Census c;
  l.forEach([&](const LineageEntry& e) {
    c.foundings += std::holds_alternative<Founding>(e);
    c.births += std::holds_alternative<Birth>(e);
    c.checkpoints += std::holds_alternative<Checkpoint>(e);
    c.deaths += std::holds_alternative<Death>(e);
  });
  return c;
}

}  // namespace

TEST(Feats, ClutchSizeComesFromFeats) {
  EXPECT_EQ(unlocksFor(0).clutchSize, 1);
  EXPECT_EQ(unlocksFor(feat::reached_adult).clutchSize, 2);
  EXPECT_EQ(unlocksFor(feat::reached_adult | feat::reached_elder).clutchSize, 3);
  EXPECT_EQ(unlocksFor(0xFFFFFFFFu).clutchSize, kMaxClutch);
  EXPECT_EQ(unlocksFor(0).wildBonus, Fx::zero());
  EXPECT_EQ(unlocksFor(feat::dreamer | feat::well_fed).wildBonus, Fx::ratio(2, 100) + Fx::ratio(2, 100));

  LifeStats s;
  s.reached = Stage::Elder;
  s.ageTicks = 8 * 24 * kTicksPerHour;
  s.fed = 40;
  EXPECT_EQ(featsOf(s, 5), feat::reached_adult | feat::reached_elder | feat::lived_a_week | feat::well_fed |
                               feat::fifth_generation);
  s.fed = 20;
  EXPECT_EQ(featsOf(s, 0) & feat::well_fed, 0u) << "20 meals in 8 days is not well fed";
}

TEST(Lineage, BirthAndDeathRecordedTwiceCountOnce) {
  MemStorage store;
  Genome starter = starterGenome(7);
  Lineage l = Lineage::open(store, starter, 9, 0);
  Creature dead = deadCreature();
  ASSERT_TRUE(dead.dead());
  l.recordDeath(dead, "Grungo", 10);
  l.recordDeath(dead, "Grungo", 11);
  Clutch k = clutchOf(starter, 1, 5);
  Egg egg(k.child(0), 1, 12);
  l.recordBirth(egg, k, 0, 12);
  l.recordBirth(egg, k, 0, 13);
  Census c = census(l);
  EXPECT_EQ(c.foundings, 1);
  EXPECT_EQ(c.deaths, 1);
  EXPECT_EQ(c.births, 1);
  uint32_t feats = l.legacyFeats();

  Lineage again = Lineage::open(store, starter, 9, 99);   // a reboot after a crash before the snapshot
  again.recordDeath(dead, "Grungo", 20);
  again.recordBirth(egg, k, 0, 21);
  c = census(again);
  EXPECT_EQ(c.foundings, 1);
  EXPECT_EQ(c.deaths, 1);
  EXPECT_EQ(c.births, 1);
  EXPECT_EQ(again.currentGeneration(), 1);
  EXPECT_EQ(again.legacyFeats(), feats);
}

TEST(Lineage, FoundingPlusFortyBirthsRebuildEveryGeneration) {
  MemStorage store;
  Genome g = starterGenome(7);
  std::vector<uint32_t> hashes = {g.hash()};
  {
    Lineage l = Lineage::open(store, g, 3, 0);
    for (uint16_t gen = 1; gen <= 40; ++gen) {
      Clutch k = clutchOf(g, gen, 1000 + gen);
      Egg egg(k.child(0), gen, gen);
      l.recordBirth(egg, k, 0, gen);
      g = egg.genome();
      hashes.push_back(g.hash());
    }
  }
  Lineage l = Lineage::open(store, starterGenome(99), 3, 0);
  EXPECT_EQ(l.currentGeneration(), 40);
  EXPECT_EQ(census(l).checkpoints, 5) << "generations 8, 16, 24, 32, 40";
  for (uint16_t gen = 0; gen <= 40; ++gen) {
    std::optional<Genome> rebuilt = l.genomeOf(gen);
    ASSERT_TRUE(rebuilt.has_value()) << "generation " << gen;
    EXPECT_EQ(rebuilt->hash(), hashes[gen]) << "generation " << gen;
  }
  EXPECT_TRUE(l.diffOf(17).has_value());
  EXPECT_FALSE(l.genomeOf(41).has_value()) << "no Birth for generation 41";
}

// A power cut at every byte of the Death append: open keeps the whole
// entries, cuts the tail, and the line goes on as if the death never landed.
TEST(Lineage, ATornTailIsTruncatedAtEveryByte) {
  MemStorage store;
  Genome starter = starterGenome(7);
  Lineage l = Lineage::open(store, starter, 9, 0);
  Clutch k = clutchOf(starter, 1, 5);
  l.recordBirth(Egg(k.child(0), 1, 1), k, 0, 1);
  const std::vector<uint8_t> before = store.files[kLog];
  Creature dead = deadCreature(1);
  l.recordDeath(dead, "Grungo", 2);
  const std::vector<uint8_t> full = store.files[kLog];
  ASSERT_GT(full.size(), before.size());

  for (size_t cut = before.size(); cut < full.size(); ++cut) {
    MemStorage torn;
    torn.files[kLog].assign(full.begin(), full.begin() + std::ptrdiff_t(cut));
    Lineage reopened = Lineage::open(torn, starter, 9, 0);
    ASSERT_EQ(torn.files[kLog], before) << "cut at byte " << cut;
    EXPECT_EQ(census(reopened).deaths, 0);
    EXPECT_EQ(reopened.currentGeneration(), 1);
    reopened.recordDeath(dead, "Grungo", 3);
    EXPECT_EQ(census(Lineage::open(torn, starter, 9, 0)).deaths, 1) << "cut at byte " << cut;
  }

  for (size_t tear = 0; tear < full.size() - before.size(); ++tear) {
    MemStorage s;
    s.files[kLog] = before;
    Lineage live = Lineage::open(s, starter, 9, 0);
    s.tearAt(tear);
    live.recordDeath(dead, "Grungo", 2);
    ASSERT_EQ(s.files[kLog], before) << "a failed append at byte " << tear << " is rolled back";
    EXPECT_EQ(census(live).deaths, 0);
    live.recordDeath(dead, "Grungo", 2);
    EXPECT_EQ(s.files[kLog], full);
  }
}

TEST(Egg, WarmthHatchesFaster) {
  auto ticksToHatch = [](bool warm) {
    Egg egg(Offspring{starterGenome(7), {}}, 0, 0);
    SenseOut s;
    if (warm) s.set(locus::held, Fx::one());
    uint32_t n = 1;
    while (!egg.tick(s)) ++n;
    return n;
  };
  uint32_t cold = ticksToHatch(false), warm = ticksToHatch(true);
  EXPECT_EQ(cold, 30 * kTicksPerMinute);
  EXPECT_LT(warm, cold * 7 / 10);
}

TEST(Clutch, TheTimeoutPicksTheCursorEgg) {
  Clutch k = clutchOf(starterGenome(7), 1, 5);
  k.count = 3;
  SenseOut quiet;
  std::optional<uint8_t> pick;
  uint32_t ticks = 0;
  while (!pick) pick = k.tick(quiet), ++ticks;
  EXPECT_EQ(ticks, Clutch::kVigilTicks + Clutch::kPickTimeoutTicks);
  EXPECT_EQ(*pick, 0);
  for (uint8_t i = 0; i < 3; ++i) k.derivePreviewStep();
  k.derivePreviewStep();
  EXPECT_EQ(k.previewed, 3);
  EXPECT_GE(k.previews[0].mindChanges, 1);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
