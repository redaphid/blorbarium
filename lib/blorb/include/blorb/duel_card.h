#pragma once
// The creature -> battle card, and whether he is up for a duel at all
// (docs/design/BATTLES.md, "Traits and learning" and "Consent and discovery").
//
// The card is DERIVED, never saved: built when the owner opens a duel, from
// the creature as he is now, and frozen for that duel so both devices read the
// same bytes while his live chemistry goes on beside it. Nothing about it can
// be set from the phone.
//
//   species, power, knack   Phenotype.species (the species, power and knack genes)
//   mood                    his chemistry right now (cardMood)
//   level, prowess          the duel book's (duel_book.h): a badge, and the assist's input
//   look                    the tints, marks and size present() draws him with
#include <cstdint>
#include "blorb/creature.h"
#include "blorb/duel.h"
#include "blorb/stakes.h"

namespace blorb {

BattleCard cardOf(const Creature&, uint64_t lineageId, uint8_t level, uint8_t prowess);

// Each mood byte moves 128 per unit of what feeds it, from where a fed,
// rested starter sits:
//   nerve   128 + mettle - dread - fear: a battle-hardened pet leans bolder, a
//           beaten or frightened one leans Guard
//   pep     128 + boredom + energy above his resting 0.75: a restless pet leans Pounce
//   spirit  128 - sleepiness - pain
//   trust   128 + (0.5 - loneliness - need_touch): a pet his kid keeps company
//           sits near 192 and leans gently the way his kid usually goes, less
//           than one knack rank; a lonely one does not
Mood cardMood(const Chemistry&);

// Why not, as a picture on both screens (ART3). Empathy, not an error, and the
// session limit parents want, made of the pet's own state rather than a timer.
// readiness() gives the first seven; ceilingFor() gives the next four, one per
// tier he may not enter; the session adds Unfair to the same Ceiling::why.
enum class Decline : uint8_t {
  None,
  NotACreature,   // an egg, a clutch or a pet already gone: nothing to duel with
  Asleep,         // zzz
  Sleepy,         // sleepiness >= 0.8: duels make him sleepy, so about five an hour
  Hungry,         // hunger >= 0.8: an empty bowl; feed him first
  Resting,        // within 2 minutes of his last duel's end, 5 after a faint
  WantsACuddle,   // three losses in a row: cradle him and he is ready again
  TooYoung,       // a cradle: younger than the tier's youngest stage
  Wounded,        // a bandage: injury 0.5 or more; he still plays, he just does not fight
  Fragile,        // a skull: the tier's worst case would take all his woundRoom
  DeviceCeiling,  // a lock: a grown-up lowered this device's ceiling (TWIST stakes)
  Unfair,         // a balance: not a fair fight (the assist, too few picked turns, a lopsided head-to-head, or today's for-keeps duel already fought)
};

inline constexpr Fx kDeclineSleepy = Fx::ratio(8, 10);
inline constexpr Fx kDeclineHungry = Fx::ratio(8, 10);
inline constexpr uint32_t kRestTicks = 2 * kTicksPerMinute;
inline constexpr uint32_t kRestAfterFaintTicks = 5 * kTicksPerMinute;
inline constexpr uint8_t kLossesBeforeACuddle = 3;
inline constexpr Fx kWoundedInjury = Fx::ratio(1, 2);

// ticksSinceLastDuel is UINT32_MAX for a pet who has never duelled. A cradle
// resets lossesInARow in the book, not here.
Decline readiness(const Occupant&, uint8_t lossesInARow, uint32_t ticksSinceLastDuel, bool faintedLast);

// The highest tier he may enter now, and for each tier the picture for why
// not (None for the tiers he may). A tier is his only if every tier below it
// is, so `top` is where the first refusal stops him. The pet's own limits are
// here: his stage against StakesRule::youngestStage, a Spar while he is
// Wounded, a tier whose forecast() skull is full, and the device's ceiling.
// The fairness rules (maxAssistHearts, minManualTurns, the head-to-head floor)
// need both cards and are the session's.
struct Ceiling {
  Stakes top = Stakes::Spar;
  Decline why[kStakesTiers]{};   // by Stakes
};
Ceiling ceilingFor(const Creature&, Stakes deviceCeiling);

}  // namespace blorb
