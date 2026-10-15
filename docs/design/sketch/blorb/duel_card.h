#pragma once
// SKETCH (docs/design/BATTLES.md). The creature -> battle card, and whether
// he is up for a duel at all.
//
// The card is DERIVED, never saved: built when the owner opens a duel, from
// the creature as it is now. Nothing about it can be set from the phone.
//
//   species, power   Phenotype.species (express_species / express_power)
//   knack            Phenotype.species.knackOrder (express_knack)
//   mood             his chemistry right now, frozen (fear -> nerve, energy -> pep,
//                    boredom -> spirit, fondness or low loneliness -> trust)
//   level, prowess   DuelBook (this life's duel XP; the kid's running performance)
//   look             the same tints, marks and size Appearance draws
#include <cstdint>
#include "blorb/creature.h"
#include "blorb/duel.h"
#include "blorb/stakes.h"

namespace blorb {

BattleCard cardOf(const Creature&, uint64_t lineageId, uint8_t level, uint8_t prowess);

// Graft from candidate B: a pet that is not up for it says so, on BOTH
// screens, as a picture (zzz, an empty bowl, a hug). That is empathy, not an
// error, and it is also the session limit parents want, made of the pet's
// own state rather than a timer.
enum class Decline : uint8_t {
  None,
  NotACreature,   // an egg or a clutch: nothing to duel with
  Asleep,         // zzz
  Sleepy,         // sleepiness >= 0.8: duels make him sleepy, so about five an hour (estimate from the starter genes)
  Hungry,         // hunger >= 0.8: an empty bowl; feed him first (the body's job)
  Resting,        // within 2 minutes of his last duel's end, 5 after a faint
  WantsACuddle,   // three losses in a row: cradle him (3 s) and he is ready again
};
Decline readiness(const Occupant&, uint8_t lossesInARow, uint32_t ticksSinceLastDuel, bool faintedLast);

// The highest tier he may enter now: his stage against StakesRule::youngestStage,
// Spar while his injury is 0.5 or more (a bandage on the rim: wounded pets
// still play, they just do not fight), and the device's own ceiling (TWIST
// stakes). The fairness rules (maxAssistHearts, minManualTurns, the
// head-to-head floor) are checked once both cards are known, in the session.
Stakes ceilingFor(const Creature&, Stakes deviceCeiling);

}  // namespace blorb
