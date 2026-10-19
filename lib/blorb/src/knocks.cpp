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

// Steady over the whole history: the magnitude's spread, and the jerk of the
// quietest three samples in four. Also takes gravity before, from the oldest
// three quarters of it (the newest may already lean into the knock).
bool KnockSense::steadyBefore() {
  if (histN_ < kHistory) return false;
  int64_t sum = 0, sq = 0;
  int32_t jerks[kHistory];
  g0x_ = g0y_ = g0z_ = 0;
  constexpr uint8_t kOld = kHistory * 3 / 4;
  for (uint8_t i = 0; i < kHistory; ++i) {
    const Past& p = hist_[(histAt_ + i) % kHistory];   // oldest first
    sum += p.mag, sq += int64_t(p.mag) * p.mag;
    jerks[i] = p.jerk;
    if (i < kOld) g0x_ += p.ax, g0y_ += p.ay, g0z_ += p.az;
  }
  g0x_ /= kOld, g0y_ /= kOld, g0z_ /= kOld;
  const int64_t mean = sum / kHistory, var = sq / kHistory - mean * mean;
  std::sort(jerks, jerks + kHistory);
  return var < int64_t(kSteadySdMg) * kSteadySdMg && jerks[kHistory * 3 / 4] < kSteadyJerkMg;
}

uint8_t KnockSense::decide() {
  inBurst_ = false;
  if (!steady_ || knocks_ > 3 || sumN_ == 0) return 0;
  const int32_t dx = sumX_ / sumN_ - g0x_, dy = sumY_ / sumN_ - g0y_, dz = sumZ_ / sumN_ - g0z_;
  if (int64_t(dx) * dx + int64_t(dy) * dy + int64_t(dz) * dz >= int64_t(kDriftMg) * kDriftMg) return 0;
  return knocks_ >= 2 ? 2 : 1;
}

uint8_t KnockSense::sample(const BodySample& s, uint32_t ms) {
  const int32_t jz = primed_ ? iabs32(s.az - pz_) : 0;
  const int32_t jerk = primed_ ? iabs32(s.ax - px_) + iabs32(s.ay - py_) + jz : 0;
  uint8_t out = 0;
  if (inBurst_ && ms - last_ > kChainMs) out = decide();
  if (primed_ && jz >= kSpikeMg) {
    if (inBurst_ && ms - last_ <= kMergeMs) {
      last_ = ms;
    } else if (inBurst_) {
      ++knocks_, last_ = ms;
    } else {
      inBurst_ = true, knocks_ = 1, last_ = ms;
      steady_ = steadyBefore();
    }
    sumX_ = sumY_ = sumZ_ = sumN_ = 0;
  } else if (inBurst_ && ms - last_ >= kSettleMs) {
    sumX_ += s.ax, sumY_ += s.ay, sumZ_ += s.az, ++sumN_;
  }
  const int32_t mag = isqrt32(uint32_t(int32_t(s.ax) * s.ax + int32_t(s.ay) * s.ay + int32_t(s.az) * s.az));
  hist_[histAt_] = Past{s.ax, s.ay, s.az, mag, jerk};
  histAt_ = uint8_t((histAt_ + 1) % kHistory);
  if (histN_ < kHistory) ++histN_;
  px_ = s.ax, py_ = s.ay, pz_ = s.az;
  primed_ = true;
  return out;
}

}  // namespace blorb
