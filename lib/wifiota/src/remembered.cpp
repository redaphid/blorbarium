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

namespace {

// An entry: SSID length and bytes, password length and bytes. 0 when it does not fit.
size_t encodeEntry(const improv::Credentials& c, uint8_t* out, size_t cap) {
  const size_t ssid = std::strlen(c.ssid), pass = std::strlen(c.password);
  const size_t n = 1 + ssid + 1 + pass;
  if (n > cap) return 0;
  out[0] = uint8_t(ssid);
  std::memcpy(out + 1, c.ssid, ssid);
  out[1 + ssid] = uint8_t(pass);
  std::memcpy(out + 2 + ssid, c.password, pass);
  return n;
}

// One entry at p (n bytes left): the credentials and how many bytes it took.
std::optional<improv::Credentials> decodeEntry(const uint8_t* p, size_t n, size_t& used) {
  if (n < 2) return std::nullopt;
  const size_t ssid = p[0];
  if (n < 1 + ssid + 1) return std::nullopt;
  const size_t pass = p[1 + ssid];
  if (n < 2 + ssid + pass) return std::nullopt;
  used = 2 + ssid + pass;
  return credentials(std::string_view(reinterpret_cast<const char*>(p + 1), ssid),
                     std::string_view(reinterpret_cast<const char*>(p + 2 + ssid), pass));
}

}  // namespace

size_t encodeRemembered(const improv::Credentials& c, uint8_t* out, size_t cap) {
  if (cap < 1) return 0;
  const size_t n = encodeEntry(c, out + 1, cap - 1);
  if (!n) return 0;
  out[0] = kRememberedVersion;
  return 1 + n;
}

std::optional<improv::Credentials> decodeRemembered(const uint8_t* p, size_t n) {
  if (n < 3 || p[0] != kRememberedVersion) return std::nullopt;
  size_t used = 0;
  const std::optional<improv::Credentials> c = decodeEntry(p + 1, n - 1, used);
  if (!c || 1 + used != n) return std::nullopt;
  return c;
}

void Networks::remember(const improv::Credentials& c) {
  forget(c.ssid);
  if (n_ == kMaxNetworks) --n_;   // the oldest goes
  for (size_t i = n_; i > 0; --i) nets_[i] = nets_[i - 1];
  nets_[0] = c;
  ++n_;
  untried_ = uint8_t((untried_ << 1) & ((1u << n_) - 1u));   // the new one is tried until said otherwise
}

void Networks::setUntried(size_t i, bool on) {
  if (i >= n_) return;
  untried_ = on ? uint8_t(untried_ | 1u << i) : uint8_t(untried_ & ~(1u << i));
}

std::optional<size_t> Networks::next(const Seen* seen, size_t count) const {
  for (size_t i = 0; i < n_; ++i)
    if (untried(i)) return i;
  return pick(seen, count);
}

bool Networks::forgetAt(size_t at) {
  if (at >= n_) return false;
  for (size_t i = at; i + 1 < n_; ++i) nets_[i] = nets_[i + 1];
  --n_;
  const uint8_t below = uint8_t(untried_ & ((1u << at) - 1u));
  untried_ = uint8_t(below | ((untried_ >> (at + 1)) << at));
  return true;
}

std::optional<size_t> Networks::find(std::string_view ssid) const {
  for (size_t i = 0; i < n_; ++i)
    if (ssid == nets_[i].ssid) return i;
  return std::nullopt;
}

bool Networks::forget(std::string_view ssid) {
  const std::optional<size_t> at = find(ssid);
  return at && forgetAt(*at);
}

std::optional<size_t> Networks::pick(const Seen* seen, size_t count) const {
  std::optional<size_t> best;
  int bestRssi = 0;
  for (size_t s = 0; s < count; ++s) {
    const std::optional<size_t> at = find(seen[s].ssid);
    if (!at) continue;
    if (!best || seen[s].rssi > bestRssi || (seen[s].rssi == bestRssi && *at < *best)) best = at, bestRssi = seen[s].rssi;
  }
  return best;
}

size_t Networks::encode(uint8_t* out, size_t cap) const {
  if (cap < 2) return 0;
  out[0] = kNetworksVersion;
  out[1] = uint8_t(n_);
  size_t at = 2;
  for (size_t i = 0; i < n_; ++i) {
    const size_t n = encodeEntry(nets_[i], out + at, cap - at);
    if (!n) return 0;
    at += n;
  }
  return at;
}

std::optional<Networks> Networks::decode(const uint8_t* p, size_t n) {
  Networks out;
  if (n >= 1 && p[0] == kRememberedVersion) {
    const std::optional<improv::Credentials> c = decodeRemembered(p, n);
    if (!c) return std::nullopt;
    out.remember(*c);
    return out;
  }
  if (n < 2 || p[0] != kNetworksVersion || p[1] > kMaxNetworks) return std::nullopt;
  size_t at = 2;
  for (uint8_t i = 0; i < p[1]; ++i) {
    size_t used = 0;
    const std::optional<improv::Credentials> c = decodeEntry(p + at, n - at, used);
    if (!c) return std::nullopt;
    if (out.find(c->ssid)) return std::nullopt;   // never written twice
    out.nets_[out.n_++] = *c;
    at += used;
  }
  if (at != n) return std::nullopt;
  return out;
}

}  // namespace wifiota
