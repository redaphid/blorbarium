// "Turn me around!": while an update asks, a turn of the badge says yes, so a
// child who cannot press the button can still take an update. The turn is the
// screen's own settled rotation (orient::Up: a quarter turn held 0.4 s, with
// jolts ignored), so carrying or bumping it is no yes. As with the button, a
// turn already under way when the question appears (the first kMinMs) is not
// a yes: it only moves where the turn is counted from. Pure, so it is tested
// natively (test/test_power). docs/ota.md, "Confirming".
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
