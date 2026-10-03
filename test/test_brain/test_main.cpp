#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include "blorb/brain.h"

using namespace blorb;

namespace {

size_t fi(LocusId id) {
  for (size_t i = 0; i < kFeatureCount; ++i) if (FEATURES[i] == id) return i;
  ADD_FAILURE() << "locus " << int(id.v) << " is not a feature";
  return 0;
}

size_t di(DriveId id) {
  for (size_t i = 0; i < kDriveCount; ++i) if (DRIVES[i].id == id) return i;
  ADD_FAILURE() << "drive " << int(id.v) << " is not a drive";
  return 0;
}

Temperament midTemperament() {
  return Temperament{Fx::unitByte(128), Fx::unitByte(128), Fx::unitByte(128), Fx::unitByte(128),
                     300, 30, decayFromByte(60)};
}

Fx frac(int num, int den) { return Fx::ratio(num, den); }

// Eat with food near lowers hunger by 0.3; chase with the marble near lowers
// boredom by 0.3. Nothing else does anything.
struct World {
  Fx features[kFeatureCount]{};
  Fx drives[kDriveCount]{};

  void see(LocusId l, bool on) { features[fi(l)] = on ? Fx::one() : Fx::zero(); }
  Fx& drive(DriveId d) { return drives[di(d)]; }
  bool sees(LocusId l) const { return features[fi(l)] != Fx::zero(); }

  void answer(ActionId a) {
    if (a == action::eat && sees(locus::food_near)) drive(drive::hunger) = clamp01(drive(drive::hunger) - frac(3, 10));
    if (a == action::chase && sees(locus::marble_near)) drive(drive::boredom) = clamp01(drive(drive::boredom) - frac(3, 10));
  }
};

// One independent trial. The brain decides with learning off, so the test's
// fresh situation is not learned as the last action's effect. The world
// answers, the action runs for its minTicks, and the think that ends it learns.
ActionId trial(Brain& b, World& w, const Temperament& t, Rng& rng) {
  Temperament decideOnly = t;
  decideOnly.learnRate = Fx::zero();
  ActionId a = b.think(w.features, w.drives, Fx::zero(), decideOnly, true, rng).action;
  w.answer(a);
  uint32_t thinks = (ACTIONS[a.v].minTicks + kBrainEvery - 1) / kBrainEvery;
  for (uint32_t i = 0; i < thinks; ++i) b.think(w.features, w.drives, Fx::zero(), t, false, rng);
  return a;
}

// How often a copy of `b` picks `a` in situation `w`, over many seeds. Learning
// is off, so each probe sees the same brain.
double share(const Brain& b, const World& w, ActionId a, Temperament t, int seeds = 200) {
  t.learnRate = Fx::zero();
  int hits = 0;
  for (int s = 0; s < seeds; ++s) {
    Brain copy = b;
    Rng rng = Rng::seeded(1000 + s);
    hits += copy.think(w.features, w.drives, Fx::zero(), t, true, rng).action == a;
  }
  return double(hits) / seeds;
}

World situation(Fx hunger, bool food) {
  World w;
  w.see(locus::light, true);
  w.see(locus::food_near, food);
  w.drive(drive::hunger) = hunger;
  w.drive(drive::boredom) = frac(1, 10);
  return w;
}

Fx contextPrediction(const Brain& b, const World& w, ActionId a, DriveId d) {
  Fx p;
  for (size_t f = 0; f < kFeatureCount; ++f) p += w.features[f] * b.predict(FEATURES[f], a, d);
  return p;
}

double toDouble(Fx v) { return double(v.raw) / Fx::kOne; }

Instinct instinct(LocusId cue, ActionId a, DriveId d, uint8_t levelByte, uint8_t strengthByte) {
  return Instinct{{cue, LocusId{255}, LocusId{255}}, 1, a, d, Fx::signedByte(levelByte), Fx::unitByte(strengthByte)};
}

}  // namespace

TEST(Learning, EatsWhenHungryAndFoodIsNearButNotWhenFull) {
  Temperament t = midTemperament();
  Brain b;
  Rng rng = Rng::seeded(7);
  for (int i = 0; i < 300; ++i) {
    World w;
    w.see(locus::light, rng.chance(frac(7, 10)));
    w.see(locus::food_near, rng.chance(frac(1, 2)));
    w.see(locus::marble_near, rng.chance(frac(1, 2)));
    w.drive(drive::hunger) = rng.unit();
    w.drive(drive::boredom) = rng.unit();
    trial(b, w, t, rng);
  }
  EXPECT_GE(share(b, situation(frac(9, 10), true), action::eat, t), 0.8);
  EXPECT_LE(share(b, situation(Fx::zero(), true), action::eat, t), 0.2) << "eating while full";
  EXPECT_LE(share(b, situation(frac(9, 10), false), action::eat, t), 0.3) << "eating with no food near";
}

TEST(Learning, MidTemperamentConvergesWithinTensOfTrialsWithoutOvershoot) {
  Temperament t = midTemperament();
  Brain b;
  Rng rng = Rng::seeded(3);
  std::vector<double> afterEachEat;
  for (int i = 0; i < 400 && afterEachEat.size() < 20; ++i) {
    World w = situation(frac(8, 10), true);
    if (trial(b, w, t, rng) == action::eat)
      afterEachEat.push_back(toDouble(contextPrediction(b, situation(frac(8, 10), true), action::eat, drive::hunger)));
  }
  ASSERT_EQ(afterEachEat.size(), 20u);
  for (size_t k = 0; k < afterEachEat.size(); ++k) {
    EXPECT_GE(afterEachEat[k], -0.3 - 0.005) << "overshot at eat trial " << k + 1;
    if (k > 0) EXPECT_LE(afterEachEat[k], afterEachEat[k - 1] + 1e-4) << "oscillated at eat trial " << k + 1;
  }
  EXPECT_NEAR(afterEachEat[19], -0.3, 0.03);
}

TEST(Habituation, RepeatedPointlessFavouriteLosesToAlternativesButStaysFavourite) {
  World w;
  w.see(locus::light, true);
  w.drive(drive::boredom) = frac(1, 2);
  auto run = [&](uint8_t habituationByte) {
    Temperament t = midTemperament();
    t.learnRate = Fx::zero();   // the wrong belief is never corrected, so only habituation can move him
    t.forgetRate = Fx::zero();
    t.explore = Fx::zero();
    t.habituation = Fx::unitByte(habituationByte);
    Brain b;
    Rng rng = Rng::seeded(11);
    b.queueInstinct(instinct(locus::light, action::rest, drive::boredom, 128 - 26, 255));
    b.dream(t, rng);
    std::vector<int> counts(kActionCount, 0);
    for (int i = 0; i < 200; ++i) {
      ActionId a = b.think(w.features, w.drives, Fx::zero(), t, true, rng).action;
      for (size_t k = 0; k < kActionCount; ++k) counts[k] += ACTIONS[k].id == a;
    }
    return counts;
  };

  std::vector<int> stubborn = run(0);
  EXPECT_EQ(stubborn[action::rest.v], 200) << "control: without habituation the favourite never loses";

  std::vector<int> counts = run(128);
  EXPECT_LT(counts[action::rest.v], 100);
  EXPECT_GE(std::count_if(counts.begin(), counts.end(), [](int c) { return c > 0; }), 4);
  for (size_t k = 0; k < kActionCount; ++k)
    if (ACTIONS[k].id != action::rest) EXPECT_GT(counts[action::rest.v], counts[k]) << ACTIONS[k].name;
}

TEST(Holding, ActionIsHeldForMinTicksUnlessFinished) {
  Temperament t = midTemperament();
  t.explore = Fx::zero();
  World w;
  Rng rng = Rng::seeded(5);
  Brain b;
  Decision first = b.think(w.features, w.drives, Fx::zero(), t, false, rng);
  ASSERT_EQ(first.action, action::rest);
  Brain finishedEarly = b;

  uint32_t thinksToHold = ACTIONS[action::rest.v].minTicks / kBrainEvery;
  for (uint32_t i = 1; i < thinksToHold; ++i) {
    Decision d = b.think(w.features, w.drives, Fx::zero(), t, false, rng);
    EXPECT_EQ(d.action, action::rest) << "think " << i;
    EXPECT_FALSE(d.changed);
  }
  Decision after = b.think(w.features, w.drives, Fx::zero(), t, false, rng);
  EXPECT_NE(after.action, action::rest) << "habit should win once the hold is over";
  EXPECT_TRUE(after.changed);

  Decision early = finishedEarly.think(w.features, w.drives, Fx::zero(), t, true, rng);
  EXPECT_NE(early.action, action::rest);
}

TEST(Instinct, DreamedInstinctMakesNewbornActOnItsCueWithNoExperience) {
  Temperament t = midTemperament();
  Brain newborn;
  newborn.queueInstinct(instinct(locus::food_near, action::eat, drive::hunger, 64, 200));
  Brain undreamt = newborn;

  Rng rng = Rng::seeded(9);
  EXPECT_TRUE(newborn.dream(t, rng));
  EXPECT_FALSE(newborn.dream(t, rng)) << "nothing left to dream";
  EXPECT_FALSE(newborn.dream(t, rng));

  EXPECT_GE(share(newborn, situation(frac(9, 10), true), action::eat, t), 0.9);
  EXPECT_LE(share(newborn, situation(frac(9, 10), false), action::eat, t), 0.2) << "acted without the cue";
  EXPECT_LE(share(undreamt, situation(frac(9, 10), true), action::eat, t), 0.2) << "queued but never dreamt";
}

// Naps cut short by a shake must not pile up a refresh per bedtime.
TEST(Instinct, ABedtimeRefreshWaitsBehindItsOwnQueuedCopy) {
  Temperament t = midTemperament();
  const std::vector<Instinct> genome = {instinct(locus::food_near, action::eat, drive::hunger, 40, 200),
                                        instinct(locus::cradled, action::rest, drive::need_touch, 64, 200)};
  Brain b;
  b.refreshInstincts(genome);
  b.refreshInstincts(genome);
  Rng rng = Rng::seeded(3);
  int dreamt = 0;
  for (int i = 0; i < 6; ++i) dreamt += b.dream(t, rng);
  EXPECT_EQ(dreamt, 2);
  EXPECT_LT(b.predict(locus::food_near, action::eat, drive::hunger), Fx::zero()) << "the refresh was dreamt";
}

TEST(Instinct, NamingNoRegistryRowIsInert) {
  Temperament t = midTemperament();
  t.forgetRate = Fx::zero();
  Brain b;
  Brain untouched = b;
  b.queueInstinct(instinct(locus::food_near, ActionId{200}, drive::hunger, 0, 255));
  b.queueInstinct(instinct(LocusId{250}, action::eat, drive::hunger, 0, 255));
  Rng rng = Rng::seeded(1);
  EXPECT_FALSE(b.dream(t, rng));
  EXPECT_FALSE(b.dream(t, rng));
  EXPECT_EQ(b.hash(), untouched.hash());
}

TEST(Dreaming, ReplayedEpisodeMovesPredictionFurtherTowardWhatHappened) {
  Temperament t = midTemperament();
  t.forgetRate = Fx::zero();
  t.explore = Fx::zero();
  Brain b;
  Rng rng = Rng::seeded(2);
  b.queueInstinct(instinct(locus::food_near, action::eat, drive::hunger, 128 - 6, 255));
  b.dream(t, rng);
  World w = situation(frac(9, 10), true);
  ASSERT_EQ(b.think(w.features, w.drives, Fx::zero(), t, true, rng).action, action::eat);
  w.answer(action::eat);
  b.think(w.features, w.drives, Fx::zero(), t, true, rng);
  Fx once = contextPrediction(b, situation(frac(9, 10), true), action::eat, drive::hunger);
  ASSERT_LT(once, -frac(1, 10)) << "one experience should already teach something";

  int replays = 0;
  for (int i = 0; i < 400; ++i) replays += b.dream(t, rng);
  ASSERT_GT(replays, 0);
  Fx later = contextPrediction(b, situation(frac(9, 10), true), action::eat, drive::hunger);
  EXPECT_LT(later, once);
  EXPECT_GE(toDouble(later), -0.3 - 0.005);
}

// One LSB per dream, rounded up, took the same 0.14 off every weight each
// night: a small lesson was gone by morning while a large one barely moved.
TEST(Forgetting, ANightFadesBigAndSmallBeliefsByTheSameDesignedFraction) {
  Brain::Axes axes = Brain::currentAxes();
  std::vector<Q15> w(kFeatureCount * kActionCount * kDriveCount);
  auto at = [&](LocusId f, ActionId a, DriveId d) -> Q15& {
    return w[(fi(f) * kActionCount + a.v) * kDriveCount + di(d)];
  };
  const Q15 big{16384}, small{1638};   // 0.5 and 0.05
  at(locus::light, action::rest, drive::hunger) = big;
  at(locus::light, action::eat, drive::hunger) = Q15{int16_t(-big.v)};
  at(locus::held, action::rest, drive::fear) = small;
  at(locus::held, action::eat, drive::fear) = Q15{int16_t(-small.v)};

  Temperament starter = midTemperament();
  starter.forgetRate = Fx::unitByte(16);
  constexpr uint32_t kNight = 4500;
  const double keep = std::pow(1.0 - 16.0 / 255.0 / 2048.0, kNight);
  ASSERT_NEAR(1.0 - keep, 0.129, 0.001) << "the designed fade of a 10-hour night";

  Brain b;
  b.loadWeights(axes, w.data());
  b.forget(starter, kNight);
  const double lsb = 1.0 / 32768;
  struct Cell { LocusId f; DriveId d; Q15 was; };
  for (Cell c : {Cell{locus::light, drive::hunger, big}, Cell{locus::held, drive::fear, small}}) {
    Fx pos = b.predict(c.f, action::rest, c.d), neg = b.predict(c.f, action::eat, c.d);
    EXPECT_NEAR(toDouble(pos), toDouble(fromQ15(c.was)) * keep, lsb) << c.was.v;
    EXPECT_EQ(pos, -neg) << "both signs fade alike";
  }

  Brain kept;
  kept.loadWeights(axes, w.data());
  Temperament never = starter;
  never.forgetRate = Fx::zero();
  kept.forget(never, kNight);
  EXPECT_EQ(kept.predict(locus::held, action::rest, drive::fear), fromQ15(small));
}

TEST(Remap, SavedWeightsSurviveAddedReorderedAndRemovedRows) {
  Brain::Axes now = Brain::currentAxes();
  Brain::Axes saved;
  // An older firmware: it lacked the first feature, the last action and the
  // last drive, had rows since removed, and listed them in another order.
  saved.features.assign(now.features.rbegin(), now.features.rend() - 1);
  saved.features.insert(saved.features.begin() + 3, LocusId{250});
  saved.actions.assign(now.actions.begin(), now.actions.end() - 1);
  saved.actions.push_back(ActionId{200});
  std::rotate(saved.actions.begin(), saved.actions.begin() + 4, saved.actions.end());
  saved.drives.assign(now.drives.rbegin() + 1, now.drives.rend());

  size_t nF = saved.features.size(), nA = saved.actions.size(), nD = saved.drives.size();
  std::vector<Q15> w(nF * nA * nD);
  for (size_t i = 0; i < w.size(); ++i) w[i] = Q15{int16_t(1 + i)};

  Brain b;
  b.queueInstinct(instinct(now.features[0], now.actions.back(), now.drives.back(), 0, 255));
  Rng rng = Rng::seeded(6);
  b.dream(midTemperament(), rng);
  ASSERT_NE(b.predict(now.features[0], now.actions.back(), now.drives.back()), Fx::zero());
  b.loadWeights(saved, w.data());

  auto pos = [](const auto& v, auto id) -> int {
    for (size_t i = 0; i < v.size(); ++i) if (v[i] == id) return int(i);
    return -1;
  };
  int kept = 0, fresh = 0;
  for (LocusId f : now.features)
    for (ActionId a : now.actions)
      for (DriveId d : now.drives) {
        int sf = pos(saved.features, f), sa = pos(saved.actions, a), sd = pos(saved.drives, d);
        if (sf < 0 || sa < 0 || sd < 0) {
          EXPECT_EQ(b.predict(f, a, d), Fx::zero());
          ++fresh;
        } else {
          EXPECT_EQ(b.predict(f, a, d), fromQ15(w[(size_t(sf) * nA + size_t(sa)) * nD + size_t(sd)]));
          ++kept;
        }
      }
  EXPECT_EQ(kept, int((kFeatureCount - 1) * (kActionCount - 1) * (kDriveCount - 1)));
  EXPECT_GT(fresh, 0);
}

TEST(Beliefs, StrongestComeFirstAndZeroCellsAreNotBeliefs) {
  Brain::Axes axes = Brain::currentAxes();
  std::vector<Q15> w(kFeatureCount * kActionCount * kDriveCount);
  auto set = [&](LocusId f, ActionId a, DriveId d, Fx v) {
    w[(fi(f) * kActionCount + a.v) * kDriveCount + di(d)] = toQ15(v);
  };
  set(locus::marble_near, action::chase, drive::boredom, -frac(1, 10));
  set(locus::food_near, action::eat, drive::hunger, -frac(1, 2));
  set(locus::light, action::rest, drive::sleepiness, frac(3, 10));
  Brain b;
  b.loadWeights(axes, w.data());

  std::vector<Belief> top = b.strongestBeliefs(2);
  ASSERT_EQ(top.size(), 2u);
  EXPECT_EQ(top[0].feature, locus::food_near);
  EXPECT_EQ(top[0].action, action::eat);
  EXPECT_EQ(top[0].drive, drive::hunger);
  EXPECT_EQ(top[0].confidence, Fx::one());
  EXPECT_EQ(top[1].action, action::rest);

  std::vector<Belief> all = b.strongestBeliefs(50);
  ASSERT_EQ(all.size(), 3u);
  EXPECT_EQ(all[2].action, action::chase);
  EXPECT_LT(all[2].confidence, Fx::one());
}

TEST(Hash, CoversQueueAndDecisionState) {
  Brain a, b;
  EXPECT_EQ(a.hash(), b.hash());
  b.queueInstinct(instinct(locus::light, action::rest, drive::boredom, 100, 100));
  EXPECT_NE(a.hash(), b.hash());

  Brain c;
  World w;
  w.drive(drive::hunger) = frac(1, 2);
  Rng rng = Rng::seeded(8);
  c.think(w.features, w.drives, Fx::zero(), midTemperament(), true, rng);
  EXPECT_NE(a.hash(), c.hash());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
