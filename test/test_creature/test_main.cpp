#include <gtest/gtest.h>
#include <initializer_list>
#include <utility>
#include <vector>
#include "blorb/creature.h"

using namespace blorb;

namespace {

constexpr Fx kHalf = Fx::ratio(1, 2);

Creature hatchOf(Genome g) {
  Egg egg(Offspring{std::move(g), {}}, 0, 0);
  return Creature::hatch(egg, 0, 0);
}

// The Dish's part around one creature: the pet clock, the day detector and the
// habitat, with the dish held level and no hand on it.
struct Rig {
  Creature c;
  Habitat habitat;
  Behaviours behaviours;
  PetClock clock;
  DayDetector day;
  uint32_t tick = 0;

  explicit Rig(Genome g = starterGenome(7)) : c(hatchOf(std::move(g))) {
    habitat.pantry = c.phenotype().habitat.pantrySize;
  }
  void step(SenseOut s = {}) {
    clock.advance(false);
    day.tick(TickContext{clock, false, tick}, s);
    s.set(locus::tilt_x, kHalf);
    s.set(locus::tilt_y, kHalf);
    habitat.step(c.phenotype().habitat, kHalf, kHalf, c.body().at, tick, s);
    c.tick(s, habitat, behaviours, tick);
    ++tick;
  }
  void run(uint32_t ticks) {
    for (uint32_t i = 0; i < ticks; ++i) step();
  }
  void fire(StimId id) {
    SenseOut s;
    s.fire(id);
    step(s);
  }
  Fx locus(LocusId l) const { return c.chemistry().locus[l.v]; }
};

// The starter's receptor from adrenaline onto the startle locus; its gain is hop height.
Genome withStartleGain(int scale) {
  Genome g = starterGenome(7);
  GenomeBuilder b = GenomeBuilder::from(g);
  g.forEach([&](const GeneView& v) {
    if (v.header.type == GeneKindOf<ReceptorGene>::value && v.body[0] == chem::adrenaline.v &&
        v.body[1] == locus::startle.v)
      b.setByte(v.header.uid, 4, uint8_t(128 + scale * (v.body[4] - 128)));
  });
  return *b.build();
}

}  // namespace

TEST(Reflex, AShakeHopsWithTheAlarmedFace) {
  Rig r;
  r.run(50);
  r.fire(stim::shake);
  const Body& b = r.c.body();
  ASSERT_TRUE(b.reflex.has_value());
  EXPECT_EQ(b.reflex->kind, reflex::hop);
  EXPECT_GT(b.reflex->strength, Fx::zero());
  EXPECT_EQ(b.pose, pose::hop);
  EXPECT_EQ(r.c.face().current, expr::alarmed);
  r.run(REFLEXES[reflex::hop.v].ticks);
  EXPECT_FALSE(r.c.body().reflex.has_value()) << "the hop hands the body back";
  EXPECT_EQ(r.c.stats().hops, 1u);
  EXPECT_EQ(r.c.stats().shaken, 1u);
}

TEST(Reflex, ASecondShakeWhileRattledGivesNoSecondHop) {
  Rig r;
  r.run(50);
  r.fire(stim::shake);
  r.run(20);
  ASSERT_FALSE(r.c.body().reflex.has_value());
  r.fire(stim::shake);
  EXPECT_FALSE(r.c.body().reflex.has_value());
  r.run(20);
  EXPECT_EQ(r.c.stats().hops, 1u);
  EXPECT_EQ(r.c.stats().shaken, 2u);
  r.run(90 * 10);
  r.fire(stim::shake);
  EXPECT_TRUE(r.c.body().reflex.has_value()) << "calm again, he hops again";
  EXPECT_EQ(r.c.stats().hops, 2u);
}

TEST(Reflex, DoubledStartleGainHopsHigher) {
  Rig plain, bouncy(withStartleGain(2));
  plain.run(50);
  bouncy.run(50);
  plain.fire(stim::shake);
  bouncy.fire(stim::shake);
  ASSERT_TRUE(plain.c.body().reflex && bouncy.c.body().reflex);
  EXPECT_GT(bouncy.c.body().reflex->strength, plain.c.body().reflex->strength + Fx::ratio(2, 10));
}

TEST(Reflex, AKnockFlinches) {
  Rig r;
  r.run(50);
  r.fire(stim::knock);
  ASSERT_TRUE(r.c.body().reflex.has_value());
  EXPECT_EQ(r.c.body().reflex->kind, reflex::flinch);
  EXPECT_EQ(r.c.body().pose, pose::curl);
  EXPECT_EQ(r.c.stats().hops, 0u);
}

// Ask 7: nobody forces it. Within a pet morning he chooses to scry, and the
// glow locus his genes drive from it climbs while he does.
TEST(Foresee, HeForeseesOnHisOwnAndHisGlowRises) {
  constexpr uint32_t kWithin = 6 * kTicksPerHour;
  Rig r;
  while (r.tick < kWithin && r.c.action() != action::foresee) r.step();
  ASSERT_EQ(r.c.action(), action::foresee) << "no foresight in " << kWithin / kTicksPerHour << " pet hours";
  Fx atStart = r.locus(locus::glow), peak = atStart;
  for (int i = 0; i < 30 && r.c.action() == action::foresee; ++i) {
    r.step();
    peak = fxMax(peak, r.locus(locus::glow));
  }
  EXPECT_GT(peak, atStart + Fx::ratio(3, 10));
  EXPECT_GE(peak, Fx::ratio(6, 10));
  EXPECT_EQ(r.c.body().pose, pose::foresee);
  EXPECT_GE(r.c.stats().foresights, 1u);
}

TEST(Foresee, ForceHoldsTheActionForItsMinTicks) {
  Rig r;
  r.run(20);
  r.c.force(action::foresee);
  r.step();
  ASSERT_EQ(r.c.action(), action::foresee);
  uint32_t held = 1;
  while (r.c.action() == action::foresee && held < 1000) r.step(), ++held;
  EXPECT_GE(held, ACTIONS[action::foresee.v].minTicks);
  EXPECT_GT(r.locus(locus::glow), Fx::zero());
}

TEST(Lifecycle, StagesAdvanceAsLifeFallsEachExpressedOnce) {
  Rig r;
  r.run(10);
  ASSERT_EQ(r.c.stage(), Stage::Baby);
  struct Step { Fx life; Stage want; };
  const Step steps[] = {{Fx::ratio(85, 100), Stage::Child}, {Fx::ratio(60, 100), Stage::Adult},
                        {Fx::ratio(15, 100), Stage::Elder}};
  for (const Step& s : steps) {
    r.c.inject(chem::life, s.life);
    r.run(3);
    EXPECT_EQ(r.c.stage(), s.want);
    EXPECT_EQ(r.c.stats().reached, s.want);
    size_t instincts = r.c.phenotype().instincts.size(), faces = r.c.phenotype().faces.size();
    uint8_t stages = r.c.phenotype().expressedStages;
    EXPECT_EQ(stages, uint8_t((2u << uint8_t(s.want)) - 1));
    r.run(50);
    EXPECT_EQ(r.c.phenotype().instincts.size(), instincts) << "a stage expresses once";
    EXPECT_EQ(r.c.phenotype().faces.size(), faces);
    EXPECT_FALSE(r.c.dead());
  }
  EXPECT_EQ(r.c.phenotype().temperament.learnRate, Fx::unitByte(90)) << "the Elder temperament gene took over";
}

TEST(Lifecycle, LowLifeDiesOfOldAgeAndTheTickStops) {
  Rig r;
  r.run(10);
  r.c.inject(chem::life, Fx::ratio(5, 100));
  r.step();
  ASSERT_TRUE(r.c.dead());
  EXPECT_EQ(r.c.cause(), DeathCause::OldAge);
  uint32_t h = r.c.hash();
  r.run(20);
  EXPECT_EQ(r.c.hash(), h) << "a dead creature does not tick";
}

namespace {

// The starter plus a poison death: toxin past 0.9 writes Poisoned's code and dies.
Genome withPoisonDeath() {
  constexpr uint8_t kPoisonedCode = 128 + 8 * 8;   // 8/16 as a signed byte
  GenomeBuilder b = GenomeBuilder::from(starterGenome(7));
  b.append(ReceptorGene{chem::toxin.v, locus::cause.v, 230, kPoisonedCode, 128, 1}, GeneFlags::Mutable);
  b.append(ReceptorGene{chem::toxin.v, locus::die.v, 230, 255, 128, 1}, GeneFlags::Mutable);
  return *b.build();
}

// The chemical a cause receptor reads, at the level that fires it.
struct Trigger { ChemId chem; Fx level; };
std::vector<Trigger> causeTriggers(const Phenotype& p) {
  std::vector<Trigger> out;
  for (const Receptor& r : p.chem.receptors)
    if (r.locus == locus::cause) out.push_back({r.chem, r.invert ? Fx::zero() : Fx::one()});
  return out;
}

ChemId starvationChem(const Phenotype& p) {
  for (const Trigger& t : causeTriggers(p))
    if (t.chem != chem::life && t.chem != chem::injury && t.chem != chem::toxin) return t.chem;
  return ChemId{0};
}

DeathCause dieOf(std::initializer_list<Trigger> triggers) {
  Rig r(withPoisonDeath());
  r.run(10);
  for (const Trigger& t : triggers) r.c.inject(t.chem, t.level);
  r.step();
  return r.c.dead() ? r.c.cause() : DeathCause::Unknown;
}

}  // namespace

TEST(Lifecycle, EachCauseAloneDecodesToItself) {
  Rig r(withPoisonDeath());
  ASSERT_EQ(causeTriggers(r.c.phenotype()).size(), 4u);
  ChemId starving = starvationChem(r.c.phenotype());
  ASSERT_NE(starving.v, 0);
  EXPECT_EQ(dieOf({{chem::life, Fx::ratio(5, 100)}}), DeathCause::OldAge);
  EXPECT_EQ(dieOf({{starving, Fx::one()}}), DeathCause::Starved);
  EXPECT_EQ(dieOf({{chem::injury, Fx::one()}}), DeathCause::Injured);
  EXPECT_EQ(dieOf({{chem::toxin, Fx::one()}}), DeathCause::Poisoned);
}

// Causes that fire together sum on one locus; the sum must still name the
// most serious one, not a third cause.
TEST(Lifecycle, OldAndStarvingDiesStarvedAndWorseCausesWin) {
  ChemId starving = starvationChem(Rig(withPoisonDeath()).c.phenotype());
  const Trigger old{chem::life, Fx::ratio(5, 100)}, starved{starving, Fx::one()}, injured{chem::injury, Fx::one()},
      poisoned{chem::toxin, Fx::one()};
  EXPECT_EQ(dieOf({old, starved}), DeathCause::Starved);
  EXPECT_EQ(dieOf({old, injured}), DeathCause::Injured);
  EXPECT_EQ(dieOf({starved, injured}), DeathCause::Injured);
  EXPECT_EQ(dieOf({old, starved, injured, poisoned}), DeathCause::Poisoned);
}

TEST(Replay, SameGenomeAndInputsGiveTheSameHash) {
  Rig a, b;
  for (int i = 0; i < 3000; ++i) {
    SenseOut s;
    if (i % 700 == 300) s.fire(stim::shake);
    if (i % 500 == 100) s.fire(stim::knock);
    a.step(s);
    b.step(s);
  }
  EXPECT_EQ(a.c.hash(), b.c.hash());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
