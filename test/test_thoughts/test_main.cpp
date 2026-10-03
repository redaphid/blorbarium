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
Genome quickEgg() { return edited(starterGenome(7), GeneKindOf<EggGene>::value, 255, 0, 1); }
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
  void observe() {
    Appearance a = dish.appearance();
    if (a.thinking && (!wasThinking || a.thoughtPhase < lastPhase))
      said.push_back({dish.tickCount(), a.thought, a.line, a.prophecy});
    wasThinking = a.thinking;
    lastPhase = a.thoughtPhase;
  }
  void run(uint32_t forMs) {
    for (uint32_t end = ms + forMs; ms < end;) {
      runFor(dish, link, ms, 100);
      observe();
    }
  }
  // A real shake through the IMU: `jolts` alternating jolts, one every 80 ms.
  void shake(int jolts) {
    for (int i = 0; i < jolts; ++i) {
      BodySample j = still();
      j.az = i % 2 == 0 ? 2000 : 400;
      for (uint32_t t = 0; t < 80; t += kSampleMs, ms += kSampleMs) {
        dish.sample(t == 0 ? j : still(), ms);
        dish.tick(ms, link);
      }
      observe();
    }
  }
  int prophecies() const {
    int n = 0;
    for (const Line& l : said) n += l.prophecy;
    return n;
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

TEST(Say, ThePhonesLineCrossesOnceInTheCroakFaceThenHisThoughtsComeBack) {
  Rig r;
  r.pastWarmUp();
  const std::string line = "HELLO FROG";
  ASSERT_TRUE(r.dish.say(line));
  r.run(100);
  Appearance a = r.dish.appearance();
  EXPECT_TRUE(a.thinking);
  EXPECT_FALSE(a.prophecy);
  EXPECT_EQ(std::string(a.line), line);
  EXPECT_EQ(a.expression, expr::croak);
  r.dish.fire(stim::shake);
  r.run(passTicks(line.size()) * kTickMs - 200);
  a = r.dish.appearance();
  EXPECT_EQ(std::string(a.line), line) << "neither a thought nor a shake's prophecy cuts the phone's line short";
  EXPECT_EQ(a.expression, expr::croak);
  EXPECT_GT(a.thoughtPhase, Fx::ratio(9, 10)) << "most of the way across";
  r.run(200);
  a = r.dish.appearance();
  EXPECT_NE(std::string(a.line), line) << "gone after one pass";
  EXPECT_NE(a.expression, expr::croak);
  const size_t before = r.said.size();
  r.run(10 * kMinute);
  bool thought = false;
  for (size_t k = before; k < r.said.size(); ++k) thought = thought || r.said[k].text != line;
  EXPECT_TRUE(thought) << "his own lines resume";
}

TEST(Prophecy, EveryShakeForetellsWearingTheForeseeFace) {
  Rig r;
  r.pastWarmUp();
  constexpr int kShakes = 20;   // more than the rows a cooldown leaves ready
  for (int i = 0; i < kShakes; ++i) {
    const int before = r.prophecies();
    r.shake(6);
    r.run(400);
    Appearance a = r.dish.appearance();
    ASSERT_EQ(r.prophecies(), before + 1) << "shake " << i << " foretold once";
    ASSERT_TRUE(a.thinking && a.prophecy) << "shake " << i;
    EXPECT_STRNE(a.line, "");
    EXPECT_FALSE(a.reflexActive && a.reflex == reflex::hop) << "the hop does not show while he foretells";
    EXPECT_EQ(a.expression, expr::foresee);
    EXPECT_TRUE(a.foreseeing);
    EXPECT_GE(a.glow, kForeseeGlowFloor);
    EXPECT_TRUE(thoughtInfo(a.thought)->prophecy());
    r.run(2 * kSecond);
  }
  EXPECT_EQ(r.prophecies(), kShakes);
}

TEST(Prophecy, AShakeDuringAProphecyReplacesItFromTheStart) {
  Rig r;
  r.pastWarmUp();
  r.shake(6);
  r.run(1500);
  const Appearance first = r.dish.appearance();
  ASSERT_TRUE(first.thinking && first.prophecy);
  ASSERT_GT(first.thoughtPhase, Fx::ratio(1, 10)) << "the first line is partway across";
  r.shake(6);
  r.run(100);
  const Appearance second = r.dish.appearance();
  EXPECT_TRUE(second.thinking && second.prophecy);
  EXPECT_LT(second.thoughtPhase, first.thoughtPhase) << "the new line starts from the beginning";
  EXPECT_EQ(r.prophecies(), 2);
  while (r.dish.appearance().thinking && r.dish.appearance().prophecy) r.run(100);
  r.run(30 * kSecond);
  EXPECT_EQ(r.prophecies(), 2) << "the cut-off line does not come back after the new one";
}

TEST(Prophecy, OneLongShakeIsOneProphecy) {
  Rig r;
  r.pastWarmUp();
  r.shake(40);
  r.run(kSecond);
  EXPECT_EQ(r.prophecies(), 1);
}

TEST(Prophecy, TheVoiceChangesHowTheSameLineIsSaid) {
  char mystic[kThoughtLineCap], hoarder[kThoughtLineCap], terse[kThoughtLineCap];
  thoughtLine(thought::p_mud, 0, voice::mystic, 0, mystic);
  thoughtLine(thought::p_mud, 0, voice::hoarder, 0, hoarder);
  thoughtLine(thought::p_mud, 0, voice::terse, 0, terse);
  EXPECT_STRNE(mystic, hoarder);
  EXPECT_STRNE(mystic, terse);
  EXPECT_NE(std::string(hoarder).find("MUD"), std::string::npos) << "the voice frames the line; it keeps its meaning";
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
