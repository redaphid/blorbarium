// Feeding, from the knuckle to the dish. The owner (2026-10-21): "The
// 'feeding' mechanism on double knock either doesn't work, or its effects are
// not obvious." So every way a real knock knock could fail to drop a pellet
// is played here through the whole body path the badge runs: 250 Hz reads
// summed into 50 Hz samples with their peak (main.cpp bodyq::sense), the
// shell (hw/feel.h: KnockSense, the power policy, the turn into the screen's
// frame at either home), then the Dish, ticked once a frame as the loop does.
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "../../src/debug_verbs.h"   // here, so the dependency finder links lib/debugble
#include "feeding_badge.h"

using namespace blorb;
using namespace feedingtest;

// ---- it works: a knock knock drops one pellet --------------------------------------

TEST(KnockKnock, FeedsHimWithUsbCUpAsHome) {
  Badge b(kHomeUp);
  const Count c = b.knock({0, 300});
  EXPECT_EQ(c.pellets, 1);
  EXPECT_EQ(c.presses, 1) << "one press, from the knock knock";
  EXPECT_TRUE(c.oneMore) << "the first knock lit \"one more\"";
}

// The owner's badges run badge128_flip: home is USB-C down, the screen does
// not turn, and the knocks are judged in the IMU's own frame, so nothing changes.
TEST(KnockKnock, FeedsHimOnAFlippedBadge) {
  Badge b(kHomeDown);
  EXPECT_EQ(b.knock({0, 300}).pellets, 1);
  EXPECT_EQ(b.knock({0, 250, 500}).pellets, 1) << "kids knock thrice";
}

TEST(KnockKnock, FeedsHimLyingOnATable) {
  for (uint8_t home : {kHomeUp, kHomeDown}) {
    Badge b(home);
    b.hand.pose = Pose::Table;
    b.run(1500);
    EXPECT_EQ(b.knock({0, 300}).pellets, 1) << "home " << int(home);
  }
}

// Every recorded knock knock (test/fixtures/hands_2026-10-19.txt) drops a
// pellet through the shell and the Dish, and every stretch of handling
// drops none: what test_knocks proves of KnockSense alone, end to end. Each
// is played again turned half round, as a badge in a flipped case is held,
// with home USB-C down.
TEST(KnockKnock, EveryRecordedKnockKnockFeedsHimAndHandlingNever) {
  std::ifstream f("test/fixtures/hands_2026-10-19.txt");
  ASSERT_TRUE(f.good());
  struct Seg { std::string label; int want = 0; bool atLeast = false; std::vector<std::pair<uint32_t, BodySample>> s; };
  std::vector<Seg> segs;
  for (std::string line; std::getline(f, line);) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    if (line[0] == '=') {
      Seg s;
      std::string eq, kk;
      in >> eq >> s.label >> kk;
      s.atLeast = kk.back() == '+';
      s.want = std::stoi(kk);
      segs.push_back(s);
      continue;
    }
    uint32_t ms;
    BodySample b;
    in >> ms >> b.ax >> b.ay >> b.az;
    if (in >> b.peakAx >> b.peakAy >> b.peakAz) b.hasPeak = true;
    segs.back().s.push_back({ms, b});
  }
  for (const Seg& seg : segs)
    for (uint8_t home : {kHomeUp, kHomeDown}) {
      Badge b(home);
      const uint32_t t0 = b.ms + 20;
      const uint32_t first = seg.s.front().first;
      for (auto [ms, s] : seg.s) {
        if (home == kHomeDown) {   // the same hands, the badge turned half round in its case
          s.ax = int16_t(-s.ax), s.ay = int16_t(-s.ay);
          s.peakAx = int16_t(-s.peakAx), s.peakAy = int16_t(-s.peakAy);
        }
        b.ms = t0 + (ms - first);
        b.ring.push_back({s, b.ms});
        if (int32_t(b.ms - b.nextFrame) >= 0) b.frame(), b.nextFrame = b.ms + b.frameMs;
      }
      for (int i = 0; i < 60; ++i) b.ms += 20, b.ring.push_back({seg.s.back().second, b.ms}), b.frame();
      std::printf("[ feed ] %-24s home %d: %d pellets (want %s%d)\n", seg.label.c_str(), int(home), b.count.pellets,
                  seg.atLeast ? ">=" : "", seg.want);
      if (seg.atLeast) EXPECT_GE(b.count.pellets, seg.want) << seg.label << " home " << int(home);
      else EXPECT_EQ(b.count.pellets, seg.want) << seg.label << " home " << int(home);
    }
}

// ---- the ways it could fail ------------------------------------------------------------

// The loop takes the sense task's queue once a frame. On main a frame is
// ~190 ms; the framerate work makes it 20 ms; a save can stall it for
// seconds. The knocks are judged on the samples' own times, so the frame rate
// never decides whether he is fed.
TEST(KnockKnock, FeedsHimWhateverTheFrameRate) {
  for (uint32_t frameMs : {20u, 40u, 190u, 1000u, 2400u}) {
    Badge b(kHomeDown);
    b.frameMs = frameMs;
    EXPECT_EQ(b.knock({0, 300}, 4000).pellets, 1) << frameMs << " ms frames";
  }
}

// No cooldown swallows the next one: two knock knocks, a breath apart, are two pellets.
TEST(KnockKnock, TwoKnockKnocksAreTwoPellets) {
  Badge b(kHomeUp);
  EXPECT_EQ(b.knock({0, 300, 1100, 1400}).pellets, 2);
}

// One knock is a knock, not a feed: it lights "one more" and the pet asks
// for the second (WHO'S THERE? TRY KNOCK KNOCK!).
TEST(KnockKnock, OneKnockFeedsNobodyButAsksForASecond) {
  Badge b(kHomeUp);
  const Count c = b.knock({0});
  EXPECT_EQ(c.pellets, 0);
  EXPECT_TRUE(c.oneMore);
}

// Known and kept (knocks.h): five or more knocks in one burst are hammering,
// and feed nobody. A kid who hammers is fed by his next knock knock.
TEST(KnockKnock, HammeringFeedsNobodyAndTheNextKnockKnockDoes) {
  Badge b(kHomeUp);
  EXPECT_EQ(b.knock({0, 250, 500, 750, 1000}).pellets, 0);
  EXPECT_EQ(b.knock({0, 300}).pellets, 1);
}

// The pantry holds four and one comes back every two hours (the metabolism
// gene): a fifth knock knock in a row finds it empty and drops nothing. This
// is the likeliest "it doesn't work" on a badge a kid plays with, and the
// screen must say so (feed_show.h).
TEST(KnockKnock, AnEmptyPantryDropsNothing) {
  Badge b(kHomeUp);
  b.keepPantryFull = false;
  int dropped = 0;
  for (int i = 0; i < 5; ++i) dropped += b.knock({0, 300}).pellets;
  EXPECT_EQ(dropped, 4);
  EXPECT_EQ(b.dish->habitat().pantry, 0);
}

// Asleep, a knock knock drops a pellet (the engine's rule; he eats it when he
// wakes, if it has not rotted). The screen says he is asleep (feed_show.h).
TEST(KnockKnock, AsleepAPelletStillDrops) {
  Badge b(kHomeUp);
  b.creature().inject(driveChem(drive::sleepiness), Fx::one());
  b.creature().force(action::sleep);
  b.run(500);
  ASSERT_TRUE(b.creature().body().asleep);
  EXPECT_EQ(b.knock({0, 300}).pellets, 1);
  EXPECT_TRUE(b.creature().body().asleep) << "a pellet does not wake him";
}

// Fight mode keeps the dish, and a knock knock there still feeds him (boot.h
// Lobby). Opened by the hang from home, at either home.
TEST(KnockKnock, InFightModeAKnockKnockStillFeedsHim) {
  for (uint8_t home : {kHomeUp, kHomeDown}) {
    Badge b(home);
    b.hungry(Fx::zero());   // up for a fight: a hungry pet is refused one
    b.hand.pose = Pose::UpsideDown;
    b.run(4500);
    b.hand.pose = Pose::Held;
    b.run(2000);
    ASSERT_TRUE(b.dish->duelActive()) << "the hang opened fight mode, home " << int(home);
    ASSERT_EQ(b.dish->bootContext(), BootContext::Lobby);
    EXPECT_EQ(b.knock({0, 300}).pellets, 1) << "home " << int(home);
  }
}

// A question owns BOOT (the update's "Turn me around!", the debug unlock's),
// and the knock knock is BOOT's twin: it answers nothing and feeds nobody.
// Until 2026-10-21 the shell passed it to the Dish, which dropped a pellet
// behind the update's screen, where it rotted unseen.
TEST(KnockKnock, AQuestionOnTheScreenTakesTheKnockKnock) {
  Badge b(kHomeUp);
  b.asking = true;
  const Count asked = b.knock({0, 300});
  EXPECT_EQ(asked.pellets, 0);
  EXPECT_FALSE(asked.oneMore) << "nor does \"one more\" light under the question";
  b.asking = false;
  EXPECT_EQ(b.knock({0, 300}).pellets, 1) << "once it is answered, a knock knock feeds him";
}

// A dark badge (low power) is woken by the knocks, and nobody feeds him
// unseen: a knock knock whose first knock lit the panel feeds him once lit;
// none drops a pellet while the panel is dark.
TEST(KnockKnock, ADarkBadgeIsLitAndNeverFedInTheDark) {
  for (uint8_t home : {kHomeUp, kHomeDown}) {
    Badge b(home);
    b.policy.sleepNow(0);
    b.run(1000);
    ASSERT_TRUE(b.policy.low());
    const Count c = b.knock({0, 300});
    EXPECT_FALSE(b.policy.low()) << "the knocks woke it, home " << int(home);
    EXPECT_EQ(c.pelletsInTheDark, 0);
    EXPECT_LE(c.pellets, 1);
    EXPECT_EQ(b.knock({0, 300}).pellets, 1) << "lit, the next knock knock feeds him";
  }
}

// ---- what the screen shows (feed_show.h) ---------------------------------------------

namespace {

FeedView feedOf(Badge& b) { return b.dish->appearance().feed; }

}  // namespace

// The second knock flashes the rim at once: ~0.4 s before the knock knock is
// decided and the pellet drops (KnockSense::moreMs: a third knock is waited for). Then the pellet is in the air, the newest item.
TEST(Show, TheSecondKnockFlashesTheRimBeforeThePelletDrops) {
  Badge b(kHomeUp);
  b.frameMs = 20;
  b.hand.knocks = {b.ms + 50, b.ms + 350};
  ASSERT_TRUE(runUntil(b, [&] { return feedOf(b).heardMs != FeedView::kNever; }, 1000));
  const uint32_t heardAt = b.ms;
  EXPECT_LE(heardAt, b.hand.knocks[1] + 40 + KnockSense::kHeardQuietMs + 40) << "as the second knock rings out";
  EXPECT_EQ(feedOf(b).droppedMs, FeedView::kNever) << "the pellet is not out yet";
  ASSERT_TRUE(runUntil(b, [&] { return feedOf(b).droppedMs != FeedView::kNever; }, 2000));
  EXPECT_GE(b.ms - heardAt, KnockSense::moreMs(300) - 100) << "the flash came first, by the burst's window";
  const Appearance a = b.dish->appearance();
  ASSERT_GE(a.feed.flying, 0);
  ASSERT_LT(a.feed.flying, int(a.itemCount));
  EXPECT_EQ(a.feed.refused, FeedRefusal::None) << "hungry: no refusal";
  EXPECT_LT(a.feed.meterMs, 200) << "the belly meter shows";
  EXPECT_EQ(a.feed.pantryMax, 4);
}

TEST(Show, ABootPressFlashesAtOnce) {
  Badge b(kHomeUp);
  BodySample s;
  s.az = 1000, s.buttonDown = true;
  for (int i = 0; i < 6; ++i) b.dish->sample(s, b.ms += 20);
  s.buttonDown = false;
  b.dish->sample(s, b.ms += 20), b.dish->sample(s, b.ms += 20);
  EXPECT_LT(feedOf(b).heardMs, 100);
}

// A press that feeds nobody says why, and never silently.
TEST(Show, ARefusalSaysWhy) {
  {
    Badge b(kHomeUp);
    b.hungry(Fx::zero());
    b.knock({0, 300});
    EXPECT_EQ(feedOf(b).refused, FeedRefusal::Full) << "not hungry: no thanks (the pellet still lies there)";
  }
  {
    Badge b(kHomeUp);
    b.creature().inject(driveChem(drive::sleepiness), Fx::one());
    b.creature().force(action::sleep);
    b.run(500);
    b.knock({0, 300});
    EXPECT_EQ(feedOf(b).refused, FeedRefusal::Asleep);
  }
  {
    Badge b(kHomeUp);
    b.keepPantryFull = false;
    b.dish->habitat().pantry = 0;
    b.knock({0, 300});
    const FeedView f = feedOf(b);
    EXPECT_EQ(f.refused, FeedRefusal::Empty);
    EXPECT_EQ(f.droppedMs, FeedView::kNever);
    EXPECT_GT(f.refill, Fx::zero()) << "the next pellet is on its way";
    EXPECT_LT(f.refill, Fx::one());
  }
  {
    Badge b(kHomeUp);
    b.knock({0, 300});
    EXPECT_EQ(feedOf(b).refused, FeedRefusal::None) << "hungry, awake, a pellet: no refusal";
  }
}

// He bites: the chomp, and the meter rises from where his belly was.
TEST(Show, TheBiteIsAChompAndHisBellyRises) {
  Badge b(kHomeUp);
  b.frameMs = 20;
  b.knock({0, 300}, 1500);
  bool bit = runUntil(b, [&] {
    if (b.ms % 1500 < 20) b.creature().force(action::eat);
    return feedOf(b).chompMs != FeedView::kNever;
  }, 20000);
  ASSERT_TRUE(bit) << "he ate the pellet";
  b.run(300);
  const FeedView f = feedOf(b);
  EXPECT_FALSE(f.rotten);
  EXPECT_GT(f.fullness, f.fullnessFrom) << "fuller after the bite";
  EXPECT_LT(f.meterMs, 400);
}

// ---- home at run time (src/hw/home.h): set from NVS at boot, or by DEBUG home ----------

// A shell built at one home and told the other at boot feels every sample
// exactly as one built at that home: the screen's frame, the hang, the side
// hold. So a universal release on a badge whose NVS says "down" is the flip build.
TEST(Home, AShellToldItsHomeAtRunTimeFeelsAsOneBuiltThere) {
  for (uint8_t told : {kHomeUp, kHomeDown}) {
    ::power::Policy pa, pb;
    hw::Feel built(kR0, told, pa);
    hw::Feel set(kR0, told == kHomeUp ? kHomeDown : kHomeUp, pb);
    set.setHome(told);
    EXPECT_EQ(set.home(), told);
    int hangs = 0, sides = 0;
    // Held at either home, upside down, on each side, face up, tipped: 2 s of each.
    const int16_t poses[][3] = {{0, 1000, 0}, {0, -1000, 0}, {1000, 0, 0}, {-1000, 0, 0}, {0, 0, 1000}, {0, 700, 700}};
    uint32_t ms = 0;
    for (const auto& p : poses)
      for (int i = 0; i < 100; ++i, ms += 20) {
        BodySample s;
        s.ax = p[0], s.ay = p[1], s.az = p[2];
        const hw::Felt a = built.sample(s, ms, false), b = set.sample(s, ms, false);
        ASSERT_EQ(a.screen.ax, b.screen.ax) << "home " << int(told) << " at " << ms;
        ASSERT_EQ(a.screen.ay, b.screen.ay);
        ASSERT_EQ(a.screen.az, b.screen.az);
        ASSERT_EQ(a.screen.invertedDown, b.screen.invertedDown);
        ASSERT_EQ(a.screen.sideDown, b.screen.sideDown);
        ASSERT_EQ(a.rot, b.rot);
        hangs += a.screen.invertedDown, sides += a.screen.sideDown;
      }
    EXPECT_GT(hangs, 0) << "the hang was felt, home " << int(told);
    EXPECT_GT(sides, 0) << "the side hold was felt, home " << int(told);
  }
}

// The hang is upside down from whichever home the badge was told: USB-C up is
// the hang on a flipped badge, and the right way up on any other.
TEST(Home, TheHangFollowsTheHomeTold) {
  for (uint8_t told : {kHomeUp, kHomeDown}) {
    ::power::Policy p;
    hw::Feel shell(kR0, kHomeUp, p);
    shell.setHome(told);
    // Gravity along +y settles rotation 2 (USB-C up), along -y rotation 0 (orient.h, ORIENT_R0 = 1).
    bool hungUsbUp = false, hungUsbDown = false;
    uint32_t ms = 0;
    for (int16_t y : {int16_t(1000), int16_t(-1000)})
      for (int i = 0; i < 100; ++i, ms += 20) {
        BodySample s;
        s.ay = y;
        const hw::Felt f = shell.sample(s, ms, false);
        if (i > 60) (y > 0 ? hungUsbUp : hungUsbDown) |= f.screen.invertedDown;
      }
    EXPECT_EQ(hungUsbUp, told == kHomeDown) << "home " << int(told);
    EXPECT_EQ(hungUsbDown, told == kHomeUp) << "home " << int(told);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
