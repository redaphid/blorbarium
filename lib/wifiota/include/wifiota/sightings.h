#pragma once
// What the radio heard of the networks around it, for the join and for
// `DEBUG wifi` (src/hw/wifi_station.h keeps one, in PSRAM): the last scan's
// access points, the strongest kMax of them, and the one the last join
// reached. Kept until the next scan, not until the radio goes down: `DEBUG
// wifi` is asked after the window has closed, and 0.7.35 said "not in the
// last scan" of a network it had just seen and joined, because the radio's
// going cleared the list. A provision joins without a scan, so the access
// point it joined is kept as heard too: its signal, channel and security.
// Pure, so test/test_wifi_ota drives it.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "wifiota/remembered.h"

namespace wifiota {

struct Ap {
  char ssid[33] = "";
  int rssi = -127;
  uint8_t channel = 0;
  uint8_t auth = 0;   // the driver's wifi_auth_mode_t
  uint8_t bssid[6] = {};
};

class Sightings {
 public:
  static constexpr uint8_t kMax = 24;

  Sightings() = default;
  Sightings(const Sightings&) = delete;   // seen() points into it
  Sightings& operator=(const Sightings&) = delete;

  // A scan begins: what the last one saw goes, and the last join with it (the scan is newer).
  void beginScan() { n_ = 0, joined_.reset(); }
  // One access point the scan found. Past kMax, the weakest goes.
  void add(const Ap&);
  // The access point a join reached, scan or none.
  void joined(const Ap& a) { joined_ = a; }

  // The scan's strongest access point of that SSID (the join goes straight
  // to its channel and BSSID). Null when the scan did not see it.
  const Ap* strongest(std::string_view ssid) const;

  struct Heard {
    int rssi;
    uint8_t channel;
    uint8_t auth;
    bool joined;   // from the join since the last scan, not the scan
  };
  // The access point joined since the last scan when it is that SSID's,
  // else the scan's strongest. Nothing: neither saw it.
  std::optional<Heard> heard(std::string_view ssid) const;

  const Seen* seen() const { return views_; }
  uint8_t count() const { return n_; }

 private:
  Ap aps_[kMax];
  Seen views_[kMax] = {};
  uint8_t n_ = 0;
  std::optional<Ap> joined_;
};

}  // namespace wifiota
