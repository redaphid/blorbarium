#pragma once
// The small values an update passes between its parts: versions, the page's
// token, the connection a write came from, and their text forms.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

namespace ota {

enum class Confirm : uint8_t { NotOnTrial, Confirmed, NotYours };   // what OTA CONFIRM found

// One Bluetooth connection, numbered so a reconnect is a different peer.
using Peer = uint32_t;

struct Version {
  uint16_t major = 0, minor = 0, patch = 0;
  friend bool operator<(Version a, Version b) {
    if (a.major != b.major) return a.major < b.major;
    if (a.minor != b.minor) return a.minor < b.minor;
    return a.patch < b.patch;
  }
  friend bool operator==(Version a, Version b) { return !(a < b) && !(b < a); }
};
std::optional<Version> parseVersion(std::string_view);

// 16 random bytes the page makes for one update and keeps: the session, its
// resume, ABORT, REBOOT and the new image's CONFIRM all need it.
struct Token {
  uint8_t b[16] = {};
  bool operator==(const Token& o) const { return std::memcmp(b, o.b, sizeof b) == 0; }
  bool operator!=(const Token& o) const { return !(*this == o); }
};
std::optional<Token> parseToken(std::string_view);   // 22 characters of unpadded base64url

// Unpadded base64url of exactly `n` bytes, or false.
bool unb64url(std::string_view, uint8_t* out, size_t n);
void b64url(const uint8_t* p, size_t n, char* out);   // out holds (n * 8 + 5) / 6 + 1

}  // namespace ota
