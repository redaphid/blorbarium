#include <gtest/gtest.h>
#include "blorb/senses.h"   // first, so PlatformIO's dependency finder links lib/blorb
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

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
