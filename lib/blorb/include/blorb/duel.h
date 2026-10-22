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

// The duel's tuning, in one place (issue #58). Every number resolve() plays
// by is here or in powers.def's cost column; tools/duel_balance.cpp measures
// them, and BATTLES.md ("Shorter duels") has the before and after.
//
// Both devices must play by the same numbers, so they are part of the wire
// contract: duelRulesHash() folds every one of them, kRulesVersion and the
// roster into the `roster` field every beacon and State carries, and a badge
// that hears a different hash greys that friend out and never matches him
// (duel_session.cpp). Bump kRulesVersion whenever resolve() itself changes
// how a turn plays without a number here moving.
struct DuelRules {
  static constexpr uint8_t kRulesVersion = 1;       // 1: the 0.7 rules, five hearts and one heart a lost clash
  static constexpr uint8_t kHearts = 5;             // everyone starts here, plus any assist hearts
  static constexpr uint8_t kClashHearts = 1;        // what a lost clash costs (a stagger included)
  static constexpr uint8_t kMaxAssistSteps = 2;     // the weaker kid's help comes in at most this many steps (Assist)
  static constexpr uint8_t kAssistStepHearts = 1;   // extra hearts per step
  static constexpr uint8_t kElementHeadStart = 1;   // the side whose element wins starts with a pip lit
  static constexpr uint8_t kSparkPerTurn = 1;       // pips each side not dozing gains every turn
  static constexpr uint8_t kRallySpark = 1;         // and one more for a side hurt while the other was not
  static constexpr uint8_t kMaxTurns = 15;          // then whoever has more hearts wins; equal is a draw
  static constexpr uint8_t kLogSlots = kMaxTurns + 1;   // one more for the winner's verdict on a downed pet
  static constexpr uint8_t kMaxLevel = 12;          // display only: level never changes a number in resolve()
  static constexpr uint8_t kCardVersion = 1;
};

enum class Side : uint8_t { A = 0, B = 1 };
constexpr Side other(Side s) { return s == Side::A ? Side::B : Side::A; }

// The queued special (BATTLES.md, "The queued special"). A power is never
// instant: BOOT commits it on turn t with a wind-up of 1 (Quick) or 2 (Heavy)
// turns, and MoveLog::appendSpecial writes every entry it needs at once: turn
// t carries `windup` (its stance is written as Pounce and ignored, so it leaks
// nothing), a Heavy's turn t+1 is a placeholder, and turn t + windup carries
// the hidden landing stance with `power` set. resolve() reads the wind-up from
// turn t's entry, so the queue lives in the logs both devices already
// exchange, and the landing stance is unreadable until that turn resolves.
enum class Windup : uint8_t { None = 0, Quick = 1, Heavy = 2 };

// The special's numbers, from sketch/tools/special_probe.py (measured
// 2026-10-13) and retuned by tools/duel_balance.cpp (#58). A landing (or a
// riposte) costs a lost clash's hearts plus the extra.
struct SpecialRules {
  static constexpr uint8_t kQuickExtra = 1;   // hearts on top of a clash's when a quick special lands
  static constexpr uint8_t kHeavyExtra = 2;   // ... when a heavy one lands
  static constexpr uint8_t extra(Windup w) { return w == Windup::Heavy ? kHeavyExtra : kQuickExtra; }
  static constexpr uint8_t hearts(Windup w) { return uint8_t(DuelRules::kClashHearts + extra(w)); }
};

// What the powers do, in hearts (duel_powers.cpp; the costs are powers.def's).
struct PowerRules {
  static constexpr uint8_t kZapHearts = 1;     // zap: on top of the landing
  static constexpr uint8_t kMendHearts = 1;    // mend: grown back after the landing, never past full
  static constexpr uint8_t kShellHearts = 1;   // shell: taken off the next hit that lands
};

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
  Windup windup = Windup::None;
  constexpr uint8_t pack() const {
    return uint8_t(uint8_t(stance) | (power ? 4 : 0) | (byPet ? 8 : 0) | (uint8_t(verdict) << 4) |
                   (uint8_t(windup) << 6));
  }
  static constexpr Move unpack(uint8_t b) {
    return Move{Stance((b & 3) % kCycle), (b & 4) != 0, (b & 8) != 0, Verdict(((b >> 4) & 3) % 3),
                Windup(((b >> 6) & 3) % 3)};
  }
};
static_assert(Move::unpack(Move{Stance::Trick, true, false, Verdict::None, Windup::Heavy}.pack()).windup == Windup::Heavy,
              "the wind-up survives the wire");

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
  // The commit: turn t's wind-up entry, a Heavy's placeholder, and the landing
  // entry, appended together. False, and nothing appended, when the landing
  // would fall past kMaxTurns (the hint glyph greys out near the end). Whether
  // the meter holds the cost is resolve()'s to judge, from the same logs.
  constexpr bool appendSpecial(Windup w, Stance landing, bool byPet = false) {
    const uint8_t turns = uint8_t(w);
    if (turns == 0 || count_ + turns >= DuelRules::kMaxTurns) return false;
    append(Move{Stance::Pounce, false, byPet, Verdict::None, w});
    if (w == Windup::Heavy) append(Move{Stance::Pounce, false, byPet});
    return append(Move{landing, true, byPet});
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

// The genome's knack, ranked: one of genes.h's six orders over (Pounce,
// Guard, Trick), knackOrderOf(KnackGene). Taste, never strength.
struct Knack {
  uint8_t order = 0;   // 0..5
  constexpr uint8_t rank(Stance s) const { return kKnackOrders[order % 6][uint8_t(s)]; }
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
  Stance shown[2]{};       // what the clash animation plays; meaningless where dozed or bracing
  bool dozed[2]{};         // slept through it (spores): no stance, no spark, no special
  bool bracing[2]{};       // winding up a special this turn (the telegraph, on both screens)
  enum class Special : uint8_t { None, Committed, Landed, Riposted, Whiffed, Staggered } special[2]{};
  Clash clash = Clash::Bonk;
  uint8_t hit[2]{};        // hearts each side lost (after blocks)
  uint8_t healed[2]{};
  PowerId fired[2]{};      // valid where powered[i]; for gadget, the power it became
  bool powered[2]{};       // a special landed and its power fired
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
  uint8_t hushTurns = 0;            // turns left in which a lost clash costs me nothing and a special whiffs (hush)
  Windup queued = Windup::None;     // a special in flight (its strength: a stagger drops Heavy to Quick)
  uint8_t landsOn = 0;              // the turn it lands, valid while queued
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
      const uint8_t b[] = {s.hearts,    s.maxHearts,    s.spark,     s.shell,           s.dozing,
                           s.bonkTurns, s.dazzledTurns, s.hushTurns, uint8_t(s.queued), s.landsOn};
      for (uint8_t x : b) h = fnvByte(h, x);
    }
    for (uint8_t t = 0; t < resolved && t < DuelRules::kMaxTurns; ++t) {
      const TurnReport& r = turns[t];
      h = fnvByte(h, uint8_t(r.clash));
      for (uint8_t i = 0; i < 2; ++i) {
        const uint8_t b[] = {r.move[i].pack(), uint8_t(r.shown[i]), r.dozed[i],   r.bracing[i], uint8_t(r.special[i]),
                             r.hit[i],         r.healed[i],         r.fired[i].v, r.powered[i], r.blocked[i],
                             r.super[i],       r.zone[i]};
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
// dead band nobody gets help; past it the weaker side gets one step of help
// per kStep points, at most kMaxAssistSteps, each worth kAssistStepHearts
// extra hearts. The stronger side never loses anything, and a kid who improves
// earns the help away. The stakes' fairness gate and the whisper's brightness
// count steps; the hearts are what resolve() and the gauge use.
struct Assist {
  static constexpr uint8_t kDeadBand = 24, kStep = 32;
  static constexpr uint8_t steps(uint8_t mine, uint8_t theirs) {
    if (theirs <= mine + kDeadBand) return 0;
    const uint8_t n = uint8_t((theirs - mine - kDeadBand) / kStep + 1);
    return n > DuelRules::kMaxAssistSteps ? DuelRules::kMaxAssistSteps : n;
  }
  static constexpr uint8_t hearts(uint8_t mine, uint8_t theirs) {
    return uint8_t(steps(mine, theirs) * DuelRules::kAssistStepHearts);
  }
};
static_assert(Assist::steps(128, 140) == 0 && Assist::steps(100, 140) == 1 && Assist::steps(20, 250) == 2 &&
                  Assist::steps(250, 20) == 0,
              "only the weaker side gets help, and at most two steps");

// What two badges must agree on to duel: the roster (species.def and
// powers.def, rosterHash) and every rule above. It rides in the `roster` field
// of every beacon and State (duel_wire.h), so a badge on other rules is greyed
// out in the lobby, never matched, and its States are ignored: a mixed pair
// refuses to fight instead of resolving one battle two ways. 0.7 badges send
// plain rosterHash() there, which this differs from (test_duel_resolve).
constexpr uint16_t duelRulesHash() {
  uint32_t h = fnvByte(fnvByte(2166136261u, uint8_t(rosterHash())), uint8_t(rosterHash() >> 8));
  const uint8_t rules[] = {DuelRules::kRulesVersion, DuelRules::kHearts,          DuelRules::kClashHearts,
                           DuelRules::kMaxAssistSteps, DuelRules::kAssistStepHearts, DuelRules::kElementHeadStart,
                           DuelRules::kSparkPerTurn,
                           DuelRules::kRallySpark,   DuelRules::kMaxTurns,        SpecialRules::kQuickExtra,
                           SpecialRules::kHeavyExtra, PowerRules::kZapHearts,     PowerRules::kMendHearts,
                           PowerRules::kShellHearts, Assist::kDeadBand,           Assist::kStep,
                           kMeterMax};
  for (uint8_t b : rules) h = fnvByte(h, b);
  return uint16_t(h ^ (h >> 16));
}

// Replays every turn with both moves known, from turn 0, every call (at most
// 15 turns: microseconds). Before turn 0: hearts = kHearts + Assist::hearts,
// and the side whose element beats the other's starts with kElementHeadStart
// pips. Inside one turn:
//   1 what each side plays. A dozing side has no stance. A side with a
//     special in flight braces until its landing turn, then plays the landing
//     entry's hidden stance (asleep on its landing turn, the special is lost).
//     Otherwise a wind-up entry commits a special if the meter holds the cost
//     (spent now) and the landing fits in kMaxTurns, and he braces this turn.
//     Any other entry, a short-metered commit included, is a plain stance
//   2 a landing meets the friend's stance, and there is no clash that turn.
//     If the friend's stance beats it, it is riposted: its owner takes
//     1 + extra and the power does not fire (a Foresee whiffs instead: he saw
//     it coming). A hushing friend dodges it: a whiff. Otherwise, ties
//     included, it lands: the friend takes 1 + extra and the power fires
//     (gadget draws the power it becomes from its own luck)
//   3 with no landing, the clash: beats(), a sleeper loses to anyone awake, and
//     only a Trick beats a bracer (a Pounce or a Guard bonks off). Equal is a
//     head bonk, which a side with bonkTurns > 0 wins. The loser takes 1 heart
//     unless the winner is dazzled and misses on its luck, or the loser
//     hushes. A bracer that loses the clash is staggered: a Heavy drops to
//     Quick, a Quick is lost and its landing entry plays as a plain stance
//   4 the fired power: OnDamage handlers only ADD hits (zap) and set what
//     lasts (shell, dash, lantern, hush); then a shell absorbs one hit; then
//     AfterDamage handlers (mend, spores)
//   5 sparks: +1 to each side not dozing, +1 more to a side that took hits
//     while the other took none (the rally), capped at kMeterMax; the turn
//     counters tick down. Nothing rewards the winner
//   6 a side at 0 hearts faints (Spar, Scrap) or is downed (Mortal) and the
//     battle ends; both at 0 is a draw and nobody is downed. After kMaxTurns,
//     more hearts wins
Outcome resolve(const BattleCard& a, const BattleCard& b, uint32_t seed, Stakes, const MoveLog& movesA,
                const MoveLog& movesB);

// What a power handler sees: the turn so far, from its own side's view.
// Handlers are called only for a side whose special landed this turn; what
// lasts past the turn lives in SideState and the turn's own steps read it.
struct PowerCtx {
  Side me;
  PowerId acting;                // the row's power, or what gadget became
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
// `special` is the autopilot's own commit: Quick or Heavy when the meter
// holds the cost and the pet is sure of its read (a spirited pet goes Heavy),
// None otherwise. Against a bracing friend the lean is Trick (stagger it); on
// the friend's landing turn it is the counter of the landing it foresees.
struct Lean { Stance stance; uint8_t whisper; Windup special = Windup::None; };
Lean petChoice(const DuelSkill&, const Outcome&, Side me, const BattleCard& mine, const BattleCard& theirs,
               uint8_t assistSteps, uint32_t seed);
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
