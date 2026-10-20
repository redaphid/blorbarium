#pragma once
// The Wi-Fi networks a badge remembers (docs/wifi-ota.md "Credentials"): a
// few, the newest first, as one blob in the default `nvs` partition under
// namespace "wifi" (src/hw/wifi_store.h), never in the pet's partitions.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace wifiota {

class Networks {
 public:
  static constexpr size_t kMax = 3;                    // a house, a grandparent's, one more
  static constexpr size_t kBlobMax = 1 + kMax * (1 + 32 + 1 + 64);
  static constexpr uint8_t kBlobVersion = 1;

  struct Entry {
    char ssid[33] = "";
    char password[65] = "";
  };

  size_t size() const { return count_; }
  const Entry& operator[](size_t i) const { return list_[i]; }

  // The newest first: the same SSID again replaces its password, and past
  // kMax the oldest goes. False for an SSID of 0 or over 32 bytes, or a password over 64.
  bool remember(std::string_view ssid, std::string_view password);
  void forgetAll() { count_ = 0; }

  // The order to try them in, given what a scan heard: the remembered
  // networks it heard, strongest first, then the rest (a hidden network is
  // never heard) in remembered order. Returns how many indices it wrote.
  struct Heard {
    std::string_view ssid;
    int8_t rssi;
  };
  size_t order(const Heard* heard, size_t n, uint8_t out[kMax]) const;

  // The stored form: a version byte, then each entry as length-prefixed SSID and password.
  size_t encode(uint8_t* out, size_t cap) const;
  static std::optional<Networks> decode(const uint8_t* p, size_t n);

 private:
  Entry list_[kMax];
  size_t count_ = 0;
};

}  // namespace wifiota
