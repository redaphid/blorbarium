#pragma once
// The Wi-Fi networks a badge remembers (docs/wifi-ota.md "Credentials"): up
// to kMaxNetworks, newest first: home, and perhaps a grandparent's or a
// phone's hotspot. A phone sends one over Improv, or the cable sets one
// (`DEBUG wifi set`). The same SSID again replaces its password; a fourth
// pushes out the oldest. A Check scans once and joins the strongest of them
// in range (pick()). Stored as one blob in the default `nvs` partition under
// namespace "wifi" (src/hw/wifi_store.h), never in the pet's partitions.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "wifiota/improv.h"

namespace wifiota {

inline constexpr uint8_t kRememberedVersion = 1;   // one network (the first branch's blob, still read)
inline constexpr size_t kRememberedMax = 1 + 1 + 32 + 1 + 64;

// Nothing for an SSID of 0 or over 32 bytes, a password over 64, or a NUL in either.
std::optional<improv::Credentials> credentials(std::string_view ssid, std::string_view password);

// One network's stored form: a version byte, then the SSID and the password,
// each length-prefixed. 0 when `cap` is too small.
size_t encodeRemembered(const improv::Credentials&, uint8_t* out, size_t cap);
std::optional<improv::Credentials> decodeRemembered(const uint8_t* p, size_t n);

inline constexpr size_t kMaxNetworks = 3;
inline constexpr uint8_t kNetworksVersion = 2;
inline constexpr size_t kNetworksMax = 2 + kMaxNetworks * (1 + 32 + 1 + 64);

// A network a scan saw.
struct Seen {
  std::string_view ssid;
  int rssi;
};

class Networks {
 public:
  size_t size() const { return n_; }
  bool empty() const { return n_ == 0; }
  const improv::Credentials& operator[](size_t i) const { return nets_[i]; }

  // To the front; the same SSID's old entry goes, and past kMaxNetworks the oldest.
  void remember(const improv::Credentials&);
  // False when no network has that SSID.
  bool forget(std::string_view ssid);
  void clear() { n_ = 0; }
  std::optional<size_t> find(std::string_view ssid) const;

  // Which remembered network to join, from what a scan saw: the strongest
  // in range, the newer on a tie. Nothing when none is in range.
  std::optional<size_t> pick(const Seen* seen, size_t count) const;

  // Version 2, a count, then each network as one entry of encodeRemembered
  // without its version byte. 0 when `cap` is too small.
  size_t encode(uint8_t* out, size_t cap) const;
  // Version 2, or a version 1 blob (one network). Nothing for junk.
  static std::optional<Networks> decode(const uint8_t* p, size_t n);

 private:
  improv::Credentials nets_[kMaxNetworks];
  size_t n_ = 0;
};

}  // namespace wifiota
