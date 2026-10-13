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
//   Scrap   real wounds: injury and pain per heart lost, a scar per hard hit,
//           and the long trait chemicals drift (mettle from wins, dread from
//           losses). A scrap can never kill by itself: the engine forecasts
//           the worst case and refuses a scrap that could. Wounds heal by the
//           pet's own chemistry, and a wounded, neglected pet can still die
//           later of the engine's ordinary causes.
//   Mortal  bigger wounds, and the loser is DOWNED, not dead. The winner's kid
//           decides: a knock (or 15 s of nothing) spares; holding BOOT for 3 s
//           while a skull fills finishes. Death is always a choice someone made
//           with their hands, never a roll of luck.
//
// Death has to mean something, so it feeds the lineage (BATTLES.md, "When a
// pet falls"): the fallen leaves remains, a clutch and a vendetta; the victor
// keeps a trophy, a scar, a feat and a memory.
//
// What the filter guards is the step from a duel stimulus into chemistry.
// Stimulus genes mutate and every body byte is legal, so the amounts are the
// genes' but the LIMITS are code. Wounds themselves are not gene-driven: the
// engine applies them from the agreed Outcome by these tables, so no mutation
// can make a scrap lethal.
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
  Fx injuryPerHeart;        // engine-applied per heart lost
  Fx injuryCapPerDuel;      // the most one duel can add
  bool scars, traits, downs;
  Stage youngestStage;      // both pets must be at least this old
  uint8_t maxAssistHearts;  // the fight must be this fair (Assist::hearts) or the tier is refused
  enum class Confirm : uint8_t { Knock, Press, Hold3s } confirm;
};
inline constexpr StakesRule kStakes[] = {
    /* Spar   */ {Fx::zero(), Fx::zero(), false, false, false, Stage::Baby, 2, StakesRule::Confirm::Knock},
    /* Scrap  */ {Fx::ratio(6, 100), Fx::ratio(35, 100), true, true, false, Stage::Child, 2, StakesRule::Confirm::Press},
    /* Mortal */ {Fx::ratio(9, 100), Fx::ratio(60, 100), true, true, true, Stage::Adult, 1, StakesRule::Confirm::Hold3s},
};
constexpr const StakesRule& rule(Stakes s) { return kStakes[uint8_t(s) % 3]; }

// Stand-ins until the rows land (stimuli.def and chemicals.def are unit D2's).
// Ids sit in the gap v2 leaves.
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
namespace duelstim {
inline constexpr StimId bonked{43}, landed{44}, won{45}, fainted{46}, befriended{47}, spared{42}, slew{41},
    mourned{40};
}
namespace duelchem {
inline constexpr ChemId daze{25}, pluck{26}, mettle{27}, dread{28};
}

inline constexpr Fx kDuelDriveRiseCap = Fx::ratio(3, 10);
constexpr bool isDriveChem(ChemId c) { return c.v >= kDriveBase && c.v < kDriveBase + kDriveCount; }

// The stimulus filter, an allowlist. Spar allows what a play-fight may do. Scrap and Mortal
// also let a duel stimulus raise pain (it hurts) and the trait chemicals; no
// tier lets a stimulus touch life, injury, toxin or an unnamed chemical
// directly, because wounds come from woundFor(), not from genes.
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

// Where a gene reading daze or pluck may write; anything else is inert at
// expression (genes.cpp, unit D3), so a mutation cannot wire a swoon to death.
inline constexpr LocusId kDuelChemTargets[] = {locus::sleep_gate};

// ---- wounds and scars, from the agreed Outcome -------------------------------------------
// A scar is for this life: drawn over the body, not inherited (the lineage
// remembers it in words). Kind follows what hit him: a claw for a Pounce, a
// scorch for Spark, a bite-notch for Sprout, a ripple for Splash, a ring for a
// zap. Up to four; a fifth replaces the faintest.
struct Scar {
  enum class Kind : uint8_t { Claw, Scorch, Notch, Ripple, Ring } kind = Kind::Claw;
  uint8_t x = 0, y = 0;       // on the body frame, 0..255 of its bbox, chosen from the hit's luck
  uint8_t fade = 255;         // fades toward 96 over the life, never to nothing
};
inline constexpr uint8_t kMaxScars = 4;

struct Wound {
  Fx injury{};                // added to chem::injury once, at the Meeting (capped by the tier)
  Scar scars[2]{};            // at most two new scars a duel (the two hardest turns)
  uint8_t scarCount = 0;
  bool downed = false;        // Mortal: fell to 0 hearts; the verdict decides
};
struct Outcome;
enum class Side : uint8_t;
Wound woundFor(Stakes, const Outcome&, Side me);

// The forecast both screens show before anyone confirms: would the worst case
// of this tier leave him alive? A coarse dry run (the viability machinery's
// stepCoarse) of his chemistry after injuryCapPerDuel lands now. A Scrap that
// could kill is refused; a Mortal shows a skull and how close it is.
struct Danger {
  bool scrapCouldKill = false;
  Fx worstInjury{};           // where injury would peak
  uint8_t skull = 0;          // 0..255: how close the worst case comes to the die receptor
};
Danger forecast(const Creature&, Stakes);

// Creature gains (creature.h, unit D3; creature.cpp, unit D2):
//   void wound(Fx injury);            // adds to chem::injury once; the pet's own receptors decide the rest
//   bool addScar(Scar);               // into Body's scars (keepsake: trailing bytes on the Body chunk)
//   void fall();                      // die + cause loci as Injured: the ordinary death path runs
// and Appearance gains `Scar scars[kMaxScars]; uint8_t scarCount;` (drawn by paint).

// The verdict on a downed pet, the winner's kid's to give. It travels as the
// last entry of the winner's MoveLog (duel.h), so both devices read the same
// verdict, and a missing one is Spare after kMercyTicks.
enum class Verdict : uint8_t { None = 0, Spare = 1, Finish = 2 };
inline constexpr uint16_t kMercyTicks = 150;     // 15 s
inline constexpr uint16_t kFinishHoldTicks = 30; // BOOT held 3 s while the skull fills; letting go early spares nothing yet

}  // namespace blorb
