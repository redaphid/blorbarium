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
    EXPECT_EQ(x.firstGeneration, y.firstGeneration) << i;
    EXPECT_EQ(x.lastAt, y.lastAt) << i;
    EXPECT_EQ(a.friends[i].lastDay, b.friends[i].lastDay) << i;
    EXPECT_EQ(a.friends[i].forKeepsDay, b.friends[i].forKeepsDay) << i;
    EXPECT_EQ(a.friends[i].forKeepsThatDay, b.friends[i].forKeepsThatDay) << i;
  }
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

// A book 0.7.0 wrote: a Mortal the pet lost and was finished in (stakes 2,
// verdict Finish, the fell flag), the downing before it, the victor's trophy
// and the line's vendetta. It opens, its battle counts as the loss it was,
// and the dark ledger is read past: nothing of it reaches the book.
TEST(Log, A070BookWithItsDarkLedgerOpensAndReadsItPast) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  Meeting lost = meeting(Result::Lost, kFizzLine, kFizz, Stakes::Scrap);
  lost.skillAfter.win[3][1].v = 1234;
  b.record(lost, 2, 10, 1);
  std::vector<uint8_t>& log = s.files[kLog];
  const auto reseal = [&](size_t at) {
    const size_t len = size_t(log[at + 1]) | size_t(log[at + 2]) << 8;
    const uint32_t crc = crc32(log.data() + at, 3 + len);
    for (int i = 0; i < 4; ++i) log[at + 3 + len + i] = uint8_t(crc >> (8 * i));
  };
  // The Met's payload ends: verdict, flags, scars taken, then the Skill.
  const size_t len = size_t(log[1]) | size_t(log[2]) << 8, skill = 12 * 3 * 2 + 2;
  log[3 + 8 + 5] = 2;                        // the battle's stakes: 0.7.0's Mortal
  log[3 + len - skill - 3] = 2;              // verdict: Finish
  log[3 + len - skill - 2] = 1;              // flags: I fell
  reseal(0);
  const auto append = [&](uint8_t kind, std::vector<uint8_t> body) {
    const size_t at = log.size();
    log.push_back(kind), log.push_back(uint8_t(body.size())), log.push_back(uint8_t(body.size() >> 8));
    log.insert(log.end(), body.begin(), body.end());
    log.resize(log.size() + 4);
    reseal(at);
  };
  append(2, std::vector<uint8_t>(18, 7));    // Downed: battle, victor, at
  append(5, {uint8_t(kFizz.v), 2, 0});       // Trophy
  append(6, std::vector<uint8_t>(11, 3));    // Vendetta

  DuelBook old = DuelBook::open(s);
  EXPECT_EQ(old.xp(2), 2) << "the battle he lost";
  ASSERT_TRUE(old.friendOf(kFizzLine).has_value());
  EXPECT_EQ(old.friendOf(kFizzLine)->met, 1);
  EXPECT_EQ(old.friendOf(kFizzLine)->lost, 1) << "the Downed entry adds no second loss";
  EXPECT_EQ(old.lossesInARow(), 1);
  EXPECT_EQ(old.skill(2).win[3][1].v, 1234) << "every byte after the retired ones still lines up";
  const DuelSkill fresh = old.skill(3), zero{};
  EXPECT_EQ(std::memcmp(&fresh, &zero, sizeof fresh), 0) << "no vendetta seeds the next life";
  old.record(meeting(Result::Won, kFizzLine, kFizz, Stakes::Scrap), 3, 20, 2);
  EXPECT_EQ(DuelBook::open(s).friendOf(kFizzLine)->met, 2) << "and it takes the next battle";
}

TEST(Log, CompactionKeepsOneSummaryPerFriendAndTheSameBook) {
  MemStorage s;
  DuelBook b = DuelBook::open(s);
  constexpr uint64_t kFriends = 12;
  std::vector<Meeting> recent;
  for (int i = 0; i < 700; ++i) {
    const uint64_t line = 0x5000 + uint64_t(i) % kFriends;
    Meeting m = meeting(Result(i % 4), line, SpeciesId{uint8_t(line % kSpeciesCount)}, Stakes(i % kStakesTiers));
    m.performance = uint8_t(i * 37);
    m.manualTurns = uint8_t(i % 5);
    m.skillAfter.win[i % 12][i % 3].v = int16_t(i);
    m.fled = i % 13 == 0;
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
