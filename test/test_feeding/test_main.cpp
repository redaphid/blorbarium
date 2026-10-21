// Feeding, from the knuckle to the dish. The owner (2026-10-21): "The
// 'feeding' mechanism on double knock either doesn't work, or its effects are
// not obvious." So every way a real knock knock could fail to drop a pellet
// is played here through the whole body path the badge runs: 250 Hz reads
// summed into 50 Hz samples with their peak (main.cpp bodyq::sense), the
// shell (hw/feel.h: KnockSense, the power policy, the turn into the screen's
// frame at either home), then the Dish, ticked once a frame as the loop does.
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "fake_air.h"
#include "links.h"
#include "mem_storage.h"
#include "paint/duel.h"
#include "paint/sprite_pack.h"
#include "../../src/debug_verbs.h"
#include "../../src/hw/feel.h"

using namespace blorb;
using blorbtest::Ether;
using blorbtest::FakeAir;
using blorbtest::MemStorage;
using blorbtest::NullLink;
using blorbtest::Weather;

namespace {

constexpr uint8_t kHomeUp = 2;     // ORIENT_HOME: USB-C up (badge128)
constexpr uint8_t kHomeDown = 0;   // USB-C down (badge128_flip, the owner's badges)
constexpr int kR0 = 1;             // ORIENT_R0 on the 1.28

// How the badge lies or is held, in the IMU's raw frame (milli-g; gravity
// reads +1 g along whichever way is up). Held: on edge, upright at its home,
// tipped back a little, with a hand's tremor. Upside down: the hang.
enum class Pose : uint8_t { Held, Table, UpsideDown };

struct Read { int16_t ax, ay, az; };

// The reads a badge gives at `ms` (every 4 ms awake), with knocks: a knuckle
// through the face jolts az one 20 ms step up and the next down (DEBUG knock's
// shape), or on a table a 300 mg blip in one read.
struct Hand {
  uint8_t home = kHomeUp;
  Pose pose = Pose::Held;
  std::vector<uint32_t> knocks;
  Rng rng = Rng::seeded(7);
  Read at(uint32_t ms) {
    const int16_t up = home == 2 ? 1000 : -1000;   // raw +y is up at home 2 (orient.h)
    Read r{0, 0, 0};
    if (pose == Pose::Table) r = {0, 0, 1000};
    else if (pose == Pose::UpsideDown) r = {0, int16_t(-up), 0};
    else r = {0, int16_t(up * 97 / 100), -150};
    const int tremor = pose == Pose::Table ? 2 : 12;
    r.ax = int16_t(r.ax + int(rng.below(2 * tremor + 1)) - tremor);
    r.ay = int16_t(r.ay + int(rng.below(2 * tremor + 1)) - tremor);
    r.az = int16_t(r.az + int(rng.below(2 * tremor + 1)) - tremor);
    for (uint32_t k : knocks) {
      if (ms < k) continue;
      const uint32_t off = ms - k;
      if (pose == Pose::Table) {
        if (off < 4) r.az = int16_t(r.az + 300);
      } else if (off < 20) {
        r.az = int16_t(r.az + 1800);
      } else if (off < 40) {
        r.az = int16_t(r.az - 1700);
      }
    }
    return r;
  }
};

struct Count {
  int pellets = 0;            // pellets that left the pantry
  int pelletsInTheDark = 0;   // ...while the panel was dark
  int presses = 0;            // stim::button the Dish took (a knock knock, as a press)
  bool oneMore = false;       // the rim's "one more" lit
};

// One badge: the IMU, the sense task, the shell and the Dish, on one clock.
struct Badge {
  Ether ether{1, Weather{}};
  FakeAir air{ether, PeerAddr{{2, 0, 0, 0, 0, 1}}};
  MemStorage store;
  std::optional<Dish> dish;
  NullLink link;
  ::power::Policy policy;   // src/hw/power.h (blorb has a namespace power too: the duel powers)
  hw::Feel shell;
  Hand hand;
  uint32_t ms = 0;            // the reads' clock
  uint32_t frameMs = 40;      // how often the loop takes the queue and ticks (main.cpp loop)
  uint32_t nextFrame = 0;
  bool asking = false;        // a question owns BOOT (the update's, the debug unlock's)
  bool keepPantryFull = true;
  Count count;
  // The sense task's queue (main.cpp bodyq::ring) and its peak.
  std::deque<std::pair<BodySample, uint32_t>> ring;
  BodySample handed{};
  bool primed = false;
  int64_t best = -1;
  int16_t pk[3] = {};
  uint8_t sub = 0;

  explicit Badge(uint8_t home) : shell(kR0, home, policy) {
    hand.home = home;
    static const Genome g = starterGenome(33);
    DishOptions o;
    o.founder = &g;
    o.air = &air;
    dish.emplace(store, 33, 0xFEEDull, o);
    static Badge* self;
    self = this;
    const char* why = debugverbs::growTo(*dish, "adult", [](uint32_t ticks) {
      for (uint32_t t = 0; t < ticks; ++t) self->dish->tick(self->ms += 100, self->link);
    });
    EXPECT_EQ(why, nullptr) << why;
    PetClock& c = dish->clock();   // pet noon: awake
    c.phaseOffsetTicks = int32_t(PetClock::kDayTicks / 2) - int32_t(c.petTicks % PetClock::kDayTicks);
    hungry(Fx::ratio(7, 10));
    for (int i = 0; i < 20 && creature().body().asleep; ++i) dish->tick(ms += 100, link);
    nextFrame = ms;
    run(1500);   // held a moment: the shell's history, the orientation settled
  }

  Creature& creature() { return std::get<Creature>(dish->occupant()); }
  void hungry(Fx h) {
    for (const auto& d : DRIVES) creature().inject(driveChem(d.id), Fx::zero());
    creature().inject(driveChem(drive::hunger), h);
  }

  // The loop: the queue through the shell into the Dish, then the ticks due.
  void frame() {
    for (; !ring.empty(); ring.pop_front()) {
      const hw::Felt f = shell.sample(ring.front().first, ring.front().second, asking);
      dish->sample(f.screen, ring.front().second);
      count.oneMore = count.oneMore || f.screen.knockWaiting;
      if (dish->firedLastSample() >> stim::button.v & 1) ++count.presses;
    }
    const uint8_t before = dish->habitat().pantry;
    dish->tick(ms, link);
    const uint8_t after = dish->habitat().pantry;
    if (after < before) {
      count.pellets += before - after;
      if (policy.low()) count.pelletsInTheDark += before - after;
      if (keepPantryFull) dish->habitat().pantry = before;
    }
    policy.update(ms, false);
  }

  // forMs of reads: 250 Hz awake, one per sample while dark (main.cpp bodyq::sense).
  void run(uint32_t forMs) {
    for (const uint32_t end = ms + forMs; ms < end;) {
      const bool fast = !policy.low();
      ms += fast ? 4 : kSampleMs;
      const Read r = hand.at(ms);
      BodySample s;
      s.ax = r.ax, s.ay = r.ay, s.az = r.az;
      const int64_t dx = s.ax - handed.ax, dy = s.ay - handed.ay, dz = s.az - handed.az;
      if (dx * dx + dy * dy + dz * dz > best) best = dx * dx + dy * dy + dz * dz, pk[0] = s.ax, pk[1] = s.ay, pk[2] = s.az;
      if (fast && ++sub < kSampleMs / 4) continue;
      sub = 0;
      s.hasPeak = primed;
      s.peakAx = pk[0], s.peakAy = pk[1], s.peakAz = pk[2];
      ring.push_back({s, ms});
      handed = s, primed = true, best = -1;
      if (int32_t(ms - nextFrame) >= 0) frame(), nextFrame = ms + frameMs;
    }
  }

  // Knocks at these offsets from now, then long enough for any burst to be decided and a pellet to drop.
  Count knock(std::initializer_list<uint32_t> at, uint32_t settleMs = 2500) {
    count = {};
    for (uint32_t a : at) hand.knocks.push_back(ms + 50 + a);
    run(50 + (at.size() ? *(at.end() - 1) : 0) + settleMs);
    hand.knocks.clear();
    return count;
  }
};

}  // namespace

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
    b.policy.sleepNow();
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

// Runs 20 ms at a time until `done` or `limitMs`; true when done.
template <class Pred>
bool runUntil(Badge& b, Pred done, uint32_t limitMs) {
  for (uint32_t end = b.ms + limitMs; b.ms < end;) {
    b.run(20);
    if (done()) return true;
  }
  return false;
}

FeedView feedOf(Badge& b) { return b.dish->appearance().feed; }

}  // namespace

// The second knock flashes the rim at once: ~0.6 s before the knock knock is
// decided and the pellet drops. Then the pellet is in the air, the newest item.
TEST(Show, TheSecondKnockFlashesTheRimBeforeThePelletDrops) {
  Badge b(kHomeUp);
  b.frameMs = 20;
  b.hand.knocks = {b.ms + 50, b.ms + 350};
  ASSERT_TRUE(runUntil(b, [&] { return feedOf(b).heardMs != FeedView::kNever; }, 1000));
  const uint32_t heardAt = b.ms;
  EXPECT_LE(heardAt, b.hand.knocks[1] + 60) << "on the second knock";
  EXPECT_EQ(feedOf(b).droppedMs, FeedView::kNever) << "the pellet is not out yet";
  ASSERT_TRUE(runUntil(b, [&] { return feedOf(b).droppedMs != FeedView::kNever; }, 2000));
  EXPECT_GE(b.ms - heardAt, 500u) << "the flash came first, by the burst's window";
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

// FEEDING_DUMP=<dir> writes the feeding as a strip of frames for a person to
// look at (tools: python3 -c "from PIL import Image; ..." to PNG). FEEDING_PLAIN=1
// draws them as the screen did before feed_show.h (the FeedView cleared).
namespace {
void writePpm(const std::string& path, const paint::Canvas240& cv) {
  FILE* f = std::fopen(path.c_str(), "wb");
  ASSERT_NE(f, nullptr) << path;
  std::fprintf(f, "P6\n240 240\n255\n");
  for (uint16_t p : cv.px) {
    const int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    const uint8_t rgb[3] = {uint8_t(r << 3 | r >> 2), uint8_t(g << 2 | g >> 4), uint8_t(b << 3 | b >> 2)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
}
void shoot(Badge& b, const std::string& dir, const std::string& name) {
  static paint::Canvas240 cv;
  Appearance a = b.dish->appearance();
  if (std::getenv("FEEDING_PLAIN")) a.feed = FeedView{};
  paint::draw(a, paint::packFor(a.species), cv, b.ms);
  paint::drawBootHint(cv, paint::rimHint(a.boot));
  writePpm(dir + "/" + name + ".ppm", cv);
}
}  // namespace

TEST(Film, DumpWhenAsked) {
  const char* dirEnv = std::getenv("FEEDING_DUMP");
  if (!dirEnv) GTEST_SKIP() << "set FEEDING_DUMP to a directory to write the frames";
  const std::string dir = dirEnv;
  {   // hungry: knock knock, the pellet, he eats it
    Badge b(kHomeDown);
    b.frameMs = 20;
    shoot(b, dir, "fed_0_before");
    b.hand.knocks = {b.ms + 50, b.ms + 350};
    b.run(50 + 20);
    shoot(b, dir, "fed_1_first_knock");
    b.run(300);
    shoot(b, dir, "fed_2_second_knock");
    b.run(200);
    shoot(b, dir, "fed_3_heard");
    runUntil(b, [&] { return feedOf(b).droppedMs != FeedView::kNever; }, 2000);
    b.run(160);
    shoot(b, dir, "fed_4_pellet_flying");
    b.run(300);
    shoot(b, dir, "fed_5_pellet_landing");
    b.run(400);
    shoot(b, dir, "fed_6_pellet_landed");
    b.hand.knocks.clear();
    runUntil(b, [&] {
      if (b.ms % 1500 < 20) b.creature().force(action::eat);
      return feedOf(b).chompMs != FeedView::kNever;
    }, 20000);
    b.run(60);
    shoot(b, dir, "fed_7_chomp");
    b.run(400);
    shoot(b, dir, "fed_8_yum");
    b.run(600);
    shoot(b, dir, "fed_9_hearts");
    b.run(900);
    shoot(b, dir, "fed_10_belly_risen");
  }
  {   // full: no thanks
    Badge b(kHomeDown);
    b.frameMs = 20;
    b.hungry(Fx::zero());
    b.hand.knocks = {b.ms + 50, b.ms + 350};
    runUntil(b, [&] { return feedOf(b).refusedMs != FeedView::kNever; }, 2000);
    b.run(600);
    shoot(b, dir, "full_no_thanks");
  }
  {   // asleep
    Badge b(kHomeDown);
    b.frameMs = 20;
    b.creature().inject(driveChem(drive::sleepiness), Fx::one());
    b.creature().force(action::sleep);
    b.run(1000);
    b.hand.knocks = {b.ms + 50, b.ms + 350};
    runUntil(b, [&] { return feedOf(b).refusedMs != FeedView::kNever; }, 2000);
    b.run(700);
    shoot(b, dir, "asleep_zzz");
  }
  {   // the pantry empty
    Badge b(kHomeDown);
    b.frameMs = 20;
    b.keepPantryFull = false;
    b.dish->habitat().pantry = 0;
    b.hand.knocks = {b.ms + 50, b.ms + 350};
    runUntil(b, [&] { return feedOf(b).refusedMs != FeedView::kNever; }, 2000);
    b.run(200);
    shoot(b, dir, "empty_pantry_shake");
    b.run(900);
    shoot(b, dir, "empty_pantry");
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
