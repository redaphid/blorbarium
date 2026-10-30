#include <gtest/gtest.h>
#include "blorb/senses.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "../../src/hw/orient.h"
#include "../../src/hw/home.h"

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

// Home is USB-C up in the kids' cases (board_lcd128.h ORIENT_HOME = 2).
constexpr uint8_t kHome = 2;

TEST(Orient, HungAndTurnedOntoASideAreThePoses) {
  orient::Up up(kR0);
  feed(up, kHeld[kHome], 1000, 3000);
  EXPECT_FALSE(up.inverted(kHeld[kHome], kHome));
  EXPECT_FALSE(up.sideways(kHeld[kHome], kHome));
  feed(up, kHeld[0], 3000, 5000);
  EXPECT_TRUE(up.inverted(kHeld[0], kHome)) << "USB-C down";
  feed(up, kHeld[1], 5000, 7000);
  EXPECT_TRUE(up.sideways(kHeld[1], kHome));
  EXPECT_FALSE(up.inverted(kHeld[1], kHome));
}

// Righted after a hang: the settled rotation lags (about 1.5 s) and still says
// upside down, but gravity has already swung off its down. That lag is no hang.
TEST(Orient, TheRotationsLagAfterRightingIsNoHang) {
  orient::Up up(kR0);
  feed(up, kHeld[kHome], 1000, 3000);
  feed(up, kHeld[0], 3000, 5000);
  ASSERT_TRUE(up.inverted(kHeld[0], kHome));
  bool lagged = false;
  for (uint32_t t = 5000; t < 7000; t += 20) {
    up.sample(kHeld[kHome], t);
    lagged |= up.rotation() == 0;
    EXPECT_FALSE(up.inverted(kHeld[kHome], kHome)) << "at " << t;
  }
  EXPECT_TRUE(lagged) << "the settle should still say upside down for a while";
}

// The same lag after a side hold turned back: no hold.
TEST(Orient, TheRotationsLagAfterATurnBackIsNoSide) {
  orient::Up up(kR0);
  feed(up, kHeld[kHome], 1000, 3000);
  feed(up, kHeld[1], 3000, 5000);
  ASSERT_TRUE(up.sideways(kHeld[1], kHome));
  bool lagged = false;
  for (uint32_t t = 5000; t < 7000; t += 20) {
    up.sample(kHeld[kHome], t);
    lagged |= up.rotation() == 1;
    EXPECT_FALSE(up.sideways(kHeld[kHome], kHome)) << "at " << t;
  }
  EXPECT_TRUE(lagged);
}

// ---- home, per badge (src/hw/home.h) ------------------------------------------------

using hw::home::atBoot;
using hw::home::kDown;
using hw::home::kUp;

// A normal release on a badge never told: USB-C up, as every badge was.
TEST(Home, AReleaseOnABadgeNeverToldIsUpAndWritesNothing) {
  const hw::home::AtBoot h = atBoot(std::nullopt, kUp);
  EXPECT_EQ(h.rot, kUp);
  EXPECT_FALSE(h.write);
  EXPECT_STREQ(h.why, "default");
}

// The three flipped badges: their first boot of a flip build that knows NVS
// writes "down", so the next release, built up, keeps them down.
TEST(Home, AFlipBuildMigratesItselfOnce) {
  const hw::home::AtBoot first = atBoot(std::nullopt, kDown);
  EXPECT_EQ(first.rot, kDown);
  EXPECT_TRUE(first.write);
  EXPECT_STREQ(first.why, "migrated");
  const hw::home::AtBoot again = atBoot(kDown, kDown);
  EXPECT_EQ(again.rot, kDown);
  EXPECT_FALSE(again.write) << "written once, not every boot";
  const hw::home::AtBoot release = atBoot(kDown, kUp);
  EXPECT_EQ(release.rot, kDown) << "a universal release keeps a flipped badge flipped";
  EXPECT_FALSE(release.write);
}

// What the badge was told wins over how it was built, either way round.
TEST(Home, StoredWinsOverTheBuild) {
  EXPECT_EQ(atBoot(kUp, kDown).rot, kUp) << "DEBUG home up on a flip build stays up";
  EXPECT_FALSE(atBoot(kUp, kDown).write) << "and is not overwritten by the migration";
  EXPECT_EQ(atBoot(kDown, kUp).rot, kDown);
}

// A stored byte that is neither home (a torn write, a future value) is no home:
// the build decides, and a flip build writes its own over it.
TEST(Home, AStoredByteThatIsNoHomeIsIgnored) {
  for (uint8_t junk : {uint8_t(1), uint8_t(3), uint8_t(0xff)}) {
    EXPECT_EQ(atBoot(junk, kUp).rot, kUp);
    EXPECT_FALSE(atBoot(junk, kUp).write);
    EXPECT_EQ(atBoot(junk, kDown).rot, kDown);
    EXPECT_TRUE(atBoot(junk, kDown).write);
  }
  EXPECT_EQ(atBoot(std::nullopt, 1).rot, kUp) << "a build flag that is no home falls back to up";
}

TEST(Home, ParsesTheCablesWords) {
  EXPECT_EQ(hw::home::parse("up"), kUp);
  EXPECT_EQ(hw::home::parse("down"), kDown);
  EXPECT_EQ(hw::home::parse("2"), kUp);
  EXPECT_EQ(hw::home::parse("0"), kDown);
  EXPECT_FALSE(hw::home::parse("1"));
  EXPECT_FALSE(hw::home::parse("Down"));
  EXPECT_FALSE(hw::home::parse(""));
  EXPECT_FALSE(hw::home::parse(nullptr));
  EXPECT_STREQ(hw::home::name(kDown), "down");
  EXPECT_STREQ(hw::home::name(kUp), "up");
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
