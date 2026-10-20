#include "wifiota/remembered.h"

#include <cstring>

namespace wifiota {

std::optional<improv::Credentials> credentials(std::string_view ssid, std::string_view password) {
  if (ssid.empty() || ssid.size() > 32 || password.size() > 64) return std::nullopt;
  if (ssid.find('\0') != std::string_view::npos || password.find('\0') != std::string_view::npos) return std::nullopt;
  improv::Credentials c;
  std::memcpy(c.ssid, ssid.data(), ssid.size());
  std::memcpy(c.password, password.data(), password.size());
  return c;
}

size_t encodeRemembered(const improv::Credentials& c, uint8_t* out, size_t cap) {
  const size_t ssid = std::strlen(c.ssid), pass = std::strlen(c.password);
  const size_t n = 1 + 1 + ssid + 1 + pass;
  if (n > cap) return 0;
  out[0] = kRememberedVersion;
  out[1] = uint8_t(ssid);
  std::memcpy(out + 2, c.ssid, ssid);
  out[2 + ssid] = uint8_t(pass);
  std::memcpy(out + 3 + ssid, c.password, pass);
  return n;
}

std::optional<improv::Credentials> decodeRemembered(const uint8_t* p, size_t n) {
  if (n < 3 || p[0] != kRememberedVersion) return std::nullopt;
  const size_t ssid = p[1];
  if (n < 2 + ssid + 1) return std::nullopt;
  const size_t pass = p[2 + ssid];
  if (n != 3 + ssid + pass) return std::nullopt;
  return credentials(std::string_view(reinterpret_cast<const char*>(p + 2), ssid),
                     std::string_view(reinterpret_cast<const char*>(p + 3 + ssid), pass));
}

}  // namespace wifiota
