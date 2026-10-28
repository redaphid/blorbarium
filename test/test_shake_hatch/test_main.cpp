// Shaking hatches the egg, no phone needed (owner, 2026-10-27: "it stays in
// egg form forever until a phone claims it. Nobody is doing that. Just make
// it so shaking it hatches the pets." Then: "Have people need to shake it for
// a while, and have the egg show cracks etc while being shook.").
//
// The firmware's eggs wait on a phone's ADOPT for their clock
// (DishOptions::hatchNeedsPhone), so these run with it set, as the badge
// does: time alone never hatches them; a few seconds of hard shaking does,
// cracking it on the way; a pick-up, a knock or a bump never starts it.
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <variant>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "links.h"
#include "mem_storage.h"
#include "time_sources.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint64_t kLineage = 0x5ea4c0ffee11ull;

// A badge as the firmware runs it: the founder's own 15-minute egg, waiting
// on a phone for its clock.
struct Badge {
  MemStorage store;
  FakeRtc rtc;
  Genome founder = starterGenome(7);
  std::optional<Dish> dish;
  NullLink link;
  uint32_t ms = 0;

  Badge() { rtc.poweredMs = &ms; }
  Dish& boot() {
    DishOptions o{&rtc, &founder};
    o.hatchNeedsPhone = true;
    o.foundingHour = kUnaskedFoundingHour;
    dish.emplace(store, 7, kLineage, o);
    ms = 0;
    dish->tick(ms, link);
    rest(2000);   // lying still a moment: the detectors learn which way is down
    return *dish;
  }
  void at(const BodySample& s) {
    ms += kSampleMs;
    dish->sample(s, ms);
    dish->tick(ms, link);
  }
  void rest(uint32_t forMs) {
    for (uint32_t end = ms + forMs; ms < end;) at(still());
  }
  // A hard shake in a hand: swung both ways along x, a swing every 60 ms.
  void shake(uint32_t forMs) {
    for (uint32_t n = forMs / kSampleMs; n; --n, ++swing) {
      BodySample s;
      s.ax = int16_t(swing / 3 % 2 ? -1500 : 1500), s.az = 1000;
      at(s);
    }
  }
  uint32_t swing = 0;
  bool egg() const { return std::holds_alternative<Egg>(dish->occupant()); }
  const Egg& theEgg() const { return std::get<Egg>(dish->occupant()); }
  Creature& creature() { return std::get<Creature>(dish->occupant()); }
  // Shakes in bursts of `burstMs`, a rest between, until it hatches: the shaking it took, ms.
  uint32_t shakeUntilHatched(uint32_t burstMs, uint32_t restMs = 800) {
    uint32_t shaken = 0;
    while (egg() && shaken < 60000) shake(burstMs), shaken += burstMs, rest(restMs);
    return shaken;
  }
};

}  // namespace

// ---- the egg's own count --------------------------------------------------------------------

TEST(Egg, kShakeTicksOfShakingHatchIt) {
  Egg e(Offspring{starterGenome(7), {}}, 0, 0);
  for (int i = 0; i < Egg::kShakeTicks - 1; ++i) ASSERT_FALSE(e.shaken(true, true)) << i;
  EXPECT_TRUE(e.shaken(true, true));
  EXPECT_EQ(e.crackStage(), Egg::kCrackStages);
}

TEST(Egg, ACrackHalfMadeClosesGentlyButTheStagesReachedStay) {
  Egg e(Offspring{starterGenome(7), {}}, 0, 0);
  for (int i = 0; i < 4; ++i) e.shaken(true, true);
  EXPECT_EQ(e.crackStage(), 0);
  const Fx made = e.progress();
  e.shaken(false, true);
  EXPECT_LT(e.progress(), made) << "settles";
  EXPECT_GT(e.progress(), made - Fx::ratio(1, 20)) << "gently";
  for (int i = 0; i < 1000; ++i) e.shaken(false, true);
  EXPECT_EQ(e.progress(), Fx::zero()) << "a few shakes short of a crack heal";

  for (int i = 0; i < Egg::kShakeTicks / 2 + 3; ++i) e.shaken(true, true);
  EXPECT_EQ(e.crackStage(), 2);
  for (int i = 0; i < 1000; ++i) e.shaken(false, true);
  EXPECT_EQ(e.crackStage(), 2) << "the cracks a kid made stay";
  EXPECT_EQ(e.progress(), Fx::ratio(1, 2));
}

TEST(Egg, IncubatingByTimeItNeverSettles) {
  Egg e(Offspring{starterGenome(7), {}}, 0, 0);
  for (int i = 0; i < 4; ++i) e.shaken(true, false);
  const Fx made = e.progress();
  for (int i = 0; i < 100; ++i) e.shaken(false, false);
  EXPECT_EQ(e.progress(), made);
}

// ---- on the badge ---------------------------------------------------------------------------

TEST(ShakeHatch, TimeAloneNeverHatchesItAndAPhoneIsNotNeeded) {
  Badge b;
  b.boot();
  ASSERT_TRUE(b.egg());
  ASSERT_TRUE(b.dish->hatchWaits());
  b.rest(20 * 60 * 1000);   // past its 15 minutes
  ASSERT_TRUE(b.egg()) << "the clock still waits on a phone";
  EXPECT_EQ(b.theEgg().progress(), Fx::zero());
  const uint32_t took = b.shakeUntilHatched(1000);
  EXPECT_FALSE(b.egg()) << "shaking hatched it";
  EXPECT_FALSE(b.dish->adopted()) << "with no phone";
  // A few seconds of real shaking, not one shake: never under 3 s, never over 8.
  EXPECT_GE(took, 3000u);
  EXPECT_LE(took, 8000u);
}

TEST(ShakeHatch, OneLongShakeHatchesItInAboutFourSeconds) {
  Badge b;
  b.boot();
  uint32_t shaken = 0;
  while (b.egg() && shaken < 20000) b.shake(100), shaken += 100;
  EXPECT_FALSE(b.egg());
  EXPECT_GE(shaken, 4000u);
  EXPECT_LE(shaken, 5500u);
}

// The egg cracks as it is shaken: the screen sees its progress climb and the
// shake in the hand, and once the shaking stops it settles back no further
// than the last crack made.
TEST(ShakeHatch, TheEggCracksAsItIsShakenAndKeepsItsCracks) {
  Badge b;
  b.boot();
  EXPECT_EQ(b.dish->appearance().eggProgress, Fx::zero());
  b.shake(800);
  Appearance a = b.dish->appearance();
  EXPECT_GT(a.eggProgress, Fx::zero()) << "a crack begins";
  EXPECT_GT(a.eggShaken, Fx::ratio(1, 2)) << "and it jiggles with the hand";
  b.shake(1600);
  const uint8_t stage = b.theEgg().crackStage();
  EXPECT_GE(stage, 2);
  b.rest(60 * 1000);
  EXPECT_EQ(b.theEgg().crackStage(), stage) << "kept";
  EXPECT_EQ(b.dish->appearance().eggShaken, Fx::zero()) << "still now";
  // ...and kept over a replug.
  b.dish->flush();
  b.boot();
  ASSERT_TRUE(b.egg());
  EXPECT_EQ(b.theEgg().crackStage(), stage);
}

// What a hand does to a badge all day: none of it starts the egg cracking.
TEST(ShakeHatch, PickUpsKnocksAndBumpsNeverCrackIt) {
  Badge b;
  b.boot();
  for (int round = 0; round < 20; ++round) {
    // Five short jolts, quiet between: a Shake to the detector, as a pick-up
    // from face down was on the recordings, but no hard shake.
    for (int i = 0; i < 50; ++i) {
      BodySample s;
      s.az = 1000;
      if (i % 4 == 0) s.ax = int16_t(i / 4 % 2 ? -1300 : 1300);
      b.at(s);
    }
    b.rest(700);
    // Bumps in a bag: a jolt up and back down now and then.
    for (int i = 0; i < 6; ++i) b.at(jolt(true)), b.at(jolt(false)), b.rest(300);
    // A knock knock: a sharp tap twice.
    for (int k = 0; k < 2; ++k) {
      BodySample s = still();
      s.az = 2400;
      b.at(s);
      b.rest(200);
    }
    b.rest(1000);
  }
  ASSERT_TRUE(b.egg());
  EXPECT_EQ(b.theEgg().progress(), Fx::zero());
}

// A badge stuck as an egg before this firmware: its egg saved, never
// adopted. The new image boots it as it was, and a shake hatches it.
TEST(ShakeHatch, AnEggSavedWaitingHatchesOnAShakeAfterTheUpdate) {
  Badge b;
  b.boot();
  b.rest(5 * 60 * 1000);
  b.dish->flush();
  EXPECT_EQ(b.boot().boot(), Boot::Resumed);
  ASSERT_TRUE(b.egg());
  b.shakeUntilHatched(1500);
  EXPECT_FALSE(b.egg());
}

// Hatched by a shake, he is a pet like any other: the shell flies off him,
// he gets hungry, a press feeds him, he ages, and he says things.
TEST(ShakeHatch, AShakenHatchlingLivesAsAnyOther) {
  Badge b;
  b.boot();
  b.shakeUntilHatched(1500);
  ASSERT_FALSE(b.egg());
  Appearance a = b.dish->appearance();
  EXPECT_EQ(a.kind, Appearance::Kind::Creature);
  EXPECT_EQ(a.stage, Stage::Baby);
  EXPECT_LT(a.hatchedMs, Appearance::kHatchBurstMs) << "the shell flies off him";
  b.rest(Appearance::kHatchBurstMs);
  EXPECT_EQ(b.dish->appearance().hatchedMs, UINT16_MAX);

  const uint32_t born = b.creature().stats().ageTicks;
  Fx least = Fx::one();
  bool said = false;
  for (int minute = 0; minute < 4 * 60; ++minute) {
    b.rest(60 * 1000);
    said = said || b.dish->appearance().thinking;
    least = std::min(least, b.creature().chemistry().drive(drive::hunger));
  }
  Creature& c = b.creature();
  EXPECT_GT(c.stats().ageTicks, born + 4 * 3600 * 1000 / kTickMs - 100) << "he ages";
  const Fx hungry = c.chemistry().drive(drive::hunger);
  EXPECT_GT(hungry, least) << "he gets hungry";
  EXPECT_TRUE(said) << "he says things";

  const uint16_t fed = c.stats().fed;
  b.at(pressed());   // a press drops a cookie
  b.rest(60 * 1000);
  EXPECT_GT(b.creature().stats().fed, fed) << "a press feeds him";
  EXPECT_LT(b.creature().chemistry().drive(drive::hunger), hungry) << "and he eats it";
}

// The phone stays an option: ADOPT still lets the clock hatch it alone.
TEST(ShakeHatch, AnAdoptedEggStillHatchesOnItsClock) {
  Badge b;
  b.boot();
  ASSERT_TRUE(b.dish->adopt());
  EXPECT_FALSE(b.dish->hatchWaits());
  b.rest(16 * 60 * 1000);
  EXPECT_FALSE(b.egg());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
