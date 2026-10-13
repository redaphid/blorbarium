#pragma once
// SKETCH (docs/design/BATTLES.md). The duel's rules: pure, integer, no clock,
// no radio.
//
// Each turn both kids secretly pick a stance with the body (tilt to a rim
// sector, knock to lock in; BOOT fires the power when the spark meter holds
// its cost). A turn resolves only when BOTH stances for it are known, and
// every device computes the whole battle from scratch with one function:
//
//   resolve(cardA, cardB, seed, movesA, movesB) -> Outcome
//
// Same inputs, same Outcome, on x86, Xtensa and WASM (integers and fnv only).
// The result never depends on which device is side A: luck is drawn per side
// from fnv(seed, turn, that side's lineage), never from one shared stream,
// and powers in one phase read the same inputs. test_duel_resolve checks
// resolve(B, A, s, mB, mA) is the mirror of resolve(A, B, s, mA, mB).
//
// Stakes (stakes.h) are agreed before turn 0 and are an input like the cards:
// resolve() knows whether a side at 0 hearts faints (Spar, Scrap) or is
// downed and waits for a verdict (Mortal). Nothing in here touches a
// creature: each device applies its OWN pet's wound, scars and fate from the
// shared Outcome (dish_duel.h), so both screens agree on what happened.
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
  static constexpr uint8_t kElementHeadStart = 1;   // SUPER: the side whose element wins starts with a pip lit
  static constexpr uint8_t kMaxTurns = 15;          // then whoever has more hearts wins; equal is a draw
  static constexpr uint8_t kLogSlots = kMaxTurns + 1;   // one more for the winner's verdict on a downed pet
  static constexpr uint8_t kMaxLevel = 12;          // display only: level never changes a number in resolve()
  static constexpr uint8_t kCardVersion = 1;
};

enum class Side : uint8_t { A = 0, B = 1 };   // also declared in stakes.h
constexpr Side other(Side s) { return s == Side::A ? Side::B : Side::A; }

// One locked choice. `byPet` marks a turn the kid let run out, or a turn the
// pet slept through under spores: the pet chose (its Skill), which the screen
// celebrates rather than scolds. A dozing side's move is appended at once by
// the session, so the logs keep marching; resolve() ignores its stance.
// After a Mortal downing on turn t, the winner appends one more Move at index
// t + 1 (== Outcome::resolved) whose `verdict` is Spare or Finish; its stance
// is ignored, and resolve() reads a verdict only from the winner's log at that
// index. kLogSlots leaves room for it after a downing on the last turn.
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
// about the battle (per-actor state; nothing is shared). My log grows by
// append(); the friend's grows only by adopt() from a packet. A packet is
// "everything that side has committed so far", so a lost, doubled or late
// packet is a stale prefix, adopt() keeps the longest, and both ends converge.
class MoveLog {
 public:
  uint8_t count() const { return count_; }
  Move at(uint8_t turn) const { return Move::unpack(packed_[turn]); }
  const uint8_t* packed() const { return packed_; }
  bool append(Move);                 // false once kLogSlots are in

  enum class Merge : uint8_t { Same, Grew, Stale, Conflict };
  // Conflict: the heard prefix disagrees with what is held. Only a bug or a
  // different battle can do that; the session ends the duel as a draw.
  Merge adopt(const uint8_t* packed, uint8_t count);

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
// in resolve(): a frightened pet leans Guard, a bored one Pounce, a pet who
// trusts his kid leans the way his kid usually goes. Frozen so both devices
// read the same bytes; his live chemistry goes on living beside the duel.
struct Mood { uint8_t nerve = 128, pep = 128, spirit = 128, trust = 128; };

// Enough of a pet to draw it on the friend's screen and in the friend book.
// No name and no free text: what crosses the air is a species, tints, marks
// and a stage (a friend's pet is shown, never named, unless the owner names
// it in the friend book from the phone).
struct CardLook {
  Tint regions[kRegionCount]{};
  uint8_t scalePct = 100;
  Stage stage = Stage::Adult;
  uint8_t markCount = 0;
  struct Mark { uint8_t layer, variant; } marks[4]{};
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
enum class Clash : uint8_t { AWins, BWins, Bonk };   // Bonk: same stance, heads bonk, nobody hurt

struct TurnReport {
  Move move[2]{};          // as locked, by side
  Stance shown[2]{};       // after foresee: what the clash animation plays
  bool dozed[2]{};         // slept through it (spores)
  Clash clash = Clash::Bonk;
  uint8_t hit[2]{};        // hearts each side lost
  uint8_t healed[2]{};
  PowerId fired[2]{};      // valid where powered[i]; for gadget, the power it became
  bool powered[2]{};
  bool blocked[2]{};       // a shell, a hush or a dazzle turned a hit away (the "bonk off" sparkle)
  bool super[2]{};         // display only: the clash winner's element beats the loser's ("SUPER!")
  bool zone[2]{};          // display only: won with its knack's best stance (a sparkle)
};

// One side's state between turns. The counters are how multi-turn powers
// (dash, lantern, hush, spores, shell) last, and they are part of the
// replayed state, so they are as deterministic as hearts.
struct SideState {
  uint8_t hearts = DuelRules::kHearts, maxHearts = DuelRules::kHearts;
  uint8_t spark = 0;                // 0..kMeterMax
  bool shell = false;               // the next landing hit bonks off
  bool dozing = false;              // next turn: no stance, no spark
  uint8_t bonkTurns = 0;            // turns left winning head bonks (dash, lantern)
  uint8_t dazzledTurns = 0;         // turns left in which MY winning hits miss half the time (lantern)
  uint8_t hushTurns = 0;            // turns left in which a lost clash costs me nothing (hush)
};

enum class Ending : uint8_t { Ongoing, AWon, BWon, Draw };

struct Outcome {
  Stakes stakes = Stakes::Spar;
  SideState side[2]{};
  TurnReport turns[DuelRules::kMaxTurns]{};
  uint8_t resolved = 0;    // turns with both moves known; turns past it are unreadable by design
  Ending ending = Ending::Ongoing;
  bool downed[2]{};        // Mortal: fell to 0 hearts (Spar and Scrap: fainted, nothing pending)
  Verdict verdict = Verdict::None;   // Mortal: the winner's, once it arrives; None while pending
  bool awaitingVerdict() const { return stakes == Stakes::Mortal && (downed[0] != downed[1]) && verdict == Verdict::None; }
  uint32_t hash() const;   // both devices log it; the two books can be compared
};

// Symmetric in the two nonces, so neither device has to be "first". The
// rematch number is part of the seed AND of the battle id on the wire, so a
// late packet from the previous bout can never feed the next one.
uint32_t duelSeed(uint32_t nonceA, uint32_t nonceB, uint8_t rematch, Stakes);

// The battle's name, in both books and on the air. Before turn 0 (Greeting,
// Staking) the pair is named by `stakes` = Spar and the session matches on
// `seed` alone; from turn 0 on, the id carries the agreed stakes, so two
// devices can never resolve one battle at two tiers. A rematch is a fresh open
// with fresh nonces (dish_duel.h), so its id differs and a late packet from
// the last bout never feeds it; `rematch` only numbers bouts for the book.
// The stakes are part of the id too: two devices can never resolve one battle
// at two different stakes.
struct BattleId {
  uint32_t seed = 0;     // duelSeed(nonceA, nonceB, rematch, stakes)
  uint8_t rematch = 0;
  Stakes stakes = Stakes::Spar;
  friend bool operator==(BattleId a, BattleId b) { return a.seed == b.seed && a.rematch == b.rematch && a.stakes == b.stakes; }
  friend bool operator!=(BattleId a, BattleId b) { return !(a == b); }
};

// The handicap (graft from candidate C): skill, not level. Each card carries
// its kid's prowess, a running mean of how well that kid has been playing
// (duel_book.h). Inside a dead band nobody gets help; past it the weaker side
// gets one extra heart per step, at most two. The stronger side never loses
// anything, and a kid who improves earns the help away. The probe measured a
// reading parent against a habit-bound kid at 0.923 with no help and 0.801
// with two hearts; following the pet's lean is the kid's bigger lever (0.491
// at no help), which is why the lean glows brighter as the assist rises.
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
// and the side whose element beats the other's starts with
// kElementHeadStart pips. Inside one turn:
//   1 a dozing side has no stance this turn (it loses the clash, deals nothing)
//   2 powers: a side whose move asks for power and whose meter holds the
//     cost spends it; gadget draws the power it becomes from its own luck.
//     BeforeClash handlers read both ORIGINAL stances (foresee turns its
//     stance into the counter; dash and lantern set bonkTurns; lantern
//     dazzles the friend; hush sets hushTurns)
//   3 clash: beats(), or a head bonk on equal stances, which a side with
//     bonkTurns > 0 wins (both or neither: still a bonk)
//   4 damage: 1 heart to the clash loser; OnDamage handlers only ADD hits (zap)
//   5 Block handlers only TAKE hits away (shell, hush, a dazzled winner's
//     luck missing), reading the hits both OnDamage passes left
//   6 AfterDamage handlers (mend, spores); then sparks: +1 to each side not
//     dozing, +1 more to the clash loser (the rally), capped at kMeterMax;
//     the turn counters tick down. Nothing rewards the winner: the probe
//     measured every winner reward snowballing.
//   7 a side at 0 hearts faints (Spar, Scrap) or is downed (Mortal): the
//     battle ends (both at 0: a draw, and nobody is downed). A downed side's
//     fate is the winner's verdict, read from the winner's last log slot.
// A powered move whose meter is short plays as an unpowered move.
Outcome resolve(const BattleCard& a, const BattleCard& b, uint32_t seed, Stakes, const MoveLog& movesA,
                const MoveLog& movesB);

// What a power handler sees: the turn so far, from its own side's view.
struct PowerCtx {
  Side me;
  PowerId acting;                // the row's power, or what gadget became
  const BattleCard& mine;
  const BattleCard& theirs;
  const Stance (&original)[2];   // as locked, before any handler
  Stance (&shown)[2];            // what the clash plays
  Clash& clash;
  uint8_t (&hit)[2];
  bool (&blocked)[2];
  SideState (&side)[2];
  uint32_t luck;                 // fnv(seed, turn, mine.lineageId): this side's own luck;
                                 // each use draws fnv(luck, purpose), so gadget's pick and a
                                 // dazzle miss in one turn are independent
};
#define BLORB_POWER(id, name, cost, glyph) void power_##name(PowerPhase, PowerCtx&);
#include "blorb/defs/powers.def"
#undef BLORB_POWER
using PowerFn = void (*)(PowerPhase, PowerCtx&);
inline constexpr PowerFn POWER_HANDLERS[] = {
#define BLORB_POWER(id, name, cost, glyph) &power_##name,
#include "blorb/defs/powers.def"
#undef BLORB_POWER
};
static_assert(countOf(POWER_HANDLERS) == kPowerCount, "one handler per powers.def row");

// ---- the pet's own mind in a duel ------------------------------------------------------
// What the pet would pick, and how it learns (graft from candidate B: keyed
// by who it is fighting). Context = the friend's element (3) x the friend's
// last shown stance or none (4) = 12; moves = the three stances. This is v2's
// Skill/chooseMove/learnMove for a games.def row (duel: 12 contexts x 3
// moves); until v2's Games chunk exists it is its own small table, saved in
// duels.log.
struct DuelSkill {
  static constexpr uint8_t kContexts = 12;
  Q15 win[kContexts][kCycle]{};
  uint16_t battles = 0;
};
uint8_t duelContext(const Outcome&, Side me, const BattleCard& theirs);
// Argmax of win, plus the knack's lean, plus the mood's lean, plus noise from
// fnv(seed, turn). Reads only resolved turns, so the pet never peeks at an
// unpaired move. `whisper` (0..255) is how brightly the screen shows it: it
// rises with the assist, so a young kid is shown the reading the pet has made.
struct Lean { Stance stance; uint8_t whisper; };
Lean petChoice(const DuelSkill&, const Outcome&, Side me, const BattleCard& mine, const BattleCard& theirs,
               uint8_t assistHearts, uint32_t seed);
// After each resolved turn: NLMS toward "this stance won here".
void learnTurn(DuelSkill&, const Outcome&, Side me, const BattleCard& theirs, uint8_t turn);

// ---- shake: the pet tells the future (ask 25) --------------------------------------------
// A shake in a duel is still the 8-ball: the pet prophesies the friend's next
// stance, in its voice, on its own screen. It is a real prediction from the
// friend's resolved history (their habit, their species' style), judged when
// the turn reveals: kept fires prophecy_kept (pride), broken is sheepish.
// Grungo reads two turns of pattern, not one, so the seer is the better seer.
// One prophecy per turn; a second shake repeats it (and still hops him).
struct Foretelling {
  Stance stance = Stance::Pounce;
  Fx confidence{};   // shown as how hard the bubble glows
};
Foretelling foretell(const Outcome&, Side about, const BattleCard& seer, const BattleCard& friendCard, uint32_t seed);

}  // namespace blorb
