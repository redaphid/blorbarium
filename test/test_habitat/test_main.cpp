#include <gtest/gtest.h>
#include <cmath>
#include "blorb/habitat.h"

using namespace blorb;

namespace {

const HabitatRules kRules{3, 100, 1000, Fx::ratio(1, 4)};
const Fx kLevel = Fx::ratio(1, 2);

double d(Fx v) { return double(v.raw) / Fx::kOne; }
double radius(DishPos p) { return std::hypot(d(p.x), d(p.y)); }
DishPos at(double x, double y) { return DishPos{Fx{int32_t(std::lround(x * Fx::kOne))}, Fx{int32_t(std::lround(y * Fx::kOne))}}; }

struct World {
  Habitat h;
  Rng rng = Rng::seeded(42);
  SenseOut out;
  uint32_t tick = 0;
  DishPos creature = at(0, 0);
  int marbleHits = 0;
  void step(Fx tx = kLevel, Fx ty = kLevel) {
    out.clear();
    h.step(kRules, tx, ty, creature, tick++, out);
    for (uint8_t i = 0; i < out.stimCount; ++i) marbleHits += out.stimuli[i] == stim::marble_hit;
  }
  void steps(uint32_t n, Fx tx = kLevel, Fx ty = kLevel) { for (uint32_t i = 0; i < n; ++i) step(tx, ty); }
  void fill() { steps(kRules.refillTicks * kRules.pantrySize + 1); }
  bool drop() {
    SenseOut o;
    bool ok = h.dropPellet(kRules, rng, tick, o);
    EXPECT_EQ(o.stimCount, ok ? 1 : 0);
    if (ok) { EXPECT_EQ(o.stimuli[0], stim::pellet_dropped); }
    return ok;
  }
  double locus(LocusId id) const {
    for (uint8_t i = 0; i < out.lociCount; ++i) if (out.loci[i].locus == id) return d(out.loci[i].value);
    ADD_FAILURE() << "locus " << int(id.v) << " not written";
    return -1;
  }
};

}  // namespace

TEST(Pantry, RefillsOnePelletEveryRefillTicksAndNeverPastItsSize) {
  World w;
  w.steps(100);
  EXPECT_EQ(w.h.pantry, 0);
  w.step();
  EXPECT_EQ(w.h.pantry, 1);
  uint8_t most = 0;
  for (int i = 0; i < 5000; ++i) { w.step(); most = std::max(most, w.h.pantry); }
  EXPECT_EQ(most, kRules.pantrySize);
}

TEST(Pantry, DroppingEmptiesItAndFailsWhenEmpty) {
  World w;
  w.fill();
  ASSERT_EQ(w.h.pantry, 3);
  EXPECT_TRUE(w.drop());
  EXPECT_TRUE(w.drop());
  EXPECT_TRUE(w.drop());
  EXPECT_EQ(w.h.pantry, 0);
  EXPECT_FALSE(w.drop());
  int present = 0;
  for (const Pellet& p : w.h.pellets) {
    if (!p.present) continue;
    ++present;
    EXPECT_LE(radius(p.at), 0.6);
  }
  EXPECT_EQ(present, 3);
}

TEST(Pantry, ARefillAfterTakingFromAFullPantryWaitsAWholeInterval) {
  World w;
  w.fill();
  w.steps(500);
  w.drop();
  w.steps(98);
  EXPECT_EQ(w.h.pantry, 2);
  w.steps(2);
  EXPECT_EQ(w.h.pantry, 3);
}

TEST(Pantry, TheDropSpotIsSeeded) {
  World a, b, c;
  c.rng = Rng::seeded(43);
  for (World* w : {&a, &b, &c}) { w->fill(); w->drop(); }
  EXPECT_EQ(a.h.hash(), b.h.hash());
  EXPECT_NE(a.h.hash(), c.h.hash());
}

TEST(Pantry, AFullDishReplacesItsOldestPellet) {
  World w;
  auto dropOne = [&w] { w.h.pantry = 1; w.step(); ASSERT_TRUE(w.drop()); };
  for (int i = 0; i < Habitat::kMaxPellets; ++i) dropOne();
  ASSERT_TRUE(w.h.bite(kRules, w.h.pellets[0].at, w.tick));
  dropOne();
  dropOne();
  uint32_t oldest = UINT32_MAX;
  for (const Pellet& p : w.h.pellets) { EXPECT_TRUE(p.present); oldest = std::min(oldest, p.droppedTick); }
  EXPECT_EQ(oldest, 3u);
}

TEST(Bite, OnlyReachesAPelletWithinBiteRange) {
  World w;
  w.fill();
  w.drop();
  DishPos p = *w.h.nearestPellet(at(0, 0));
  EXPECT_FALSE(w.h.bite(kRules, at(d(p.x) + 0.2, d(p.y)), w.tick));
  auto b = w.h.bite(kRules, at(d(p.x) + 0.1, d(p.y)), w.tick);
  ASSERT_TRUE(b);
  EXPECT_EQ(b->food, kRules.biteSize);
  EXPECT_FALSE(b->rotten);
  EXPECT_FALSE(w.h.nearestPellet(at(0, 0)));
  EXPECT_FALSE(w.h.bite(kRules, p, w.tick));
}

TEST(Bite, APelletOlderThanRotTicksIsRotten) {
  World w;
  w.fill();
  uint32_t dropped = w.tick;
  w.drop();
  w.drop();
  DishPos first = w.h.pellets[0].at, second = w.h.pellets[1].at;
  auto fresh = w.h.bite(kRules, first, dropped + kRules.rotTicks);
  auto old = w.h.bite(kRules, second, dropped + kRules.rotTicks + 1);
  ASSERT_TRUE(fresh && old);
  EXPECT_FALSE(fresh->rotten);
  EXPECT_TRUE(old->rotten);
}

TEST(Loci, FoodRottenFollowsTheNearestPellet) {
  World w;
  w.fill();
  w.drop();
  w.step();
  EXPECT_EQ(w.locus(locus::food_rotten), 0.0);
  w.steps(kRules.rotTicks + 1);
  EXPECT_EQ(w.locus(locus::food_rotten), 1.0);
}

TEST(Loci, FoodNearRisesAsHeGetsCloserAndIsZeroWithNoFood) {
  World w;
  w.step();
  EXPECT_EQ(w.locus(locus::food_near), 0.0);
  w.fill();
  w.drop();
  DishPos p = *w.h.nearestPellet(at(0, 0));
  w.creature = at(d(p.x) + 0.8, d(p.y));
  w.step();
  double far = w.locus(locus::food_near);
  w.creature = at(d(p.x) + 0.1, d(p.y));
  w.step();
  double close = w.locus(locus::food_near);
  EXPECT_GT(far, 0.0);
  EXPECT_GT(close, far + 0.5);
}

TEST(Loci, PantryAndMarbleNearAreWritten) {
  World w;
  w.fill();
  w.drop();
  w.step();
  EXPECT_NEAR(w.locus(locus::pantry), 2.0 / 3.0, 1e-6);
  w.creature = at(0.05, 0);
  w.step();
  EXPECT_GT(w.locus(locus::marble_near), 0.9);
  w.creature = at(-0.9, 0);
  w.step();
  EXPECT_LT(w.locus(locus::marble_near), 0.2);
}

TEST(Roll, TiltRollsPelletsAndTheMarbleDownhill) {
  World w;
  w.fill();
  w.drop();
  w.drop();
  DishPos before[2] = {w.h.pellets[0].at, w.h.pellets[1].at};
  Fx raisedPlusX = Fx::ratio(85, 100), raisedPlusY = Fx::ratio(80, 100);
  w.steps(5, raisedPlusX, kLevel);
  for (int i = 0; i < 2; ++i) {
    EXPECT_LT(d(w.h.pellets[i].at.x), d(before[i].x) - 0.05);
    EXPECT_EQ(w.h.pellets[i].at.y, before[i].y);
  }
  EXPECT_LT(d(w.h.marble.at.x), -0.01);
  w.steps(5, kLevel, raisedPlusY);
  EXPECT_LT(d(w.h.marble.vy), 0.0);
}

TEST(Roll, NothingLeavesTheRimHoweverLongItIsTilted) {
  World w;
  w.fill();
  w.drop();
  w.drop();
  w.drop();
  double furthest = 0;
  for (int i = 0; i < 3000; ++i) {
    double phase = i / 300.0;
    w.step(Fx{int32_t((0.5 + 0.45 * std::cos(phase)) * Fx::kOne)}, Fx{int32_t((0.5 + 0.45 * std::sin(phase)) * Fx::kOne)});
    for (const Pellet& p : w.h.pellets) if (p.present) furthest = std::max(furthest, radius(p.at));
    furthest = std::max(furthest, radius(w.h.marble.at));
  }
  EXPECT_GT(furthest, 0.9);
  EXPECT_LE(furthest, 0.92 + 1e-6);
}

TEST(Roll, ALevelDishLeavesEverythingWhereItIs) {
  World w;
  w.fill();
  w.drop();
  w.h.marble.at = at(0.3, -0.2);
  uint32_t before = w.h.hash();
  Habitat copy = w.h;
  w.steps(500, Fx::ratio(51, 100), Fx::ratio(49, 100));
  EXPECT_EQ(w.h.pellets[0].at.x, copy.pellets[0].at.x);
  EXPECT_EQ(w.h.marble.at.x, copy.marble.at.x);
  EXPECT_EQ(w.h.marble.at.y, copy.marble.at.y);
  EXPECT_NE(w.h.hash(), before);   // the pantry ticked on
}

TEST(Marble, RollingIntoHimIsOneHitEvenWhenItComesToRestAgainstHim) {
  World w;
  w.h.marble.at = at(0.3, 0);
  w.creature = at(-0.85, 0);
  w.steps(600, Fx::ratio(85, 100), kLevel);
  EXPECT_EQ(w.marbleHits, 1);
  EXPECT_LT(radius(w.h.marble.at) , 0.92 + 1e-6);

  World away;
  away.h.marble.at = at(0.3, 0);
  away.creature = at(0, 0.7);
  away.steps(600, Fx::ratio(85, 100), kLevel);
  EXPECT_EQ(away.marbleHits, 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
