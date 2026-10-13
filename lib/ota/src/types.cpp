#include "ota/types.h"

namespace ota {
namespace {

int b64urlValue(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

}  // namespace

bool unb64url(std::string_view s, uint8_t* out, size_t n) {
  if (s.size() != (n * 8 + 5) / 6) return false;
  uint32_t acc = 0;
  int bits = 0;
  size_t k = 0;
  for (char c : s) {
    const int v = b64urlValue(c);
    if (v < 0) return false;
    acc = acc << 6 | uint32_t(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (k < n) out[k++] = uint8_t(acc >> bits);
    }
  }
  return k == n && (acc & ((1u << bits) - 1)) == 0;   // leftover bits are zero in a canonical encoding
}

void b64url(const uint8_t* p, size_t n, char* out) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < n; ++i) {
    acc = acc << 8 | p[i];
    bits += 8;
    while (bits >= 6) *out++ = kAlphabet[(acc >> (bits -= 6)) & 63];
  }
  if (bits) *out++ = kAlphabet[(acc << (6 - bits)) & 63];
  *out = 0;
}

std::optional<Version> parseVersion(std::string_view s) {
  uint32_t part[3] = {};
  size_t p = 0;
  bool digit = false;
  for (char c : s) {
    if (c == '.') {
      if (!digit || ++p > 2) return std::nullopt;
      digit = false;
    } else if (c >= '0' && c <= '9') {
      part[p] = part[p] * 10 + uint32_t(c - '0');
      if (part[p] > 0xffff) return std::nullopt;
      digit = true;
    } else {
      return std::nullopt;
    }
  }
  if (p != 2 || !digit) return std::nullopt;
  return Version{uint16_t(part[0]), uint16_t(part[1]), uint16_t(part[2])};
}

std::optional<Token> parseToken(std::string_view s) {
  Token t;
  if (!unb64url(s, t.b, sizeof t.b)) return std::nullopt;
  return t;
}

}  // namespace ota
