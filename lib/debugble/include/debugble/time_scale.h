#pragma once
// The debug link's fast-forward (SCALE, docs/debug-ble.md). It never moves a
// clock: it owes extra 100 ms ticks, and the firmware runs them as real ticks
// of the Dish, so his age in the save is exactly the ticks he lived, and the
// loop's clock only ever runs forward. RAM only: a reboot is 1x, and so is
// the end of the connection that asked, so a phone that walks away cannot
// leave him ageing sixty times over.
#include <algorithm>
#include <cstdint>

namespace debugble {

class TimeScale {
 public:
  static constexpr uint16_t kMax = 60;
  // A loop too slow to keep up drops what it owes past this (a pet minute),
  // so a long frame never turns into a burst of hours.
  static constexpr uint32_t kMaxOwedTicks = 600;
  static constexpr uint32_t kTickMs = 100;   // blorb::kTickMs

  // 1 is real time. `connection` is whose request it was.
  void set(uint16_t factor, uint32_t now, uint32_t connection) {
    factor_ = std::clamp<uint16_t>(factor, 1, kMax);
    connection_ = connection;
    last_ = now;
    carryMs_ = 0;
    owed_ = 0;
  }
  void reset(uint32_t now) { set(1, now, 0); }
  uint16_t factor() const { return factor_; }
  uint32_t connection() const { return connection_; }

  // The extra ticks owed by now, beyond the real time the loop already runs.
  uint32_t owed(uint32_t now) {
    const uint32_t elapsed = now - last_;
    last_ = now;
    if (factor_ <= 1) return owed_ = 0;
    const uint64_t extraMs = uint64_t(elapsed) * (factor_ - 1) + carryMs_;
    carryMs_ = uint32_t(extraMs % kTickMs);
    const uint64_t total = owed_ + extraMs / kTickMs;
    if (total > kMaxOwedTicks) dropped_ += uint32_t(total - kMaxOwedTicks);
    owed_ = uint32_t(std::min<uint64_t>(total, kMaxOwedTicks));
    return owed_;
  }
  void ran(uint32_t ticks) { owed_ -= std::min(ticks, owed_); }
  uint32_t dropped() const { return dropped_; }   // ticks a slow loop could not run, since boot

 private:
  uint16_t factor_ = 1;
  uint32_t connection_ = 0, last_ = 0, carryMs_ = 0, owed_ = 0, dropped_ = 0;
};

}  // namespace debugble
