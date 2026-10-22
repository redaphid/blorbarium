#pragma once
// Food (#57), every number in one place. The owner, playing with a badge: "I
// keep running out of food and I don't know how to get more but it keeps
// complaining about being starving." The pantry held 4 and one came back
// every 2 hours, while a pet asks for a pellet about every 3 to 4 hours awake
// and wakes from a night starving: a kid who fed him whenever he asked, and
// a few more times because feeding is the fun part, ran dry and then had
// nothing to give for hours. test_food_balance measures it (FOOD_SWEEP=1).
//
// Where food comes from:
//   the pantry   refills on the pet clock, one pellet every few minutes; a
//                knock knock (or BOOT) drops one into the dish (habitat.h)
//   foraging     at the end of a dig (and now and then a sniff) he may turn
//                up a snack of his own, which lands at his feet (actions.cpp)
//   coming home  the first hand on the badge after a long time untouched, or
//                after it was off, tops the pantry up (dish.cpp)
//   spoils       the hook for a fight's win and a friend's gift (#46, #33):
//                Habitat::give and Habitat::take, nothing calls them yet
//
// The metabolism gene (genes.h MetabolismGene) still gives each line its own
// pantry and pace: its bytes are read through the scales below, so a pet
// already in the field gets the new balance without a gene edit, and a
// mutated byte still makes a bigger or smaller pantry, a faster or slower one.
// Starvation is untouched: it is the chemistry's (starter_genome.cpp), and
// only comes of hours with nobody feeding him.
#include <cstdint>
#include "blorb/fixed.h"
#include "blorb/ids.h"

namespace blorb::food {

// ---- the pantry, from the metabolism gene's bytes ---------------------------
// pantrySize: slots per unit of the byte. The starter's 4 is 8 slots.
inline constexpr uint8_t kPantryPerByte = 2;
// The most it can hold whatever the gene says: the rim's slots (paint).
inline constexpr uint8_t kMaxPantry = 12;
// refill: one pellet back every <byte> of these. The starter's 8 is 5 min 20 s,
// so an empty pantry is full again in 43 minutes: faster than a kid who
// knocks for pellets for the fun of it at every other glance (about one each
// 8 minutes, test_food_balance) can empty it.
inline constexpr uint32_t kRefillTicksPerByte = 40 * 1000 / kTickMs;   // 40 s
// Before #57 the byte was quarter hours (8 = 2 h). The viability dry run
// (mutate.cpp) still feeds at that pace on purpose: it asks whether a genome
// survives a slow keeper, and a mutation that passed before passes now.
inline constexpr uint32_t kDryRunRefillTicksPerByte = kTicksPerHour / 4;

// ---- foraging ---------------------------------------------------------------
// The end of a dig turns up a snack one time in this many: a pellet at his
// feet, fresh, that he eats when he wants it. A pet left to play digs about a
// hundred times a day (cooldown 3 min), so this is two or three snacks a day:
// a treat, never his keep (at 1 in 2 he fed himself and never asked). He only
// plays an idle when nothing presses (creature.cpp kPlayHunger), so a
// starving, neglected pet never forages his way out of it.
inline constexpr uint8_t kDigFindsOneIn = 40;

// ---- coming home -----------------------------------------------------------
// Untouched this long (powered, or the board off), the first hand on the badge
// tops the pantry up by kHomecoming, up to its size.
inline constexpr uint32_t kAwayTicks = 2 * kTicksPerHour;
inline constexpr uint8_t kHomecoming = 2;

// ---- the countdown ------------------------------------------------------------
// Hungry enough to ask (the feed hint's level, appearance.cpp): with the
// pantry empty and nothing in the dish he would eat, the rim says when the
// next pellet comes (feed_show.h, waiting).
inline constexpr Fx kAsksForFood = Fx::ratio(1, 2);

// ---- spoils (#46, #33): the hook --------------------------------------------
// What a win takes from the loser's pantry for the winner's, at most.
inline constexpr uint8_t kSpoils = 1;

}  // namespace blorb::food
