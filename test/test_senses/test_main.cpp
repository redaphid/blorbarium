#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "blorb/senses.h"
#include "road.h"

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
  // A hand's jolts: 1 g up and 1 g down in turn, one sample each, periodMs apart.
  Gesture& shake(int jolts, uint32_t periodMs) {
    for (int i = 0; i < jolts; ++i) {
      jolt(i % 2 == 0 ? 1000 : -1000);
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
  Gesture& raw(const BodySample& s) { emit(s); return *this; }
  Gesture& raw(const std::vector<BodySample>& v) { for (const BodySample& s : v) emit(s); return *this; }
  Gesture& temp(int16_t cx10) { rest_.tempCx10 = cx10; return *this; }
  Gesture& touch(bool down) { rest_.touchDown = down; return *this; }
  // Held up on edge, upside down: in the screen's frame (main.cpp turns the
  // sample) gravity is along -y, as when held upright. Righted: back on edge, upright.
  Gesture& inverted(bool down) {
    rest_.invertedDown = down;
    return pose(0, -1000, 0);
  }
  // Turned onto a side, held on edge (main.cpp's rotation 1 or 3): in the
  // screen's frame gravity is along -y again; only the level says so.
  Gesture& sided(bool down) {
    rest_.sideDown = down;
    return pose(0, -1000, 0);
  }
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

// Potholes in a row, each a hard lift and a softer rebound: the old shake
// (any 4 jolts off 1 g) fired on these, and a car is full of them.
TEST(Shake, BumpsThatOnlyLiftAreNotAShake) {
  Gesture g;
  g.still(500);
  for (int i = 0; i < 8; ++i) g.jolt(1400).jolt(1000).jolt(-600).still(120);
  EXPECT_EQ(play(g.still(1000)).count(stim::shake), 0);
}

TEST(Shake, JoltsAreMeasuredFromGravitySoASidewaysShakeCounts) {
  Gesture g;
  g.still(500);
  for (int i = 0; i < 6; ++i) {
    BodySample s;
    s.ax = i % 2 == 0 ? 900 : -900;
    s.az = 1000;
    g.raw(s).still(60);
  }
  EXPECT_EQ(play(g.still(1000)).count(stim::shake), 1);
}

TEST(Shake, AGentleWobbleIsNotAShake) {
  Gesture g;
  g.still(500);
  for (int i = 0; i < 10; ++i) g.jolt(i % 2 == 0 ? 600 : -600).still(60);
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

// The tap engine hears a pothole too, and reports it after its quiet window.
TEST(Knock, ATapJustAfterTheWholeBodyLiftedIsTheRoad) {
  EXPECT_EQ(play(Gesture().still(200).jolt(600).jolt(800).jolt(300).still(100).tap(1).still(200)).count(stim::knock), 0);
  EXPECT_EQ(play(Gesture().still(200).jolt(600).jolt(800).still(320).tap(1).still(200)).count(stim::knock), 1)
      << "a third of a second after the bump, a knock is heard";
}

// A brisk clink (BATTLES.md, matching) swings the badge 1 g sideways for a
// tenth of a second: 1.5 g in all, but across gravity, so the knock counts.
TEST(Knock, AClinksSwingIsNotTheRoad) {
  Gesture g;
  g.still(200);
  BodySample swing;
  swing.ax = 1100;
  swing.az = 1000;
  for (int i = 0; i < 5; ++i) g.raw(swing);
  EXPECT_EQ(play(g.still(100).tap(1).still(200)).count(stim::knock), 1);
}

// At 50 Hz a knuckle lands on one sample at most, and only sometimes.
TEST(Knock, AKnuckleCaughtOnOneSampleIsStillAKnock) {
  EXPECT_EQ(play(Gesture().still(200).jolt(2000).still(120).tap(1).still(200)).count(stim::knock), 1);
  EXPECT_EQ(play(Gesture().still(200).jolt(-900).jolt(-400).still(100).tap(2).still(200)).count(stim::double_knock), 1)
      << "pushed down and away is lighter, not a bump";
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

// Upright is a pose, not a pair of hands: a badge propped in a stand sits
// tilted and undisturbed, and after fifteen seconds of it he is parked,
// not held -- so he stops saying he is being held while he sits there.
TEST(Held, ParkedUprightIsNotHeldHoweverLongHeSits) {
  Heard r = play(Gesture().still(1000).tilt(500, 0).still(20000));
  EXPECT_EQ(r.count(stim::picked_up), 1);
  EXPECT_EQ(r.at(locus::held), 0.0) << "a stand is not a hand";
}

// A hand is never that still: a grip shifting every couple of seconds is
// disturbance enough, and he stays held the whole time.
TEST(Held, AHandThatKeepsShiftingItsGripIsStillHoldingHim) {
  Gesture g = Gesture().still(1000).tilt(500, 0);
  for (int i = 0; i < 10; ++i) g.still(1900).jolt(300);
  Heard r = play(g);
  EXPECT_EQ(r.at(locus::held), 1.0);
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

// The shell says the pose (invertedDown: its settled rotation is 180); the
// detector gives it a kid's intent: held, steady, for a full second.
TEST(Hang, HeldUpsideDownForASecondIsAHang) {
  Heard r = play(Gesture().still(2000).inverted(true).still(1500));
  EXPECT_EQ(r.count(stim::hang), 1);
  EXPECT_EQ(r.at(locus::hanging), 1.0);
}

TEST(Hang, AShortTurnUpsideDownIsNotAHang) {
  Heard r = play(Gesture().still(2000).inverted(true).still(600).inverted(false).still(1000));
  EXPECT_EQ(r.count(stim::hang), 0);
  EXPECT_EQ(r.at(locus::hanging), 0.0);
}

// One hang per hang: staying upside down in the lobby he just opened never
// asks again, and a second hang needs a righting first.
TEST(Hang, OneHangPerHangAndAHangAgainAfterRighting) {
  Gesture g;
  g.still(2000).inverted(true).still(3500).inverted(false).still(700).inverted(true).still(1500);
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::hang), 2);
}

// Small hands wobble. A hang that drains instead of resetting still lands:
// half a second upside down, a brief righting, and the rest of the second
// is enough -- where a strict dwell would have started over twice.
TEST(Hang, AWobblyHangStillLandsWithoutStartingOver) {
  Gesture g;
  g.still(2000).inverted(true).still(500).inverted(false).still(150).inverted(true).still(800);
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::hang), 1);
}

// A jolt mid-hang pauses the ask rather than erasing it.
TEST(Hang, AJoltPausesAHangInsteadOfErasingIt) {
  Gesture g;
  g.still(2000).inverted(true).still(600).pose(0, -1400, 0).still(400).pose(0, -1000, 0).still(400);
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::hang), 1);
}

// A badge swung or tossed through upside down is off 1 g while it moves:
// the pose alone, unsteady, is no ask.
TEST(Hang, AnUnsteadyUpsideDownIsNotAHang) {
  Heard r = play(Gesture().still(2000).inverted(true).pose(0, -1400, 0).still(2000));
  EXPECT_EQ(r.count(stim::hang), 0);
  EXPECT_EQ(r.at(locus::hanging), 0.0);
}

// Lying on a table with one edge propped up: the shell settles at 180, but
// the face is near flat. That is furniture, not a hand.
TEST(Hang, LyingSlantedOnATableIsNotAHang) {
  Gesture g;
  g.still(2000).pose(0, -500, 866);   // 30 degrees up from flat, bottom edge raised
  g.inverted(false).pose(0, -500, 866);
  BodySample s;
  s.ay = -500, s.az = 866, s.invertedDown = true;
  for (int i = 0; i < 300; ++i) g.raw(s);   // 6 s
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::hang), 0);
}

// Switched on upside down (in a case hung on a hook): no ask until someone
// has turned it the right way up and back.
TEST(Hang, BootedUpsideDownIsNotAHangUntilTurned) {
  Gesture g;
  BodySample s;
  s.ay = -1000, s.invertedDown = true;
  for (int i = 0; i < 200; ++i) g.raw(s);   // 4 s, from the first sample
  Heard r0 = play(g);
  EXPECT_EQ(r0.count(stim::hang), 0);
  g.inverted(false).still(500).inverted(true).still(1500);
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::hang), 1);
}

// ---- the twins (controls.h) ----------------------------------------------------

// The knock knock is a press: a Button with it, and still a DoubleKnock for him.
TEST(Twins, AKnockKnockIsAPress) {
  Heard r = play(Gesture().still(200).tap(2).still(200));
  EXPECT_EQ(r.count(stim::double_knock), 1);
  EXPECT_EQ(r.count(stim::button), 1);
  EXPECT_EQ(r.count(stim::button_double), 0);
}

// One knock is no press: a desk is bumped and a keyboard typed on.
TEST(Twins, OneKnockIsNoPress) {
  Heard r = play(Gesture().still(200).tap(1).still(500).tap(1).still(200));
  EXPECT_EQ(r.count(stim::knock), 2);
  EXPECT_EQ(r.count(stim::button), 0);
}

// The road's knock knock is the road's: masked like its knocks, so no pellet.
TEST(Twins, ARoadsKnockKnockIsNoPress) {
  Heard r = play(Gesture().still(200).jolt(-900).jolt(-400).still(100).tap(2).still(200));
  EXPECT_EQ(r.count(stim::button), r.count(stim::double_knock));
}

TEST(Twins, TurnedOntoASideAndHeldIsAHold) {
  Detectors d;
  SenseOut out;
  Gesture g;
  g.still(2000).sided(true).still(1500);
  int holds = 0, presses = 0;
  bool heldAtEnd = false;
  for (const Step& st : g.steps()) {
    d.sample(st.s, st.ms, out);
    heldAtEnd = d.side.holding();
  }
  for (uint8_t i = 0; i < out.stimCount; ++i) holds += out.stimuli[i] == stim::button_hold, presses += out.stimuli[i] == stim::button;
  EXPECT_EQ(holds, 1);
  EXPECT_EQ(presses, 0);
  EXPECT_TRUE(heldAtEnd) << "the level goes on while he is held there (the 3 s holds time by it)";
}

// Let go sooner, a turn does nothing at all: a release is never a press.
TEST(Twins, AShortTurnOntoASideIsNothing) {
  Heard r = play(Gesture().still(2000).sided(true).still(800).sided(false).still(1000));
  EXPECT_EQ(r.count(stim::button_hold), 0);
  EXPECT_EQ(r.count(stim::button), 0);
}

// One hold per turn: kept on his side he holds once; upright again, and the next turn holds again.
TEST(Twins, OneHoldPerTurnAndAgainAfterUpright) {
  Gesture g;
  g.still(2000).sided(true).still(5000).sided(false).still(500).sided(true).still(1500);
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::button_hold), 2);
}

// A jolt, or a moment past the edge, is a small hand, not a let-go.
TEST(Twins, AJoltOrAWobbleDoesNotEndASideHold) {
  Detectors d;
  SenseOut out;
  Gesture g;
  g.still(2000).sided(true).still(600).pose(0, -1500, 0).still(60).pose(0, -800, 600).still(80).pose(0, -1000, 0).still(800);
  int holds = 0;
  for (const Step& st : g.steps()) d.sample(st.s, st.ms, out);
  for (uint8_t i = 0; i < out.stimCount; ++i) holds += out.stimuli[i] == stim::button_hold;
  EXPECT_EQ(holds, 1);
}

// Lying on a table on its side settles the panel too; flat is no hold.
TEST(Twins, LyingFlatWithTheRotationOnASideIsNoHold) {
  Gesture g;
  g.still(2000);
  BodySample s;
  s.ay = -500, s.az = 866, s.sideDown = true;
  for (int i = 0; i < 300; ++i) g.raw(s);
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::button_hold), 0);
}

// Switched on sideways, or never upright since: nobody turned him.
TEST(Twins, BootedOnASideHoldsNothingUntilTurned) {
  Gesture g;
  BodySample s;
  s.ay = -1000, s.sideDown = true;
  for (int i = 0; i < 200; ++i) g.raw(s);
  EXPECT_EQ(play(g).count(stim::button_hold), 0);
  g.sided(false).still(500).sided(true).still(1500);
  EXPECT_EQ(play(g).count(stim::button_hold), 1);
}

// Turned back after 0.8 s: the settled rotation lags (orient.h) and still says
// "on a side" for a second and a half, but in the screen's frame gravity has
// already swung off down. That lag is no hold (found on badge 1: a 1 s turn
// held at 2.4 s, after the kid had let go).
TEST(Twins, TheRotationsLagAfterATurnBackIsNoHold) {
  Gesture g;
  g.still(2000).sided(true).still(800);
  BodySample back;   // upright again, as the screen's frame still turned a quarter sees it
  back.ax = -1000, back.ay = 0, back.az = 0, back.sideDown = true;
  for (int i = 0; i < 75; ++i) g.raw(back);   // 1.5 s
  g.sided(false).still(1000);
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::button_hold), 0);
}

// The same lag after a hang let go early: righted, not hanging.
TEST(Hang, TheRotationsLagAfterRightingIsNoHang) {
  Gesture g;
  g.still(2000).inverted(true).still(500);
  BodySample back;   // righted, as the screen's frame still turned 180 sees it
  back.ay = 1000, back.invertedDown = true;
  for (int i = 0; i < 75; ++i) g.raw(back);
  g.inverted(false).still(1000);
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::hang), 0);
}

// Laid on its side and left there, leaning so the face sits right on the
// on-edge line: it flickers in and out of the pose for ten minutes, and holds
// once. Only the right way up again re-arms it (PoseRearm).
TEST(Twins, LaidOnItsSideAndLeftItHoldsOnceNotOverAndOver) {
  Gesture g;
  g.still(2000);
  BodySample lean;
  lean.ay = -870, lean.sideDown = true;
  for (int i = 0; i < 30000; ++i) {   // 10 min at 50 Hz
    // Settled on edge for its first 2 s, then across |az| = 500 every 0.74 s or every 3 s.
    lean.az = int16_t(i < 100 ? 480 : (i < 15000 ? i / 37 % 2 : i / 150 % 2) ? 480 : 530);
    g.raw(lean);
  }
  Heard r = play(g);
  EXPECT_EQ(r.count(stim::button_hold), 1);
  g.sided(false).still(1000).sided(true).still(1500);
  EXPECT_EQ(play(g).count(stim::button_hold), 2) << "picked up the right way and turned again: another";
}

// The same for the hang: leaning upside down against a wall, the face
// flickering across the line, it asks for a fight once.
TEST(Hang, LeftLeaningUpsideDownItHangsOnceNotOverAndOver) {
  Gesture g;
  g.still(2000);
  BodySample lean;
  lean.ay = -870, lean.invertedDown = true;
  for (int i = 0; i < 30000; ++i) {
    lean.az = int16_t(i < 100 ? 480 : (i < 15000 ? i / 37 % 2 : i / 150 % 2) ? 480 : 530);
    g.raw(lean);
  }
  EXPECT_EQ(play(g).count(stim::hang), 1);
}

// Upside down is the hang, not a hold, and a side hold is no hang.
TEST(Twins, TheHangAndTheSideHoldNeverCross) {
  Heard a = play(Gesture().still(2000).inverted(true).still(2000));
  EXPECT_EQ(a.count(stim::hang), 1);
  EXPECT_EQ(a.count(stim::button_hold), 0);
  Heard b = play(Gesture().still(2000).sided(true).still(2000));
  EXPECT_EQ(b.count(stim::hang), 0);
  EXPECT_EQ(b.count(stim::button_hold), 1);
}

// 300 ms in the air is a fall of 44 cm: a lap to a car's floor.
TEST(Drop, FreeFallThenImpactIsDroppedAndItsBounceIsNotAShake) {
  Heard r = play(Gesture().still(300).fall(300).jolt(1000).still(60).jolt(-600).still(500));
  EXPECT_EQ(r.count(stim::dropped), 1);
  EXPECT_EQ(r.count(stim::shake), 0);
}

TEST(Drop, ImpactWithoutAFallIsNotDropped) {
  EXPECT_EQ(play(Gesture().still(300).jolt(1000).still(500)).count(stim::dropped), 0);
}

TEST(Drop, ABriefDipIsNotAFall) {
  EXPECT_EQ(play(Gesture().still(300).fall(60).jolt(1000).still(500)).count(stim::dropped), 0);
}

// A pothole can throw him off the seat for up to 150 ms (3 cm up and back).
TEST(Drop, AHopOffASeatIsNotAFall) {
  EXPECT_EQ(play(Gesture().still(300).fall(160).jolt(1500).still(500)).count(stim::dropped), 0);
  EXPECT_EQ(play(Gesture().still(300).fall(220).jolt(1500).still(500)).count(stim::dropped), 1) << "20 cm is";
}

TEST(Drop, AFallCaughtSoftlyIsNotDropped) {
  EXPECT_EQ(play(Gesture().still(300).fall(300).still(1000).jolt(1000).still(300)).count(stim::dropped), 0);
}

TEST(Lid, FaceDownAndStillFor30SecondsIsLidDownAndDarkensLight) {
  Heard r = play(Gesture().still(300).flip().still(30000));
  EXPECT_EQ(r.count(stim::lid_down), 0);
  EXPECT_EQ(r.at(locus::light), 1.0);

  r = play(Gesture().still(300).flip().still(32000));
  EXPECT_EQ(r.count(stim::lid_down), 1);
  EXPECT_EQ(r.at(locus::light), 0.0);

  r = play(Gesture().still(300).flip().still(32000).right().still(400));
  EXPECT_EQ(r.count(stim::lid_up), 1);
  EXPECT_EQ(r.at(locus::light), 1.0);
}

// A table: the sensor's own noise, and the time it takes is 30 s and a bit.
TEST(Lid, FaceDownOnATableSleepsAfterAboutThirtySeconds) {
  for (uint64_t seed = 1; seed <= 8; ++seed) {
    blorbtest::Rand n(seed);
    auto table = [&](int16_t az, uint32_t ms) {
      std::vector<BodySample> v;
      for (uint32_t t = 0; t < ms; t += kSampleMs)
        v.push_back(blorbtest::toSample({0.003 * n.gauss(), 0.003 * n.gauss(), az / 1000.0 + 0.003 * n.gauss()}));
      return v;
    };
    Gesture g;
    g.raw(table(1000, 2000));
    uint32_t flippedAt = g.now();
    Heard r = play(g.raw(table(-1000, 40000)));
    ASSERT_EQ(r.count(stim::lid_down), 1) << "seed " << seed;
    EXPECT_GE(r.firstAt(stim::lid_down) - flippedAt, 30000u) << "seed " << seed;
    EXPECT_LE(r.firstAt(stim::lid_down) - flippedAt, 32000u) << "seed " << seed;
  }
}

TEST(Lid, AJostleStartsTheWaitAgain) {
  Gesture g = Gesture().still(300).flip().still(20000);
  BodySample bump;
  bump.ax = 60;
  bump.az = -1000;
  uint32_t jostledAt = g.now();
  Heard r = play(g.raw(bump).still(25000));
  EXPECT_EQ(r.count(stim::lid_down), 0) << "45 s face down, but 25 s since the jostle";
  r = play(g.still(7000));
  ASSERT_EQ(r.count(stim::lid_down), 1);
  EXPECT_GE(r.firstAt(stim::lid_down) - jostledAt, 30000u);
}

TEST(Lid, AKnockOnALiddedDishIsNotLidUp) {
  Heard r = play(Gesture().still(300).flip().still(32000).jolt(1500).still(500));
  EXPECT_EQ(r.count(stim::lid_down), 1);
  EXPECT_EQ(r.count(stim::lid_up), 0);
}

TEST(Lid, JostledOnceLiddedStaysLidded) {
  Gesture g = Gesture().still(300).flip().still(32000);
  for (int i = 0; i < 50; ++i) g.jolt(i % 2 == 0 ? 300 : -300).still(80);
  Heard r = play(g.still(500));
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

// ---- in a car (support/road.h) ----------------------------------------------------

namespace {

using blorbtest::Lying;
using blorbtest::Road;
using blorbtest::Vec;

struct Tally {
  int shake = 0, dropped = 0, knocks = 0, flipped = 0, righted = 0, lid = 0, pickedUp = 0, cradle = 0;
  uint32_t taps = 0, throws = 0, seconds = 0;
  void add(const Heard& r) {
    shake += r.count(stim::shake), dropped += r.count(stim::dropped);
    knocks += r.count(stim::knock) + r.count(stim::double_knock);
    flipped += r.count(stim::flipped), righted += r.count(stim::righted), lid += r.count(stim::lid_down);
    pickedUp += r.count(stim::picked_up), cradle += r.count(stim::cradle);
  }
  void print(const char* name) const {
    std::printf("[road] %s, %u s, %u throws: shake %d, dropped %d, knocks %d of %u taps, flipped %d, righted %d, lid %d, picked_up %d, cradle %d\n",
                name, unsigned(seconds), unsigned(throws), shake, dropped, knocks, unsigned(taps), flipped, righted, lid, pickedUp, cradle);
  }
};

constexpr uint64_t kSeeds = 8;

Tally rides(Lying lying, const Road& road, uint32_t seconds) {
  Tally t;
  for (uint64_t seed = 1; seed <= kSeeds; ++seed) {
    blorbtest::Ride ride = blorbtest::ride(lying, road, seed, seconds);
    t.add(play(Gesture().raw(ride.samples)));
    t.taps += ride.taps;
    t.throws += ride.throws;
    t.seconds += seconds;
  }
  return t;
}

// A hand's acceleration added to a ride, clipped like the IMU.
void add(std::vector<BodySample>& v, size_t i, Vec d) {
  auto axis = [](int32_t a, double dg) {
    long r = std::lround(a + dg * 1000);
    return int16_t(r > 1999 ? 1999 : r < -1999 ? -1999 : r);
  };
  v[i].ax = axis(v[i].ax, d.x), v[i].ay = axis(v[i].ay, d.y), v[i].az = axis(v[i].az, d.z);
}

constexpr size_t kAt10s = 10000 / kSampleMs;

}  // namespace

TEST(Road, ABumpyRideOnTheSeatTroublesHimNot) {
  Tally t = rides(Lying::FaceUp, Road{}, 180);
  t.print("face up on the seat");
  EXPECT_GT(t.taps, 100u) << "the road offered a knock storm";
  EXPECT_EQ(t.shake, 0);
  EXPECT_EQ(t.dropped, 0);
  EXPECT_EQ(t.knocks, 0);
  EXPECT_EQ(t.flipped + t.righted, 0);
  EXPECT_EQ(t.lid, 0);
}

TEST(Road, ARoughRideThatThrowsHimOffTheSeatTroublesHimLittle) {
  Tally t = rides(Lying::FaceUp, Road{100, 4, 10, 20}, 180);
  t.print("face up, rough, thrown");
  EXPECT_LE(t.shake * 50, int(t.throws)) << "thrown twice in a second is shaken, but rarely";
  EXPECT_EQ(t.dropped, 0);
  EXPECT_EQ(t.knocks, 0);
  EXPECT_EQ(t.flipped + t.righted, 0);
  EXPECT_EQ(t.lid, 0);
}

TEST(Road, HeldInAHandOnABumpyRide) {
  Tally t = rides(Lying::InHand, Road{}, 180);
  t.print("in a hand");
  EXPECT_EQ(t.shake, 0);
  EXPECT_EQ(t.dropped, 0);
  EXPECT_EQ(t.knocks, 0);
  EXPECT_EQ(t.flipped + t.righted, 0);
}

// Face down on the seat, from the smoothest highway to a rough lane: never a bedtime.
TEST(Road, FaceDownInAMovingCarHeNeverSleeps) {
  for (double buzz : {20.0, 60.0, 100.0}) {
    Tally t = rides(Lying::FaceDown, Road{buzz, 8, 12, 0}, 300);
    t.print(buzz < 30 ? "face down, smooth highway" : buzz < 80 ? "face down, town" : "face down, rough");
    EXPECT_EQ(t.lid, 0) << buzz;
    EXPECT_EQ(t.flipped, int(kSeeds)) << "once each, as he is laid down";
    EXPECT_EQ(t.righted, 0);
    EXPECT_EQ(t.shake + t.dropped + t.knocks, 0);
  }
}

// A kid's shake, 3 to 6 swings a second at 1 to 2 g, for two and a half
// swings or half a second, along any axis: one shake on a table and one in a moving car.
TEST(Road, AHandShakeStillFiresInACar) {
  uint64_t seed = 100;
  for (double hz : {3.0, 4.0, 5.0, 6.0})
    for (double g : {1.0, 1.5, 2.0})
      for (int axis = 0; axis < 3; ++axis)
        for (bool car : {false, true}) {
          std::vector<BodySample> v = car ? blorbtest::ride(Lying::InHand, Road{}, ++seed, 20).samples
                                          : std::vector<BodySample>(20000 / kSampleMs, blorbtest::toSample({0, -0.5, 0.866}));
          const double dur = std::max(0.5, 2.5 / hz);
          for (size_t i = 0; i * kSampleMs < dur * 1000; ++i) {
            const double a = g * std::sin(2 * M_PI * hz * i * kSampleMs / 1000.0);
            add(v, kAt10s + i, Vec{axis == 0 ? a : 0, axis == 1 ? a : 0, axis == 2 ? a : 0});
          }
          EXPECT_EQ(play(Gesture().raw(v)).count(stim::shake), 1)
              << hz << " Hz, " << g << " g, axis " << axis << (car ? ", in a car" : ", at rest");
        }
}

// Knocks every 2-6 s in a hand on a bumpy ride. A knuckle lands on a sample a
// quarter of the time; the tap engine reports it 100-160 ms later. Every one
// is heard but those reported with a pothole's lift, from 0.2 s before to 0.5 s after.
TEST(Road, KnocksStillLandInACar) {
  int clear = 0, clearHeard = 0, nearBump = 0, nearHeard = 0, heard = 0;
  for (uint64_t seed = 1; seed <= kSeeds; ++seed) {
    blorbtest::Ride ride = blorbtest::ride(Lying::InHand, Road{}, seed, 180);
    std::vector<BodySample>& v = ride.samples;
    for (BodySample& s : v) s.tapCode = 0;   // only the knuckle's taps this time
    blorbtest::Rand r(seed + 1000);
    std::vector<size_t> reports;
    for (size_t i = size_t(r.in(2, 6) * 50); i + 10 < v.size(); i += size_t(r.in(2, 6) * 50)) {
      if (r.unit() < 0.25) add(v, i, Vec{0, 0, -r.in(1, 2.5)});
      reports.push_back(i + size_t(r.in(5, 8)));
      v[reports.back()].tapCode = 1;
    }
    Gesture g;
    const uint32_t start = g.now();
    Heard h = play(g.raw(v));
    heard += h.count(stim::knock);
    for (size_t at : reports) {
      const uint32_t ms = start + uint32_t(at) * kSampleMs;
      bool got = false;
      for (const Fired& f : h.fired) got |= f.stim == stim::knock && f.ms >= ms && f.ms <= ms + kTickMs;
      bool bumped = false;
      for (double lift : ride.lifts) bumped |= at * kSampleMs / 1000.0 - lift >= -0.2 && at * kSampleMs / 1000.0 - lift <= 0.5;
      (bumped ? nearBump : clear) += 1;
      (bumped ? nearHeard : clearHeard) += got;
    }
  }
  std::printf("[road] knocks in a hand on a bumpy ride: %d of %d heard away from potholes, %d of %d with one\n",
              clearHeard, clear, nearHeard, nearBump);
  EXPECT_EQ(clearHeard, clear);
  EXPECT_EQ(heard, clearHeard + nearHeard) << "no knock the kid did not make";
  EXPECT_GE(clearHeard + nearHeard, (clear + nearBump) * 9 / 10);
}

// From a lap to the floor, 30 to 80 cm, mid-ride.
TEST(Road, ADropStillLandsInACar) {
  for (uint64_t seed = 1; seed <= 16; ++seed) {
    std::vector<BodySample> v = blorbtest::ride(Lying::InHand, Road{}, seed, 20).samples;
    blorbtest::Rand r(seed + 2000);
    const size_t air = size_t(r.in(250, 400) / kSampleMs);
    for (size_t i = 0; i < air; ++i) v[kAt10s + i] = blorbtest::toSample({0.01 * r.gauss(), 0.01 * r.gauss(), 0.01 * r.gauss()});
    v[kAt10s + air] = blorbtest::toSample({0, 0, r.in(2, 4)});
    EXPECT_EQ(play(Gesture().raw(v)).count(stim::dropped), 1) << "seed " << seed << ", " << air * kSampleMs << " ms";
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
