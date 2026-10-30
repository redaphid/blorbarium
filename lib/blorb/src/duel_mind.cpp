// The pet's own mind in a duel: its lean (the whisper and the 10 s
// autopilot), what it learns from each turn, and the shake's prophecy. Only
// the pet's own device runs these, and what they pick reaches the friend as a
// logged move, so unlike resolve() they owe the other device nothing.
#include "blorb/duel.h"
#include <algorithm>

namespace blorb {
namespace {

constexpr int32_t kOne = 32767;          // a Q15 cell's "this stance always wins here"
constexpr int32_t kKnackLean = 2048;     // per knack rank: a fresh Skill leans to the pet's taste
constexpr int32_t kMoodLean = 48;        // per mood point past its threshold
// Four knack ranks wide. At one rank, two pets on autopilot replayed the same
// battle every time (grungo lost to puffkin 300 times in 300); at four, pet v
// pet spreads like random play and a learned cell still outvotes it.
constexpr uint32_t kNoise = 8192;
constexpr int32_t kSureToQueue = 24;     // a score margin of 3,072: three-quarters of a knack rank

uint32_t mix(uint32_t h) {
  h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
  return h;
}

// The stance a side chose on turn t, or none (kCycle) if it slept or braced
// through it: a bracing turn's entry is a placeholder, and the pet is reading
// the kid, not the log.
uint8_t chosenBy(const TurnReport& r, uint8_t side) {
  return r.dozed[side] || r.bracing[side] ? kCycle : uint8_t(r.shown[side]);
}

uint8_t contextAt(const Outcome& o, uint8_t me, const BattleCard& theirs, uint8_t turn) {
  const uint8_t last = turn == 0 ? kCycle : chosenBy(o.turns[turn - 1], uint8_t(1 - me));
  return uint8_t(uint8_t(speciesInfo(theirs.species).element) * (kCycle + 1) + last);
}

// Which way my own kid usually goes: the most frequent stance among the turns
// the kid, not the pet, locked. kCycle if the kid has picked nothing yet.
uint8_t kidsHabit(const Outcome& o, uint8_t me) {
  uint8_t n[kCycle]{};
  for (uint8_t t = 0; t < o.resolved; ++t)
    if (!o.turns[t].move[me].byPet && chosenBy(o.turns[t], me) < kCycle) ++n[chosenBy(o.turns[t], me)];
  uint8_t best = kCycle;
  for (uint8_t s = 0; s < kCycle; ++s)
    if (n[s] > 0 && (best == kCycle || n[s] > n[best])) best = s;
  return best;
}

}  // namespace

uint8_t duelContext(const Outcome& o, Side me, const BattleCard& theirs) {
  return contextAt(o, uint8_t(me), theirs, o.resolved);
}

Lean petChoice(const DuelSkill& skill, const Outcome& o, Side me, const BattleCard& mine, const BattleCard& theirs,
               uint8_t assistSteps, uint32_t seed) {
  const uint8_t i = uint8_t(me);
  const int32_t bright = 32 * assistSteps;
  // Their special is public from its commit; only its landing stance is not.
  const SideState& them = o.side[1 - i];
  if (them.queued != Windup::None && o.resolved < them.landsOn)
    return Lean{Stance::Trick, uint8_t(std::min<int32_t>(255, 192 + bright))};
  if (them.queued != Windup::None && o.resolved == them.landsOn) {
    const Foretelling f = foretell(o, other(me), mine, theirs, seed);
    return Lean{counterOf(f.stance), uint8_t(std::min<int32_t>(255, 64 + (f.confidence.raw >> 18) + bright))};
  }

  const Q15(&cell)[kCycle] = skill.win[duelContext(o, me, theirs)];
  const uint8_t habit = kidsHabit(o, i);
  int32_t score[kCycle]{};
  const uint32_t noise = mix(fnvByte(fnvByte(fnvWord(2166136261u, seed), o.resolved), i));
  for (uint8_t s = 0; s < kCycle; ++s) {
    score[s] = cell[s].v + kKnackLean * mine.knack.rank(Stance(s)) + int32_t((noise >> (8 * s)) % kNoise);
    if (s == uint8_t(Stance::Guard) && mine.mood.nerve < 96) score[s] += kMoodLean * (96 - mine.mood.nerve);
    if (s == uint8_t(Stance::Pounce) && mine.mood.pep > 160) score[s] += kMoodLean * (mine.mood.pep - 160);
    if (s == habit && mine.mood.trust > 160) score[s] += kMoodLean * (mine.mood.trust - 160);
  }
  uint8_t best = 0, second = 1;
  if (score[1] > score[0]) { best = 1; second = 0; }
  if (score[2] > score[best]) { second = best; best = 2; }
  else if (score[2] > score[second]) second = 2;
  // How sure it is: the margin over the runner-up, then the assist on top.
  const int32_t sure = std::min<int32_t>(127, (score[best] - score[second]) / 128);
  // The autopilot queues when the meter holds the cost and it is sure of its
  // read, or the meter is full and waiting wastes sparks; a spirited pet
  // winds up the heavy one.
  const SideState& self = o.side[i];
  Windup special = Windup::None;
  if (self.queued == Windup::None && self.spark >= powerInfo(mine.power).cost &&
      (self.spark == kMeterMax || sure >= kSureToQueue)) {
    special = mine.mood.spirit > 160 ? Windup::Heavy : Windup::Quick;
    if (o.resolved + uint8_t(special) >= DuelRules::kMaxTurns) special = Windup::None;
  }
  return Lean{Stance(best), uint8_t(std::min<int32_t>(255, 64 + sure + bright)), special};
}

void learnTurn(DuelSkill& skill, const Outcome& o, Side me, const BattleCard& theirs, uint8_t turn) {
  if (turn >= o.resolved) return;
  const uint8_t i = uint8_t(me);
  const uint8_t them = chosenBy(o.turns[turn], uint8_t(1 - i));
  if (them == kCycle) return;
  Q15(&cell)[kCycle] = skill.win[contextAt(o, i, theirs, turn)];
  for (uint8_t s = 0; s < kCycle; ++s) {
    const int32_t target = beats(Stance(s), Stance(them)) ? kOne : beats(Stance(them), Stance(s)) ? -kOne : 0;
    cell[s].v = int16_t(cell[s].v + (target - cell[s].v) / 4);
  }
}

Foretelling foretell(const Outcome& o, Side about, const BattleCard& seer, const BattleCard& friendCard, uint32_t seed) {
  const uint8_t a = uint8_t(about);
  uint8_t h[DuelRules::kMaxTurns]{};
  uint8_t n = 0;
  for (uint8_t t = 0; t < o.resolved; ++t)
    if (chosenBy(o.turns[t], a) < kCycle) h[n++] = chosenBy(o.turns[t], a);

  // Their species' style is the prior; every turn is a vote for their habit;
  // a turn that followed the same pattern they just played is worth four.
  uint16_t votes[kCycle]{};
  votes[uint8_t(speciesInfo(friendCard.species).style)] += 1;
  for (uint8_t k = 0; k < n; ++k) votes[h[k]] += 1;
  const uint8_t order = seer.species == kGrungo ? 2 : 1;
  for (uint8_t k = order; k < n; ++k) {
    bool same = true;
    for (uint8_t j = 0; j < order; ++j) same = same && h[k - 1 - j] == h[n - 1 - j];
    if (same) votes[h[k]] += 4;
  }

  const uint8_t start = uint8_t(mix(fnvByte(fnvWord(2166136261u, seed), o.resolved)) % kCycle);
  uint8_t best = start;
  uint16_t total = 0;
  for (uint8_t k = 0; k < kCycle; ++k) {
    const uint8_t s = uint8_t((start + k) % kCycle);
    total = uint16_t(total + votes[s]);
    if (votes[s] > votes[best]) best = s;
  }
  return Foretelling{Stance(best), Fx::ratio(votes[best], total)};
}

}  // namespace blorb
