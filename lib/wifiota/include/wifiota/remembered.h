#pragma once
// The one Wi-Fi network a badge remembers (docs/wifi-ota.md "Credentials"):
// the last a phone sent over Improv or the cable set (`DEBUG wifi set`). A new
// one replaces it. Stored as one blob in the default `nvs` partition under
// namespace "wifi" (src/hw/wifi_store.h), never in the pet's partitions.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "wifiota/improv.h"

namespace wifiota {

inline constexpr uint8_t kRememberedVersion = 1;
inline constexpr size_t kRememberedMax = 1 + 1 + 32 + 1 + 64;

// Nothing for an SSID of 0 or over 32 bytes, a password over 64, or a NUL in either.
std::optional<improv::Credentials> credentials(std::string_view ssid, std::string_view password);

// The stored form: a version byte, then the SSID and the password, each
// length-prefixed. 0 when `cap` is too small.
size_t encodeRemembered(const improv::Credentials&, uint8_t* out, size_t cap);
std::optional<improv::Credentials> decodeRemembered(const uint8_t* p, size_t n);

}  // namespace wifiota
