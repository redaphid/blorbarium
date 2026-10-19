#pragma once
// The duel's controls: which stimuli mean what, in ONE table, so a build can
// rebind them without hunting through the session and the Dish. The current
// case covers the BOOT button, so the hang (held upside down, steady) is the
// live fight toggle; the button bindings sit beside it in the same masks,
// ready for a case that exposes the button again. Swapping a control is an
// edit here and nowhere else.
//
// Who reads what:
//   kFightAskMask  - DishDuel: the toggle that asks for fight mode. In the
//                    lobby and on the friends screen the same gestures mean
//                    leave / search again, read there as DuelInput's
//                    doublePress and hang.
//   kLobbyCaptures - DuelSession, searching: all the lobby takes. A plain
//                    press is NOT here: it still drops a pellet in the lobby.
//   kDuelCaptures / kHoldPhaseCaptures - DuelSession, in a battle: the
//                    stimuli the duel takes for itself, so his body does not
//                    flinch at every lock-in knock. BOOT is the duel's in
//                    every phase (the special, the confirm, the Finish, the
//                    hug), so a heavy special never also tucks him in.
// A shake always reaches him, and a drop and the lid always reach him: those
// are never taken, in any phase. In the lobby a shake also leaves fight mode;
// in a battle it tells the future.
#include <cstdint>

#include "blorb/registry.h"

namespace blorb {

constexpr uint64_t duelStimBit(StimId s) { return uint64_t(1) << s.v; }

// The fight toggle: a BOOT double press, or the hang — the same ask from a
// case that covers the button.
inline constexpr uint64_t kFightAskMask = duelStimBit(stim::button_double) | duelStimBit(stim::hang);

inline constexpr uint64_t kLobbyCaptures = duelStimBit(stim::button_double) | duelStimBit(stim::button_hold) |
                                           duelStimBit(stim::hang);

inline constexpr uint64_t kDuelCaptures = duelStimBit(stim::knock) | duelStimBit(stim::double_knock) |
                                          duelStimBit(stim::button) | duelStimBit(stim::button_double) |
                                          duelStimBit(stim::hang);
inline constexpr uint64_t kHoldPhaseCaptures = kDuelCaptures | duelStimBit(stim::button_hold);

static_assert((kDuelCaptures & kFightAskMask) == kFightAskMask, "the ask is the duel's own once it opens");
static_assert((kHoldPhaseCaptures & kLobbyCaptures) == kLobbyCaptures, "the lobby's set is a hold-phase subset");
static_assert((kHoldPhaseCaptures & (duelStimBit(stim::shake) | duelStimBit(stim::dropped) |
                                     duelStimBit(stim::lid_down) | duelStimBit(stim::cradle))) == 0,
              "BOOT is the duel's; a shake, a drop, the lid and a cuddle are always the pet's");

}  // namespace blorb
