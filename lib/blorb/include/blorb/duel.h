#pragma once
// The duel's rules (docs/design/BATTLES.md): pure, integer, no clock, no radio.
//
// Each turn both kids secretly pick a stance with the body (tilt to a rim
// sector, knock to lock in; BOOT arms the power). A turn resolves only when
// BOTH stances for it are known, and every device computes the whole battle
// from scratch with one function:
//
//   resolve(cardA, cardB, seed, stakes, movesA, movesB) -> Outcome
//
// Same inputs, same Outcome, on x86 and Xtensa: integers and fnv only, and the
// core is constexpr so tools/duel_determinism.sh has the S3 compiler itself
// evaluate the golden battles' hashes. The result never depends on which
// device is side A: luck is drawn per side from fnv(seed, turn, that side's
// lineage), never from one shared stream, and both sides' powers in one phase
// read the same inputs. test_duel_resolve checks that resolve(B, A, mB, mA)
// is the mirror of resolve(A, B, mA, mB) over 10,000 random battles.
//
// Stakes (stakes.h) are agreed before turn 0 and are an input like the cards:
// resolve() knows whether a side at 0 hearts faints (Spar, Scrap) or is downed
// and waits for the winner's verdict (Mortal). Nothing in here touches a
// creature: each device applies its OWN pet's wounds, scars and fate from the
// shared Outcome (turnHits, then stakes.h's woundFor).
#include <cstddef>
#include <cstdint>
#include "blorb/fixed.h"
#include "blorb/genes.h"
#include "blorb/species.h"
#include "blorb/stakes.h"

namespace blorb {

struct DuelRules {
  static constexpr uint8_t kHearts = 5;             // everyone starts here, plus any assist hearts
  static constexpr uint8_t kMaxAssistHearts = 2;
  static constexpr uint8_t kElementHeadStart = 1;   // the side whose element wins starts with a pip lit
  static constexpr uint8_t kMaxTurns = 15;          // then whoever has more hearts wins; equal is a draw
  static constexpr uint8_t kLogSlots = kMaxTurns + 1;   // one more for the winner's verdict on a downed pet
  static constexpr uint8_t kMaxLevel = 12;          // display only: level never changes a number in resolve()
  static constexpr uint8_t kCardVersion = 1;
};

enum class Side : uint8_t { A = 0, B = 1 };
constexpr Side other(Side s) { return s == Side::A ? Side::B : Side::A; }

// One locked choice. `byPet` marks a turn the kid let run out, or a turn the
// pet slept through under spores: the pet chose, which the screen celebrates
// rather than scolds. A dozing side's move is appended at once by the session,
// so the logs keep marching; resolve() ignores its stance and power.
//
// After a Mortal downing, the WINNER appends one more Move at index
// Outcome::resolved (the slot right after the downing turn) whose `verdict` is
// Spare or Finish. resolve() reads a verdict only there and only from the
// winner's log; the loser's log at that index, and any verdict bits on a turn
// move, mean nothing. kLogSlots leaves room for it after a downing on turn 15.
struct Move {
  Stance stance = Stance::Pounce;
  bool power = false;
  bool byPet = false;
  Verdict verdict = Verdict::None;
  constexpr uint8_t pack() const {
    return uint8_t(uint8_t(stance) | (power ? 4 : 0) | (byPet ? 8 : 0) | (uint8_t(verdict) << 4));
  }
  static constexpr Move unpack(uint8_t b) {
    return Move{Stance((b & 3) % kCycle), (b & 4) != 0, (b & 8) != 0, Verdict(((b >> 4) & 3) % 3)};
  }
};

// One side's moves: append-only, and the only state either device writes
// about the battle. My log grows by append(); the friend's grows only by
// adopt() from a packet. A packet carries everything that side has committed
// so far, so a lost, doubled or late packet is a stale prefix, adopt() keeps
// the longest, and both ends converge.
//
// The session must not let a side append turn t+1 before turn t resolves:
// after a downing, the winner's next slot is the verdict's.
class MoveLog {
 public:
  constexpr uint8_t count() const { return count_; }
  constexpr Move at(uint8_t turn) const { return Move::unpack(packed_[turn]); }
  constexpr const uint8_t* packed() const { return packed_; }
  constexpr bool append(Move m) {
    if (count_ >= DuelRules::kLogSlots) return false;
    packed_[count_++] = m.pack();
    return true;
  }

  enum class Merge : uint8_t { Same, Grew, Stale, Conflict };
  // Conflict: the heard prefix disagrees with what is held, or is longer than
  // a log can be. Only a bug or a different battle can do that; the session
  // ends the duel as a draw.
  constexpr Merge adopt(const uint8_t* packed, uint8_t count) {
    if (count > DuelRules::kLogSlots) return Merge::Conflict;
    const uint8_t common = count < count_ ? count : count_;
    for (uint8_t i = 0; i < common; ++i)
      if (packed[i] != packed_[i]) return Merge::Conflict;
    if (count < count_) return Merge::Stale;
    if (count == count_) return Merge::Same;
    for (uint8_t i = count_; i < count; ++i) packed_[i] = packed[i];
    count_ = count;
    return Merge::Grew;
  }

 private:
  uint8_t packed_[DuelRules::kLogSlots]{};
  uint8_t count_ = 0;
};

// The genome's knack, ranked: one of the six permutations of {2, 1, 0} over
// (Pounce, Guard, Trick). Taste, never strength.
struct Knack {
  uint8_t order = 0;   // 0..5
  static constexpr uint8_t kOrders[6][kCycle] = {{2, 1, 0}, {2, 0, 1}, {1, 2, 0}, {0, 2, 1}, {1, 0, 2}, {0, 1, 2}};
  constexpr uint8_t rank(Stance s) const { return kOrders[order % 6][uint8_t(s)]; }
  constexpr Stance best() const {
    return rank(Stance::Pounce) == 2 ? Stance::Pounce : rank(Stance::Guard) == 2 ? Stance::Guard : Stance::Trick;
  }
};

// How he feels as the duel opens, frozen from his chemistry into the card
// (0..255 each). It colours his lean and his autopilot pick, never a number
// in resolve(): a frightened pet (low nerve) leans Guard, a restless one (high
// pep) Pounce, a pet who trusts his kid leans the way his kid usually goes.
// Frozen so both devices read the same bytes.
struct Mood { uint8_t nerve = 128, pep = 128, spirit = 128, trust = 128; };

// Enough of a pet to draw it on the friend's screen and in the friend book.
// No name and no free text: what crosses the air is a species, tints, marks
// and a stage.
struct CardLook {
  Tint regions[kRegionCount]{};
  uint8_t scalePct = 100;
  Stage stage = Stage::Adult;
  uint8_t markCount = 0;
  struct Mark { uint8_t layer = 0, variant = 0; } marks[4]{};
  uint32_t lifeSeed = 0;   // mottling and blink jitter, so the friend's pet looks like itself
};

// Everything a duel needs from a pet, derived when the owner opens a duel
// (duel_card.h). It travels whole in every state packet, so the friend never
// needs the genome for the battle. Element and style are not here: both ends
// read them from species.def by `species`, one source of truth.
struct BattleCard {
  uint8_t version = DuelRules::kCardVersion;
  SpeciesId species = kGrungo;   // body and element
  PowerId power = power::foresee;
  Knack knack{};
  Mood mood{};
  uint8_t level = 1;             // this life's duel level: a badge on the rim, nothing more
  uint8_t prowess = 128;         // how well THIS KID has been playing (duel_book.h): the assist's only input
  uint64_t lineageId = 0;        // who this is in the friend book; the side's luck key
  CardLook look{};
};

// ---- the outcome ---------------------------------------------------------------------
enum class Clash : uint8_t { AWins, BWins, Bonk };   // Bonk: heads bonk, nobody hurt by the clash

struct TurnReport {
  Move move[2]{};          // as locked, by side
  Stance shown[2]{};       // after foresee: what the clash animation plays
  bool dozed[2]{};         // slept through it (spores): no stance, no spark, no power
  Clash clash = Clash::Bonk;
  uint8_t hit[2]{};        // hearts each side lost (after blocks)
  uint8_t healed[2]{};
  PowerId fired[2]{};      // valid where powered[i]; for gadget, the power it became
  bool powered[2]{};
  bool blocked[2]{};       // a shell, a hush or a dazzle turned a hit away (the "bonk off" sparkle)
  bool super[2]{};         // display only: the clash winner's element beats the loser's ("SUPER!")
  bool zone[2]{};          // display only: won the clash with its knack's best stance (a sparkle)
};

// One side's state between turns. The counters are how multi-turn powers
// last, and they are part of the replayed state, so they are as deterministic
// as hearts.
struct SideState {
  uint8_t hearts = DuelRules::kHearts, maxHearts = DuelRules::kHearts;
  uint8_t spark = 0;                // 0..kMeterMax
  bool shell = false;               // the next landing hit bonks off
  bool dozing = false;              // next turn: no stance, no spark
  uint8_t bonkTurns = 0;            // turns left winning head bonks (dash, lantern)
  uint8_t dazzledTurns = 0;         // turns left in which MY winning clash hits miss half the time (lantern)
  uint8_t hushTurns = 0;            // turns left in which a lost clash costs me nothing (hush)
};

enum class Ending : uint8_t { Ongoing, AWon, BWon, Draw };

constexpr uint32_t fnvByte(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }
constexpr uint32_t fnvWord(uint32_t h, uint32_t w) {
  for (uint8_t i = 0; i < 4; ++i) h = fnvByte(h, uint8_t(w >> (8 * i)));
  return h;
}

struct Outcome {
  Stakes stakes = Stakes::Spar;
  SideState side[2]{};
  TurnReport turns[DuelRules::kMaxTurns]{};
  uint8_t resolved = 0;    // turns with both moves known; turns past it are unreadable by design
  Ending ending = Ending::Ongoing;
  bool downed[2]{};        // Mortal: fell to 0 hearts (Spar and Scrap: fainted, nothing pending)
  Verdict verdict = Verdict::None;   // Mortal: the winner's, once it arrives; None while pending
  constexpr bool awaitingVerdict() const {
    return stakes == Stakes::Mortal && downed[0] != downed[1] && verdict == Verdict::None;
  }
  constexpr bool over() const { return ending != Ending::Ongoing; }
  // Both devices log it, so the two books can be compared. Field by field, in
  // a fixed order, so padding and the compiler's layout never reach it.
  constexpr uint32_t hash() const {
    uint32_t h = 2166136261u;
    h = fnvByte(h, uint8_t(stakes)); h = fnvByte(h, resolved); h = fnvByte(h, uint8_t(ending));
    h = fnvByte(h, downed[0]); h = fnvByte(h, downed[1]); h = fnvByte(h, uint8_t(verdict));
    for (const SideState& s : side) {
      const uint8_t b[] = {s.hearts, s.maxHearts, s.spark, s.shell, s.dozing, s.bonkTurns, s.dazzledTurns, s.hushTurns};
      for (uint8_t x : b) h = fnvByte(h, x);
    }
    for (uint8_t t = 0; t < resolved && t < DuelRules::kMaxTurns; ++t) {
      const TurnReport& r = turns[t];
      h = fnvByte(h, uint8_t(r.clash));
      for (uint8_t i = 0; i < 2; ++i) {
        const uint8_t b[] = {r.move[i].pack(), uint8_t(r.shown[i]), r.dozed[i], r.hit[i],  r.healed[i],
                             r.fired[i].v,     r.powered[i],         r.blocked[i], r.super[i], r.zone[i]};
        for (uint8_t x : b) h = fnvByte(h, x);
      }
    }
    return h;
  }
};

// Symmetric in the two nonces, so neither device has to be "first". The
// rematch number and the stakes are part of the seed, so a late packet from
// the previous bout, or from the same pair at another tier, never feeds this one.
constexpr uint32_t duelSeed(uint32_t nonceA, uint32_t nonceB, uint8_t rematch, Stakes stakes) {
  const uint32_t lo = nonceA < nonceB ? nonceA : nonceB, hi = nonceA < nonceB ? nonceB : nonceA;
  uint32_t h = fnvWord(fnvWord(2166136261u, lo), hi);
  return fnvByte(fnvByte(h, rematch), uint8_t(stakes));
}

// The battle's name, in both books and on the air. Before turn 0 (Greeting,
// Staking) the pair is named with `stakes` = Spar; from turn 0 on, the id
// carries the agreed stakes, so two devices can never resolve one battle at
// two tiers. `rematch` numbers bouts for the book.
struct BattleId {
  uint32_t seed = 0;     // duelSeed(nonceA, nonceB, rematch, stakes)
  uint8_t rematch = 0;
  Stakes stakes = Stakes::Spar;
  friend constexpr bool operator==(BattleId a, BattleId b) {
    return a.seed == b.seed && a.rematch == b.rematch && a.stakes == b.stakes;
  }
  friend constexpr bool operator!=(BattleId a, BattleId b) { return !(a == b); }
};

// The handicap: skill, not level. Each card carries its kid's prowess, a
// running mean of how well that kid has been playing (duel_book.h). Inside a
// dead band nobody gets help; past it the weaker side gets one extra heart per
// step, at most two. The stronger side never loses anything, and a kid who
// improves earns the help away.
struct Assist {
  static constexpr uint8_t kDeadBand = 24, kStep = 32;
  static constexpr uint8_t hearts(uint8_t mine, uint8_t theirs) {
    if (theirs <= mine + kDeadBand) return 0;
    const uint8_t steps = uint8_t((theirs - mine - kDeadBand) / kStep + 1);
    return steps > DuelRules::kMaxAssistHearts ? DuelRules::kMaxAssistHearts : steps;
  }
};
static_assert(Assist::hearts(128, 140) == 0 && Assist::hearts(100, 140) == 1 && Assist::hearts(20, 250) == 2 &&
                  Assist::hearts(250, 20) == 0,
              "only the weaker side gets help, and at most two hearts");

// Replays every turn with both moves known, from turn 0, every call (at most
// 15 turns: microseconds). Before turn 0: hearts = kHearts + Assist::hearts,
// and the side whose element beats the other's starts with kElementHeadStart
// pips. Inside one turn:
//   1 a dozing side has no stance (it loses the clash and deals nothing),
//     earns no spark and cannot fire
//   2 powers: a side whose move asks for power and whose meter holds the
//     cost spends it; gadget draws the power it becomes from its own luck.
//     BeforeClash handlers read both ORIGINAL stances: foresee turns its
//     stance into the counter, dash and lantern win head bonks, lantern
//     dazzles the friend, hush starts, shell goes up
//   3 clash: beats(), or a head bonk on equal stances, which a side with
//     bonkTurns > 0 wins (both or neither: still a bonk)
//   4 damage: 1 heart to the clash loser; OnDamage handlers only ADD hits (zap)
//   5 blocks only TAKE hits away: hush cancels a lost clash, a dazzled
//     winner's clash hit misses on its own luck, then a shell absorbs one hit
//   6 AfterDamage handlers (mend, spores); then sparks: +1 to each side not
//     dozing, +1 more to the clash loser (the rally), capped at kMeterMax;
//     the turn counters tick down. Nothing rewards the winner: every winner
//     reward snowballed in the probe
//   7 a side at 0 hearts faints (Spar, Scrap) or is downed (Mortal) and the
//     battle ends; both at 0 is a draw and nobody is downed. After kMaxTurns,
//     more hearts wins
// A powered move whose meter is short plays as an unpowered move.
Outcome resolve(const BattleCard& a, const BattleCard& b, uint32_t seed, Stakes, const MoveLog& movesA,
                const MoveLog& movesB);

// What a power handler sees: the turn so far, from its own side's view.
// Handlers are called only for a side whose power fired this turn; what
// lasts past the turn lives in SideState and the turn's own steps read it.
struct PowerCtx {
  Side me;
  PowerId acting;                // the row's power, or what gadget became
  const Stance (&original)[2];   // as locked, before any handler
  const bool (&dozed)[2];
  Stance (&shown)[2];            // what the clash plays
  uint8_t (&hit)[2];
  uint8_t (&healed)[2];
  SideState (&side)[2];
  uint32_t luck;                 // this side's own luck this turn; each use draws fnv(luck, purpose)
};

// ---- what a turn did to me, for stakes.h's woundFor ------------------------------------
// One TurnHit per resolved turn, for my side: the hearts I lost, the kind of
// what hit me (a zap's ring over the clash's mark when both landed), and my
// own luck that turn for where the scar lands. `out` holds kMaxTurns; entries
// past o.resolved are left untouched.
void turnHits(const Outcome& o, Side me, uint32_t seed, const BattleCard& mine, const BattleCard& theirs,
              TurnHit* out);

// ---- the pet's own mind in a duel ------------------------------------------------------
// What the pet would pick, and how it learns, keyed by who it is fighting.
// Context = the friend's element (3) x the friend's last locked stance or none
// (4) = 12; moves = the three stances. This is v2's Skill/chooseMove/learnMove
// for a games.def row; until v2's Games chunk exists it is its own small
// table, saved in duels.log.
struct DuelSkill {
  static constexpr uint8_t kContexts = 12;
  Q15 win[kContexts][kCycle]{};
  uint16_t battles = 0;
};
uint8_t duelContext(const Outcome&, Side me, const BattleCard& theirs);
// The Skill's argmax, plus the knack's lean, plus the mood's lean, plus a
// little noise from fnv(seed, turn). Reads only resolved turns, so the pet
// never peeks at an unpaired move. `whisper` (0..255) is how brightly the
// screen shows it: it rises with how sure the pet is and with the assist, so a
// young kid is shown the reading the pet has made. Also the 10 s autopilot.
struct Lean { Stance stance; uint8_t whisper; };
Lean petChoice(const DuelSkill&, const Outcome&, Side me, const BattleCard& mine, const BattleCard& theirs,
               uint8_t assistHearts, uint32_t seed);
// After each resolved turn: the friend's shown stance makes every one of my
// three stances' payoff known, so all three cells of the context learn.
void learnTurn(DuelSkill&, const Outcome&, Side me, const BattleCard& theirs, uint8_t turn);

// ---- shake: the pet tells the future -----------------------------------------------------
// The pet prophesies the friend's next stance from the friend's resolved
// history (habit, plus their species' style as a prior), judged when the turn
// reveals. Grungo reads two turns of pattern, not one, so the seer is the
// better seer. One prophecy per turn: same inputs, same words.
struct Foretelling {
  Stance stance = Stance::Pounce;
  Fx confidence{};   // shown as how hard the bubble glows
};
Foretelling foretell(const Outcome&, Side about, const BattleCard& seer, const BattleCard& friendCard, uint32_t seed);

}  // namespace blorb
