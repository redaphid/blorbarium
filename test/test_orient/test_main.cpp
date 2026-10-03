#include <gtest/gtest.h>
#include "blorb/senses.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "blorb/habitat.h"
#include "../../src/hw/orient.h"

namespace {

constexpr int kR0 = 1;   // board_lcd128.h

blorb::BodySample mg(int ax, int ay, int az) {
  blorb::BodySample s;
  s.ax = int16_t(ax), s.ay = int16_t(ay), s.az = int16_t(az);
  return s;
}

// Raw IMU reading held still with the native top, right, bottom, left edge up.
const blorb::BodySample kHeld[4] = {mg(0, -1000, 0), mg(1000, 0, 0), mg(0, 1000, 0), mg(-1000, 0, 0)};
// Raw direction of the screen's +x at each rotation: the edge the owner sees on the right.
const int kRight[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};

// The loop's 50 Hz, from `from` up to but not including `to`.
void feed(orient::Up& up, const blorb::BodySample& s, uint32_t from, uint32_t to) {
  for (uint32_t t = from; t < to; t += 20) up.sample(s, t);
}

TEST(Orient, FourHeldOrientationsGiveFourRotations) {
  for (int r = 0; r < 4; ++r) {
    orient::Up up(kR0);
    feed(up, kHeld[r], 1000, 3000);
    EXPECT_EQ(up.rotation(), r) << "held with native edge " << r << " up";
  }
}

TEST(Orient, PhysicalDownIsScreenDownAtEveryRotation) {
  for (int r = 0; r < 4; ++r) {
    orient::Up up(kR0);
    feed(up, kHeld[r], 1000, 3000);
    ASSERT_EQ(up.rotation(), r);
    const blorb::BodySample upright = up.toScreen(kHeld[r]);
    EXPECT_EQ(upright.ax, 0) << "rot " << r;
    EXPECT_EQ(upright.ay, -1000) << "rot " << r << ": gravity up the screen, so downhill is +y";
    const blorb::BodySample right = up.toScreen(mg(300 * kRight[r][0], 300 * kRight[r][1], 950));
    EXPECT_EQ(right.ax, 300) << "rot " << r << ": the owner's right edge raised reads +x";
    EXPECT_EQ(right.ay, 0) << "rot " << r;
    EXPECT_EQ(right.az, 950) << "rot " << r;
  }
}

TEST(Orient, ATurnFlipsOnlyAfterSettling) {
  orient::Up up(kR0);
  feed(up, kHeld[0], 1000, 3000);
  ASSERT_EQ(up.rotation(), 0);
  feed(up, kHeld[1], 3000, 3790);
  EXPECT_EQ(up.rotation(), 0) << "at 10 Hz the smoothing crosses 45 deg at 3200, then 600 ms more";
  feed(up, kHeld[1], 3790, 3830);
  EXPECT_EQ(up.rotation(), 1);
}

TEST(Orient, ATurnShorterThanTheSettleSnapsBack) {
  orient::Up up(kR0);
  feed(up, kHeld[0], 1000, 3000);
  feed(up, kHeld[1], 3000, 3700);
  ASSERT_EQ(up.rotation(), 0);
  feed(up, kHeld[0], 3700, 6000);
  EXPECT_EQ(up.rotation(), 0);
}

TEST(Orient, LyingFlatKeepsTheRotationItHad) {
  orient::Up up(kR0);
  feed(up, kHeld[1], 1000, 3000);
  ASSERT_EQ(up.rotation(), 1);
  feed(up, mg(20, 30, 1000), 3000, 9000);   // face up, the in-plane noise pointing at rot 2
  EXPECT_EQ(up.rotation(), 1);
}

TEST(Orient, AJoltIsNotGravity) {
  orient::Up up(kR0);
  feed(up, kHeld[1], 1000, 3000);
  feed(up, mg(0, 1500, 0), 3000, 9000);   // 1.5 g toward rot 2: a hand, not a turn
  EXPECT_EQ(up.rotation(), 1);
}

// ---- the whole chain: IMU, orient, the engine's senses, the marble ---------------

// Raw direction of each native edge (top, right, bottom, left): kHeld[e] / 1000.
const double kEdge[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
constexpr double kDeg = 3.14159265358979 / 180;

// The badge lying back from flat by `deg` with native edge `low` lowest.
blorb::BodySample laid(int low, double deg) {
  const double g = 1000 * std::sin(deg * kDeg);
  return mg(int(std::lround(-g * kEdge[low][0])), int(std::lround(-g * kEdge[low][1])),
            int(std::lround(1000 * std::cos(deg * kDeg))));
}

// Upright, its up edge turned `deg` from native top toward native right.
blorb::BodySample standing(double deg) {
  return mg(int(std::lround(1000 * std::sin(deg * kDeg))), int(std::lround(-1000 * std::cos(deg * kDeg))), 0);
}

// Where a raw in-plane direction points on the screen the owner sees at rot r.
std::pair<double, double> onScreen(double rx, double ry, int r) {
  return {rx * kRight[r][0] + ry * kRight[r][1], -(rx * kEdge[r][0] + ry * kEdge[r][1])};
}

double fx(blorb::Fx v) { return double(v.raw) / blorb::Fx::kOne; }

// main.cpp's step() and Dish::runOneTick, minus the creature: 50 Hz samples
// through orient into the detectors, a 10 Hz tick into the habitat.
struct Chain {
  orient::Up up{kR0};
  blorb::Detectors det;
  blorb::Habitat hab;
  blorb::PetClock clock;
  blorb::SenseOut pending;
  blorb::Fx tx = blorb::Fx::ratio(1, 2), ty = blorb::Fx::ratio(1, 2);
  uint32_t ms = 1000, tick = 0;
  bool turnTilt = true;   // main.cpp's dish->turned on a rotation change
  double down[2] = {0, 0};   // when set, each tick scores its push against this raw physical down
  double worstCos = 1;
  uint32_t tickPhaseMs = 0;   // the engine tick and orient's 100 ms sample run unsynchronised on the board
  const blorb::HabitatRules rules{3, 100, 1000, blorb::Fx::ratio(1, 4)};

  void sample(const blorb::BodySample& raw) {
    const uint8_t was = up.rotation(), rot = up.sample(raw, ms);
    if (turnTilt && rot != was) det.tilt.turn(uint8_t(rot - was));
    det.sample(up.toScreen(raw), ms, pending);
    ms += 20;
    if (ms % 100 == tickPhaseMs) tickOnce();
  }
  void hold(const blorb::BodySample& raw, uint32_t forMs) {
    for (uint32_t end = ms + forMs; ms < end;) sample(raw);
  }
  void tickOnce() {
    blorb::SenseOut out = pending;
    pending.clear();
    clock.advance(false);
    det.tick(blorb::TickContext{clock, false, tick}, out);
    for (uint8_t i = 0; i < out.lociCount; ++i) {
      if (out.loci[i].locus == blorb::locus::tilt_x) tx = out.loci[i].value;
      if (out.loci[i].locus == blorb::locus::tilt_y) ty = out.loci[i].value;
    }
    hab.step(rules, tx, ty, blorb::DishPos{blorb::Fx::zero(), blorb::Fx::ratio(-7, 10)}, tick++, out);
    if (down[0] == 0 && down[1] == 0) return;
    auto [px, py] = push();
    auto [ex, ey] = onScreen(down[0], down[1], up.rotation());
    worstCos = std::min(worstCos, (px * ex + py * ey) / std::hypot(px, py));
  }
  // The downhill push the habitat gets this tick, before its dead zone.
  std::pair<double, double> push() const { return {0.5 - fx(tx), 0.5 - fx(ty)}; }
  void restMarbleAt(double x, double y) {
    hab.marble = blorb::Marble{{blorb::Fx{int32_t(x * blorb::Fx::kOne)}, blorb::Fx{int32_t(y * blorb::Fx::kOne)}}, {}, {}};
  }
};

TEST(Chain, AtEveryRotationTheMarbleRollsTowardTheEdgeThatIsPhysicallyLowest) {
  for (int r = 0; r < 4; ++r)
    for (int low = 0; low < 4; ++low) {
      Chain c;
      c.hold(kHeld[r], 3000);
      ASSERT_EQ(c.up.rotation(), r);
      c.hold(laid(low, 25), 300);
      c.restMarbleAt(0, 0);
      c.hold(laid(low, 25), 400);
      auto [ex, ey] = onScreen(kEdge[low][0], kEdge[low][1], r);
      const double mx = fx(c.hab.marble.at.x), my = fx(c.hab.marble.at.y), moved = std::hypot(mx, my);
      EXPECT_GT(moved, 0.2) << "rot " << r << ", native edge " << low << " lowest";
      EXPECT_GT(mx * ex + my * ey, 0.95 * moved) << "rot " << r << ", native edge " << low << " lowest: toward ("
                                                 << ex << ", " << ey << ") on screen";
      c.hold(laid(low, 25), 3000);
      EXPECT_EQ(c.up.rotation(), r) << "a 25 degree tilt to roll the marble turned the screen";
    }
}

// The smoothed tilt used to keep the old frame after a turn, so the marble
// was pushed toward the edge that had been down until it caught up.
void turnAQuarterMidRoll(bool turnTilt, double& worst) {
  Chain c;
  c.turnTilt = turnTilt;
  c.tickPhaseMs = 20;   // worst case: a tick one sample after the flip
  c.hold(standing(0), 3000);
  ASSERT_EQ(c.up.rotation(), 0);
  c.restMarbleAt(-0.5, 0);
  for (int step = 0; step <= 20; ++step) {   // a quarter turn in 420 ms
    c.down[0] = -std::sin(step * 4.5 * kDeg), c.down[1] = std::cos(step * 4.5 * kDeg);
    c.hold(standing(step * 4.5), 20);
  }
  c.hold(standing(90), 3000);
  EXPECT_EQ(c.up.rotation(), 1);
  worst = c.worstCos;
}

TEST(Chain, AQuarterTurnMidRollNeverPushesTheMarbleAnyWayButDown) {
  double worst = 0;
  turnAQuarterMidRoll(true, worst);
  EXPECT_GT(worst, 0.95) << "cosine between the push and physical down, worst tick through and after the turn";
  double stale = 1;
  turnAQuarterMidRoll(false, stale);
  EXPECT_LT(stale, 0.7) << "without the turn the push is the old frame's down: the test sees the defect";
}

TEST(Chain, A25DegreeTiltRollsItAcrossTheDishFromRestInUnderASecond) {
  Chain c;
  c.hold(kHeld[0], 3000);
  c.hold(laid(0, 0), 1000);
  c.restMarbleAt(0.92, 0);
  int lowLeft = 0;
  for (int e = 0; e < 4; ++e)
    if (onScreen(kEdge[e][0], kEdge[e][1], 0).first < -0.5) lowLeft = e;
  int crossedMs = -1;
  bool rolling = false;
  for (int t = 1; t <= 30 && crossedMs < 0; ++t) {
    c.hold(laid(lowLeft, 25), 100);
    rolling = rolling || c.hab.marble.vx < blorb::Fx::zero();
    if (fx(c.hab.marble.at.x) <= -0.85 || (rolling && c.hab.marble.vx > blorb::Fx::zero())) crossedMs = t * 100;
  }
  printf("crossing: %d ms from the tilt\n", crossedMs);
  EXPECT_GT(crossedMs, 0);
  EXPECT_LT(crossedMs, 1000) << "rim to rim, with the senses' smoothing in the way";
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
