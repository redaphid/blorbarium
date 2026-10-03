#pragma once
// A body-only caretaker: no phone, only the gestures a friend with the toy
// makes. It looks at him (his drives stand in for his face and the care
// hint) and, one gesture at a time: presses BOOT for a pellet when he is
// hungry and the dish is empty, cradles him when he wants touch, lays him
// face down at dusk, warms the egg in a hand, and at the clutch moves the
// cursor once and holds BOOT to pick.
#include <cstdint>
#include <variant>
#include "bench.h"
#include "traces.h"

namespace blorbtest {

class ScriptedOwner {
 public:
  struct Counts { uint32_t presses = 0, cradles = 0, tucks = 0, picks = 0, cursorMoves = 0; };

  blorb::BodySample next(uint32_t ms, const Bench& bench) {
    if (ms >= until_) choose(ms, bench);
    switch (gesture_) {
      case Gesture::Press:
      case Gesture::Hold: return pressed();
      case Gesture::Cradle: return cradled();
      case Gesture::Lid: return faceDown();
      default: return still();
    }
  }
  const Counts& counts() const { return counts_; }

 private:
  enum class Gesture : uint8_t { Rest, Press, Hold, Cradle, Lid };
  static constexpr uint32_t kLookEveryMs = 30000;
  static constexpr uint32_t kPressMs = 200, kHoldMs = 1500, kCradleMs = 4000, kTuckMs = 10 * 60000;

  void act(Gesture g, uint32_t ms, uint32_t forMs) {
    gesture_ = g;
    until_ = ms + forMs;
  }

  void choose(uint32_t ms, const Bench& bench) {
    using namespace blorb;
    // Every gesture ends at rest for a moment, so the detectors see a release.
    if (gesture_ != Gesture::Rest) return act(Gesture::Rest, ms, 2000);
    if (const auto* c = std::get_if<Creature>(&bench.occupant())) {
      const Chemistry& chem = c->chemistry();
      bool night = bench.clock().night();
      if (night && !wasNight_ && !c->body().asleep) {
        wasNight_ = night;
        ++counts_.tucks;
        return act(Gesture::Lid, ms, kTuckMs);
      }
      wasNight_ = night;
      if (chem.drive(drive::hunger) >= Fx::ratio(25, 100) && !bench.habitat().nearestPellet(DishPos{}) &&
          bench.habitat().pantry > 0) {
        ++counts_.presses;
        return act(Gesture::Press, ms, kPressMs);
      }
      if (chem.drive(drive::need_touch) >= Fx::ratio(5, 10)) {
        ++counts_.cradles;
        return act(Gesture::Cradle, ms, kCradleMs);
      }
    } else if (std::holds_alternative<Egg>(bench.occupant())) {
      ++counts_.cradles;
      return act(Gesture::Cradle, ms, kCradleMs);
    } else {
      const Clutch& k = std::get<Clutch>(bench.occupant());
      if (k.sinceDeath >= Clutch::kVigilTicks) {
        if (k.count > 1 && k.cursor == 0) {
          ++counts_.cursorMoves;
          return act(Gesture::Press, ms, kPressMs);
        }
        ++counts_.picks;
        return act(Gesture::Hold, ms, kHoldMs);
      }
    }
    act(Gesture::Rest, ms, kLookEveryMs);
  }

  Gesture gesture_ = Gesture::Rest;
  uint32_t until_ = 0;
  bool wasNight_ = false;
  Counts counts_;
};

}  // namespace blorbtest
