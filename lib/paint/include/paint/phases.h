#pragma once
// Where a frame's drawing time goes, phase by phase, for the board's `DEBUG
// fps` and the paint bench. Off (one test of a null pointer a phase) until the
// caller hands it a clock; nothing drawn depends on it.
#include <cstdint>

namespace paint {

enum class Phase : uint8_t {
  Background,   // the dish, or the duel's arena
  Figure,       // him, his egg, the clutch or the remains: items, shadow, sprites, halo (the duel: both pets and props)
  Rim,          // pantry pips, duel tokens, the care hint
  Text,         // a thought, fight mode's line, the no-fight strip, the amber's words
  Rings,        // fight mode's searching ring, the hang's ring
  Mask,         // the round mask
  Hint,         // the BOOT hint on the rim
  Count
};

struct PhaseClock {
  uint32_t (*now)() = nullptr;   // micros, or a cycle counter; null: off
  uint32_t at = 0;
  uint32_t spent[uint8_t(Phase::Count)] = {};
};
extern PhaseClock phaseClock;

// The start of a run of phases: the time from here to the first phaseEnd is the first phase's.
inline void phaseStart() {
  if (phaseClock.now) phaseClock.at = phaseClock.now();
}
// The time since the last mark goes to `p`.
inline void phaseEnd(Phase p) {
  if (!phaseClock.now) return;
  const uint32_t t = phaseClock.now();
  phaseClock.spent[uint8_t(p)] += t - phaseClock.at;
  phaseClock.at = t;
}

}  // namespace paint
