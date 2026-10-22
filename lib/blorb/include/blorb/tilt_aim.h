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

// An aim counts only once a hand has made it. Found on a board (2026-10-21):
// a puffkin left lying tilted on a desk while its eggs were out kept one
// within 20 minutes with nobody near it, a stray jolt read as the knock on an
// aim that no hand had chosen. Owner: "we may need something in the generic
// tilt selector that makes sure the device was moving before using it to
// select."
//
// So a choice screen (the eggs, a duel) opens unarmed: whatever pose the
// badge already lay in, tilted or on its side, points at nothing. It arms when
// gravity moves (a tilt into a lean, a turn onto a side, picking it up):
// kMoveMg away from where it rested, for kMoveSamples steady samples running,
// so a knock's jolt or a desk's blip is no move. It stays armed kArmedMs
// after the last move, then a badge left lying is unarmed again. While
// unarmed the aim is level for every reader: nothing is shown pointed at,
// no knock or hold selects, and a duel's or a clutch's timeout keeps only
// what an armed aim chose (the clutch: the cursor egg, as before).
// Pure: no clock of its own, so the Dish's tests and replays see it whole.
class AimArming {
 public:
  static constexpr int32_t kMoveMg = 250;       // gravity this far from where it rested: the badge was moved
  static constexpr uint8_t kMoveSamples = 2;    // ...for two steady samples running (AimDetector aims on its second)
  static constexpr int32_t kSteadyMg = 150;     // a sample this close to 1 g is gravity, not a jolt
  static constexpr uint32_t kArmedMs = 20000;   // a move arms the aim this long; lying still past it, unarmed

  // A choice screen appeared: what the badge was already doing does not count.
  void open() { seen_ = false, primed_ = false, run_ = 0; }
  void sample(int32_t ax, int32_t ay, int32_t az, uint32_t ms) {
    const int32_t mag2 = ax * ax + ay * ay + az * az;
    constexpr int32_t lo = (1000 - kSteadyMg) * (1000 - kSteadyMg), hi = (1000 + kSteadyMg) * (1000 + kSteadyMg);
    if (mag2 < lo || mag2 > hi) return run_ = 0, void();   // a jolt: neither a move nor a rest
    if (!primed_) {
      rx_ = ax, ry_ = ay, rz_ = az, primed_ = true;
      return;
    }
    const int32_t dx = ax - rx_, dy = ay - ry_, dz = az - rz_;
    if (dx * dx + dy * dy + dz * dz < kMoveMg * kMoveMg) return run_ = 0, void();
    if (++run_ < kMoveSamples) return;
    rx_ = ax, ry_ = ay, rz_ = az, run_ = 0;   // it moved: rest from here
    seen_ = true;
    movedMs_ = ms;
  }
  bool armed(uint32_t ms) const { return seen_ && ms - movedMs_ < kArmedMs; }

 private:
  int32_t rx_ = 0, ry_ = 0, rz_ = 0;
  uint32_t movedMs_ = 0;
  uint8_t run_ = 0;
  bool primed_ = false, seen_ = false;
};

}  // namespace blorb
