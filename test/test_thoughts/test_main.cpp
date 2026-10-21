#include <gtest/gtest.h>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <cstring>
#include <string>
#include <variant>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/lineage.h"
#include "blorb/taught.h"
#include "../../src/hw/orient.h"
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
      j.az = i % 2 == 0 ? 2000 : 0;
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

TEST(Thoughts, WithNoWallTimeHeStillThinksAndForetells) {
  Rig r(quickEgg(), /*knowTime=*/false);
  r.pastWarmUp();
  r.run(10 * kMinute);
  ASSERT_FALSE(r.dish.timeKnown());
  EXPECT_FALSE(r.said.empty()) << "ten minutes in, he has said something with no phone";
  const int before = r.prophecies();
  r.shake(6);
  r.run(1800);   // past the trance: the answer lands about 1.2 s after the shake
  Appearance a = r.dish.appearance();
  EXPECT_EQ(r.prophecies(), before + 1) << "a shake foretells with no phone";
  EXPECT_TRUE(a.thinking && a.prophecy);
  r.run(20 * kSecond);
  r.dish.think(thought::see_pellet);
  r.run(100);
  EXPECT_TRUE(r.dish.appearance().thinking) << "a line a script asks for shows too";
}

TEST(Thoughts, HalfADayWithoutAPhoneHeMissesItAndAVisitQuietsHim) {
  Rig r(quickEgg(), /*knowTime=*/false);
  auto missed = [&r] {
    int n = 0;
    for (const Line& l : r.said) n += l.id == thought::miss_phone;
    return n;
  };
  auto play = [&r](uint32_t forMs) {
    for (uint32_t t = 0; t < forMs; t += 20 * kMinute) {
      r.dish.fire(stim::button);
      r.run(20 * kMinute);
    }
  };
  play(11 * 60 * kMinute);
  EXPECT_EQ(missed(), 0) << "not before twelve hours";
  play(6 * 60 * kMinute);
  ASSERT_GT(missed(), 0) << "fed and played with, but no phone for 17 hours";

  const int before = missed();
  r.dish.phoneTime().set(1790000000);
  r.dish.checkWall();
  r.run(500);
  ASSERT_FALSE(r.said.empty());
  EXPECT_EQ(r.said.back().text, "THE GLOWING ONE RETURNS!") << "the visit he asked for is welcomed";
  EXPECT_GE(r.dish.appearance().glow, Fx::ratio(1, 4)) << "and he glows";
  play(4 * 60 * kMinute);
  EXPECT_EQ(missed(), before) << "a phone visit quiets it";

  r.dish.phoneTime().set(1790000000 + 4 * 3600 + 30);
  r.dish.checkWall();
  r.run(500);
  EXPECT_NE(r.said.back().text, "THE GLOWING ONE RETURNS!") << "only a phone he missed is welcomed";
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
    r.run(1800);   // past the trance: the answer lands about 1.2 s after the shake
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
  // A longer line crosses slower (passTicks), so wait on the phase, not the clock.
  for (int i = 0; i < 30 && r.dish.appearance().thoughtPhase <= Fx::ratio(1, 10); ++i) r.run(100);
  const Appearance first = r.dish.appearance();
  ASSERT_TRUE(first.thinking && first.prophecy);
  ASSERT_GT(first.thoughtPhase, Fx::ratio(1, 10)) << "the first line is partway across: " << first.line;
  r.shake(6);
  r.run(100);
  const Appearance trance = r.dish.appearance();
  EXPECT_TRUE(trance.prophecy && !trance.thinking) << "the new shake cuts the line and goes back into the trance";
  EXPECT_EQ(r.prophecies(), 1) << "nothing has been answered yet";
  r.run(1500);
  const Appearance second = r.dish.appearance();
  EXPECT_TRUE(second.thinking && second.prophecy);
  EXPECT_LT(second.thoughtPhase, first.thoughtPhase) << "the new line starts from the beginning";
  EXPECT_EQ(r.prophecies(), 2);
  while (r.dish.appearance().thinking && r.dish.appearance().prophecy) r.run(100);
  r.run(30 * kSecond);
  EXPECT_EQ(r.prophecies(), 2) << "the cut-off line does not come back after the new one";
}

// The 8-Ball ritual: a shake does not answer at once. First the trance --
// the foresee face, the prophecy presentation, and no words -- and only
// when it ends does the answer land, whole.
// Discoverability: on his first day of life he teaches the two gestures a
// kid cannot guess -- the flip that finds a fight and the shake that
// foretells -- saying them as ordinary thoughts. From day two, never:
// the lessons are for the first day with a new pet.
TEST(Thoughts, HeTeachesHisGesturesOnHisFirstDayOnly) {
  Rig r;
  r.pastWarmUp();
  bool taught = false;
  for (uint32_t t = 0; t < 3 * 3600 * 1000 && !taught; t += 60 * 1000) {
    r.run(60 * 1000);
    for (const Line& l : r.said) taught = taught || l.id.v == thought::teach_fight.v || l.id.v == thought::teach_shake.v;
  }
  EXPECT_TRUE(taught) << "on his first day he names the flip and the shake";
  // Live past his first day, however much of it the search above used.
  const uint32_t dayMs = PetClock::kDayTicks * kTickMs;
  const uint32_t livedMs = r.dish.tickCount() * kTickMs;
  if (livedMs < dayMs + 3600 * 1000) r.run(dayMs + 3600 * 1000 - livedMs);
  const size_t mark = r.said.size();
  r.run(2 * 3600 * 1000);
  ASSERT_GT(r.said.size(), mark) << "he still thinks on day two";
  for (size_t i = mark; i < r.said.size(); ++i)
    EXPECT_TRUE(r.said[i].id.v != thought::teach_fight.v && r.said[i].id.v != thought::teach_shake.v)
        << "day two: the lessons are done";
}

TEST(Prophecy, AShakeTrancesBeforeItAnswers) {
  Rig r;
  r.pastWarmUp();
  r.shake(6);
  const Appearance a = r.dish.appearance();
  EXPECT_TRUE(a.prophecy) << "the trance wears the prophecy presentation";
  EXPECT_EQ(a.expression, expr::foresee);
  EXPECT_FALSE(a.thinking) << "no words yet: the ball is still swirling";
  EXPECT_STREQ(a.line, "");
  EXPECT_EQ(r.prophecies(), 0);
  r.run(1800);
  EXPECT_EQ(r.prophecies(), 1);
  EXPECT_TRUE(r.dish.appearance().thinking && r.dish.appearance().prophecy);
}

// A day's first foretelling is a fortune in his own voice, never a bare
// verdict; after that the ball flips its coin, and in 23 flips it answers
// plainly at least once. The verdict texts are thoughts.cpp's own.
TEST(Prophecy, TheDaysFirstForetellingIsAFortuneAndLaterOnesMayBeVerdicts) {
  static const char* kVerdictTexts[] = {"YES.", "YES. DEFINITELY.", "SIGNS POINT TO YES.", "COUNT ON IT.",
                                        "NO.", "NO WAY.", "MY ANSWER IS NO.", "DO NOT COUNT ON IT.",
                                        "MAYBE.", "CANNOT SAY YET.", "THE MIST IS THICK.", "ASK AGAIN LATER."};
  auto isVerdict = [](const std::string& text) {
    for (const char* v : kVerdictTexts)
      if (text == v) return true;
    return false;
  };
  Rig r;
  r.pastWarmUp();
  int verdicts = 0;
  for (int i = 0; i < 24; ++i) {
    const size_t before = r.said.size();
    r.shake(6);
    r.run(1800);
    ASSERT_GT(r.said.size(), before) << "shake " << i << " foretold";
    const Line& line = r.said.back();
    EXPECT_TRUE(line.prophecy);
    if (isVerdict(line.text)) ++verdicts;
    if (i == 0) EXPECT_FALSE(isVerdict(line.text)) << "a fortune, not a verdict: " << line.text;
  }
  EXPECT_GE(verdicts, 1) << "in 23 coin flips the ball answered plainly at least once";
  EXPECT_LE(verdicts, 23);
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
  const std::string name(kMaxNameText, 'N'), taught(kMaxTaughtText, 'T');   // the longest each can be
  for (const VoiceInfo& v : VOICES)
    for (const ThoughtInfo& t : THOUGHTS)
      for (uint8_t pick = 0; pick < 4; ++pick) {
        size_t n = thoughtLine(t.id, 65535, v.id, pick, out, kSharedOnly, Words{name, taught});
        ASSERT_GT(n, 0u);
        ASSERT_LT(n, kThoughtLineCap);
        ASSERT_EQ(std::strlen(out), n);
        for (size_t i = 0; i < n; ++i) ASSERT_TRUE(inFont(out[i])) << v.name << " " << t.name << ": " << out;
      }
}

TEST(Voice, ATerseVoiceStopsAtTheFirstSentenceAndAnEllipsisIsNotOne) {
  char out[kThoughtLineCap];
  thoughtLine(thought::peckish, 0, voice::terse, 0, out);
  EXPECT_STREQ(out, "SNACK?");
  thoughtLine(thought::see_pellet, 0, voice::terse, 0, out);
  EXPECT_STREQ(out, "I SEE... A MORSEL.");
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

TEST(Personality, PreviewSaysWhatTheDeviceSays) {
  struct Case { uint8_t species, thought, voice, pick; uint16_t num; const char* text; };
  static constexpr Case kCases[] = {
#include "said_lines.inc"
  };
  const Words kParityWords{"MOSS BOY", "I LOVE YOU, LITTLE ONE"};   // tools/personality.py PARITY_NAME, PARITY_TAUGHT
  char out[kThoughtLineCap];
  for (const Case& k : kCases) {
    thoughtLine(ThoughtId{k.thought}, k.num, VoiceId{k.voice}, k.pick, out, SpeciesId{k.species}, kParityWords);
    ASSERT_STREQ(out, k.text) << "tools/personality.py frames thought " << int(k.thought) << " differently; port the change";
  }
}

TEST(Personality, ASpeciesLineIsHisAlone) {
  const SpeciesId mossby{1};
  auto heard = [](SpeciesId s) {
    std::vector<std::string> lines;
    char out[kThoughtLineCap];
    for (uint8_t pick = 0; pick < 8; ++pick) {
      thoughtLine(thought::peckish, 0, voice::mystic, pick, out, s);
      lines.emplace_back(out);
    }
    return lines;
  };
  auto says = [](const std::vector<std::string>& lines, const char* what) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(what) != std::string::npos; });
  };
  EXPECT_TRUE(says(heard(kGrungo), "I FORESEE FLIES. IN MY MOUTH."));
  EXPECT_FALSE(says(heard(kGrungo), "THE GOOD BITS")) << "his own line replaces the shared one";
  EXPECT_FALSE(says(heard(mossby), "FORESEE FLIES"));
  EXPECT_TRUE(says(heard(mossby), "A SNACK WOULD BE NICE. LATER."));
  EXPECT_FALSE(says(heard(mossby), "THE GOOD BITS"));
  EXPECT_TRUE(says(heard(kSharedOnly), "SNACK? SNACK. THE GOOD BITS!")) << "no species: the shared line";
}

// ---- what a phone adds -------------------------------------------------------------

namespace {

constexpr uint32_t kMonday = 1791763200;   // 2026-10-12 00:00, read as local time
constexpr uint32_t kHour = 3600, kDay = 24 * kHour;

int saidWhere(const std::vector<Line>& lines, const char* whenPrefix) {
  int n = 0;
  for (const Line& l : lines) n += thoughtInfo(l.id) && startsWith(thoughtInfo(l.id)->whenName, whenPrefix);
  return n;
}

// What the trial hold's storage looks like to the Dish: every write refused.
struct HeldStore : MemStorage {
  bool held = false;
  bool writeAtomic(const char* f, const uint8_t* d, size_t n) override { return !held && MemStorage::writeAtomic(f, d, n); }
  bool append(const char* f, const uint8_t* d, size_t n) override { return !held && MemStorage::append(f, d, n); }
};

}  // namespace

TEST(FromPhone, TheWallClockConditionsReadTheOwnersLocalDayAndHour) {
  Rig r;
  r.pastWarmUp();
  ASSERT_FALSE(r.creature().body().asleep);
  const Heirlooms none;
  auto holds = [&](ThoughtWhen when, std::optional<uint32_t> local, uint16_t* num = nullptr) {
    FromPhone phone;
    phone.localNow = local;
    uint16_t n = 0;
    const bool held = when(Observed{r.creature(), r.dish.habitat(), r.dish.clock(), none, r.dish.tickCount(), phone}, n);
    if (num) *num = n;
    return held;
  };
  for (const ThoughtInfo& t : THOUGHTS)
    if (startsWith(t.whenName, "wall_")) EXPECT_FALSE(holds(t.when, std::nullopt)) << t.name << " guessed the time";
  const uint32_t mondayMorning = kMonday + 7 * kHour + 1800;
  EXPECT_TRUE(holds(when::wall_monday, mondayMorning));
  EXPECT_TRUE(holds(when::wall_morning, mondayMorning));
  EXPECT_FALSE(holds(when::wall_late, mondayMorning));
  EXPECT_FALSE(holds(when::wall_weekend, mondayMorning));
  EXPECT_TRUE(holds(when::wall_late, kMonday + 2 * kHour));
  EXPECT_TRUE(holds(when::wall_weekend, kMonday - 2 * kDay + 12 * kHour)) << "Saturday";
  EXPECT_TRUE(holds(when::wall_weekend, kMonday - 1)) << "Sunday's last second";
  EXPECT_TRUE(holds(when::wall_friday, kMonday - 3 * kDay + 20 * kHour));
  EXPECT_FALSE(holds(when::wall_friday, kMonday + 20 * kHour));
  uint16_t hour = 0;
  ASSERT_TRUE(holds(when::wall_hour, kMonday + 13 * kHour + 1800, &hour));
  EXPECT_EQ(hour, 1);
  ASSERT_TRUE(holds(when::wall_hour, kMonday + 1800, &hour));
  EXPECT_EQ(hour, 12) << "half past midnight";
}

TEST(FromPhone, WithNoPhoneHeNeverSaysAWallClockNameOrTaughtLine) {
  Rig r(quickEgg(), /*knowTime=*/false);
  r.pastWarmUp();
  r.run(6 * 60 * kMinute);
  EXPECT_GT(r.said.size(), 6u) << "he talks all the same";
  EXPECT_EQ(saidWhere(r.said, "wall_"), 0);
  EXPECT_EQ(saidWhere(r.said, "named"), 0);
  EXPECT_EQ(saidWhere(r.said, "taught"), 0);
  EXPECT_FALSE(r.dish.localNow());
}

TEST(FromPhone, AfterAPhonesTimeHeKnowsTheOwnersDayUntilAReboot) {
  Rig r(quickEgg(), /*knowTime=*/false);
  r.pastWarmUp();
  // Monday 10:30 in Phoenix (UTC-7), as the page sends it.
  const uint32_t local = kMonday + 10 * kHour + 1800, utc = local + 7 * kHour;
  r.dish.protocol().handle("#1 TIME " + std::to_string(utc) + " -420", r.dish, r.link);
  ASSERT_TRUE(r.dish.localNow());
  EXPECT_EQ(*r.dish.localNow(), local);
  const uint32_t told = r.dish.tickCount();
  const size_t before = r.said.size();
  r.run(4 * 60 * kMinute);
  const std::vector<Line> after(r.said.begin() + long(before), r.said.end());
  EXPECT_GT(saidWhere(after, "wall_"), 0) << "a Monday morning with the phone's time brings a wall-clock line";
  for (const Line& l : after) {
    if (l.id == thought::oclock) {
      const uint32_t hour = (local + (l.tick - told) * kTickMs / 1000) % kDay / kHour;
      EXPECT_NE(l.text.find(std::to_string(hour % 12 ? hour % 12 : 12) + " O'CLOCK"), std::string::npos) << l.text;
    }
    EXPECT_NE(l.id, thought::weekend) << "Monday is not the weekend";
  }
  const Genome founder = quickEgg();
  Dish rebooted(r.store, 7, 1, DishOptions{nullptr, &founder});
  EXPECT_FALSE(rebooted.localNow()) << "a replug forgets the time, and his wall-clock lines with it";
}

TEST(FromPhone, OnlyANameTheOwnerGaveIsSaidAndItOutlivesAReboot) {
  Rig r;
  r.pastWarmUp();
  EXPECT_FALSE(r.dish.lineage().named()) << "his species' name is not one the owner gave";
  r.run(10 * kMinute);
  EXPECT_EQ(saidWhere(r.said, "named"), 0);

  r.dish.rename("Moss_Boy");
  EXPECT_TRUE(r.dish.lineage().named());
  const size_t before = r.said.size();
  r.run(3 * 60 * kMinute);
  bool saidIt = false;
  for (size_t k = before; k < r.said.size(); ++k)
    if (r.said[k].id == thought::my_name) {
      saidIt = true;
      EXPECT_NE(r.said[k].text.find("MOSS BOY"), std::string::npos) << r.said[k].text;
    }
  EXPECT_TRUE(saidIt) << "an afternoon after a rename he has said his name";

  const Genome founder = quickEgg();
  Dish rebooted(r.store, 7, 1, DishOptions{nullptr, &founder});
  EXPECT_TRUE(rebooted.lineage().named()) << "the rename is on the lineage log";
}

TEST(Taught, ALineTaughtIsSaidNowThenNowAndThenAndKeptAcrossAReboot) {
  Rig r;
  r.pastWarmUp();
  const uint32_t hash = r.dish.hash();
  ASSERT_TRUE(r.dish.teach("I LOVE YOU. BE GOOD."));
  EXPECT_EQ(r.dish.hash(), hash) << "taught lines are not in the save";
  r.run(100);
  EXPECT_STREQ(r.dish.appearance().line, "I LOVE YOU. BE GOOD.") << "he says it as soon as he learns it";
  const size_t before = r.said.size();
  r.run(3 * 60 * kMinute);
  int parroted = 0;
  for (size_t k = before; k < r.said.size(); ++k)
    if (r.said[k].id == thought::parrot) {
      ++parroted;
      EXPECT_NE(r.said[k].text.find("I LOVE YOU. BE GOOD."), std::string::npos) << "whole, in his voice: " << r.said[k].text;
    }
  EXPECT_GE(parroted, 2) << "now and then";
  EXPECT_LE(parroted, 5) << "but not every line";

  const Genome founder = quickEgg();
  Dish rebooted(r.store, 7, 1, DishOptions{nullptr, &founder});
  ASSERT_EQ(rebooted.taught().count, 1);
  EXPECT_EQ(rebooted.taught().line(0), "I LOVE YOU. BE GOOD.");
}

TEST(Taught, HeKeepsTheLastFourAndALineTaughtAgainMovesToNewest) {
  Taught t;
  for (const char* l : {"ONE", "TWO", "THREE", "FOUR"}) t = t.with(l);
  t = t.with("TWO");
  ASSERT_EQ(t.count, 4);
  EXPECT_EQ(t.line(3), "TWO") << "taught again: no twin, now the newest";
  t = t.with("FIVE");
  ASSERT_EQ(t.count, 4);
  EXPECT_EQ(t.line(0), "THREE") << "ONE, the oldest, went";
  EXPECT_EQ(t.line(3), "FIVE");
}

TEST(Taught, AFileThatIsTornOrCorruptIsNoLines) {
  MemStorage s;
  ASSERT_TRUE(Taught{}.with("HELLO").with("BYE NOW").save(s));
  ASSERT_EQ(Taught::load(s).count, 2);
  const std::vector<uint8_t> good = s.files[Taught::kFile];
  for (size_t cut = 0; cut < good.size(); ++cut) {
    s.files[Taught::kFile].assign(good.begin(), good.begin() + long(cut));
    EXPECT_EQ(Taught::load(s).count, 0) << "torn at " << cut;
  }
  for (size_t i = 0; i < good.size(); ++i) {
    s.files[Taught::kFile] = good;
    s.files[Taught::kFile][i] ^= 0x20;
    EXPECT_EQ(Taught::load(s).count, 0) << "byte " << i << " flipped";
  }
  s.files.erase(Taught::kFile);
  EXPECT_EQ(Taught::load(s).count, 0) << "no file";
}

TEST(Taught, WhenTheFlashRefusesWritesNothingIsTaughtOrForgotten) {
  HeldStore store;
  const Genome founder = quickEgg();
  Dish dish(store, 7, 1, DishOptions{nullptr, &founder});
  ASSERT_TRUE(dish.teach("KEEP ME"));
  store.held = true;   // an image on trial
  EXPECT_FALSE(dish.teach("LOST"));
  EXPECT_FALSE(dish.unteach());
  ASSERT_EQ(dish.taught().count, 1) << "what he says is what the flash holds";
  EXPECT_EQ(dish.taught().line(0), "KEEP ME");
  store.held = false;
  ASSERT_TRUE(dish.unteach());
  EXPECT_EQ(dish.taught().count, 0);
  Dish rebooted(store, 7, 1, DishOptions{nullptr, &founder});
  EXPECT_EQ(rebooted.taught().count, 0) << "forgotten on the flash too";
}

TEST(Taught, ATerseVoiceSaysATaughtLineWhole) {
  char out[kThoughtLineCap];
  thoughtLine(thought::parrot, 0, voice::terse, 0, out, kSharedOnly, Words{"", "HI. I MISS YOU."});
  EXPECT_STREQ(out, "HI. I MISS YOU.");
  thoughtLine(thought::my_name, 0, voice::terse, 0, out, kSharedOnly, Words{"PIP", ""});
  EXPECT_STREQ(out, "PIP.") << "his own lines still stop at the first sentence";
}

TEST(Taught, ForcingTheParrotWithNothingTaughtSaysNothing) {
  Rig r;
  r.pastWarmUp();
  r.run(10 * kMinute);
  const size_t before = r.said.size();
  r.dish.think(thought::parrot);
  r.run(100);
  EXPECT_EQ(r.said.size(), before);
}

// ---- offline, with nothing pressing --------------------------------------------------

// The badge's main life: no phone, every need met. He talks every few minutes
// while awake, and a long day rarely repeats a line.
TEST(Idle, AContentPetWithNoPhoneIsNeverQuietLongAndRarelyRepeats) {
  Rig r(quickEgg(), /*knowTime=*/false);
  r.pastWarmUp();
  constexpr uint32_t kHours = 6;
  uint32_t lastSpoke = r.dish.tickCount(), longest = 0;
  for (uint32_t s = 0; s < kHours * 3600; ++s) {
    if (s % 10 == 0)
      for (const DriveInfo& d : DRIVES) r.creature().inject(driveChem(d.id), Fx::zero());
    r.run(kSecond);
    const uint32_t now = r.dish.tickCount();
    if (r.dish.appearance().thinking || r.creature().body().asleep) lastSpoke = now;
    longest = std::max(longest, now - lastSpoke);
  }
  // Urgent rows (kUrgentThought and up) answer what he is doing this moment,
  // so how often they come is that moment's business; the rest are his idle talk.
  std::map<std::string, int> times;
  int most = 0;
  for (const Line& l : r.said)
    if (thoughtInfo(l.id)->priority < kUrgentThought) most = std::max(most, ++times[thoughtInfo(l.id)->name]);
  std::string top;
  for (const auto& [name, n] : times) if (n == most) top += name + " ";
  std::cout << "[ idle ] " << r.said.size() << " lines, " << times.size() << " distinct, longest awake silence "
            << longest / 10 << " s, most said " << most << "x: " << top << "\n";

  EXPECT_LE(longest, Thinker::kQuietTicks + Thinker::kQuietJitterTicks + 2 * (1000 / kTickMs))
      << "awake and content, he is never quiet for more than the longest quiet gap";
  EXPECT_GE(times.size(), 50u) << "a content day is varied";
  EXPECT_LE(most, int(kHours)) << "no line comes back more than about once an hour: " << top;
}

// "Why is the sky down there" is for a pet who really is upside down: the
// badge turned half round from home (the hang), where the screen, held at
// home, shows him on his head. It used to read TiltDetector's upside_down,
// which is face down (the screen to the floor) and latched until the screen
// faced well up again: a badge laid face down for a moment, then held up on
// edge or worn on a lanyard, said it every five minutes the right way up (and
// before 0.7.12 the IMU's z was inverted, so lying face up on a desk said it).
//
// The shell's half of main.cpp feel(): the raw IMU sample turned into the
// screen held at `home`, and the poses judged from home.
struct Shell {
  orient::Up up{1};   // board_lcd128.h ORIENT_R0
  uint8_t home;
  explicit Shell(uint8_t h) : home(h) {}
  BodySample feel(const BodySample& raw, uint32_t ms) {
    up.sample(raw, ms);
    BodySample s = up.toFrame(raw, home);
    s.invertedDown = up.inverted(raw, home);
    s.sideDown = up.sideways(raw, home);
    return s;
  }
};

BodySample raw(int ax, int ay, int az) {
  BodySample s;
  s.ax = int16_t(ax), s.ay = int16_t(ay), s.az = int16_t(az);
  return s;
}
// Held still on edge with the native top, right, bottom, left edge up (test_orient's kHeld).
BodySample edgeUp(uint8_t rot) {
  constexpr int16_t kX[4] = {0, 1000, 0, -1000}, kY[4] = {-1000, 0, 1000, 0};
  return raw(kX[rot & 3], kY[rot & 3], 0);
}

// Held in `pose` for `forMs`, through the shell, awake and content (so a
// sleepy or hungry pet does not change the subject).
void holdPose(Rig& r, Shell& sh, const BodySample& pose, uint32_t forMs) {
  for (uint32_t end = r.ms + forMs; r.ms < end; r.ms += kSampleMs) {
    if (r.ms % (10 * kSecond) == 0)
      for (const DriveInfo& d : DRIVES) r.creature().inject(driveChem(d.id), Fx::zero());
    r.dish.sample(sh.feel(pose, r.ms), r.ms);
    r.dish.tick(r.ms, r.link);
    if (r.ms % kTickMs == 0) r.observe();
  }
}

TEST(Thoughts, TheSkyIsDownOnlyWhenHeIsUpsideDownFromHome) {
  for (uint8_t home : {uint8_t(2), uint8_t(0)}) {   // USB-C up (badge128), USB-C down (badge128_flip)
    SCOPED_TRACE(home == 2 ? "home USB-C up" : "home USB-C down");
    Rig r;
    r.pastWarmUp();
    Shell sh(home);
    const BodySample atHome = edgeUp(home);
    const BodySample leaningBack = raw(edgeUp(home).ax * 866 / 1000, edgeUp(home).ay * 866 / 1000, 500);
    holdPose(r, sh, atHome, 5 * kSecond);
    for (int round = 0; round < 3; ++round) {
      holdPose(r, sh, raw(0, 0, -1000), 5 * kSecond);   // face down a moment: not long enough to sleep
      holdPose(r, sh, atHome, 6 * kMinute);              // held up the right way, or on a lanyard
      holdPose(r, sh, raw(0, 0, -1000), 5 * kSecond);
      holdPose(r, sh, leaningBack, 6 * kMinute);         // leaning back in a hand, looking at him
      holdPose(r, sh, raw(0, 0, 1000), 6 * kMinute);     // face up on a desk
      holdPose(r, sh, edgeUp(home + 1), 3 * kSecond);    // on a side (the side hold) and back
      holdPose(r, sh, atHome, 2 * kSecond);
      holdPose(r, sh, edgeUp(home + 3), 3 * kSecond);
      holdPose(r, sh, atHome, 6 * kMinute);
    }
    ASSERT_TRUE(std::holds_alternative<Creature>(r.dish.occupant()));
    ASSERT_FALSE(r.creature().body().asleep);
    EXPECT_EQ(countOf(r.said, thought::sky_down), 0) << "the right way up, or face down, he is not upside down";
    EXPECT_FALSE(r.said.empty()) << "he talked meanwhile";

    holdPose(r, sh, edgeUp(home + 2), 10 * kSecond);   // the hang: half round from home
    EXPECT_EQ(countOf(r.said, thought::sky_down), 1) << "upside down from home, he notices";
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
