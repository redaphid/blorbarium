// "Tip me on my side!": while a question asks (an update's, Wi-Fi's), a turn
// of the badge says yes, so a child who cannot press the button can still
// answer. The turn is the screen's own settled rotation (orient::Up: a
// quarter turn held 0.6 s on edge, with jolts ignored), so carrying or
// bumping it is no yes. On its side is the least that answers (upside down
// does too, being a turn): the words name exactly that (owner, 2026-10-29:
// "Turn me around!" did not say which turn). As with the button, a turn
// already under way when the question appears (the first kMinMs) is not a yes:
// it only moves where the turn is counted from. The turn is the question's:
// hw::Feel keeps it from the pet (no hang, no fight mode). Pure, so it is
// tested natively (test/test_power, test/test_feeding). docs/ota.md, "Confirming".
#pragma once

#include <cstdint>

namespace hw {

class TurnYes {
 public:
  static constexpr uint32_t kMinMs = 1500;   // Session::kMinAskMs

  // Every sample: true once, on the turn that answers.
  bool sample(bool asking, uint8_t rot, uint32_t now) {
    if (!asking) return open_ = false;
    if (!open_) {
      open_ = true, from_ = rot, since_ = now;
      return false;
    }
    if (rot == from_) return false;
    from_ = rot;
    return now - since_ >= kMinMs;
  }

 private:
  bool open_ = false;
  uint8_t from_ = 0;
  uint32_t since_ = 0;
};

}  // namespace hw
