#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <string>
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
    std::swap(r.bracing[0], r.bracing[1]); std::swap(r.special[0], r.special[1]);
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

// How many lost clashes down an unassisted pet, and a run of one move.
constexpr uint8_t kDown = (DuelRules::kHearts + DuelRules::kClashHearts - 1) / DuelRules::kClashHearts;
std::string run(char c, int n) { return std::string(size_t(n), c); }
Outcome play(const BattleCard& a, const BattleCard& b, const std::string& ma, const std::string& mb,
             Stakes s = Stakes::Spar, uint32_t seed = 1) {
  return play(a, b, ma.c_str(), mb.c_str(), s, seed);
}

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
  // Either log may run ahead (a committed special writes up to three turns at
  // once), and what the other has not reached is never read.
  Random r{{0x9F}};
  for (int k = 0; k < 2000; ++k) {
    const BattleCard a = r.card(), b = r.card();
    const uint32_t seed = r.rng.next();
    const MoveLog ma = r.log(15), mb = r.log(15);
    const Outcome full = resolve(a, b, seed, Stakes::Scrap, ma, mb);
    for (uint8_t n = 0; n < 15; ++n)
      for (uint8_t cut = 0; cut < 3; ++cut) {
        MoveLog pa, pb;
        pa.adopt(ma.packed(), cut == 1 ? ma.count() : n);
        pb.adopt(mb.packed(), cut == 2 ? mb.count() : n);
        const Outcome part = resolve(a, b, seed, Stakes::Scrap, pa, pb);
        ASSERT_LE(part.resolved, n);
        for (uint8_t t = 0; t < part.resolved; ++t)
          ASSERT_EQ(std::memcmp(&part.turns[t], &full.turns[t], sizeof(TurnReport)), 0)
              << "battle " << k << " turn " << +t << " cut " << +cut;
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
        ASSERT_LE(s.maxHearts, DuelRules::kHearts + DuelRules::kMaxAssistSteps * DuelRules::kAssistStepHearts);
        ASSERT_LE(s.spark, kMeterMax);
      }
    }
  }
}

TEST(Meter, AShortMeterPlaysPlain) {
  // Turn 0: nobody's meter holds a cost, so a commit is the plain stance its
  // entry carries, and its landing entry is a plain stance too.
  const Outcome o = play(grungoCard, gillyCard, "qg", "GT");
  EXPECT_FALSE(o.turns[0].bracing[0]);
  EXPECT_EQ(o.turns[0].special[0], TurnReport::Special::None);
  EXPECT_EQ(o.side[0].queued, Windup::None);
  EXPECT_EQ(o.turns[0].clash, Clash::BWins) << "the commit's placeholder pounce meets a guard";
  EXPECT_EQ(o.turns[1].clash, Clash::BWins) << "the landing entry's guard meets a trick";
  EXPECT_FALSE(o.turns[1].powered[0]);
}

TEST(MoveLog, AppendSpecialWritesTheCommitAndRefusesALandingPastTheEnd) {
  MoveLog l = logOf("PPPPPPPPPPPPP");
  EXPECT_FALSE(l.appendSpecial(Windup::Heavy, Stance::Guard)) << "it would land on turn 16";
  EXPECT_EQ(l.count(), 13);
  EXPECT_TRUE(l.appendSpecial(Windup::Quick, Stance::Guard)) << "it lands on turn 15";
  ASSERT_EQ(l.count(), 15);
  EXPECT_EQ(l.at(13).windup, Windup::Quick);
  EXPECT_EQ(l.at(13).stance, Stance::Pounce) << "the commit leaks nothing";
  EXPECT_EQ(l.at(14).stance, Stance::Guard);
  EXPECT_TRUE(l.at(14).power);
  MoveLog h;
  EXPECT_TRUE(h.appendSpecial(Windup::Heavy, Stance::Trick));
  ASSERT_EQ(h.count(), 3);
  EXPECT_EQ(h.at(0).windup, Windup::Heavy);
  EXPECT_EQ(h.at(1).windup, Windup::None);
  EXPECT_EQ(h.at(1).stance, Stance::Pounce);
  EXPECT_EQ(h.at(2).stance, Stance::Trick);
}

TEST(Start, AssistHeartsAndTheElementHeadStart) {
  const Outcome o = play(cardOf(sp::grungo, 1, 200), cardOf(sp::fizz, 2, 100), "", "");
  EXPECT_EQ(o.side[0].maxHearts, DuelRules::kHearts);
  EXPECT_EQ(o.side[1].maxHearts, DuelRules::kHearts + DuelRules::kMaxAssistSteps * DuelRules::kAssistStepHearts);
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

// ---- the queued special --------------------------------------------------------------

// Mudge's dash costs 3: three head bonks fill it.
constexpr BattleCard kMudge = cardOf(sp::mudge, 0x31);

TEST(Special, AQuickBracesOneTurnThenLandsForTheQuickHearts) {
  const Outcome o = play(kMudge, gillyCard, "PPPqp", "PPPGT");
  EXPECT_EQ(o.turns[3].special[0], TurnReport::Special::Committed);
  EXPECT_TRUE(o.turns[3].bracing[0]);
  EXPECT_EQ(o.turns[3].clash, Clash::Bonk) << "a guard does nothing to a bracer";
  EXPECT_EQ(o.turns[3].hit[0] + o.turns[3].hit[1], 0);
  EXPECT_EQ(o.turns[4].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(o.turns[4].hit[1], SpecialRules::hearts(Windup::Quick));
  EXPECT_TRUE(o.turns[4].powered[0]);
  EXPECT_EQ(o.side[0].queued, Windup::None);
}

TEST(Special, AHeavyBracesTwoTurnsThenLandsForTheHeavyHearts) {
  const Outcome o = play(kMudge, gillyCard, "PPPh_p", "PPPPGT");
  EXPECT_TRUE(o.turns[3].bracing[0]);
  EXPECT_TRUE(o.turns[4].bracing[0]);
  EXPECT_EQ(o.turns[3].clash, Clash::Bonk) << "a pounce bonks off a bracer";
  EXPECT_EQ(o.turns[5].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(o.turns[5].hit[1], SpecialRules::hearts(Windup::Heavy));
}

TEST(Special, ATieGoesToTheSpecial) {
  const Outcome o = play(kMudge, gillyCard, "PPPqg", "PPPPG");
  EXPECT_EQ(o.turns[4].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(o.turns[4].hit[1], SpecialRules::hearts(Windup::Quick));
}

TEST(Special, ACounteredLandingIsRipostedAndThePowerDoesNotFire) {
  const Outcome o = play(kMudge, gillyCard, "PPPh_p", "PPPPPG");
  EXPECT_EQ(o.turns[5].special[0], TurnReport::Special::Riposted);
  EXPECT_EQ(o.turns[5].hit[0], SpecialRules::hearts(Windup::Heavy));
  EXPECT_EQ(o.turns[5].hit[1], 0);
  EXPECT_FALSE(o.turns[5].powered[0]);
  EXPECT_EQ(o.side[0].bonkTurns, 0);
}

TEST(Special, ATrickStaggersABracerAndAStaggeredQuickLandsPlain) {
  const Outcome o = play(kMudge, gillyCard, "PPPqp", "PPPTT");
  EXPECT_EQ(o.turns[3].special[0], TurnReport::Special::Staggered);
  EXPECT_EQ(o.turns[3].clash, Clash::BWins);
  EXPECT_EQ(o.turns[3].hit[0], DuelRules::kClashHearts);
  EXPECT_FALSE(o.turns[4].bracing[0]);
  EXPECT_EQ(o.turns[4].special[0], TurnReport::Special::None);
  EXPECT_EQ(o.turns[4].clash, Clash::AWins) << "the landing entry's pounce, played plain";
  EXPECT_EQ(o.turns[4].hit[1], DuelRules::kClashHearts) << "a clash's hearts, not the special's";
  EXPECT_FALSE(o.turns[4].powered[0]);
}

TEST(Special, AStaggeredHeavyDropsToQuickStrength) {
  const Outcome o = play(kMudge, gillyCard, "PPPh_p", "PPPTPT");
  EXPECT_EQ(o.turns[3].special[0], TurnReport::Special::Staggered);
  EXPECT_TRUE(o.turns[4].bracing[0]) << "still winding up";
  EXPECT_EQ(o.turns[5].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(o.turns[5].hit[1], SpecialRules::hearts(Windup::Quick));
}

TEST(Special, AsleepOnItsLandingTurnTheSpecialIsLost) {
  // Puffkin's spores land on a mossby who has just committed his own quick, so
  // he sleeps through the turn his shell would have landed.
  const BattleCard puffkin = cardOf(sp::puffkin, 1), mossby = cardOf(sp::mossby, 2);
  const Outcome o = play(puffkin, mossby, "PPPPPPqpT", "PPPPPPPqg");
  ASSERT_EQ(o.turns[7].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(o.turns[7].special[1], TurnReport::Special::Committed);
  EXPECT_TRUE(o.turns[8].dozed[1]);
  EXPECT_FALSE(o.turns[8].powered[1]);
  EXPECT_EQ(o.side[1].queued, Windup::None);
  EXPECT_EQ(o.turns[8].hit[1], DuelRules::kClashHearts) << "a trick beats a sleeper";
}

// ---- one case per power -------------------------------------------------------------

TEST(Powers, ForeseeWhiffsWhereOthersAreRiposted) {
  const Outcome foresee = play(grungoCard, gillyCard, "PPPPPqp", "PPPPPGG");
  EXPECT_EQ(foresee.turns[6].special[0], TurnReport::Special::Whiffed);
  EXPECT_EQ(foresee.turns[6].hit[0] + foresee.turns[6].hit[1], 0);
  const Outcome mend = play(gillyCard, grungoCard, "PPPPPPqp", "PPPPPPGG");
  EXPECT_EQ(mend.turns[7].special[0], TurnReport::Special::Riposted);
  EXPECT_EQ(mend.turns[7].hit[0], SpecialRules::hearts(Windup::Quick));
}

TEST(Powers, ShellStaysUpUntilAHitUsesIt) {
  const BattleCard mossby = cardOf(sp::mossby, 1), hushwing = cardOf(sp::hushwing, 2);
  const Outcome o = play(mossby, hushwing, "PPPPPqgPP", "PPPPPPPGG");
  ASSERT_EQ(o.turns[6].special[0], TurnReport::Special::Landed);
  EXPECT_TRUE(o.side[0].shell == false) << "used on turn 7";
  EXPECT_EQ(o.turns[7].clash, Clash::BWins);
  EXPECT_EQ(o.turns[7].hit[0], std::max(0, DuelRules::kClashHearts - PowerRules::kShellHearts));
  EXPECT_TRUE(o.turns[7].blocked[0]);
  EXPECT_EQ(o.turns[8].hit[0], DuelRules::kClashHearts);
}

TEST(Powers, ZapAddsAHeartToTheLanding) {
  const BattleCard fizz = cardOf(sp::fizz, 1), mothwick = cardOf(sp::mothwick, 2);
  const Outcome o = play(fizz, mothwick, "PPPPPPqp", "PPPPPPPT");
  ASSERT_EQ(o.turns[7].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(o.turns[7].hit[1], SpecialRules::hearts(Windup::Quick) + PowerRules::kZapHearts);
}

TEST(Powers, ZapAgainstShellLosesOneHeartToIt) {
  // Mossby's shell lands on fizz while fizz winds up; fizz's zap lands next.
  const BattleCard fizz = cardOf(sp::fizz, 1), mossby = cardOf(sp::mossby, 2);
  const Outcome o = play(fizz, mossby, "PPPPPPqp", "PPPPPqgP");
  ASSERT_EQ(o.turns[6].special[1], TurnReport::Special::Landed);
  ASSERT_EQ(o.turns[7].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(o.turns[7].hit[1], SpecialRules::hearts(Windup::Quick) + PowerRules::kZapHearts - PowerRules::kShellHearts);
  EXPECT_TRUE(o.turns[7].blocked[1]);
  EXPECT_FALSE(o.side[1].shell);
}

TEST(Powers, SporesPutTheFriendToSleepNextTurn) {
  const BattleCard puffkin = cardOf(sp::puffkin, 1), mossby = cardOf(sp::mossby, 2);
  const Outcome o = play(puffkin, mossby, "PPPPPPqpT", "PPPPPPPTg");
  ASSERT_EQ(o.turns[7].special[0], TurnReport::Special::Landed);
  EXPECT_TRUE(o.turns[8].dozed[1]);
  EXPECT_EQ(o.turns[8].clash, Clash::AWins) << "even trick beats a sleeper";
  EXPECT_EQ(o.turns[8].hit[1], DuelRules::kClashHearts);
}

TEST(Powers, MendGrowsBackAHeartButNeverPastFull) {
  const Outcome hurt = play(gillyCard, grungoCard, "PPPPPqp", "GPPPPPT");
  ASSERT_EQ(hurt.turns[6].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(hurt.turns[6].healed[0], std::min(PowerRules::kMendHearts, DuelRules::kClashHearts));
  EXPECT_EQ(hurt.side[0].hearts, DuelRules::kHearts);
  const Outcome full = play(gillyCard, grungoCard, "PPPPPPqp", "PPPPPPPT");
  ASSERT_EQ(full.turns[7].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(full.turns[7].healed[0], 0);
}

TEST(Powers, DashWinsTheNextHeadBonk) {
  const BattleCard gilly = cardOf(sp::gilly, 0x62, 60);   // assisted, so she stands long enough to see it wear off
  const Outcome o = play(kMudge, gilly, "PPPqpPP", "PPPGTPP");
  ASSERT_EQ(o.resolved, 7);
  ASSERT_EQ(o.turns[4].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(o.turns[5].clash, Clash::AWins);
  EXPECT_EQ(o.turns[5].hit[1], DuelRules::kClashHearts);
  EXPECT_EQ(o.turns[6].clash, Clash::Bonk);
}

TEST(Powers, HushWhiffsTheNextSpecialAndCancelsTheNextLostClash) {
  const BattleCard hushwing = cardOf(sp::hushwing, 1), puffkin = cardOf(sp::puffkin, 2);
  const Outcome whiff = play(hushwing, puffkin, "PPPPPqpP", "PPPPPPqp");
  ASSERT_EQ(whiff.turns[6].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(whiff.turns[7].special[1], TurnReport::Special::Whiffed);
  EXPECT_EQ(whiff.turns[7].hit[0], 0);
  const Outcome clash = play(hushwing, puffkin, "PPPPPqpGG", "PPPPPPTTT");
  ASSERT_EQ(clash.turns[6].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(clash.turns[7].hit[0], 0);
  EXPECT_TRUE(clash.turns[7].blocked[0]);
  EXPECT_EQ(clash.turns[8].hit[0], DuelRules::kClashHearts) << "worn off";
}

TEST(Powers, BlackBellUsesItsTwoTurnHush) {
  const BattleCard octopus = cardOf(sp::octopus, 1), puffkin = cardOf(sp::puffkin, 2);
  const Outcome whiff = play(octopus, puffkin, "PPPPPqpP", "PPPPPPqp");
  ASSERT_EQ(whiff.turns[6].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(whiff.turns[7].special[1], TurnReport::Special::Whiffed);
  EXPECT_EQ(whiff.turns[7].hit[0], 0);
  const Outcome clash = play(octopus, puffkin, "PPPPPqpGG", "PPPPPPTTT");
  ASSERT_EQ(clash.turns[6].special[0], TurnReport::Special::Landed);
  EXPECT_EQ(clash.turns[7].hit[0], 0);
  EXPECT_TRUE(clash.turns[7].blocked[0]);
  EXPECT_EQ(clash.turns[8].hit[0], DuelRules::kClashHearts) << "the Black Bell's hush has worn off";
}

TEST(Powers, LanternDazzlesTheFriendNextTurn) {
  const BattleCard mothwick = cardOf(sp::mothwick, 1), fizz = cardOf(sp::fizz, 2);
  int landed = 0;
  for (uint32_t seed = 0; seed < 1000; ++seed) {
    const Outcome o = play(mothwick, fizz, "PPPqtTT", "PPPPGPP", Stakes::Spar, seed);
    ASSERT_EQ(o.turns[4].special[0], TurnReport::Special::Landed);
    landed += o.turns[5].hit[0] > 0;
    ASSERT_EQ(o.turns[6].hit[0], DuelRules::kClashHearts) << "worn off";
  }
  EXPECT_GT(landed, 400);
  EXPECT_LT(landed, 600);
}

TEST(Powers, GadgetDrawsEachOfTheOtherEightAndNeverItself) {
  const BattleCard tinker = cardOf(sp::tinker, 0x7111);
  int drawn[kPowerCount]{};
  for (uint32_t seed = 0; seed < 4000; ++seed) {
    const Outcome o = play(tinker, cardOf(sp::fizz, 2), "PPPPPqp", "PPPPPPT", Stakes::Spar, seed);
    ASSERT_TRUE(o.turns[6].powered[0]);
    ++drawn[o.turns[6].fired[0].v];
  }
  EXPECT_EQ(drawn[power::gadget.v], 0);
  for (const PowerInfo& p : POWERS) {
    if (p.id != power::gadget) { EXPECT_NEAR(drawn[p.id.v], 500, 100) << p.name; }
  }
}

// ---- endings, downed and the verdict --------------------------------------------------

TEST(Verdict, AMortalLoserIsDownedAndTheWinnersSlotDecides) {
  const BattleCard mudge = cardOf(sp::mudge, 3), hushwing = cardOf(sp::hushwing, 4);
  const std::string win = run('P', kDown), lose = run('T', kDown);
  const Outcome pending = play(mudge, hushwing, win, lose, Stakes::Mortal);
  EXPECT_EQ(pending.ending, Ending::AWon);
  EXPECT_TRUE(pending.downed[1]);
  EXPECT_FALSE(pending.downed[0]);
  EXPECT_EQ(pending.resolved, kDown);
  EXPECT_TRUE(pending.awaitingVerdict());
  EXPECT_EQ(play(mudge, hushwing, win + "F", lose, Stakes::Mortal).verdict, Verdict::Finish);
  EXPECT_EQ(play(mudge, hushwing, win + "S", lose, Stakes::Mortal).verdict, Verdict::Spare);
  const Outcome loserSpeaks = play(mudge, hushwing, win, lose + "F", Stakes::Mortal);
  EXPECT_EQ(loserSpeaks.verdict, Verdict::None) << "only the winner's slot counts";
  EXPECT_TRUE(loserSpeaks.awaitingVerdict());
  const Outcome onTurnMove = play(mudge, hushwing, run('P', kDown - 1) + "F", lose, Stakes::Mortal);
  EXPECT_TRUE(onTurnMove.downed[1]);
  EXPECT_EQ(onTurnMove.verdict, Verdict::None) << "a verdict on a turn move means nothing";
}

TEST(Verdict, ReadFromTheSixteenthSlotAfterADowningOnTurnFifteen) {
  const std::string winner = run('P', DuelRules::kMaxTurns) + "F";
  const std::string loser = run('P', DuelRules::kMaxTurns - kDown) + run('T', kDown) + "S";
  const Outcome o = play(gillyCard, grungoCard, winner, loser, Stakes::Mortal);
  EXPECT_EQ(o.resolved, DuelRules::kMaxTurns);
  EXPECT_TRUE(o.downed[1]);
  EXPECT_EQ(o.verdict, Verdict::Finish);
  EXPECT_EQ(resolve(grungoCard, gillyCard, 1, Stakes::Mortal, logOf(loser.c_str()), logOf(winner.c_str())).verdict,
            Verdict::Finish)
      << "whichever side the winner is";
}

TEST(Verdict, NobodyIsDownedInASparOrAScrapOrOnPoints) {
  for (Stakes s : {Stakes::Spar, Stakes::Scrap}) {
    const Outcome o = play(gillyCard, grungoCard, run('P', kDown) + "F", run('T', kDown), s);
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
  const Outcome o = play(gillyCard, grungoCard, run('P', kDown + 3), run('T', kDown) + "GGG");
  EXPECT_EQ(o.resolved, kDown);
  EXPECT_EQ(o.side[1].hearts, 0);
}

// ---- what the Dish wounds from ------------------------------------------------------------

TEST(TurnHits, HeartsKindAndLuckPerTurn) {
  const BattleCard fizz = cardOf(sp::fizz, 1), mothwick = cardOf(sp::mothwick, 2, 60);   // assisted: still up for turn 8
  const Outcome o = play(fizz, mothwick, "PPPPPPqpT", "TPPPPPPTG");
  ASSERT_EQ(o.resolved, 9);
  TurnHit hits[DuelRules::kMaxTurns]{};
  turnHits(o, Side::B, 1, mothwick, fizz, hits);
  EXPECT_EQ(hits[0].hearts, DuelRules::kClashHearts);
  EXPECT_EQ(hits[0].kind, Scar::Kind::Claw) << "a pounce claws";
  EXPECT_EQ(hits[1].hearts, 0);
  EXPECT_EQ(hits[7].hearts, SpecialRules::hearts(Windup::Quick) + PowerRules::kZapHearts) << "a quick landing and its zap";
  EXPECT_EQ(hits[7].kind, Scar::Kind::Bite) << "a zap bites";
  EXPECT_EQ(hits[8].hearts, DuelRules::kClashHearts);
  EXPECT_EQ(hits[8].kind, Scar::Kind::Scorch) << "a spark pet's trick scorches";
  EXPECT_NE(hits[0].luck, hits[7].luck);
}

// ---- the pet's mind -----------------------------------------------------------------------

TEST(Mind, ThePetLearnsToCounterAFriendsHabit) {
  DuelSkill skill{};
  const BattleCard mine = grungoCard, theirs = cardOf(sp::fizz, 2);   // grungo's knack is trick, not guard
  MoveLog ma, mb;
  for (uint8_t t = 0; t < 6; ++t) {
    ma.append(Move{Stance::Pounce});   // head bonks: nobody falls, and every cell of the context still learns
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

TEST(Mind, ThePetLeansTrickAtABracerAndTheCounterAtALanding) {
  const DuelSkill fresh{};
  const Outcome bracing = play(gillyCard, kMudge, "PPPP", "PPPh_p");   // the commit shows once turn 3 resolves
  ASSERT_TRUE(bracing.side[1].queued != Windup::None);
  EXPECT_EQ(petChoice(fresh, bracing, Side::A, gillyCard, kMudge, 0, 1).stance, Stance::Trick);
  const Outcome landing = play(gillyCard, kMudge, "PPPPP", "PPPh_p");
  ASSERT_EQ(landing.resolved, 5);
  const Foretelling f = foretell(landing, Side::B, gillyCard, kMudge, 1);
  EXPECT_EQ(petChoice(fresh, landing, Side::A, gillyCard, kMudge, 0, 1).stance, counterOf(f.stance));
}

TEST(Mind, TheAutopilotQueuesOnAFullMeterAndASpiritedPetGoesHeavy) {
  const DuelSkill fresh{};
  BattleCard calm = gillyCard, spirited = gillyCard;
  spirited.mood.spirit = 220;
  const Outcome full = play(calm, grungoCard, "PPPPPP", "PPPPPP");
  ASSERT_EQ(full.side[0].spark, kMeterMax);
  EXPECT_EQ(petChoice(fresh, full, Side::A, calm, grungoCard, 0, 1).special, Windup::Quick);
  EXPECT_EQ(petChoice(fresh, full, Side::A, spirited, grungoCard, 0, 1).special, Windup::Heavy);
  const Outcome empty = play(calm, grungoCard, "P", "P");
  EXPECT_EQ(petChoice(fresh, empty, Side::A, calm, grungoCard, 0, 1).special, Windup::None);
}

TEST(Foretell, GrungoReadsATwoTurnPatternTheOthersMiss) {
  // The friend plays P P G, P P G, P P: a one-turn reader sees "after P" split
  // between P and G; a two-turn reader sees "after P P" always G.
  const BattleCard friendCard = cardOf(sp::mudge, 9);
  const BattleCard seerSide = cardOf(sp::gilly, 8);   // pouncing: bonks, and two lost turns to the guards
  const Outcome o = play(seerSide, cardOf(sp::mudge, 9), "PPPPPPPP", "PPGPPGPP");
  ASSERT_EQ(o.resolved, 8);
  EXPECT_EQ(foretell(o, Side::B, cardOf(sp::grungo, 8), friendCard, 3).stance, Stance::Guard);
  EXPECT_EQ(foretell(o, Side::B, cardOf(sp::gilly, 8), friendCard, 3).stance, Stance::Pounce);
  const Foretelling f = foretell(o, Side::B, cardOf(sp::grungo, 8), friendCard, 3);
  EXPECT_GT(f.confidence, Fx::zero());
  EXPECT_LE(f.confidence, Fx::one());
}

// ---- the wheel ----------------------------------------------------------------------------

TEST(Balance, TheWheelHoldsAndDuelsAreShort) {
  using namespace blorbtool;
  const BalanceReport r = tournament(800, {Player::Random, Player::Random, 0}, 1);
  EXPECT_TRUE(wheelHolds(r)) << "worst pair " << SPECIES[r.worstA].name << " over " << SPECIES[r.worstB].name << " "
                             << r.worstPermille;
  for (uint8_t s = 0; s < kSpeciesCount; ++s) {
    EXPECT_GE(r.speciesPermille[s], kSpeciesLo) << SPECIES[s].name;
    EXPECT_LE(r.speciesPermille[s], kSpeciesHi) << SPECIES[s].name;
  }
  EXPECT_TRUE(lengthHolds(r)) << "median " << int(r.medianTurns) << ", p90 " << int(r.p90Turns);
}

TEST(Balance, AReaderBeatsAHabitKid) {
  using namespace blorbtool;
  const BalanceReport r = tournament(300, {Player::Reader, Player::Habit, 0}, 2);
  EXPECT_GE(r.sideAPermille, 560) << "skill matters";
}

// The specials are worth a kid's press: a kid who queues beats one who never does.
TEST(Balance, TheSpecialsAreWorthUsing) {
  using namespace blorbtool;
  EXPECT_GE(tournament(200, {Player::Random, Player::Plain, 0}, 4).sideAPermille, 520);
  EXPECT_GE(tournament(200, {Player::Queuer, Player::Plain, 0}, 4).sideAPermille, 520);
}

// The help for a younger kid still helps: each assist step lowers a reading
// parent's rate, and a kid who follows the pet's whisper half the time holds
// that parent near even with no help at all.
TEST(Balance, TheAssistAndTheWhisperStillHelp) {
  using namespace blorbtool;
  for (const Player kid : {Player::Habit, Player::LeanHalf}) {
    uint16_t last = 1000;
    for (uint8_t steps = 0; steps <= DuelRules::kMaxAssistSteps; ++steps) {
      const uint16_t parent = tournament(200, {Player::Reader, kid, steps}, 2).sideAPermille;
      EXPECT_LT(parent + 40, last) << "each step is worth at least 40 permille, step " << int(steps);
      last = parent;
    }
  }
  EXPECT_LE(tournament(200, {Player::Reader, Player::LeanHalf, 0}, 2).sideAPermille, 520);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
