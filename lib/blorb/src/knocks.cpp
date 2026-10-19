#include "blorb/knocks.h"

#include <algorithm>

namespace blorb {
namespace {

int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }

int32_t isqrt32(uint32_t v) {
  uint32_t r = 0, bit = uint32_t(1) << 30;
  while (bit > v) bit >>= 2;
  for (; bit; bit >>= 2) {
    if (v >= r + bit) v -= r + bit, r = (r >> 1) + bit;
    else r >>= 1;
  }
  return int32_t(r);
}

}  // namespace

// The history oldest first, less the newest kLead samples.
void KnockSense::gravityBefore(int32_t& x, int32_t& y, int32_t& z) const {
  x = y = z = 0;
  constexpr uint8_t kN = kHistory - kLead;
  for (uint8_t i = 0; i < kN; ++i) {
    const Past& p = hist_[(histAt_ + i) % kHistory];
    x += p.ax, y += p.ay, z += p.az;
  }
  x /= kN, y /= kN, z /= kN;
}

// Steady before the knock: the magnitude's spread, and the jerk of the
// quietest three samples in four.
bool KnockSense::steadyBefore() {
  constexpr uint8_t kN = kHistory - kLead;
  int64_t sum = 0, sq = 0;
  int32_t jerks[kN];
  for (uint8_t i = 0; i < kN; ++i) {
    const Past& p = hist_[(histAt_ + i) % kHistory];
    sum += p.mag, sq += int64_t(p.mag) * p.mag;
    jerks[i] = p.jerk;
  }
  const int64_t mean = sum / kN, var = sq / kN - mean * mean;
  std::sort(jerks, jerks + kN);
  return var < int64_t(kSteadySdMg) * kSteadySdMg && jerks[kN * 3 / 4] < kSteadyJerkMg;
}

uint8_t KnockSense::decide() {
  inBurst_ = false;
  if (!steady_ || knocks_ > kMaxKnocks || sumN_ == 0) return 0;
  const int32_t dx = sumX_ / sumN_ - g0x_, dy = sumY_ / sumN_ - g0y_, dz = sumZ_ / sumN_ - g0z_;
  if (int64_t(dx) * dx + int64_t(dy) * dy + int64_t(dz) * dz >= int64_t(kDriftMg) * kDriftMg) return 0;
  return knocks_ >= 2 ? 2 : 1;
}

uint8_t KnockSense::sample(const BodySample& s, uint32_t ms) {
  const int32_t jerk = primed_ ? iabs32(s.ax - px_) + iabs32(s.ay - py_) + iabs32(s.az - pz_) : 0;
  uint8_t out = 0;
  if (inBurst_ && ms - last_ > kChainMs) out = decide();
  int32_t gx = g0x_, gy = g0y_, gz = g0z_;
  const bool known = inBurst_ || histN_ == kHistory;
  if (histN_ == kHistory && !inBurst_) gravityBefore(gx, gy, gz);
  int32_t dev = 0;
  if (known) {
    const int32_t dx = s.ax - gx, dy = s.ay - gy, dz = s.az - gz;
    dev = isqrt32(uint32_t(std::min<int64_t>(int64_t(dx) * dx + int64_t(dy) * dy + int64_t(dz) * dz, 0x7fffffff)));
  }
  const bool spike = known && jerk >= kJerkMg && dev >= (inBurst_ ? kNextDevMg : kDevMg);
  if (spike) {
    const bool calm = dev1_ < kCalmMg && dev2_ < kCalmMg;
    const bool fast = ms - onset_ >= kFastMs && calm && dev >= kFastDevMg;
    if (inBurst_ && ms - onset_ <= kMergeMs && !fast) {
      last_ = ms;   // its bounce
    } else if (inBurst_) {
      ++knocks_, last_ = onset_ = ms;
    } else {
      inBurst_ = true, knocks_ = 1, last_ = onset_ = ms;
      g0x_ = gx, g0y_ = gy, g0z_ = gz;
      steady_ = steadyBefore();
    }
    sumX_ = sumY_ = sumZ_ = sumN_ = 0;
  } else if (inBurst_ && ms - last_ >= kSettleMs) {
    sumX_ += s.ax, sumY_ += s.ay, sumZ_ += s.az, ++sumN_;
  }
  dev2_ = dev1_, dev1_ = dev;
  const int32_t mag = isqrt32(uint32_t(int32_t(s.ax) * s.ax + int32_t(s.ay) * s.ay + int32_t(s.az) * s.az));
  hist_[histAt_] = Past{s.ax, s.ay, s.az, mag, jerk};
  histAt_ = uint8_t((histAt_ + 1) % kHistory);
  if (histN_ < kHistory) ++histN_;
  px_ = s.ax, py_ = s.ay, pz_ = s.az;
  primed_ = true;
  return out;
}

}  // namespace blorb
