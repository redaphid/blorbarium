#include "wifiota/schedule.h"

namespace wifiota {

const char* windowName(Window w) {
  constexpr const char* kNames[] = {"none", "check", "forced", "confirm", "provision"};
  return kNames[uint8_t(w)];
}

const char* holdName(Hold h) {
  constexpr const char* kNames[] = {"open", "disabled", "on_battery", "not_idle", "someone_there", "radio_busy", "not_due"};
  return kNames[uint8_t(h)];
}

}  // namespace wifiota
