#include <gtest/gtest.h>
#include <variant>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/appearance.h"
#include "blorb/duel_card.h"

using namespace blorb;

namespace {

constexpr Fx kHalf = Fx::ratio(1, 2);
constexpr Stakes kTiers[] = {Stakes::Spar, Stakes::Scrap, Stakes::Mortal};

// The Dish's part around one occupant, held level with no hand on it.
struct Rig {
  Occupant o;
  Habitat habitat;
  Behaviours behaviours;
  PetClock clock;
  uint32_t tick = 0;

  explicit Rig(const Genome& g) : o(std::in_place_type<Creature>, Egg(Offspring{g, {}}, 0, 0), 0u, 0u) {
    habitat.pantry = c().phenotype().habitat.pantrySize;
  }
  Creature& c() { return std::get<Creature>(o); }
  void step() {
    SenseOut s;
    s.set(locus::tilt_x, kHalf);
    s.set(locus::tilt_y, kHalf);
    habitat.step(c().phenotype().habitat, kHalf, kHalf, c().body().at, tick, s);
    c().tick(s, habitat, behaviours, tick);
    ++tick;
  }
  void run(int n) { while (n-- > 0) step(); }
  void growTo(Stage s) {
    const Fx life[] = {Fx::one(), Fx::ratio(85, 100), Fx::ratio(60, 100), Fx::ratio(15, 100)};
    for (uint8_t k = 1; k <= uint8_t(s); ++k) c().inject(chem::life, life[k]), run(3);
  }
  Mood moodWith(std::initializer_list<std::pair<ChemId, Fx>> levels) {
    for (auto [id, level] : levels) c().inject(id, level);
    return cardOf(c(), 1, 1, 128).mood;
  }
};

Rig starter(SpeciesId s) {
  Rig r(starterGenomeOf(s, 7));
  r.run(10);
  return r;
}

// Every mood byte exactly 128, so one injected chemical is all that moves.
Rig calm() {
  Rig r = starter(kGrungo);
  for (uint8_t d = 0; d < kDriveCount; ++d) r.c().inject(driveChem(DriveId{d}), Fx::zero());
  r.c().inject(driveChem(drive::loneliness), Fx::ratio(1, 4));
  r.c().inject(driveChem(drive::need_touch), Fx::ratio(1, 4));
  r.c().inject(chem::energy, Fx::ratio(3, 4));
  r.c().inject(chem::mettle, Fx::zero());
  r.c().inject(chem::dread, Fx::zero());
  return r;
}

::testing::AssertionResult topIsTheFirstRefusal(const Ceiling& k) {
  for (Stakes s : kTiers) {
    const bool within = uint8_t(s) <= uint8_t(k.top);
    if (within != (k.why[uint8_t(s)] == Decline::None))
      return ::testing::AssertionFailure() << "tier " << int(s) << " vs top " << int(k.top) << ": why "
                                           << int(k.why[uint8_t(s)]);
  }
  return ::testing::AssertionSuccess();
}

}  // namespace

// ---- the card ---------------------------------------------------------------------------

TEST(Card, EachStarterBringsItsSpeciesPowerKnackAndTheBookAsGiven) {
  for (uint8_t i = 0; i < kSpeciesCount; ++i) {
    const SpeciesInfo& info = speciesInfo(SpeciesId{i});
    Rig r = starter(info.id);
    const BattleCard card = cardOf(r.c(), 0xC0FFEEull + i, 7, 141);
    EXPECT_EQ(card.version, DuelRules::kCardVersion);
    EXPECT_EQ(card.species, info.id) << info.name;
    EXPECT_EQ(card.power, info.power) << info.name;
    EXPECT_EQ(card.knack.order, knackOrderOf(knackFor(info.style))) << info.name;
    EXPECT_EQ(card.knack.best(), info.style) << info.name;
    EXPECT_EQ(card.lineageId, 0xC0FFEEull + i);
    EXPECT_EQ(card.level, 7);
    EXPECT_EQ(card.prowess, 141);
  }
}

TEST(Card, AHybridBringsItsBodyAndItsOwnPower) {
  const Genome zapper = *GenomeBuilder::from(starterGenome(7)).append(PowerGene{power::zap.v, 0}, GeneFlags::Mutable).build();
  Rig r(zapper);
  r.run(10);
  ASSERT_TRUE(r.c().phenotype().species.hybrid());
  const BattleCard card = cardOf(r.c(), 1, 1, 128);
  EXPECT_EQ(card.species, kGrungo);
  EXPECT_EQ(card.power, power::zap) << "a grungo who zaps";
}

// The friend draws him from the card, so it must be what his own screen draws.
TEST(Card, TheLookIsWhatPresentDrawsForEveryStarterAtEveryStage) {
  for (uint8_t i = 0; i < kSpeciesCount; ++i) {
    for (Stage stage : {Stage::Baby, Stage::Child, Stage::Adult, Stage::Elder}) {
      Rig r = starter(SpeciesId{i});
      r.growTo(stage);
      ASSERT_EQ(r.c().stage(), stage);
      r.c().inject(chem::adrenaline, Fx::ratio(7, 10));   // whatever a palette gene binds, bind something
      const CardLook look = cardOf(r.c(), 1, 1, 128).look;
      const Appearance a = present(r.o, r.habitat, r.clock, r.tick);
      const std::string at = std::string(speciesInfo(SpeciesId{i}).name) + " stage " + std::to_string(int(stage));
      EXPECT_EQ(look.stage, a.stage) << at;
      EXPECT_EQ(look.scalePct, a.scalePct) << at;
      EXPECT_EQ(look.lifeSeed, a.lifeSeed) << at;
      for (uint8_t k = 0; k < kRegionCount; ++k) {
        EXPECT_EQ(look.regions[k].hue, a.regions[k].hue) << at << " region " << int(k);
        EXPECT_EQ(look.regions[k].sat, a.regions[k].sat) << at << " region " << int(k);
        EXPECT_EQ(look.regions[k].val, a.regions[k].val) << at << " region " << int(k);
      }
      EXPECT_EQ(look.markCount, std::min<uint8_t>(a.markCount, 4)) << at;
      for (uint8_t k = 0; k < look.markCount; ++k) {
        EXPECT_EQ(look.marks[k].layer, a.marks[k].layer) << at;
        EXPECT_EQ(look.marks[k].variant, a.marks[k].variant) << at;
      }
    }
  }
}

TEST(Card, ABuiltCardIsFrozenWhileHisChemistryGoesOn) {
  Rig r = calm();
  const BattleCard frozen = cardOf(r.c(), 1, 1, 128);
  r.c().inject(driveChem(drive::fear), Fx::one());
  r.run(5);
  EXPECT_LT(cardOf(r.c(), 1, 1, 128).mood.nerve, frozen.mood.nerve) << "a new card reads him as he is now";
  EXPECT_EQ(frozen.mood.nerve, 128);
}

// ---- the mood -----------------------------------------------------------------------------

TEST(Mood, ACalmPetSitsInTheMiddleAndLeansNowhere) {
  const Mood m = calm().moodWith({});
  EXPECT_EQ(m.nerve, 128);
  EXPECT_EQ(m.pep, 128);
  EXPECT_EQ(m.trust, 128);
  EXPECT_EQ(m.spirit, 128);
}

// duel_mind leans Guard below nerve 96 and Pounce above pep 160, and follows
// the kid's habit by 48 a point of trust past 160; one knack rank is 2048.
TEST(Mood, ARestedStarterLeansOnlyGentlyTowardHisKid) {
  for (uint8_t i = 0; i < kSpeciesCount; ++i) {
    Rig r = starter(SpeciesId{i});
    for (int minutes : {0, 15}) {
      r.run(minutes * int(kTicksPerMinute));
      const Mood m = cardOf(r.c(), 1, 1, 128).mood;
      const std::string at = std::string(speciesInfo(SpeciesId{i}).name) + " +" + std::to_string(minutes) + " min";
      EXPECT_GE(m.nerve, 96) << at;
      EXPECT_LE(m.pep, 160) << at;
      EXPECT_GT(m.trust, 160) << at << ": kept company, he trusts his kid";
      EXPECT_LT(48 * (m.trust - 160), 2048) << at << ": but less than his knack";
    }
  }
}

TEST(Mood, FearAndDreadLowerNerveAndMettleRaisesIt) {
  EXPECT_LT(calm().moodWith({{driveChem(drive::fear), kHalf}}).nerve, 96) << "a frightened pet leans Guard";
  EXPECT_LT(calm().moodWith({{chem::dread, kHalf}}).nerve, 96) << "a beaten pet leans Guard";
  EXPECT_GT(calm().moodWith({{chem::mettle, kHalf}}).nerve, 160) << "a battle-hardened pet leans bolder";
  EXPECT_EQ(calm().moodWith({{chem::mettle, kHalf}, {chem::dread, kHalf}}).nerve, 128) << "they cancel";
  EXPECT_EQ(calm().moodWith({{driveChem(drive::fear), Fx::one()}, {chem::dread, Fx::one()}}).nerve, 0)
      << "clamped, not wrapped";
  EXPECT_EQ(calm().moodWith({{chem::mettle, Fx::one()}}).nerve, 255);
}

TEST(Mood, EnergyAndBoredomRaisePep) {
  EXPECT_GT(calm().moodWith({{driveChem(drive::boredom), kHalf}}).pep, 160) << "a bored pet leans Pounce";
  EXPECT_EQ(calm().moodWith({{chem::energy, Fx::one()}}).pep, 160);
  EXPECT_EQ(calm().moodWith({{chem::energy, Fx::zero()}}).pep, 32) << "a tired pet does not";
}

TEST(Mood, SleepinessAndPainLowerSpirit) {
  EXPECT_EQ(calm().moodWith({{driveChem(drive::sleepiness), kHalf}}).spirit, 64);
  EXPECT_EQ(calm().moodWith({{driveChem(drive::pain), kHalf}}).spirit, 64);
}

TEST(Mood, CompanyRaisesTrustAndLonelinessLowersIt) {
  EXPECT_EQ(calm().moodWith({{driveChem(drive::loneliness), Fx::zero()}, {driveChem(drive::need_touch), Fx::zero()}})
                .trust,
            192);
  EXPECT_EQ(calm().moodWith({{driveChem(drive::loneliness), Fx::one()}, {driveChem(drive::need_touch), Fx::one()}})
                .trust,
            0);
}

// Through D4's own mind: the mood bytes are read the way duel_mind reads them.
TEST(Mood, AFrightenedPetsAutopilotGuardsMoreThanACalmOnes) {
  const BattleCard calmCard = cardOf(calm().c(), 1, 1, 128);
  Rig scared = calm();
  scared.c().inject(driveChem(drive::fear), Fx::ratio(7, 10));
  const BattleCard scaredCard = cardOf(scared.c(), 1, 1, 128);
  const BattleCard friendCard = cardOf(starter(SpeciesId{1}).c(), 2, 1, 128);
  const DuelSkill fresh{};
  const Outcome o = resolve(calmCard, friendCard, 1, Stakes::Spar, MoveLog{}, MoveLog{});
  int calmGuards = 0, scaredGuards = 0;
  for (uint32_t seed = 0; seed < 300; ++seed) {
    calmGuards += petChoice(fresh, o, Side::A, calmCard, friendCard, 0, seed).stance == Stance::Guard;
    scaredGuards += petChoice(fresh, o, Side::A, scaredCard, friendCard, 0, seed).stance == Stance::Guard;
  }
  EXPECT_GT(scaredGuards, calmGuards + 60);
}

// ---- readiness ------------------------------------------------------------------------------

TEST(Readiness, ARestedFedAwakePetIsReady) {
  Rig r = calm();
  EXPECT_EQ(readiness(r.o, 0, UINT32_MAX, false), Decline::None);
  EXPECT_EQ(readiness(r.o, 2, kRestTicks, false), Decline::None);
}

TEST(Readiness, AnEggAClutchAndAPetAlreadyGoneHaveNothingToDuelWith) {
  const Occupant egg{std::in_place_type<Egg>, Offspring{starterGenome(7), {}}, uint16_t(0), 0u};
  EXPECT_EQ(readiness(egg, 0, UINT32_MAX, false), Decline::NotACreature);
  const Occupant clutch{std::in_place_type<Clutch>};
  EXPECT_EQ(readiness(clutch, 0, UINT32_MAX, false), Decline::NotACreature);
  Rig r = calm();
  r.c().inject(chem::life, Fx::ratio(5, 100));
  r.step();
  ASSERT_TRUE(r.c().dead());
  EXPECT_EQ(readiness(r.o, 0, UINT32_MAX, false), Decline::NotACreature);
}

TEST(Readiness, AnAsleepPetSleepsOn) {
  Rig r = calm();
  for (int i = 0; i < 50 && !r.c().body().asleep; ++i) {
    r.c().inject(driveChem(drive::sleepiness), Fx::one());
    r.c().inject(chem::melatonin, Fx::one());
    r.c().force(action::sleep);
    r.step();
  }
  ASSERT_TRUE(r.c().body().asleep);
  EXPECT_EQ(readiness(r.o, 0, UINT32_MAX, false), Decline::Asleep);
}

TEST(Readiness, AVerySleepyPetDeclines) {
  Rig r = calm();
  r.c().inject(driveChem(drive::sleepiness), kDeclineSleepy - Fx::ratio(1, 100));
  EXPECT_EQ(readiness(r.o, 0, UINT32_MAX, false), Decline::None);
  r.c().inject(driveChem(drive::sleepiness), kDeclineSleepy);
  EXPECT_EQ(readiness(r.o, 0, UINT32_MAX, false), Decline::Sleepy);
}

TEST(Readiness, AVeryHungryPetWantsHisBowlFirst) {
  Rig r = calm();
  r.c().inject(driveChem(drive::hunger), kDeclineHungry - Fx::ratio(1, 100));
  EXPECT_EQ(readiness(r.o, 0, UINT32_MAX, false), Decline::None);
  r.c().inject(driveChem(drive::hunger), kDeclineHungry);
  EXPECT_EQ(readiness(r.o, 0, UINT32_MAX, false), Decline::Hungry);
}

TEST(Readiness, HeRestsTwoMinutesAfterADuelAndFiveAfterAFaint) {
  Rig r = calm();
  EXPECT_EQ(readiness(r.o, 0, 2 * kTicksPerMinute - 1, false), Decline::Resting);
  EXPECT_EQ(readiness(r.o, 0, 2 * kTicksPerMinute, false), Decline::None);
  EXPECT_EQ(readiness(r.o, 0, 5 * kTicksPerMinute - 1, true), Decline::Resting);
  EXPECT_EQ(readiness(r.o, 0, 5 * kTicksPerMinute, true), Decline::None);
}

TEST(Readiness, ThreeLossesInARowAndHeWantsACuddle) {
  Rig r = calm();
  EXPECT_EQ(readiness(r.o, 2, UINT32_MAX, false), Decline::None);
  EXPECT_EQ(readiness(r.o, 3, UINT32_MAX, false), Decline::WantsACuddle);
  EXPECT_EQ(readiness(r.o, 255, UINT32_MAX, false), Decline::WantsACuddle);
}

// ---- the ceiling ------------------------------------------------------------------------------

TEST(Ceiling, EachStageMayEnterTheTiersItIsOldEnoughFor) {
  struct Row { Stage stage; Stakes top; Decline scrap, mortal; };
  const Row rows[] = {{Stage::Baby, Stakes::Spar, Decline::TooYoung, Decline::TooYoung},
                      {Stage::Child, Stakes::Scrap, Decline::None, Decline::TooYoung},
                      {Stage::Adult, Stakes::Mortal, Decline::None, Decline::None},
                      {Stage::Elder, Stakes::Mortal, Decline::None, Decline::None}};
  for (const Row& row : rows) {
    Rig r = starter(kGrungo);
    r.growTo(row.stage);
    r.c().inject(chem::injury, Fx::zero());
    ASSERT_EQ(r.c().stage(), row.stage);
    const Ceiling k = ceilingFor(r.c(), Stakes::Mortal);
    EXPECT_EQ(k.top, row.top) << int(row.stage);
    EXPECT_EQ(k.why[0], Decline::None) << "a Spar is always on offer";
    EXPECT_EQ(k.why[1], row.scrap) << int(row.stage);
    EXPECT_EQ(k.why[2], row.mortal) << int(row.stage);
    EXPECT_TRUE(topIsTheFirstRefusal(k));
  }
}

TEST(Ceiling, AWoundedPetStillSparsButDoesNotFight) {
  Rig r = starter(kGrungo);
  r.growTo(Stage::Adult);
  r.c().inject(chem::injury, kWoundedInjury);
  const Ceiling k = ceilingFor(r.c(), Stakes::Mortal);
  EXPECT_EQ(k.top, Stakes::Spar);
  EXPECT_EQ(k.why[0], Decline::None);
  EXPECT_EQ(k.why[1], Decline::Wounded);
  EXPECT_EQ(k.why[2], Decline::Wounded);
  EXPECT_TRUE(topIsTheFirstRefusal(k));
  r.c().inject(chem::injury, kWoundedInjury - Fx::ratio(1, 100));
  EXPECT_NE(ceilingFor(r.c(), Stakes::Mortal).why[1], Decline::Wounded) << "the bandage comes off at 0.5";
}

// The forecast's skull full: the tier's worst case would take all the room
// woundRoom leaves him, so the tier is greyed with the skull.
TEST(Ceiling, ATierWhoseForecastSkullIsFullIsRefused) {
  Rig r = starter(kGrungo);
  r.growTo(Stage::Adult);
  r.c().inject(chem::injury, Fx::zero());
  const Fx healthyRoom = woundRoom(r.c());
  ASSERT_GT(healthyRoom, rule(Stakes::Mortal).injuryCapPerDuel) << "a healthy adult has room for a mortal duel";
  EXPECT_LT(forecast(r.c(), Stakes::Mortal).skull, 255);
  EXPECT_EQ(ceilingFor(r.c(), Stakes::Mortal).top, Stakes::Mortal);

  // Hurt him until the room is between the two tiers' caps.
  const Fx between = (rule(Stakes::Scrap).injuryCapPerDuel + rule(Stakes::Mortal).injuryCapPerDuel) * kHalf;
  r.c().inject(chem::injury, healthyRoom - between);
  ASSERT_LT(r.c().chemistry().chem[chem::injury.v], kWoundedInjury) << "not yet a bandage";
  ASSERT_EQ(forecast(r.c(), Stakes::Mortal).skull, 255);
  ASSERT_LT(forecast(r.c(), Stakes::Scrap).skull, 255);
  Ceiling k = ceilingFor(r.c(), Stakes::Mortal);
  EXPECT_EQ(k.top, Stakes::Scrap);
  EXPECT_EQ(k.why[2], Decline::Fragile);
  EXPECT_TRUE(topIsTheFirstRefusal(k));

  // No room at all: only the Spar, which wounds nobody, is left.
  r.c().inject(chem::injury, healthyRoom);
  ASSERT_EQ(woundRoom(r.c()), Fx::zero());
  k = ceilingFor(r.c(), Stakes::Mortal);
  EXPECT_EQ(k.top, Stakes::Spar);
  EXPECT_EQ(k.why[0], Decline::None) << "a spar never wounds, so it is never too much";
}

TEST(Ceiling, TheDevicesCeilingHoldsAHealthyAdultDown) {
  Rig r = starter(kGrungo);
  r.growTo(Stage::Adult);
  r.c().inject(chem::injury, Fx::zero());
  for (Stakes device : kTiers) {
    const Ceiling k = ceilingFor(r.c(), device);
    EXPECT_EQ(k.top, device);
    for (Stakes s : kTiers)
      EXPECT_EQ(k.why[uint8_t(s)], uint8_t(s) > uint8_t(device) ? Decline::DeviceCeiling : Decline::None);
    EXPECT_TRUE(topIsTheFirstRefusal(k));
  }
}

TEST(Ceiling, ThePetsOwnReasonIsShownBeforeTheDevices) {
  Rig r = starter(kGrungo);
  r.growTo(Stage::Child);
  const Ceiling k = ceilingFor(r.c(), Stakes::Spar);
  EXPECT_EQ(k.why[1], Decline::DeviceCeiling);
  EXPECT_EQ(k.why[2], Decline::TooYoung);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
