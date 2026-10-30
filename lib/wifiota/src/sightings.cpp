#include "wifiota/sightings.h"

#include <algorithm>

namespace wifiota {

void Sightings::add(const Ap& a) {
  if (n_ < kMax) {
    aps_[n_] = a;
    views_[n_] = Seen{aps_[n_].ssid, a.rssi};
    ++n_;
    return;
  }
  Ap* weakest = std::min_element(aps_, aps_ + n_, [](const Ap& x, const Ap& y) { return x.rssi < y.rssi; });
  if (a.rssi <= weakest->rssi) return;
  *weakest = a;
  const size_t i = size_t(weakest - aps_);
  views_[i] = Seen{aps_[i].ssid, a.rssi};
}

const Ap* Sightings::strongest(std::string_view ssid) const {
  const Ap* best = nullptr;
  for (uint8_t i = 0; i < n_; ++i)
    if (ssid == aps_[i].ssid && (!best || aps_[i].rssi > best->rssi)) best = &aps_[i];
  return best;
}

std::optional<Sightings::Heard> Sightings::heard(std::string_view ssid) const {
  if (joined_ && ssid == joined_->ssid) return Heard{joined_->rssi, joined_->channel, joined_->auth, true};
  if (const Ap* a = strongest(ssid)) return Heard{a->rssi, a->channel, a->auth, false};
  return std::nullopt;
}

}  // namespace wifiota
