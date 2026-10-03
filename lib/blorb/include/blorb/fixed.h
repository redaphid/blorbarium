#pragma once
// Integer-only arithmetic for the whole engine.
//
// Invariant: no float or double appears anywhere under lib/blorb. A run
// (genome + seed + input script) replays bit-identically on x86 (host tests),
// on the Xtensa LX7 (device) and in the website, so a state hash recorded on
// the host is valid on the board.
//
// One scalar type, Fx: Q8.24, saturating. 24 fraction bits, not 16, because a
// drive that climbs 0 -> 1 over 6 hours at 10 Hz moves 4.6e-6 per tick. That
// is below one LSB of a 16-fraction-bit format (1.5e-5) and 77 LSB here, so
// slow genetic rates never silently round to zero.
#include <cstddef>
#include <cstdint>

namespace blorb {

struct Fx {
  int32_t raw = 0;
  static constexpr int kFrac = 24;
  static constexpr int32_t kOne = int32_t(1) << kFrac;

  static constexpr Fx sat(int64_t v) {
    return Fx{v > INT32_MAX ? INT32_MAX : (v < INT32_MIN ? INT32_MIN : int32_t(v))};
  }
  static constexpr Fx zero() { return Fx{0}; }
  static constexpr Fx one() { return Fx{kOne}; }
  static constexpr Fx ratio(int32_t num, int32_t den) { return sat((int64_t(num) << kFrac) / den); }
  // Gene decoders use these two, so every byte value is legal.
  static constexpr Fx unitByte(uint8_t b) { return Fx{int32_t((int64_t(b) * kOne) / 255)}; }       // 0..255 -> 0..1
  static constexpr Fx signedByte(uint8_t b) { return Fx{int32_t((int64_t(b) - 128) * (kOne / 128))}; }  // 128 = 0

  constexpr Fx operator+(Fx o) const { return sat(int64_t(raw) + o.raw); }
  constexpr Fx operator-(Fx o) const { return sat(int64_t(raw) - o.raw); }
  constexpr Fx operator-() const { return sat(-int64_t(raw)); }
  constexpr Fx operator*(Fx o) const { return sat((int64_t(raw) * o.raw) >> kFrac); }
  constexpr Fx& operator+=(Fx o) { return *this = *this + o; }
  constexpr Fx& operator-=(Fx o) { return *this = *this - o; }
  friend constexpr bool operator==(Fx a, Fx b) { return a.raw == b.raw; }
  friend constexpr bool operator!=(Fx a, Fx b) { return a.raw != b.raw; }
  friend constexpr bool operator<(Fx a, Fx b) { return a.raw < b.raw; }
  friend constexpr bool operator<=(Fx a, Fx b) { return a.raw <= b.raw; }
  friend constexpr bool operator>(Fx a, Fx b) { return a.raw > b.raw; }
  friend constexpr bool operator>=(Fx a, Fx b) { return a.raw >= b.raw; }
};

constexpr Fx fxMin(Fx a, Fx b) { return a < b ? a : b; }
constexpr Fx fxMax(Fx a, Fx b) { return a > b ? a : b; }
constexpr Fx clamp01(Fx v) { return fxMax(Fx::zero(), fxMin(v, Fx::one())); }

// Compact storage for the one large table (the brain's weights). Arithmetic
// always happens in Fx; this is only how W sits in RAM and in the keepsake.
struct Q15 { int16_t v = 0; };
constexpr Q15 toQ15(Fx x) {
  return Q15{int16_t(x.raw >= (int32_t(1) << 24) ? 32767 : (x.raw <= -(int32_t(1) << 24) ? -32768 : x.raw >> 9))};
}
constexpr Fx fromQ15(Q15 q) { return Fx{int32_t(q.v) * 512}; }

// Exponential decay from a one-byte half-life gene value (log scale,
// about 1 s at byte 1 up to weeks at byte 254; 0 = never decays).
// `keep` is the survival fraction per application as Q0.32, applied only
// every 2^shift ticks, so slow chemicals (Life over weeks) keep precision and
// cost nothing most ticks (the C1/C2 tick-mask trick).
struct Decay { uint32_t keep = 0xFFFFFFFFu; uint8_t shift = 31; };
Decay decayFromByte(uint8_t halfLifeByte);
Fx applyDecay(Fx c, Decay d, uint32_t tick);
Fx applyDecayTicks(Fx c, Decay d, uint32_t nTicks);   // closed form (repeated squaring)

// xoshiro128**, so host and device agree. State is saved with the creature.
struct Rng {
  uint32_t s[4];
  static Rng seeded(uint64_t seed);
  uint32_t next();
  uint32_t below(uint32_t n);
  Fx unit();            // [0, 1)
  bool chance(Fx p);
};

uint32_t fnv1a(const void* data, size_t len, uint32_t seed = 0x811c9dc5u);
uint32_t crc32(const void* data, size_t len);

}  // namespace blorb
