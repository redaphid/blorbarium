// The lever that tunes the wheel: every species pair (81, mirrors included)
// played many times through the real resolve() by scripted players, printed
// as the numbers BATTLES.md quotes. Run it by hand while tuning powers.def
// costs (WSL survivor, from the checkout):
//
//   g++ -std=c++17 -O2 -Ilib/blorb/include -o /tmp/duel_balance tools/duel_balance.cpp
//       lib/blorb/src/duel_resolve.cpp lib/blorb/src/duel_mind.cpp     (one command)
//   /tmp/duel_balance [battles per pair]
//
// test_duel_resolve includes this file with DUEL_BALANCE_NO_MAIN and holds
// the same gate, so a row change that tips the wheel fails the suite.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "blorb/duel.h"

namespace blorbtool {
using namespace blorb;

// The players are special_probe.py's, so the two can be compared.
enum class Player : uint8_t {
  Random,     // a six-year-old mashing: uniform stances, queues on 70% of turns (quick or heavy)
  Habit,      // the pet's loved stance 60% of the time, else uniform; always queues quick: readable
  Reader,     // a parent: staggers a bracing friend, counters a landing, else counters the
              // friend's most frequent stance and queues it (heavy half the time)
  Pet,        // petChoice() every turn, learning as it goes from a fresh Skill: the autopilot
  LeanHalf,   // takes the pet's whispered lean half the time, else plays Habit
  Queuer,     // queues whenever it can (heavy half the time), staggers a bracing friend
};

struct Matchup {
  Player a = Player::Random, b = Player::Random;
  uint8_t assistB = 0;   // extra hearts for side B, through its kid's lower prowess
};

struct Pick { Stance stance; Windup special = Windup::None; };

struct Rng {
  uint64_t s;
  uint32_t next() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return uint32_t((z ^ (z >> 31)) >> 32);
  }
  uint32_t below(uint32_t n) { return next() % n; }
  bool chance(uint32_t permille) { return below(1000) < permille; }
};

struct Seat {
  Player player;
  DuelSkill skill{};
};

BattleCard cardOf(SpeciesId s, uint64_t lineage, uint8_t prowess) {
  BattleCard c{};
  c.species = s;
  c.power = speciesInfo(s).power;
  c.knack = Knack{knackOrderOf(knackFor(speciesInfo(s).style))};
  c.lineageId = lineage;
  c.prowess = prowess;
  return c;
}

// The prowess pair that gives side B exactly `hearts` assist hearts.
constexpr uint8_t kParentProwess = 200;
constexpr uint8_t kidProwess(uint8_t hearts) {
  return hearts == 0 ? kParentProwess : uint8_t(kParentProwess - Assist::kDeadBand - 1 - Assist::kStep * (hearts - 1));
}
static_assert(Assist::hearts(kidProwess(0), kParentProwess) == 0 && Assist::hearts(kidProwess(1), kParentProwess) == 1 &&
                  Assist::hearts(kidProwess(2), kParentProwess) == 2,
              "kidProwess(n) buys exactly n hearts");

Pick habit(Rng& rng, const BattleCard& mine) {
  return {rng.chance(600) ? speciesInfo(mine.species).style : Stance(rng.below(kCycle)), Windup::Quick};
}

Windup heavyOrQuick(Rng& rng) { return rng.chance(500) ? Windup::Heavy : Windup::Quick; }

// The friend's special: bracing now, landing now, or neither.
enum class Their : uint8_t { Free, Bracing, Landing };
Their theirSpecial(const Outcome& o, uint8_t me) {
  const SideState& s = o.side[1 - me];
  if (s.queued == Windup::None) return Their::Free;
  return o.resolved < s.landsOn ? Their::Bracing : Their::Landing;
}

// The stance that beats the friend's most frequent chosen one (ties at random).
Stance counterTheirHabit(Rng& rng, const Outcome& o, uint8_t me) {
  uint8_t n[kCycle]{};
  bool any = false;
  for (uint8_t t = 0; t < o.resolved; ++t) {
    const TurnReport& r = o.turns[t];
    if (!r.dozed[1 - me] && !r.bracing[1 - me]) { ++n[uint8_t(r.shown[1 - me])]; any = true; }
  }
  if (!any) return Stance(rng.below(kCycle));
  const uint8_t top = *std::max_element(n, n + kCycle);
  uint8_t ties[kCycle]{}, k = 0;
  for (uint8_t s = 0; s < kCycle; ++s)
    if (n[s] == top) ties[k++] = s;
  return counterOf(Stance(ties[rng.below(k)]));
}

Pick choose(Seat& seat, Rng& rng, const Outcome& o, uint8_t me, const BattleCard (&card)[2], uint32_t seed,
            uint8_t assist) {
  const BattleCard& mine = card[me];
  const BattleCard& theirs = card[1 - me];
  const Their t = theirSpecial(o, me);
  switch (seat.player) {
    case Player::Random: {
      const Stance s = Stance(rng.below(kCycle));
      return {s, rng.chance(700) ? heavyOrQuick(rng) : Windup::None};
    }
    case Player::Habit: return habit(rng, mine);
    case Player::Reader: {
      if (t == Their::Bracing) return {Stance::Trick};
      const Stance s = counterTheirHabit(rng, o, me);
      return {s, t == Their::Landing ? Windup::None : heavyOrQuick(rng)};
    }
    case Player::Pet: {
      const Lean l = petChoice(seat.skill, o, Side(me), mine, theirs, assist, seed);
      return {l.stance, l.special};
    }
    case Player::LeanHalf: {
      if (!rng.chance(500)) return habit(rng, mine);
      const Lean l = petChoice(seat.skill, o, Side(me), mine, theirs, assist, seed);
      return {l.stance, l.special};
    }
    case Player::Queuer:
      if (t == Their::Bracing) return {Stance::Trick};
      return {Stance(rng.below(kCycle)), heavyOrQuick(rng)};
  }
  return {Stance::Pounce};
}

// What the specials did, over a whole tournament.
struct SpecialStats { uint64_t committed = 0, landed = 0, riposted = 0, whiffed = 0, staggered = 0; };

struct Battle { Ending ending; uint8_t turns; };

// A kid can commit only when the hint glyph is lit: the meter holds the cost
// and the landing fits before the last turn. Otherwise the press locks a
// plain stance.
bool canQueue(const Outcome& o, uint8_t me, const BattleCard& mine, Windup w) {
  return o.side[me].spark >= powerInfo(mine.power).cost && o.resolved + uint8_t(w) < DuelRules::kMaxTurns;
}

// A commit staggered on its own commit turn reports Staggered, not Committed.
void count(const Outcome& o, SpecialStats& st) {
  using S = TurnReport::Special;
  for (uint8_t t = 0; t < o.resolved; ++t)
    for (uint8_t i = 0; i < 2; ++i) {
      const S sp = o.turns[t].special[i];
      st.committed += sp == S::Committed || (sp == S::Staggered && o.turns[t].move[i].windup != Windup::None);
      st.landed += sp == S::Landed;
      st.riposted += sp == S::Riposted;
      st.whiffed += sp == S::Whiffed;
      st.staggered += sp == S::Staggered;
    }
}

Battle play(SpeciesId sa, SpeciesId sb, const Matchup& m, Rng& rng, uint32_t seed, SpecialStats& stats) {
  const BattleCard card[2] = {cardOf(sa, 0xA11CE, kParentProwess), cardOf(sb, 0xB0B, kidProwess(m.assistB))};
  Seat seat[2] = {{m.a}, {m.b}};
  const uint8_t assist[2] = {0, m.assistB};
  MoveLog log[2];
  Outcome o = resolve(card[0], card[1], seed, Stakes::Spar, log[0], log[1]);
  while (!o.over()) {
    for (uint8_t i = 0; i < 2; ++i) {
      if (log[i].count() > o.resolved) continue;   // a committed special already wrote this turn
      if (o.side[i].dozing) { log[i].append(Move{Stance::Pounce, false, true}); continue; }
      const Pick p = choose(seat[i], rng, o, i, card, seed + i, assist[i]);
      if (p.special == Windup::None || !canQueue(o, i, card[i], p.special)) log[i].append(Move{p.stance});
      else log[i].appendSpecial(p.special, p.stance);
    }
    o = resolve(card[0], card[1], seed, Stakes::Spar, log[0], log[1]);
    for (uint8_t i = 0; i < 2; ++i) learnTurn(seat[i].skill, o, Side(i), card[1 - i], uint8_t(o.resolved - 1));
  }
  count(o, stats);
  return Battle{o.ending, o.resolved};
}

struct PairResult { uint32_t aWins = 0, bWins = 0, draws = 0; };

struct BalanceReport {
  uint32_t perPair = 0;
  PairResult pairs[kSpeciesCount][kSpeciesCount]{};
  uint16_t speciesPermille[kSpeciesCount]{};   // overall, on either side, a draw counting half
  uint16_t sideAPermille = 0;                  // side A's overall rate: the reader's, in a fairness run
  uint8_t medianTurns = 0;
  uint8_t worstA = 0, worstB = 0;              // the pair furthest from even
  uint16_t worstPermille = 500;                // as A's rate in that pair
  SpecialStats specials{};
};

uint16_t permille(uint64_t wins2, uint64_t games) { return uint16_t((wins2 * 1000 / 2 + games / 2) / games); }
uint16_t pairPermille(const PairResult& p, uint32_t n) { return permille(2ull * p.aWins + p.draws, n); }

// Each pair gets its own stream, so a pair's numbers do not move when another
// pair's battles change length.
BalanceReport tournament(uint32_t perPair, const Matchup& m, uint32_t seed) {
  BalanceReport r{};
  r.perPair = perPair;
  uint64_t won2[kSpeciesCount]{}, played[kSpeciesCount]{}, sideA2 = 0;
  uint32_t turnCount[DuelRules::kMaxTurns + 1]{};
  for (uint8_t a = 0; a < kSpeciesCount; ++a)
    for (uint8_t b = 0; b < kSpeciesCount; ++b) {
      Rng rng{(uint64_t(seed) << 32) ^ (uint64_t(a) << 8) ^ b};
      PairResult& p = r.pairs[a][b];
      for (uint32_t k = 0; k < perPair; ++k) {
        const Battle x = play(SpeciesId{a}, SpeciesId{b}, m, rng, rng.next(), r.specials);
        ++turnCount[x.turns];
        if (x.ending == Ending::AWon) ++p.aWins;
        else if (x.ending == Ending::BWon) ++p.bWins;
        else ++p.draws;
      }
      won2[a] += 2ull * p.aWins + p.draws;
      won2[b] += 2ull * p.bWins + p.draws;
      played[a] += perPair;
      played[b] += perPair;
      sideA2 += 2ull * p.aWins + p.draws;
      const uint16_t pm = pairPermille(p, perPair);
      if (std::abs(int(pm) - 500) > std::abs(int(r.worstPermille) - 500)) {
        r.worstPermille = pm; r.worstA = a; r.worstB = b;
      }
    }
  for (uint8_t s = 0; s < kSpeciesCount; ++s) r.speciesPermille[s] = permille(won2[s], played[s]);
  r.sideAPermille = permille(sideA2, uint64_t(perPair) * kSpeciesCount * kSpeciesCount);
  const uint64_t half = uint64_t(perPair) * kSpeciesCount * kSpeciesCount / 2;
  uint64_t seen = 0;
  for (uint8_t t = 0; t <= DuelRules::kMaxTurns; ++t)
    if ((seen += turnCount[t]) > half) { r.medianTurns = t; break; }
  return r;
}

// The gate BATTLES.md sets: every species within 430..570 permille and every
// pair within 300..700, Random against Random.
bool wheelHolds(const BalanceReport& r) {
  for (uint16_t pm : r.speciesPermille)
    if (pm < 430 || pm > 570) return false;
  for (uint8_t a = 0; a < kSpeciesCount; ++a)
    for (uint8_t b = 0; b < kSpeciesCount; ++b) {
      const uint16_t pm = pairPermille(r.pairs[a][b], r.perPair);
      if (pm < 300 || pm > 700) return false;
    }
  return true;
}

// special_probe.py's species rates, Random v Random at 800 a pair, with its
// heavy special winding up two turns as the design says (the committed probe
// lands a heavy after one; BATTLES.md, "Measured through resolve()"). The
// tournament must land within 30 permille of each.
inline constexpr uint16_t kProbePermille[kSpeciesCount] = {465, 501, 557, 492, 460, 483, 532, 509, 502, 506};

void printWheel(const char* label, const BalanceReport& r) {
  std::printf("%s, %u a pair: median %u turns, worst pair %s over %s %u\n  ", label, r.perPair, r.medianTurns,
              SPECIES[r.worstA].name, SPECIES[r.worstB].name, r.worstPermille);
  for (uint8_t s = 0; s < kSpeciesCount; ++s) std::printf("%s %u  ", SPECIES[s].name, r.speciesPermille[s]);
  const SpecialStats& st = r.specials;
  const uint64_t q = st.committed ? st.committed : 1;
  std::printf("\n  specials (permille of %llu queued): landed %llu, riposted %llu, whiffed %llu, staggered %llu\n",
              (unsigned long long)st.committed, (unsigned long long)(st.landed * 1000 / q),
              (unsigned long long)(st.riposted * 1000 / q), (unsigned long long)(st.whiffed * 1000 / q),
              (unsigned long long)(st.staggered * 1000 / q));
}

}  // namespace blorbtool

#ifndef DUEL_BALANCE_NO_MAIN
int main(int argc, char** argv) {
  using namespace blorbtool;
  const uint32_t n = argc > 1 ? uint32_t(std::strtoul(argv[1], nullptr, 10)) : 800;
  const BalanceReport random = tournament(n, {Player::Random, Player::Random, 0}, 1);
  printWheel("random v random", random);
  int furthest = 0;
  for (uint8_t s = 0; s < kSpeciesCount; ++s)
    furthest = std::max(furthest, std::abs(int(random.speciesPermille[s]) - int(kProbePermille[s])));
  std::printf("  furthest species from special_probe.py: %d permille; gate %s\n", furthest,
              wheelHolds(random) ? "holds" : "FAILS");
  printWheel("queuer v queuer", tournament(n, {Player::Queuer, Player::Queuer, 0}, 1));
  printWheel("pet v pet (autopilot both sides)", tournament(n / 4, {Player::Pet, Player::Pet, 0}, 1));

  const uint32_t m = n / 4;
  const Player kids[] = {Player::Habit, Player::LeanHalf, Player::Pet, Player::Queuer};
  const char* names[] = {"habit kid", "kid follows the lean half the time", "pet on autopilot", "queuer"};
  for (uint8_t k = 0; k < 4; ++k)
    for (uint8_t assist = 0; assist <= DuelRules::kMaxAssistHearts; ++assist)
      std::printf("parent (reader) v %s, assist %u: parent %u\n", names[k], assist,
                  tournament(m, {Player::Reader, kids[k], assist}, 2).sideAPermille);
  return wheelHolds(random) ? 0 : 1;
}
#endif
