#pragma once
// SKETCH (docs/design/BATTLES.md). What a duel may do to a pet's body: the owner's rule,
// "a battle never kills, injures or permanently weakens a pet", as code.
//
// Duel outcomes reach a creature only as stimuli (the v2 contract: a game
// touches drives only through stimuli, so the brain and genes learn from it).
// Stimulus genes mutate, and their chemical byte is MutRule::Any, so a
// descendant could carry a gene that makes `duel_bonked` raise injury. The
// rule therefore cannot live in the genes. It lives here: Creature's stimulus
// step passes every delta of a Duel-sourced stimulus through kidSafe(), which
// is an allowlist, not a blocklist, so a chemical added next year is refused
// until someone decides it is safe.
//
// Allowed: drives may fall freely and rise a little (a flinch of fear, a
// sleepy swoon); adrenaline, daze and pluck may move. Refused: life, injury,
// toxin, pain rising, every private chemical a mutation might wire to the die
// locus. The faint is daze -> sleep_gate: a nap, nothing more.
#include "blorb/fixed.h"
#include "blorb/ids.h"
#include "blorb/registry.h"

namespace blorb {

// Stand-ins until the rows land (stimuli.def and chemicals.def are not this
// sketch's to edit). Ids sit in the gap v2 leaves (43..47 between v2's 42 and
// 48), with a new StimSource::Duel so the guard can find them.
//   BLORB_STIM(43, duel_bonked,    Duel, 0)  // I lost a heart this turn
//   BLORB_STIM(44, duel_landed,    Duel, 0)  // I bonked them
//   BLORB_STIM(45, duel_won,       Duel, 0)
//   BLORB_STIM(46, duel_fainted,   Duel, 0)  // the swoon: daze floods, he naps
//   BLORB_STIM(47, duel_befriended, Duel, 1) // the battle is over and they are friends (both sides, always)
//   BLORB_CHEM(25, daze,  0xC81F, 1)  // the faint; half-life 90 s; receptor -> sleep_gate
//   BLORB_CHEM(26, pluck, 0xFD20, 1)  // the battle high; reacts away boredom
namespace duelstim {
inline constexpr StimId bonked{43}, landed{44}, won{45}, fainted{46}, befriended{47};
}
namespace duelchem {
inline constexpr ChemId daze{25}, pluck{26};
}

// The largest rise a duel stimulus may give a drive: a flinch, not a fright.
inline constexpr Fx kDuelDriveRiseCap = Fx::ratio(3, 10);

constexpr bool isDriveChem(ChemId c) { return c.v >= kDriveBase && c.v < kDriveBase + kDriveCount; }

// The whole rule. Pure, so it is unit-tested directly and static_asserted.
constexpr Fx kidSafe(ChemId c, Fx delta) {
  if (c == duelchem::daze || c == duelchem::pluck || c == chem::adrenaline) return delta;
  if (isDriveChem(c)) {
    if (delta <= Fx::zero()) return delta;
    if (c == driveChem(drive::pain)) return Fx::zero();
    return fxMin(delta, kDuelDriveRiseCap);
  }
  return Fx::zero();
}

static_assert(kidSafe(chem::injury, Fx::one()) == Fx::zero(), "a duel never injures");
static_assert(kidSafe(chem::life, -Fx::one()) == Fx::zero(), "a duel never shortens a life");
static_assert(kidSafe(chem::toxin, Fx::one()) == Fx::zero(), "a duel never poisons");
static_assert(kidSafe(driveChem(drive::pain), Fx::one()) == Fx::zero(), "a duel never hurts");
static_assert(kidSafe(ChemId{200}, Fx::one()) == Fx::zero(), "an unnamed chemical is refused, not trusted");
static_assert(kidSafe(driveChem(drive::loneliness), -Fx::one()) == -Fx::one(), "a duel may comfort freely");
static_assert(kidSafe(driveChem(drive::fear), Fx::one()) == kDuelDriveRiseCap, "a fright is capped to a flinch");

}  // namespace blorb
