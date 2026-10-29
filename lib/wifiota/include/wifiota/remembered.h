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
  // By place, 0 the newest. False when there is none there.
  bool forgetAt(size_t i);
  void clear() { n_ = 0, untried_ = 0; }
  std::optional<size_t> find(std::string_view ssid) const;

  // Which remembered network to join, from what a scan saw: the strongest
  // in range, the newer on a tie. Nothing when none is in range.
  std::optional<size_t> pick(const Seen* seen, size_t count) const;

  // Untried: a network an Improv provision could not join (a weak signal
  // while the phone was connected, 2026-10-29) is remembered all the same,
  // and the next Check, dark and with no phone, tries it before any other,
  // in range or not (it may be hidden). Kept beside the list as a mask, bit
  // i for network i (WifiStore's "untried"), never in the list's blob: an
  // older image rolled back to still reads the list.
  bool untried(size_t i) const { return i < n_ && (untried_ >> i & 1u); }
  void setUntried(size_t i, bool on);
  uint8_t untriedMask() const { return untried_; }
  void setUntriedMask(uint8_t m) { untried_ = uint8_t(m & ((1u << n_) - 1u)); }
  // Which network a Check joins: the newest untried one, else pick().
  std::optional<size_t> next(const Seen* seen, size_t count) const;

  // Version 2, a count, then each network as one entry of encodeRemembered
  // without its version byte. 0 when `cap` is too small.
  size_t encode(uint8_t* out, size_t cap) const;
  // Version 2, or a version 1 blob (one network). Nothing for junk.
  static std::optional<Networks> decode(const uint8_t* p, size_t n);

 private:
  improv::Credentials nets_[kMaxNetworks];
  size_t n_ = 0;
  uint8_t untried_ = 0;
};

}  // namespace wifiota
