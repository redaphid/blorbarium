#pragma once
// Whether the badge is on external power, inferred from the battery voltage
// (docs/wifi-ota.md "Knowing it is plugged in"). The 1.28 board brings out
// no VBUS or charge-status pin: its one power signal is the battery through a
// divider on GPIO1. So:
//
//   - at or above kChargerMv: on power. Only a charger holds a cell there
//     while the badge draws its few tens of mA;
//   - a rise of kStepMv between two readings: on power (a charger's current
//     starting lifts the terminal voltage at once);
//   - a fall of kStepMv between two readings: on battery (that current stopping);
//   - below kChargerMv and falling for kFallMs, kFallMv or more under the
//     highest reading since it was judged on power: on battery. This is a
//     badge unplugged after its charger had already finished, which steps down
//     by too little to see;
//   - under kNoBatteryMv: no reading at all (an unwired pin): on battery,
//     which keeps Wi-Fi off. Unknown errs to battery.
//
// Readings come every kEveryMs, each an average the caller takes. Pure: the
// numbers are tested in test/test_wifi_ota and wait on the board to be checked.
#include <cstdint>

namespace wifiota {

class PowerSense {
 public:
  static constexpr uint32_t kEveryMs = 10000;
  static constexpr uint16_t kChargerMv = 4170;
  static constexpr uint16_t kStepMv = 40;
  static constexpr uint16_t kFallMv = 30;
  static constexpr uint32_t kFallMs = 10 * 60 * 1000;
  static constexpr uint16_t kNoBatteryMv = 2500;

  enum class Why : uint8_t { Start, Charger, Rise, Fall, Drained, NoReading };

  bool onPower() const { return on_; }
  Why why() const { return why_; }
  uint16_t lastMv() const { return last_; }

  // One averaged reading of the battery, in millivolts at the cell.
  void reading(uint16_t mv, uint32_t now) {
    const bool first = !seen_;
    const uint16_t before = last_;
    seen_ = true;
    last_ = mv;
    if (mv < kNoBatteryMv) return set(false, Why::NoReading, mv, now);
    if (mv >= kChargerMv) return set(true, Why::Charger, mv, now);
    if (!first && mv >= before + kStepMv) return set(true, Why::Rise, mv, now);
    if (!first && mv + kStepMv <= before) return set(false, Why::Fall, mv, now);
    if (first) return set(false, Why::Start, mv, now);
    if (!on_) return;
    // On power and below the charger's voltage: charging, or unplugged after it finished.
    if (mv > peak_) peak_ = mv, peakAt_ = now;
    if (mv + kFallMv <= peak_ && now - peakAt_ >= kFallMs) set(false, Why::Drained, mv, now);
  }

 private:
  void set(bool on, Why why, uint16_t mv, uint32_t now) {
    on_ = on;
    why_ = why;
    peak_ = mv;
    peakAt_ = now;
  }

  bool seen_ = false, on_ = false;
  Why why_ = Why::Start;
  uint16_t last_ = 0, peak_ = 0;
  uint32_t peakAt_ = 0;
};

inline const char* powerWhyName(PowerSense::Why w) {
  constexpr const char* kNames[] = {"start", "charger", "rise", "fall", "drained", "no_reading"};
  return kNames[uint8_t(w)];
}

}  // namespace wifiota
