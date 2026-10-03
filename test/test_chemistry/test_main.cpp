#include <gtest/gtest.h>
#include <algorithm>
#include "blorb/chemistry.h"
#include "blorb/creature.h"
#include "blorb/genes.h"

using namespace blorb;

namespace {

constexpr ChemId A{40}, B{41}, C{42}, D{43};
constexpr LocusId kAct{150}, kFree{200};
constexpr uint32_t kDay = 24 * kTicksPerHour;

Emitter emitter(LocusId l, ChemId c, Fx gain, bool digital = true, Fx threshold = Fx::zero()) {
  return Emitter{l, c, threshold, gain, digital, false, false, 0};
}
Receptor receptor(ChemId c, LocusId l, Fx nominal, Fx gain = Fx::zero(), bool digital = true,
                  Fx threshold = Fx::zero()) {
  return Receptor{c, l, threshold, nominal, gain, digital, false};
}

double level(Fx f) { return double(f.raw) / Fx::kOne; }

}  // namespace

// Emitter -> reaction -> decay -> clear -> receptor, all inside one step:
// the act locus equals B after this step's decay, which only that order gives.
TEST(Step, RunsEmittersReactionsDecayClearThenReceptors) {
  ChemRules rules;
  rules.emitters.push_back(emitter(locus::always, A, Fx::ratio(1, 2)));
  rules.reactions.push_back(Reaction{A, ChemId{0}, B, ChemId{0}, 1, 0, 1, 0, Fx::one()});
  rules.decay[B.v] = decayFromByte(1);
  rules.receptors.push_back(receptor(B, kAct, Fx::zero(), Fx::one(), false));
  Chemistry c;
  c.set(locus::always, Fx::one());
  c.set(kAct, Fx::ratio(9, 10));
  c.step(rules, 0);
  Fx decayed = applyDecay(Fx::ratio(1, 2), rules.decay[B.v], 0);
  ASSERT_LT(decayed, Fx::ratio(1, 2));
  EXPECT_EQ(c.chem[A.v], Fx::zero());
  EXPECT_EQ(c.chem[B.v], decayed);
  EXPECT_EQ(c.locus[kAct.v], decayed);
}

TEST(Step, ReceptorsOnOneLocusSumThenClamp) {
  ChemRules rules;
  rules.receptors.push_back(receptor(A, kAct, Fx::ratio(9, 10)));
  rules.receptors.push_back(receptor(A, kAct, Fx::ratio(9, 10)));
  rules.receptors.push_back(receptor(A, kAct, -Fx::ratio(1, 2)));
  rules.receptors.push_back(receptor(A, kFree, -Fx::ratio(1, 2)));
  Chemistry c;
  c.add(A, Fx::ratio(1, 2));
  c.step(rules, 1);
  EXPECT_EQ(c.locus[kAct.v], Fx::one());     // 1.3 -> 1; clamping as it goes would leave 0.5
  EXPECT_EQ(c.locus[kFree.v], Fx::zero());
}

TEST(Step, AnActLocusClearsWhenItsReceptorStops) {
  ChemRules rules;
  rules.receptors.push_back(receptor(A, locus::startle, Fx::one(), Fx::zero(), true, Fx::ratio(1, 2)));
  rules.receptors.push_back(receptor(A, kFree, Fx::one(), Fx::zero(), true, Fx::ratio(1, 2)));
  Chemistry c;
  c.add(A, Fx::ratio(8, 10));
  c.set(locus::light, Fx::ratio(3, 10));
  c.step(rules, 1);
  EXPECT_GT(c.locus[locus::startle.v], Fx::ratio(9, 10));
  EXPECT_GT(c.locus[kFree.v], Fx::ratio(9, 10));
  c.chem[A.v] = Fx::ratio(2, 10);
  c.step(rules, 2);
  EXPECT_EQ(c.locus[locus::startle.v], Fx::zero());
  EXPECT_EQ(c.locus[kFree.v], Fx::zero());
  EXPECT_EQ(c.locus[locus::light.v], Fx::ratio(3, 10));   // sense loci belong to the senses
}

TEST(Step, ReactionsKeepTheirRatios) {
  ChemRules rules;
  rules.reactions.push_back(Reaction{A, B, C, D, 2, 1, 3, 1, Fx::ratio(1, 20)});
  Chemistry c;
  c.add(A, Fx::ratio(6, 10));
  c.add(B, Fx::ratio(5, 10));
  Fx a0 = c.chem[A.v], b0 = c.chem[B.v];
  for (uint32_t t = 0; t < 400; ++t) {
    c.step(rules, t);
    int32_t usedB = b0.raw - c.chem[B.v].raw;
    EXPECT_EQ(a0.raw - c.chem[A.v].raw, 2 * usedB);
    EXPECT_EQ(c.chem[C.v].raw, 3 * usedB);
    EXPECT_EQ(c.chem[D.v].raw, usedB);
  }
  EXPECT_GE(c.chem[A.v], Fx::zero());
  EXPECT_LT(c.chem[A.v], Fx::ratio(1, 100));   // A limits
  c.stepCoarse(rules, 100000, 0);
  int32_t usedB = b0.raw - c.chem[B.v].raw;
  EXPECT_EQ(a0.raw - c.chem[A.v].raw, 2 * usedB);
  EXPECT_EQ(c.chem[D.v].raw, usedB);
  EXPECT_LT(c.chem[A.v].raw, 2);
}

TEST(Step, EmittersHonourThresholdInvertPeriodAndClear) {
  ChemRules rules;
  rules.emitters.push_back(Emitter{locus::light, A, Fx::ratio(3, 10), Fx::ratio(1, 20), false, true, false, 0});
  rules.emitters.push_back(Emitter{locus::always, B, Fx::zero(), Fx::ratio(1, 100), true, false, false, 3});
  rules.emitters.push_back(Emitter{kFree, C, Fx::zero(), Fx::ratio(1, 10), true, false, true, 0});
  Chemistry c;
  c.set(locus::always, Fx::one());
  c.set(locus::light, Fx::ratio(1, 10));
  c.set(kFree, Fx::one());
  for (uint32_t t = 1; t <= 16; ++t) c.step(rules, t);
  EXPECT_NEAR(c.chem[A.v].raw, 16 * (Fx::ratio(1, 20) * Fx::ratio(2, 10)).raw, 16);
  EXPECT_EQ(c.chem[B.v], Fx::ratio(1, 100) + Fx::ratio(1, 100));   // ticks 8 and 16 only
  EXPECT_EQ(c.chem[C.v], Fx::ratio(1, 10));                        // the clear made it one-shot
}

TEST(Chemistry, ChemZeroIsNoneAndAddClamps) {
  Chemistry c;
  c.add(ChemId{0}, Fx::one());
  EXPECT_EQ(c.chem[0], Fx::zero());
  c.add(A, Fx{5 * Fx::kOne});
  EXPECT_EQ(c.chem[A.v], Fx::one());
  c.add(A, -Fx{5 * Fx::kOne});
  EXPECT_EQ(c.chem[A.v], Fx::zero());
}

// ---- the starter genome, coarse and fine -------------------------------------

namespace {

constexpr uint32_t kStride = 5 * kTicksPerMinute;
constexpr uint32_t kFeedEvery = 2 * kTicksPerHour;

bool night(uint32_t tick) { return (tick / kTicksPerHour) % 24 >= 16; }

DeathCause causeOf(Fx level) {
  int code = std::min(15, (level.raw + (Fx::kOne >> 5)) >> (Fx::kFrac - 4));
  for (int bit = 3; bit >= 0; --bit)
    if (code & (1 << bit)) return DeathCause(bit);
  return DeathCause::Unknown;
}

struct Pet {
  Genome genome = starterGenome(7);
  Phenotype p{};
  Chemistry c;
  Stage stage = Stage::Baby;

  Pet() { express(Stage::Baby); }
  void express(Stage s) {
    size_t from = p.chem.seeds.size();
    expressStage(genome, s, 0, p);
    for (size_t i = from; i < p.chem.seeds.size(); ++i) c.chem[p.chem.seeds[i].chem.v] = p.chem.seeds[i].level;
    stage = s;
  }
  void senses(uint32_t tick) {
    c.set(locus::always, Fx::one());
    c.set(locus::light, night(tick) ? Fx::zero() : Fx::one());
  }
  void stimulate(StimId s) {
    for (const auto& r : p.stimuli)
      if (r.stim == s)
        for (int i = 0; i < 3; ++i) c.add(r.chem[i], r.amount[i]);
  }
  Fx injuryDeathThreshold() const {
    for (const Receptor& r : p.chem.receptors)
      if (r.chem == chem::injury && r.locus == locus::die) return r.threshold;
    return Fx::zero();
  }
};

struct Lifespan {
  double child = -1, adult = -1, elder = -1, died = -1;
  DeathCause cause = DeathCause::Unknown;
  Fx maxInjury{};
};

double days(uint32_t tick) { return double(tick) / kDay; }

// What the creature does with the act loci: each stage locus past 0.5
// expresses the next stage once; the die locus past 0.5 ends the life.
Lifespan live(bool fed) {
  Pet pet;
  Lifespan run;
  for (uint32_t tick = 0; tick < 14 * kDay; tick += kStride) {
    if (fed && tick % kFeedEvery == 0) pet.c.add(chem::food, pet.p.habitat.biteSize);
    pet.senses(tick);
    pet.c.stepCoarse(pet.p.chem, kStride, tick);
    uint32_t now = tick + kStride;
    run.maxInjury = fxMax(run.maxInjury, pet.c.chem[chem::injury.v]);
    const Fx* l = pet.c.locus;
    if (pet.stage == Stage::Baby && l[locus::become_child.v] >= Fx::ratio(1, 2)) {
      pet.express(Stage::Child), run.child = days(now);
    }
    if (pet.stage == Stage::Child && l[locus::become_adult.v] >= Fx::ratio(1, 2)) {
      pet.express(Stage::Adult), run.adult = days(now);
    }
    if (pet.stage == Stage::Adult && l[locus::become_elder.v] >= Fx::ratio(1, 2)) {
      pet.express(Stage::Elder), run.elder = days(now);
    }
    if (l[locus::die.v] >= Fx::ratio(1, 2)) {
      run.died = days(now);
      run.cause = causeOf(l[locus::cause.v]);
      break;
    }
  }
  return run;
}

}  // namespace

TEST(Life, FedEveryTwoHoursHeLivesAboutTenDaysAndDiesOfOldAge) {
  Lifespan run = live(true);
  EXPECT_NEAR(run.child, 0.5, 0.2);
  EXPECT_NEAR(run.adult, 2.0, 0.3);
  EXPECT_NEAR(run.elder, 7.5, 0.5);
  EXPECT_TRUE(run.child < run.adult && run.adult < run.elder && run.elder < run.died);
  EXPECT_GE(run.died, 8.5);
  EXPECT_LE(run.died, 11.5);
  EXPECT_EQ(run.cause, DeathCause::OldAge);
  EXPECT_LT(run.maxInjury, Fx::ratio(5, 100));
}

// The product default (kNeglectCanKill): sustained neglect is an illness that
// ends the life within a few pet days, and the cause says so.
TEST(Life, NeverFedHeDiesOfNeglectWithinFourDays) {
  constexpr double kStarvedDeathBy = 4.0;
  Lifespan run = live(false);
  ASSERT_GT(run.died, 0);
  EXPECT_GT(run.died, 1.0);
  EXPECT_LT(run.died, kStarvedDeathBy);
  EXPECT_EQ(run.cause, DeathCause::Starved);
  EXPECT_LT(run.maxInjury, Pet{}.injuryDeathThreshold());
  EXPECT_GT(run.maxInjury, Fx::ratio(2, 10));   // he was ill before he died
}

TEST(Life, TwoShakesTwoSecondsApartGiveOneStartle) {
  Pet pet;
  uint32_t edges = 0;
  Fx prev{}, peak{};
  for (uint32_t tick = 1; tick <= 120 * 10; ++tick) {
    if (tick == 100 || tick == 120 || tick == 1000) pet.stimulate(stim::shake);
    pet.senses(0);
    pet.c.step(pet.p.chem, tick);
    Fx startle = pet.c.locus[locus::startle.v];
    if (tick < 1000) {
      edges += prev <= Fx::ratio(1, 2) && startle > Fx::ratio(1, 2);
      peak = fxMax(peak, startle);
    }
    if (tick == 1000) { EXPECT_LE(prev, Fx::ratio(1, 2)) << "rattled for 90 s"; }
    if (tick == 1001) { EXPECT_GT(startle, Fx::ratio(1, 2)) << "a shake once calm hops again"; }
    prev = startle;
  }
  EXPECT_EQ(edges, 1u);
  EXPECT_GT(peak, Fx::ratio(6, 10));
}

TEST(Life, AKnockFlinchesAndForeseeingGlows) {
  Pet pet;
  pet.stimulate(stim::knock);
  pet.senses(0);
  pet.c.step(pet.p.chem, 1);
  EXPECT_GT(pet.c.locus[locus::flinch.v], Fx::ratio(1, 2));
  EXPECT_LT(pet.c.locus[locus::startle.v], Fx::ratio(1, 2));
  for (uint32_t t = 2; t < 40; ++t) pet.senses(0), pet.c.step(pet.p.chem, t);
  EXPECT_EQ(pet.c.locus[locus::flinch.v], Fx::zero());
  for (uint32_t t = 40; t < 60; ++t) {
    pet.senses(0);
    pet.c.set(locus::foreseeing, Fx::one());
    pet.c.step(pet.p.chem, t);
  }
  EXPECT_GT(pet.c.locus[locus::glow.v], Fx::ratio(6, 10));
}

// Every chemical of a real day, coarse against fine, at the 5-minute stride
// viability's dry run uses. A reaction converts a whole stride's input at
// once, before the reagent's own decay takes its share, so a product can
// run ahead by one stride's loss: under 0.03 for melatonin -> sleepiness.
TEST(Coarse, ADayOfTheStarterGenomeMatchesStepping) {
  Pet fine, coarse;
  for (uint32_t tick = 0; tick < kDay; ++tick) {
    if (tick % kFeedEvery == 0) fine.c.add(chem::food, fine.p.habitat.biteSize);
    fine.senses(tick);
    fine.c.step(fine.p.chem, tick);
  }
  for (uint32_t tick = 0; tick < kDay; tick += kStride) {
    if (tick % kFeedEvery == 0) coarse.c.add(chem::food, coarse.p.habitat.biteSize);
    coarse.senses(tick);
    coarse.c.stepCoarse(coarse.p.chem, kStride, tick);
  }
  for (uint16_t i = 1; i < kChemSlots; ++i)
    EXPECT_NEAR(level(coarse.c.chem[i]), level(fine.c.chem[i]), 0.03) << "chem " << i;
  EXPECT_NEAR(level(coarse.c.chem[chem::life.v]), level(fine.c.chem[chem::life.v]), 0.0005);
}

TEST(Coarse, ATonicDriveSettlesWhereSteppingDoes) {
  ChemRules rules;
  rules.decay[A.v] = decayFromByte(131);   // about 30 minutes
  rules.emitters.push_back(emitter(locus::always, A, Fx::ratio(1, 40000)));
  Chemistry fine, coarse;
  fine.set(locus::always, Fx::one());
  coarse.set(locus::always, Fx::one());
  for (uint32_t t = 0; t < 6 * kTicksPerHour; ++t) fine.step(rules, t);
  for (uint32_t t = 0; t < 6 * kTicksPerHour; t += kStride) coarse.stepCoarse(rules, kStride, t);
  EXPECT_NEAR(level(coarse.chem[A.v]), level(fine.chem[A.v]), 0.004);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
