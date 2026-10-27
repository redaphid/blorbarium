// Growing up and starving on the badge's own clock: no battery, no phone, no
// wall time, so his life is the hours he is powered. A field tinker booted at
// age=493487 (13.7 powered hours), starving, still a Child, and Child drew
// exactly as Baby: "the pet never seems to age". These hold that growing up
// shows on the first powered day, and that a pet nobody feeds dies of it,
// with remains and a grave, on that same clock.
#include <gtest/gtest.h>
#include <cstdio>
#include <variant>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/appearance.h"
#include "blorb/species.h"
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
