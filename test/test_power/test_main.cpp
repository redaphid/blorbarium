#include <gtest/gtest.h>
#include <vector>
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
  Policy p(10 * kMin);
  feed(p, kFlat, 0, 10 * kMin - 20);
  EXPECT_EQ(p.mode(), Mode::Full);
  feed(p, kFlat, 10 * kMin - 20, 10 * kMin + 20);
  EXPECT_EQ(p.mode(), Mode::Low);
  EXPECT_EQ(p.why(), Why::Idle);
  EXPECT_EQ(p.changes(), 1u);
}

// The default (the owner, 2026-10-27): dim at 30 s, dark at a minute, the nap
// two minutes after that.
TEST(Power, TheDefaultDimsAtHalfAMinuteAndGoesDarkAtAMinute) {
  Policy p;
  EXPECT_EQ(p.idleMs(), 60u * 1000u);
  EXPECT_EQ(p.dimMs(), 30u * 1000u);
  EXPECT_EQ(p.napMs(), 2 * kMin);
  feed(p, kFlat, 0, 30000 - 20);
  EXPECT_FALSE(p.dim(30000 - 20));
  feed(p, kFlat, 30000 - 20, 30020);
  EXPECT_TRUE(p.dim(30000));
  EXPECT_FALSE(p.low());
  feed(p, kFlat, 30020, 60000 - 20);
  EXPECT_FALSE(p.low());
  feed(p, kFlat, 60000 - 20, 60020);
  EXPECT_TRUE(p.low());
  EXPECT_FALSE(p.dim(60020)) << "dark is not dim";
  EXPECT_EQ(p.why(), Why::Idle);
}

// Dim is the timeout's own: any input brightens it, and a hold never lets it dim.
TEST(Power, InputUndimsAndAHoldNeverDims) {
  Policy p;
  feed(p, kFlat, 0, 40000);
  ASSERT_TRUE(p.dim(40000));
  feed(p, mg(260, 0, 966), 40000, 41000);   // tipped 15 degrees
  EXPECT_FALSE(p.dim(41000));
  EXPECT_EQ(p.msLeft(41000), p.idleMs() - (41000 - p.lastInput()));
  Policy held;
  feed(held, kFlat, 0, 10 * kMin, true);   // a duel: ten minutes held still
  EXPECT_FALSE(held.dim(10 * kMin - 20));
  EXPECT_FALSE(held.low());
}

// Dark for napMs: the nap is due. Input wakes it all the way up, and the next
// dark starts the nap's wait again.
TEST(Power, TheNapIsDueTwoMinutesAfterDark) {
  Policy p;
  feed(p, kFlat, 0, 60020);
  ASSERT_TRUE(p.low());
  const uint32_t dark = p.lowSince();
  EXPECT_GE(dark, 60000u);
  EXPECT_LE(dark, 60020u);
  feed(p, kFlat, 60020, dark + 2 * kMin - 20);
  EXPECT_FALSE(p.napDue(dark + 2 * kMin - 20));
  EXPECT_TRUE(p.napDue(dark + 2 * kMin));
  // A second at a time while it naps: the policy is fed one sample a second.
  for (uint32_t t = dark + 2 * kMin; t < dark + 30 * kMin; t += 1000) {
    p.sample(kFlat, t);
    p.update(t, false);
  }
  EXPECT_TRUE(p.low());
  EXPECT_TRUE(p.napDue(dark + 30 * kMin));
  blorb::BodySample press = kFlat;
  press.buttonDown = true;
  p.sample(press, dark + 30 * kMin);
  EXPECT_FALSE(p.low());
  EXPECT_FALSE(p.napDue(dark + 30 * kMin));
  feed(p, kFlat, dark + 30 * kMin, dark + 31 * kMin + 20);
  ASSERT_TRUE(p.low());
  EXPECT_FALSE(p.napDue(dark + 31 * kMin + 20)) << "a new dark waits its two minutes again";
}

// Napping, the policy sees one sample a second (the IMU's pin wakes it for
// anything quick): a badge picked up and tilted still wakes within seconds.
TEST(Power, OneSampleASecondStillSeesATilt) {
  Policy p;
  feed(p, kFlat, 0, 4 * kMin);
  ASSERT_TRUE(p.napDue(4 * kMin));
  uint32_t t = 4 * kMin;
  for (; t < 5 * kMin; t += 1000) p.sample(kFlat, t), p.update(t, false);
  ASSERT_TRUE(p.low());
  uint32_t woke = 0;
  for (; t < 5 * kMin + 10000 && !woke; t += 1000) {
    p.sample(mg(0, 500, 866), t);   // held up at 30 degrees
    p.update(t, false);
    if (!p.low()) woke = t;
  }
  ASSERT_NE(woke, 0u);
  EXPECT_LE(woke - 5 * kMin, 4000u);
  EXPECT_EQ(p.why(), Why::Drift);
}

// The nap glances at gravity once a wake (a minute apart): a badge lifted
// too gently for the IMU's pin is still a move; the same pose is not, nor is
// a few mg of drift.
TEST(Power, AGlanceAMinuteApartSeesALiftButNotTheDesk) {
  Policy p;
  feed(p, kFlat, 0, 4 * kMin);
  ASSERT_TRUE(p.low());
  uint32_t t = 4 * kMin;
  for (int i = 0; i < 30; ++i, t += kMin) EXPECT_FALSE(p.glance(mg(i % 3, -(i % 5), 1000 + i % 7), t));
  EXPECT_TRUE(p.low());
  EXPECT_TRUE(p.glance(mg(0, 500, 866), t)) << "held up at 30 degrees";
  EXPECT_FALSE(p.low());
  EXPECT_EQ(p.why(), Why::Drift);
  feed(p, mg(0, 500, 866), t, t + 2 * kMin);   // it settles in the new pose
  ASSERT_TRUE(p.low());
  EXPECT_FALSE(p.glance(mg(0, 505, 862), t + 3 * kMin)) << "the new pose is where it settled";
}

// ---- The reminder: 3 to 5 days after the last real interaction, any hour ----

constexpr uint32_t kDay = 24u * 3600 * 1000;

TEST(Reminder, ThreeToFiveDaysAfterAnInteraction) {
  uint32_t rnd = 12345, shortest = UINT32_MAX, longest = 0;
  for (int i = 0; i < 2000; ++i) {
    rnd = rnd * 1664525u + 1013904223u;
    power::Reminder r;
    const uint32_t t0 = rnd % (40u * kDay);   // anywhere on the clock, wrap included
    r.restart(t0, rnd);
    const uint32_t in = r.dueIn(t0);
    shortest = std::min(shortest, in), longest = std::max(longest, in);
    EXPECT_FALSE(r.due(t0 + in - 1));
    EXPECT_TRUE(r.due(t0 + in));
  }
  EXPECT_GE(shortest, 3 * kDay);
  EXPECT_LE(longest, 5 * kDay);
  EXPECT_LT(shortest, 3 * kDay + kDay / 4) << "the whole range is used";
  EXPECT_GT(longest, 5 * kDay - kDay / 4);
}

// Restarts and naps neither reset nor lose it: the board keeps remainingS in
// NVS and resumes it at boot, on a millis() that starts again at 0.
TEST(Reminder, ItsCountdownSurvivesARestart) {
  power::Reminder r;
  r.restart(1000, 7);
  const uint32_t dueAt = 1000 + r.dueIn(1000);
  const uint32_t kept = r.remainingS(1000 + 2 * kDay);   // saved at a nap two days on
  power::Reminder after;   // the restart
  after.resume(0, kept);
  EXPECT_NEAR(double(after.dueIn(0)), double(dueAt - (1000 + 2 * kDay)), 1000.0);
  power::Reminder silly;
  silly.resume(0, UINT32_MAX);   // a garbled NVS value
  EXPECT_LE(silly.dueIn(0), 5 * kDay);
  power::Reminder now;
  now.resume(5000, 0);   // `DEBUG power remind`
  EXPECT_TRUE(now.due(5000));
}

// The board's loop, as main.cpp runs it: an interaction starts the countdown
// over, and a dark due badge lights for a minute. Thirty days played with
// every day: no reminder. Thirty days alone: one every 3 to 5 days.
int remindersOver30Days(bool playedWith, std::vector<uint32_t>* at = nullptr) {
  Policy p;
  power::Reminder r;
  uint32_t seen = p.interactions(), rnd = 99;
  r.restart(0, rnd);
  int shown = 0;
  feed(p, kFlat, 0, 4 * kMin);
  for (uint32_t t = 4 * kMin; t < 30 * kDay; t += 60000) {   // a pass a minute: the nap's longest sleep
    if (playedWith && t % kDay == 12 * 3600 * 1000) {
      blorb::BodySample press = kFlat;
      press.buttonDown = true;
      p.sample(press, t);
    }
    p.update(t, false);
    if (p.interactions() != seen) seen = p.interactions(), r.restart(t, rnd = rnd * 1664525u + 1013904223u);
    if (r.due(t) && p.low()) {
      EXPECT_TRUE(p.nudge(t, power::kReminderLitMs));
      EXPECT_EQ(p.interactions(), seen) << "a reminder is no interaction";
      r.restart(t, rnd = rnd * 1664525u + 1013904223u);
      ++shown;
      if (at) at->push_back(t);
    }
  }
  return shown;
}

TEST(Reminder, APlayedWithBadgeNeverNeedsOneAndALeftOneGetsOneEvery3To5Days) {
  EXPECT_EQ(remindersOver30Days(true), 0);
  std::vector<uint32_t> at;
  const int alone = remindersOver30Days(false, &at);
  EXPECT_GE(alone, 6);
  EXPECT_LE(alone, 10);
  for (size_t i = 1; i < at.size(); ++i) {
    EXPECT_GE(at[i] - at[i - 1], 3 * kDay);
    EXPECT_LE(at[i] - at[i - 1], 5 * kDay + 2 * 60000);
  }
}

// The minute itself: full brightness (never dim), then dark and straight into
// the nap. A hand during it is an ordinary wake with the whole timeout.
TEST(Reminder, ItsMinuteIsLitUndimmedThenNapsAndAHandMakesItAWake) {
  Policy p;
  feed(p, kFlat, 0, 4 * kMin);
  ASSERT_TRUE(p.low());
  uint32_t t = 4 * kMin;
  EXPECT_FALSE(Policy().nudge(0, 1000)) << "only from dark";
  ASSERT_TRUE(p.nudge(t, power::kReminderLitMs));
  EXPECT_FALSE(p.low());
  EXPECT_EQ(p.why(), Why::Reminder);
  EXPECT_STREQ(power::whyName(p.why()), "reminder");
  for (uint32_t u = t; u < t + 60000 - 40; u += 20) {
    p.sample(kFlat, u), p.update(u, false);
    ASSERT_FALSE(p.low()) << "lit the whole minute, at " << u - t;
    ASSERT_FALSE(p.dim(u)) << "never dimmed, at " << u - t;
  }
  feed(p, kFlat, t + 60000 - 40, t + 60000 + 40);
  EXPECT_TRUE(p.low());
  EXPECT_TRUE(p.napDue(t + 60000 + 40)) << "back to the nap at once, not two minutes dark first";

  Policy q;
  feed(q, kFlat, 0, 4 * kMin);
  ASSERT_TRUE(q.nudge(t, power::kReminderLitMs));
  const uint32_t before = q.interactions();
  feed(q, kFlat, t, t + 10000);
  q.sample(mg(0, 500, 866), t + 10000);   // picked up
  feed(q, mg(0, 500, 866), t + 10020, t + 11000);
  EXPECT_FALSE(q.nudging());
  EXPECT_GT(q.interactions(), before);
  feed(q, mg(0, 500, 866), t + 11000, t + 10000 + q.idleMs() - 100);
  EXPECT_FALSE(q.low()) << "a whole timeout from the hand, not the reminder's minute";
}

// The nap sleeps to whichever is first: the reminder, the Wi-Fi check, a minute.
TEST(Reminder, IsANapDeadlineLikeTheWifiCheck) {
  power::Reminder r;
  r.resume(0, 30);
  EXPECT_EQ(std::min(power::napSleepMs(UINT32_MAX), r.dueIn(0)), 30000u);
  EXPECT_EQ(std::min(power::napSleepMs(20000), r.dueIn(0)), 20000u);
  r.resume(0, 3 * 24 * 3600);
  EXPECT_EQ(std::min(power::napSleepMs(UINT32_MAX), r.dueIn(0)), power::kNapCapMs);
}

// The debug links' `power nap`: dark and the nap due at once.
TEST(Power, AskedToNapNapsAtOnce) {
  Policy p;
  feed(p, kFlat, 0, 1000);
  p.sleepNow(1000, true);
  EXPECT_TRUE(p.low());
  EXPECT_TRUE(p.napDue(1000));
  EXPECT_EQ(p.why(), Why::Asked);
  Policy q;
  feed(q, kFlat, 0, 1000);
  q.sleepNow(1000);
  EXPECT_FALSE(q.napDue(1000));
  EXPECT_TRUE(q.napDue(1000 + 2 * kMin));
}

// The nap never takes the radio from the scheduled Wi-Fi check, an OTA
// trial's confirm, a phone or an update: each keeps it from napping, and it
// naps once they are done.
TEST(Power, TheNapWaitsForWifiTheTrialAPhoneAndTheCable) {
  Policy p;
  feed(p, kFlat, 0, 4 * kMin);
  const uint32_t t = 4 * kMin;
  ASSERT_TRUE(p.napDue(t));
  EXPECT_EQ(power::napBlocker(p, t, {}), nullptr);
  power::NapFacts f;
  f.wifi = true;
  EXPECT_STREQ(power::napBlocker(p, t, f), "wifi");
  f = {}, f.trial = true;
  EXPECT_STREQ(power::napBlocker(p, t, f), "trial");
  f = {}, f.phone = true;
  EXPECT_STREQ(power::napBlocker(p, t, f), "phone");
  f = {}, f.update = true;
  EXPECT_STREQ(power::napBlocker(p, t, f), "update");
  f = {}, f.cable = true;
  EXPECT_STREQ(power::napBlocker(p, t, f), "cable");
  f = {}, f.imu = false;
  EXPECT_STREQ(power::napBlocker(p, t, f), "no_imu") << "nothing but BOOT could wake it";
  f = {}, f.held = true;
  EXPECT_STREQ(power::napBlocker(p, t, f), "held");
  f = {}, f.air = true;
  EXPECT_STREQ(power::napBlocker(p, t, f), "air") << "fight mode's radio is never slept through";
  Policy awake;
  feed(awake, kFlat, 0, 1000);
  EXPECT_STREQ(power::napBlocker(awake, 1000, {}), "awake");
  Policy dark;
  feed(dark, kFlat, 0, 61000);
  EXPECT_STREQ(power::napBlocker(dark, 61000, {}), "not_due");
}

// Face down is bedtime: dark 15 s after it was laid down, not the full timeout.
TEST(Power, FaceDownGoesDarkAfterFifteenSeconds) {
  Policy p(10 * kMin);
  feed(p, kFlat, 0, 1000);
  feed(p, mg(0, 0, -1000), 1000, 1000 + 15000 - 500);   // turned over at 1 s: that move is the last input
  EXPECT_EQ(p.mode(), Mode::Full);
  feed(p, mg(0, 0, -1000), 1000 + 15000 - 500, 1000 + 15000 + 1000);
  EXPECT_EQ(p.mode(), Mode::Low);
  EXPECT_EQ(p.why(), Why::FaceDown);
  EXPECT_STREQ(power::whyName(p.why()), "facedown");
}

// Face up, or held on edge, the full ten minutes still apply.
TEST(Power, FaceUpOrOnEdgeKeepsTheFullTimeout) {
  for (const blorb::BodySample& s : {kFlat, mg(0, 1000, 0), mg(0, -600, -700)}) {
    Policy p(10 * kMin);
    feed(p, s, 0, 3 * kMin);
    EXPECT_EQ(p.mode(), Mode::Full) << s.az;
  }
}

TEST(Power, ThermalDriftAndNoiseNeverWake) {
  Policy p(10 * kMin);
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
  Policy p(10 * kMin);
  feed(p, kFlat, 0, 11 * kMin);
  ASSERT_TRUE(p.low());
  feed(p, mg(260, 0, 966), 11 * kMin, 11 * kMin + 1000);   // tipped 15 degrees, slowly enough to be no jolt
  EXPECT_EQ(p.mode(), Mode::Full);
  EXPECT_EQ(p.why(), Why::Drift);
}

TEST(Power, AShakeWakesIt) {
  Policy p(10 * kMin);
  feed(p, kFlat, 0, 11 * kMin);
  ASSERT_TRUE(p.low());
  p.sample(mg(0, 500, 1000), 11 * kMin);   // half a g sideways...
  p.sample(kFlat, 11 * kMin + 20);
  p.sample(mg(0, -500, 1000), 11 * kMin + 40);   // ...and back: a hand
  EXPECT_EQ(p.mode(), Mode::Full);
  EXPECT_EQ(p.why(), Why::Jolt);
}

TEST(Power, OneBumpOfTheTableIsNoInput) {
  Policy p(10 * kMin);
  feed(p, kFlat, 0, 11 * kMin);
  ASSERT_TRUE(p.low());
  for (uint32_t t = 11 * kMin; t < 12 * kMin; t += 2000) {   // a bump every 2 s for a minute
    EXPECT_FALSE(p.sample(mg(0, 0, 1500), t));
    feed(p, kFlat, t + 20, t + 2000);
  }
  EXPECT_TRUE(p.low());
}

TEST(Power, StrayTapsAreNoInputButAKnockKnockIs) {
  Policy p(10 * kMin);
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
    Policy p(10 * kMin);
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
  Policy p(10 * kMin);
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
  Policy p(10 * kMin);
  feed(p, kFlat, 0, 1000);
  feed(p, mg(0, -1000, 0), 1000, 3000);   // stood on its edge: one wake's worth of input
  const uint32_t at = p.lastInput();
  feed(p, mg(0, -1000, 0), 3000, 3000 + 11 * kMin);
  EXPECT_TRUE(p.low()) << "standing still on its edge is no longer input";
  EXPECT_LT(at, 3000u);
}

TEST(Power, HoldKeepsItAwakeAndWakesIt) {
  Policy p(10 * kMin);
  feed(p, kFlat, 0, 30 * kMin, true);
  EXPECT_FALSE(p.low());
  feed(p, kFlat, 30 * kMin, 41 * kMin);
  ASSERT_TRUE(p.low());
  p.update(41 * kMin, true);
  EXPECT_FALSE(p.low());
  EXPECT_EQ(p.why(), Why::Hold);
}

TEST(Power, APhoneWakesAnIdleBadgeButNotOneAskedToSleep) {
  Policy p(10 * kMin);
  feed(p, kFlat, 0, 11 * kMin);
  ASSERT_TRUE(p.low());
  p.phone(11 * kMin);
  EXPECT_FALSE(p.low());
  EXPECT_EQ(p.why(), Why::Phone);
  p.sleepNow(11 * kMin + 50);
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
  p.sleepNow(1000);
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
