// "Tip me on my side!": while a question asks (an update's, Wi-Fi's), tipping
// the badge onto either side says yes, so a child who cannot press the button
// can still answer. Exactly that: the screen's own settled rotation
// (orient::Up: a quarter turn held 0.6 s on edge, jolts ignored) coming to
// rest a quarter turn from home, left or right. Not upside down: that is the
// hang, which opens fight mode, and the words must name the one gesture the
// badge checks (owner, 2026-10-29: "Turn me around!" did not say which, and
// the upside-down turn kids found opened fight mode). A side tip already
// under way when the question appears (the first kMinMs) is not a yes, nor is
// a badge already on that side: it only moves where the turn is counted from.
// The tip is the question's: hw::Feel keeps it from the pet (no side hold).
// Pure, so it is tested natively (test/test_power, test/test_feeding).
// docs/ota.md, "Confirming".
#pragma once

#include <cstdint>

namespace hw {

class TurnYes {
 public:
  static constexpr uint32_t kMinMs = 1500;   // Session::kMinAskMs

  // Whether the settled rotation `rot` is on a side, from `home`.
  static bool onSide(uint8_t rot, uint8_t home) { return ((rot - home) & 1) != 0; }

  // Every sample: true once, on the tip onto a side that answers.
  bool sample(bool asking, uint8_t rot, uint8_t home, uint32_t now) {
    if (!asking) return open_ = false;
    if (!open_) {
      open_ = true, from_ = rot, since_ = now;
      return false;
    }
    if (rot == from_) return false;
    from_ = rot;
    return onSide(rot, home) && now - since_ >= kMinMs;
  }

 private:
  bool open_ = false;
  uint8_t from_ = 0;
  uint32_t since_ = 0;
};

}  // namespace hw
