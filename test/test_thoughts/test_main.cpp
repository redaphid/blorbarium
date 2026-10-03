#include <gtest/gtest.h>
#include <algorithm>
#include <iterator>
#include <cstring>
#include <string>
#include <variant>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/lineage.h"
#include "links.h"
#include "mem_storage.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint32_t kSecond = 1000, kMinute = 60 * kSecond;

// Every gene of `type` (whose first body byte is `firstByte`, 255 = any) gets `value` at `offset`.
Genome edited(Genome g, uint8_t type, uint8_t firstByte, uint8_t offset, uint8_t value) {
  GenomeBuilder b = GenomeBuilder::from(g);
  g.forEach([&](const GeneView& v) {
    if (v.header.type == type && (firstByte == 255 || v.body[0] == firstByte)) b.setByte(v.header.uid, offset, value);
  });
  return *b.build();
}
constexpr uint8_t kOracle = GeneKindOf<OracleGene>::value;

Genome quickEgg() { return edited(starterGenome(7), GeneKindOf<EggGene>::value, 255, 0, 1); }
// The founder with an oracle gene (grungo's species default when it carries none), one byte set.
Genome oracle(uint8_t offset, uint8_t value, Genome g = quickEgg()) {
  bool carries = false;
  g.forEach([&](const GeneView& v) { carries = carries || v.header.type == kOracle; });
  if (!carries) {
    const Phenotype::Oracle d{};
    OracleGene body{d.chance, d.voice.v, {}};
    std::copy(std::begin(d.topics), std::end(d.topics), body.topics);
    g = *GenomeBuilder::from(g).append(body, GeneFlags::Mutable).build();
  }
  return edited(g, kOracle, 255, offset, value);
}
Genome withChance(uint8_t chance) { return oracle(0, chance); }
Genome withVoice(VoiceId v) { return oracle(1, v.v); }
// Every topic weight 1 but `favourite`'s, so its prophecies dominate whatever holds.
Genome leaning(TopicId favourite) {
  Genome g = quickEgg();
  for (size_t t = 0; t < kTopicCount; ++t) g = oracle(uint8_t(2 + t), t == favourite.v ? 255 : 1, g);
  return g;
}

struct Line { uint32_t tick; ThoughtId id; std::string text; bool prophecy; };

struct Rig {
  MemStorage store;
  NullLink link;
  Dish dish;
  uint32_t ms = 0;
  std::vector<Line> said;   // each line once, as it starts
  bool wasThinking = false;
  Fx lastPhase{};

  explicit Rig(const Genome& founder = quickEgg(), bool knowTime = true)
      : dish(store, 7, 1, DishOptions{nullptr, &founder}) {
    if (knowTime) {
      dish.phoneTime().set(1790000000);
      dish.checkWall();
    }
  }
  void run(uint32_t forMs) {
    for (uint32_t end = ms + forMs; ms < end;) {
      runFor(dish, link, ms, 100);
      Appearance a = dish.appearance();
      if (a.thinking && (!wasThinking || a.thoughtPhase < lastPhase))
        said.push_back({dish.tickCount(), a.thought, a.line, a.prophecy});
      wasThinking = a.thinking;
      lastPhase = a.thoughtPhase;
    }
  }
  void hatch() {
    for (int s = 0; !std::holds_alternative<Creature>(dish.occupant()); ++s) {
      if (s == 24 * 3600) return ADD_FAILURE() << "no hatch within a pet day";
      run(kSecond);
    }
  }
  void pastWarmUp() {
    hatch();
    run(Thinker::kWarmUpTicks * kTickMs + kSecond);
  }
  Creature& creature() { return std::get<Creature>(dish.occupant()); }
  // A shake every `everyMs`, `n` times; how many brought a prophecy.
  int shakes(int n, uint32_t everyMs) {
    int prophecies = 0;
    for (int i = 0; i < n; ++i) {
      size_t before = said.size();
      dish.fire(stim::shake);
      run(everyMs);
      for (size_t k = before; k < said.size(); ++k) prophecies += said[k].prophecy;
    }
    return prophecies;
  }
};

int countOf(const std::vector<Line>& lines, ThoughtId id) {
  int n = 0;
  for (const Line& l : lines) n += l.id == id;
  return n;
}

}  // namespace

TEST(Thoughts, AThoughtComesWhenItsConditionHoldsAndNotAgainUntilItsCooldownEnds) {
  Rig r;
  r.pastWarmUp();
  const ThoughtInfo& row = *thoughtInfo(thought::starving);
  auto starve = [&](uint32_t forMs) {
    for (uint32_t t = 0; t < forMs; t += 10 * kSecond) {
      r.creature().inject(driveChem(drive::hunger), Fx::ratio(85, 100));
      r.run(10 * kSecond);
    }
  };
  starve(5 * kMinute);
  ASSERT_EQ(countOf(r.said, thought::starving), 1) << "starving, he says so once the quiet gap ends";
  uint32_t first = 0;
  for (const Line& l : r.said) if (l.id == thought::starving) first = l.tick;
  starve(row.cooldownS * kSecond - 6 * kMinute);
  EXPECT_EQ(countOf(r.said, thought::starving), 1) << "still starving, but inside the row's cooldown";
  starve(12 * kMinute);
  ASSERT_EQ(countOf(r.said, thought::starving), 2) << "after the cooldown, the same need says it again";
  uint32_t second = 0;
  for (const Line& l : r.said) if (l.id == thought::starving) second = l.tick;
  EXPECT_GE(second - first, uint32_t(row.cooldownS) * 10);
}

TEST(Thoughts, AWellFedCalmFrogDoesNotSayHeIsStarving) {
  Rig r;
  r.pastWarmUp();
  for (int i = 0; i < 60; ++i) {
    r.creature().inject(driveChem(drive::hunger), Fx::zero());
    r.run(10 * kSecond);
  }
  EXPECT_EQ(countOf(r.said, thought::starving), 0);
  EXPECT_EQ(countOf(r.said, thought::peckish), 0);
  EXPECT_FALSE(r.said.empty()) << "ten minutes in, he has said something";
}

TEST(Thoughts, AHatchlingsFirstMinutesAreWordless) {
  Rig r;
  r.hatch();
  r.shakes(10, 10 * kSecond);
  r.run(Thinker::kWarmUpTicks * kTickMs - 110 * kSecond);
  EXPECT_TRUE(r.said.empty()) << r.said.front().text;
}

TEST(Thoughts, TimeUnknownOutranksAnyThought) {
  Rig r(quickEgg(), /*knowTime=*/false);
  r.hatch();
  r.run(Thinker::kWarmUpTicks * kTickMs + 3 * kMinute);
  r.dish.think(thought::see_pellet);
  r.run(100);
  Appearance a = r.dish.appearance();
  EXPECT_TRUE(a.timeUnknown);
  EXPECT_FALSE(a.thinking) << "with no wall time, the only line is the request for a phone";
  EXPECT_TRUE(r.said.empty());
}

TEST(Thoughts, TheSameSeedAndRoutineSayTheSameLinesAtTheSameTicks) {
  auto routine = [](Rig& r) {
    r.pastWarmUp();
    for (int i = 0; i < 20; ++i) {
      r.dish.fire(i % 3 ? stim::knock : stim::shake);
      r.run(45 * kSecond);
    }
  };
  Rig a, b;
  routine(a);
  routine(b);
  ASSERT_GE(a.said.size(), 4u);
  ASSERT_EQ(a.said.size(), b.said.size());
  for (size_t i = 0; i < a.said.size(); ++i) {
    EXPECT_EQ(a.said[i].tick, b.said[i].tick);
    EXPECT_EQ(a.said[i].id, b.said[i].id);
    EXPECT_EQ(a.said[i].text, b.said[i].text);
  }
  EXPECT_EQ(a.dish.hash(), b.dish.hash());
}

TEST(Thoughts, LinesNeverChangeTheCreature) {
  Rig talking, quiet;
  talking.pastWarmUp();
  quiet.pastWarmUp();
  for (int i = 0; i < 12; ++i) {
    talking.dish.think(THOUGHTS[size_t(i * 5) % kThoughtCount].id);
    talking.run(20 * kSecond);
    quiet.run(20 * kSecond);
  }
  EXPECT_EQ(talking.dish.hash(), quiet.dish.hash()) << "a line is presentation: the replay hash ignores it";
}

TEST(Prophecy, TheOracleGeneSetsHowOftenAShakeForetells) {
  auto prophecies = [](uint8_t chance) {
    Rig r(withChance(chance));
    r.pastWarmUp();
    return r.shakes(40, 20 * kSecond);
  };
  const int never = prophecies(0), sometimes = prophecies(64), always = prophecies(255);
  EXPECT_EQ(never, 0);
  EXPECT_GT(sometimes, 2) << "about one shake in four";
  EXPECT_LT(sometimes, 20);
  EXPECT_GT(always, 20) << "a seer who always foretells, cooldowns allowing";
  EXPECT_GT(always, sometimes);
}

TEST(Prophecy, HeForetellsInsteadOfHoppingWearingTheForeseeFace) {
  Rig r(withChance(255));
  r.pastWarmUp();
  r.dish.fire(stim::shake);
  r.run(400);
  Appearance a = r.dish.appearance();
  ASSERT_TRUE(a.thinking && a.prophecy);
  EXPECT_FALSE(a.reflexActive) << "the hop does not show while he foretells";
  EXPECT_EQ(a.expression, expr::foresee);
  EXPECT_TRUE(a.foreseeing);
  EXPECT_GE(a.glow, kForeseeGlowFloor);

  Rig hopper(withChance(0));
  hopper.pastWarmUp();
  hopper.dish.fire(stim::shake);
  hopper.run(400);
  EXPECT_TRUE(hopper.dish.appearance().reflexActive) << "with no oracle, a shake is a hop";
}

TEST(Prophecy, TopicGenesShiftWhatHeForesees) {
  auto foodShare = [](const Genome& g) {
    Rig r(g);
    r.pastWarmUp();
    const Heirlooms none;
    const Observed o{r.creature(), r.dish.habitat(), r.dish.clock(), none, r.dish.tickCount()};
    int food = 0;
    constexpr int kDraws = 2000;
    for (int i = 0; i < kDraws; ++i) {
      Rng rng = Rng::seeded(uint64_t(i) * 7919u + 1);
      std::optional<ThoughtPick> p = chooseProphecy(o, nullptr, rng);
      if (p && thoughtInfo(p->id)->topic == topic::food) ++food;
    }
    return double(food) / kDraws;
  };
  const double gourmand = foodShare(leaning(topic::food)), pondish = foodShare(leaning(topic::pond));
  EXPECT_GT(gourmand, 0.8);
  EXPECT_LT(pondish, 0.1);
}

TEST(Heirloom, TheLineageNamesTheGenerationThatLearnedAFear) {
  MemStorage store;
  Lineage lineage = Lineage::open(store, quickEgg(), 1, 0);
  Clutch k;
  k.parent = quickEgg();
  k.generation = 4;
  k.heirlooms = {Belief{locus::recent(stim::shake), action::curl, drive::fear, Fx::ratio(1, 2), Fx::one()}};
  k.seeds[0] = 99;
  Egg egg(k.child(0), k.generation, 0);
  lineage.recordBirth(egg, k, 0, 0);

  const Heirlooms found = heirloomsOf(egg.genome(), lineage);
  ASSERT_EQ(found.count, 1);
  EXPECT_EQ(found.at[0].drive, drive::fear);
  EXPECT_EQ(found.at[0].learnedBy, 3) << "the parent of generation 4 learned it";
  EXPECT_EQ(heirloomsOf(quickEgg(), lineage).count, 0) << "the founder carries none";

  char out[kThoughtLineCap];
  thoughtLine(thought::ancestor_fear, found.at[0].learnedBy, voice::terse, 0, out);
  EXPECT_STREQ(out, "GEN 3 WAS SCARED OF THIS TOO");
}

TEST(Voice, TheVoiceGeneChangesHowTheSameLineIsSaid) {
  auto said = [](VoiceId v) {
    Rig r(withVoice(v));
    r.pastWarmUp();
    r.dish.think(thought::p_mud);
    r.run(100);
    return std::string(r.dish.appearance().line);
  };
  const std::string mystic = said(voice::mystic), hoarder = said(voice::hoarder), terse = said(voice::terse);
  EXPECT_NE(mystic, hoarder);
  EXPECT_NE(mystic, terse);
  EXPECT_NE(std::string(hoarder).find("MUD"), std::string::npos) << "the voice frames the line; it keeps its meaning";
}

TEST(Voice, EveryVoiceSaysEveryLineWithinTheFontAndTheCap) {
  char out[kThoughtLineCap];
  for (const VoiceInfo& v : VOICES)
    for (const ThoughtInfo& t : THOUGHTS)
      for (uint8_t pick = 0; pick < 4; ++pick) {
        size_t n = thoughtLine(t.id, 65535, v.id, pick, out);
        ASSERT_GT(n, 0u);
        ASSERT_LT(n, kThoughtLineCap);
        ASSERT_EQ(std::strlen(out), n);
        for (size_t i = 0; i < n; ++i) ASSERT_TRUE(inFont(out[i])) << v.name << " " << t.name << ": " << out;
      }
}

TEST(Voice, ATerseVoiceStopsAtTheFirstSentenceAndAnEllipsisIsNotOne) {
  char out[kThoughtLineCap];
  thoughtLine(thought::peckish, 0, voice::terse, 0, out);
  EXPECT_STREQ(out, "PELLET?");
  thoughtLine(thought::see_pellet, 0, voice::terse, 0, out);
  EXPECT_STREQ(out, "I SEE... A PELLET");
  thoughtLine(thought::no_hat, 12, voice::terse, 0, out);
  EXPECT_STREQ(out, "GEN 12.");
}

TEST(Voice, AVoicesOwnLineReplacesTheFrame) {
  char out[kThoughtLineCap];
  thoughtLine(thought::p_pond_no, 0, voice::terse, 0, out);
  EXPECT_STREQ(out, "NO.");
  thoughtLine(thought::no_hat, 4, voice::ominous, 0, out);
  EXPECT_STREQ(out, "GEN 4. HATLESS. AS FORETOLD.");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
