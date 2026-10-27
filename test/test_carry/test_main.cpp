// Carried about (blorb/carry_show.h): each reaction starts from its motion
// (src/hw/carry_ride.h) played through the whole body path, never from a
// gesture, and keeps its cooldown; and on the recordings of real hands
// (test/fixtures/hands_2026-10-19.txt) the gestures start none.
#include <gtest/gtest.h>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "../../src/debug_verbs.h"   // here, so the dependency finder links lib/debugble
#include "carry_badge.h"

using namespace blorb;
using namespace carrytest;
using hw::ride::Kind;
using feedingtest::kHomeDown;
using feedingtest::kHomeUp;

namespace {

struct Segment {
  std::string label;
  std::vector<std::pair<uint32_t, BodySample>> samples;
};

std::vector<Segment> loadHands() {
  std::ifstream f("test/fixtures/hands_2026-10-19.txt");
  std::vector<Segment> out;
  for (std::string line; std::getline(f, line);) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    if (line[0] == '=') {
      Segment s;
      std::string eq;
      in >> eq >> s.label;
      out.push_back(s);
      continue;
    }
    uint32_t ms;
    BodySample b;
    in >> ms >> b.ax >> b.ay >> b.az;
    if (in >> b.peakAx >> b.peakAy >> b.peakAz) b.hasPeak = true;
    out.back().samples.push_back({ms, b});
  }
  return out;
}

// A fresh adult of `species` (default grungo), awake at pet noon, held still.
struct Held {
  Badge b;
  explicit Held(std::optional<SpeciesId> species = std::nullopt, uint8_t home = kHomeDown) : b(home, species) {
    keepAwake(b);
    ride(b, Kind::Still, 2000);
  }
  Starts now() const { return Starts::of(b); }
};

// The motion `k` played once, then still: what it started.
Starts after(Held& h, Kind k, uint32_t stillMs = 3000) {
  const Starts before = h.now();
  keepAwake(h.b);
  ride(h.b, k);
  ride(h.b, Kind::Still, stillMs);
  return h.now().since(before);
}

}  // namespace

// ---- the recordings: gestures start nothing --------------------------------------------

// Every segment of real hands, replayed through the shell and the Dish. The
// gesture segments (knock knocks, single knocks, side holds, short turns,
// hangs, knocks on a table, a desk's noise) start no reaction at all;
// handling (1, 8: picked up, carried, set down hard, pocketed) and the shake
// (9) may, and the test prints what they did.
TEST(Hands, NoGestureInTheRecordingsStartsAReaction) {
  const std::vector<Segment> segs = loadHands();
  ASSERT_GE(segs.size(), 15u);
  for (SpeciesId sp : {kGrungo, SpeciesId{2}, SpeciesId{5}, SpeciesId{9}}) {
    for (const Segment& s : segs) {
      Held h(sp, kHomeUp);   // recorded USB-C up
      const Starts before = h.now();
      const uint32_t t0 = h.b.ms;
      Starts last = before;
      std::string at;
      auto watch = [&] {
        const Starts now = h.now();
        for (uint8_t k = 1; k < kCarryKinds; ++k)
          if (now.n[k] != last.n[k]) {
            const CarryShow::Signals& g = h.b.dish->carry().signals();
            char b[128];
            std::snprintf(b, sizeof b, " %s@%.1f(j%d p%d o%d st%u sw%u why%u)", carryName(Carry(k)), (h.b.ms - t0) / 1000.0,
                          g.jerk, g.pull, g.off, g.steps, g.swings, g.dizzyWhy);
            at += b;
          }
        last = now;
        const char* seg = std::getenv("CARRY_SEG");   // CARRY_SEG=<label>: that segment's signals every 100 ms
        if (seg && s.label == seg && (h.b.ms - t0) % 100 < 40) {
          const CarryShow::Signals& g = h.b.dish->carry().signals();
          std::printf("[ seg ] %7.2f j%4d p%5d o%5d rough%4d bumps%u rattle%5u ride%5u st%2u sw%2u spin%4d turn%d quiet%d why%u %s\n",
                      (h.b.ms - t0) / 1000.0, g.jerk, g.pull, g.off, g.rough, g.bumps, g.rattleMs, g.rideMs, g.steps, g.swings,
                      g.spinDeg, g.turning, g.quiet, g.dizzyWhy, carryName(h.b.dish->carry().playing(h.b.ms)));
        }
      };
      for (size_t i = 0; i < s.samples.size(); i += 500) {
        keepAwake(h.b);
        const size_t end = std::min(s.samples.size(), i + 500);
        replay(h.b, {s.samples.begin() + long(i), s.samples.begin() + long(end)}, watch);
      }
      const Starts d = h.now().since(before);
      const bool handling = s.label.rfind("1_", 0) == 0 || s.label.rfind("8_", 0) == 0 || s.label.rfind("9_", 0) == 0;
      std::printf("[ carry ] %-9s %-26s %s%s%s\n", speciesInfo(sp).name, s.label.c_str(), d.str().c_str(),
                  handling ? "  (handling: allowed)" : "", at.c_str());
      if (!handling) {
        EXPECT_EQ(d.total(), 0) << speciesInfo(sp).name << " " << s.label << d.str();
      }
    }
  }
}

// ---- each motion, played through the body path ---------------------------------------

// What every ride starts, for each species and a few pets of each (their
// genomes jitter the temperament and seed every roll): printed for a person,
// and each reaction must come from its own motion for some pet.
TEST(Rides, EachMotionStartsItsReactionForSomePets) {
  struct Want { Kind ride; Carry react; };
  const Want wants[] = {{Kind::Bumpy, Carry::Cling}, {Kind::Spin, Carry::Dizzy},  {Kind::Shake, Carry::Dizzy},
                        {Kind::Jolt, Carry::Jelly},  {Kind::Swing, Carry::Whee},  {Kind::Walk, Carry::Glance},
                        {Kind::Settle, Carry::Settle}};
  for (const Want& w : wants) {
    int fired = 0, pets = 0;
    std::string line;
    for (size_t sp = 0; sp < kSpeciesCount; ++sp) {
      Held h(SpeciesId{uint8_t(sp)});
      const Starts d = after(h, w.ride);
      ++pets;
      fired += d[w.react] > 0;
      line += std::string(" ") + speciesInfo(SpeciesId{uint8_t(sp)}).name + ":" + d.str().substr(1);
    }
    std::printf("[ carry ] %-7s -> %-6s %d/%d:%s\n", hw::ride::kRides[size_t(w.ride)].name, carryName(w.react), fired,
                pets, line.c_str());
    EXPECT_GT(fired, 0) << hw::ride::kRides[size_t(w.ride)].name;
  }
}

// Held still, or turned in a car, nothing starts (the turn only leans him: the brace).
TEST(Rides, StillAndACarTurnStartNothingButTheTurnLeansHim) {
  for (size_t sp = 0; sp < kSpeciesCount; ++sp) {
    Held h(SpeciesId{uint8_t(sp)});
    EXPECT_EQ(after(h, Kind::Still).total(), 0);
    const Starts before = h.now();
    ride(h.b, Kind::Turn, 1400);   // into the turn
    const CarryView v = h.b.dish->appearance().carry;
    EXPECT_LT(v.braceX, -16) << "leans against a turn toward +x: " << v.braceX;
    ride(h.b, Kind::Turn);
    ride(h.b, Kind::Still, 3000);
    EXPECT_EQ(h.now().since(before).total(), 0) << h.now().since(before).str();
    EXPECT_EQ(h.b.dish->appearance().carry.braceX, 0) << "and stands up again";
  }
}

// ---- never a gesture ------------------------------------------------------------------

// A hand turning the badge from upright (0 degrees) to `deg` in the screen's
// plane over 300 ms, holding it `holdMs`, and back over 300 ms: the side hold
// (90), the hang (180), a tilt to aim (30).
void turnAndHold(Badge& b, int deg, uint32_t holdMs) {
  const uint8_t home = b.hand.home;
  play(b, [&](uint32_t t) {
    const double full = deg * hw::ride::kPi / 180;
    const double k = t < 300 ? t / 300.0 : t < 300 + holdMs ? 1 : t < 600 + holdMs ? 1 - (t - 300 - holdMs) / 300.0 : 0;
    const double th = full * k;
    hw::ride::V v{1000 * std::sin(th), -970 * std::cos(th), -150};
    v.x += hw::ride::noise(t, 1, 12), v.y += hw::ride::noise(t, 2, 12), v.z += hw::ride::noise(t, 3, 12);
    return hw::ride::toRaw(v, home, feedingtest::kR0);
  }, 600 + holdMs + 1500);
}

TEST(Gestures, AKnockKnockASideHoldAHangAndATiltStartNothing) {
  for (size_t sp = 0; sp < kSpeciesCount; ++sp) {
    Held h(SpeciesId{uint8_t(sp)});
    const Starts before = h.now();
    const uint64_t pressesBefore = h.b.count.presses;
    (void)pressesBefore;
    for (int i = 0; i < 3; ++i) {
      keepAwake(h.b);
      EXPECT_GE(h.b.knock({0, 300}).presses, 1) << "the knock knock was heard";
    }
    h.b.hand.knocks.clear();
    turnAndHold(h.b, 90, 2600);    // a side hold (fires the hold)
    turnAndHold(h.b, -90, 2600);   // the other side
    turnAndHold(h.b, 90, 1200);    // a short turn
    for (int aim : {30, -30, 40, -40}) turnAndHold(h.b, aim, 2000);   // tilts, as to aim
    EXPECT_EQ(h.now().since(before).total(), 0) << speciesInfo(SpeciesId{uint8_t(sp)}).name << h.now().since(before).str();
  }
  // The hang last: it opens fight mode.
  for (size_t sp = 0; sp < kSpeciesCount; ++sp) {
    Held h(SpeciesId{uint8_t(sp)});
    const Starts before = h.now();
    turnAndHold(h.b, 180, 2000);
    EXPECT_EQ(h.now().since(before).total(), 0) << speciesInfo(SpeciesId{uint8_t(sp)}).name << h.now().since(before).str();
  }
}

// A reaction playing is cut short the moment a gesture begins, so the hold's
// ring (or the hang's charge) is never drawn over a pet reacting to a ride.
TEST(Gestures, AReactionPlayingIsCutShortByATurn) {
  Held h;
  h.b.dish->forceCarry(Carry::Cling);
  ride(h.b, Kind::Still, 200);
  ASSERT_EQ(h.b.dish->carry().playing(h.b.ms), Carry::Cling);
  turnAndHold(h.b, 90, 2600);
  EXPECT_EQ(h.b.dish->carry().playing(h.b.ms), Carry::None);
  EXPECT_EQ(h.b.dish->appearance().carry.react, Carry::None);
}

// ---- sometimes, not every time ------------------------------------------------------------

// The same motion again at once does not start the same reaction again: each has its cooldown.
TEST(Cooldown, TheSameMotionAgainAtOnceStartsNothingNew) {
  struct Case { Kind ride; Carry react; };
  for (const Case c : {Case{Kind::Jolt, Carry::Jelly}, Case{Kind::Spin, Carry::Dizzy}, Case{Kind::Swing, Carry::Whee},
                       Case{Kind::Bumpy, Carry::Cling}}) {
    int checked = 0;
    for (size_t sp = 0; sp < kSpeciesCount; ++sp) {
      Held h(SpeciesId{uint8_t(sp)});
      // Half a second after each ride: a swing is 6 s, so two swings and two
      // 3 s pauses outlast the shortest whee cooldown (12 s) for some seeds.
      if (after(h, c.ride, 500)[c.react] == 0) continue;   // this pet did not take it the first time
      ++checked;
      EXPECT_EQ(after(h, c.ride, 500)[c.react], 0) << hw::ride::kRides[size_t(c.ride)].name << " "
                                                     << speciesInfo(SpeciesId{uint8_t(sp)}).name;
    }
    EXPECT_GT(checked, 0) << hw::ride::kRides[size_t(c.ride)].name;
  }
}

// Two pets of a kind react differently: the genome jitters the temperament
// and seeds every roll. Over many frogs, a jolt sets some wobbling, not all.
// CarryShow alone, fed a ride in the screen's frame at 50 Hz, as the Dish feeds it.
Starts showRide(CarryShow& show, uint32_t& ms, Kind k, const CarryContext& c, uint32_t forMs = 0) {
  Starts before;
  for (uint8_t i = 0; i < kCarryKinds; ++i) before.n[i] = show.started(Carry(i));
  uint32_t len = forMs;
  for (const hw::ride::Info& r : hw::ride::kRides)
    if (r.kind == k && !len) len = r.ms;
  for (uint32_t t = 0; t < len; t += kSampleMs) {
    const hw::ride::V v = hw::ride::at(k, t);
    BodySample s;
    s.ax = hw::ride::clip(v.x), s.ay = hw::ride::clip(v.y), s.az = hw::ride::clip(v.z);
    show.sample(s, ms += kSampleMs, c);
  }
  Starts now;
  for (uint8_t i = 0; i < kCarryKinds; ++i) now.n[i] = show.started(Carry(i));
  return now.since(before);
}

TEST(Sometimes, AJoltWobblesSomeFrogsNotAll) {
  int wobbled = 0, frogs = 0;
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    CarryShow show;
    CarryContext c;
    c.canReact = true, c.species = kGrungo.v, c.seed = seed * 0x9E3779B1u;
    uint32_t ms = 1000;
    showRide(show, ms, Kind::Still, c, 2000);
    ++frogs;
    wobbled += showRide(show, ms, Kind::Jolt, c)[Carry::Jelly] > 0;
  }
  std::printf("[ carry ] a jolt wobbled %d of %d frogs\n", wobbled, frogs);
  EXPECT_GT(wobbled, frogs / 5);
  EXPECT_LT(wobbled, frogs);
}

// Not in a duel, fight mode, a meal or sleep (canReact false), and not while a pose is held.
TEST(Context, NothingStartsWhenHeCannotReactOrIsPosing) {
  for (Kind k : {Kind::Bumpy, Kind::Jolt, Kind::Spin, Kind::Swing, Kind::Walk, Kind::Shake}) {
    for (int which = 0; which < 2; ++which) {
      CarryShow show;
      CarryContext c;
      c.canReact = which == 1, c.posing = which == 1, c.seed = 0x1234567u;
      uint32_t ms = 1000;
      showRide(show, ms, Kind::Still, c, 1000);
      const Starts d = showRide(show, ms, k, c);
      EXPECT_EQ(d.total(), 0) << hw::ride::kRides[size_t(k)].name << (which ? " posing" : " cannot react") << d.str();
      EXPECT_EQ(show.view(ms).braceX, 0);
    }
  }
}

// Replayed, the same samples draw the same reactions: two badges, one ride.
TEST(Determinism, TheSameRideStartsTheSameReactionsAtTheSameMoments) {
  Held a, b;
  std::vector<CarryView> va, vb;
  for (Kind k : {Kind::Bumpy, Kind::Jolt, Kind::Spin, Kind::Swing, Kind::Walk}) {
    ride(a.b, k, 0, [&] { va.push_back(a.b.dish->appearance().carry); });
    ride(b.b, k, 0, [&] { vb.push_back(b.b.dish->appearance().carry); });
  }
  ASSERT_EQ(va.size(), vb.size());
  for (size_t i = 0; i < va.size(); ++i) {
    EXPECT_EQ(va[i].react, vb[i].react) << i;
    EXPECT_EQ(va[i].ageMs, vb[i].ageMs) << i;
    EXPECT_EQ(va[i].braceX, vb[i].braceX) << i;
  }
}

// Held still, his CarryView is all at rest, so the frame memo skips the frames between ticks as before.
TEST(Memo, HeldStillTheCarryViewIsAtRest) {
  Held h;
  ride(h.b, Kind::Still, 4000);
  const CarryView v = h.b.dish->appearance().carry;
  EXPECT_EQ(v.react, Carry::None);
  EXPECT_EQ(v.ageMs, CarryView::kNever);
  EXPECT_EQ(v.braceX, 0);
  EXPECT_EQ(v.braceY, 0);
}

// Asleep, he reacts to nothing (the brace included).
TEST(Asleep, ASleeperRidesUnmoved) {
  Held h;
  h.b.creature().inject(driveChem(drive::sleepiness), Fx::one());
  h.b.creature().force(action::sleep);
  ride(h.b, Kind::Still, 1000);
  ASSERT_TRUE(h.b.creature().body().asleep);
  // The ride may wake him (the engine feels it); while he sleeps, nothing plays and he does not lean.
  int asleepFrames = 0;
  for (Kind k : {Kind::Bumpy, Kind::Jolt, Kind::Spin, Kind::Swing})
    ride(h.b, k, 0, [&] {
      if (!h.b.creature().body().asleep) return;
      ++asleepFrames;
      const CarryView v = h.b.dish->appearance().carry;
      EXPECT_EQ(v.react, Carry::None);
      EXPECT_EQ(v.braceX, 0);
    });
  EXPECT_GT(asleepFrames, 50);
}

// CARRY_TRACE=1: each ride's signals every 100 ms, for a person tuning them.
TEST(Rides, TracesEachRideWhenAsked) {
  if (!std::getenv("CARRY_TRACE")) GTEST_SKIP();
  for (const hw::ride::Info& r : hw::ride::kRides) {
    Held h;
    uint32_t t0 = h.b.ms, last = 0;
    std::printf("[ trace ] %s\n", r.name);
    auto each = [&] {
      if (h.b.ms - last < 100) return;
      last = h.b.ms;
      const CarryShow::Signals& g = h.b.dish->carry().signals();
      std::printf("[ trace ] %5u j%4d p%5d o%5d push%4d ride%5u rough%5d bumps%u rattle%5u st%2u sw%2u spin%4d turn%d quiet%d why%u fired=%llx %s\n",
                  unsigned(h.b.ms - t0), g.jerk, g.pull, g.off, g.push, g.rideMs, g.rough, g.bumps, g.rattleMs, g.steps,
                  g.swings, g.spinDeg, g.turning, g.quiet, g.dizzyWhy, (unsigned long long)h.b.dish->firedLastSample(),
                  carryName(h.b.dish->carry().playing(h.b.ms)));
    };
    ride(h.b, r.kind, 0, each);
    ride(h.b, Kind::Still, 3000, each);
  }
}

// For a person tuning the thresholds: the most each ride's signals reached.
TEST(Rides, PrintsWhatEachRideMeasures) {
  for (const hw::ride::Info& r : hw::ride::kRides) {
    Held h;
    CarryShow::Signals top;
    auto keep = [&](int16_t& a, int16_t b) { if (std::abs(b) > std::abs(a)) a = b; };
    ride(h.b, r.kind, 0, [&] {
      const CarryShow::Signals& s = h.b.dish->carry().signals();
      keep(top.jerk, s.jerk), keep(top.pull, s.pull), keep(top.off, s.off), keep(top.push, s.push), keep(top.spinDeg, s.spinDeg);
      top.rideMs = std::max(top.rideMs, s.rideMs), top.stillMs = std::max(top.stillMs, s.stillMs);
      top.steps = std::max(top.steps, s.steps), top.swings = std::max(top.swings, s.swings);
    });
    std::printf("[ carry ] %-7s jerk %4d pull %5d off %5d push %4d ride %5u still %5u steps %3u swings %3u spin %4d\n",
                r.name, top.jerk, top.pull, top.off, top.push, top.rideMs, top.stillMs, top.steps, top.swings, top.spinDeg);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
