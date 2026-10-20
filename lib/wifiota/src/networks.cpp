#include "wifiota/networks.h"

#include <cstring>

namespace wifiota {

bool Networks::remember(std::string_view ssid, std::string_view password) {
  if (ssid.empty() || ssid.size() > 32 || password.size() > 64) return false;
  if (ssid.find('\0') != std::string_view::npos || password.find('\0') != std::string_view::npos) return false;
  Entry e;
  std::memcpy(e.ssid, ssid.data(), ssid.size());
  std::memcpy(e.password, password.data(), password.size());
  size_t from = count_ < kMax ? count_ : kMax - 1;   // the slot that falls off: the oldest, or the same SSID
  for (size_t i = 0; i < count_; ++i)
    if (ssid == list_[i].ssid) from = i;
  for (size_t i = from; i > 0; --i) list_[i] = list_[i - 1];
  list_[0] = e;
  if (from == count_) ++count_;
  return true;
}

size_t Networks::order(const Heard* heard, size_t n, uint8_t out[kMax]) const {
  int best[kMax];
  for (size_t i = 0; i < count_; ++i) {
    best[i] = -1000;
    for (size_t k = 0; k < n; ++k)
      if (heard[k].ssid == list_[i].ssid && heard[k].rssi > best[i]) best[i] = heard[k].rssi;
  }
  size_t m = 0;
  bool used[kMax] = {};
  for (;;) {   // heard ones, strongest first
    size_t pick = count_;
    for (size_t i = 0; i < count_; ++i)
      if (!used[i] && best[i] > -1000 && (pick == count_ || best[i] > best[pick])) pick = i;
    if (pick == count_) break;
    used[pick] = true;
    out[m++] = uint8_t(pick);
  }
  for (size_t i = 0; i < count_; ++i)
    if (!used[i]) out[m++] = uint8_t(i);
  return m;
}

size_t Networks::encode(uint8_t* out, size_t cap) const {
  if (cap < 1) return 0;
  size_t at = 0;
  out[at++] = kBlobVersion;
  for (size_t i = 0; i < count_; ++i) {
    for (const char* s : {list_[i].ssid, list_[i].password}) {
      const size_t len = std::strlen(s);
      if (at + 1 + len > cap) return 0;
      out[at++] = uint8_t(len);
      std::memcpy(out + at, s, len);
      at += len;
    }
  }
  return at;
}

std::optional<Networks> Networks::decode(const uint8_t* p, size_t n) {
  if (n < 1 || p[0] != kBlobVersion) return std::nullopt;
  Networks out;
  Entry got[kMax];
  size_t count = 0, at = 1;
  while (at < n) {
    if (count == kMax) return std::nullopt;
    std::string_view parts[2];
    for (std::string_view& part : parts) {
      if (at >= n || at + 1 + p[at] > n) return std::nullopt;
      part = std::string_view(reinterpret_cast<const char*>(p + at + 1), p[at]);
      at += 1 + p[at];
    }
    if (parts[0].empty() || parts[0].size() > 32 || parts[1].size() > 64) return std::nullopt;
    std::memcpy(got[count].ssid, parts[0].data(), parts[0].size());
    std::memcpy(got[count].password, parts[1].data(), parts[1].size());
    ++count;
  }
  // Stored newest first; remembered oldest first so the order comes back the same.
  for (size_t i = count; i > 0; --i) out.remember(got[i - 1].ssid, got[i - 1].password);
  if (out.size() != count) return std::nullopt;
  return out;
}

}  // namespace wifiota
