#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "blorb/senses.h"

using namespace blorb;

namespace {

struct Step { uint32_t ms; BodySample s; };

// A gesture script, sampled every 20 ms like the firmware.
class Gesture {
 public:
  Gesture() { rest_.az = 1000; }
  Gesture& still(uint32_t durMs) {
    for (uint32_t t = 0; t < durMs; t += kSampleMs) emit(rest_);
    return *this;
  }
  Gesture& pose(int16_t ax, int16_t ay, int16_t az) {
    rest_.ax = ax; rest_.ay = ay; rest_.az = az;
    return *this;
  }
  Gesture& tilt(int16_t ax, int16_t ay) {
    return pose(ax, ay, int16_t(std::lround(std::sqrt(1e6 - double(ax) * ax - double(ay) * ay))));
  }
  Gesture& flip() { return pose(0, 0, -1000); }
  Gesture& right() { return pose(0, 0, 1000); }
  Gesture& jolt(int16_t dz) {
    BodySample s = rest_;
    s.az = int16_t(s.az + dz);
    emit(s);
    return *this;
  }
  // Jolts alternate up and down, one sample each, periodMs apart.
  Gesture& shake(int jolts, uint32_t periodMs) {
    for (int i = 0; i < jolts; ++i) {
      jolt(i % 2 == 0 ? 1000 : -600);
      still(periodMs - kSampleMs);
    }
    return *this;
  }
  Gesture& tap(uint8_t code) {
    BodySample s = rest_;
    s.tapCode = code;
    emit(s);
    return *this;
  }
  Gesture& press(uint32_t durMs) {
    BodySample s = rest_;
    s.buttonDown = true;
    for (uint32_t t = 0; t < durMs; t += kSampleMs) emit(s);
    return *this;
  }
  Gesture& fall(uint32_t durMs) {
    for (uint32_t t = 0; t < durMs; t += kSampleMs) emit(BodySample{});
    return *this;
  }
  Gesture& temp(int16_t cx10) { rest_.tempCx10 = cx10; return *this; }
  Gesture& touch(bool down) { rest_.touchDown = down; return *this; }
  uint32_t now() const { return ms_; }
  const std::vector<Step>& steps() const { return steps_; }

 private:
  void emit(const BodySample& s) { steps_.push_back({ms_, s}); ms_ += kSampleMs; }
  BodySample rest_{};
  uint32_t ms_ = 10000;
  std::vector<Step> steps_;
};

struct Fired { uint32_t ms; StimId stim; };

struct Heard {
  std::vector<Fired> fired;
  Fx locus[256]{};
  PetClock clock;
  int count(StimId id) const {
    int n = 0;
    for (const Fired& f : fired) n += f.stim == id;
    return n;
  }
  uint32_t firstAt(StimId id) const {
    for (const Fired& f : fired) if (f.stim == id) return f.ms;
    return 0;
  }
  double at(LocusId id) const { return double(locus[id.v].raw) / Fx::kOne; }
};

PetClock clockAt(uint32_t hour, uint32_t minute = 0) {
  PetClock c;
  c.alignToWall(hour * 3600 + minute * 60, 0);
  return c;
}

// Plays a gesture into every detector the way the Dish does: samples into a
// pending SenseOut, then each 100 ms the pet clock advances, the tick-rate
// detectors run, and the tick's output is taken. everyMs > 20 feeds only the
// samples on that grid, offset by phaseMs.
Heard play(const Gesture& g, uint32_t everyMs = kSampleMs, uint32_t phaseMs = 0, PetClock clock = clockAt(12)) {
  Detectors d;
  SenseOut out;
  Heard r;
  uint32_t start = g.steps().front().ms, tick = 0, nextTickMs = start + kTickMs;
  auto collect = [&](uint32_t ms) {
    clock.advance(d.lid.lidded());
    d.tick(TickContext{clock, false, tick++}, out);
    for (uint8_t i = 0; i < out.stimCount; ++i) r.fired.push_back({ms, out.stimuli[i]});
    for (uint8_t i = 0; i < out.lociCount; ++i) r.locus[out.loci[i].locus.v] = out.loci[i].value;
    out.clear();
  };
  for (const Step& st : g.steps()) {
    if ((st.ms - start + phaseMs) % everyMs == 0) d.sample(st.s, st.ms, out);
    while (st.ms + kSampleMs >= nextTickMs) { collect(nextTickMs); nextTickMs += kTickMs; }
  }
  r.clock = clock;
  return r;
}

}  // namespace

TEST(Shake, FourJoltsAt50HzAreAShake) {
  Heard r = play(Gesture().still(500).shake(6, 80).still(1000));
  EXPECT_EQ(r.count(stim::shake), 1);
  EXPECT_GT(r.at(locus::motion), 0.0);
}

TEST(Shake, TheSameShakeSampledAt10HzIsMissed) {
  Gesture g = Gesture().still(500).shake(12, 80).still(1000);
  ASSERT_EQ(play(g).count(stim::shake), 1);
  for (uint32_t phase = 0; phase < kTickMs; phase += kSampleMs)
    EXPECT_EQ(play(g, kTickMs, phase).count(stim::shake), 0) << "phase " << phase;
}

TEST(Shake, ThreeJoltsAreNotAShake) {
  EXPECT_EQ(play(Gesture().still(500).shake(3, 80).still(1000)).count(stim::shake), 0);
}

TEST(Shake, FourJoltsSpreadPastTheWindowAreNotAShake) {
  EXPECT_EQ(play(Gesture().still(500).shake(4, 320).still(1000)).count(stim::shake), 0);
}

TEST(Shake, SamplesOfOneBumpCloserThanTheGapAreOneJolt) {
  Gesture g;
  g.still(500);
  for (int i = 0; i < 3; ++i) g.jolt(1000).jolt(1000).jolt(1000).still(200);
  EXPECT_EQ(play(g.still(1000)).count(stim::shake), 0);
}

TEST(Shake, KeptUpForSecondsIsOneShakeUntilItRests) {
  EXPECT_EQ(play(Gesture().still(500).shake(40, 80).still(1000)).count(stim::shake), 1);
  EXPECT_EQ(play(Gesture().still(500).shake(6, 80).still(700).shake(6, 80).still(500)).count(stim::shake), 2);
  EXPECT_EQ(play(Gesture().still(500).shake(6, 80).still(400).shake(6, 80).still(500)).count(stim::shake), 1);
}

TEST(Knock, TapCodesBecomeKnockAndDoubleKnock) {
  Heard r = play(Gesture().still(200).tap(1).still(600).tap(2).still(200));
  EXPECT_EQ(r.count(stim::knock), 1);
  EXPECT_EQ(r.count(stim::double_knock), 1);
}

TEST(Knock, TapsInside400MsAreOneKnock) {
  EXPECT_EQ(play(Gesture().still(200).tap(1).still(200).tap(1).still(200)).count(stim::knock), 1);
  EXPECT_EQ(play(Gesture().still(200).tap(1).still(420).tap(1).still(200)).count(stim::knock), 2);
}

TEST(Held, TiltedFor400MsIsPickedUpAndFlatFor1500MsIsPutDown) {
  Heard r = play(Gesture().still(1000).tilt(600, 0).still(1000).right().still(1000));
  EXPECT_EQ(r.count(stim::picked_up), 1);
  EXPECT_EQ(r.count(stim::put_down), 0);
  EXPECT_EQ(r.at(locus::held), 1.0);

  r = play(Gesture().still(1000).tilt(600, 0).still(1000).right().still(1600));
  EXPECT_EQ(r.count(stim::put_down), 1);
  EXPECT_EQ(r.at(locus::held), 0.0);
}

TEST(Held, ABriefTiltIsNotAPickUp) {
  EXPECT_EQ(play(Gesture().still(1000).tilt(600, 0).still(300).right().still(1000)).count(stim::picked_up), 0);
}

TEST(Cradle, HeldStillAndUprightFor3SecondsThenEvery10) {
  Gesture g = Gesture().still(500).tilt(500, 0);
  uint32_t heldAt = g.now();
  Heard r = play(g.still(2800));
  EXPECT_EQ(r.count(stim::cradle), 0);
  EXPECT_EQ(r.at(locus::cradled), 0.0);

  r = play(g.still(11000));
  EXPECT_EQ(r.count(stim::cradle), 2);
  EXPECT_EQ(r.at(locus::cradled), 1.0);
  EXPECT_GE(r.firstAt(stim::cradle) - heldAt, 3000u);
}

TEST(Cradle, JigglingRestartsTheWait) {
  Gesture g = Gesture().still(500).tilt(500, 0);
  for (int i = 0; i < 5; ++i) g.still(2000).jolt(300);
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::cradle), 0);
  EXPECT_EQ(r.count(stim::picked_up), 1);
}

TEST(Flip, FaceDownIsFlippedAndFaceUpIsRighted) {
  Heard r = play(Gesture().still(300).flip().still(500).right().still(300));
  EXPECT_EQ(r.count(stim::flipped), 1);
  EXPECT_EQ(r.count(stim::righted), 1);
  EXPECT_LT(r.firstAt(stim::flipped), r.firstAt(stim::righted));
  EXPECT_EQ(r.at(locus::upside_down), 0.0);
}

TEST(Flip, AJoltWhileFaceDownIsNotARighting) {
  Heard r = play(Gesture().still(300).flip().still(300).jolt(2600).still(300));
  EXPECT_EQ(r.count(stim::flipped), 1);
  EXPECT_EQ(r.count(stim::righted), 0);
  EXPECT_EQ(r.at(locus::upside_down), 1.0);
}

TEST(Drop, FreeFallThenImpactIsDroppedAndItsBounceIsNotAShake) {
  Heard r = play(Gesture().still(300).fall(200).jolt(1000).still(60).jolt(-600).still(500));
  EXPECT_EQ(r.count(stim::dropped), 1);
  EXPECT_EQ(r.count(stim::shake), 0);
}

TEST(Drop, ImpactWithoutAFallIsNotDropped) {
  EXPECT_EQ(play(Gesture().still(300).jolt(1000).still(500)).count(stim::dropped), 0);
}

TEST(Drop, ABriefDipIsNotAFall) {
  EXPECT_EQ(play(Gesture().still(300).fall(60).jolt(1000).still(500)).count(stim::dropped), 0);
}

TEST(Drop, AFallCaughtSoftlyIsNotDropped) {
  EXPECT_EQ(play(Gesture().still(300).fall(200).still(1000).jolt(1000).still(300)).count(stim::dropped), 0);
}

TEST(Lid, FaceDownFor2SecondsIsLidDownAndDarkensLight) {
  Heard r = play(Gesture().still(300).flip().still(1900));
  EXPECT_EQ(r.count(stim::lid_down), 0);
  EXPECT_EQ(r.at(locus::light), 1.0);

  r = play(Gesture().still(300).flip().still(2100));
  EXPECT_EQ(r.count(stim::lid_down), 1);
  EXPECT_EQ(r.at(locus::light), 0.0);

  r = play(Gesture().still(300).flip().still(2100).right().still(400));
  EXPECT_EQ(r.count(stim::lid_up), 1);
  EXPECT_EQ(r.at(locus::light), 1.0);
}

TEST(Lid, AKnockOnALiddedDishIsNotLidUp) {
  Heard r = play(Gesture().still(300).flip().still(2100).jolt(1500).still(500));
  EXPECT_EQ(r.count(stim::lid_down), 1);
  EXPECT_EQ(r.count(stim::lid_up), 0);
}

TEST(Button, AShortPressIsButton) {
  Heard r = play(Gesture().still(200).press(300).still(200));
  EXPECT_EQ(r.count(stim::button), 1);
  EXPECT_EQ(r.count(stim::button_hold), 0);
}

TEST(Button, HoldingFor1200MsIsButtonHoldOnceAndNoButton) {
  Gesture g = Gesture().still(200);
  uint32_t downAt = g.now();
  Heard r = play(g.press(3000).still(200));
  EXPECT_EQ(r.count(stim::button_hold), 1);
  EXPECT_EQ(r.count(stim::button), 0);
  EXPECT_GE(r.firstAt(stim::button_hold) - downAt, 1200u);
  EXPECT_LE(r.firstAt(stim::button_hold) - downAt, 1300u);
}

TEST(Warmth, UnknownTemperatureIsZeroAndWarmingRaisesIt) {
  EXPECT_EQ(play(Gesture().still(500)).at(locus::warmth), 0.0);
  Heard r = play(Gesture().temp(250).still(2000).temp(265).still(2000));
  EXPECT_GT(r.at(locus::warmth), 0.4);
  EXPECT_EQ(play(Gesture().temp(250).still(4000)).at(locus::warmth), 0.0);
}

TEST(Touch, SilentUntilTouched) {
  Heard r = play(Gesture().still(500));
  EXPECT_EQ(r.count(stim::touched), 0);
  r = play(Gesture().still(200).touch(true).still(200).touch(false).still(200));
  EXPECT_EQ(r.count(stim::touched), 1);
  EXPECT_EQ(r.at(locus::touch), 0.0);
}

TEST(SenseOut, ARepeatedStimulusInOneTickMerges) {
  SenseOut out;
  out.fire(stim::knock);
  out.fire(stim::knock);
  out.fire(stim::shake);
  EXPECT_EQ(out.stimCount, 2);
  out.set(locus::motion, Fx::ratio(1, 4));
  out.set(locus::motion, Fx::ratio(3, 4));
  ASSERT_EQ(out.lociCount, 1);
  EXPECT_EQ(out.loci[0].value, Fx::ratio(3, 4));
  out.clear();
  EXPECT_EQ(out.stimCount, 0);
  EXPECT_EQ(out.lociCount, 0);
}

TEST(SenseOut, OverflowDropsInsteadOfWritingPastTheEnd) {
  SenseOut out;
  for (uint8_t i = 0; i < 40; ++i) { out.fire(StimId{i}); out.set(LocusId{i}, Fx::one()); }
  EXPECT_EQ(out.stimCount, 16);
  EXPECT_EQ(out.lociCount, 24);
}

namespace {
uint32_t todTicks(const PetClock& c) { return uint32_t((uint64_t(c.dayFraction().raw) * PetClock::kDayTicks + Fx::kOne / 2) >> Fx::kFrac); }
}

TEST(PetClock, NightIsNinePmToSevenAm) {
  EXPECT_FALSE(clockAt(20, 59).night());
  EXPECT_TRUE(clockAt(21, 0).night());
  EXPECT_TRUE(clockAt(3).night());
  EXPECT_TRUE(clockAt(6, 59).night());
  EXPECT_FALSE(clockAt(7, 0).night());
  EXPECT_EQ(clockAt(12).dayFraction(), Fx::ratio(1, 2));
}

TEST(PetClock, AlignToWallUsesTheTimeZoneAndKeepsTicking) {
  PetClock c;
  for (int i = 0; i < 12345; ++i) c.advance(false);
  c.alignToWall(86400u * 20000 + 23 * 3600, 60);   // 23:00 UTC is 00:00 at UTC+1
  EXPECT_EQ(todTicks(c), 0u);
  c.alignToWall(86400u * 20000 + 1 * 3600, -120);   // 01:00 UTC is 23:00 the day before at UTC-2
  EXPECT_EQ(todTicks(c), 23 * kTicksPerHour);
  c.advance(false);
  EXPECT_EQ(todTicks(c), 23 * kTicksPerHour + 1);
}

TEST(PetClock, NeverLiddedNeverDrifts) {
  PetClock c = clockAt(8);
  int32_t before = c.phaseOffsetTicks;
  for (uint32_t t = 0; t < 3 * PetClock::kDayTicks; ++t) c.advance(false);
  EXPECT_EQ(c.phaseOffsetTicks, before);
}

TEST(PetClock, LiddedAllTheTimeMovesThePhaseAtMostAnHourPerDay) {
  PetClock c = clockAt(0);
  uint32_t moved = 0;
  for (int day = 0; day < 10; ++day) {
    uint32_t movedToday = 0;
    for (uint32_t t = 0; t < PetClock::kDayTicks; ++t) {
      int32_t before = c.phaseOffsetTicks;
      c.advance(true);
      movedToday += c.phaseOffsetTicks != before;
    }
    EXPECT_LE(movedToday, kTicksPerHour) << "day " << day;
    moved += movedToday;
  }
  EXPECT_GT(moved, 0u);
}

// The owner sleeps 21:00 to 07:00 and tucks him in for it every night.
// Returns pet time of day at each 21:00 of the owner's.
std::vector<uint32_t> ownerNights(PetClock c, int days) {
  std::vector<uint32_t> atBedtime;
  for (int day = 0; day < days; ++day) {
    for (uint32_t t = 0; t < PetClock::kDayTicks; ++t) {
      uint32_t owner = t;   // the owner's time of day, from midnight
      if (owner == 21 * kTicksPerHour) atBedtime.push_back(todTicks(c));
      c.advance(owner >= 21 * kTicksPerHour || owner < 7 * kTicksPerHour);
    }
  }
  return atBedtime;
}

TEST(PetClock, AnOwnersNightPullsALatePetNightEarlierByAtMostAnHourADay) {
  std::vector<uint32_t> bed = ownerNights(clockAt(21), 15);   // pet three hours behind
  // clockAt(21) at owner midnight puts the pet at 21:00 when the owner is at 00:00.
  ASSERT_EQ(bed[0], 18 * kTicksPerHour);
  for (size_t i = 1; i < bed.size(); ++i) {
    EXPECT_GE(bed[i], bed[i - 1]) << "day " << i;
    EXPECT_LE(bed[i] - bed[i - 1], kTicksPerHour) << "day " << i;
  }
  EXPECT_EQ(bed[1] - bed[0], kTicksPerHour);
  EXPECT_GE(bed.back(), 20 * kTicksPerHour + 29 * kTicksPerMinute);
  EXPECT_LE(bed.back(), 21 * kTicksPerHour);
}

TEST(PetClock, AnOwnersNightHoldsBackAnEarlyPetDawnByAtMostAnHourADay) {
  std::vector<uint32_t> bed = ownerNights(clockAt(2), 6);   // pet two hours ahead
  ASSERT_EQ(bed[0], 22 * kTicksPerHour);   // the first morning already held dawn back an hour
  for (size_t i = 1; i < bed.size(); ++i) {
    EXPECT_LE(bed[i], bed[i - 1]) << "day " << i;
    EXPECT_LE(bed[i - 1] - bed[i], kTicksPerHour) << "day " << i;
  }
  EXPECT_LE(21 * kTicksPerHour - bed.back(), 1u);   // the last dark tick lands on 07:00 itself
}

TEST(Day, DuskAndDawnFireOnTheirEdgesOnly) {
  PetClock c = clockAt(20, 59);
  DayDetector d;
  SenseOut out;
  int dusk = 0, dawn = 0;
  for (uint32_t t = 0; t < 11 * kTicksPerHour; ++t) {
    c.advance(false);
    d.tick(TickContext{c, false, t}, out);
    for (uint8_t i = 0; i < out.stimCount; ++i) {
      dusk += out.stimuli[i] == stim::dusk;
      dawn += out.stimuli[i] == stim::dawn;
    }
    if (t == kTicksPerHour) { EXPECT_EQ(out.loci[0].value, Fx::zero()) << "light at 22:00"; }
    out.clear();
  }
  EXPECT_EQ(dusk, 1);
  EXPECT_EQ(dawn, 1);
}

TEST(Day, BootingAtNightIsNotADusk) {
  PetClock c = clockAt(23);
  DayDetector d;
  SenseOut out;
  for (uint32_t t = 0; t < 10; ++t) { c.advance(false); d.tick(TickContext{c, false, t}, out); }
  EXPECT_EQ(out.stimCount, 0);
}

TEST(Day, SinAndCosFollowThePetDayMappedToUnit) {
  for (uint32_t hour : {0u, 3u, 6u, 9u, 12u, 17u, 22u}) {
    PetClock c = clockAt(hour);
    DayDetector d;
    SenseOut out;
    d.tick(TickContext{c, false, 0}, out);
    Heard r;
    for (uint8_t i = 0; i < out.lociCount; ++i) r.locus[out.loci[i].locus.v] = out.loci[i].value;
    double angle = 2 * M_PI * hour / 24.0;
    EXPECT_NEAR(r.at(locus::day_sin), (std::sin(angle) + 1) / 2, 1e-3) << hour;
    EXPECT_NEAR(r.at(locus::day_cos), (std::cos(angle) + 1) / 2, 1e-3) << hour;
  }
}

TEST(Owner, ArrivalAndLeavingAreEdges) {
  PetClock c = clockAt(12);
  OwnerDetector d;
  SenseOut out;
  std::vector<StimId> fired;
  for (bool near : {false, true, true, true, false, false}) {
    d.tick(TickContext{c, near, 0}, out);
    for (uint8_t i = 0; i < out.stimCount; ++i) fired.push_back(out.stimuli[i]);
    out.clear();
  }
  ASSERT_EQ(fired.size(), 2u);
  EXPECT_EQ(fired[0], stim::owner_arrived);
  EXPECT_EQ(fired[1], stim::owner_left);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
