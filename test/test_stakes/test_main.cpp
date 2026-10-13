#include <gtest/gtest.h>
#include <optional>
#include <variant>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/stakes.h"
#include "blorb/starter_duel.h"
#include "links.h"
#include "mem_storage.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr Fx kHalf = Fx::ratio(1, 2);
constexpr uint8_t kTurns = 15;   // DuelRules::kMaxTurns (duel.h, unit D4)
constexpr Stakes kTiers[] = {Stakes::Spar, Stakes::Scrap, Stakes::Mortal};
constexpr StimId kDuelStimuli[] = {stim::duel_mourned, stim::duel_slew,  stim::duel_spared, stim::duel_bonked,
                                   stim::duel_landed,  stim::duel_won,   stim::duel_fainted, stim::duel_befriended};

Genome duelStarter() {
  GenomeBuilder b = GenomeBuilder::from(starterGenome(7));
  return *appendDuelGenes(b).build();
}

Genome plus(const Genome& g, GeneHeader h, const uint8_t* body) {
  h.uid = g.nextUid();
  return *GenomeBuilder::from(g).append(h, body).build();
}
Genome withStimulusGene(const Genome& g, StimulusGene s) {
  return plus(g, GeneHeader{GeneKindOf<StimulusGene>::value, sizeof s, GeneFlags::Mutable, Stage::Baby, 0, 64, {}},
              &s.stim);
}
Genome withReceptor(const Genome& g, ReceptorGene r) {
  return plus(g, GeneHeader{GeneKindOf<ReceptorGene>::value, sizeof r, GeneFlags::Mutable, Stage::Baby, 0, 64, {}},
              &r.chem);
}

// The Dish's part around one creature, held level with no hand on it.
struct Rig {
  Creature c;
  Habitat habitat;
  Behaviours behaviours;
  uint32_t tick = 0;

  explicit Rig(const Genome& g) : c(Egg(Offspring{g, {}}, 0, 0), 0, 0) {
    habitat.pantry = c.phenotype().habitat.pantrySize;
  }
  void step(SenseOut s = {}) {
    s.set(locus::tilt_x, kHalf);
    s.set(locus::tilt_y, kHalf);
    habitat.step(c.phenotype().habitat, kHalf, kHalf, c.body().at, tick, s);
    c.tick(s, habitat, behaviours, tick);
    ++tick;
  }
  void fire(StimId id) {
    SenseOut s;
    s.fire(id);
    step(s);
  }
  Fx chem(ChemId id) const { return c.chemistry().chem[id.v]; }
};

// Every duel stimulus 1,000 times, one a tick, beside a twin that hears none.
// Returns false at the first tick their injury, toxin or life part.
::testing::AssertionResult feltLikeTwin(const Genome& g, Stakes s) {
  Rig fed(g), twin(g);
  fed.c.setDuelStakes(s);
  twin.c.setDuelStakes(s);
  for (int round = 0; round < 1000; ++round)
    for (StimId id : kDuelStimuli) {
      fed.fire(id);
      twin.step();
      for (ChemId c : {chem::injury, chem::toxin, chem::life})
        if (fed.chem(c) != twin.chem(c))
          return ::testing::AssertionFailure() << "chem " << int(c.v) << " parted at round "
                                               << round << " stim " << int(id.v) << ": " << fed.chem(c).raw
                                               << " vs " << twin.chem(c).raw;
      if (fed.c.dead()) return ::testing::AssertionFailure() << "died at round " << round;
    }
  return ::testing::AssertionSuccess();
}

}  // namespace

// ---- the filter ---------------------------------------------------------------------------

TEST(Filter, NoTierLetsAStimulusTouchInjuryToxinLifeOrAnUnnamedChemical) {
  for (Stakes s : kTiers)
    for (int c = 0; c < 256; ++c) {
      const ChemId id{uint8_t(c)};
      const bool named = isDriveChem(id) || id == chem::adrenaline || id == chem::daze || id == chem::pluck ||
                         id == chem::mettle || id == chem::dread;
      for (Fx delta : {Fx::one(), -Fx::one(), Fx::ratio(1, 100)}) {
        const Fx out = duelFilter(s, id, delta);
        if (!named) EXPECT_EQ(out, Fx::zero()) << "chem " << c << " tier " << int(s);
        if (named && delta > Fx::zero() && id != chem::daze && id != chem::pluck)
          EXPECT_LE(out, kDuelDriveRiseCap) << "chem " << c << " rose past the cap";
      }
    }
}

TEST(Filter, ASparLeavesNothingLastingAndNeverHurts) {
  EXPECT_EQ(duelFilter(Stakes::Spar, chem::mettle, Fx::ratio(1, 10)), Fx::zero());
  EXPECT_EQ(duelFilter(Stakes::Spar, driveChem(drive::pain), Fx::ratio(1, 10)), Fx::zero());
  EXPECT_EQ(duelFilter(Stakes::Scrap, chem::mettle, Fx::ratio(1, 10)), Fx::ratio(1, 10));
  EXPECT_EQ(duelFilter(Stakes::Spar, driveChem(drive::fear), -Fx::one()), -Fx::one()) << "drives fall freely";
}

TEST(Filter, OnlyDuelStimuliAreFiltered) {
  // A drop still injures, mid-duel or not: the body is real.
  Rig r(duelStarter());
  r.c.setDuelStakes(Stakes::Spar);
  const Fx before = r.chem(chem::injury);
  r.fire(stim::dropped);
  r.step();
  EXPECT_GT(r.chem(chem::injury), before);
}

TEST(Filter, EveryDuelStimulusAThousandTimesKeepsInjuryToxinAndLifeAtEachTier) {
  for (Stakes s : kTiers) EXPECT_TRUE(feltLikeTwin(duelStarter(), s)) << "tier " << int(s);
}

TEST(Filter, APlantedBonkToInjuryGeneChangesNothing) {
  const Genome planted = withStimulusGene(
      duelStarter(), StimulusGene{stim::duel_bonked.v, 1, {chem::injury.v, chem::toxin.v, chem::life.v}, {255, 255, 0}});
  for (Stakes s : kTiers) EXPECT_TRUE(feltLikeTwin(planted, s)) << "tier " << int(s);
}

TEST(Filter, ADuelStimulusRaisesPainOnlyWhenTheStakesAllowIt) {
  for (Stakes s : kTiers) {
    Rig fed(duelStarter()), twin(duelStarter());
    fed.c.setDuelStakes(s);
    fed.fire(stim::duel_bonked);
    twin.step();
    const Fx pain = fed.chem(driveChem(drive::pain)), twinPain = twin.chem(driveChem(drive::pain));
    if (s == Stakes::Spar) EXPECT_EQ(pain, twinPain);
    else EXPECT_GT(pain, twinPain);
  }
}

TEST(Filter, AFaintFloodsDazeAndHoldsTheSleepGate) {
  Rig r(duelStarter());
  r.fire(stim::duel_fainted);
  r.step();
  EXPECT_GT(r.chem(chem::daze), Fx::ratio(9, 10));
  EXPECT_GE(r.c.chemistry().locus[locus::sleep_gate.v], kHalf);
}

// The expression guards are genes.cpp's (unit D3); these are the rules it applies.
TEST(Guards, DazeAndPainNeverReachTheDieLocus) {
  EXPECT_FALSE(receptorExpresses(chem::daze, locus::die));
  EXPECT_FALSE(receptorExpresses(chem::pluck, locus::cause));
  EXPECT_FALSE(receptorExpresses(driveChem(drive::pain), locus::die));
  EXPECT_FALSE(receptorExpresses(chem::dread, locus::die));
  EXPECT_FALSE(reactionExpresses(chem::daze, ChemId{}));
  EXPECT_TRUE(receptorExpresses(chem::daze, locus::sleep_gate));
  EXPECT_TRUE(receptorExpresses(chem::injury, locus::die));
  EXPECT_TRUE(receptorExpresses(kStarvationChem, locus::die));
}

TEST(Guards, PlantedDazeAndPainToDieReceptorsAreInert) {
  GTEST_SKIP() << "unit D3 applies receptorExpresses in genes.cpp's expression; un-skip it there";
  for (ChemId reads : {chem::daze, driveChem(drive::pain)}) {
    const Genome g = withReceptor(duelStarter(), ReceptorGene{reads.v, locus::die.v, 0, 255, 128, 1});
    Rig r(g);
    r.c.setDuelStakes(Stakes::Mortal);
    for (int i = 0; i < 20; ++i) r.fire(i % 2 ? stim::duel_fainted : stim::duel_bonked);
    EXPECT_FALSE(r.c.dead()) << "chem " << int(reads.v);
  }
}

// ---- wounds -------------------------------------------------------------------------------

TEST(Wounds, ASparWoundsNothing) {
  TurnHit hits[kTurns]{};
  for (TurnHit& h : hits) h.hearts = 2;
  for (uint8_t t = 0; t < kTurns; ++t) {
    const TurnWound w = woundFor(Stakes::Spar, hits, t, Fx::one());
    EXPECT_EQ(w.injury, Fx::zero());
    EXPECT_FALSE(w.scar);
  }
}

TEST(Wounds, EachHeartCostsTheTiersInjuryUpToItsCapPerDuel) {
  TurnHit hits[kTurns]{};
  for (TurnHit& h : hits) h.hearts = 1;
  for (Stakes s : {Stakes::Scrap, Stakes::Mortal}) {
    Fx total{};
    for (uint8_t t = 0; t < kTurns; ++t) {
      const TurnWound w = woundFor(s, hits, t, Fx::one());
      if (t == 0) EXPECT_EQ(w.injury, rule(s).injuryPerHeart);
      total += w.injury;
    }
    EXPECT_EQ(total, rule(s).injuryCapPerDuel) << "tier " << int(s);
  }
}

TEST(Wounds, AWoundNeverExceedsTheRoomGiven) {
  TurnHit hits[kTurns]{};
  hits[0].hearts = 3;
  EXPECT_EQ(woundFor(Stakes::Mortal, hits, 0, Fx::ratio(1, 100)).injury, Fx::ratio(1, 100));
  EXPECT_EQ(woundFor(Stakes::Mortal, hits, 0, -Fx::one()).injury, Fx::zero());
}

TEST(Wounds, TheFirstTwoTurnsThatCostHeartsLeaveScarsOfWhatHitHim) {
  TurnHit hits[kTurns]{};
  hits[2] = TurnHit{1, Scar::Kind::Scorch, 0x1234};
  hits[5] = TurnHit{2, Scar::Kind::Ring, 0};
  hits[9] = TurnHit{1, Scar::Kind::Claw, 0};
  std::vector<uint8_t> scarred;
  for (uint8_t t = 0; t < kTurns; ++t)
    if (woundFor(Stakes::Scrap, hits, t, Fx::one()).scar) scarred.push_back(t);
  EXPECT_EQ(scarred, (std::vector<uint8_t>{2, 5}));
  const Scar m = woundFor(Stakes::Scrap, hits, 2, Fx::one()).mark;
  EXPECT_EQ(m.kind, Scar::Kind::Scorch);
  EXPECT_EQ(m.x, 0x34);
  EXPECT_EQ(m.y, 0x12);
}

TEST(Wounds, CreatureWoundIsClampedByWoundRoom) {
  Rig r(duelStarter());
  const Fx room = woundRoom(r.c);
  ASSERT_GT(room, Fx::zero());
  EXPECT_EQ(r.c.wound(Fx::one()), room);
  EXPECT_EQ(woundRoom(r.c), Fx::zero());
  EXPECT_EQ(r.c.wound(Fx::ratio(1, 10)), Fx::zero()) << "no room, no wound";
  EXPECT_EQ(r.c.wound(-Fx::one()), Fx::zero()) << "a wound never heals";
}

TEST(Wounds, TheStartersRoomIsHisLethalLevelLessADropAndTheMargin) {
  // Injury kills the starter past 0.9 (starter_genome.cpp); a drop gives about 0.17.
  Rig r(duelStarter());
  const Fx room = woundRoom(r.c);
  EXPECT_GT(room, Fx::ratio(60, 100));
  EXPECT_LT(room, Fx::ratio(72, 100));
}

// Testing item 8: no sequence of wounds at any tier, from any starting injury,
// plus a drop, kills a starter or any of 1,000 mutated descendants. The twin
// takes the same drop with no wounds, so a death the drop alone causes is not
// the duel's.
TEST(Wounds, NoWoundsPlusADropKillAStarterOrAThousandDescendants) {
  Genome g = duelStarter();
  Rng rng = Rng::seeded(2026);
  int checked = 0;
  for (int gen = 0; gen <= 1000; ++gen) {
    if (gen > 0) g = mutate(g, policyOf(g, Fx::ratio(1, 4)), {}, rng).genome;
    for (Stakes s : {Stakes::Scrap, Stakes::Mortal})
      for (int start : {0, 30, 60, 85}) {
        Rig hurt(g), twin(g);
        for (Rig* r : {&hurt, &twin}) r->c.inject(chem::injury, Fx::ratio(start, 100));
        const Fx room0 = woundRoom(hurt.c), injury0 = hurt.chem(chem::injury);
        TurnHit hits[kTurns]{};
        for (uint8_t t = 0; t < kTurns; ++t) {
          hits[t].hearts = 2;
          hurt.c.wound(woundFor(s, hits, t, woundRoom(hurt.c)).injury);
        }
        ASSERT_LE(hurt.chem(chem::injury), injury0 + room0) << "gen " << gen;
        for (Rig* r : {&hurt, &twin}) {
          r->fire(stim::dropped);
          for (int i = 0; i < 5; ++i) r->step();
        }
        if (!twin.c.dead()) {
          ++checked;
          ASSERT_FALSE(hurt.c.dead()) << "gen " << gen << " tier " << int(s) << " from " << start;
        }
      }
  }
  EXPECT_GT(checked, 4000) << "most cases must survive the drop alone, or the test proves nothing";
}

TEST(Forecast, ASparForecastsOnlyTheWoundsHeAlreadyHas) {
  Rig r(duelStarter());
  EXPECT_EQ(forecast(r.c, Stakes::Spar).skull, 0);
  const Danger scrap = forecast(r.c, Stakes::Scrap), mortal = forecast(r.c, Stakes::Mortal);
  EXPECT_GT(scrap.skull, 0);
  EXPECT_GT(mortal.skull, scrap.skull);
  EXPECT_LT(mortal.skull, 255) << "a fresh pet has room past a mortal duel's cap";
}

TEST(Forecast, AWoundedPetsSkullIsFull) {
  Rig r(duelStarter());
  r.c.inject(chem::injury, Fx::ratio(3, 4));
  EXPECT_EQ(woundRoom(r.c), Fx::zero());
  EXPECT_EQ(forecast(r.c, Stakes::Scrap).skull, 255);
  EXPECT_LT(forecast(r.c, Stakes::Mortal).worstInjury, Fx::ratio(9, 10)) << "the clamp holds the worst short of death";
}

// ---- scars --------------------------------------------------------------------------------

TEST(Scars, AFifthReplacesTheFaintestAndAFainterOneIsRefused) {
  Rig r(duelStarter());
  const uint32_t unscarred = r.c.hash();
  for (uint8_t f : {200, 150, 250, 180}) EXPECT_TRUE(r.c.addScar(Scar{Scar::Kind::Claw, 0, 0, f}));
  EXPECT_NE(r.c.hash(), unscarred) << "a scar is state the replay check sees";
  EXPECT_FALSE(r.c.addScar(Scar{Scar::Kind::Notch, 0, 0, 100}));
  EXPECT_TRUE(r.c.addScar(Scar{Scar::Kind::Ring, 9, 9, 255}));
  ASSERT_EQ(r.c.scars().count, kMaxScars);
  for (const Scar& m : r.c.scars().mark) EXPECT_NE(m.fade, 150) << "the faintest went";
}

// ---- the fall -----------------------------------------------------------------------------

TEST(Fall, AFallRunsTheOrdinaryDeathPathOnce) {
  MemStorage store;
  const Genome g = duelStarter();
  GenomeBuilder quick = GenomeBuilder::from(g);   // a one-minute egg
  g.forEach([&](const GeneView& v) {
    if (v.header.type == GeneKindOf<EggGene>::value) quick.setByte(v.header.uid, 0, 1);
  });
  const Genome founder = *quick.build();
  Dish dish(store, 7, 0x5eedull, DishOptions{nullptr, &founder});
  NullLink link;
  uint32_t ms = 0;
  for (int s = 0; !std::holds_alternative<Creature>(dish.occupant()); ++s) {
    ASSERT_LT(s, 24 * 3600) << "no hatch within a pet day";
    runFor(dish, link, ms, 1000);
  }
  Creature& c = std::get<Creature>(dish.occupant());
  c.fall();
  ASSERT_TRUE(c.dead());
  EXPECT_EQ(c.cause(), DeathCause::Injured);
  c.fall();   // a second Finish is a no-op
  runFor(dish, link, ms, 5000);

  ASSERT_TRUE(std::holds_alternative<Clutch>(dish.occupant())) << "the remains, then the clutch";
  EXPECT_EQ(std::get<Clutch>(dish.occupant()).cause, DeathCause::Injured);
  int deaths = 0;
  dish.lineage().forEach([&](const LineageEntry& e) {
    if (const Death* d = std::get_if<Death>(&e)) {
      ++deaths;
      EXPECT_EQ(d->cause, DeathCause::Injured);
    }
  });
  EXPECT_EQ(deaths, 1);
}

TEST(Stakes, TheDuelRunsAtTheLowerChoice) {
  EXPECT_EQ(agreedStakes(Stakes::Mortal, Stakes::Scrap), Stakes::Scrap);
  EXPECT_EQ(agreedStakes(Stakes::Spar, Stakes::Mortal), Stakes::Spar);
  EXPECT_EQ(agreedStakes(Stakes::Mortal, Stakes::Mortal), Stakes::Mortal);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
