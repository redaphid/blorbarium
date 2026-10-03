#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <variant>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "links.h"
#include "mem_storage.h"
#include "time_sources.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

Genome edited(Genome g, uint8_t type, uint8_t firstByte, uint8_t offset, uint8_t value) {
  GenomeBuilder b = GenomeBuilder::from(g);
  g.forEach([&](const GeneView& v) {
    if (v.header.type == type && (firstByte == 255 || v.body[0] == firstByte)) b.setByte(v.header.uid, offset, value);
  });
  return *b.build();
}

Genome quickEgg() { return edited(starterGenome(7), GeneKindOf<EggGene>::value, 255, 0, 1); }

// The vision -> glow receptor writes nothing and the glow region is black:
// every gene that could light the halo is zeroed.
Genome glowless() {
  Genome g = edited(quickEgg(), GeneKindOf<ReceptorGene>::value, chem::vision.v, 3, 128);
  g = edited(g, GeneKindOf<ReceptorGene>::value, chem::vision.v, 4, 128);
  GenomeBuilder b = GenomeBuilder::from(g);
  g.forEach([&](const GeneView& v) {
    if (v.header.type == GeneKindOf<PaletteGene>::value && v.body[0] == region::glow.v) {
      b.setByte(v.header.uid, 3, 0);
      b.setByte(v.header.uid, 5, 0);
    }
  });
  return *b.build();
}

struct Rig {
  MemStorage store;
  NullLink link;
  Dish dish;
  uint32_t ms = 0;
  explicit Rig(const Genome& founder = quickEgg()) : dish(store, 7, 1, DishOptions{nullptr, &founder}) {}
  void run(uint32_t forMs) { runFor(dish, link, ms, forMs); }
  void hatch() {
    while (!std::holds_alternative<Creature>(dish.occupant())) run(1000);
  }
  Creature& creature() { return std::get<Creature>(dish.occupant()); }
  template <class Pred> void runUntil(Pred done, uint32_t limitMs) {
    for (uint32_t end = ms + limitMs; ms < end && !done();) run(100);
  }
};

int pm(Fx v) { return int((int64_t(v.raw) * 1000) >> Fx::kFrac); }

// Every field, so a golden catches any drift in the seam.
std::string dump(const Appearance& a) {
  char b[1024];
  int n = std::snprintf(b, sizeof b,
                        "kind=%d gen=%u stage=%d seed=%08x at=%d,%d facing=%d scale=%u pose=%u/%u face=%u<%u %d t%u "
                        "reflex=%d/%u %d %d glow=%d seer=%d asleep=%d dream=%d call=%d eat=%d injury=%d wobble=%d "
                        "night=%d egg=%d fade=%d eggs=%u@%u hint=%d/%u %d stim=%u+%u unknown=%d",
                        int(a.kind), unsigned(a.generation), int(a.stage), unsigned(a.lifeSeed), pm(a.at.x), pm(a.at.y),
                        pm(a.facing), unsigned(a.scalePct), unsigned(a.pose.v), unsigned(a.poseTick),
                        unsigned(a.expression.v), unsigned(a.previous.v), pm(a.intensity), unsigned(a.exprTicks),
                        int(a.reflexActive), unsigned(a.reflex.v), pm(a.reflexPhase), pm(a.reflexStrength), pm(a.glow),
                        int(a.foreseeing), int(a.asleep), int(a.dreaming), int(a.calling), int(a.eating), pm(a.injury),
                        pm(a.wobble), pm(a.night), pm(a.eggProgress), pm(a.remainsFade), unsigned(a.eggCount),
                        unsigned(a.cursor), int(a.hasHint), unsigned(a.hint.v), pm(a.hintUrgency),
                        unsigned(a.lastStim.v), unsigned(a.ticksSinceStim), int(a.timeUnknown));
  std::string s(b, size_t(n));
  for (const Tint& t : a.regions) s += " r" + std::to_string(t.hue) + "," + std::to_string(t.sat) + "," + std::to_string(t.val);
  for (uint8_t i = 0; i < a.markCount; ++i)
    s += " m" + std::to_string(a.marks[i].layer) + "," + std::to_string(a.marks[i].variant) + "," +
         std::to_string(a.marks[i].tint.hue);
  for (uint8_t i = 0; i < a.itemCount; ++i)
    s += " i" + std::to_string(int(a.items[i].what)) + "@" + std::to_string(pm(a.items[i].at.x)) + "," +
         std::to_string(pm(a.items[i].at.y));
  s += " pantry=" + std::to_string(a.pantry);
  for (uint8_t i = 0; i < a.eggCount; ++i)
    s += " e" + std::to_string(a.eggs[i].cloak.hue) + "," + std::to_string(a.eggs[i].lookChanges) + "," +
         std::to_string(a.eggs[i].mindChanges);
  return s;
}

void growTo(Rig& r, Stage s) {
  const Fx life[] = {Fx::ratio(85, 100), Fx::ratio(60, 100), Fx::ratio(15, 100)};
  for (uint8_t i = 0; uint8_t(r.creature().stage()) < uint8_t(s) && i < 3; ++i) {
    r.creature().inject(chem::life, life[i]);
    r.run(500);
  }
}

}  // namespace

// ---- struct goldens: the nine states the pack draws ----------------------------------

TEST(Golden, Egg) {
  Rig r(starterGenome(7));
  r.run(10000);
  EXPECT_EQ(dump(r.dish.appearance()),
            "kind=0 gen=0 stage=0 seed=82438f44 at=0,0 facing=0 scale=100 pose=0/0 face=0<0 0 t0 reflex=0/0 0 "
            "0 glow=0 seer=0 asleep=0 dream=0 call=0 eat=0 injury=0 wobble=0 night=999 egg=5 fade=0 eggs=0@0 "
            "hint=0/0 0 stim=0+0 unknown=1 r3,128,128 r0,128,137 r0,128,118 r0,128,128 r0,128,128 r0,140,160 "
            "r0,128,128 m0,4,0 i2@0,0 pantry=0");
}

TEST(Golden, Hatchling) {
  Rig r;
  r.hatch();
  r.run(20000);
  EXPECT_EQ(dump(r.dish.appearance()),
            "kind=1 gen=0 stage=0 seed=386b0319 at=69,-175 facing=-516 scale=55 pose=0/12 face=0<0 349 t210 "
            "reflex=0/0 0 0 glow=420 seer=0 asleep=0 dream=0 call=0 eat=0 injury=0 wobble=0 night=998 egg=0 "
            "fade=0 eggs=0@0 hint=0/0 0 stim=34+14 unknown=1 r3,128,128 r0,128,137 r0,128,118 r0,128,128 "
            "r0,128,128 r0,140,169 r0,128,128 m0,4,0 i2@52,-545 pantry=4");
}

TEST(Golden, IdleAdult) {
  Rig r;
  r.hatch();
  growTo(r, Stage::Adult);
  r.creature().force(action::rest);
  r.run(1000);
  EXPECT_EQ(dump(r.dish.appearance()),
            "kind=1 gen=0 stage=2 seed=386b0319 at=90,10 facing=192 scale=103 pose=0/9 face=0<0 349 t30 "
            "reflex=0/0 0 0 glow=0 seer=0 asleep=0 dream=0 call=0 eat=0 injury=0 wobble=0 night=998 egg=0 "
            "fade=0 eggs=0@0 hint=0/0 0 stim=255+65535 unknown=1 r3,128,128 r0,128,137 r0,128,118 r0,128,128 "
            "r0,128,128 r0,140,160 r0,128,128 m0,4,0 i2@52,-545 pantry=4");
}

TEST(Golden, Eating) {
  Rig r;
  r.hatch();
  r.dish.sample(pressed(), r.ms);
  r.run(300);
  r.creature().force(action::eat);
  r.runUntil([&] { return r.dish.appearance().eating; }, 60000);
  EXPECT_EQ(dump(r.dish.appearance()),
            "kind=1 gen=0 stage=0 seed=386b0319 at=87,39 facing=334 scale=55 pose=0/0 face=1<0 548 t1 "
            "reflex=0/0 0 0 glow=0 seer=0 asleep=0 dream=0 call=0 eat=1 injury=0 wobble=0 night=998 egg=0 "
            "fade=0 eggs=0@0 hint=0/0 0 stim=17+1 unknown=1 r3,128,128 r0,128,137 r0,128,118 r0,128,128 "
            "r0,128,128 r0,140,160 r0,128,128 m0,4,0 i2@52,-545 pantry=3");
}

TEST(Golden, Foresee) {
  Rig r;
  r.hatch();
  r.creature().force(action::foresee);
  r.run(1500);
  EXPECT_EQ(dump(r.dish.appearance()),
            "kind=1 gen=0 stage=0 seed=386b0319 at=39,-11 facing=-152 scale=55 pose=4/21 face=0<0 349 t25 "
            "reflex=0/0 0 0 glow=1000 seer=1 asleep=0 dream=0 call=0 eat=0 injury=0 wobble=0 night=998 egg=0 "
            "fade=0 eggs=0@0 hint=0/0 0 stim=33+14 unknown=1 r3,128,128 r0,128,137 r0,128,118 r0,128,128 "
            "r0,128,128 r0,140,255 r0,128,128 m0,4,0 i2@52,-545 pantry=4");
}

TEST(Golden, Hop) {
  Rig r;
  r.hatch();
  r.run(5000);
  for (int i = 0; i < 8; ++i, r.ms += 80) {   // four up-jolts: jolt(false) is under the 0.55 g threshold
    r.dish.sample(jolt(i % 2 == 0), r.ms);
    r.dish.tick(r.ms, r.link);
  }
  r.runUntil([&] { return r.dish.appearance().reflexActive; }, 3000);
  r.run(300);
  EXPECT_EQ(dump(r.dish.appearance()),
            "kind=1 gen=0 stage=0 seed=386b0319 at=80,65 facing=-369 scale=55 pose=9/4 face=2<0 643 t5 "
            "reflex=1/0 416 643 glow=1000 seer=1 asleep=0 dream=0 call=0 eat=0 injury=0 wobble=0 night=998 "
            "egg=0 fade=0 eggs=0@0 hint=0/0 0 stim=2+5 unknown=1 r3,128,128 r0,128,137 r0,128,118 r0,128,128 "
            "r0,128,128 r0,140,244 r0,128,128 m0,4,0 i2@52,-545 pantry=4");
}

TEST(Golden, Sleep) {
  Rig r;
  r.hatch();
  r.creature().inject(driveChem(drive::sleepiness), Fx::one());
  r.creature().force(action::sleep);
  r.runUntil([&] { return r.dish.appearance().asleep; }, 10000);
  r.run(2000);
  EXPECT_EQ(dump(r.dish.appearance()),
            "kind=1 gen=0 stage=0 seed=386b0319 at=39,-11 facing=-152 scale=55 pose=3/27 face=8<0 953 t21 "
            "reflex=0/0 0 0 glow=0 seer=0 asleep=1 dream=0 call=0 eat=0 injury=0 wobble=0 night=998 egg=0 "
            "fade=0 eggs=0@0 hint=0/0 0 stim=255+65535 unknown=1 r3,128,128 r0,128,137 r0,128,118 r0,128,128 "
            "r0,128,128 r0,140,160 r0,128,128 m0,4,0 i2@52,-545 pantry=4");
}

TEST(Golden, Remains) {
  Rig r;
  r.hatch();
  r.creature().inject(chem::life, Fx::ratio(5, 100));
  r.run(60000);
  EXPECT_EQ(dump(r.dish.appearance()),
            "kind=2 gen=0 stage=1 seed=386b0319 at=0,0 facing=0 scale=100 pose=0/0 face=0<0 0 t0 reflex=0/0 0 "
            "0 glow=0 seer=0 asleep=0 dream=0 call=0 eat=0 injury=0 wobble=0 night=997 egg=0 fade=33 eggs=1@0 "
            "hint=0/0 0 stim=0+0 unknown=1 r3,128,128 r0,128,137 r0,128,118 r0,128,128 r0,128,128 r0,140,160 "
            "r0,128,128 m0,4,0 i2@52,-545 pantry=4 e0,1,6");
}

TEST(Golden, Clutch) {
  Rig r;
  r.hatch();
  growTo(r, Stage::Adult);
  r.creature().inject(chem::life, Fx::ratio(5, 100));
  r.run(Clutch::kVigilTicks * kTickMs + 5000);
  EXPECT_EQ(dump(r.dish.appearance()),
            "kind=3 gen=1 stage=0 seed=386b0319 at=0,0 facing=0 scale=100 pose=0/0 face=0<0 0 t0 reflex=0/0 0 "
            "0 glow=0 seer=0 asleep=0 dream=0 call=0 eat=0 injury=0 wobble=0 night=956 egg=0 fade=0 eggs=3@0 "
            "hint=0/0 0 stim=0+0 unknown=1 r0,128,128 r0,128,128 r0,128,128 r0,128,128 r0,128,128 r0,128,128 "
            "r0,128,128 i2@52,-545 pantry=4 e0,1,5 e0,1,3 e0,1,4");
}

// ---- properties ------------------------------------------------------------------------

namespace {

struct ForeseeRun { uint32_t foreseeTicks = 0, belowFloor = 0; Fx maxGlowLocus{}; };

// Six pet hours of an idle morning with a forced scry every half hour, checked every tick.
ForeseeRun foreseeFor(const Genome& g) {
  Rig r(g);
  r.hatch();
  ForeseeRun out;
  uint32_t last = r.dish.tickCount();
  for (uint32_t end = r.ms + 6 * 3600 * 1000u; r.ms < end; r.ms += kSampleMs) {
    if (r.ms % (30 * 60000) == 0 && std::holds_alternative<Creature>(r.dish.occupant())) r.creature().force(action::foresee);
    r.dish.sample(still(), r.ms);
    r.dish.tick(r.ms, r.link);
    if (r.dish.tickCount() == last || !std::holds_alternative<Creature>(r.dish.occupant())) continue;
    last = r.dish.tickCount();
    const Creature& c = r.creature();
    out.maxGlowLocus = fxMax(out.maxGlowLocus, c.chemistry().locus[locus::glow.v]);
    if (c.action() != action::foresee) continue;
    ++out.foreseeTicks;
    Appearance a = r.dish.appearance();
    out.belowFloor += !a.foreseeing || a.glow < kForeseeGlowFloor;
  }
  return out;
}

}  // namespace

TEST(Foresee, EveryForeseeTickGlowsAtLeastTheFloor) {
  ForeseeRun run = foreseeFor(quickEgg());
  EXPECT_GT(run.foreseeTicks, 200u);
  EXPECT_EQ(run.belowFloor, 0u);
}

TEST(Foresee, EvenWhenEveryGlowGeneIsZeroed) {
  ForeseeRun run = foreseeFor(glowless());
  EXPECT_GT(run.foreseeTicks, 200u);
  EXPECT_EQ(run.maxGlowLocus, Fx::zero()) << "the genes alone would never light him";
  EXPECT_EQ(run.belowFloor, 0u);
}

TEST(Care, TheHintNamesTheGestureForTheMostPressingNeed) {
  Rig r;
  r.hatch();
  r.creature().inject(driveChem(drive::hunger), Fx::ratio(9, 10));
  r.creature().inject(driveChem(drive::boredom), Fx::ratio(6, 10));
  r.run(200);
  Appearance a = r.dish.appearance();
  ASSERT_TRUE(a.hasHint);
  EXPECT_EQ(a.hint, care::feed);
  r.creature().inject(driveChem(drive::hunger), Fx::zero());
  r.creature().inject(driveChem(drive::boredom), Fx::ratio(9, 10));
  r.run(200);
  EXPECT_EQ(r.dish.appearance().hint, care::play);
}

TEST(Time, TheMarqueeShowsUntilASourceGivesTheTime) {
  Rig r;
  r.run(5000);
  EXPECT_TRUE(r.dish.appearance().timeUnknown);
  r.dish.phoneTime().set(1790000000u);
  r.dish.checkWall();
  EXPECT_FALSE(r.dish.appearance().timeUnknown);

  MemStorage store;
  FakeRtc rtc;
  rtc.atBoot = 1790000000u;
  NullLink link;
  Dish withRtc(store, 7, 1, DishOptions{&rtc, nullptr});
  withRtc.tick(0, link);
  EXPECT_FALSE(withRtc.appearance().timeUnknown);
}

TEST(Portrait, AnAncestorAtRestWithItsStageScale) {
  Genome g = starterGenome(7);
  Appearance baby = portrait(g, Stage::Baby, 0, 0), adult = portrait(g, Stage::Adult, 0, 0);
  EXPECT_EQ(baby.scalePct, 55);
  EXPECT_EQ(adult.scalePct, 103) << "the adult size gene's base is 136";
  EXPECT_EQ(adult.expression, expr::neutral);
  EXPECT_EQ(adult.lifeSeed, g.hash());
  EXPECT_EQ(adult.markCount, 1) << "the long-stalk mark waits for an elder in the lineage";
  EXPECT_EQ(portrait(g, Stage::Adult, 0, feat::reached_elder).markCount, 2);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
