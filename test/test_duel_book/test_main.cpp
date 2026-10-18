#include <gtest/gtest.h>
#include <cstring>
#include <optional>
#include <variant>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/duel_book.h"
#include "blorb/lineage.h"
#include "blorb/protocol.h"
#include "mem_storage.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr const char* kLog = "duels.log";
constexpr SpeciesId kMossby{1}, kFizz{6}, kGilly{3};
constexpr uint64_t kFizzLine = 0xF1220000F1220001ull, kGillyLine = 0x6111700006111701ull;

uint32_t nextSeed = 1000;

BattleCard cardOf(SpeciesId s, uint64_t lineage) {
  BattleCard c;
  c.species = s;
  c.power = speciesInfo(s).power;
  c.lineageId = lineage;
  c.look.lifeSeed = uint32_t(lineage);
  c.look.regions[0] = Tint{-12, 200, 90};
  return c;
}

// A battle I won, lost, drew or that fizzled, at a tier, against a friend.
enum class Result { Won, Lost, Drew, Fizzled };
Meeting meeting(Result r, uint64_t lineage = kFizzLine, SpeciesId s = kFizz, Stakes stakes = Stakes::Spar) {
  Meeting m;
  m.friendCard = cardOf(s, lineage);
  m.me = Side::B;
  m.ending = r == Result::Won ? Ending::BWon : r == Result::Lost ? Ending::AWon : Ending::Draw;
  m.fizzled = r == Result::Fizzled;
  m.battle = BattleId{nextSeed++, 0, stakes};
  m.turns = 9;
  m.manualTurns = 6;
  m.performance = 200;
  return m;
}

// Every field of the fold, so "the same book" is checked whole.
void expectSameBook(const BookState& a, const BookState& b) {
  EXPECT_EQ(a.generation, b.generation);
  EXPECT_EQ(a.xp, b.xp);
  EXPECT_EQ(a.feats, b.feats);
  EXPECT_EQ(std::memcmp(&a.skill, &b.skill, sizeof a.skill), 0);
  EXPECT_EQ(a.prowess, b.prowess);
  EXPECT_EQ(a.lossStreak, b.lossStreak);
  EXPECT_EQ(a.manualTurns, b.manualTurns);
  ASSERT_EQ(a.friends.size(), b.friends.size());
  for (size_t i = 0; i < a.friends.size(); ++i) {
    const FriendSummary &x = a.friends[i].s, &y = b.friends[i].s;
    EXPECT_EQ(x.lineageId, y.lineageId) << i;
    EXPECT_EQ(x.species, y.species) << i;
    EXPECT_EQ(std::memcmp(&x.look, &y.look, sizeof x.look), 0) << i;
    EXPECT_EQ(x.met, y.met) << i;
    EXPECT_EQ(x.won, y.won) << i;
    EXPECT_EQ(x.lost, y.lost) << i;
    EXPECT_EQ(x.drawn, y.drawn) << i;
    EXPECT_EQ(x.daysMet, y.daysMet) << i;
    EXPECT_EQ(x.fellTo, y.fellTo) << i;
    EXPECT_EQ(x.slew, y.slew) << i;
    EXPECT_EQ(x.owesUs, y.owesUs) << i;
    EXPECT_EQ(x.weOwe, y.weOwe) << i;
    EXPECT_EQ(x.firstGeneration, y.firstGeneration) << i;
    EXPECT_EQ(x.lastAt, y.lastAt) << i;
    EXPECT_EQ(a.friends[i].lastDay, b.friends[i].lastDay) << i;
    EXPECT_EQ(a.friends[i].forKeepsDay, b.friends[i].forKeepsDay) << i;
    EXPECT_EQ(a.friends[i].forKeepsThatDay, b.friends[i].forKeepsThatDay) << i;
  }
  ASSERT_EQ(a.trophies.size(), b.trophies.size());
  for (size_t i = 0; i < a.trophies.size(); ++i) {
    EXPECT_EQ(a.trophies[i].fallen, b.trophies[i].fallen);
    EXPECT_EQ(a.trophies[i].generation, b.trophies[i].generation);
  }
  ASSERT_EQ(a.vendettas.size(), b.vendettas.size());
  for (size_t i = 0; i < a.vendettas.size(); ++i) {
    EXPECT_EQ(a.vendettas[i].against, b.vendettas[i].against);
    EXPECT_EQ(a.vendettas[i].species, b.vendettas[i].species);
    EXPECT_EQ(a.vendettas[i].sinceGeneration, b.vendettas[i].sinceGeneration);
  }
  ASSERT_EQ(a.downed.size(), b.downed.size());
}

// The kinds of the frames in duels.log, in order.
std::vector<uint8_t> kinds(MemStorage& s) {
  std::vector<uint8_t> out;
  const std::vector<uint8_t>& f = s.files[kLog];
  for (size_t at = 0; at + 7 <= f.size();) {
    out.push_back(f[at]);
    at += 7 + (size_t(f[at + 1]) | size_t(f[at + 2]) << 8);
  }
  return out;
}

// ---- the fold ---------------------------------------------------------------------------

TEST(Fold, XpIsThreeAWinTwoALossOrDrawOneAFizzleAndEachLifeStartsAtZero) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.record(meeting(Result::Won), 1, 10, 1);
  b.record(meeting(Result::Lost), 1, 20, 1);
  b.record(meeting(Result::Drew), 1, 30, 1);
  b.record(meeting(Result::Fizzled), 1, 40, 1);
  EXPECT_EQ(b.xp(1), 3 + 2 + 2 + 1);
  EXPECT_EQ(b.level(1), levelFromXp(8));
  EXPECT_EQ(b.level(1), 3);
  b.record(meeting(Result::Won), 2, 50, 2);
  EXPECT_EQ(b.xp(2), 3) << "a new life's XP starts from nothing";
  EXPECT_EQ(b.xp(1), 0) << "only the newest life's XP is kept";
  EXPECT_EQ(DuelBook::open(s).xp(2), 3) << "folded back from the log";
}

TEST(Fold, ProwessStepsAnEighthTowardEachBattlesPerformanceAndAPetPlayedBattleMovesItNot) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  EXPECT_EQ(b.prowess(), 128);
  Meeting m = meeting(Result::Won);
  m.performance = 216;
  b.record(m, 1, 10, 1);
  EXPECT_EQ(b.prowess(), 128 + (216 - 128) / 8);
  Meeting alone = meeting(Result::Lost);
  alone.performance = 0;
  alone.manualTurns = 0;
  b.record(alone, 1, 20, 1);
  EXPECT_EQ(b.prowess(), 139) << "the pet played alone: nothing about the kid to learn";
  Meeting poor = meeting(Result::Lost);
  poor.performance = 19;
  b.record(poor, 2, 30, 2);
  EXPECT_EQ(b.prowess(), 139 + (19 - 139) / 8) << "prowess is the kid's, so it carries across lives";
  EXPECT_EQ(b.manualTurns(), 12u);
  EXPECT_EQ(DuelBook::open(s).prowess(), b.prowess());
}

TEST(Fold, SkillIsTheLifesNewestFrameAndANewLifeHatchesBlank) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  Meeting m = meeting(Result::Won);
  m.skillAfter.win[5][2].v = 1234;
  m.skillAfter.battles = 1;
  b.record(m, 3, 10, 1);
  m = meeting(Result::Lost);
  m.skillAfter.win[5][2].v = -777;
  m.skillAfter.battles = 2;
  b.record(m, 3, 20, 1);
  EXPECT_EQ(b.skill(3).win[5][2].v, -777);
  EXPECT_EQ(b.skill(3).battles, 2);
  EXPECT_EQ(DuelBook::open(s).skill(3).win[5][2].v, -777);
  const DuelSkill fresh = b.skill(4);
  EXPECT_EQ(fresh.battles, 0);
  EXPECT_EQ(fresh.win[5][2].v, 0) << "each life learns afresh";
}

TEST(Fold, TheDarkLedgerCountsFallsSlayingsAndSparedLivesAcrossGenerations) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  Meeting fell = meeting(Result::Lost, kFizzLine, kFizz, Stakes::Mortal);
  fell.iFell = true;
  fell.verdict = Verdict::Finish;
  b.record(fell, 1, 10, 1);
  Meeting spared = meeting(Result::Lost, kFizzLine, kFizz, Stakes::Mortal);
  spared.wasSpared = true;
  spared.verdict = Verdict::Spare;
  b.record(spared, 2, 20, 3);
  Meeting slew = meeting(Result::Won, kFizzLine, kFizz, Stakes::Mortal);
  slew.iSlew = true;
  slew.verdict = Verdict::Finish;
  b.record(slew, 3, 30, 5);
  Meeting mercy = meeting(Result::Won, kFizzLine, kFizz, Stakes::Mortal);
  mercy.iSpared = true;
  b.record(mercy, 3, 40, 6);
  for (const DuelBook& book : {b, DuelBook::open(s)}) {
    std::optional<FriendSummary> f = book.friendOf(kFizzLine);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->fellTo, 1);
    EXPECT_EQ(f->slew, 1);
    EXPECT_EQ(f->weOwe, 1);
    EXPECT_EQ(f->owesUs, 1);
    EXPECT_EQ(f->met, 4);
    EXPECT_EQ(f->won, 2);
    EXPECT_EQ(f->lost, 2);
    EXPECT_EQ(f->daysMet, 4);
    EXPECT_EQ(f->firstGeneration, 1) << "the friendship began with great-grandma";
  }
}

TEST(Fold, TrophiesAreTheNewestThreeSlainSpeciesNewestFirstAndSlayerIsThatLifesFeat) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  const SpeciesId slain[] = {kFizz, kGilly, kMossby, SpeciesId{8}};
  uint16_t gen = 1;
  for (SpeciesId sp : slain) {
    Meeting m = meeting(Result::Won, 0x1000 + sp.v, sp, Stakes::Mortal);
    m.iSlew = true;
    b.record(m, gen++, 10 * gen, gen);
  }
  std::vector<SpeciesId> seen;
  DuelBook::open(s).forEachTrophy([&](const Trophy& t) { seen.push_back(t.fallen); });
  EXPECT_EQ(seen, (std::vector<SpeciesId>{SpeciesId{8}, kMossby, kGilly}));
  EXPECT_EQ(b.featsThisLife(4), feat::slayer);
  b.record(meeting(Result::Won), 5, 100, 9);
  EXPECT_EQ(b.featsThisLife(5), 0u) << "the feat is the slayer's life's, then the lineage's through Death";
}

TEST(Fold, FriendsAreListedMostRecentFirst) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.record(meeting(Result::Won, kFizzLine, kFizz), 1, 10, 1);
  b.record(meeting(Result::Won, kGillyLine, kGilly), 1, 20, 1);
  b.record(meeting(Result::Lost, kFizzLine, kFizz), 1, 30, 1);
  std::vector<uint64_t> order;
  DuelBook::open(s).forEachFriend([&](const FriendSummary& f) { order.push_back(f.lineageId); });
  EXPECT_EQ(order, (std::vector<uint64_t>{kFizzLine, kGillyLine}));
  EXPECT_EQ(b.friendCount(), 2);
  EXPECT_EQ(b.friendOf(kGillyLine)->look.regions[0].hue, -12) << "the friend's look is kept to draw them";
}

TEST(Fold, LossesInARowCountUntilAnythingButALoss) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.record(meeting(Result::Lost), 1, 10, 1);
  b.record(meeting(Result::Lost), 1, 20, 1);
  EXPECT_EQ(b.lossesInARow(), 2);
  EXPECT_EQ(DuelBook::open(s).lossesInARow(), 2);
  b.record(meeting(Result::Drew), 1, 30, 1);
  EXPECT_EQ(b.lossesInARow(), 0);
}

TEST(Fold, ACuddleEndsTheLosingStreakAndTheBookRemembersItAcrossABoot) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.recordCuddle(5);
  EXPECT_EQ(s.size(kLog), 0u) << "a cuddle with no losses to soothe writes nothing";
  for (uint32_t at : {10u, 20u, 30u}) b.record(meeting(Result::Lost), 1, at, 1);
  ASSERT_EQ(b.lossesInARow(), 3);
  b.recordCuddle(40);
  EXPECT_EQ(b.lossesInARow(), 0);
  DuelBook again = DuelBook::open(s);
  EXPECT_EQ(again.lossesInARow(), 0);
  EXPECT_EQ(again.friendOf(kFizzLine)->lost, 3) << "the losses are still his record";
  again.record(meeting(Result::Lost), 1, 50, 1);
  EXPECT_EQ(DuelBook::open(s).lossesInARow(), 1) << "a new streak starts after the cuddle";
}

TEST(Fold, HeadToHeadAndForKeepsPerDayComeFromTheLinesRecord) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.record(meeting(Result::Won, kFizzLine, kFizz, Stakes::Scrap), 1, 10, 4);
  b.record(meeting(Result::Lost, kFizzLine, kFizz, Stakes::Spar), 1, 20, 4);
  b.record(meeting(Result::Lost, kFizzLine, kFizz, Stakes::Spar), 1, 30, 4);
  const DuelBook::HeadToHead h = DuelBook::open(s).headToHead(kFizzLine);
  EXPECT_EQ(h.duels, 3);
  EXPECT_EQ(h.myWins, 1);
  EXPECT_EQ(h.theirWins, 2);
  EXPECT_EQ(b.forKeepsOn(kFizzLine, 4), 1) << "a spar is not for keeps";
  EXPECT_EQ(b.forKeepsOn(kFizzLine, 5), 0) << "a new pet day";
  EXPECT_EQ(b.forKeepsOn(kGillyLine, 4), 0);
  EXPECT_EQ(b.headToHead(kGillyLine).duels, 0);
}

// ---- vendettas --------------------------------------------------------------------------

Meeting fellTo(uint64_t lineage, SpeciesId s) {
  Meeting m = meeting(Result::Lost, lineage, s, Stakes::Mortal);
  m.iFell = true;
  m.verdict = Verdict::Finish;
  return m;
}

TEST(Vendetta, AFallSwearsOneForTheNextThreeGenerationsAndNoLonger) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.record(fellTo(kFizzLine, kFizz), 4, 10, 1);
  for (const DuelBook& book : {b, DuelBook::open(s)}) {
    EXPECT_FALSE(book.vendetta(4)) << "the fallen himself swears nothing";
    for (uint16_t g : {5, 6, 7}) {
      std::optional<Vendetta> v = book.vendetta(g);
      ASSERT_TRUE(v) << g;
      EXPECT_EQ(v->against, kFizzLine);
      EXPECT_EQ(v->species, kFizz);
      EXPECT_EQ(v->sinceGeneration, 4);
    }
    EXPECT_FALSE(book.vendetta(8)) << "three generations, then it lapses";
  }
}

TEST(Vendetta, AScrapOrMortalWinAgainstThatLineAvengesItAndNothingElseDoes) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.record(fellTo(kFizzLine, kFizz), 1, 10, 1);
  b.record(meeting(Result::Won, kFizzLine, kFizz, Stakes::Spar), 2, 20, 2);
  EXPECT_TRUE(b.vendetta(2)) << "a spar is play, not vengeance";
  b.record(meeting(Result::Won, kGillyLine, kGilly, Stakes::Scrap), 2, 30, 2);
  EXPECT_TRUE(b.vendetta(2)) << "the wrong line";
  b.record(meeting(Result::Lost, kFizzLine, kFizz, Stakes::Scrap), 2, 40, 3);
  EXPECT_TRUE(b.vendetta(2));
  b.record(meeting(Result::Won, kFizzLine, kFizz, Stakes::Scrap), 2, 50, 4);
  EXPECT_FALSE(b.vendetta(2)) << "avenged";
  EXPECT_FALSE(b.vendetta(3));
  EXPECT_FALSE(DuelBook::open(s).vendetta(3)) << "and it stays avenged in the log";
}

TEST(Vendetta, ANewbornsSkillLeansToTheCounterOfTheVictorsKindAndOnlyAgainstItsElement) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.record(fellTo(kFizzLine, kFizz), 1, 10, 1);
  const SpeciesInfo& fizz = speciesInfo(kFizz);
  const uint8_t counter = uint8_t(counterOf(fizz.style));
  const DuelSkill seeded = DuelBook::open(s).skillForNewLife(2);
  int nudged = 0;
  for (uint8_t ctx = 0; ctx < DuelSkill::kContexts; ++ctx)
    for (uint8_t st = 0; st < kCycle; ++st) {
      const bool theirElement = ctx / (kCycle + 1) == uint8_t(fizz.element);
      if (theirElement && st == counter) {
        EXPECT_GT(seeded.win[ctx][st].v, 0) << int(ctx);
        ++nudged;
      } else {
        EXPECT_EQ(seeded.win[ctx][st].v, 0) << int(ctx) << " " << int(st);
      }
    }
  EXPECT_EQ(nudged, kCycle + 1);
  EXPECT_EQ(b.skill(2).win[uint8_t(fizz.element) * (kCycle + 1)][counter].v, seeded.win[uint8_t(fizz.element) * (kCycle + 1)][counter].v)
      << "a life that has not dueled yet plays what it hatched with";
  const DuelSkill lapsed = b.skillForNewLife(5);
  for (const auto& ctx : lapsed.win)
    for (Q15 q : ctx) EXPECT_EQ(q.v, 0);
}

// ---- downed, verdicts and fleeing ---------------------------------------------------------

TEST(Downed, AMeetingAfterTheDowningIsTheOnlyRecordOfIt) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  Meeting m = meeting(Result::Lost, kFizzLine, kFizz, Stakes::Mortal);
  m.wasSpared = true;
  b.recordDowned(m.battle, kFizzLine, 10);
  b.record(m, 1, 20, 1);
  for (const DuelBook& book : {b, DuelBook::open(s)}) {
    EXPECT_EQ(book.friendOf(kFizzLine)->met, 1);
    EXPECT_EQ(book.friendOf(kFizzLine)->lost, 1);
    EXPECT_EQ(book.lossesInARow(), 1);
  }
}

TEST(Downed, ABootWithNoVerdictAfterADowningReadsItAsFled) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.record(meeting(Result::Won, kFizzLine, kFizz), 1, 10, 1);
  b.recordDowned(BattleId{77, 1, Stakes::Mortal}, kFizzLine, 20);
  DuelBook again = DuelBook::open(s);
  EXPECT_EQ(again.friendOf(kFizzLine)->met, 2);
  EXPECT_EQ(again.friendOf(kFizzLine)->lost, 1) << "walking away from the verdict is a loss";
  EXPECT_EQ(again.lossesInARow(), 1);
  EXPECT_EQ(again.xp(1), 3) << "and earns nothing";
  EXPECT_EQ(DuelBook::open(s).friendOf(kFizzLine)->lost, 1) << "every boot reads it the same, once";
}

TEST(Downed, ALaterBattleSettlesAnUnansweredDowningAsFled) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.recordDowned(BattleId{77, 1, Stakes::Mortal}, kFizzLine, 20);
  b.record(meeting(Result::Won, kGillyLine, kGilly), 1, 30, 1);
  for (const DuelBook& book : {b, DuelBook::open(s)}) {
    ASSERT_TRUE(book.friendOf(kFizzLine));
    EXPECT_EQ(book.friendOf(kFizzLine)->lost, 1);
    EXPECT_EQ(book.lossesInARow(), 0) << "the win came after";
  }
}

// ---- the log ----------------------------------------------------------------------------

TEST(Log, RecordIsIdempotentByBattleIdAcrossABoot) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  const Meeting m = meeting(Result::Won);
  b.record(m, 1, 10, 1);
  const size_t size = s.size(kLog);
  b.record(m, 1, 10, 1);
  DuelBook again = DuelBook::open(s);
  again.record(m, 1, 10, 1);
  again.recordDowned(m.battle, kFizzLine, 11);
  EXPECT_EQ(s.size(kLog), size) << "a repeat after a crash writes nothing";
  EXPECT_EQ(again.xp(1), 3);
  EXPECT_EQ(again.friendOf(kFizzLine)->met, 1);
  Meeting rematch = m;
  rematch.battle.rematch = 1;
  again.record(rematch, 1, 20, 1);
  EXPECT_EQ(again.friendOf(kFizzLine)->met, 2) << "a rematch is another battle";
}

TEST(Log, ATornTailIsTruncatedAndHidesNothingBeforeIt) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.record(meeting(Result::Won), 1, 10, 1);
  b.record(meeting(Result::Lost), 1, 20, 1);
  const std::vector<uint8_t> whole = s.files[kLog];
  const BookState before = b.state();

  s.tearAt(30);
  b.record(meeting(Result::Won), 1, 30, 1);
  EXPECT_EQ(s.files[kLog], whole) << "a torn append is put back";
  expectSameBook(b.state(), before);

  s.files[kLog].insert(s.files[kLog].end(), {1, 200, 0, 9, 9, 9});   // a power cut mid-frame
  DuelBook again = DuelBook::open(s);
  EXPECT_EQ(s.files[kLog], whole) << "open cuts the torn tail off";
  expectSameBook(again.state(), before);
  again.record(meeting(Result::Won), 1, 40, 1);
  EXPECT_EQ(DuelBook::open(s).xp(1), 3 + 2 + 3) << "and an append after it is read";
}

TEST(Log, AnUnknownKindIsSkippedSoANewerBookStillOpens) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  b.record(meeting(Result::Won), 1, 10, 1);
  {
    const uint8_t body[] = {0x7E, 3, 0, 1, 2, 3};
    std::vector<uint8_t> f(body, body + sizeof body);
    const uint32_t crc = crc32(f.data(), f.size());
    for (int i = 0; i < 4; ++i) f.push_back(uint8_t(crc >> (8 * i)));
    s.files[kLog].insert(s.files[kLog].end(), f.begin(), f.end());
  }
  b = DuelBook::open(s);
  b.record(meeting(Result::Lost), 1, 20, 1);
  EXPECT_EQ(DuelBook::open(s).xp(1), 5);
}

TEST(Log, CompactionKeepsOneSummaryPerFriendAndTheSameBook) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  constexpr uint64_t kFriends = 12;
  std::vector<Meeting> recent;
  for (int i = 0; i < 700; ++i) {
    const uint64_t line = 0x5000 + uint64_t(i) % kFriends;
    Meeting m = meeting(Result(i % 4), line, SpeciesId{uint8_t(line % kSpeciesCount)}, Stakes(i % 3));
    m.performance = uint8_t(i * 37);
    m.manualTurns = uint8_t(i % 5);
    m.skillAfter.win[i % 12][i % 3].v = int16_t(i);
    m.iSlew = i % 97 == 0;
    m.iFell = i % 89 == 0;
    m.wasSpared = i % 13 == 0;
    m.iSpared = i % 17 == 0;
    if (i == 650) b.recordDowned(BattleId{55, 0, Stakes::Mortal}, 0x5003, uint32_t(i));
    b.record(m, uint16_t(1 + i / 100), uint32_t(i * 10), uint16_t(i / 7));
    recent.push_back(m);
  }
  EXPECT_LT(s.size(kLog), DuelBook::kByteCap);

  const std::vector<uint8_t> k = kinds(s);
  EXPECT_EQ(std::count(k.begin(), k.end(), 4), int(kFriends)) << "one Friend summary per friend";
  EXPECT_EQ(std::count(k.begin(), k.end(), 3), 1) << "one Self summary";
  EXPECT_GE(std::count(k.begin(), k.end(), 1), int(DuelBook::kKeepRecent));

  // The book in RAM was folded entry by entry and never re-read; the one
  // reopened here is folded from what compaction left.
  expectSameBook(DuelBook::open(s).state(), b.state());

  const size_t size = s.size(kLog);
  b.record(recent.back(), uint16_t(1 + 699 / 100), 6990, 699 / 7);
  EXPECT_EQ(s.size(kLog), size) << "a recent battle stays a no-op through compaction";
}

// A downing still waiting for its verdict when compaction cuts the log is
// carried in the summary, so the next boot still reads the flight as a loss.
TEST(Log, CompactionCarriesADowningStillWaitingForItsVerdict) {
  size_t metBytes = 0, downedBytes = 0;
  {
    MemStorage probe;
    DuelBook p = DuelBook::open(probe);
    p.record(meeting(Result::Won, kGillyLine, kGilly), 1, 0, 0);
    metBytes = probe.size(kLog);
    p.recordDowned(BattleId{1, 0, Stakes::Mortal}, kFizzLine, 0);
    downedBytes = probe.size(kLog) - metBytes;
  }
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  // Fill so that the eighth meeting after the downing is the one that crosses the cap.
  uint32_t at = 0;
  while (s.size(kLog) + DuelBook::kKeepRecent * metBytes + downedBytes <= DuelBook::kByteCap)
    b.record(meeting(Result::Won, kGillyLine, kGilly), 1, at += 10, 1);
  b.recordDowned(BattleId{77, 0, Stakes::Mortal}, kFizzLine, at += 10);
  for (uint8_t i = 0; i < DuelBook::kKeepRecent; ++i) b.record(meeting(Result::Won, kGillyLine, kGilly), 1, at += 10, 1);
  ASSERT_LT(s.size(kLog), DuelBook::kByteCap) << "compacted";

  const std::vector<uint8_t> k = kinds(s);
  ASSERT_GE(k.size(), size_t(DuelBook::kKeepRecent) + 1);
  EXPECT_EQ(k[k.size() - DuelBook::kKeepRecent - 1], 2) << "the downing sits just before the kept meetings";
  EXPECT_EQ(b.friendOf(kFizzLine)->lost, 1);
  const DuelBook again = DuelBook::open(s);
  expectSameBook(again.state(), b.state());
  EXPECT_EQ(again.friendOf(kFizzLine)->lost, 1) << "walking away from the verdict is still a loss";
}

// ---- Founding's species ---------------------------------------------------------------------

std::vector<uint8_t> foundingFrame(uint64_t lineageId, uint32_t at, const char (&species)[16], const Genome& g) {
  std::vector<uint8_t> p;
  for (int i = 0; i < 8; ++i) p.push_back(uint8_t(lineageId >> (8 * i)));
  for (int i = 0; i < 4; ++i) p.push_back(uint8_t(at >> (8 * i)));
  p.insert(p.end(), species, species + 16);
  const std::vector<uint8_t>& bytes = g.bytes();
  p.push_back(uint8_t(bytes.size())), p.push_back(uint8_t(bytes.size() >> 8));
  p.insert(p.end(), bytes.begin(), bytes.end());
  std::vector<uint8_t> f{1, uint8_t(p.size()), uint8_t(p.size() >> 8)};
  f.insert(f.end(), p.begin(), p.end());
  const uint32_t crc = crc32(f.data(), f.size());
  for (int i = 0; i < 4; ++i) f.push_back(uint8_t(crc >> (8 * i)));
  return f;
}

TEST(Founding, AnOrdinaryPetsFoundingIsByteForByteWhatItWasBeforeSpecies) {
  MemStorage s;
  const Genome starter = starterGenome(7);
  Lineage l = Lineage::open(s, starter, 0xABCDEF, 42);
  EXPECT_STREQ(l.currentName(), "Grungo");
  const char grungo[16] = "Grungo";
  EXPECT_EQ(s.files["lineage.log"], foundingFrame(0xABCDEF, 42, grungo, starter));
}

TEST(Founding, TheFoundingPicksSpeciesNamesTheLine) {
  MemStorage s;
  const Genome starter = starterGenomeOf(kMossby, 7);
  Lineage l = Lineage::open(s, starter, 0xABCDEF, 42);
  EXPECT_STREQ(l.currentName(), "Mossby");
  const char mossby[16] = "Mossby";
  EXPECT_EQ(s.files["lineage.log"], foundingFrame(0xABCDEF, 42, mossby, starter));
  std::optional<Founding> f;
  l.forEach([&](const LineageEntry& e) { if (auto* x = std::get_if<Founding>(&e)) f = *x; });
  ASSERT_TRUE(f);
  EXPECT_STREQ(f->species, "Mossby");
  EXPECT_STREQ(Lineage::open(s, starterGenome(7), 1, 0).currentName(), "Mossby") << "the log, not the next starter, names it";
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
