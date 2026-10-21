// GestureCues (blorb/gesture_cues.h): the progress of a BOOT word in flight,
// from BOOT or its body twin, drawn onto the rim's hint.
#include <gtest/gtest.h>

#include "blorb/dish_duel.h"
#include "blorb/gesture_cues.h"

using namespace blorb;

namespace {

BootHint dressed(const GestureCues& c, BootContext ctx, Fx hang = Fx::zero()) {
  BootHint h = bootHint(ctx, nullptr);
  c.dress(h, bootMeaning(ctx), hang);
  return h;
}

BodySample body(bool button = false, bool knockWaiting = false) {
  BodySample s;
  s.buttonDown = button;
  s.knockWaiting = knockWaiting;
  return s;
}

}  // namespace

TEST(GestureCues, NothingInFlightShowsNothing) {
  GestureCues c;
  for (uint32_t ms = 0; ms < 2000; ms += 20) c.sample(body(), false, ms);
  for (size_t i = 0; i < bootCount(kBoot); ++i) {
    const BootHint h = dressed(c, BootContext(i));
    EXPECT_EQ(h.holdFill, 0) << bootContextName(BootContext(i));
    EXPECT_EQ(h.doubleFill, 0);
    EXPECT_FALSE(h.holdVoid);
    EXPECT_FALSE(h.knockWait);
  }
}

TEST(GestureCues, AFirstKnockLightsOneMoreWhereAPressDoesSomething) {
  GestureCues c;
  c.sample(body(false, true), false, 1000);
  EXPECT_TRUE(dressed(c, BootContext::Awake).knockWait);
  EXPECT_TRUE(dressed(c, BootContext::ClutchPick).knockWait);
  EXPECT_FALSE(dressed(c, BootContext::WindingUp).knockWait) << "a press does nothing while winding up";
  EXPECT_FALSE(dressed(c, BootContext::Fallen).knockWait);
  c.sample(body(false, false), false, 1020);
  EXPECT_FALSE(dressed(c, BootContext::Awake).knockWait) << "the second landed or the window closed: it goes out at once";
}

TEST(GestureCues, TheSideHoldFillsTheRingAndIsTheDuelsLevel) {
  GestureCues c;
  c.sample(body(), false, 0);
  c.sample(body(), true, 1000);
  EXPECT_TRUE(c.holding());
  c.sample(body(), true, 1600);
  EXPECT_EQ(c.heldMs(), 600u);
  EXPECT_EQ(dressed(c, BootContext::Awake).holdFill, 600 * 255 / kSideHoldMs) << "full as the side hold fires, not BOOT's 1.2 s";
  c.sample(body(), false, 5500);   // let go
  c.sample(body(true), false, 6000);
  c.sample(body(true), false, 6600);
  EXPECT_EQ(dressed(c, BootContext::Awake).holdFill, 600 * 255 / kButtonHoldMs) << "BOOT's own hold fills to 1.2 s";
  c.sample(body(), true, 7000);
  c.sample(body(), true, 11000);
  EXPECT_EQ(dressed(c, BootContext::Awake).holdFill, 255) << "full, not wrapped";
  c.sample(body(), false, 11020);
  EXPECT_FALSE(c.holding());
  EXPECT_EQ(dressed(c, BootContext::Awake).holdFill, 0);
}

TEST(GestureCues, AHoldThatDoesNothingHereShowsAFadedRingNotATap) {
  GestureCues c;
  c.sample(body(true), false, 1000);
  c.sample(body(true), false, 1100);
  EXPECT_FALSE(dressed(c, BootContext::Lobby).holdVoid) << "a tap is no hold";
  c.sample(body(true), false, 1000 + GestureCues::kVoidRingAfterMs);
  const BootHint lobby = dressed(c, BootContext::Lobby);
  EXPECT_TRUE(lobby.holdVoid);
  EXPECT_GT(lobby.holdFill, 0);
  EXPECT_FALSE(dressed(c, BootContext::Awake).holdVoid) << "a hold that tucks him in is the real ring";
  EXPECT_FALSE(dressed(c, BootContext::MercyLoser).holdVoid) << "where a hold is the press, holding pleads";
  EXPECT_FALSE(dressed(c, BootContext::OtaQuestion).holdVoid);
}

TEST(GestureCues, AFirstPressWaitsVisiblyForItsSecond) {
  GestureCues c;
  c.sample(body(true), false, 1000);
  c.sample(body(false), false, 1100);
  EXPECT_EQ(c.lastPressMs(), 100u);
  EXPECT_EQ(c.lastGapMs(), UINT32_MAX);
  EXPECT_GT(dressed(c, BootContext::Awake).doubleFill, 200);
  EXPECT_EQ(dressed(c, BootContext::ClutchPick).doubleFill, 0) << "no double here: no bars";
  c.sample(body(false), false, 1100 + kDoublePressMs + 20);
  EXPECT_EQ(dressed(c, BootContext::Awake).doubleFill, 0);
  c.sample(body(true), false, 2000);
  EXPECT_EQ(c.lastGapMs(), 2000u - 1100u);
}

TEST(GestureCues, AHangOnItsWayUpLightsTheDoubleBarsAsFarAsItHasGot) {
  GestureCues c;
  c.sample(body(), false, 1000);
  const uint8_t quarter = dressed(c, BootContext::Awake, Fx::ratio(1, 4)).doubleFill;
  const uint8_t most = dressed(c, BootContext::Awake, Fx::ratio(3, 4)).doubleFill;
  EXPECT_GT(quarter, 0);
  EXPECT_GT(most, quarter);
  EXPECT_GT(dressed(c, BootContext::Lobby, Fx::ratio(1, 2)).doubleFill, 0) << "the hang leaves the lobby too";
  EXPECT_EQ(dressed(c, BootContext::Staking, Fx::ratio(1, 2)).doubleFill, 0) << "no double while staking";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
