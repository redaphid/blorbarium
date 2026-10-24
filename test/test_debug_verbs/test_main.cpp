// The cable's DEBUG verbs the acceptance run needs (#55): `DEBUG chem`, the
// debug link's CHEM without its unlock, and `DEBUG pickup`, a pick-up played
// through the whole body path (feeding_badge.h: the 250 Hz reads, the shell,
// the Dish) so the real HeldDetector fires picked_up, at either home.
#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <utility>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "../../src/debug_verbs.h"   // here, so the dependency finder links lib/debugble
#include "feeding_badge.h"

using namespace blorb;
using namespace feedingtest;

namespace {

bool fired(const Count& c, StimId s) { return c.fired >> s.v & 1; }

// The stimuli a pick-up must never be read as.
void noOtherGesture(const Count& c, const std::string& where) {
  for (StimId s : {stim::knock, stim::double_knock, stim::shake, stim::dropped, stim::flipped, stim::button,
                   stim::button_hold, stim::button_double, stim::hang, stim::cradle, stim::lid_down})
    EXPECT_FALSE(fired(c, s)) << where << ": " << int(s.v);
  EXPECT_EQ(c.pellets, 0) << where;
}

}  // namespace

// ---- DEBUG chem ----------------------------------------------------------------------

TEST(DebugChem, SetsADriveOrAChemicalAsTheLinksChemDoes) {
  Badge b(kHomeUp);
  int level = -1;
  EXPECT_EQ(debugverbs::chem(*b.dish, "hunger", "900", level), nullptr);
  EXPECT_EQ(level, 900);
  EXPECT_EQ(b.creature().chemistry().drive(drive::hunger), Fx::ratio(900, 1000));
  EXPECT_EQ(debugverbs::chem(*b.dish, "toxin", "250", level), nullptr);
  EXPECT_EQ(level, 250);
  EXPECT_EQ(b.creature().chemistry().chem[chem::toxin.v], Fx::ratio(250, 1000));
  EXPECT_EQ(debugverbs::chem(*b.dish, "sleepiness", "0", level), nullptr);
  EXPECT_EQ(level, 0);
  // A chemical by its number too, as CHEM and twists take it.
  EXPECT_EQ(debugverbs::chem(*b.dish, std::to_string(chem::injury.v).c_str(), "400", level), nullptr);
  EXPECT_EQ(b.creature().chemistry().chem[chem::injury.v], Fx::ratio(400, 1000));
}

TEST(DebugChem, RefusesWhatCHEMRefusesAndChangesNothing) {
  Badge b(kHomeUp);
  b.creature().inject(driveChem(drive::hunger), Fx::ratio(1, 2));
  int level = -1;
  for (auto [name, permille] : {std::pair{"nonsense", "5"}, std::pair{"", "5"}, std::pair{"hunger", "1001"},
                                std::pair{"hunger", ""}, std::pair{"hunger", "9x"}, std::pair{"hunger", "-1"}})
    EXPECT_NE(debugverbs::chem(*b.dish, name, permille, level), nullptr) << name << " " << permille;
  EXPECT_EQ(level, -1);
  EXPECT_EQ(b.creature().chemistry().drive(drive::hunger), Fx::ratio(1, 2));
}

TEST(DebugChem, NoCreatureNoChange) {
  blorbtest::MemStorage store;
  Dish dish(store, 7, 0x9f31c2d04a7bull);
  ASSERT_TRUE(std::holds_alternative<Egg>(dish.occupant()));
  int level = -1;
  EXPECT_STREQ(debugverbs::chem(dish, "hunger", "500", level), "no creature in the dish");
}

// ---- DEBUG pickup --------------------------------------------------------------------

BodySample reading(int16_t ax, int16_t ay, int16_t az) {
  BodySample s;
  s.ax = ax, s.ay = ay, s.az = az;
  return s;
}

// The pose itself: from the real reading, still and flat, then tipped toward
// home (never past the face-up line a hang or a side reads), then flat again,
// then back to the real reading, then over.
TEST(DebugPickup, ThePoseLiesLiftsTowardHomeAndEnds) {
  for (uint8_t home : {kHomeUp, kHomeDown}) {
    const BodySample table = reading(0, 0, 1000);
    for (uint32_t t = 0; t < debugverbs::kPickupMs; t += 4) {
      BodySample s = table;
      ASSERT_TRUE(debugverbs::pickupPose(t, home, t * 2654435761u, s));
      EXPECT_GT(s.az, 500) << "face up, so no hang or side: t=" << t;
      EXPECT_GE(home == kHomeUp ? s.ay : -s.ay, -10) << "toward home's up: t=" << t;
    }
    BodySample s = table;
    ASSERT_TRUE(debugverbs::pickupPose(debugverbs::kPickupStillMs + debugverbs::kPickupLiftMs + 100, home, 0, s));
    EXPECT_GT(std::abs(s.ay), 500) << "lifted: past HeldDetector's 0.3 g in the plane";
    EXPECT_FALSE(debugverbs::pickupPose(debugverbs::kPickupMs, home, 0, s));
    // In a hand when asked: it starts and ends where the hand has it, no jolt.
    const int16_t up = home == kHomeUp ? 970 : -970;
    s = reading(0, up, -150);
    ASSERT_TRUE(debugverbs::pickupPose(0, home, 0, s));
    EXPECT_EQ(s.ay, up);
    s = reading(0, up, -150);
    ASSERT_TRUE(debugverbs::pickupPose(debugverbs::kPickupMs - 1, home, 0, s));
    EXPECT_NEAR(s.ay, up, 10);
    BodySample last = reading(0, up, -150);
    for (uint32_t t = 0; t < debugverbs::kPickupMs; t += 4) {
      BodySample n = reading(0, up, -150);
      debugverbs::pickupPose(t, home, 0, n);
      EXPECT_LT(std::abs(n.ax - last.ax) + std::abs(n.ay - last.ay) + std::abs(n.az - last.az), 60) << "t=" << t;
      last = n;
    }
  }
}

// On a table, at either home: picked up while carried, put down after.
TEST(DebugPickup, FiresPickedUpThroughTheRealDetectorAtEitherHome) {
  for (uint8_t home : {kHomeUp, kHomeDown}) {
    const std::string where = "home " + std::to_string(home);
    Badge b(home);
    b.hand.pose = Pose::Table;
    b.run(3000);
    b.count = {};
    b.hand.pickupAt = b.ms + 40;
    b.run(40 + debugverbs::kPickupStillMs + debugverbs::kPickupLiftMs + 1000);   // into the carry
    EXPECT_TRUE(fired(b.count, stim::picked_up)) << where;
    EXPECT_FALSE(fired(b.count, stim::put_down)) << where << ": the table before was already put down";
    b.run(debugverbs::kPickupMs + 2500);   // the rest of it, then the table again
    EXPECT_TRUE(fired(b.count, stim::put_down)) << where;
    noOtherGesture(b.count, where);
  }
}

// Asked while he is already in a hand: the still start puts him down first,
// so the lift is a fresh pick-up, not a hold carried on.
TEST(DebugPickup, AlreadyHeldHeIsPutDownThenPickedUp) {
  for (uint8_t home : {kHomeUp, kHomeDown}) {
    const std::string where = "home " + std::to_string(home);
    Badge b(home);   // held, upright at home
    b.count = {};
    b.hand.pickupAt = b.ms + 40;
    b.run(40 + debugverbs::kPickupStillMs);
    EXPECT_TRUE(fired(b.count, stim::put_down)) << where;
    EXPECT_FALSE(fired(b.count, stim::picked_up)) << where;
    b.run(debugverbs::kPickupLiftMs + 1000);
    EXPECT_TRUE(fired(b.count, stim::picked_up)) << where;
    noOtherGesture(b.count, where);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
