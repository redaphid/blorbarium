// Growing up and starving on the badge's own clock: no battery, no phone, no
// wall time, so his life is the hours he is powered. A field tinker booted at
// age=493487 (13.7 powered hours), starving, still a Child, and Child drew
// exactly as Baby: "the pet never seems to age". These hold that growing up
// shows on the first powered day, and that a pet nobody feeds dies of it,
// with remains and a grave, on that same clock.
#include <gtest/gtest.h>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <variant>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/appearance.h"
#include "blorb/species.h"
#include "blorb/thoughts.h"
#include "links.h"
#include "mem_storage.h"
#include "time_sources.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint32_t kHourTicks = 3600 * 1000 / kTickMs;
constexpr SpeciesId kTinker{8};

}  // namespace

TEST(Aging, AnUnfedPetGrowsUpOnHisFirstDayAndStarvesToDeath) {
  MemStorage store;
  NullLink link;
  FakeRtc rtc;   // a flat battery: never any wall time, so nothing but powered time is lived
  uint32_t ms = 0;
  rtc.poweredMs = &ms;
  const Genome founder = starterGenomeOf(kTinker, 0x5EED0000u);
  DishOptions opt;
  opt.rtc = &rtc;
  opt.founder = &founder;
  Dish dish(store, 1, 1, opt);
  uint32_t childAt = 0, adultAt = 0, diedAt = 0, ageAtDeath = 0;
  // Left lying still and never fed, for up to four powered days, a second a step.
  for (uint64_t t = 0; t < 4ull * 24 * 3600 * 1000 && !diedAt; t += 1000) {
    ms = uint32_t(t);
    dish.sample(still(), ms);
    dish.tick(ms, link);
    if (const Creature* c = std::get_if<Creature>(&dish.occupant())) {
      ASSERT_EQ(c->stats().fed, 0u) << "nobody fed him";
      if (!childAt && c->stage() >= Stage::Child) childAt = c->stats().ageTicks;
      if (!adultAt && c->stage() >= Stage::Adult) adultAt = c->stats().ageTicks;
      ageAtDeath = c->stats().ageTicks;
    } else if (const Clutch* k = std::get_if<Clutch>(&dish.occupant())) {
      diedAt = dish.tickCount();
      EXPECT_EQ(k->cause, DeathCause::Starved);
      EXPECT_EQ(k->reached, Stage::Adult) << "his remains are the adult he grew into";
    }
  }
  std::printf("child at %.1f h, adult at %.1f h, starved at %.1f h\n", double(childAt) / kHourTicks,
              double(adultAt) / kHourTicks, double(ageAtDeath) / kHourTicks);
  ASSERT_NE(childAt, 0u);
  EXPECT_GE(childAt, 2 * kHourTicks) << "a baby a while first";
  EXPECT_LE(childAt, 4 * kHourTicks) << "a child within his first evening";
  ASSERT_NE(adultAt, 0u);
  EXPECT_LE(adultAt, 30 * kHourTicks) << "an adult within his first powered day or so";
  ASSERT_NE(diedAt, 0u) << "a pet nobody feeds dies of it";
  EXPECT_GE(ageAtDeath, 36 * kHourTicks) << "time for a hungry pet to be fed";
  EXPECT_LE(ageAtDeath, 72 * kHourTicks);
  int graves = 0;
  dish.lineage().forEach([&](const LineageEntry& e) {
    if (const Death* d = std::get_if<Death>(&e)) {
      ++graves;
      EXPECT_EQ(d->cause, DeathCause::Starved);
      EXPECT_EQ(d->stats.fed, 0u);
      EXPECT_GE(d->stats.ageTicks, 36 * kHourTicks);
    }
  });
  EXPECT_EQ(graves, 1) << "his grave";
}

// Baby and Child drew alike, so the first growing up was invisible: a child
// stands bigger than a baby and smaller than the adult he becomes.
TEST(Aging, AChildDrawsBetweenTheBabyAndTheAdult) {
  for (size_t sp = 0; sp < kSpeciesCount; ++sp) {
    const Genome g = starterGenomeOf(SPECIES[sp].id, 0x5EED0000u);
    const Appearance baby = portrait(g, Stage::Baby, 0, 0), child = portrait(g, Stage::Child, 0, 0),
                     adult = portrait(g, Stage::Adult, 0, 0);
    EXPECT_GT(child.scalePct, baby.scalePct + 10) << SPECIES[sp].name;
    EXPECT_LT(child.scalePct, adult.scalePct) << SPECIES[sp].name;
  }
}

// ---- naps: powered, the screen dark, the chip asleep between wakes ------------------------------

namespace {

// A tinker hatched and ten minutes old, every second ticked, never fed.
struct Napper {
  MemStorage store;
  NullLink link;
  FakeRtc rtc;   // no wall time: a nap is powered time, not an absence
  uint32_t ms = 0;
  Genome founder = starterGenomeOf(kTinker, 0x5EED0000u);
  std::optional<Dish> dish;
  Napper() {
    rtc.poweredMs = &ms;
    DishOptions opt;
    opt.rtc = &rtc;
    opt.founder = &founder;
    dish.emplace(store, 1, 1, opt);
    while (!std::holds_alternative<Creature>(dish->occupant()) || creature().stats().ageTicks < 6000) step(1000);
  }
  void step(uint32_t forMs) {
    ms += forMs;
    dish->sample(still(), ms);
    dish->tick(ms, link);
  }
  void awake(uint32_t forMs) {
    for (uint32_t end = ms + forMs; ms < end;) step(1000);
  }
  const Creature& creature() const { return std::get<Creature>(dish->occupant()); }
};

double level(Fx v) { return double(v.raw) / double(Fx::kOne); }

}  // namespace

// He wakes from a nap as old as the same time awake makes him, and as hungry,
// in the one call that ends the nap: no fast-forward on the glass after.
TEST(Nap, ANapAgesAndHungersHimAsTheSameTimeAwake) {
  for (uint32_t minutes : {10u, 60u, 5u * 60}) {
    Napper up, nap;
    ASSERT_EQ(up.dish->tickCount(), nap.dish->tickCount());
    up.awake(minutes * 60000);
    nap.step(minutes * 60000);   // the chip slept: one tick() when it woke
    const Creature &a = up.creature(), &b = nap.creature();
    EXPECT_EQ(nap.dish->tickCount(), up.dish->tickCount()) << minutes << " min: caught up in the one call";
    EXPECT_EQ(b.stats().ageTicks, a.stats().ageTicks) << minutes << " min";
    EXPECT_EQ(b.stage(), a.stage()) << minutes << " min";
    const Chemistry &ca = a.chemistry(), &cb = b.chemistry();
    EXPECT_NEAR(level(cb.chem[chem::life.v]), level(ca.chem[chem::life.v]), 0.002) << minutes << " min";
    EXPECT_NEAR(level(cb.drive(drive::hunger)), level(ca.drive(drive::hunger)), 0.1) << minutes << " min";
    EXPECT_NEAR(level(cb.chem[chem::energy.v]), level(ca.chem[chem::energy.v]), 0.03) << minutes << " min";
    EXPECT_EQ(nap.dish->lastCatchUp().awaySeconds, 0u) << "powered: no absence";
    const uint32_t t = nap.dish->tickCount();
    nap.step(100);
    EXPECT_LE(nap.dish->tickCount() - t, 1u) << "no backlog left to run fast on the glass";
  }
}

// A minute's nap (the battery build's longest light sleep) is ticked in full,
// as the firmware's wake does it: tick() again until nothing is behind.
TEST(Nap, AMinutesNapIsTickedInFullAtTheWake) {
  Napper up, nap;
  up.awake(60000);
  nap.ms += 60000;
  int calls = 0;
  while (nap.ms - nap.dish->lastTickMs() >= kTickMs) nap.dish->tick(nap.ms, nap.link), ++calls;
  EXPECT_EQ(calls, 60) << "ten ticks a call";
  EXPECT_EQ(nap.dish->tickCount(), up.dish->tickCount());
  EXPECT_EQ(nap.creature().stats().ageTicks, up.creature().stats().ageTicks);
  EXPECT_EQ(nap.dish->lastCatchUp().ticks, 0u) << "no coarse catch-up for a minute";
}

// The unpowered cap holds for a nap too: twelve hours lived, the rest dropped,
// and it costs a wake almost nothing (the ESP32-S3 is some 20x slower than this host).
TEST(Nap, ANapLongerThanTheCapLivesTwelveHoursQuickly) {
  Napper nap;
  const uint32_t age = nap.creature().stats().ageTicks;
  const auto t0 = std::chrono::steady_clock::now();
  nap.step(30u * 3600 * 1000);
  const double took = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("a 30 h nap: lived in %.2f ms\n", took);
  ASSERT_TRUE(std::holds_alternative<Creature>(nap.dish->occupant()));
  const uint32_t lived = nap.creature().stats().ageTicks - age;
  EXPECT_GE(lived, 12u * 3600 * 10);
  EXPECT_LE(lived, 12u * 3600 * 10 + 10) << "the cap, and the last second";
  EXPECT_EQ(nap.dish->lastCatchUp().clampedTicks, 18u * 3600 * 10 - 10);
  EXPECT_LT(took, 50.0) << "well under a second on the badge";
  const uint32_t t = nap.dish->tickCount();
  nap.step(100);
  EXPECT_LE(nap.dish->tickCount() - t, 1u) << "the dropped hours are not ticked later";
}

// ---- growing up, seen ----------------------------------------------------------------------------

// He grows up in a nap with nobody there: nothing is said to the dark. The
// next hand on him wakes the screen, and he glows and says it, plain, once.
TEST(GrowingUp, AStageReachedInANapIsShownAtTheNextTouchOnce) {
  Napper nap;
  ASSERT_EQ(nap.creature().stage(), Stage::Baby);
  nap.step(4u * 3600 * 1000);   // asleep through his third hour
  ASSERT_EQ(nap.creature().stage(), Stage::Child);
  const std::string said = thoughtInfo(thought::grew_child)->text;
  auto saying = [&] { return std::string(nap.dish->appearance().line); };
  for (int s = 0; s < 30; ++s) {
    nap.step(1000);
    ASSERT_NE(saying(), said) << "nobody is looking: it waits";
  }
  const Fx glowBefore = nap.creature().chemistry().chem[chem::vision.v];
  bool shown = false;
  for (uint32_t t = 0; t < 4000 && !shown; t += kSampleMs) {   // picked up and held
    nap.ms += kSampleMs;
    nap.dish->sample(cradled(), nap.ms);
    nap.dish->tick(nap.ms, nap.link);
    shown = saying() == said;
  }
  ASSERT_TRUE(shown) << "a hand on him: he says he grew";
  EXPECT_GT(nap.creature().chemistry().chem[chem::vision.v], glowBefore) << "and glows";
  for (int s = 0; s < 120; ++s) nap.step(1000);
  int again = 0;
  for (uint32_t t = 0; t < 60000; t += kSampleMs) {
    nap.ms += kSampleMs;
    nap.dish->sample(cradled(), nap.ms);
    nap.dish->tick(nap.ms, nap.link);
    again += saying() == said;
  }
  EXPECT_EQ(again, 0) << "once";
}

TEST(GrowingUp, TheLinesArePlainAndFitTheFont) {
  for (ThoughtId id : {thought::grew_child, thought::grew_adult, thought::grew_elder}) {
    const std::string text = thoughtInfo(id)->text;
    EXPECT_EQ(text.find("DISH"), std::string::npos) << text;
    EXPECT_EQ(text.find("SAY"), std::string::npos) << text;
    EXPECT_TRUE(writable(text.c_str(), kMaxThoughtText, "")) << text;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
