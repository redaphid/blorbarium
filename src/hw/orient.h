// Which way is up. claude-notification-screen's orientRotation (src/orient.h),
// fed the BodySample the loop already reads rather than an I2C read of its
// own, plus the same quarter turn applied to that sample, so the engine's
// downhill is the one the owner sees. Pure: no Arduino, no clock, no board.
//
// LovyanGFX setRotation(r) puts the image's top at the panel's native top,
// right, bottom, left edge for r = 0..3 (Panel_FrameBufferBase::
// drawPixelPreclipped; the GC9A01 MADCTL table agrees). The sibling picks
// r = round(atan2(ay, ax) / 90 deg) + ORIENT_R0, so with ORIENT_R0 = 1 the
// IMU's x and y are the panel's native right and down, and a sample turns into
// screen coordinates like this (gravity reads +1 g along whichever way is up,
// so upright on screen is ay = -1000 and downhill is +y):
//
//   rot  held with           screen ax  screen ay
//    0   native top up          ax         ay
//    1   native right up        ay        -ax
//    2   native bottom up      -ax        -ay
//    3   native left up        -ay         ax
#pragma once

#include <cmath>
#include <cstdint>

#include "blorb/senses.h"

namespace orient {

constexpr uint32_t kSampleMs = 100;   // ORIENT_SAMPLE_MS: every fifth 50 Hz sample
constexpr uint32_t kSettleMs = 600;   // ORIENT_DWELL_MS: a knock past a boundary does not flip it
constexpr float kFlatG = 0.6f;        // under about 37 degrees from flat the screen holds: tilting to roll the marble must not turn it
constexpr float kJoltG = 0.25f;       // TILT_MAX_SHAKE_G: further off 1 g is a hand, not gravity
constexpr float kSmooth = 0.25f;      // TILT_SMOOTH, per 10 Hz sample: about 1 s to follow a turn

class Up {
 public:
  explicit Up(int r0) : r0_(r0) {}

  uint8_t rotation() const { return rot_; }

  uint8_t sample(const blorb::BodySample& s, uint32_t now) {
    if (now - lastMs_ < kSampleMs) return rot_;
    lastMs_ = now;
    const float ax = s.ax / 1000.0f, ay = s.ay / 1000.0f, az = s.az / 1000.0f;
    if (std::fabs(std::sqrt(ax * ax + ay * ay + az * az) - 1.0f) > kJoltG) return rot_;
    sx_ += (ax - sx_) * kSmooth;
    sy_ += (ay - sy_) * kSmooth;
    if (std::sqrt(sx_ * sx_ + sy_ * sy_) < kFlatG) return rot_;
    const uint8_t q = uint8_t((std::lround(std::atan2(sy_, sx_) * 2.0f / 3.14159265f) + r0_) & 3);
    // The first honest sample is taken as read: at boot there is nothing to flip back to.
    if (!settled_) {
      settled_ = true;
      rot_ = candidate_ = q;
      return rot_;
    }
    if (q != candidate_) {
      candidate_ = q;
      candidateAtMs_ = now;
      return rot_;
    }
    if (q != rot_ && now - candidateAtMs_ >= kSettleMs) rot_ = q;
    return rot_;
  }

  // The table above, for any ORIENT_R0: (rot - r0 + 1) quarter turns.
  blorb::BodySample toScreen(blorb::BodySample s) const {
    const int16_t x = s.ax, y = s.ay;
    switch ((rot_ - r0_ + 1) & 3) {
      case 0: break;
      case 1: s.ax = y, s.ay = int16_t(-x); break;
      case 2: s.ax = int16_t(-x), s.ay = int16_t(-y); break;
      case 3: s.ax = int16_t(-y), s.ay = x; break;
    }
    return s;
  }

 private:
  int r0_;
  uint8_t rot_ = 0, candidate_ = 0;
  bool settled_ = false;
  uint32_t lastMs_ = 0, candidateAtMs_ = 0;
  float sx_ = 0.0f, sy_ = 0.0f;
};

}  // namespace orient
