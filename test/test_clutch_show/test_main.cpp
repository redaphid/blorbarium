// Choosing an egg through a badge's whole body path (feeding_badge.h: the IMU
// at 250 Hz, the shell, the Dish ticked a frame at a time): the screen
// follows the tilt as soon as a sample has it, a knock closes the ring as it
// lands, and the egg kept is the one the screen pointed at
// (blorb/clutch_show.h, lib/paint/src/clutch.cpp).
#include <gtest/gtest.h>
#include <cstring>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "../../src/debug_verbs.h"   // here, so the dependency finder links lib/debugble
#include "feeding_badge.h"

using namespace blorb;
using namespace feedingtest;

namespace {

const Clutch* clutch(Badge& b) { return std::get_if<Clutch>(&b.dish->occupant()); }

// An elder dies and his three eggs are out, held flat in a hand. A hand
// tips it over a quarter second, never in one read (a jump of gravity in one
// read is a knock's shape).
void toTheEggs(Badge& b) {
  b.frameMs = 20;
  b.hand.turnMs = 250;
  b.hand.pose = Pose::Flat;
  b.creature().fall();
  b.run(300);
  ASSERT_NE(clutch(b), nullptr);
  for (uint32_t t = 0; t < Clutch::kVigilTicks; ++t) b.dish->tick(b.ms += 100, b.link);
  b.nextFrame = b.ms;
  b.run(1500);
  ASSERT_NE(clutch(b), nullptr);
  ASSERT_EQ(clutch(b)->count, 3);
  ASSERT_GE(clutch(b)->sinceDeath, Clutch::kVigilTicks);
}

uint8_t bornOf(Badge& b) {
  int chosen = -1;
  b.dish->lineage().forEach([&](const LineageEntry& e) {
    if (const auto* x = std::get_if<Birth>(&e)) chosen = x->chosen;
  });
  return uint8_t(chosen);
}

}  // namespace

TEST(Picker, LevelPointsAtNoEggAndTeachesTheTilt) {
  Badge b(kHomeDown, std::nullopt, "elder");
  toTheEggs(b);
  const Appearance a = b.dish->appearance();
  EXPECT_TRUE(a.clutch.picking);
  EXPECT_EQ(a.clutch.aim, -1) << "flat in a hand: level";
  EXPECT_EQ(a.eggCount, 3);
}

// The screen's aim comes from the sample, the engine's from the tick after
// it: the screen is never behind, and they agree.
TEST(Picker, TheScreenPointsNoLaterThanTheEngineAndAtTheSameEgg) {
  for (Pose p : {Pose::TiltLeft, Pose::TiltRight, Pose::TiltNear}) {
    Badge b(kHomeDown, std::nullopt, "elder");
    toTheEggs(b);
    const int8_t want = p == Pose::TiltLeft ? 0 : p == Pose::TiltRight ? 1 : 2;
    b.hand.pose = p == Pose::TiltLeft ? Pose::TiltRight : Pose::TiltLeft;   // from another egg
    b.run(1500);
    ASSERT_NE(clutch(b), nullptr) << "kept while tilting to another egg";
    ASSERT_NE(clutch(b)->cursor, want);
    b.hand.pose = p;
    uint32_t start = b.ms, screenAt = 0, engineAt = 0;
    while (b.ms - start < 3000 && !(screenAt && engineAt)) {
      b.run(4);
      if (!screenAt && b.dish->appearance().clutch.aim == want) screenAt = b.ms;
      ASSERT_NE(clutch(b), nullptr) << "kept while tilting";
      if (!engineAt && clutch(b)->cursor == want) engineAt = b.ms;
    }
    ASSERT_TRUE(screenAt && engineAt) << "egg " << int(want);
    EXPECT_LE(screenAt, engineAt) << "egg " << int(want) << ": the screen waited for the tick";
    EXPECT_LT(screenAt - start, 1000u) << "egg " << int(want);
  }
}

// The ring starts closing as the knuckle lands, about half a second before a
// lone knock is decided; then the egg it pointed at is kept, and its moment shows.
TEST(Picker, AKnockClosesTheRingAsItLandsAndKeepsThatEgg) {
  Badge b(kHomeDown, std::nullopt, "elder");
  toTheEggs(b);
  b.hand.pose = Pose::TiltRight;
  b.run(1200);
  ASSERT_EQ(b.dish->appearance().clutch.aim, 1);
  const uint32_t knockAt = b.ms + 50;
  b.hand.knocks = {knockAt};
  uint32_t ringAt = 0;
  while (b.ms < knockAt + 2000 && clutch(b)) {
    b.run(4);
    if (!ringAt && clutch(b) && b.dish->appearance().clutch.knockMs != ClutchView::kNever) ringAt = b.ms;
  }
  ASSERT_NE(ringAt, 0u) << "the ring never closed";
  EXPECT_LT(ringAt - knockAt, 80u) << "the ring waited for the knock to be decided";
  ASSERT_EQ(clutch(b), nullptr) << "a knock keeps the egg tilted toward";
  EXPECT_EQ(bornOf(b), 1);
  const Appearance a = b.dish->appearance();
  EXPECT_EQ(a.kind, Appearance::Kind::Egg);
  EXPECT_EQ(a.clutch.chosen, 1);
  EXPECT_EQ(a.eggCount, 3) << "the clutch's eggs, for the others to fade";
  b.run(ClutchShow::kChosenMs + 100);
  EXPECT_EQ(b.dish->appearance().clutch.chosen, -1) << "then the plain egg";
}

// A tip is no knock (KnockSense, 0d63eff): a brisk tilt toward an egg, or
// from one egg to the other, points at it and keeps nothing. Before that fix
// a tip of 100 ms or less from a still badge read as one knock about 0.6 s
// later, and a knock while tilted keeps the egg (found filming this picker).
TEST(Picker, ABriskTipPointsButKeepsNothing) {
  for (uint32_t turn : {0u, 40u, 100u}) {
    for (Pose from : {Pose::Flat, Pose::TiltLeft}) {
      Badge b(kHomeDown, std::nullopt, "elder");
      toTheEggs(b);
      b.hand.pose = from;
      b.run(1500);
      ASSERT_NE(clutch(b), nullptr);
      b.hand.turnMs = turn;
      b.hand.pose = Pose::TiltRight;
      b.run(2500);
      ASSERT_NE(clutch(b), nullptr) << "a " << turn << " ms tip kept an egg";
      EXPECT_EQ(clutch(b)->cursor, 1) << "but it points at the egg it tipped toward";
    }
  }
}

// Level, a knock knock (a kid feeding him, as he did yesterday) keeps nothing.
TEST(Picker, LevelAKnockKnockKeepsNothing) {
  Badge b(kHomeDown, std::nullopt, "elder");
  toTheEggs(b);
  b.hand.knocks = {b.ms + 50, b.ms + 350};
  b.run(3000);
  EXPECT_NE(clutch(b), nullptr);
  EXPECT_EQ(b.dish->appearance().clutch.knockMs, ClutchView::kNever) << "no egg to close a ring on";
}

// The frame memo (main.cpp) draws a frame only when the Appearance changed:
// the picker's must change every frame, or its slide and its hints would
// stand still between ticks.
TEST(Picker, EveryFrameOfThePickerIsANewAppearance) {
  Badge b(kHomeDown, std::nullopt, "elder");
  toTheEggs(b);
  Appearance last = b.dish->appearance();
  for (int f = 0; f < 10; ++f) {
    b.run(20);
    const Appearance now = b.dish->appearance();
    EXPECT_NE(std::memcmp(static_cast<const void*>(&now), static_cast<const void*>(&last), sizeof now), 0) << "frame " << f;
    last = now;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
