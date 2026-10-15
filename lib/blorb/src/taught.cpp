#include "blorb/taught.h"

#include <algorithm>
#include <cstring>
#include <vector>
#include "blorb/fixed.h"

namespace blorb {
namespace {

// "BLTL", a version byte, the count, each line as its length then its
// characters, and a crc32 of everything before it (little-endian).
constexpr uint8_t kMagic[4] = {'B', 'L', 'T', 'L'};
constexpr uint8_t kVersion = 1;
constexpr size_t kHeadLen = sizeof kMagic + 2, kCrcLen = 4;
constexpr size_t kMaxFileLen = kHeadLen + Taught::kMax * (1 + kMaxTaughtText) + kCrcLen;

bool sayable(const char* s, size_t n) {
  if (n == 0 || n > kMaxTaughtText) return false;
  for (size_t i = 0; i < n; ++i)
    if (!inFont(s[i])) return false;
  return true;
}

}  // namespace

Taught Taught::load(Storage& s) {
  const size_t size = s.size(kFile);
  if (size < kHeadLen + kCrcLen || size > kMaxFileLen) return {};
  uint8_t b[kMaxFileLen];
  const std::optional<size_t> n = s.read(kFile, 0, b, size);
  if (!n || *n != size || std::memcmp(b, kMagic, sizeof kMagic) != 0 || b[4] != kVersion || b[5] > kMax) return {};
  uint32_t crc = 0;
  for (size_t i = 0; i < kCrcLen; ++i) crc |= uint32_t(b[size - kCrcLen + i]) << (8 * i);
  if (crc != crc32(b, size - kCrcLen)) return {};
  Taught t;
  size_t at = kHeadLen;
  for (uint8_t i = 0; i < b[5]; ++i) {
    const size_t len = at < size - kCrcLen ? b[at] : 0;
    const char* text = reinterpret_cast<const char*>(b + at + 1);
    if (at + 1 + len > size - kCrcLen || !sayable(text, len)) return {};
    std::memcpy(t.at[i], text, len);
    t.count = uint8_t(i + 1);
    at += 1 + len;
  }
  return at == size - kCrcLen ? t : Taught{};
}

bool Taught::save(Storage& s) const {
  std::vector<uint8_t> b(kMagic, kMagic + sizeof kMagic);
  b.push_back(kVersion);
  b.push_back(count);
  for (uint8_t i = 0; i < count; ++i) {
    const size_t len = std::strlen(at[i]);
    b.push_back(uint8_t(len));
    b.insert(b.end(), at[i], at[i] + len);
  }
  const uint32_t crc = crc32(b.data(), b.size());
  for (size_t i = 0; i < kCrcLen; ++i) b.push_back(uint8_t(crc >> (8 * i)));
  return s.writeAtomic(kFile, b.data(), b.size());
}

Taught Taught::with(std::string_view line) const {
  Taught t;
  for (uint8_t i = 0; i < count; ++i)
    if (line != this->line(i)) std::memcpy(t.at[t.count++], at[i], sizeof at[i]);
  if (t.count == kMax) {   // full: the oldest goes
    std::memmove(t.at[0], t.at[1], sizeof t.at[0] * (kMax - 1));
    --t.count;
  }
  std::memset(t.at[t.count], 0, sizeof t.at[0]);
  std::memcpy(t.at[t.count++], line.data(), std::min(line.size(), kMaxTaughtText));
  return t;
}

}  // namespace blorb
