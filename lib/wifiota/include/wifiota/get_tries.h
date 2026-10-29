// The manifest's GET in a window, once the badge has joined: one that got
// nothing back (a lookup, a connect or a handshake that failed: "connection
// refused" in 3 of 8 first tries on 2026-10-29, every later one fine) is
// tried again after kWaitMs, up to kTries in all, before the window gives
// up. One that got bytes back is the server's answer: not tried again.
// Pure, so it is tested natively (test/test_wifi_ota).
#pragma once

#include <cstdint>

namespace wifiota {

struct GetTries {
  static constexpr uint8_t kTries = 3;
  static constexpr uint32_t kWaitMs = 1500;

  void reset() { done_ = 0; }
  // A try ended without its answer: true to wait kWaitMs and try again.
  bool again(bool gotBytes) {
    ++done_;
    return !gotBytes && done_ < kTries;
  }
  uint8_t done() const { return done_; }

 private:
  uint8_t done_ = 0;
};

}  // namespace wifiota
