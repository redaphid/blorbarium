// Food balance (#57). The owner, playing with a badge: "I keep running out of
// food and I don't know how to get more but it keeps complaining about being
// starving." A fresh pet of each species is lived for days through the Dish at
// 50 Hz, as main.cpp drives it, with a kid who feeds him whenever he asks: in
// the day the kid glances every few minutes, gives him a cuddle, and presses
// for a pellet whenever he is hungry (the blep, the feed hint, the hungry
// lines). At night nobody touches him. What is counted: the times he asked
// and the pantry was empty, how long he was starving, the pellets.
//
// FOOD_SWEEP=1 prints the table for every species and FOOD_SEEDS seeds
// (FOOD_DAYS days, 3 by default); the tests below are the floors.
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <variant>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/food.h"
#include "blorb/founding.h"
#include "blorb/genes.h"
#include <optional>
#include "blorb/registry.h"
#include "blorb/species.h"
#include "links.h"
#include "mem_storage.h"
#include "time_sources.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint32_t kMinuteMs = 60000;
constexpr uint32_t kHourMs = 60 * kMinuteMs;
constexpr Fx kAsks = Fx::ratio(1, 2);       // hungry: the blep, the feed hint, the hungry lines
constexpr Fx kStarvingAt = Fx::ratio(8, 10);

double pm(Fx v) { return double(v.raw) / double(Fx::kOne); }

// Attentive: one pellet at a time, when he asks and none is down. Eager: a
// press every time he asks, pellet down or not (a real kid with a pet that
// keeps complaining). Playful: Eager, and a knock knock for a pellet at half
// of the other glances too, because it is fun. Gone: Attentive for the first
// twelve hours, then nobody, ever (neglect).
enum class Kid : uint8_t { Attentive, Eager, Playful, Gone };
const char* kidName(Kid k) {
  return k == Kid::Attentive ? "attentive" : k == Kid::Eager ? "eager" : k == Kid::Playful ? "playful" : "gone";
}

struct Life {
  std::string species;
  uint32_t seed = 0;
  Kid kid = Kid::Attentive;
  double days = 0;
  uint32_t asks = 0, dryAsks = 0, presses = 0, dryPresses = 0, eaten = 0, rotten = 0, found = 0;
  double firstDryH = -1, starvingMin = 0, hungryAwakeMin = 0, hungerMean = 0, hungerMax = 0, injuryMax = 0;
  double neglectStarvingH = -1, diedH = -1;   // hours after the last touch
  bool died = false;
  std::string cause;
};

Life live(SpeciesId species, uint32_t seed, Kid kid, double days) {
  Life r;
  r.species = speciesInfo(species).name;
  r.seed = seed;
  r.kid = kid;
  r.days = days;
  MemStorage store;
  NullLink link;
  const Genome founder = starterGenomeOf(species, seed);
  DishOptions opt;
  opt.founder = &founder;
  opt.foundingHour = kUnaskedFoundingHour;
  Dish dish(store, seed, seed, opt);
  Rng rng = Rng::seeded(seed ^ 0xF00DF00Dull);

  enum class G : uint8_t { Rest, Press, Hold };
  G g = G::Rest;
  uint32_t until = 0;
  double sum = 0;
  uint32_t samples = 0, lastFed = 0, lastTick = 0;
  const uint32_t endMs = uint32_t(days * 24 * kHourMs);
  const uint32_t goneAt = kid == Kid::Gone ? 12 * kHourMs : UINT32_MAX;
  uint32_t lastTouch = 0;
  for (uint32_t ms = 0; ms < endMs; ms += kSampleMs) {
    const uint32_t dayMs = ms % (24 * kHourMs);
    const bool around = dayMs < 12 * kHourMs && ms < goneAt;   // 09:00 to 21:00 on his clock
    if (ms >= until) {
      if (g != G::Rest || !around) {
        g = G::Rest;
        until = ms + (around ? (2 + rng.below(5)) * kMinuteMs : kMinuteMs);
      } else if (const Creature* c = std::get_if<Creature>(&dish.occupant())) {
        const Fx hunger = c->chemistry().drive(drive::hunger);
        const bool asks = !c->body().asleep && hunger >= kAsks;
        const bool down = dish.habitat().nearestFood(c->phenotype().habitat, c->body().at, dish.tickCount(), false)
                              .has_value();
        lastTouch = ms;
        if (asks) {
          ++r.asks;
          if (dish.habitat().pantry == 0 && !down) {
            ++r.dryAsks;
            if (r.firstDryH < 0) r.firstDryH = double(ms) / kHourMs;
          }
        }
        const bool press = asks ? kid == Kid::Eager || kid == Kid::Playful || !down
                                : kid == Kid::Playful && rng.below(2) == 0;
        if (press) {
          g = G::Press, until = ms + 200;
          ++r.presses;
          r.dryPresses += dish.habitat().pantry == 0;
        } else {
          g = G::Hold, until = ms + 5000;   // a cuddle, so he is looked at
        }
      } else {
        g = G::Hold, until = ms + 5000;   // the egg is held
      }
    }
    const BodySample s = g == G::Press ? pressed() : g == G::Hold ? cradled() : still();
    dish.sample(s, ms);
    dish.tick(ms, link);
    if (dish.tickCount() == lastTick) continue;
    lastTick = dish.tickCount();
    if (const Clutch* k = std::get_if<Clutch>(&dish.occupant())) {
      if (!r.died) {
        r.died = true;
        const char* causes[] = {"old age", "starved", "injured", "poisoned", "unknown"};
        r.cause = causes[int(k->cause)];
        r.diedH = double(ms - lastTouch) / kHourMs;
      }
      break;
    }
    const Creature* c = std::get_if<Creature>(&dish.occupant());
    if (!c) continue;
    const double hunger = pm(c->chemistry().drive(drive::hunger));
    sum += hunger;
    ++samples;
    r.hungerMax = std::max(r.hungerMax, hunger);
    r.injuryMax = std::max(r.injuryMax, pm(c->chemistry().chem[chem::injury.v]));
    if (hunger >= pm(kStarvingAt)) {
      r.starvingMin += 1.0 / (60 * 10);
      if (ms >= goneAt && r.neglectStarvingH < 0) r.neglectStarvingH = double(ms - lastTouch) / kHourMs;
    }
    if (hunger >= pm(kAsks) && !c->body().asleep) r.hungryAwakeMin += 1.0 / (60 * 10);
    if (std::getenv("FOOD_TRACE") && lastTick % (10 * kTicksPerMinute) == 0)
      std::printf("  %6.2fh hunger=%.2f energy=%.2f food=%.2f %s %-12s pantry=%u down=%d eaten=%u\n", double(ms) / kHourMs,
                  hunger, pm(c->chemistry().chem[chem::energy.v]), pm(c->chemistry().chem[chem::food.v]),
                  c->body().asleep ? "ASLEEP" : "awake ", c->doing(), unsigned(dish.habitat().pantry),
                  int(dish.habitat().nearestPellet(DishPos{}).has_value()), r.eaten);
    if (c->stats().fed != lastFed) {
      lastFed = c->stats().fed;
      ++r.eaten;
      r.rotten += c->body().mouth == Mouthful::RottenPellet;
    }
    // A pellet that landed this tick: a press's, or a snack he dug up (the screen says which).
    for (const Pellet& p : dish.habitat().pellets)
      if (p.present && p.droppedTick + 1 == lastTick && dish.appearance().feed.foundMs < 200) {
        ++r.found;
        break;
      }
  }
  if (samples) r.hungerMean = sum / samples;
  return r;
}

std::string row(const Life& r) {
  char b[400];
  std::snprintf(b, sizeof b,
                "%-9s %10u %-9s days=%.1f asks=%4u dry=%4u firstDry=%6.1fh presses=%4u/%4u eaten=%4u rotten=%3u "
                "found=%3u starving=%7.1fm hungryAwake=%7.1fm hunger=%.2f/%.2f inj=%.2f neglectStarve=%5.1fh died=%5.1fh %s",
                r.species.c_str(), unsigned(r.seed), kidName(r.kid), r.days, r.asks, r.dryAsks, r.firstDryH,
                r.dryPresses, r.presses, r.eaten, r.rotten, r.found, r.starvingMin, r.hungryAwakeMin, r.hungerMean, r.hungerMax,
                r.injuryMax, r.neglectStarvingH, r.diedH, r.died ? ("DIED " + r.cause).c_str() : "");
  return b;
}

double envDays(double fallback) {
  const char* d = std::getenv("FOOD_DAYS");
  return d ? std::atof(d) : fallback;
}

}  // namespace

TEST(Sweep, EverySpeciesAndKid) {
  if (!std::getenv("FOOD_SWEEP")) GTEST_SKIP() << "FOOD_SWEEP=1 runs it";
  const char* n = std::getenv("FOOD_SEEDS");
  const uint32_t seeds = n ? uint32_t(std::atoi(n)) : 2;
  const double days = envDays(3);
  for (size_t sp = 0; sp < kSpeciesCount; ++sp)
    for (uint32_t s = 0; s < seeds; ++s)
      for (Kid k : {Kid::Attentive, Kid::Eager, Kid::Playful, Kid::Gone}) {
        const Life r = live(SPECIES[sp].id, 0x5EED0000u + s * 7919u, k, k == Kid::Gone ? 4 : days);
        std::printf("%s\n", row(r).c_str());
        std::fflush(stdout);
      }
}

// ---- the balance ------------------------------------------------------------------

// The owner's complaint, as a floor: a kid who presses every time he asks,
// pellet down or not (more than one who feeds only when none is down),
// never finds the pantry empty when he asks, over two days, for a day
// species, the hungriest (mudge, appetite 1.3) and a night species.
// (FOOD_SWEEP=1 runs every species, both kids and more days.)
TEST(Balance, AKidWhoFeedsWhenAskedNeverRunsDry) {
  for (SpeciesId sp : {SpeciesId{1}, SpeciesId{5}, SpeciesId{7}})
    for (Kid k : {Kid::Eager}) {
      const Life r = live(sp, 0x5EED0000u, k, 2);
      const std::string at = row(r);
      EXPECT_FALSE(r.died) << at;
      EXPECT_GE(r.asks, 2u) << "he asked, day after day: " << at;
      EXPECT_EQ(r.dryAsks, 0u) << "never asked with nothing to give: " << at;
      EXPECT_EQ(r.dryPresses, 0u) << "never pressed into an empty pantry: " << at;
      EXPECT_GE(r.eaten, r.asks / 2) << at;
    }
}

// A kid who also knocks for pellets for the fun of it, half his glances,
// hardly ever finds it empty, and never when he asks: the refill is minutes away.
TEST(Balance, AKidWhoFeedsForFunAlmostNeverRunsDry) {
  const Life r = live(SpeciesId{5}, 0x5EED0000u, Kid::Playful, 2);
  const std::string at = row(r);
  EXPECT_FALSE(r.died) << at;
  EXPECT_LE(r.dryPresses * 50, r.presses) << "under 2 percent of presses find it empty: " << at;
  EXPECT_EQ(r.dryAsks, 0u) << at;
}

// Starvation is only neglect's: fed for twelve hours and then never touched,
// he is not starving for hours after the last hand, and starves in the end.
TEST(Balance, NeglectStillStarvesHimButOnlyAfterHours) {
  const Life r = live(SpeciesId{1}, 0x5EED0000u, Kid::Gone, 4);
  const std::string at = row(r);
  EXPECT_GE(r.neglectStarvingH, 1.0) << "hours untouched before he starves: " << at;
  ASSERT_TRUE(r.died) << "neglect still kills: " << at;
  EXPECT_EQ(r.cause, "starved") << at;
  EXPECT_GE(r.diedH, 24.0) << "and it takes more than a day: " << at;
}

// ---- foraging ---------------------------------------------------------------------

namespace {
struct Pen {
  Habitat h;
  HabitatRules rules{8, 8 * kTicksPerMinute, 90 * kTicksPerMinute, Fx::ratio(3, 10)};
  Body body;
  IdleState idle;
  SenseOut out;
  Rng rng;
  explicit Pen(uint64_t seed) : rng(Rng::seeded(seed)) { h.pantry = 3; }
  // One whole act of `id`, as the creature plays it.
  void play(IdleId id, uint32_t tick) {
    idle.now = id, idle.ticks = 0, idle.length = 60;
    ActionCtx c{h, rules, Fx::ratio(1, 2), Fx::ratio(1, 2), tick, rng, out};
    c.idle = &idle;
    for (int i = 0; i < 100 && behave::Play{}.step(body, c) == Status::Running; ++i) c.tick = ++tick;
  }
  int pellets() const {
    int n = 0;
    for (const Pellet& p : h.pellets) n += p.present;
    return n;
  }
};
}  // namespace

// A dig that ends inside its forage window's slot turns up a snack, one that
// ends outside it nothing, and a sniff or a dance never; the snack lands just
// ahead of him, fresh, in bite range, and the pantry gives nothing for it.
TEST(Foraging, ADigInItsWindowsSlotTurnsUpASnackAtHisFeet) {
  for (uint32_t w = 0; w < 50; ++w) {
    const uint32_t slot = w * food::kForageWindowTicks + food::forageSlotOf(w);
    struct Case { uint32_t endsAt; bool finds; };
    const Case cases[] = {{slot + 10, true}, {slot + food::kForageSlotTicks - 1, true},
                          {slot - 1, false}, {slot + food::kForageSlotTicks, false}};
    for (const Case& k : cases) {
      if (k.endsAt < 59) continue;
      Pen dig(w + 1), sniff(w + 1), toy(w + 1);
      dig.play(idles::dig, k.endsAt - 59);
      sniff.play(idles::sniff, k.endsAt - 59);
      toy.play(idles::dance, k.endsAt - 59);
      EXPECT_EQ(dig.pellets(), k.finds ? 1 : 0) << "window " << w << " ends at " << k.endsAt;
      EXPECT_EQ(sniff.pellets() + toy.pellets(), 0) << "only a dig";
      EXPECT_EQ(dig.h.pantry, 3) << "a snack he found is not the pantry's";
      for (const Pellet& p : dig.h.pellets) {
        if (!p.present) continue;
        EXPECT_EQ(p.droppedTick, k.endsAt) << "fresh: it landed as the dig ended";
        const Fx dx = p.at.x - dig.body.at.x, dy = p.at.y - dig.body.at.y;
        EXPECT_LE(dx * dx + dy * dy, Fx::ratio(12, 100) * Fx::ratio(12, 100)) << "in bite range";
      }
    }
  }
  // A slot is shorter than a dig's cooldown, so a window gives at most one.
  EXPECT_LT(food::kForageSlotTicks, uint32_t(IDLES[idles::dig.v].cooldownS) * 1000 / kTickMs);
}

// Over days of ordinary play he forages a few snacks of his own: a treat,
// never his keep, so he still asks his kid.
TEST(Foraging, InOrdinaryPlayHeFindsAFewSnacks) {
  const Life r = live(SpeciesId{1}, 0x5EED0000u, Kid::Attentive, 2);
  EXPECT_GE(r.found, 1u) << row(r);
  EXPECT_LE(r.found, 2u * 24 / 3) << "one a forage window at most: " << row(r);
  EXPECT_GE(r.asks, 2u) << row(r);
}

// ---- coming home ------------------------------------------------------------------

namespace {
GeneUid metabolismOf(const Genome& g) {
  GeneUid uid{};
  g.forEach([&](const GeneView& v) {
    if (v.header.type == GeneKindOf<MetabolismGene>::value) uid = v.header.uid;
  });
  return uid;
}

// A hatched mossby, the pantry emptied, its refill this slow (minutes).
struct Home {
  MemStorage store;
  NullLink link;
  Genome founder;
  std::optional<Dish> dish;
  uint32_t ms = 0;
  explicit Home(uint8_t refillMinutes) : founder(starterGenomeOf(SpeciesId{1}, 7)) {
    boot();
    runFor(*dish, link, ms, 20 * kMinuteMs);   // hatched
    Creature& c = std::get<Creature>(dish->occupant());
    EXPECT_TRUE(dish->editGene(metabolismOf(c.genome()), 1, refillMinutes));
    dish->habitat().pantry = 0;
    dish->habitat().pantryTick = dish->tickCount();
  }
  void boot() {
    DishOptions o;
    o.founder = &founder;
    dish.reset();
    dish.emplace(store, 7, 7, o);
  }
  void touch() {
    for (int i = 0; i < 100; ++i, ms += kSampleMs) dish->sample(cradled(), ms), dish->tick(ms, link);
  }
};
}  // namespace

// Untouched for hours, the first hand tops the pantry up (here the refill is
// slowed to hours, so the pantry is still empty when he comes back).
TEST(ComingHome, TheFirstHandAfterHoursAwayTopsThePantryUp) {
  Home home(250);   // one back every 250 x 40 s: 2 h 47 min
  runFor(*home.dish, home.link, home.ms, 135 * kMinuteMs);
  ASSERT_EQ(home.dish->habitat().pantry, 0) << "nothing came back on its own yet";
  home.touch();
  EXPECT_EQ(home.dish->habitat().pantry, food::kHomecoming);
  EXPECT_EQ(home.dish->appearance().feed.topped, food::kHomecoming) << "the slots it filled pop in";
  // Touched again soon after: nothing more.
  runFor(*home.dish, home.link, home.ms, 10 * kMinuteMs);
  home.touch();
  EXPECT_EQ(home.dish->habitat().pantry, food::kHomecoming) << "only after a long time away";
}

// A hand on him an hour after the last is no homecoming.
TEST(ComingHome, AnHourAwayIsNotLongEnough) {
  Home home(250);
  home.touch();
  const uint8_t before = home.dish->habitat().pantry;
  runFor(*home.dish, home.link, home.ms, 60 * kMinuteMs);
  home.touch();
  EXPECT_EQ(home.dish->habitat().pantry, before);
}

// The badge was off (no battery: a kid unplugged it): the first hand after the
// board comes back up is coming home too.
TEST(ComingHome, AfterTheBoardWasOffTheFirstHandTopsUp) {
  Home home(250);
  home.touch();
  ASSERT_TRUE(home.dish->flush());
  const uint8_t before = home.dish->habitat().pantry;
  home.boot();
  ASSERT_EQ(home.dish->boot(), Boot::Resumed);
  home.ms = 0;
  home.touch();
  EXPECT_EQ(home.dish->habitat().pantry, std::min<int>(before + food::kHomecoming, 8));
}

// ---- the countdown ----------------------------------------------------------------

// Hungry with nothing left: the rim says when the next one comes, counting
// down in seconds, and stops saying so once it is back.
TEST(Countdown, AnEmptyPantryCountsDownToTheNextPellet) {
  Home home(8);   // the starter's 8: 5 min 20 s
  home.touch();   // he is up
  std::get<Creature>(home.dish->occupant()).inject(driveChem(drive::hunger), Fx::ratio(7, 10));
  home.dish->habitat().pantry = 0;
  home.dish->habitat().pantryTick = home.dish->tickCount();
  runFor(*home.dish, home.link, home.ms, 1000);
  FeedView f = home.dish->appearance().feed;
  ASSERT_TRUE(f.waiting) << "hungry, awake, nothing left";
  EXPECT_GE(f.refillSecs, 8 * 40 - 2);
  EXPECT_LE(f.refillSecs, 8 * 40);
  const uint16_t first = f.refillSecs;
  runFor(*home.dish, home.link, home.ms, 60 * 1000);
  f = home.dish->appearance().feed;
  EXPECT_NEAR(int(f.refillSecs), int(first) - 60, 1) << "a minute later, a minute less";
  EXPECT_GT(f.refill, Fx::zero()) << "and the slot's pie has filled a little";
  runFor(*home.dish, home.link, home.ms, 5 * 60 * 1000);
  EXPECT_GE(home.dish->habitat().pantry, 1);
  EXPECT_FALSE(home.dish->appearance().feed.waiting) << "it is back: nothing to wait on";
}

// Not hungry, the pantry empty: the slot still fills, but nobody is waiting.
TEST(Countdown, NotHungryNothingIsWaitedOn) {
  Home home(8);
  std::get<Creature>(home.dish->occupant()).inject(driveChem(drive::hunger), Fx::zero());
  runFor(*home.dish, home.link, home.ms, 1000);
  const FeedView f = home.dish->appearance().feed;
  EXPECT_FALSE(f.waiting);
  EXPECT_GT(f.refillSecs, 0);
}

TEST(Sweep, OneTrace) {
  if (!std::getenv("FOOD_TRACE")) GTEST_SKIP() << "FOOD_TRACE=1 runs it";
  std::printf("%s\n", row(live(SpeciesId{1}, 0x5EED0000u, Kid::Attentive, envDays(1))).c_str());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
