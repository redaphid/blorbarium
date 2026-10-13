#pragma once
// What a duel may do to a pet, chosen by both owners before it starts
// (docs/design/BATTLES.md, "Stakes"). The duel runs at the LOWER of the two
// choices:
//
//   Spar    play-fighting. No wounds, nothing lasting. A faint is a nap.
//   Scrap   real wounds: injury per heart lost, scars, and the long trait
//           chemicals drift (mettle from wins, dread from losses).
//   Mortal  bigger wounds, and the loser is DOWNED, not dead. The winner's kid
//           decides: holding BOOT for 3 s while a skull fills finishes; a knock,
//           or 15 s of nothing, spares.
//
// The one rule that makes death a choice: NO WOUND EVER KILLS. Every wound is
// clamped so his injury stays below his own lethal level, less a margin as big
// as his own drop injury (woundRoom), whatever the tier, the turn, his genes or
// a fumble mid-duel. The only death a duel causes is a Finish someone chose
// with their hands (Creature::fall). A wounded pet left untended can still die
// days later of the engine's ordinary causes, which is how wounds work.
//
// Wounds are not gene-driven: the engine applies them from the agreed Outcome
// by these tables, turn by turn. Genes decide only how a duel FEELS, behind
// duelFilter.
#include <cstdint>
#include "blorb/fixed.h"
#include "blorb/ids.h"
#include "blorb/registry.h"

namespace blorb {

class Creature;

enum class Stakes : uint8_t { Spar = 0, Scrap = 1, Mortal = 2 };
constexpr Stakes agreedStakes(Stakes a, Stakes b) { return uint8_t(a) < uint8_t(b) ? a : b; }

// Per tier, as data for the owner to tune. The confirm gesture grows with the
// stakes, so nobody agrees to a mortal duel by tapping through a screen.
struct StakesRule {
  Fx injuryPerHeart;        // engine-applied per heart lost, as each turn resolves
  Fx injuryCapPerDuel;      // the most one duel can add (before the woundRoom clamp)
  bool scars, traits, downs;
  Stage youngestStage;      // both pets must be at least this old
  uint8_t maxAssistHearts;  // the fight must be this fair (Assist::hearts) or the tier is refused
  uint16_t minManualTurns;  // both kids must have picked this many turns themselves
  bool headToHead;          // the two lines' record must not be lopsided (kHeadToHeadFloor)
  enum class Confirm : uint8_t { Knock, Press, Hold3s } confirm;
};
inline constexpr StakesRule kStakes[] = {
    /* Spar   */ {Fx::zero(), Fx::zero(), false, false, false, Stage::Baby, 2, 0, false, StakesRule::Confirm::Knock},
    /* Scrap  */ {Fx::ratio(6, 100), Fx::ratio(35, 100), true, true, false, Stage::Child, 2, 0, true,
                  StakesRule::Confirm::Press},
    /* Mortal */ {Fx::ratio(9, 100), Fx::ratio(60, 100), true, true, true, Stage::Adult, 1, 30, true,
                  StakesRule::Confirm::Hold3s},
};
constexpr const StakesRule& rule(Stakes s) { return kStakes[uint8_t(s) % 3]; }

// Mortal's fairness gate also reads the two lineages' head-to-head record in
// duels.log: with at least kHeadToHeadMin duels between them, the lower side's
// win share must be at least kHeadToHeadFloor permille. Prowess is a win share
// against whoever each kid usually plays, which does not compare across a
// parents' pool and a kids' pool; the record between these two does.
inline constexpr uint8_t kHeadToHeadMin = 3;
inline constexpr uint16_t kHeadToHeadFloor = 250;

// No farming: at most this many for-keeps duels (Scrap or Mortal) between the
// same two lineages per pet-day, counted from duels.log on both devices.
inline constexpr uint8_t kForKeepsPerPairPerDay = 1;

// ---- what a duel stimulus may touch -------------------------------------------------
inline constexpr Fx kDuelDriveRiseCap = Fx::ratio(3, 10);
constexpr bool isDriveChem(ChemId c) { return c.v >= kDriveBase && c.v < kDriveBase + kDriveCount; }

// An allowlist per tier, applied to every delta a Duel-source stimulus gene
// writes (Creature's stimulus step). Genes mutate and every body byte is legal,
// so genes set how much a duel stirs him and this sets what it may touch at
// all. No tier lets a stimulus touch life, injury, toxin or an unnamed
// chemical, because wounds come from woundFor(), not from genes.
constexpr Fx duelFilter(Stakes s, ChemId c, Fx delta) {
  if (c == chem::daze || c == chem::pluck) return delta;
  if (c == chem::mettle || c == chem::dread) return rule(s).traits ? fxMin(delta, kDuelDriveRiseCap) : Fx::zero();
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
static_assert(duelFilter(Stakes::Mortal, chem::toxin, Fx::one()) == Fx::zero(), "no stimulus poisons");
static_assert(duelFilter(Stakes::Spar, driveChem(drive::pain), Fx::one()) == Fx::zero(), "a spar never hurts");
static_assert(duelFilter(Stakes::Scrap, driveChem(drive::pain), Fx::one()) == kDuelDriveRiseCap, "a scrap stings");
static_assert(duelFilter(Stakes::Spar, chem::dread, Fx::one()) == Fx::zero(), "a spar leaves nothing lasting");
static_assert(duelFilter(Stakes::Scrap, ChemId{200}, Fx::one()) == Fx::zero(), "an unnamed chemical is refused, not trusted");

// Two expression-time rules, because the filter guards only the first step
// and genes can wire the second. genes.cpp (unit D3) applies them at
// expression, so every path that expresses a genome gets them:
//  * a receptor or reaction that READS daze or pluck is inert unless it writes
//    one of kDuelChemTargets (a swoon cannot be wired to death). A reaction
//    writes chemicals, never a locus, so every reaction reading them is inert;
//  * a receptor that WRITES the die or cause locus is inert unless it reads one
//    of kVitalChems. That guards the target, not the source: pain, adrenaline,
//    the drives, mettle and dread all rise in a duel, and a mutated receptor
//    from any of them to `die` would otherwise fire mid-Scrap. It is a rule for
//    every pet, duel or not (BATTLES.md, open question 14).
inline constexpr LocusId kDuelChemTargets[] = {locus::sleep_gate};
// The starter's starvation integrator is an unnamed chemical (kStarvation in
// starter_genome.cpp), so it is named here by id.
inline constexpr ChemId kStarvationChem{34};
inline constexpr ChemId kVitalChems[] = {chem::life, chem::injury, chem::toxin, kStarvationChem};

constexpr bool readsDuelChem(ChemId c) { return c == chem::daze || c == chem::pluck; }
constexpr bool receptorExpresses(ChemId reads, LocusId writes) {
  if (readsDuelChem(reads)) {
    bool target = false;
    for (LocusId t : kDuelChemTargets) target = target || t == writes;
    if (!target) return false;
  }
  if (writes == locus::die || writes == locus::cause) {
    bool vital = false;
    for (ChemId v : kVitalChems) vital = vital || v == reads;
    if (!vital) return false;
  }
  return true;
}
constexpr bool reactionExpresses(ChemId a, ChemId b) { return !readsDuelChem(a) && !readsDuelChem(b); }
static_assert(receptorExpresses(chem::daze, locus::sleep_gate), "a swoon may hold him asleep");
static_assert(!receptorExpresses(chem::daze, locus::die), "a swoon is never wired to death");
static_assert(!receptorExpresses(driveChem(drive::pain), locus::die), "pain alone never kills");
static_assert(receptorExpresses(chem::injury, locus::die), "injury still kills, as it always has");

// ---- wounds and scars -----------------------------------------------------------------
// A scar is for this life: drawn over the body, not inherited (the lineage
// remembers it in words). Kind follows what hit him: a claw for a Pounce, a
// scorch for Spark, a notch for Sprout, stitches for Splash, a bite for a zap.
struct Scar {
  enum class Kind : uint8_t { Claw, Scorch, Notch, Stitches, Bite } kind = Kind::Claw;
  uint8_t x = 0, y = 0;       // from the hit's luck; paint maps them onto a limb, shoulder, side or the head, never chest or belly
  uint8_t fade = 255;         // fades toward 96 over the life, never to nothing
};
inline constexpr uint8_t kMaxScars = 4;
// Up to four; a fifth replaces the faintest (Creature::addScar).
struct Scars { Scar mark[kMaxScars]{}; uint8_t count = 0; };

// How much more injury he can take and still be safe: his lethal level (the
// lowest injury at which his own expressed receptors write `die`), less the
// injury his own `dropped` stimulus genes give, less kWoundMargin, less what he
// has. Never negative. Every wound is clamped to it (Creature::wound).
inline constexpr Fx kWoundMargin = Fx::ratio(5, 100);
Fx woundRoom(const Creature&);

// What one resolved turn did to my side, as a wound needs it. D4 reduces its
// Outcome to these: the hearts I lost that turn, the kind of what hit me, and
// that turn's luck for where the scar lands.
struct TurnHit { uint8_t hearts = 0; Scar::Kind kind = Scar::Kind::Claw; uint32_t luck = 0; };
struct TurnWound {
  Fx injury{};                // already clamped to roomNow
  bool scar = false;          // the first two turns of a duel that cost him hearts
  Scar mark{};
};
// The wound of turn `turn` for my side, from `hits[0..turn]` (none in a Spar).
// The Dish applies it the tick the turn resolves and saves at once.
TurnWound woundFor(Stakes, const TurnHit* hits, uint8_t turn, Fx roomNow);

// The forecast both screens show before anyone confirms: how wounded the
// worst case (every heart lost, plus a drop) would leave him, after the clamp.
// skull 0 = untouched, 255 = at the clamp's ceiling. It never says "this could
// kill", because no wound can.
struct Danger { Fx worstInjury{}; uint8_t skull = 0; };
Danger forecast(const Creature&, Stakes);

// ---- the verdict ----------------------------------------------------------------------
// The verdict on a downed pet is the winner's kid's, and it travels as ONE
// MORE entry in the winner's MoveLog, at index `Outcome::resolved` (the slot
// right after the downing turn). Only the winner's device decides it: a 3 s
// hold writes Finish, a knock writes Spare, and after kMercyTicks with neither
// the winner's device writes Spare itself.
enum class Verdict : uint8_t { None = 0, Spare = 1, Finish = 2 };
inline constexpr uint16_t kMercyTicks = 150;      // 15 s on the winner's device
inline constexpr uint16_t kFinishHoldTicks = 30;  // BOOT held 3 s while the skull fills

}  // namespace blorb
