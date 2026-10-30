#pragma once
// A badge carried about: feeding_badge.h's whole body path (the 250 Hz reads
// and their peak, the shell, the Dish ticked once a frame), played with a
// motion from src/hw/carry_ride.h or a recording, not a knuckle. Used by
// test/test_carry and test/test_carry_film.
#include <functional>
#include <vector>
#include "feeding_badge.h"
#include "../../src/hw/carry_ride.h"

namespace carrytest {

using namespace blorb;
using feedingtest::Badge;

// forMs of reads from `raw(t)` (t: ms since the call; the IMU's frame), as Badge::run reads its hand.
inline void play(Badge& b, const std::function<BodySample(uint32_t)>& raw, uint32_t forMs,
                 const std::function<void()>& eachFrame = {}) {
  const uint32_t t0 = b.ms;
  for (const uint32_t end = b.ms + forMs; b.ms < end;) {
    const bool fast = !b.policy.low();
    b.ms += fast ? 4 : kSampleMs;
    BodySample s = raw(b.ms - t0);
    const int64_t dx = s.ax - b.handed.ax, dy = s.ay - b.handed.ay, dz = s.az - b.handed.az;
    if (dx * dx + dy * dy + dz * dz > b.best) b.best = dx * dx + dy * dy + dz * dz, b.pk[0] = s.ax, b.pk[1] = s.ay, b.pk[2] = s.az;
    if (fast && ++b.sub < kSampleMs / 4) continue;
    b.sub = 0;
    s.hasPeak = b.primed;
    s.peakAx = b.pk[0], s.peakAy = b.pk[1], s.peakAz = b.pk[2];
    b.ring.push_back({s, b.ms});
    b.handed = s, b.primed = true, b.best = -1;
    if (int32_t(b.ms - b.nextFrame) >= 0) {
      b.frame(), b.nextFrame = b.ms + b.frameMs;
      if (eachFrame) eachFrame();
    }
  }
}

// One ride (carry_ride.h) for its own length, or forMs.
inline void ride(Badge& b, hw::ride::Kind k, uint32_t forMs = 0, const std::function<void()>& eachFrame = {}) {
  uint32_t len = forMs;
  for (const hw::ride::Info& r : hw::ride::kRides)
    if (r.kind == k && !len) len = r.ms;
  const uint8_t home = b.hand.home;
  play(b, [&](uint32_t t) { return hw::ride::toRaw(hw::ride::at(k, t), home, feedingtest::kR0); }, len, eachFrame);
}

// Recorded 50 Hz samples (with their peak when the recording has it), as the sense task hands them on.
inline void replay(Badge& b, const std::vector<std::pair<uint32_t, BodySample>>& samples,
                   const std::function<void()>& eachFrame = {}) {
  if (samples.empty()) return;
  const uint32_t t0 = samples.front().first, base = b.ms;
  for (const auto& [ms, s] : samples) {
    b.ms = base + (ms - t0) + kSampleMs;
    b.ring.push_back({s, b.ms});
    if (int32_t(b.ms - b.nextFrame) >= 0) {
      b.frame(), b.nextFrame = b.ms + b.frameMs;
      if (eachFrame) eachFrame();
    }
  }
}

// Every count of CarryShow::started, so a test can see what a motion started.
struct Starts {
  uint16_t n[kCarryKinds]{};
  static Starts of(const Badge& b) {
    Starts s;
    for (uint8_t i = 0; i < kCarryKinds; ++i) s.n[i] = b.dish->carry().started(Carry(i));
    return s;
  }
  Starts since(const Starts& before) const {
    Starts d;
    for (uint8_t i = 0; i < kCarryKinds; ++i) d.n[i] = uint16_t(n[i] - before.n[i]);
    return d;
  }
  int total() const {
    int t = 0;
    for (uint8_t i = 1; i < kCarryKinds; ++i) t += n[i];
    return t;
  }
  int operator[](Carry k) const { return n[uint8_t(k)]; }
  std::string str() const {
    std::string s;
    for (uint8_t i = 1; i < kCarryKinds; ++i)
      if (n[i]) s += std::string(" ") + carryName(Carry(i)) + "=" + std::to_string(n[i]);
    return s.empty() ? " none" : s;
  }
};

// Keeps him awake and at home in the dish for a long recording: no sleep pressure, no bedtime.
inline void keepAwake(Badge& b) {
  b.creature().inject(driveChem(drive::sleepiness), Fx::zero());
}

}  // namespace carrytest
