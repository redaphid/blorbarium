// Arduino's WiFiGeneric retries a failed join by itself once per boot: after
// the first disconnect that is not the station leaving on purpose (its static
// first_connect), never again. A join with a failure reason waits for that
// retry only when it came inside this join; any later join has failed at its
// first disconnect (0.7.41's soak waited out the 15 s budget on a NO_AP_FOUND
// that came at 3.5 s). Pure, so it is tested natively (test/test_wifi_ota);
// src/hw/wifi_station.h keeps the counts, on the core's event task.
#pragma once

#include <cstdint>

namespace wifiota::coreretry {

inline constexpr uint8_t kLeave = 8;   // WIFI_REASON_ASSOC_LEAVE: ours, on purpose

// After a disconnect: `disconnects` counts it already; `retryAt` (0: not yet)
// is the disconnect the core retried after, set here at the first one.
inline uint32_t retryAt(uint32_t retryAt, uint32_t disconnects, uint8_t reason) {
  return retryAt || reason == kLeave ? retryAt : disconnects;
}

// A join that began when `atJoin` disconnects had been counted, whose last
// disconnect gave a failure reason: has it failed, or is the core's retry
// still to come?
inline bool failed(uint32_t disconnects, uint32_t atJoin, uint32_t retryAt) {
  if (disconnects == atJoin) return false;
  const bool coreRetriedInThisJoin = retryAt > atJoin;
  return !(coreRetriedInThisJoin && disconnects - atJoin < 2);
}

}  // namespace wifiota::coreretry
