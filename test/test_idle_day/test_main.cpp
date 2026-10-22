// Days and nights on a badge as it is really kept (docs/notes/sleep-idle.md):
// the pet driven at 50 Hz through the Dish exactly as main.cpp does, for a
// day or three of wall time, on a desk most of the day with a kid glancing
// at it now and then, and on the nightstand all night. With no phone and no
// clock (an RTC that never knows the time), or with a phone's TIME at the
// start, as the page sends it on connecting.
//
// The owner, 2026-10-21: "They all also sleep constantly! I need them to do
// more interesting things at idle." The floors below hold what that fixed;
// IDLE_DAY_SWEEP=1 prints the per-hour chart for every case (IDLE_DAY_HOURS,
// default 24), and IDLE_DAY_OUT=<dir> writes each run's hours as CSV.
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/registry.h"
#include "blorb/species.h"
#include "founding_040.h"
#include "links.h"
#include "mem_storage.h"
#include "time_sources.h"
#include "traces.h"
#include "paint/sprite_pack.h"
#include "paint/tween.h"
#include "blorb/genes.h"
#include "blorb/genome.h"
#include "blorb/idle.h"
#include "../../lib/paint/src/lively.h"   // the idle layer (#61: his genes and pace in his motion)

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint32_t kMinuteMs = 60000;
constexpr uint32_t kHourMs = 60 * kMinuteMs;
constexpr uint32_t kTicksPerSec = 1000 / kTickMs;
constexpr uint32_t kStartHour = 7;   // the wall clock when the run starts: a kid's morning
// 2026-10-21 07:00 UTC, so with tz 0 the wall hour is kStartHour + hours run.
constexpr uint32_t kStartUnix = 1792540800u + kStartHour * 3600u;

enum class Clock : uint8_t { None, Phone };
enum class Night : uint8_t { Nightstand, Tucked };   // face up all night, or face down
const char* clockName(Clock c) { return c == Clock::None ? "noclock" : "phone"; }
const char* nightName(Night n) { return n == Night::Nightstand ? "faceup" : "tucked"; }

uint32_t wallHour(uint32_t ms) { return (kStartHour + ms / kHourMs) % 24; }
bool kidAwake(uint32_t ms) { const uint32_t h = wallHour(ms); return h >= 7 && h < 21; }

// The kid: on the desk from 07:00 to 21:00 with a look every 4 to 25 minutes
// (a knock, a knock knock for a pellet when the rim says feed, a pick-up of
// 10 to 40 s, a tilt), and nothing at all at night. Tucked: face down at
// 21:00, picked up at 07:00.
class Kid {
 public:
  Kid(uint64_t seed, Night night) : rng_(Rng::seeded(seed ^ 0x51EE9ull)), night_(night) {}
  struct Counts { uint32_t looks = 0, knocks = 0, feeds = 0, pickups = 0, tilts = 0; };

  BodySample next(uint32_t ms, const Dish& dish, const Appearance& a) {
    if (!kidAwake(ms)) {
      g_ = G::Rest, until_ = ms;
      return night_ == Night::Tucked ? faceDown() : still();
    }
    if (ms >= until_) choose(ms, dish, a);
    BodySample s = still();
    switch (g_) {
      case G::Knock:
        if (!sent_) s.tapCode = knockCode_, sent_ = true;
        break;
      case G::Hold: s = cradled(); break;
      case G::Tilt: s.ax = tiltX_, s.az = 975; break;
      case G::Rest: break;
    }
    return s;
  }
  const Counts& counts() const { return n_; }

 private:
  enum class G : uint8_t { Rest, Knock, Hold, Tilt };
  void act(G g, uint32_t ms, uint32_t forMs) { g_ = g, until_ = ms + forMs, sent_ = false; }
  uint32_t between(uint32_t lo, uint32_t hi) { return lo + rng_.below(hi - lo + 1); }

  void choose(uint32_t ms, const Dish& dish, const Appearance& a) {
    if (g_ != G::Rest) return act(G::Rest, ms, 1500);
    if (!looked_) {
      looked_ = true;
      return act(G::Rest, ms, between(4, 25) * kMinuteMs);
    }
    looked_ = false;
    ++n_.looks;
    const bool pelletDown = dish.habitat().nearestPellet(DishPos{}).has_value();
    if (a.hasHint && a.hint == care::feed && !pelletDown && dish.habitat().pantry > 0) {
      ++n_.feeds, knockCode_ = 2;   // knock knock: a press, a pellet
      return act(G::Knock, ms, 300);
    }
    switch (rng_.below(6)) {
      case 0: case 1: ++n_.knocks, knockCode_ = 1; return act(G::Knock, ms, 300);
      case 2: case 3: ++n_.pickups; return act(G::Hold, ms, between(10, 40) * 1000);
      case 4: ++n_.tilts, tiltX_ = int16_t(rng_.below(2) ? 220 : -220); return act(G::Tilt, ms, between(4, 10) * 1000);
      default: return act(G::Rest, ms, 1000);   // a look, no touch
    }
  }

  Rng rng_;
  Night night_;
  G g_ = G::Rest;
  uint32_t until_ = 0;
  bool looked_ = false, sent_ = false;
  uint8_t knockCode_ = 1;
  int16_t tiltX_ = 0;
  Counts n_;
};

struct DayRun {
  std::string species;
  Clock clock = Clock::None;
  Night night = Night::Nightstand;
  uint8_t foundingHour = 9;
  bool founded040 = false;   // founded by the 0.4.0 recipe (founding_040.h): the long sleeper
  double hours = 24;
  double asleepMin[96]{};                   // per wall hour since the start
  std::map<std::string, uint32_t> actionTicks[96];
  double dayAwake = 0, dayTicks = 0, nightAsleep = 0, nightTicks = 0;
  uint32_t naps = 0;
  double sleepyMax = 0;
  bool died = false;
  // Kinds of thing he did, per 2-minute window, while awake in the day and watched.
  double kindsPerWindow = 0;
  uint32_t windows = 0;
  std::set<std::string> kinds;
};

double pm(Fx v) { return double(v.raw) / double(Fx::kOne); }

// What STATE says he is doing (an idle he plays by its own name, where this
// build has idles), so the same file films a checkout from before them.
#if __has_include("blorb/idle.h")
std::string doing(const Creature& c) { return c.doing(); }
#else
std::string doing(const Creature& c) { return ACTIONS[c.action().v].name; }
#endif
// What a kid can tell apart on the glass: an idle by its name, anything else
// by its pose (a rest, a wander and a tilt-follow on a level desk all look
// like him standing about).
std::string looks(const Creature& c) {
#if __has_include("blorb/idle.h")
  if (c.playing()) return c.doing();
#endif
  return POSES[c.body().pose.v].name;
}

DayRun live(SpeciesId species, uint32_t seed, Clock clock, Night night, uint8_t foundingHour, double hours,
         bool founded040 = false) {
  DayRun r;
  r.species = speciesInfo(species).name;
  r.clock = clock, r.night = night, r.foundingHour = foundingHour, r.hours = hours, r.founded040 = founded040;
  MemStorage store;
  NullLink link;
  FakeRtc rtc;   // a flat battery: never any wall time
  uint32_t ms = 0;
  rtc.poweredMs = &ms;
  const Genome founder = founded040 ? foundingGenome040(seed) : starterGenomeOf(species, seed);
  DishOptions opt;
  opt.rtc = &rtc;
  opt.founder = &founder;
  opt.foundingHour = foundingHour;
  Dish dish(store, seed, seed, opt);
  if (clock == Clock::Phone) {   // what cmd_time does
    dish.phoneTime().set(kStartUnix);
    dish.checkWall();
    dish.clock().alignToWall(kStartUnix, 0);
    dish.setZone(0);
  }
  Kid kid(seed, night);
  Appearance a = dish.appearance();
  uint32_t lastTick = 0, windowStart = 0;
  bool wasAsleep = false;
  std::set<std::string> window;
  const uint32_t endMs = uint32_t(hours * kHourMs);
  for (; ms < endMs; ms += kSampleMs) {
    dish.sample(kid.next(ms, dish, a), ms);
    dish.tick(ms, link);
    if (dish.tickCount() == lastTick) continue;
    lastTick = dish.tickCount();
    a = dish.appearance();
    if (std::holds_alternative<Clutch>(dish.occupant())) r.died = true;
    const Creature* c = std::get_if<Creature>(&dish.occupant());
    if (!c) continue;
    const uint32_t h = std::min<uint32_t>(95, ms / kHourMs);
    const bool asleep = c->body().asleep;
    if (asleep) r.asleepMin[h] += 1.0 / (60 * kTicksPerSec);
    if (asleep && !wasAsleep) ++r.naps;
    wasAsleep = asleep;
    r.sleepyMax = std::max(r.sleepyMax, pm(c->chemistry().drive(drive::sleepiness)));
    const std::string act = asleep ? "sleep" : doing(*c);
    r.actionTicks[h][act]++;
    if (kidAwake(ms)) {
      r.dayTicks += 1, r.dayAwake += !asleep;
      if (!asleep) window.insert(looks(*c)), r.kinds.insert(looks(*c));
      if (ms - windowStart >= 2 * kMinuteMs) {
        if (!window.empty()) r.kindsPerWindow += double(window.size()), ++r.windows;
        window.clear(), windowStart = ms;
      }
    } else {
      r.nightTicks += 1, r.nightAsleep += asleep;
      windowStart = ms, window.clear();
    }
  }
  return r;
}

double dayAwakePct(const DayRun& r) { return r.dayTicks ? 100.0 * r.dayAwake / r.dayTicks : 0; }
double nightAsleepPct(const DayRun& r) { return r.nightTicks ? 100.0 * r.nightAsleep / r.nightTicks : 0; }

std::string label(const DayRun& r) {
  char b[96];
  std::snprintf(b, sizeof b, "%-8s%s %-7s %-6s pet%02u", r.species.c_str(), r.founded040 ? "(0.4.0)" : "", clockName(r.clock),
                nightName(r.night), unsigned(r.foundingHour));
  return b;
}

// One line per run: a cell per wall hour, '#' asleep 45+ min, '+' 15-45, '.' under 15.
std::string chart(const DayRun& r) {
  std::string s = label(r) + " |";
  for (int h = 0; h < int(r.hours); ++h) {
    const double m = r.asleepMin[h];
    s += m >= 45 ? '#' : m >= 15 ? '+' : '.';
    if ((kStartHour + h + 1) % 24 == 0) s += '|';
  }
  char b[160];
  std::snprintf(b, sizeof b, "| day awake %5.1f%%  night asleep %5.1f%%  naps %3u  kinds/2min %.1f  kinds %zu%s",
                dayAwakePct(r), nightAsleepPct(r), unsigned(r.naps), r.windows ? r.kindsPerWindow / r.windows : 0.0,
                r.kinds.size(), r.died ? "  DIED" : "");
  return s + b;
}

void writeCsv(const DayRun& r) {
  const char* dir = std::getenv("IDLE_DAY_OUT");
  if (!dir) return;
  std::string name = r.species + (r.founded040 ? "-040" : "") + "-" + clockName(r.clock) + "-" + nightName(r.night) + "-pet" +
                     std::to_string(r.foundingHour) + ".csv";
  if (FILE* f = std::fopen((std::string(dir) + "/" + name).c_str(), "w")) {
    std::fprintf(f, "hour,wall_hour,asleep_min,actions\n");
    for (int h = 0; h < int(r.hours); ++h) {
      std::string acts;
      for (const auto& [n, t] : r.actionTicks[h]) acts += n + ":" + std::to_string(t) + " ";
      std::fprintf(f, "%d,%u,%.1f,%s\n", h, unsigned((kStartHour + h) % 24), r.asleepMin[h], acts.c_str());
    }
    std::fclose(f);
  }
}

double envHours() {
  const char* h = std::getenv("IDLE_DAY_HOURS");
  return h ? std::atof(h) : 24.0;
}

}  // namespace

// The measurement: species of both kinds (day and night sleepers), each clock,
// face up and tucked in, a fresh no-phone founding (pet 09:00) and an old one
// (pet 00:00, a 0.4.0 board), and a grungo 0.4.0 founded by its own recipe.
TEST(Sweep, DaysAndNightsOnADesk) {
  if (!std::getenv("IDLE_DAY_SWEEP")) GTEST_SKIP() << "IDLE_DAY_SWEEP=1 runs it";
  const double hours = envHours();
  std::printf("wall hours from %02u:00; '#' asleep 45+ min of the hour, '+' 15-45, '.' under 15; '|' wall midnight\n",
              unsigned(kStartHour));
  for (SpeciesId sp : {SpeciesId{2}, SpeciesId{0}, SpeciesId{5}, SpeciesId{7}, SpeciesId{1}})
    for (Clock c : {Clock::None, Clock::Phone})
      for (Night n : {Night::Nightstand, Night::Tucked})
        for (uint8_t founding : {uint8_t(9), uint8_t(0)}) {
          if (c == Clock::Phone && founding == 0) continue;   // TIME snaps the clock: the founding hour is gone
          const DayRun r = live(sp, 0x5EED0000u, c, n, founding, hours);
          std::printf("%s\n", chart(r).c_str());
          std::fflush(stdout);
          writeCsv(r);
        }
  for (Clock c : {Clock::None, Clock::Phone}) {
    const DayRun r = live(SpeciesId{0}, 0x5EED0000u, c, Night::Nightstand, 0, hours, /*founded040=*/true);
    std::printf("%s\n", chart(r).c_str());
    writeCsv(r);
  }
}

// A fresh pet, hatched and awake, on the desk with no clock.
struct Desk {
  MemStorage store;
  NullLink link;
  FakeRtc rtc;
  uint32_t ms = 0;
  Genome founder;
  std::optional<Dish> dish;
  explicit Desk(SpeciesId sp, uint8_t foundingHour = kUnaskedFoundingHour, uint32_t seed = 0x5EED0000u,
                const Genome* genome = nullptr)
      : founder(genome ? *genome : starterGenomeOf(sp, seed)) {
    rtc.poweredMs = &ms;
    DishOptions opt;
    opt.rtc = &rtc;
    opt.founder = &founder;
    opt.foundingHour = foundingHour;
    dish.emplace(store, seed, seed, opt);
    run(20 * kMinuteMs);   // a 15-minute egg
  }
  void run(uint32_t forMs, BodySample s = still()) {
    for (uint32_t end = ms + forMs; ms < end; ms += kSampleMs) {
      dish->sample(s, ms);
      dish->tick(ms, link);
    }
  }
  void knock() {
    BodySample s = still();
    s.tapCode = 1;
    dish->sample(s, ms);
    run(500);
  }
  Creature& c() { return std::get<Creature>(dish->occupant()); }
};

// The field bug: a board with no clock founded at pet 00:00 (a 0.4.0 board)
// believed it was night for its first ten hours, and every pet hour from
// 21:00 to 07:00 after, whatever the wall said. Unknown time is not night.
TEST(NoClock, UnknownTimeIsNeverNight) {
  Desk d(SpeciesId{2}, /*foundingHour=*/0);
  for (int i = 0; i < 12; ++i) {
    d.run(60 * kMinuteMs, i % 2 ? still() : cradled());   // handled now and then
    EXPECT_FALSE(d.dish->clock().night()) << "hour " << i;
  }
  EXPECT_FALSE(d.dish->timeKnown());
}

// A phone's TIME makes the pet hours the wall's, and night is believed again.
TEST(Phone, TimeBringsTheNightBack) {
  Desk d(SpeciesId{2}, 0);
  d.dish->phoneTime().set(kStartUnix + 16 * 3600);   // 23:00
  d.dish->checkWall();
  d.dish->clock().alignToWall(kStartUnix + 16 * 3600, 0);
  d.run(1000);
  EXPECT_TRUE(d.dish->clock().night());
  EXPECT_TRUE(d.dish->clock().dark());
}

// Left untouched long enough it is dim for him, and a hand ends it.
TEST(NoClock, ALongUntouchedStretchIsDimAndAHandEndsIt) {
  Desk d(SpeciesId{0});
  d.run(3000, cradled());
  d.run(PetClock::kDimAfterTicks * kTickMs - 30 * 1000);
  EXPECT_FALSE(d.dish->clock().dim());
  d.run(60 * 1000);
  EXPECT_TRUE(d.dish->clock().dim());
  EXPECT_TRUE(d.dish->clock().dark());
  d.run(3000, cradled());
  EXPECT_FALSE(d.dish->clock().dim());
}

// A knock gets a sleeping pet up, and he stays up a while.
TEST(Rouse, AKnockWakesHimAndHeStaysUp) {
  Desk d(SpeciesId{5});
  d.c().inject(driveChem(drive::sleepiness), Fx::one());
  d.c().force(action::sleep);
  d.run(5000);
  ASSERT_TRUE(d.c().body().asleep);
  d.knock();
  EXPECT_FALSE(d.c().body().asleep);
  EXPECT_LE(d.c().chemistry().drive(drive::sleepiness), Fx::ratio(46, 100));
  d.run(5 * kMinuteMs);
  EXPECT_FALSE(d.c().body().asleep) << "roused, he is up for more than a moment";
}

// Face down is bedtime, and a knock does not get him up while he lies there.
TEST(Rouse, LiddedHeSleepsOn) {
  Desk d(SpeciesId{5});
  d.run(40 * 1000, faceDown());
  ASSERT_TRUE(d.c().body().asleep);
  BodySample s = faceDown();
  s.tapCode = 1;
  d.dish->sample(s, d.ms);
  d.run(2000, faceDown());
  EXPECT_TRUE(d.c().body().asleep);
}

// The owner's ask: no clock, the badge on a desk, a kid looking now and then
// by day and nobody at night. Awake most of the kid's day; asleep most of the
// night, because nobody touched him (dim), not because of a clock he has not got.
TEST(NoClock, AKidsDayIsMostlyAwakeAndTheNightMostlyAsleep) {
  // A night-sleeper sleeps most of the night; a species that sleeps by day
  // (nocturnal, species.def) is up for the first of it, and sleeps the rest.
  for (SpeciesId sp : {SpeciesId{2}, SpeciesId{0}}) {
    const DayRun r = live(sp, 0x5EED0000u, Clock::None, Night::Nightstand, 0, 24);
    EXPECT_GE(dayAwakePct(r), 80) << chart(r);
    EXPECT_GE(nightAsleepPct(r), speciesInfo(sp).starter.nocturnal ? 45 : 75) << chart(r);
    EXPECT_FALSE(r.died) << chart(r);
  }
}

// What he does, awake by day: many different things, several in any two minutes.
TEST(Idle, AWideVarietyEveryFewMinutes) {
  for (SpeciesId sp : {SpeciesId{2}, SpeciesId{7}, SpeciesId{5}}) {
    const DayRun r = live(sp, 0x5EED0000u, Clock::None, Night::Nightstand, 9, 8);
    EXPECT_GE(r.kinds.size(), 14u) << chart(r);
    EXPECT_GE(r.kindsPerWindow / std::max(1u, r.windows), 3.0) << chart(r);
  }
}

// Each species has favourites, and two pets of one species differ (their genes).
TEST(Idle, FavouritesBySpeciesAndByPet) {
  auto top = [](SpeciesId sp, uint32_t seed) {
    Desk d(sp, kUnaskedFoundingHour, seed);
    std::map<std::string, uint32_t> n;
    for (int i = 0; i < 4 * 3600 * 10; ++i) {
      d.run(kTickMs, i % (10 * 600) < 30 ? cradled() : still());   // picked up every ten minutes, so it is never dim
      if (d.c().playing() && d.c().idle().ticks == 1) ++n[d.c().doing()];
    }
    return n;
  };
  auto sum = [](const std::map<std::string, uint32_t>& n, std::initializer_list<const char*> names) {
    uint32_t t = 0;
    for (const char* k : names) t += n.count(k) ? n.at(k) : 0;
    return t;
  };
  const auto puff = top(SpeciesId{2}, 1), owl = top(SpeciesId{7}, 1), puff2 = top(SpeciesId{2}, 2);
  const auto bouncy = {"dance", "roll_over", "chase_tail", "zoomies", "sneeze"};
  const auto watchful = {"look_around", "daydream"};
  auto all = [](const std::map<std::string, uint32_t>& n) {
    uint32_t t = 0;
    for (const auto& kv : n) t += kv.second;
    return t;
  };
  // Shares of all he played, so a pet with fewer favourites (each on its cooldown) compares fairly.
  EXPECT_GT(sum(puff, bouncy) * all(owl), 2 * sum(owl, bouncy) * all(puff)) << "a puffball bounces, an owl less";
  EXPECT_GT(2 * sum(owl, watchful) * all(puff), 3 * sum(puff, watchful) * all(owl)) << "an owl watches, a puffball less";
  EXPECT_NE(puff, puff2) << "two puffballs, their own favourites";
}

// A kid's hand gets a hello: knocked while he is awake and content, the next
// thing he plays is for the kid (a Show idle or a dance), within seconds.
TEST(Idle, AKnockGetsAGreeting) {
  int greeted = 0, tries = 0;
  for (SpeciesId sp : {SpeciesId{2}, SpeciesId{3}, SpeciesId{7}, SpeciesId{8}}) {
    Desk d(sp);
    for (int k = 0; k < 6; ++k) {
      d.run(90 * 1000);
      if (d.c().body().asleep) continue;
      ++tries;
      d.knock();
      for (int i = 0; i < 50; ++i) {
        d.run(kTickMs);
        const IdleInfo* row = idleInfo(d.c().idle().now);
        if (d.c().playing() && row && d.c().idle().ticks <= 2) {
          greeted += row->family == IdleFamily::Show || row->id == idles::dance;
          break;
        }
      }
    }
  }
  EXPECT_GE(greeted * 2, tries) << greeted << " greetings of " << tries << " knocks";
}

// STATE says the idle by name, and an idle half done survives a save and a reload.
TEST(Idle, StateNamesItAndASaveKeepsIt) {
  Desk d(SpeciesId{2});
  for (int i = 0; i < 20 * 600 && !(d.c().playing() && d.c().idle().ticks == 5); ++i) d.run(kTickMs);
  ASSERT_TRUE(d.c().playing());
  const std::string name = d.c().doing();
  EXPECT_NE(name, "rest");
  ASSERT_TRUE(d.dish->flush());
  const uint32_t before = d.dish->hash();
  DishOptions opt;
  opt.founder = &d.founder;
  Dish again(d.store, 0x5EED0000u, 0x5EED0000u, opt);
  ASSERT_EQ(again.boot(), Boot::Resumed);
  EXPECT_EQ(again.hash(), before);
  EXPECT_EQ(std::get<Creature>(again.occupant()).doing(), name);
}

// ---- rot (#21; owner, 2026-10-21: "refusing rotten food unless starving") ---------

// A pellet `ageTicks` old at `at`, in the first free slot.
void putPellet(Desk& d, DishPos at, uint32_t ageTicks) {
  for (Pellet& p : d.dish->habitat().pellets)
    if (!p.present) {
      p = Pellet{at, d.dish->tickCount() - ageTicks, true};
      return;
    }
}
uint32_t rotTicks(Desk& d) { return d.c().phenotype().habitat.rotTicks; }

// Hungry but not starving, with only rot in the dish: he goes up to it, sniffs
// it and turns away (the yuck face), and eats none of it. Several genomes.
TEST(Rot, TurnedDownUnlessStarving) {
  for (SpeciesId sp : {SpeciesId{0}, SpeciesId{2}, SpeciesId{5}, SpeciesId{7}}) {
    Desk d(sp);
    putPellet(d, DishPos{Fx::ratio(3, 10), Fx::ratio(2, 10)}, rotTicks(d) + 10);
    const uint16_t fed = d.c().stats().fed;
    bool yuck = false;
    for (int i = 0; i < 20 * 600; ++i) {
      d.c().inject(driveChem(drive::hunger), Fx::ratio(65, 100));   // hungry, short of kEatsRotAt
      d.run(kTickMs, i % 6000 < 30 ? cradled() : still());           // looked at now and then
      yuck = yuck || (d.c().picky().refusedAt && d.c().face().current == expr::annoyed);
    }
    EXPECT_EQ(d.c().stats().fed, fed) << speciesInfo(sp).name << " ate rot while not starving";
    EXPECT_EQ(d.c().chemistry().chem[chem::toxin.v], Fx::zero()) << speciesInfo(sp).name;
    EXPECT_TRUE(yuck) << speciesInfo(sp).name << " never sniffed it and turned away";
  }
}

// A fresh pellet beside the rot: he eats the fresh one.
TEST(Rot, TheFreshOneOverTheRot) {
  Desk d(SpeciesId{2});
  putPellet(d, DishPos{Fx::ratio(1, 10), Fx::zero()}, rotTicks(d) + 10);
  putPellet(d, DishPos{Fx::ratio(-5, 10), Fx::ratio(2, 10)}, 10);
  d.c().inject(driveChem(drive::hunger), Fx::ratio(7, 10));
  for (int i = 0; i < 10 * 600 && d.c().stats().fed == 0; ++i) {
    if (i % 50 == 0 && d.c().action() != action::eat) d.c().force(action::eat);
    d.run(kTickMs);
  }
  ASSERT_GE(d.c().stats().fed, 1);
  EXPECT_EQ(d.c().chemistry().chem[chem::toxin.v], Fx::zero()) << "he ate the rotten one";
}

// Starving, he eats rot, and it poisons him: neglect can still hurt.
TEST(Rot, EatenWhenStarving) {
  for (SpeciesId sp : {SpeciesId{0}, SpeciesId{5}}) {
    Desk d(sp);
    putPellet(d, DishPos{Fx::ratio(2, 10), Fx::zero()}, rotTicks(d) + 10);
    const uint16_t fed = d.c().stats().fed;
    for (int i = 0; i < 10 * 600 && d.c().stats().fed == fed; ++i) {
      d.c().inject(driveChem(drive::hunger), Fx::ratio(9, 10));
      if (i % 50 == 0 && d.c().action() != action::eat) d.c().force(action::eat);
      d.run(kTickMs);
    }
    EXPECT_GT(d.c().stats().fed, fed) << speciesInfo(sp).name;
    d.run(2000);
    EXPECT_GT(d.c().chemistry().chem[chem::toxin.v] + d.c().chemistry().chem[chem::injury.v], Fx::zero())
        << speciesInfo(sp).name << ": rot still poisons";
  }
}

// The test puffkin's death (2026-10-21): a kid knocks pellets in while he
// sleeps, they rot, he wakes hours later and eats them all. Now he turns them
// down until he is starving, eats one then, and the days after are lived.
TEST(Rot, PelletsKnockedInWhileHeSleptNoLongerKillHim) {
  for (SpeciesId sp : {SpeciesId{2}, SpeciesId{0}, SpeciesId{5}}) {
    Desk d(sp);
    d.run(40 * 1000, faceDown());   // bedtime
    ASSERT_TRUE(d.c().body().asleep);
    for (int k = 0; k < 4; ++k) {   // four knock knocks, a pellet each
      BodySample s = faceDown();
      s.tapCode = 2;
      d.dish->sample(s, d.ms);
      d.run(3000, faceDown());
    }
    d.run(2 * 60 * kMinuteMs, faceDown());   // two hours: they rot
    d.run(3000);                           // turned back up: morning
    // Every pellet in the dish is rotten now: each bite he takes, he takes starving.
    uint16_t fed = d.c().stats().fed;
    Fx hunger = d.c().chemistry().drive(drive::hunger);
    int bites = 0;
    for (int t = 0; t < 8 * 3600 * 10; ++t) {
      d.run(kTickMs, (t / 36000) % 2 ? still() : cradled());
      ASSERT_TRUE(std::holds_alternative<Creature>(d.dish->occupant())) << speciesInfo(sp).name << " died at hour " << t / 36000;
      if (d.c().stats().fed != fed) {
        ++bites;
        EXPECT_GE(hunger, kEatsRotAt - Fx::ratio(2, 100)) << speciesInfo(sp).name << " ate rot not starving, hour " << t / 36000;
        fed = d.c().stats().fed;
      }
      hunger = d.c().chemistry().drive(drive::hunger);
    }
    EXPECT_LE(bites, 3) << speciesInfo(sp).name;
    EXPECT_LT(d.c().chemistry().chem[chem::injury.v], Fx::ratio(9, 10)) << speciesInfo(sp).name;
  }
}

void writePpm(const std::string& path, const paint::Canvas240& cv) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  std::fprintf(f, "P6\n240 240\n255\n");
  for (uint16_t p : cv.px) {
    const int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    const uint8_t rgb[3] = {uint8_t(r << 3 | r >> 2), uint8_t(g << 2 | g >> 4), uint8_t(b << 3 | b >> 2)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
}

// A few minutes of a fresh pet left on the desk with no clock, filmed: from
// IDLE_FILM_FROM minutes after founding (default 40), a frame every
// IDLE_FILM_EVERY ms (default 2500) for IDLE_FILM_MINUTES (default 4), each
// as the screen draws it between ticks (paint/tween.h), named with what STATE
// says he is doing. scripts/test/idle_strips.py makes the strips.
TEST(Film, AFewMinutesOnTheDesk) {
  const char* dirEnv = std::getenv("IDLE_FILM_OUT");
  if (!dirEnv) GTEST_SKIP() << "set IDLE_FILM_OUT to a directory to write the frames";
  auto env = [](const char* k, double d) { const char* v = std::getenv(k); return v ? std::atof(v) : d; };
  const uint32_t fromMs = uint32_t(env("IDLE_FILM_FROM", 40) * kMinuteMs);
  const uint32_t everyMs = uint32_t(env("IDLE_FILM_EVERY", 2500));
  const uint32_t forMs = uint32_t(env("IDLE_FILM_MINUTES", 4) * kMinuteMs);
  static paint::Canvas240 cv;
  for (SpeciesId sp : {SpeciesId{2}, SpeciesId{0}, SpeciesId{5}, SpeciesId{7}, SpeciesId{3}}) {
    MemStorage store;
    NullLink link;
    FakeRtc rtc;
    uint32_t ms = 0;
    rtc.poweredMs = &ms;
    const Genome founder = starterGenomeOf(sp, 0x5EED0000u);
    DishOptions opt;
    opt.rtc = &rtc;
    opt.founder = &founder;
    opt.foundingHour = kUnaskedFoundingHour;
    Dish dish(store, 0x5EED0000u, 0x5EED0000u, opt);
    Appearance was = dish.appearance(), now = was;
    uint32_t lastTick = dish.tickCount(), tickAt = 0, nextShot = fromMs;
    int n = 0;
    for (; ms < fromMs + forMs; ms += kSampleMs) {
      // Glanced at now and then (a knock every 6 minutes), as a kid on a desk.
      BodySample s = still();
      if (ms % (6 * kMinuteMs) == 3 * kMinuteMs) s.tapCode = 1;
      dish.sample(s, ms);
      dish.tick(ms, link);
      if (dish.tickCount() != lastTick) {
        lastTick = dish.tickCount();
        was = now, now = dish.appearance(), tickAt = ms;
      }
      if (ms < nextShot) continue;
      nextShot += everyMs;
      const Appearance a = paint::between(was, now, tickAt, ms);
      paint::draw(a, paint::packFor(a.species), cv, ms);
      std::string what = "egg";
      if (const Creature* c = std::get_if<Creature>(&dish.occupant())) what = c->body().asleep ? "asleep" : doing(*c);
      char name[160];
      std::snprintf(name, sizeof name, "%s/%s_%03d_%s.ppm", dirEnv, speciesInfo(sp).name, n++, what.c_str());
      writePpm(name, cv);
    }
  }
}

// ---- what he does through the day, by how he feels, his age and his genes (#61) ----
//
// The owner, 2026-10-22: "I also want the pets to appear to be doing different
// things throughout the day. The behavior and animation should vary depending
// on how it's feeling/its state." And: "the behavior should represent it's
// genetics." docs/notes/day-activities.md.

void setPhoneTime(Dish& dish, uint32_t unix) {   // what cmd_time does
  dish.phoneTime().set(unix);
  dish.checkWall();
  dish.clock().alignToWall(unix, 0);
  dish.setZone(0);
}

// Every size and temperament gene of `g` with these bytes (-1 leaves one be).
Genome withGenes(const Genome& g, int bulk, int squash, int explore, int habituation, int dreamLen) {
  GenomeBuilder b = GenomeBuilder::from(g);
  g.forEach([&](const GeneView& v) {
    if (v.header.type == GeneKindOf<SizeGene>::value) {
      if (bulk >= 0) b.setByte(v.header.uid, 0, uint8_t(bulk));
      if (squash >= 0) b.setByte(v.header.uid, 2, uint8_t(squash));
    }
    if (v.header.type == GeneKindOf<TemperamentGene>::value) {
      if (explore >= 0) b.setByte(v.header.uid, 2, uint8_t(explore));
      if (habituation >= 0) b.setByte(v.header.uid, 3, uint8_t(habituation));
      if (dreamLen >= 0) b.setByte(v.header.uid, 5, uint8_t(dreamLen));
    }
  });
  return *b.build();
}

const char* familyName(IdleFamily f) {
  static const char* kNames[] = {"romp", "nose", "show", "muse", "night", "feel"};
  return kNames[uint8_t(f)];
}

// What he started playing, by name, by family and by part of his day; and the pace.
struct Tally {
  std::map<std::string, uint32_t> idle, family;
  std::map<std::string, std::map<std::string, uint32_t>> byPart;   // part -> idle -> starts
  uint32_t starts = 0, paceSum = 0;
  void add(const Creature& c) {
    if (!c.playing() || c.idle().ticks != 1) return;
    const IdleInfo* row = idleInfo(c.idle().now);
    if (!row) return;
    ++idle[row->name], ++family[familyName(row->family)], ++byPart[dayPartName(c.dayPart())][row->name];
    ++starts, paceSum += c.idle().pace;
  }
  double share(std::initializer_list<const char*> names) const {
    uint32_t n = 0;
    for (const char* k : names) n += idle.count(k) ? idle.at(k) : 0;
    return starts ? double(n) / starts : 0;
  }
  double familyShare(const char* f) const { return starts && family.count(f) ? double(family.at(f)) / starts : 0; }
  double pace() const { return starts ? double(paceSum) / starts : 0; }
};

double shareIn(const std::map<std::string, uint32_t>& m, std::initializer_list<const char*> names) {
  uint32_t n = 0, all = 0;
  for (const auto& kv : m) all += kv.second;
  for (const char* k : names) n += m.count(k) ? m.at(k) : 0;
  return all ? double(n) / all : 0;
}

std::string mixLine(const std::map<std::string, uint32_t>& m) {
  std::vector<std::pair<uint32_t, std::string>> v;
  uint32_t all = 0;
  for (const auto& kv : m) v.push_back({kv.second, kv.first}), all += kv.second;
  std::sort(v.rbegin(), v.rend());
  std::string out;
  char buf[64];
  for (size_t i = 0; i < v.size() && i < 8; ++i) {
    std::snprintf(buf, sizeof buf, "%s %.0f%%  ", v[i].second.c_str(), 100.0 * v[i].first / std::max(1u, all));
    out += buf;
  }
  return out;
}

// The part of his day by the wall's hour, once a phone set it.
TEST(Day, PartsByTheWallsHour) {
  Desk d(SpeciesId{2});
  d.run(15 * kMinuteMs, cradled());   // up a while: not just woken
  const std::pair<uint32_t, DayPart> hours[] = {{8, DayPart::Morning},   {12, DayPart::Midday}, {15, DayPart::Afternoon},
                                                {19, DayPart::Evening},  {7, DayPart::Morning}};
  for (const auto& [h, part] : hours) {
    setPhoneTime(*d.dish, kStartUnix - kStartHour * 3600u + h * 3600u);
    d.run(2000, cradled());
    EXPECT_EQ(dayPartName(d.c().dayPart()), std::string(dayPartName(part))) << h << ":00";
  }
  setPhoneTime(*d.dish, kStartUnix - kStartHour * 3600u + 23 * 3600u);   // the wall's night is dark: his night
  d.run(2000, cradled());
  EXPECT_EQ(d.c().dayPart(), DayPart::Night);
}

// With no clock, his own day: groggy just up, then morning, midday,
// afternoon; the dark is his night, and a night's sleep starts his day again.
TEST(Day, NoClockHisOwnDayFromWhenHeWoke) {
  Desk d(SpeciesId{5});
  EXPECT_EQ(d.c().dayPart(), DayPart::Waking) << "just hatched";
  auto handled = [&](uint32_t minutes) {   // a look every ten minutes, so it is never dim
    for (uint32_t m = 0; m < minutes; m += 10) d.run(10 * kMinuteMs - 2000), d.run(2000, cradled());
  };
  handled(20);
  EXPECT_EQ(d.c().dayPart(), DayPart::Morning);
  handled(120);
  EXPECT_EQ(d.c().dayPart(), DayPart::Midday);
  handled(180);
  EXPECT_EQ(d.c().dayPart(), DayPart::Afternoon);
  d.run(4 * 60 * kMinuteMs, faceDown());   // tucked in face down for the night
  EXPECT_EQ(d.c().dayPart(), DayPart::Night);
  EXPECT_TRUE(d.c().body().asleep);
  d.run(3000, cradled());                   // picked up in the morning
  EXPECT_FALSE(d.c().body().asleep);
  EXPECT_EQ(d.c().dayPart(), DayPart::Waking);
  handled(20);
  EXPECT_EQ(d.c().dayPart(), DayPart::Morning) << "a new day";
}

// The owner's ask: a day on the desk, with a phone's clock, a kid looking
// now and then. What he does leans with the part of the day: washing,
// stretching, exercises and a song in the morning, the sun in the middle of
// the day, dreams in the afternoon, fireflies and yawns in the evening.
// DAY_MIX=1 prints each part's mix.
TEST(Day, WhatHeDoesChangesThroughTheDay) {
  std::map<std::string, std::map<std::string, uint32_t>> all;
  for (SpeciesId sp : {SpeciesId{2}, SpeciesId{0}, SpeciesId{5}, SpeciesId{4}}) {
    MemStorage store;
    NullLink link;
    FakeRtc rtc;
    uint32_t ms = 0;
    rtc.poweredMs = &ms;
    const Genome founder = starterGenomeOf(sp, 0x5EED0000u);
    DishOptions opt;
    opt.rtc = &rtc;
    opt.founder = &founder;
    Dish dish(store, 0x5EED0000u, 0x5EED0000u, opt);
    setPhoneTime(dish, kStartUnix);
    Kid kid(0x5EED0000u ^ sp.v, Night::Nightstand);
    Appearance a = dish.appearance();
    Tally t;
    uint32_t last = 0;
    for (; ms < 15 * kHourMs; ms += kSampleMs) {   // 07:00 to 22:00
      dish.sample(kid.next(ms, dish, a), ms);
      dish.tick(ms, link);
      if (dish.tickCount() == last) continue;
      last = dish.tickCount();
      a = dish.appearance();
      if (const Creature* c = std::get_if<Creature>(&dish.occupant())) t.add(*c);
    }
    for (const auto& [part, m] : t.byPart)
      for (const auto& [name, n] : m) all[part][name] += n;
  }
  if (std::getenv("DAY_MIX"))
    for (const char* part : {"waking", "morning", "midday", "afternoon", "evening", "night"})
      std::printf("%-9s %s\n", part, mixLine(all[part]).c_str());
  const auto mornings = {"wash", "stretch", "exercise", "sing", "tidy", "yawn"};
  EXPECT_GT(shareIn(all["morning"], mornings), 1.8 * shareIn(all["afternoon"], mornings)) << mixLine(all["morning"]);
  EXPECT_GT(shareIn(all["midday"], {"sunbathe"}), 3 * shareIn(all["evening"], {"sunbathe"})) << mixLine(all["midday"]);
  EXPECT_GT(shareIn(all["evening"], {"fireflies"}), 0.04) << mixLine(all["evening"]);
  EXPECT_EQ(shareIn(all["morning"], {"fireflies"}), 0) << mixLine(all["morning"]);
  EXPECT_GT(shareIn(all["afternoon"], {"daydream"}), shareIn(all["morning"], {"daydream"})) << mixLine(all["afternoon"]);
  EXPECT_GT(shareIn(all["evening"], {"yawn"}), shareIn(all["midday"], {"yawn"})) << mixLine(all["evening"]);
  // Still a wide variety in every part he is up for.
  for (const char* part : {"morning", "midday", "afternoon", "evening"}) EXPECT_GE(all[part].size(), 12u) << part;
}

// How he feels shows: hungry with nothing to eat he rubs his tummy or begs
// with his bowl; sleepy and up he nods off and yawns; hurt he nurses the sore
// spot; poisoned he is queasy. Content, none of them.
TEST(Feel, HowHeFeelsShows) {
  struct Case { const char* what; ChemId chem; Fx level; std::initializer_list<const char*> shows; };
  const Case cases[] = {
      {"hungry", driveChem(drive::hunger), Fx::ratio(68, 100), {"tummy", "beg"}},
      {"sleepy", driveChem(drive::sleepiness), Fx::ratio(44, 100), {"nod_off", "yawn"}},
      {"hurt", driveChem(drive::pain), Fx::ratio(5, 10), {"nurse"}},
      {"poisoned", chem::toxin, Fx::ratio(3, 10), {"queasy"}},
  };
  const auto feelings = {"tummy", "beg", "nod_off", "nurse", "queasy"};
  for (SpeciesId sp : {SpeciesId{2}, SpeciesId{0}, SpeciesId{7}}) {
    for (const Case& k : cases) {
      Desk d(sp);
      Tally t;
      for (int i = 0; i < 10 * 60 * 10; ++i) {   // ten minutes, the feeling held
        if (i % 10 == 0) d.c().inject(k.chem, k.level);
        if (k.chem == chem::toxin && i % 10 == 0) d.c().inject(chem::injury, Fx::zero());   // queasy, not dying of it
        d.run(kTickMs, i % 3000 == 0 ? cradled() : still());
        t.add(d.c());
      }
      EXPECT_GT(t.share(k.shows), 0.2) << speciesInfo(sp).name << " " << k.what << ": " << mixLine(t.idle);
    }
    Desk d(sp);   // content, handled now and then
    Tally t;
    for (int i = 0; i < 20 * 60 * 10; ++i) {
      if (i % 600 == 0)
        for (const DriveInfo& row : DRIVES) d.c().inject(driveChem(row.id), Fx::ratio(1, 10));
      d.run(kTickMs, i % 3000 == 0 ? cradled() : still());
      t.add(d.c());
    }
    EXPECT_GT(t.starts, 10u);
    EXPECT_EQ(t.share(feelings), 0) << speciesInfo(sp).name << " content: " << mixLine(t.idle);
    EXPECT_GT(t.share({"beam"}), 0) << speciesInfo(sp).name << " content: " << mixLine(t.idle);
  }
}

// The chooser, many times over: a baby romps and tumbles, an elder muses and
// remembers; a pet's genes move his mix the way their names say.
std::map<std::string, uint32_t> chooserMix(const IdleMood& m, int n = 4000) {
  Rng rng = Rng::seeded(0xC0FFEEull);
  std::map<std::string, uint32_t> out;
  for (int i = 0; i < n; ++i) {
    IdleState s;   // nothing cooling down
    const IdleId id = pickIdle(s, m, rng);
    if (const IdleInfo* row = idleInfo(id)) ++out[familyName(row->family)], ++out[row->name];
  }
  return out;
}
IdleMood calmMood(SpeciesId sp) {
  IdleMood m;
  m.species = sp, m.taste = 0x1234u, m.age = 100000;
  m.boredom = m.loneliness = m.needTouch = m.sleepiness = Fx::ratio(2, 10);
  m.explore = Fx::ratio(66, 100), m.habituation = Fx::ratio(43, 100), m.content = Fx::ratio(7, 10);
  return m;
}
double frac(const std::map<std::string, uint32_t>& m, const char* k, int n = 4000) {
  return m.count(k) ? double(m.at(k)) / n : 0;
}

TEST(Age, ABabyRompsAnElderMuses) {
  for (SpeciesId sp : {SpeciesId{2}, SpeciesId{0}, SpeciesId{5}}) {
    IdleMood baby = calmMood(sp), elder = calmMood(sp);
    baby.stage = Stage::Baby, elder.stage = Stage::Elder;
    const auto b = chooserMix(baby), e = chooserMix(elder);
    EXPECT_GT(frac(b, "romp"), 1.6 * frac(e, "romp")) << speciesInfo(sp).name;
    EXPECT_GT(frac(e, "muse"), 1.4 * frac(b, "muse")) << speciesInfo(sp).name;
    EXPECT_GT(frac(b, "tumble"), 0);
    EXPECT_EQ(frac(e, "tumble"), 0);
    EXPECT_GT(frac(e, "reminisce"), 0);
    EXPECT_EQ(frac(b, "reminisce"), 0);
  }
  // And plays it at his age's pace: brisk young, slow old.
  IdleMood m = calmMood(SpeciesId{2});
  m.stage = Stage::Baby;
  const uint8_t baby = idlePace(m);
  m.stage = Stage::Adult;
  const uint8_t adult = idlePace(m);
  m.stage = Stage::Elder;
  const uint8_t elder = idlePace(m);
  EXPECT_GT(baby, adult);
  EXPECT_GT(adult, elder);
}

// The pace shows how he feels: sleepy, hurt, sick or hungry he plays slower.
TEST(Feel, HowHeFeelsSetsThePace) {
  const IdleMood fine = calmMood(SpeciesId{2});
  const uint8_t pace = idlePace(fine);
  for (int k = 0; k < 4; ++k) {
    IdleMood m = fine;
    if (k == 0) m.sleepiness = Fx::ratio(6, 10);
    if (k == 1) m.pain = Fx::ratio(6, 10);
    if (k == 2) m.toxin = Fx::ratio(5, 10);
    if (k == 3) m.hunger = Fx::ratio(7, 10);
    EXPECT_LT(idlePace(m), pace) << k;
  }
  IdleMood happy = fine;
  happy.content = Fx::one();
  EXPECT_GT(idlePace(happy), idlePace(fine));
}

// The genes, through the chooser: a small one romps more and a big one muses
// more (size), a curious one noses about more (explore), a dreamer muses more
// (dreamLen), one quick to tire of a thing changes family more (habituation).
TEST(Genes, EachMovesTheMixTheWayItsNameSays) {
  const IdleMood base = calmMood(SpeciesId{3});
  IdleMood small = base, big = base;
  small.bulk = 40, big.bulk = 220;
  EXPECT_GT(frac(chooserMix(small), "romp"), 1.2 * frac(chooserMix(big), "romp"));
  EXPECT_GT(frac(chooserMix(big), "muse"), 1.2 * frac(chooserMix(small), "muse"));
  EXPECT_GT(idlePace(small), idlePace(big) + 15);
  IdleMood curious = base, incurious = base;
  curious.explore = Fx::ratio(95, 100), incurious.explore = Fx::ratio(5, 100);
  EXPECT_GT(frac(chooserMix(curious), "nose"), 1.2 * frac(chooserMix(incurious), "nose"));
  IdleMood dreamer = base, brisk = base;
  dreamer.dreamLenTicks = 150, brisk.dreamLenTicks = 0;
  EXPECT_GT(frac(chooserMix(dreamer), "muse"), 1.3 * frac(chooserMix(brisk), "muse"));
  // Habituation: just played a romp, how much of the next is a romp again.
  auto again = [&](Fx hab) {
    IdleMood m = base;
    m.habituation = hab;
    Rng rng = Rng::seeded(7);
    int romps = 0;
    for (int i = 0; i < 4000; ++i) {
      IdleState s;
      s.last = idles::dance;
      const IdleInfo* row = idleInfo(pickIdle(s, m, rng));
      romps += row && row->family == IdleFamily::Romp;
    }
    return romps;
  };
  EXPECT_GT(again(Fx::ratio(5, 100)), 2 * again(Fx::ratio(8, 10)));
  // What his genes make him (traits.h, #60): one doubling of a gene's rate.
  IdleMood playful = base, affectionate = base, sleepy = base, owl = base, plain = base;
  playful.traits.playfulness = 100, affectionate.traits.affection = 100, sleepy.traits.sleepiness = 100;
  owl.traits.nightOwl = 100;
  owl.part = plain.part = DayPart::Evening;
  EXPECT_GT(frac(chooserMix(playful), "romp"), 1.2 * frac(chooserMix(base), "romp"));
  EXPECT_GT(frac(chooserMix(affectionate), "show"), 1.2 * frac(chooserMix(base), "show"));
  EXPECT_GT(frac(chooserMix(sleepy), "muse"), 1.2 * frac(chooserMix(base), "muse"));
  EXPECT_GT(frac(chooserMix(owl), "fireflies"), 1.2 * frac(chooserMix(plain), "fireflies"));
  EXPECT_GT(idleLiveliness(playful), idleLiveliness(base));
  EXPECT_GT(idlePace(playful), idlePace(sleepy));
}

// The owner's ask, whole: two pets of one kind, the same seed, the same desk
// and kid; only their size and temperament genes differ. What they do and how
// briskly they do it differ the way the genes say.
TEST(Genes, TwoPetsOfOneKindDifferByTheirGenes) {
  const Genome starter = starterGenomeOf(SpeciesId{3}, 0x5EED0000u);
  const Genome sprite = withGenes(starter, 40, 220, 240, 200, 1);   // small, squashy, curious, restless, short dreams
  const Genome sage = withGenes(starter, 220, 40, 20, 40, 15);      // big, firm, incurious, settled, long dreams
  auto live4h = [](const Genome& g) {
    Desk d(SpeciesId{3}, kUnaskedFoundingHour, 0x5EED0000u, &g);
    Tally t;
    for (int i = 0; i < 4 * 3600 * 10; ++i) {
      d.run(kTickMs, i % (10 * 600) < 30 ? cradled() : still());
      t.add(d.c());
    }
    return t;
  };
  const Tally a = live4h(sprite), b = live4h(sage);
  const double aActive = a.familyShare("romp") + a.familyShare("nose"), bActive = b.familyShare("romp") + b.familyShare("nose");
  EXPECT_GT(aActive, bActive + 0.1) << "sprite " << mixLine(a.family) << "\nsage " << mixLine(b.family);
  EXPECT_GT(b.familyShare("muse"), a.familyShare("muse") + 0.1) << "sprite " << mixLine(a.family) << "\nsage " << mixLine(b.family);
  EXPECT_GT(a.pace(), b.pace() + 20);
  if (std::getenv("DAY_MIX")) {
    std::printf("sprite pace %.0f: %s\n  %s\n", a.pace(), mixLine(a.family).c_str(), mixLine(a.idle).c_str());
    std::printf("sage   pace %.0f: %s\n  %s\n", b.pace(), mixLine(b.family).c_str(), mixLine(b.idle).c_str());
  }
}

// And in his motion: a squashy gene squashes him more in an act, a big one
// breathes slower, a brisk pace bounces more.
TEST(Genes, HisBodysGenesShowInHisMotion) {
  Desk d(SpeciesId{2});
  Appearance a = d.dish->appearance();
  a.lively = true, a.reflexActive = false, a.asleep = false, a.pose = pose::idle;
  a.idle = idles::exercise, a.idleLength = 80;
  auto deepest = [&](uint8_t squash) {
    Appearance x = a;
    x.squash = squash;
    int most = 0;
    for (uint16_t t = 10; t < 70; ++t) x.idleTicks = t, most = std::max(most, std::abs(paint::livelyOf(x, 20).tall));
    return most;
  };
  EXPECT_GT(deepest(230), deepest(30) * 3 / 2);
  auto breaths = [&](uint8_t bulk) {   // breaths in a minute
    Appearance x = a;
    x.idle = idles::none, x.bulk = bulk;
    int n = 0, was = 0;
    for (uint32_t ms = 0; ms < 60000; ms += 20) {
      x.liveMs = ms;
      const int b = paint::livelyOf(x, 20).breathQ4;
      if (b > 20 && was <= 20) ++n;
      was = b;
    }
    return n;
  };
  EXPECT_GT(breaths(30), breaths(230) + 3);
}

// The day's acts filmed (#61): each of them, at a few points through it, on a
// puffkin and a grungo, as the screen draws them. DAY_ACTS_OUT=<dir>, then
// python3 scripts/test/idle_strips.py <dir> <out.png> --cols 6.
TEST(Film, TheDaysActs) {
  const char* dirEnv = std::getenv("DAY_ACTS_OUT");
  if (!dirEnv) GTEST_SKIP() << "set DAY_ACTS_OUT to a directory to write the frames";
  static paint::Canvas240 cv;
  for (SpeciesId sp : {SpeciesId{2}, SpeciesId{0}}) {
    Desk d(sp);
    int n = 0;
    for (uint8_t id = idles::wash.v; id < kIdleCount; ++id) {
      for (int k = 0; k < 6; ++k) {
        Appearance a = d.dish->appearance();
        a.lively = true, a.reflexActive = false, a.asleep = false, a.pose = pose::idle;
        a.idle = IdleId{id};
        a.idleLength = uint16_t(IDLES[id].seconds * 10);
        a.idleTicks = uint16_t(a.idleLength * (2 * k + 1) / 12);
        a.liveMs = 10000 + a.idleTicks * kTickMs;
        a.expression = idleFace(a.idle, a.idleTicks, a.idleLength), a.intensity = Fx::one();
        paint::draw(a, paint::packFor(a.species), cv, a.liveMs);
        char name[200];
        std::snprintf(name, sizeof name, "%s/%s_%03d_%s-%d.ppm", dirEnv, speciesInfo(sp).name, n++, IDLES[id].name, k);
        writePpm(name, cv);
      }
    }
  }
}

// What each act costs to draw, on this host, against him standing about and
// sleep-idle's acts: DAY_ACTS_COST=1. A ratio, not the badge's milliseconds.
TEST(Film, TheDaysActsCost) {
  if (!std::getenv("DAY_ACTS_COST")) GTEST_SKIP() << "DAY_ACTS_COST=1 times them";
  static paint::Canvas240 cv;
  Desk d(SpeciesId{2});
  auto cost = [&](IdleId id) {
    Appearance a = d.dish->appearance();
    a.lively = true, a.reflexActive = false, a.asleep = false, a.pose = pose::idle, a.idle = id;
    a.idleLength = id == idles::none ? 0 : uint16_t(IDLES[id.v].seconds * 10);
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 300; ++k) {
      a.idleTicks = uint16_t(a.idleLength ? k % a.idleLength : 0), a.liveMs = 10000 + k * 20;
      paint::draw(a, paint::packFor(a.species), cv, a.liveMs);
    }
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 300;
  };
  const double plain = cost(idles::none);
  std::printf("standing about: %.0f us a frame\n", plain);
  for (uint8_t id = 0; id < kIdleCount; ++id) std::printf("%-12s %.2fx\n", IDLES[id].name, cost(IdleId{id}) / plain);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
