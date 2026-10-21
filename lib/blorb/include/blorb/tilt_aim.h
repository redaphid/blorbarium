#pragma once
// The tilt picker: one rule for every screen where a kid picks by tilting
// the badge toward what he wants, the way a marble would roll. The duel's
// stances and stakes (DuelInput::from) and the clutch's eggs (pickEgg) both
// aim with it, so a hand that learned one has learned the other.
//
// The aim is a rim sector: tilt left (0), right (1), toward the kid (2, the
// near edge, where a badge held upright already leans), away (3); -1 level.
// AimDetector (senses.h) reads it from gravity with hysteresis, as of each
// sample; the Dish hands it on as tilt values at the sector's middle (the
// knock's own aim when a knock locks, the aim from before a turn onto a
// side), and this reads the sector back from them: tiltX and tiltY are 0.5
// level, tilt_x above it rolls things left, tilt_y below it toward the kid.
#include <cstdint>
#include "blorb/fixed.h"

namespace blorb {

inline constexpr Fx kAimDeadZone = Fx::ratio(15, 100);

inline int8_t tiltAim(Fx tiltX, Fx tiltY) {
  const Fx dx = tiltX - Fx::ratio(1, 2), dy = tiltY - Fx::ratio(1, 2);
  const Fx ax = dx < Fx::zero() ? -dx : dx, ay = dy < Fx::zero() ? -dy : dy;
  if (ax < kAimDeadZone && ay < kAimDeadZone) return -1;
  return ax >= ay ? (dx > Fx::zero() ? 0 : 1) : (dy < Fx::zero() ? 2 : 3);
}

}  // namespace blorb
