// The ESP32's system time as the Dish's RTC. It keeps counting through a soft
// reset (a crash, an OTA reboot) but not a power cut, so after a cut it is
// unset until the firmware sets it from the Dish's own wall time.
#pragma once

#include <sys/time.h>

#include <ctime>
#include <optional>

#include "blorb/seams.h"

namespace hw {

class SystemClock : public blorb::TimeSource {
 public:
  static constexpr time_t kPlausible = 1704067200;   // 2024-01-01: anything earlier was never set

  static bool isSet() { return time(nullptr) >= kPlausible; }
  static void set(uint32_t unixSeconds) {
    const timeval tv{time_t(unixSeconds), 0};
    settimeofday(&tv, nullptr);
  }
  std::optional<uint32_t> unixSeconds() override {
    if (!isSet()) return std::nullopt;
    return uint32_t(time(nullptr));
  }
};

}  // namespace hw
