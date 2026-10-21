// The battery's voltage, the 1.28 board's only power signal (docs/wifi-ota.md
// "Knowing it is plugged in"): the cell through a divider to GPIO1 (Waveshare's
// wiki, BAT_ADC). The divider is taken as 3:1 from Waveshare's demo code;
// `DEBUG wifi` prints the pin's own millivolts beside it so the board can check it.
#pragma once

#include <Arduino.h>

namespace hw {

class Battery {
 public:
  static constexpr uint8_t kPin = 1;
  static constexpr uint32_t kDivider = 3;
  static constexpr uint8_t kReads = 8;

  void begin() {
    analogSetPinAttenuation(kPin, ADC_11db);   // the pin sees up to about 1.4 V of a 4.2 V cell
  }
  // Averaged over a few reads, at the cell. Also leaves the pin's own reading in pinMv().
  uint16_t cellMv() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < kReads; ++i) sum += analogReadMilliVolts(kPin);
    pinMv_ = uint16_t(sum / kReads);
    return uint16_t(pinMv_ * kDivider);
  }
  uint16_t pinMv() const { return pinMv_; }

 private:
  uint16_t pinMv_ = 0;
};

}  // namespace hw
