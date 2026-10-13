#pragma once
// SKETCH (docs/design/BATTLES.md, "Stakes"). What a duel may do to a pet,
// chosen by both owners before it starts.
//
// Owner ruling (2026-10-13): "Pets should die. My family has a dark sense of
// humor. They want something more complex." So a duel has real stakes, on a
// ladder both kids see and confirm on their own device. The duel runs at the
// LOWER of the two choices:
//
//   Spar    play-fighting. No wounds, nothing lasting. A faint is a nap.
//   Scrap   real wounds: injury and pain per heart lost, scars, and the long
//           trait chemicals drift (mettle from wins, dread from losses).
//   Mortal  bigger wounds, and the loser is DOWNED, not dead. The winner's kid
//           decides: holding BOOT for 3 s while a skull fills finishes; a knock,
//           or 15 s of nothing, spares.
//
// The one rule that makes death a choice: NO WOUND EVER KILLS. Every wound is
// clamped so his injury stays below his own lethal level, less a margin as big
// as his own drop injury (woundRoom), whatever the tier, the turn, his genes or
// a fumble mid-duel. The only death a duel causes is a Finish someone chose
// with their hands. A wounded pet left untended can still die days later of
// the engine's ordinary causes, which is how wounds work.
//
// Wounds are not gene-driven: the engine applies them from the agreed Outcome
// by these tables, turn by turn as each turn resolves, and saves at once, so
// pulling the cable never undoes one. Genes decide only how a duel FEELS,
// behind duelFilter.
#include "blorb/creature.h"
#include "blorb/fixed.h"
#include "blorb/ids.h"
#include "blorb/registry.h"

namespace blorb {

enum class Stakes : uint8_t { Spar = 0, Scrap = 1, Mortal = 2 };
constexpr Stakes agreedStakes(Stakes a, Stakes b) { return uint8_t(a) < uint8_t(b) ? a : b; }

// Per tier, as data. The confirm gesture grows with the stakes, so nobody
// agrees to a mortal duel by tapping through a screen.
struct StakesRule {
  Fx injuryPerHeart;        // engine-applied per heart lost, as each turn resolves
  Fx injuryCapPerDuel;      // the most one duel can add (before the woundRoom clamp)
  bool scars, traits, downs;
  Stage youngestStage;      // both pets must be at least this old
  uint8_t maxAssistHearts;  // the fight must be this fair (Assist::hearts) or the tier is refused
  uint16_t minManualTurns;  // both kids must have picked this many turns themselves (prowess is real)
  enum class Confirm : uint8_t { Knock, Press, Hold3s } confirm;
};
inline constexpr StakesRule kStakes[] = {
    /* Spar   */ {Fx::zero(), Fx::zero(), false, false, false, Stage::Baby, 2, 0, StakesRule::Confirm::Knock},
    /* Scrap  */ {Fx::ratio(6, 100), Fx::ratio(35, 100), true, true, false, Stage::Child, 2, 0, StakesRule::Confirm::Press},
    /* Mortal */ {Fx::ratio(9, 100), Fx::ratio(60, 100), true, true, true, Stage::Adult, 1, 30, StakesRule::Confirm::Hold3s},
};
constexpr const StakesRule& rule(Stakes s) { return kStakes[uint8_t(s) % 3]; }

// Mortal's fairness gate also reads the two lineages' head-to-head record in
// duels.log: with at least kHeadToHeadMin duels between them, the lower side's
// win share must be at least kHeadToHeadFloor permille. Prowess alone is a win
// share against whoever each kid usually plays, which does not compare across
// a parents' pool and a kids' pool; the record between these two does.
inline constexpr uint8_t kHeadToHeadMin = 3;
inline constexpr uint16_t kHeadToHeadFloor = 250;

// Stand-ins until the rows land (stimuli.def, chemicals.def and feats.def are unit D2's).
//   BLORB_STIM(43, duel_bonked,     Duel, 0)  // I lost a heart this turn
//   BLORB_STIM(44, duel_landed,     Duel, 0)  // I bonked them
//   BLORB_STIM(45, duel_won,        Duel, 0)
//   BLORB_STIM(46, duel_fainted,    Duel, 0)  // the swoon (any tier): daze floods, he naps
//   BLORB_STIM(47, duel_befriended, Duel, 1)  // the duel is over and they know each other (both sides, always)
//   BLORB_STIM(42, duel_spared,     Duel, 0)  // downed, and spared: relief, a debt
//   BLORB_STIM(41, duel_slew,       Duel, 0)  // I finished a friend's pet: pride or remorse, by his genes
//   BLORB_STIM(40, duel_mourned,    Duel, 0)  // a pet I knew fell (heard from the book at the next duel)
//   BLORB_CHEM(25, daze,   0xC81F, 1)  // the swoon; half-life 90 s; receptor -> sleep_gate
//   BLORB_CHEM(26, pluck,  0xFD20, 1)  // the battle high; reacts away boredom
//   BLORB_CHEM(27, mettle, 0xE8A0, 1)  // a trait: half-life about 6 pet-days; wins feed it; read as nerve
//   BLORB_CHEM(28, dread,  0xE8A0, 1)  // a trait: half-life about 6 pet-days; losses and scars feed it
//   BLORB_FEAT(6, slayer)              // awarded at death from duels.log (the Dish ORs it in), never from LifeStats
namespace duelstim {
inline constexpr StimId bonked{43}, landed{44}, won{45}, fainted{46}, befriended{47}, spared{42}, slew{41},
    mourned{40};
}
namespace duelchem {
inline constexpr ChemId daze{25}, pluck{26}, mettle{27}, dread{28};
}

inline constexpr Fx kDuelDriveRiseCap = Fx::ratio(3, 10);
constexpr bool isDriveChem(ChemId c) { return c.v >= kDriveBase && c.v < kDriveBase + kDriveCount; }

// The stimulus filter, an allowlist per tier. Spar allows what a play-fight
// may do; Scrap and Mortal also let a duel stimulus raise pain and the trait
// chemicals. No tier lets a stimulus touch life, injury, toxin or an unnamed
// chemical directly, because wounds come from woundFor(), not from genes.
constexpr Fx duelFilter(Stakes s, ChemId c, Fx delta) {
  if (c == duelchem::daze || c == duelchem::pluck) return delta;
  if (c == duelchem::mettle || c == duelchem::dread) return rule(s).traits ? fxMin(delta, kDuelDriveRiseCap) : Fx::zero();
  if (c == chem::adrenaline) return delta <= Fx::zero() ? delta : fxMin(delta, kDuelDriveRiseCap);
  if (isDriveChem(c)) {
    if (delta <= Fx::zero()) return delta;
    if (c == driveChem(drive::pain) && s == Stakes::Spar) return Fx::zero();
    return fxMin(delta, kDuelDriveRiseCap);
  }
  return Fx::zero();
}
static_assert(duelFilter(Stakes::Mortal, chem::injury, Fx::one()) == Fx::zero(), "wounds are the engine's, never a gene's");
static_assert(duelFilter(Stakes::Mortal, chem::life, -Fx::one()) == Fx::zero(), "no stimulus shortens a life");
static_assert(duelFilter(Stakes::Spar, driveChem(drive::pain), Fx::one()) == Fx::zero(), "a spar never hurts");
static_assert(duelFilter(Stakes::Scrap, driveChem(drive::pain), Fx::one()) == kDuelDriveRiseCap, "a scrap stings");
static_assert(duelFilter(Stakes::Spar, duelchem::dread, Fx::one()) == Fx::zero(), "a spar leaves nothing lasting");
static_assert(duelFilter(Stakes::Scrap, ChemId{200}, Fx::one()) == Fx::zero(), "an unnamed chemical is refused, not trusted");

// Two expression-time guards (genes.cpp, unit D3), because the filter guards
// only the first step and genes can wire the second:
//  * a receptor or reaction that READS daze or pluck is inert unless it writes
//    one of kDuelChemTargets (a swoon cannot be wired to death);
//  * a receptor that WRITES the die or cause locus is inert unless it reads one
//    of kVitalChems. That guards the target, not the source: pain, adrenaline,
//    the drives, mettle and dread all rise in a duel, and a mutated receptor
//    from any of them to `die` would otherwise fire mid-Scrap. It is an
//    expression rule for every pet, duel or not (called out in BATTLES.md).
inline constexpr LocusId kDuelChemTargets[] = {locus::sleep_gate};
inline constexpr ChemId kVitalChems[] = {chem::life, chem::injury, chem::toxin};   // plus the starvation chemical, by id in starter_genome.cpp

// ---- wounds and scars --------------------------------------------------------------------
// A scar is for this life: drawn over the body, not inherited (the lineage
// remembers it in words). Kind follows what hit him: a claw for a Pounce, a
// scorch for Spark, a notch for Sprout, a ripple for Splash, a ring for a zap.
// Up to four; a fifth replaces the faintest.
struct Scar {
  enum class Kind : uint8_t { Claw, Scorch, Notch, Ripple, Ring } kind = Kind::Claw;
  uint8_t x = 0, y = 0;       // on the body frame, 0..255 of its bbox, chosen from the hit's luck
  uint8_t fade = 255;         // fades toward 96 over the life, never to nothing
};
inline constexpr uint8_t kMaxScars = 4;

// How much more injury he can take and still be safe: his lethal level (the
// lowest injury at which his own expressed receptors write `die`), less the
// injury his own `dropped` stimulus genes give, less 0.05, less what he has.
// Never negative. Every wound is clamped to it.
Fx woundRoom(const Creature&);

struct Outcome;
enum class Side : uint8_t;
struct TurnWound {
  Fx injury{};                // clamped to woundRoom at the moment it is applied
  bool scar = false;          // one of the two hardest turns of the duel, decided when it resolves
  Scar mark{};
};
// The wound of one resolved turn for my side (none in a Spar). The Dish
// applies it the tick the turn resolves and saves at once.
TurnWound woundFor(Stakes, const Outcome&, Side me, uint8_t turn, Fx roomNow);

// The forecast both screens show before anyone confirms, per tier: how close
// the worst case (every heart lost, plus a drop) comes to his lethal level,
// after the clamp. 0 = nowhere near, 255 = at the margin. It never says "this
// could kill", because no wound can; it says how wounded he would end up.
struct Danger { Fx worstInjury{}; uint8_t skull = 0; };
Danger forecast(const Creature&, Stakes);

// Creature gains (creature.h and creature.cpp, unit D2):
//   void wound(Fx injury);            // adds to chem::injury, clamped by woundRoom
//   bool addScar(Scar);               // into Body's scars (keepsake: the Body chunk's trailing bytes)
//   void fall();                      // die + cause loci as Injured: the ordinary death path runs
// and Appearance gains `Scar scars[kMaxScars]; uint8_t scarCount;` (drawn by paint).

// ---- the verdict -------------------------------------------------------------------------
// The verdict on a downed pet is the winner's kid's, and it travels as ONE
// MORE entry in the winner's MoveLog, at index `Outcome::resolved` (the slot
// right after the downing turn). Only the WINNER's device ever decides it: a
// 3 s hold writes Finish, a knock writes Spare, and after kMercyTicks with
// neither the winner's device writes Spare itself. The downed side never times
// out on its own; it waits for that slot (bounded by kSilentTicks, after which
// the victor "left before the verdict" and the downed pet lives). So both
// devices read one verdict, and a lost packet only delays it.
//
// The winner applies the slaying (duel_slew, the trophy, the slayer feat) only
// once the downed side's `heard` covers the verdict slot. If that ack never
// comes within kSilentTicks, the winner records "they left before the
// verdict" and no kill. The downed side applies fall() the tick it reads
// Finish, then keeps sending its State (from the Fallen scene) so the ack
// arrives. A device that holds a verdict never cancels it for a lid.
enum class Verdict : uint8_t { None = 0, Spare = 1, Finish = 2 };
inline constexpr uint16_t kMercyTicks = 150;      // 15 s on the winner's device
inline constexpr uint16_t kFinishHoldTicks = 30;  // BOOT held 3 s while the skull fills; letting go early stops the fill

}  // namespace blorb
