// The poses the gestures read (BodySample::sideDown and invertedDown: the side
// hold and the hang, controls.h), from gravity itself, in the screen's frame
// at home. Pure: no Arduino, no clock, no board.
//
// Until 2026-10-21 the poses were the panel's settled rotation (orient.h):
// a turn had to hold 600 ms after a ~0.4 s smoothing before it counted, a
// dwell made so that a knock past a boundary would not turn the panel. The
// panel no longer turns (2026-10-19), and each pose's detector already times
// its own hold (the hang's 900 ms, the side hold's 1.2 s), so that second
// wait was only lag: on the recordings the hold ring started 0.8-1 s after
// the badge reached its side and the hang's charge as late
// (docs/notes/controls-latency.md). Now a pose counts on its second steady
// sample, and the ring fills from the turn.
//
// A jolt (a knock, a bump: the magnitude off 1 g by kJoltMg) says nothing
// about the pose and is skipped, so a knock never flips one. Into a pose at
// 45 degrees (kInMg), out of it under kOutMg, so a hand wobbling on the line
// does not flicker it.
#pragma once

#include <cstdint>
#include <cstdlib>

#include "blorb/senses.h"

namespace hw {

class Poses {
 public:
  enum class Pose : uint8_t { Home, Side, Hang };
  static constexpr int32_t kInMg = 700;    // about 45 degrees: into a pose
  static constexpr int32_t kOutMg = 600;   // a pose held lets go under this
  static constexpr int32_t kJoltMg = 250;  // orient.h's TILT_MAX_SHAKE_G: further off 1 g is a hand, not gravity
  // Steady samples in a row that agree: a pose, 20 ms after the first. One
  // flickered the ring on the recordings' handling (12 rings, not 5: 0.4 s
  // passes on edge that hold nothing); three cost 20 ms more for nothing.
  static constexpr uint8_t kSteadyN = 2;

  // One sample in the screen's frame at home (orient::Up::toFrame).
  void sample(const blorb::BodySample& h) {
    const int32_t m2 = int32_t(h.ax) * h.ax + int32_t(h.ay) * h.ay + int32_t(h.az) * h.az;
    constexpr int32_t lo = 1000 - kJoltMg, hi = 1000 + kJoltMg;
    if (m2 < lo * lo || m2 > hi * hi) return;
    const Pose p = classify(h);
    if (p == pose_) return void(n_ = 0);
    if (p != cand_) cand_ = p, n_ = 0;
    if (++n_ >= kSteadyN) pose_ = p, n_ = 0;
  }

  // One sample's pose as the hand holds it, before any filter: -1 a jolt
  // (says nothing), 0 none, 1 on a side, 2 upside down (`DEBUG latency`'s In).
  static int8_t raw(const blorb::BodySample& h) {
    const int32_t m2 = int32_t(h.ax) * h.ax + int32_t(h.ay) * h.ay + int32_t(h.az) * h.az;
    constexpr int32_t lo = 1000 - kJoltMg, hi = 1000 + kJoltMg;
    if (m2 < lo * lo || m2 > hi * hi) return -1;
    if (std::abs(h.az) > 500) return 0;   // lying, not on edge (the detectors' kHangFaceMg)
    if (std::abs(h.ax) >= kInMg) return 1;
    return h.ay >= kInMg ? 2 : 0;
  }

  Pose pose() const { return pose_; }
  bool sideways() const { return pose_ == Pose::Side; }
  bool inverted() const { return pose_ == Pose::Hang; }

 private:
  Pose classify(const blorb::BodySample& h) const {
    const int32_t x = std::abs(h.ax), up = h.ay;   // upright at home reads ay = -1 g: a hang, +1 g
    if (pose_ == Pose::Side && x >= kOutMg) return Pose::Side;
    if (pose_ == Pose::Hang && up >= kOutMg) return Pose::Hang;
    if (x >= kInMg) return Pose::Side;
    if (up >= kInMg) return Pose::Hang;
    return Pose::Home;
  }

  Pose pose_ = Pose::Home, cand_ = Pose::Home;
  uint8_t n_ = 0;
};

}  // namespace hw
