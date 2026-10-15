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
constexpr Stakes kTiers[] = {Stakes::Spar, Stakes::Scrap};
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
  for (ChemId reads : {chem::daze, driveChem(drive::pain)}) {
    const Genome g = withReceptor(duelStarter(), ReceptorGene{reads.v, locus::die.v, 0, 255, 128, 1});
    Rig r(g);
    r.c.setDuelStakes(Stakes::Scrap);
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
  for (Stakes s : {Stakes::Scrap}) {
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
  EXPECT_EQ(woundFor(Stakes::Scrap, hits, 0, Fx::ratio(1, 100)).injury, Fx::ratio(1, 100));
  EXPECT_EQ(woundFor(Stakes::Scrap, hits, 0, -Fx::one()).injury, Fx::zero());
}

TEST(Wounds, TheFirstTwoTurnsThatCostHeartsLeaveScarsOfWhatHitHim) {
  TurnHit hits[kTurns]{};
  hits[2] = TurnHit{1, Scar::Kind::Scorch, 0x1234};
  hits[5] = TurnHit{2, Scar::Kind::Bite, 0};
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
    for (Stakes s : {Stakes::Scrap})
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
  const Danger scrap = forecast(r.c, Stakes::Scrap);
  EXPECT_GT(scrap.skull, 0);
  EXPECT_LT(scrap.skull, 255) << "a fresh pet has room past a scrap's cap";
}

TEST(Forecast, AWoundedPetsSkullIsFull) {
  Rig r(duelStarter());
  r.c.inject(chem::injury, Fx::ratio(3, 4));
  EXPECT_EQ(woundRoom(r.c), Fx::zero());
  EXPECT_EQ(forecast(r.c, Stakes::Scrap).skull, 255);
  EXPECT_LT(forecast(r.c, Stakes::Scrap).worstInjury, Fx::ratio(9, 10)) << "the clamp holds the worst short of death";
}

// ---- scars --------------------------------------------------------------------------------

TEST(Scars, AFifthReplacesTheFaintestAndAFainterOneIsRefused) {
  Rig r(duelStarter());
  const uint32_t unscarred = r.c.hash();
  for (uint8_t f : {200, 150, 250, 180}) EXPECT_TRUE(r.c.addScar(Scar{Scar::Kind::Claw, 0, 0, f}));
  EXPECT_NE(r.c.hash(), unscarred) << "a scar is state the replay check sees";
  EXPECT_FALSE(r.c.addScar(Scar{Scar::Kind::Notch, 0, 0, 100}));
  EXPECT_TRUE(r.c.addScar(Scar{Scar::Kind::Bite, 9, 9, 255}));
  ASSERT_EQ(r.c.scars().count, kMaxScars);
  for (const Scar& m : r.c.scars().mark) EXPECT_NE(m.fade, 150) << "the faintest went";
}

// ---- after the duel: how he dies, if he dies -------------------------------------------
// The owner: "Death should not be a choice" (2026-10-15), and "Close to dead
// + time = dead" (2026-10-14). No duel kills him. His wounds heal on their
// own and cost him a little of his life; what ends it is what ends any pet's:
// neglect, or old age.

namespace {

constexpr uint32_t kStride = 5 * kTicksPerMinute;
constexpr uint32_t kFeedEvery = 2 * kTicksPerHour;
constexpr uint32_t kTicksPerDay = 24 * kTicksPerHour;

// creature.cpp's reading of the cause locus: the highest bit set.
DeathCause causeOf(Fx level) {
  int code = std::min(15, (level.raw + (Fx::kOne >> 5)) >> (Fx::kFrac - 4));
  for (int bit = 3; bit >= 0; --bit)
    if (code & (1 << bit)) return DeathCause(bit);
  return DeathCause::Unknown;
}

struct Aftermath {
  double died = -1;   // pet days after the duel, or -1: alive at the end
  DeathCause cause = DeathCause::Unknown;
  Fx injuryAfterADay{};
};

// His own chemistry from where the duel left him, in the five-minute strides
// the catch-up uses, for `days`: fed every two hours, or never.
Aftermath afterTheDuel(const Creature& c, bool fed, uint32_t days) {
  Chemistry ch = c.chemistry();
  const Phenotype& p = c.phenotype();
  Aftermath out;
  for (uint32_t tick = 0; tick < days * kTicksPerDay; tick += kStride) {
    if (fed && tick % kFeedEvery == 0) ch.add(chem::food, p.habitat.biteSize);
    ch.set(locus::always, Fx::one());
    ch.set(locus::light, (tick / kTicksPerHour) % 24 >= 16 ? Fx::zero() : Fx::one());
    ch.stepCoarse(p.chem, kStride, tick);
    if (tick + kStride == kTicksPerDay) out.injuryAfterADay = ch.chem[chem::injury.v];
    if (ch.locus[locus::die.v] >= Fx::ratio(1, 2)) {
      out.died = double(tick + kStride) / kTicksPerDay;
      out.cause = causeOf(ch.locus[locus::cause.v]);
      break;
    }
  }
  return out;
}

// Scraps lost heart after heart until the clamp leaves no more room: as near
// death as any duel can bring him, and still alive.
void beatenToTheClamp(Rig& r) {
  TurnHit hits[kTurns]{};
  for (TurnHit& h : hits) h.hearts = 1;
  for (int duel = 0; duel < 8 && woundRoom(r.c) > Fx::zero(); ++duel)
    for (uint8_t t = 0; t < kTurns; ++t) r.c.wound(woundFor(Stakes::Scrap, hits, t, woundRoom(r.c)).injury);
}

}  // namespace

TEST(AfterTheDuel, AsNearDeathAsADuelLeavesHimHeIsStillAlive) {
  Rig r(duelStarter());
  beatenToTheClamp(r);
  ASSERT_EQ(woundRoom(r.c), Fx::zero());
  EXPECT_GT(r.chem(chem::injury), Fx::ratio(1, 2)) << "badly hurt";
  for (int i = 0; i < 10; ++i) r.step();
  EXPECT_FALSE(r.c.dead()) << "the duel itself never kills";
}

TEST(AfterTheDuel, CaredForHeHeals) {
  Rig r(duelStarter());
  beatenToTheClamp(r);
  const Fx hurt = r.chem(chem::injury);
  const Aftermath fed = afterTheDuel(r.c, true, 3);
  EXPECT_LT(fed.died, 0) << "fed, he lives through the next three days";
  EXPECT_LT(fed.injuryAfterADay, hurt * Fx::ratio(1, 4)) << "and his wounds close";
}

// Wounds heal on their own within the day, fed or not, so what kills a
// beaten pet nobody looks after is what kills any pet nobody looks after.
TEST(AfterTheDuel, LeftUntendedHeDiesOfNeglectAsAnyPetWould) {
  Rig hurt(duelStarter()), whole(duelStarter());
  beatenToTheClamp(hurt);
  const Aftermath wounded = afterTheDuel(hurt.c, false, 6), neglected = afterTheDuel(whole.c, false, 6);
  ASSERT_GT(wounded.died, 0) << "nobody fed him: he dies";
  ASSERT_GT(neglected.died, 0);
  EXPECT_EQ(wounded.cause, DeathCause::Starved) << "of neglect, not of the duel";
  EXPECT_EQ(neglected.cause, DeathCause::Starved);
  EXPECT_LE(wounded.died, neglected.died) << "the duel never buys him time";
}

// Injury speeds his ageing while it lasts, so a hard life is a shorter one:
// fed, the beaten pet still dies of old age, a little sooner (measured
// 2026-10-15: 9.6 pet days against 10.0).
TEST(AfterTheDuel, CaredForHeStillDiesOfOldAgeALittleSooner) {
  Rig hurt(duelStarter()), whole(duelStarter());
  beatenToTheClamp(hurt);
  const Aftermath beaten = afterTheDuel(hurt.c, true, 14), spared = afterTheDuel(whole.c, true, 14);
  ASSERT_GT(beaten.died, 0);
  ASSERT_GT(spared.died, 0);
  EXPECT_EQ(beaten.cause, DeathCause::OldAge);
  EXPECT_EQ(spared.cause, DeathCause::OldAge);
  EXPECT_LT(beaten.died, spared.died);
}

TEST(Stakes, TheDuelRunsAtTheLowerChoice) {
  EXPECT_EQ(agreedStakes(Stakes::Spar, Stakes::Scrap), Stakes::Spar);
  EXPECT_EQ(agreedStakes(Stakes::Scrap, Stakes::Spar), Stakes::Spar);
  EXPECT_EQ(agreedStakes(Stakes::Scrap, Stakes::Scrap), Stakes::Scrap);
}

// 0.7.0's Mortal byte is a Scrap here, on the air and in duels.log; past it is refused.
TEST(Stakes, A070MortalByteReadsAsAScrap) {
  Stakes s = Stakes::Spar;
  EXPECT_TRUE(stakesFromByte(0, s) && s == Stakes::Spar);
  EXPECT_TRUE(stakesFromByte(1, s) && s == Stakes::Scrap);
  EXPECT_TRUE(stakesFromByte(kLegacyMortal, s) && s == Stakes::Scrap);
  EXPECT_FALSE(stakesFromByte(3, s));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
