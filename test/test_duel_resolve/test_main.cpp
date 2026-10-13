#include <gtest/gtest.h>
#include <cstring>
#include <set>
#include "blorb/duel.h"
#include "goldens.h"
#define DUEL_BALANCE_NO_MAIN
#include "../../tools/duel_balance.cpp"

using namespace blorb;
using namespace duelgolden;

namespace {

Outcome play(const BattleCard& a, const BattleCard& b, const char* ma, const char* mb, Stakes s = Stakes::Spar,
             uint32_t seed = 1) {
  return resolve(a, b, seed, s, logOf(ma), logOf(mb));
}

Outcome mirrored(const Outcome& o) {
  Outcome m = o;
  std::swap(m.side[0], m.side[1]);
  std::swap(m.downed[0], m.downed[1]);
  if (o.ending == Ending::AWon) m.ending = Ending::BWon;
  if (o.ending == Ending::BWon) m.ending = Ending::AWon;
  for (TurnReport& r : m.turns) {
    std::swap(r.move[0], r.move[1]);   std::swap(r.shown[0], r.shown[1]);     std::swap(r.dozed[0], r.dozed[1]);
    std::swap(r.hit[0], r.hit[1]);     std::swap(r.healed[0], r.healed[1]);   std::swap(r.fired[0], r.fired[1]);
    std::swap(r.powered[0], r.powered[1]); std::swap(r.blocked[0], r.blocked[1]);
    std::swap(r.super[0], r.super[1]); std::swap(r.zone[0], r.zone[1]);
    if (r.clash != Clash::Bonk) r.clash = r.clash == Clash::AWins ? Clash::BWins : Clash::AWins;
  }
  return m;
}

struct Random {
  blorbtool::Rng rng;
  BattleCard card() {
    BattleCard c{};
    c.species = SpeciesId{uint8_t(rng.below(12))};   // past the roster too: an unknown row reads as grungo
    c.power = PowerId{uint8_t(rng.below(11))};
    c.knack = Knack{uint8_t(rng.below(8))};
    c.prowess = uint8_t(rng.below(256));
    c.lineageId = (uint64_t(rng.next()) << 32) | rng.next();
    return c;
  }
  MoveLog log(uint8_t n) {
    MoveLog l;
    for (uint8_t i = 0; i < n; ++i) l.append(Move::unpack(uint8_t(rng.below(256))));
    return l;
  }
};

constexpr BattleCard grungoCard = cardOf(sp::grungo, 0x61), gillyCard = cardOf(sp::gilly, 0x62);

// Five head bonks fill a 5-pip meter on two same-element pets.
constexpr const char* kFive = "PPPPP";

}  // namespace

// ---- golden battles and the two properties ------------------------------------------

TEST(Golden, EachBattleGivesItsPinnedHash) {
  for (const Golden& g : kGoldens)
    EXPECT_EQ(resolve(g.a, g.b, g.seed, g.stakes, logOf(g.movesA), logOf(g.movesB)).hash(), g.hash)
        << g.name << ": 0x" << std::hex << resolve(g.a, g.b, g.seed, g.stakes, logOf(g.movesA), logOf(g.movesB)).hash();
}

TEST(Mirror, TenThousandRandomBattlesMirror) {
  Random r{{0xD4}};
  for (int k = 0; k < 10000; ++k) {
    const BattleCard a = r.card(), b = r.card();
    const uint32_t seed = r.rng.next();
    const Stakes s = Stakes(r.rng.below(3));
    const MoveLog ma = r.log(uint8_t(r.rng.below(17))), mb = r.log(uint8_t(r.rng.below(17)));
    const Outcome ab = resolve(a, b, seed, s, ma, mb), ba = resolve(b, a, seed, s, mb, ma);
    ASSERT_EQ(mirrored(ab).hash(), ba.hash()) << "battle " << k;
    ASSERT_EQ(ab.verdict, ba.verdict) << "battle " << k;
  }
}

TEST(Prefix, ResolvingMoreTurnsNeverChangesTheTurnsBefore) {
  Random r{{0x9F}};
  for (int k = 0; k < 2000; ++k) {
    const BattleCard a = r.card(), b = r.card();
    const uint32_t seed = r.rng.next();
    const MoveLog ma = r.log(15), mb = r.log(15);
    const Outcome full = resolve(a, b, seed, Stakes::Scrap, ma, mb);
    for (uint8_t n = 0; n < 15; ++n) {
      MoveLog pa, pb;
      pa.adopt(ma.packed(), n);
      pb.adopt(mb.packed(), n);
      const Outcome part = resolve(a, b, seed, Stakes::Scrap, pa, pb);
      ASSERT_LE(part.resolved, n);
      for (uint8_t t = 0; t < part.resolved; ++t)
        ASSERT_EQ(std::memcmp(&part.turns[t], &full.turns[t], sizeof(TurnReport)), 0) << "battle " << k << " turn " << +t;
    }
  }
}

TEST(Hearts, StayWithinZeroAndMaxEveryTurn) {
  Random r{{0x4E}};
  for (int k = 0; k < 5000; ++k) {
    const BattleCard a = r.card(), b = r.card();
    const uint32_t seed = r.rng.next();
    const MoveLog ma = r.log(15), mb = r.log(15);
    for (uint8_t n = 1; n <= 15; ++n) {
      MoveLog pa, pb;
      pa.adopt(ma.packed(), n);
      pb.adopt(mb.packed(), n);
      const Outcome o = resolve(a, b, seed, Stakes::Spar, pa, pb);
      for (const SideState& s : o.side) {
        ASSERT_LE(s.hearts, s.maxHearts);
        ASSERT_LE(s.maxHearts, DuelRules::kHearts + DuelRules::kMaxAssistHearts);
        ASSERT_LE(s.spark, kMeterMax);
      }
    }
  }
}

TEST(Meter, AShortMeterPlaysPlain) {
  // Turn 0: nobody's meter holds a cost, so an armed move is an unarmed one.
  const Outcome armed = play(grungoCard, gillyCard, "p", "g"), plain = play(grungoCard, gillyCard, "P", "G");
  EXPECT_FALSE(armed.turns[0].powered[0]);
  EXPECT_FALSE(armed.turns[0].powered[1]);
  EXPECT_TRUE(armed.turns[0].move[0].power) << "the report keeps what was asked for";
  EXPECT_EQ(armed.side[0].hearts, plain.side[0].hearts);
  EXPECT_EQ(armed.side[1].hearts, plain.side[1].hearts);
  EXPECT_EQ(armed.side[0].spark, plain.side[0].spark);
  EXPECT_EQ(armed.side[1].spark, plain.side[1].spark);
}

TEST(Start, AssistHeartsAndTheElementHeadStart) {
  const Outcome o = play(cardOf(sp::grungo, 1, 200), cardOf(sp::fizz, 2, 100), "", "");
  EXPECT_EQ(o.side[0].maxHearts, DuelRules::kHearts);
  EXPECT_EQ(o.side[1].maxHearts, DuelRules::kHearts + 2);
  EXPECT_EQ(o.side[0].spark, DuelRules::kElementHeadStart) << "splash douses spark";
  EXPECT_EQ(o.side[1].spark, 0);
}

TEST(Seed, SymmetricInTheNoncesAndNamedByRematchAndStakes) {
  EXPECT_EQ(duelSeed(7, 9, 0, Stakes::Spar), duelSeed(9, 7, 0, Stakes::Spar));
  EXPECT_NE(duelSeed(7, 9, 0, Stakes::Spar), duelSeed(7, 9, 1, Stakes::Spar));
  EXPECT_NE(duelSeed(7, 9, 0, Stakes::Spar), duelSeed(7, 9, 0, Stakes::Scrap));
}

TEST(MoveLog, AdoptKeepsTheLongestConsistentPrefix) {
  MoveLog held = logOf("PGT");
  const MoveLog longer = logOf("PGTP"), shorter = logOf("PG"), other = logOf("PTT");
  MoveLog copy = held;
  EXPECT_EQ(copy.adopt(held.packed(), held.count()), MoveLog::Merge::Same);
  EXPECT_EQ(held.adopt(shorter.packed(), shorter.count()), MoveLog::Merge::Stale);
  EXPECT_EQ(held.count(), 3);
  EXPECT_EQ(held.adopt(other.packed(), other.count()), MoveLog::Merge::Conflict);
  EXPECT_EQ(held.count(), 3);
  EXPECT_EQ(held.adopt(longer.packed(), longer.count()), MoveLog::Merge::Grew);
  EXPECT_EQ(held.count(), 4);
  const uint8_t tooLong[DuelRules::kLogSlots + 1]{};
  MoveLog empty;
  EXPECT_EQ(empty.adopt(tooLong, DuelRules::kLogSlots + 1), MoveLog::Merge::Conflict);
  EXPECT_EQ(empty.count(), 0);
}

// ---- one case per power -------------------------------------------------------------

TEST(Powers, ForeseeTurnsHisStanceIntoTheCounter) {
  const Outcome o = play(grungoCard, gillyCard, "PPPPPp", "PPPPPP");
  ASSERT_TRUE(o.turns[5].powered[0]);
  EXPECT_EQ(o.turns[5].fired[0], power::foresee);
  EXPECT_EQ(o.turns[5].shown[0], Stance::Guard);
  EXPECT_EQ(o.turns[5].clash, Clash::AWins);
  EXPECT_EQ(o.turns[5].hit[1], 1);
  EXPECT_EQ(o.side[0].spark, 1) << "five spent, one earned";
}

TEST(Powers, ShellStaysUpUntilAHitUsesIt) {
  const BattleCard mossby = cardOf(sp::mossby, 1), hushwing = cardOf(sp::hushwing, 2);
  const Outcome o = play(mossby, hushwing, "PPPPPpPP", "PPPPPPGG");
  EXPECT_TRUE(o.turns[5].powered[0]);
  EXPECT_EQ(o.turns[5].clash, Clash::Bonk);
  EXPECT_EQ(o.turns[6].clash, Clash::BWins);
  EXPECT_EQ(o.turns[6].hit[0], 0);
  EXPECT_TRUE(o.turns[6].blocked[0]);
  EXPECT_EQ(o.turns[7].hit[0], 1) << "used up";
}

TEST(Powers, ZapAgainstShellIsAbsorbed) {
  const BattleCard fizz = cardOf(sp::fizz, 1), mossby = cardOf(sp::mossby, 2);
  const Outcome o = play(fizz, mossby, "PPPPPp", "PPPPPp");   // spark's head start: both full on turn 5
  ASSERT_TRUE(o.turns[5].powered[0] && o.turns[5].powered[1]);
  EXPECT_EQ(o.turns[5].hit[1], 0);
  EXPECT_TRUE(o.turns[5].blocked[1]);
  EXPECT_FALSE(o.side[1].shell);
}

TEST(Powers, ZapCostsAHeartWhateverTheClash) {
  const BattleCard fizz = cardOf(sp::fizz, 1), mothwick = cardOf(sp::mothwick, 2);
  const Outcome o = play(fizz, mothwick, "PPPPPPp", "PPPPPPP");
  ASSERT_TRUE(o.turns[6].powered[0]);
  EXPECT_EQ(o.turns[6].clash, Clash::Bonk);
  EXPECT_EQ(o.turns[6].hit[1], 1);
  EXPECT_EQ(o.turns[6].hit[0], 0);
}

TEST(Powers, SporesPutTheFriendToSleepNextTurn) {
  const BattleCard puffkin = cardOf(sp::puffkin, 1), mossby = cardOf(sp::mossby, 2);
  const Outcome o = play(puffkin, mossby, "PPPPPPpT", "PPPPPP.g");
  ASSERT_TRUE(o.turns[6].powered[0]);
  EXPECT_FALSE(o.turns[6].dozed[1]);
  EXPECT_TRUE(o.turns[7].dozed[1]);
  EXPECT_FALSE(o.turns[7].powered[1]) << "a dozing side cannot fire";
  EXPECT_EQ(o.turns[7].clash, Clash::AWins) << "even trick beats a sleeper";
  EXPECT_EQ(o.turns[7].hit[1], 1);
}

TEST(Powers, MendGrowsBackAHeartButNeverPastFull) {
  const Outcome hurt = play(gillyCard, grungoCard, "PPPPPp", "GPPPPP");
  ASSERT_TRUE(hurt.turns[5].powered[0]);
  EXPECT_EQ(hurt.turns[5].healed[0], 1);
  EXPECT_EQ(hurt.side[0].hearts, DuelRules::kHearts);
  const Outcome full = play(gillyCard, grungoCard, "PPPPPPp", "PPPPPPP");
  ASSERT_TRUE(full.turns[6].powered[0]);
  EXPECT_EQ(full.turns[6].healed[0], 0);
  EXPECT_EQ(full.side[0].hearts, DuelRules::kHearts);
}

TEST(Powers, DashWinsHeadBonksForTwoTurns) {
  const BattleCard mudge = cardOf(sp::mudge, 1);
  const Outcome o = play(mudge, gillyCard, "PPPpPP", "PPPPPP");
  ASSERT_TRUE(o.turns[3].powered[0]);
  EXPECT_EQ(o.turns[3].clash, Clash::AWins);
  EXPECT_EQ(o.turns[4].clash, Clash::AWins);
  EXPECT_EQ(o.turns[5].clash, Clash::Bonk);
}

TEST(Powers, HushCancelsLostClashesForTwoTurns) {
  const BattleCard hushwing = cardOf(sp::hushwing, 1), puffkin = cardOf(sp::puffkin, 2);
  const Outcome o = play(hushwing, puffkin, "PPPPPgGG", "PPPPPTTT");
  ASSERT_TRUE(o.turns[5].powered[0]);
  for (uint8_t t : {5, 6}) {
    EXPECT_EQ(o.turns[t].clash, Clash::BWins);
    EXPECT_EQ(o.turns[t].hit[0], 0) << "turn " << +t;
    EXPECT_TRUE(o.turns[t].blocked[0]);
  }
  EXPECT_EQ(o.turns[7].hit[0], 1);
}

TEST(Powers, LanternWinsTheBonkAndDazzlesTheFriendForTwoTurns) {
  const BattleCard mothwick = cardOf(sp::mothwick, 1), fizz = cardOf(sp::fizz, 2);
  const Outcome o = play(mothwick, fizz, "PPPPt", "PPPPT");
  ASSERT_TRUE(o.turns[4].powered[0]);
  EXPECT_EQ(o.turns[4].clash, Clash::AWins);
  EXPECT_EQ(o.side[1].dazzledTurns, 1) << "two turns, one used";
  // A dazzled winner's clash hit misses on its own luck, about half the time.
  int landed = 0;
  for (uint32_t seed = 0; seed < 1000; ++seed)
    landed += play(mothwick, fizz, "PPPPtTT", "PPPPTPP", Stakes::Spar, seed).turns[5].hit[0];
  EXPECT_GT(landed, 400);
  EXPECT_LT(landed, 600);
  for (uint32_t seed = 0; seed < 50; ++seed)
    EXPECT_EQ(play(mothwick, fizz, "PPPPtTTT", "PPPPTPPP", Stakes::Spar, seed).turns[7].hit[0], 1) << "worn off";
}

TEST(Powers, GadgetDrawsEachOfTheOtherEightAndNeverItself) {
  const BattleCard tinker = cardOf(sp::tinker, 0x7111);
  int drawn[kPowerCount]{};
  for (uint32_t seed = 0; seed < 4000; ++seed) {
    const Outcome o = play(tinker, cardOf(sp::fizz, 2), "PPPPPp", "PPPPPP", Stakes::Spar, seed);
    ASSERT_TRUE(o.turns[5].powered[0]);
    ++drawn[o.turns[5].fired[0].v];
  }
  EXPECT_EQ(drawn[power::gadget.v], 0);
  for (const PowerInfo& p : POWERS)
    if (p.id != power::gadget) { EXPECT_NEAR(drawn[p.id.v], 500, 100) << p.name; }
}

// ---- endings, downed and the verdict --------------------------------------------------

TEST(Verdict, AMortalLoserIsDownedAndTheWinnersSlotDecides) {
  const BattleCard mudge = cardOf(sp::mudge, 3), hushwing = cardOf(sp::hushwing, 4);
  const Outcome pending = play(mudge, hushwing, "PPPPP", "TTTTT", Stakes::Mortal);
  EXPECT_EQ(pending.ending, Ending::AWon);
  EXPECT_TRUE(pending.downed[1]);
  EXPECT_FALSE(pending.downed[0]);
  EXPECT_EQ(pending.resolved, 5);
  EXPECT_TRUE(pending.awaitingVerdict());
  EXPECT_EQ(play(mudge, hushwing, "PPPPPF", "TTTTT", Stakes::Mortal).verdict, Verdict::Finish);
  EXPECT_EQ(play(mudge, hushwing, "PPPPPS", "TTTTT", Stakes::Mortal).verdict, Verdict::Spare);
  const Outcome loserSpeaks = play(mudge, hushwing, "PPPPP", "TTTTTF", Stakes::Mortal);
  EXPECT_EQ(loserSpeaks.verdict, Verdict::None) << "only the winner's slot counts";
  EXPECT_TRUE(loserSpeaks.awaitingVerdict());
  const Outcome onTurnMove = play(mudge, hushwing, "PPPPF", "TTTTT", Stakes::Mortal);
  EXPECT_TRUE(onTurnMove.downed[1]);
  EXPECT_EQ(onTurnMove.verdict, Verdict::None) << "a verdict on a turn move means nothing";
}

TEST(Verdict, ReadFromTheSixteenthSlotAfterADowningOnTurnFifteen) {
  const char* winner = "PPPPPPPPPPPPPPPF";
  const char* loser = "PPPPPPPPPPTTTTTS";
  const Outcome o = play(gillyCard, grungoCard, winner, loser, Stakes::Mortal);
  EXPECT_EQ(o.resolved, DuelRules::kMaxTurns);
  EXPECT_TRUE(o.downed[1]);
  EXPECT_EQ(o.verdict, Verdict::Finish);
  EXPECT_EQ(resolve(grungoCard, gillyCard, 1, Stakes::Mortal, logOf(loser), logOf(winner)).verdict, Verdict::Finish)
      << "whichever side the winner is";
}

TEST(Verdict, NobodyIsDownedInASparOrAScrapOrOnPoints) {
  for (Stakes s : {Stakes::Spar, Stakes::Scrap}) {
    const Outcome o = play(gillyCard, grungoCard, "PPPPPF", "TTTTT", s);
    EXPECT_EQ(o.ending, Ending::AWon);
    EXPECT_FALSE(o.downed[0] || o.downed[1]);
    EXPECT_FALSE(o.awaitingVerdict());
    EXPECT_EQ(o.verdict, Verdict::None);
  }
  const Outcome points = play(gillyCard, grungoCard, "PPPPPPPPPPPPPPPF", "TPPPPPPPPPPPPPP", Stakes::Mortal);
  EXPECT_EQ(points.resolved, DuelRules::kMaxTurns);
  EXPECT_EQ(points.ending, Ending::AWon);
  EXPECT_FALSE(points.downed[0] || points.downed[1]);
  EXPECT_EQ(points.verdict, Verdict::None);
}

TEST(Ending, NoTurnIsReadPastTheEnd) {
  const Outcome o = play(gillyCard, grungoCard, "PPPPPPPP", "TTTTTGGG");
  EXPECT_EQ(o.resolved, 5);
  EXPECT_EQ(o.side[1].hearts, 0);
}

// ---- what the Dish wounds from ------------------------------------------------------------

TEST(TurnHits, HeartsKindAndLuckPerTurn) {
  const BattleCard fizz = cardOf(sp::fizz, 1), mothwick = cardOf(sp::mothwick, 2);
  const Outcome o = play(fizz, mothwick, "PPPPPPpT", "TPPPPPPG");
  TurnHit hits[DuelRules::kMaxTurns]{};
  turnHits(o, Side::B, 1, mothwick, fizz, hits);
  EXPECT_EQ(hits[0].hearts, 1);
  EXPECT_EQ(hits[0].kind, Scar::Kind::Claw) << "a pounce claws";
  EXPECT_EQ(hits[6].hearts, 1);
  EXPECT_EQ(hits[6].kind, Scar::Kind::Ring) << "a zap rings";
  EXPECT_EQ(hits[7].hearts, 1);
  EXPECT_EQ(hits[7].kind, Scar::Kind::Scorch) << "a spark pet's trick scorches";
  EXPECT_EQ(hits[1].hearts, 0);
  EXPECT_NE(hits[0].luck, hits[6].luck);
}

// ---- the pet's mind -----------------------------------------------------------------------

TEST(Mind, ThePetLearnsToCounterAFriendsHabit) {
  DuelSkill skill{};
  const BattleCard mine = grungoCard, theirs = cardOf(sp::fizz, 2);   // grungo's knack is trick, not guard
  MoveLog ma, mb;
  for (uint8_t t = 0; t < 6; ++t) {
    ma.append(Move{Stance::Trick});
    mb.append(Move{Stance::Pounce});
    const Outcome o = resolve(mine, theirs, 1, Stakes::Spar, ma, mb);
    learnTurn(skill, o, Side::A, theirs, uint8_t(o.resolved - 1));
  }
  const Outcome o = resolve(mine, theirs, 1, Stakes::Spar, ma, mb);
  int guard = 0;
  for (uint32_t seed = 0; seed < 100; ++seed)
    guard += petChoice(skill, o, Side::A, mine, theirs, 0, seed).stance == Stance::Guard;
  EXPECT_GE(guard, 95) << "after a pounce, against fizz, guard";
}

TEST(Mind, AFreshPetLeansToItsKnackAndTheAssistBrightensTheWhisper) {
  const DuelSkill fresh{};
  const Outcome o = play(gillyCard, grungoCard, "", "");
  int best = 0;
  for (uint32_t seed = 0; seed < 300; ++seed)
    best += petChoice(fresh, o, Side::A, gillyCard, grungoCard, 0, seed).stance == gillyCard.knack.best();
  EXPECT_GT(best, 120);
  EXPECT_LT(best, 300) << "a lean, not a rule";
  EXPECT_GT(petChoice(fresh, o, Side::A, gillyCard, grungoCard, 2, 5).whisper,
            petChoice(fresh, o, Side::A, gillyCard, grungoCard, 0, 5).whisper);
}

TEST(Mind, ThePetNeverSeesAnUnpairedMove) {
  const DuelSkill fresh{};
  const Outcome paired = play(gillyCard, grungoCard, "PG", "TT");
  const Outcome ahead = play(gillyCard, grungoCard, "PG", "TTP");
  EXPECT_EQ(paired.hash(), ahead.hash());
  for (uint32_t seed = 0; seed < 50; ++seed)
    EXPECT_EQ(petChoice(fresh, paired, Side::A, gillyCard, grungoCard, 0, seed).stance,
              petChoice(fresh, ahead, Side::A, gillyCard, grungoCard, 0, seed).stance);
}

TEST(Foretell, GrungoReadsATwoTurnPatternTheOthersMiss) {
  // The friend plays P P G, P P G, P P: a one-turn reader sees "after P" split
  // between P and G; a two-turn reader sees "after P P" always G.
  const BattleCard friendCard = cardOf(sp::mudge, 9);
  const BattleCard seerSide = cardOf(sp::gilly, 8, 100);   // two assist hearts, so six lost turns leave one
  const Outcome o = play(seerSide, cardOf(sp::mudge, 9, 200), "TTTTTTTT", "PPGPPGPP");
  ASSERT_EQ(o.resolved, 8);
  EXPECT_EQ(foretell(o, Side::B, cardOf(sp::grungo, 8), friendCard, 3).stance, Stance::Guard);
  EXPECT_EQ(foretell(o, Side::B, cardOf(sp::gilly, 8), friendCard, 3).stance, Stance::Pounce);
  const Foretelling f = foretell(o, Side::B, cardOf(sp::grungo, 8), friendCard, 3);
  EXPECT_GT(f.confidence, Fx::zero());
  EXPECT_LE(f.confidence, Fx::one());
}

// ---- the wheel ----------------------------------------------------------------------------

TEST(Balance, TheWheelHoldsAndMatchesTheProbe) {
  using namespace blorbtool;
  const BalanceReport r = tournament(1500, {Player::Random, Player::Random, Arming::Always, 0}, 1);
  EXPECT_TRUE(wheelHolds(r)) << "worst pair " << SPECIES[r.worstA].name << " over " << SPECIES[r.worstB].name << " "
                             << r.worstPermille;
  for (uint8_t s = 0; s < kSpeciesCount; ++s) {
    EXPECT_GE(r.speciesPermille[s], 430) << SPECIES[s].name;
    EXPECT_LE(r.speciesPermille[s], 570) << SPECIES[s].name;
    EXPECT_NEAR(r.speciesPermille[s], kProbePermille[s], 30) << SPECIES[s].name << " strays from roster_probe.py";
  }
  EXPECT_GE(r.medianTurns, 8);
  EXPECT_LE(r.medianTurns, 14);
}

TEST(Balance, AReaderBeatsAHabitKid) {
  using namespace blorbtool;
  const BalanceReport r = tournament(300, {Player::Reader, Player::Habit, Arming::Always, 0}, 2);
  EXPECT_GE(r.sideAPermille, 700) << "skill matters";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
