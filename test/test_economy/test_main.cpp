// The food economy (owner, 2026-10-27: the kids' pets were starving and
// "have no food"). How fast he gets hungry, what a pellet is worth, how fast
// the pantry refills and when a pellet rots, measured on the whole Dish; and
// the promise those numbers have to keep: a kid who feeds him whenever he
// asks never sees him starve, through his nights and the 12-hour catch-up,
// while a pet nobody feeds still starves ("starving pets are supposed to
// die"). One press brings a starving pet back, whatever his line's genes did
// to his fed response.
//
// ECONOMY_VERBOSE=1 prints each day; ECONOMY_ALL=1 runs every species.
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <variant>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/species.h"
#include "../../src/debug_verbs.h"   // here, so the dependency finder links lib/debugble
#include "links.h"
#include "mem_storage.h"
#include "traces.h"
#include "tinker_5c94.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint32_t kMinuteMs = 60u * 1000u;
constexpr uint32_t kHourMs = 60u * kMinuteMs;
constexpr int kStarving = 800;   // permille: creature.h kEatsRotAt, where hunger turns into injury

int permille(Fx f) { return int(int64_t(f.raw) * 1000 / Fx::kOne); }
bool verbose() { return std::getenv("ECONOMY_VERBOSE") != nullptr; }

// One badge on one store, sampled and ticked a tick at a time as the loop does.
struct Life {
  MemStorage store;
  Genome founder;
  std::optional<Dish> dish;
  NullLink link;
  uint32_t ms = 0;
  int worstHunger = 0, worstInjury = 0, starvingGlances = 0, glances = 0, presses = 0, bites = 0;

  explicit Life(SpeciesId sp, uint32_t seed) : Life(starterGenomeOf(sp, seed), seed) {}
  Life(Genome g, uint32_t seed) : founder(std::move(g)) {
    DishOptions opt;
    opt.founder = &founder;
    opt.foundingHour = kUnaskedFoundingHour;   // as the firmware founds: into his morning
    dish.emplace(store, seed, seed, opt);
    dish->tick(ms, link);
  }
  Creature* pet() { return std::get_if<Creature>(&dish->occupant()); }
  const Clutch* remains() const { return std::get_if<Clutch>(&dish->occupant()); }
  int hunger() { return permille(pet()->chemistry().drive(drive::hunger)); }

  void step(bool button = false, bool held = false) {
    ms += kTickMs;
    BodySample s = held ? cradled() : still();
    s.buttonDown = button;
    const Creature* c = pet();
    const Mouthful before = c ? c->body().mouth : Mouthful::Nothing;
    dish->sample(s, ms);
    dish->tick(ms, link);
    if (const Creature* k = pet(); k && before == Mouthful::Nothing && k->body().mouth != Mouthful::Nothing) ++bites;
  }
  void press() {
    for (int i = 0; i < 2; ++i) step(true);
    for (int i = 0; i < 6; ++i) step();
    ++presses;
  }
  // Powered, with a kid about: every glanceMs she picks the badge up (which
  // wakes him), puts it down, and presses BOOT when the rim asks for food and
  // none is in the dish. Nothing else: no extra presses, no treats at bedtime.
  void day(uint32_t forMs, uint32_t glanceMs = 10 * kMinuteMs) {
    const uint32_t end = ms + forMs;
    uint32_t next = ms;
    while (ms < end && !remains()) {
      if (ms < next || !pet()) {
        step();
        continue;
      }
      next = ms + glanceMs;
      ++glances;
      worstHunger = std::max(worstHunger, hunger());
      if (hunger() >= kStarving) ++starvingGlances;
      for (int i = 0; i < 20; ++i) step(false, true);
      for (int i = 0; i < 10; ++i) step();
      const Appearance a = dish->appearance();
      if (pet() && a.hasHint && a.hint == care::feed && !dish->habitat().nearestPellet(DishPos{})) press();
    }
    if (Creature* c = pet()) worstInjury = std::max(worstInjury, permille(c->chemistry().chem[chem::injury.v]));
  }
  // Nobody there, powered: the badge lies still and dark and he lives every tick.
  void alone(uint32_t forMs) {
    for (const uint32_t end = ms + forMs; ms < end && !remains();) step();
  }
  // Deep sleep: one gap, lived by the catch-up (at most 12 h of it).
  void gap(uint32_t forMs) {
    ms += forMs;
    dish->tick(ms, link);
  }
  void report(const char* what) {
    if (!verbose()) return;
    Creature* c = pet();
    if (!c) return (void)std::printf("  %-8s no creature\n", what);
    std::printf("  %-8s hunger=%4d energy=%4d injury=%4d pantry=%u stage=%d presses=%d bites=%d\n", what, hunger(),
                permille(c->chemistry().chem[chem::energy.v]), permille(c->chemistry().chem[chem::injury.v]),
                dish->habitat().pantry, int(c->stage()), presses, bites);
  }
};

std::vector<SpeciesId> speciesUnderTest() {
  std::vector<SpeciesId> all;
  for (uint8_t s = 0; s < kSpeciesCount; ++s) all.push_back(SpeciesId{s});
  if (std::getenv("ECONOMY_ALL")) return all;
  // The hungriest (mudge 1.3x, fizz 1.2x), the night owls (grungo, octopus,
  // hushwing), and the least hungry (mossby 0.8x).
  return {SpeciesId{0}, SpeciesId{1}, SpeciesId{5}, SpeciesId{6}, SpeciesId{7}, SpeciesId{9}};
}

// A starving adult, as 5C94094581 was found: hunger pinned, energy gone,
// hurting, an empty pantry and an empty dish. He gets one pellet back.
Life& starve(Life& life) {
  life.day(4 * kHourMs);   // hatched, fed, settled
  Creature& c = *life.pet();
  c.inject(driveChem(drive::hunger), Fx::one());
  c.inject(chem::energy, Fx::zero());
  c.inject(chem::injury, Fx::ratio(4, 10));
  life.dish->habitat().pantry = 1;
  for (Pellet& p : life.dish->habitat().pellets) p.present = false;
  for (int i = 0; i < 20; ++i) life.step(false, true);   // a hand: he is awake
  life.bites = life.presses = 0;
  return life;
}

}  // namespace

// The numbers, measured on grungo's recipe and printed for the record.
TEST(Economy, TheNumbers) {
  Life life(SpeciesId{0}, 11);
  const HabitatRules rules = [&] {
    life.day(30 * kMinuteMs);
    return life.pet()->phenotype().habitat;
  }();
  std::printf("[economy] pantry %u, one pellet back every %.1f pet-hours (%.0f a day), rots after %u min, bite %d food\n",
              unsigned(rules.pantrySize), double(rules.refillTicks) / kTicksPerHour,
              24.0 * kTicksPerHour / rules.refillTicks, unsigned(rules.rotTicks / kTicksPerMinute),
              permille(rules.biteSize));
  EXPECT_EQ(rules.refillTicks, 2 * kTicksPerHour);
  EXPECT_EQ(rules.pantrySize, 4);

  // From a pellet eaten at the feed hint, awake and fed by nobody: hours to the next hint and to starving.
  Creature& c = *life.pet();
  c.inject(driveChem(drive::hunger), Fx::ratio(5, 10));
  c.inject(chem::energy, Fx::ratio(26, 100));
  for (int i = 0; i < 20; ++i) life.step(false, true);
  life.press();
  life.alone(2 * kMinuteMs);
  const int fedHunger = life.hunger();
  double hint = -1, starving = -1;
  for (int m = 0; m < 24 * 60 && life.pet() && starving < 0; ++m) {
    for (int i = 0; i < 20; ++i) life.step(false, m % 10 == 0);   // a hand now and then, so he stays up
    life.alone(kMinuteMs - 20 * kTickMs);
    if (hint < 0 && life.hunger() >= 500) hint = m / 60.0;
    if (life.hunger() >= kStarving) starving = m / 60.0;
  }
  std::printf("[economy] a pellet at the hint: hunger 500 -> %d; hungry again after %.1f h, starving after %.1f h awake\n",
              fedHunger, hint, starving);
  EXPECT_LE(fedHunger, 200);
  EXPECT_GE(hint, 3.0) << "a pellet keeps him fed for hours";
  EXPECT_GE(24.0 * kTicksPerHour / rules.refillTicks, 2 * (24.0 / hint)) << "the pantry refills twice as fast as he eats";
}

// One press brings a starving pet back: the pellet is eaten, hunger falls
// under the feed hint at once, and he is not hungry again for hours.
TEST(Economy, OnePressEndsStarving) {
  Life life(SpeciesId{0}, 21);
  starve(life);
  life.press();
  life.alone(kMinuteMs);
  EXPECT_EQ(life.bites, 1) << "he ate it";
  EXPECT_LT(life.hunger(), 500) << "fed: under the feed hint";
  int worst = 0;
  for (int m = 0; m < 3 * 60 && life.pet(); ++m) {
    for (int i = 0; i < 10; ++i) life.step(false, m % 10 == 0);
    life.alone(kMinuteMs - 10 * kTickMs);
    worst = std::max(worst, life.hunger());
  }
  EXPECT_LT(worst, 500) << "not hungry again within 3 hours";
}

// A line whose fed gene drifted (the stimulus mutates to another id, or its
// chemical to another one: genes.cpp rules_stimulus) is fed by pellets all
// the same.
TEST(Economy, APelletFeedsHimWhateverHisFedGeneBecame) {
  Genome g = starterGenomeOf(SpeciesId{0}, 31);
  GenomeBuilder b = GenomeBuilder::from(g);
  int bent = 0;
  g.forEach([&](const GeneView& v) {
    if (v.header.type != GeneKindOf<StimulusGene>::value || v.body[0] != stim::fed.v) return;
    b.setByte(v.header.uid, 0, stim::spoken_to.v);   // his fed response now answers a phone's words
    ++bent;
  });
  ASSERT_EQ(bent, 1);
  Life life(*b.build(), 31);
  starve(life);
  life.press();
  life.alone(kMinuteMs);
  EXPECT_EQ(life.bites, 1);
  EXPECT_LT(life.hunger(), 500);
}

// The badge found starving, by its own genome: a pellet feeds him.
TEST(Economy, TheStarvingTinkerIsFedByOnePress) {
  std::optional<Genome> g = Genome::parse(kTinker5C94, sizeof kTinker5C94);
  ASSERT_TRUE(g.has_value());
  int fed = 0;
  g->forEach([&](const GeneView& v) {
    if (v.header.type != GeneKindOf<StimulusGene>::value || v.body[0] != stim::fed.v) return;
    ++fed;
    std::printf("[economy] tinker 5C94 fed gene uid %u: chem %u %u %u amount %u %u %u flags %u\n", unsigned(v.header.uid.v),
                v.body[2], v.body[3], v.body[4], v.body[5], v.body[6], v.body[7], unsigned(v.header.flags));
  });
  std::printf("[economy] tinker 5C94 has %d fed gene(s)\n", fed);
  Life life(*g, 61);
  starve(life);
  const int before = life.hunger();
  life.press();
  life.alone(kMinuteMs);
  std::printf("[economy] tinker 5C94: one press, hunger %d -> %d, bites %d\n", before, life.hunger(), life.bites);
  EXPECT_EQ(life.bites, 1);
  EXPECT_LT(life.hunger(), 500);
}

// The promise: fed whenever he asks, by day, he never starves; the nights
// are his to sleep, powered (every tick lived) or in deep sleep (one gap,
// 14 h of it, of which the catch-up lives its cap of 12).
TEST(Economy, AKidWhoFeedsHimWheneverHeIsHungryNeverSeesHimStarve) {
  for (SpeciesId sp : speciesUnderTest()) {
    Life life(sp, 41 + sp.v);
    life.day(12 * kHourMs);   // hatch at 09:00, up until 21:00
    for (int night = 0; night < 3 && !life.remains(); ++night) {
      life.report("bedtime");
      if (night % 2 == 0) life.alone(10 * kHourMs);
      else life.gap(14 * kHourMs);
      life.report("morning");
      life.day(14 * kHourMs);
    }
    life.report("end");
    std::printf("[economy] %-9s fed when hungry: alive=%d worst hunger=%d starving glances=%d/%d worst injury=%d presses=%d bites=%d\n",
                speciesInfo(sp).name, life.pet() != nullptr, life.worstHunger, life.starvingGlances, life.glances,
                life.worstInjury, life.presses, life.bites);
    EXPECT_TRUE(life.pet()) << speciesInfo(sp).name << " died";
    EXPECT_EQ(life.starvingGlances, 0) << speciesInfo(sp).name;
    EXPECT_LT(life.worstHunger, kStarving) << speciesInfo(sp).name;
    EXPECT_LT(life.worstInjury, 100) << speciesInfo(sp).name << ": hurt by hunger";
    EXPECT_GE(life.bites, life.presses * 9 / 10) << speciesInfo(sp).name << ": the pellets were eaten, not left to rot";
  }
}

// Neglect still kills: a badge nobody touches, powered on a desk, with food
// in its pantry that nobody drops. He starves, and dies of it within days.
TEST(Economy, APetNobodyFeedsStarvesToDeath) {
  for (SpeciesId sp : {SpeciesId{0}, SpeciesId{1}}) {
    Life life(sp, 51 + sp.v);
    life.alone(30 * kMinuteMs);   // hatched
    ASSERT_TRUE(life.pet());
    double starvingAt = -1;
    for (int h = 0; h < 8 * 24 && !life.remains(); ++h) {
      life.alone(kHourMs);
      if (starvingAt < 0 && life.pet() && life.hunger() >= kStarving) starvingAt = h;
      if (h % 12 == 0) life.report("alone");
    }
    const Clutch* k = life.remains();
    std::printf("[economy] %-9s unfed: starving from hour %.0f, %s at hour %.1f\n", speciesInfo(sp).name, starvingAt,
                k ? (k->cause == DeathCause::Starved ? "starved" : "died") : "alive", life.ms / double(kHourMs));
    ASSERT_NE(k, nullptr) << speciesInfo(sp).name << " outlived 8 days unfed";
    EXPECT_TRUE(k->cause == DeathCause::Starved || k->cause == DeathCause::Injured) << int(k->cause);
    EXPECT_GE(starvingAt, 0);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
