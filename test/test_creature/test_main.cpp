#include <gtest/gtest.h>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>
#include "blorb/creature.h"

using namespace blorb;

namespace {

constexpr Fx kHalf = Fx::ratio(1, 2);

Creature hatchOf(Genome g) {
  Egg egg(Offspring{std::move(g), {}}, 0, 0);
  return Creature(egg, 0, 0);
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

// The bite used to end Eat at once, so eating showed for one 100 ms tick
// and no frame could catch him at it.
TEST(Eat, HeChewsTheBiteForASecondAndAHalfThenStops) {
  Rig r;
  r.habitat.pellets[0] = Pellet{r.c.body().at, 0, true};
  r.c.force(action::eat);
  int eating = 0, chewingPose = 0;
  for (int i = 0; i < 40; ++i) {
    r.step();
    eating += r.locus(locus::eating) >= kHalf;
    chewingPose += r.c.body().pose == pose::eat;
  }
  EXPECT_FALSE(r.habitat.pellets[0].present) << "he bit it";
  EXPECT_GE(eating, 14) << "ticks with the eating locus up";
  EXPECT_LE(eating, 16) << "and then he stops";
  EXPECT_GE(chewingPose, 14);
}

// Eat decided again mid-chew (a forced eat, say) used to send him walking
// with nothing left to walk to, so the mouthful ended there.
TEST(Eat, DecidingToEatAgainMidChewFinishesTheMouthful) {
  Rig r;
  r.habitat.pellets[0] = Pellet{r.c.body().at, 0, true};
  r.c.force(action::eat);
  int eating = 0;
  for (int i = 0; i < 40; ++i) {
    if (i == 6) r.c.force(action::eat);
    r.step();
    eating += r.locus(locus::eating) >= kHalf;
  }
  EXPECT_GE(eating, 14) << "ticks with the eating locus up";
}

// A shake mid-chew hops him out of the eat pose: the chew is over and the bite
// with it, so no pellet rides through the hop or the walk that follows.
TEST(Eat, AHopMidChewEndsTheMouthful) {
  Rig r;
  r.habitat.pellets[0] = Pellet{r.c.body().at, 0, true};
  r.c.force(action::eat);
  r.run(3);
  ASSERT_EQ(r.c.body().mouth, Mouthful::Pellet) << "he bit a fresh one";
  r.fire(stim::shake);
  ASSERT_EQ(r.c.body().pose, pose::hop);
  EXPECT_EQ(r.c.body().mouth, Mouthful::Nothing);
  for (int i = 0; i < 30; ++i) {
    r.step();
    EXPECT_EQ(r.c.body().mouth, Mouthful::Nothing) << "tick " << i << " after the hop began";
  }
}

// He used to walk onto the marble and stand on it for good, so every frame
// after a chase showed it between his feet.
TEST(Chase, HeNosesTheMarbleOnAndNeverStandsOnIt) {
  Rig r;
  r.habitat.marble.at = DishPos{Fx::ratio(3, 10), Fx::zero()};
  int under = 0, touches = 0;
  bool wasTouching = false;
  for (int i = 0; i < 400; ++i) {
    r.c.force(action::chase);
    r.step();
    Fx dx = r.habitat.marble.at.x - r.c.body().at.x, dy = r.habitat.marble.at.y - r.c.body().at.y;
    Fx d2 = dx * dx + dy * dy;
    under += d2 <= Fx::ratio(36, 10000);
    bool touching = d2 <= Fx::ratio(144, 10000);
    touches += touching && !wasTouching;
    wasTouching = touching;
  }
  EXPECT_GE(touches, 3) << "he keeps catching it up";
  EXPECT_LE(under, 20) << "ticks of 400 with the marble within 0.06 of his feet";
}

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

namespace {

const ChemId kSleepiness = driveChem(drive::sleepiness);

double toDouble(Fx v) { return double(v.raw) / Fx::kOne; }

// Sleepiness held high opens the sleep gate; the forced Sleep then holds.
void putToSleep(Rig& r) {
  for (int i = 0; i < 600 && r.locus(locus::sleep_gate) < kHalf; ++i) {
    r.c.inject(kSleepiness, Fx::one());
    r.step();
  }
  r.c.force(action::sleep);
  r.step();
}

// Asleep for `ticks` more with the gate held open; let go, it closes and he
// wakes. Returns the ticks he slept.
uint32_t sleepFor(Rig& r, uint32_t ticks) {
  putToSleep(r);
  uint32_t asleep = 1;
  for (uint32_t i = 0; i < ticks; ++i) {
    r.c.inject(kSleepiness, Fx::one());
    r.step();
    asleep += r.c.body().asleep;
  }
  r.c.inject(kSleepiness, Fx::zero());
  for (int i = 0; i < 50 && r.c.body().asleep; ++i, ++asleep) r.step();
  return asleep;
}

}  // namespace

TEST(Sleep, WakingFadesEveryBeliefByTheSameFractionForTheDreamsSlept) {
  Rig r;
  const Fx big = Fx::ratio(1, 2), small = Fx::ratio(5, 100);
  ASSERT_TRUE(r.c.prophesy(locus::upside_down, action::rest, drive::hunger, big));
  ASSERT_TRUE(r.c.prophesy(locus::upside_down, action::wander, drive::hunger, small));
  uint32_t asleep = sleepFor(r, 2 * 36000);
  ASSERT_FALSE(r.c.body().asleep);
  ASSERT_GE(asleep, 2u * 36000);

  const Temperament& t = r.c.phenotype().temperament;
  double rate = double(t.forgetRate.raw) / Fx::kOne / 2048;
  double keep = std::pow(1.0 - rate, double(asleep / t.dreamEveryTicks));
  ASSERT_LT(keep, 0.98) << "a night long enough to forget something";
  auto now = [&](ActionId a) { return double(r.c.brain().predict(locus::upside_down, a, drive::hunger).raw) / Fx::kOne; };
  const double lsb = 1.0 / 32768;
  EXPECT_NEAR(now(action::rest), 0.5 * keep, 1.5 * lsb);
  EXPECT_NEAR(now(action::wander), 0.05 * keep, 1.5 * lsb) << "a small belief fades by the same fraction";
}

// The catch-up runs no brain, so hours unplugged asleep are not dreams.
TEST(Sleep, AnUnpluggedGapAsleepForgetsNothing) {
  Rig r;
  const Fx belief = Fx::ratio(1, 2);
  ASSERT_TRUE(r.c.prophesy(locus::upside_down, action::rest, drive::hunger, belief));
  putToSleep(r);
  ASSERT_TRUE(r.c.body().asleep);
  r.c.tickCoarse(SenseOut{}, 8 * 36000, r.tick);
  for (int i = 0; i < 50 && r.c.body().asleep; ++i) r.step();
  ASSERT_FALSE(r.c.body().asleep);
  EXPECT_EQ(r.c.brain().predict(locus::upside_down, action::rest, drive::hunger), belief);
}

// Experience wore food_near -> eat -> hunger from -0.56 to about 0 over a
// life, and adults stood on a pellet without eating it.
TEST(Sleep, AnInstinctWornAwayIsPartlyRestoredOvernight) {
  Rig r;
  ASSERT_TRUE(r.c.prophesy(locus::food_near, action::eat, drive::hunger, Fx::zero()));
  sleepFor(r, 36000);
  double eat = toDouble(r.c.brain().predict(locus::food_near, action::eat, drive::hunger));
  EXPECT_LT(eat, -0.1) << "back toward the instinct's -0.7";
  EXPECT_GT(eat, -0.35) << "a floor to return to, not the whole instinct in one night";
}

TEST(Sleep, ALessonLearnedOnAnInstinctCellOutlastsTheNight) {
  Rig r;
  const double lesson = 0.2, instinct = -0.7;   // eating here made him hungrier
  ASSERT_TRUE(r.c.prophesy(locus::food_near, action::eat, drive::hunger, Fx::ratio(2, 10)));
  sleepFor(r, 36000);
  double eat = toDouble(r.c.brain().predict(locus::food_near, action::eat, drive::hunger));
  EXPECT_LT(std::abs(eat - lesson), std::abs(eat - instinct)) << eat;
}

// A full-strength birth table, so even an unused instinct the nightly refresh
// has pulled all the way to its level is not mistaken for a lesson.
TEST(Heirlooms, CarryALessonNeverAContextCueOrAnInstinctHeWasBornWith) {
  Rig r;
  const Fx lesson = Fx::ratio(4, 10);
  ASSERT_TRUE(r.c.prophesy(locus::held, action::curl, drive::fear, lesson));
  ASSERT_TRUE(r.c.prophesy(locus::light, action::rest, drive::hunger, -Fx::ratio(6, 10)));
  ASSERT_TRUE(r.c.prophesy(locus::marble_near, action::wander, drive::fear, Fx::ratio(5, 10)));
  ASSERT_TRUE(r.c.prophesy(locus::food_near, action::eat, drive::hunger, -Fx::ratio(7, 10)));
  ASSERT_TRUE(r.c.prophesy(locus::cradled, action::rest, drive::need_touch, -Fx::ratio(5, 100)));

  Clutch k = r.c.layClutch(1, Fx::zero(), r.tick);
  ASSERT_EQ(k.heirlooms.size(), 1u);
  EXPECT_EQ(k.heirlooms[0].feature, locus::held);
  EXPECT_EQ(k.heirlooms[0].action, action::curl);
  EXPECT_EQ(k.heirlooms[0].drive, drive::fear);
  EXPECT_EQ(k.heirlooms[0].effect, fromQ15(toQ15(lesson))) << "the level is what he now believes";

  int inherited = 0;
  k.child(0).genome.forEach([&](const GeneView& v) {
    if (!(v.header.flags & GeneFlags::Heirloom)) return;
    ++inherited;
    EXPECT_EQ(v.body[0], locus::held.v);
  });
  EXPECT_EQ(inherited, 1) << "the lesson is born into the egg";
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
