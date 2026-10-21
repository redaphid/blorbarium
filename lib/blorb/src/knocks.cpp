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

// The magnitude's variance over the history, less the newest kLead samples.
int64_t KnockSense::spread2() const {
  constexpr uint8_t kN = kHistory - kLead;
  int64_t sum = 0, sq = 0;
  for (uint8_t i = 0; i < kN; ++i) {
    const Past& p = hist_[(histAt_ + i) % kHistory];
    sum += p.mag, sq += int64_t(p.mag) * p.mag;
  }
  return (int64_t(kN) * sq - sum * sum) / (int64_t(kN) * kN);   // exact: a truncated mean squared is off by 2 x mean x its fraction
}

// Steady before the knock: the magnitude's spread, and the jerk of the
// quietest three samples in four.
bool KnockSense::steadyBefore() {
  constexpr uint8_t kN = kHistory - kLead;
  int32_t jerks[kN];
  for (uint8_t i = 0; i < kN; ++i) jerks[i] = hist_[(histAt_ + i) % kHistory].jerk;
  std::sort(jerks, jerks + kN);
  return spread2() < int64_t(kSteadySdMg) * kSteadySdMg && jerks[kN * 3 / 4] < kSteadyJerkMg;
}

// A blip is over: does it start, carry on, or spoil a burst on the table?
void KnockSense::endBlip() {
  inBlip_ = false;
  const Blip b = blip_;
  const bool quietBefore = !hadBlip_ || b.start - lastBlip_.end >= kRestQuietMs;
  hadBlip_ = true, lastBlip_ = b;
  if (restN_ == 0) {
    if (b.steady && quietBefore && b.n <= kBlipMaxSamples && b.max >= kRestFirstMg) restN_ = 1, restLast_ = b.start;
    return;
  }
  if (b.n > kBlipMaxSamples) restN_ = 0;   // a rattle: the desk, not a knuckle
  else if (b.max >= kRestNextMg && b.start - restLast_ >= kFastMs) {
    if (++restN_ == 2) restGap_ = b.start - restLast_;
    restLast_ = b.start;
  }
}

uint8_t KnockSense::restSample(int32_t peakJerk, uint32_t ms) {
  uint8_t out = 0;
  if (inBlip_ && peakJerk < kBlipMg && ms - blip_.end > 40) endBlip();
  if (restN_ && !inBlip_ && ms - restLast_ > (restN_ >= 2 ? moreMs(restGap_) : kChainMs)) {
    out = restN_ <= kMaxKnocks ? (restN_ >= 2 ? 2 : 1) : 0;
    restN_ = 0;
  }
  if (peakJerk >= kBlipMg) {
    if (!inBlip_) {
      inBlip_ = true;
      blip_ = Blip{ms, ms, peakJerk, 1, histN_ == kHistory && spread2() < int64_t(kRestSdMg) * kRestSdMg};
    } else {
      blip_.end = ms, blip_.max = std::max(blip_.max, peakJerk), ++blip_.n;
    }
  }
  return out;
}

uint8_t KnockSense::decide() {
  inBurst_ = false;
  if (!steady_ || knocks_ > kMaxKnocks || sumN_ == 0) return 0;
  const int32_t dx = sumX_ / sumN_ - g0x_, dy = sumY_ / sumN_ - g0y_, dz = sumZ_ / sumN_ - g0z_;
  if (int64_t(dx) * dx + int64_t(dy) * dy + int64_t(dz) * dz >= int64_t(kDriftMg) * kDriftMg) return 0;
  return knocks_ >= 2 ? 2 : 1;
}

uint8_t KnockSense::sample(const BodySample& s, uint32_t ms) {
  // With the shell's 250 Hz peak, the knock is judged on the peak (the read
  // farthest from the last sample); the steadiness history stays on samples.
  const int32_t kx = s.hasPeak ? s.peakAx : s.ax, ky = s.hasPeak ? s.peakAy : s.ay, kz = s.hasPeak ? s.peakAz : s.az;
  const int32_t jerk = primed_ ? iabs32(kx - px_) + iabs32(ky - py_) + iabs32(kz - pz_) : 0;
  uint8_t out = 0;
  if (inBurst_ && ms - last_ > (knocks_ >= 2 ? moreMs(gap_) : kChainMs)) out = decide();
  int32_t gx = g0x_, gy = g0y_, gz = g0z_;
  const bool known = inBurst_ || histN_ == kHistory;
  if (histN_ == kHistory && !inBurst_) gravityBefore(gx, gy, gz);
  int32_t dev = 0;
  if (known) {
    const int32_t dx = kx - gx, dy = ky - gy, dz = kz - gz;
    dev = isqrt32(uint32_t(std::min<int64_t>(int64_t(dx) * dx + int64_t(dy) * dy + int64_t(dz) * dz, 0x7fffffff)));
  }
  const bool spike = known && jerk >= kJerkMg && dev >= (inBurst_ ? kNextDevMg : kDevMg);
  if (spike) {
    const bool calm = dev1_ < kCalmMg && dev2_ < kCalmMg;
    const bool fast = ms - onset_ >= kFastMs && calm && dev >= kFastDevMg;
    if (inBurst_ && ms - onset_ <= kMergeMs && !fast) {
      last_ = ms;   // its bounce
    } else if (inBurst_) {
      if (++knocks_ == 2) gap_ = ms - onset_;
      last_ = onset_ = ms;
    } else {
      inBurst_ = true, knocks_ = 1, last_ = onset_ = ms;
      g0x_ = gx, g0y_ = gy, g0z_ = gz;
      steady_ = steadyBefore();
    }
    sumX_ = sumY_ = sumZ_ = sumN_ = 0;
  } else if (inBurst_ && ms - last_ >= kSettleMs) {
    sumX_ += s.ax, sumY_ += s.ay, sumZ_ += s.az, ++sumN_;
  }
  if (!inBurst_ || !steady_ || knocks_ < 2 || knocks_ > kMaxKnocks) heard_ = false;
  else if (!heard_ && ms - last_ >= kHeardQuietMs && dev < kNextDevMg) heard_ = true;
  dev2_ = dev1_, dev1_ = dev;
  // On a table (rest path): only with the peak; a burst the hand path already
  // has is the hand path's, and whichever decides first speaks once.
  if (s.hasPeak) {
    const uint8_t rest = restSample(jerk, ms);
    if (inBurst_ && steady_) restN_ = 0;
    if (!out && rest) out = rest;
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
