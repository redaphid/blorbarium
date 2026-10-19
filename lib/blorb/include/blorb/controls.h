#pragma once
// The controls: which stimuli mean what, in ONE table, so a build can rebind
// them without hunting through the session and the Dish.
//
// The kids' cases cover BOOT, so every word the button says has a twin made
// with the body, as reliable as the hang that opens fight mode: a pose held
// on edge, or the case knocked twice. A twin fires the button's own stimulus,
// so every screen's row in boot.h (kBoot) means the same thing whichever way
// it was said, and the rim's hint and hold ring serve both:
//
//   BOOT says        its twin                          detector
//   press            knock knock (the case, twice)     KnockDetector
//   hold (level)     turned onto a side, held there    SideHoldDetector
//   double press     the hang: upside down, held       HangDetector
//
// So, with no button: knock knock to feed him (or step the stakes, peek in
// the egg, lay a flower, ask for a rematch, throw a quick special); turn him
// onto his side and hold to tuck him in, keep an egg, throw a heavy special,
// and, held the 3 s the ring takes, to agree to Mortal, to Finish or to hug;
// hang him upside down to open fight mode or leave it. Tilt still aims, one
// knock still locks the aimed stance and spares, a shake still tells the
// future, a cuddle still hugs and a turn still answers an update.
//
// Who reads what:
//   kKnockKnockPresses / kSideHoldHolds - the detectors: whether the twins
//                    speak at all (false: the button alone, as on 0.7.11).
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
// A shake always tells the future, and a drop and the lid always reach him
// (they also end the duel): those are never taken, in any phase.
#include <cstdint>

#include "blorb/registry.h"

namespace blorb {

// The twins (above). A knock knock still reaches him as a knock knock too.
inline constexpr bool kKnockKnockPresses = true;
inline constexpr bool kSideHoldHolds = true;

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
