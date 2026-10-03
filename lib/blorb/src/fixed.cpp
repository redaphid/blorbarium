#include "blorb/fixed.h"

namespace blorb {
namespace {

// 2^(k/12) in Q16: the half-life scale doubles every 12 byte steps.
constexpr uint32_t kSemitoneQ16[12] = {65536, 69433, 73562, 77936, 82570, 87480,
                                       92682, 98193, 104032, 110218, 116772, 123715};
constexpr uint64_t kLn2Q32 = 2977044472ull;   // ln 2 * 2^32
constexpr uint32_t kNever = 0xFFFFFFFFu;

uint64_t halfLifeTicksQ16(uint8_t b) {
  uint32_t k = uint32_t(b) - 1;
  return uint64_t(10) * kSemitoneQ16[k % 12] << (k / 12);
}

// exp(-x) for x in Q32.32 and 0 <= x < 1, as Q0.32, by its Taylor series.
uint32_t expNegQ32(uint64_t x) {
  uint64_t term = uint64_t(1) << 32, sum = term;
  for (uint32_t n = 1; n < 16 && term != 0; ++n) {
    term = ((term * x) >> 32) / n;   // term <= 2^32 and x < 2^32, so the product fits
    sum = (n & 1) ? sum - term : sum + term;
  }
  return sum >= (uint64_t(1) << 32) ? kNever - 1 : uint32_t(sum);
}

Fx scale(Fx c, uint32_t keep) {
  return Fx{int32_t((int64_t(c.raw) * keep) >> 32)};
}

uint32_t mulQ32(uint32_t a, uint32_t b) { return uint32_t((uint64_t(a) * b) >> 32); }

uint64_t splitmix64(uint64_t& x) {
  uint64_t z = (x += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

constexpr uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

}  // namespace

// Each application removes about 1/64 of the level or more, so the truncation
// of one multiply stays far below the decay it applies, however slow the gene.
Decay decayFromByte(uint8_t halfLifeByte) {
  if (halfLifeByte == 0) return Decay{};
  uint64_t hq16 = halfLifeTicksQ16(halfLifeByte);
  uint8_t shift = 0;
  while ((uint64_t(64) << (shift + 1 + 16)) <= hq16) ++shift;
  uint64_t x = (kLn2Q32 << 16) / (hq16 >> shift);
  return Decay{expNegQ32(x), shift};
}

Fx applyDecay(Fx c, Decay d, uint32_t tick) {
  if (d.keep == kNever) return c;
  if ((tick & ((uint32_t(1) << d.shift) - 1)) != 0) return c;
  return scale(c, d.keep);
}

Fx applyDecayTicks(Fx c, Decay d, uint32_t nTicks) {
  if (d.keep == kNever) return c;
  uint32_t k = nTicks >> d.shift;
  uint32_t result = kNever, base = d.keep;
  bool any = false;
  for (; k != 0; k >>= 1, base = mulQ32(base, base)) {
    if (k & 1) { result = any ? mulQ32(result, base) : base; any = true; }
  }
  return any ? scale(c, result) : c;
}

Rng Rng::seeded(uint64_t seed) {
  Rng r{};
  for (uint32_t& w : r.s) w = uint32_t(splitmix64(seed) >> 32);
  if ((r.s[0] | r.s[1] | r.s[2] | r.s[3]) == 0) r.s[0] = 1;
  return r;
}

uint32_t Rng::next() {
  uint32_t result = rotl(s[1] * 5, 7) * 9;
  uint32_t t = s[1] << 9;
  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];
  s[2] ^= t;
  s[3] = rotl(s[3], 11);
  return result;
}

uint32_t Rng::below(uint32_t n) { return n == 0 ? 0 : uint32_t((uint64_t(next()) * n) >> 32); }

Fx Rng::unit() { return Fx{int32_t(next() >> (32 - Fx::kFrac))}; }

bool Rng::chance(Fx p) { return unit() < p; }

uint32_t fnv1a(const void* data, size_t len, uint32_t seed) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t h = seed;
  for (size_t i = 0; i < len; ++i) h = (h ^ p[i]) * 0x01000193u;
  return h;
}

uint32_t crc32(const void* data, size_t len, uint32_t prev) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t c = ~prev;
  for (size_t i = 0; i < len; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

}  // namespace blorb
