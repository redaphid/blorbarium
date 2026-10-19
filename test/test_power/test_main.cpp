#include <gtest/gtest.h>
#include "blorb/senses.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "../../src/hw/power.h"
#include "../../src/hw/turn_yes.h"

namespace {

using power::Mode;
using power::Policy;
using power::Why;

constexpr uint32_t kMin = 60u * 1000u;

blorb::BodySample mg(int ax, int ay, int az) {
  blorb::BodySample s;
  s.ax = int16_t(ax), s.ay = int16_t(ay), s.az = int16_t(az);
  return s;
}
const blorb::BodySample kFlat = mg(0, 0, 1000);

// The loop at 50 Hz, from `from` up to but not including `to`, sampling `s`.
void feed(Policy& p, const blorb::BodySample& s, uint32_t from, uint32_t to, bool held = false) {
  for (uint32_t t = from; t < to; t += 20) {
    p.sample(s, t);
    p.update(t, held);
  }
}

TEST(Power, StillForTenMinutesGoesLow) {
  Policy p;
  feed(p, kFlat, 0, 10 * kMin - 20);
  EXPECT_EQ(p.mode(), Mode::Full);
  feed(p, kFlat, 10 * kMin - 20, 10 * kMin + 20);
  EXPECT_EQ(p.mode(), Mode::Low);
  EXPECT_EQ(p.why(), Why::Idle);
  EXPECT_EQ(p.changes(), 1u);
}

// Face down is bedtime: low power two minutes after it was laid down, not ten.
TEST(Power, FaceDownGoesLowAfterTwoMinutes) {
  Policy p;
  feed(p, kFlat, 0, 1000);
  feed(p, mg(0, 0, -1000), 1000, 1000 + 2 * kMin - 500);   // turned over at 1 s: that move is the last input
  EXPECT_EQ(p.mode(), Mode::Full);
  feed(p, mg(0, 0, -1000), 1000 + 2 * kMin - 500, 1000 + 2 * kMin + 1000);
  EXPECT_EQ(p.mode(), Mode::Low);
  EXPECT_EQ(p.why(), Why::FaceDown);
  EXPECT_STREQ(power::whyName(p.why()), "facedown");
}

// Face up, or held on edge, the full ten minutes still apply.
TEST(Power, FaceUpOrOnEdgeKeepsTheFullTimeout) {
  for (const blorb::BodySample& s : {kFlat, mg(0, 1000, 0), mg(0, -600, -700)}) {
    Policy p;
    feed(p, s, 0, 3 * kMin);
    EXPECT_EQ(p.mode(), Mode::Full) << s.az;
  }
}

TEST(Power, ThermalDriftAndNoiseNeverWake) {
  Policy p;
  feed(p, kFlat, 0, 10 * kMin + 20);
  ASSERT_TRUE(p.low());
  // An hour of a slow 60 mg creep plus +-20 mg of noise: below every threshold.
  for (uint32_t t = 10 * kMin + 20, i = 0; t < 70 * kMin; t += 20, ++i) {
    const int creep = int((t - 10 * kMin) / (60 * kMin / 60 + 1));
    const int noise = int(i * 2654435761u >> 27) % 41 - 20;
    p.sample(mg(creep + noise, noise, 1000 - noise), t);
    p.update(t, false);
  }
  EXPECT_TRUE(p.low());
  EXPECT_EQ(p.changes(), 1u);
}

TEST(Power, TiltingTheBadgeWakesIt) {
  Policy p;
  feed(p, kFlat, 0, 11 * kMin);
  ASSERT_TRUE(p.low());
  feed(p, mg(260, 0, 966), 11 * kMin, 11 * kMin + 1000);   // tipped 15 degrees, slowly enough to be no jolt
  EXPECT_EQ(p.mode(), Mode::Full);
  EXPECT_EQ(p.why(), Why::Drift);
}

TEST(Power, AShakeWakesIt) {
  Policy p;
  feed(p, kFlat, 0, 11 * kMin);
  ASSERT_TRUE(p.low());
  p.sample(mg(0, 500, 1000), 11 * kMin);   // half a g sideways...
  p.sample(kFlat, 11 * kMin + 20);
  p.sample(mg(0, -500, 1000), 11 * kMin + 40);   // ...and back: a hand
  EXPECT_EQ(p.mode(), Mode::Full);
  EXPECT_EQ(p.why(), Why::Jolt);
}

TEST(Power, OneBumpOfTheTableIsNoInput) {
  Policy p;
  feed(p, kFlat, 0, 11 * kMin);
  ASSERT_TRUE(p.low());
  for (uint32_t t = 11 * kMin; t < 12 * kMin; t += 2000) {   // a bump every 2 s for a minute
    EXPECT_FALSE(p.sample(mg(0, 0, 1500), t));
    feed(p, kFlat, t + 20, t + 2000);
  }
  EXPECT_TRUE(p.low());
}

TEST(Power, StrayTapsAreNoInputButAKnockKnockIs) {
  Policy p;
  feed(p, kFlat, 0, 11 * kMin);
  ASSERT_TRUE(p.low());
  blorb::BodySample tap = kFlat;
  tap.tapCode = 1;
  for (uint32_t t = 11 * kMin; t < 12 * kMin; t += 3000) {   // typing on the desk: a tap every 3 s
    p.sample(tap, t);
    feed(p, kFlat, t + 20, t + 3000);
  }
  EXPECT_TRUE(p.low()) << "a tap every few seconds is the desk";
  p.sample(tap, 12 * kMin);
  feed(p, kFlat, 12 * kMin + 20, 12 * kMin + 600);
  p.sample(tap, 12 * kMin + 600);
  EXPECT_FALSE(p.low()) << "two taps in a second and a half: a knock knock";
  EXPECT_EQ(p.why(), Why::Tap);
  feed(p, kFlat, 12 * kMin, 23 * kMin);
  ASSERT_TRUE(p.low());
  tap.tapCode = 2;   // the tap engine's own double tap
  p.sample(tap, 23 * kMin);
  EXPECT_FALSE(p.low());
}

TEST(Power, ButtonAndTapWake) {
  for (const bool button : {true, false}) {
    Policy p;
    feed(p, kFlat, 0, 11 * kMin);
    ASSERT_TRUE(p.low());
    blorb::BodySample s = kFlat;
    if (button) s.buttonDown = true;
    else s.tapCode = 2;
    p.sample(s, 11 * kMin);
    EXPECT_EQ(p.why(), button ? Why::Button : Why::Tap);
    EXPECT_FALSE(p.low());
  }
}

TEST(Power, InputRestartsTheTenMinutes) {
  Policy p;
  feed(p, kFlat, 0, 9 * kMin);
  blorb::BodySample press = kFlat;
  press.buttonDown = true;
  p.sample(press, 9 * kMin);
  feed(p, kFlat, 9 * kMin, 18 * kMin);
  EXPECT_FALSE(p.low()) << "nine minutes after the press";
  EXPECT_EQ(p.msLeft(18 * kMin), 1 * kMin);
  feed(p, kFlat, 18 * kMin, 19 * kMin + 20);
  EXPECT_TRUE(p.low());
}

TEST(Power, AMovedBadgeSettlesInItsNewPose) {
  Policy p;
  feed(p, kFlat, 0, 1000);
  feed(p, mg(0, -1000, 0), 1000, 3000);   // stood on its edge: one wake's worth of input
  const uint32_t at = p.lastInput();
  feed(p, mg(0, -1000, 0), 3000, 3000 + 11 * kMin);
  EXPECT_TRUE(p.low()) << "standing still on its edge is no longer input";
  EXPECT_LT(at, 3000u);
}

TEST(Power, HoldKeepsItAwakeAndWakesIt) {
  Policy p;
  feed(p, kFlat, 0, 30 * kMin, true);
  EXPECT_FALSE(p.low());
  feed(p, kFlat, 30 * kMin, 41 * kMin);
  ASSERT_TRUE(p.low());
  p.update(41 * kMin, true);
  EXPECT_FALSE(p.low());
  EXPECT_EQ(p.why(), Why::Hold);
}

TEST(Power, APhoneWakesAnIdleBadgeButNotOneAskedToSleep) {
  Policy p;
  feed(p, kFlat, 0, 11 * kMin);
  ASSERT_TRUE(p.low());
  p.phone(11 * kMin);
  EXPECT_FALSE(p.low());
  EXPECT_EQ(p.why(), Why::Phone);
  p.sleepNow();
  EXPECT_TRUE(p.low());
  p.phone(11 * kMin + 100);
  EXPECT_TRUE(p.low()) << "the page that asked must not wake it with its next line";
  blorb::BodySample press = kFlat;
  press.buttonDown = true;
  p.sample(press, 11 * kMin + 200);
  EXPECT_FALSE(p.low());
  EXPECT_EQ(p.why(), Why::Button);
}

TEST(Power, AskedLowStaysLowUntilInputThenTimesOutAgain) {
  Policy p(5000);
  feed(p, kFlat, 0, 1000);
  p.sleepNow();
  feed(p, kFlat, 1000, 60000);
  EXPECT_TRUE(p.low());
  p.wakeNow(60000);
  EXPECT_FALSE(p.low());
  feed(p, kFlat, 60000, 64000);
  EXPECT_FALSE(p.low());
  feed(p, kFlat, 64000, 65020);
  EXPECT_TRUE(p.low());
  EXPECT_EQ(p.why(), Why::Idle);
}

TEST(Power, AnInputStampedAfterTheLoopsNowIsNoTimeAtAll) {
  // The debug link wakes it at millis(), then the loop updates with the now it
  // took at the top of the pass, a few ms earlier: it must not read as 49 days idle.
  Policy p(20000);
  feed(p, kFlat, 0, 1000);
  p.wakeNow(5000);
  EXPECT_FALSE(p.update(4990, false));
  EXPECT_FALSE(p.low());
  EXPECT_EQ(p.msLeft(4990), 20000u);
  p.sample(kFlat, 4995);   // an older body sample does not move the input back
  EXPECT_EQ(p.lastInput(), 5000u);
  feed(p, kFlat, 5000, 25000);
  EXPECT_FALSE(p.low());
  feed(p, kFlat, 25000, 25040);
  EXPECT_TRUE(p.low());
}

}  // namespace

// ---- Turn me around: the update's yes without the button ----------------------------

TEST(TurnYes, AQuarterTurnAfterTheFirstMomentSaysYesOnce) {
  hw::TurnYes t;
  EXPECT_FALSE(t.sample(true, 0, 1000));   // the question appears, held at 0
  EXPECT_FALSE(t.sample(true, 0, 2000));
  EXPECT_TRUE(t.sample(true, 1, 3000));    // turned
  EXPECT_FALSE(t.sample(true, 1, 3100));   // once
}

TEST(TurnYes, NoTurnNoYes) {
  hw::TurnYes t;
  for (uint32_t ms = 0; ms < 30000; ms += 20) EXPECT_FALSE(t.sample(true, 2, ms));
}

TEST(TurnYes, ATurnAlreadyUnderWayIsNotAYesButTheNextOneIs) {
  hw::TurnYes t;
  EXPECT_FALSE(t.sample(true, 0, 1000));
  EXPECT_FALSE(t.sample(true, 1, 1400));   // settled 0.4 s after the question: already turning
  EXPECT_FALSE(t.sample(true, 1, 3000));
  EXPECT_TRUE(t.sample(true, 2, 3500));    // a real turn later
}

TEST(TurnYes, OnlyWhileItAsks) {
  hw::TurnYes t;
  EXPECT_FALSE(t.sample(false, 0, 0));
  EXPECT_FALSE(t.sample(false, 1, 5000));   // turning with no question is nothing
  EXPECT_FALSE(t.sample(true, 1, 6000));    // a new question starts from where it is
  EXPECT_FALSE(t.sample(false, 3, 9000));   // the question went away
  EXPECT_FALSE(t.sample(true, 3, 9100));
  EXPECT_TRUE(t.sample(true, 0, 11000));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
