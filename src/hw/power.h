// When the badge may save its battery (docs/power.md). Pure: no Arduino, no
// clock, no board; the loop feeds it and does what it says (main.cpp).
//
// The badge drops to low power after kIdleMs with no user input: the panel
// sleeps with its backlight off, no frame is drawn and the CPU runs slower.
// The pet keeps living: his ticks, the 50 Hz body samples and Bluetooth go
// on, so time and the phone work as before. Anything a person does wakes it:
//
//   - the button (BOOT);
//   - a knock knock: the IMU's double tap, or two taps within kKnockPairMs.
//     One tap alone is a desk being bumped or a keyboard being typed on;
//   - moving the badge: the smoothed gravity drifting kDriftMg from where it
//     last settled (picked up, tilted, turned over), or a shake: two samples
//     jolting kJoltMg off the smoothed one within kJoltPairMs. One jolt alone
//     is the table, not a hand;
//   - a phone: a new Bluetooth connection or a protocol line from it.
//
// Face down (the screen to the floor) is bedtime: it goes low kFaceDownMs
// after the last input instead of the full idle timeout.
//
// A badge left on a table drifts by a few mg with temperature; the reference
// follows every wake, so only a real move crosses the threshold. While a
// hold is up (a duel, an update, a question on the screen) it never sleeps.
#pragma once

#include <cstdint>
#include <cstdlib>

#include "blorb/senses.h"

// Seconds with no user input before low power: the owner's 10 minutes. A
// test build may shorten it without touching the code:
//   PLATFORMIO_BUILD_FLAGS="-DBLORB_IDLE_SECONDS=30" pio run -e badge128
// (At run time, `DEBUG power idle <s>` / the debug link's `POWER idle <s>`
// change it until reset; `idle default` goes back to this.)
#ifndef BLORB_IDLE_SECONDS
#define BLORB_IDLE_SECONDS 600
#endif
static_assert(BLORB_IDLE_SECONDS >= 1 && BLORB_IDLE_SECONDS <= 86400, "BLORB_IDLE_SECONDS: 1 s to a day");

namespace power {

constexpr uint32_t kIdleMs = uint32_t(BLORB_IDLE_SECONDS) * 1000u;
constexpr int kDriftMg = 120;                     // about 7 degrees of tilt
constexpr int kJoltMg = 350;                      // past hand tremor and a car's hum
constexpr int kSmoothShift = 3;                   // 1/8 per 50 Hz sample: about 160 ms
constexpr uint32_t kKnockPairMs = 1500;           // two single taps this close are a knock knock
constexpr uint32_t kJoltPairMs = 300;             // two jolts this close are a hand, not a bump
constexpr uint32_t kFaceDownMs = 120000;          // face down is bedtime: low power after 2 min, not the full idle
constexpr int kFaceDownMg = -800;                 // smoothed az below this: the screen faces the floor

enum class Mode : uint8_t { Full, Low };

// What woke it, or sent it to sleep; for the log and the debug link.
enum class Why : uint8_t { Boot, Button, Tap, Drift, Jolt, Phone, Hold, Idle, Asked, FaceDown };
inline const char* whyName(Why w) {
  constexpr const char* kNames[] = {"boot", "button", "tap", "drift", "jolt", "phone", "hold", "idle", "asked", "facedown"};
  return kNames[uint8_t(w)];
}

class Policy {
 public:
  explicit Policy(uint32_t idleMs = kIdleMs) : idleMs_(idleMs) {}

  Mode mode() const { return mode_; }
  bool low() const { return mode_ == Mode::Low; }
  uint32_t idleMs() const { return idleMs_; }
  void setIdleMs(uint32_t ms) { idleMs_ = ms; }   // RAM only: the debug links' shorter timeout
  Why why() const { return why_; }                // the last change's reason
  uint32_t changes() const { return changes_; }   // every sleep and wake since boot
  uint32_t lastInput() const { return lastInput_; }
  Why lastInputWhy() const { return inputWhy_; }   // what the last input was, asleep or not
  // How long until it sleeps, from `now`; 0 when it is asleep or due.
  uint32_t msLeft(uint32_t now) const {
    if (low()) return 0;
    const uint32_t since = sinceInput(now);
    return since >= idleMs_ ? 0 : idleMs_ - since;
  }

  // One body sample, in the IMU's own frame (before any turn to the screen,
  // so a panel rotation is not itself a move). True when it is user input.
  bool sample(const blorb::BodySample& s, uint32_t now) {
    if (s.buttonDown) return input(Why::Button, now);
    if (s.tapCode == 2 || (s.tapCode && tapped_ && now - lastTap_ <= kKnockPairMs)) {
      tapped_ = false;
      return input(Why::Tap, now);
    }
    if (s.tapCode) tapped_ = true, lastTap_ = now;
    const int32_t a[3] = {s.ax, s.ay, s.az};
    if (!seeded_) {
      for (int i = 0; i < 3; ++i) smooth_[i] = ref_[i] = a[i] << kSmoothShift;
      seeded_ = true;
      return false;
    }
    bool jolt = false, drift = false;
    for (int i = 0; i < 3; ++i) {
      jolt |= std::abs(a[i] - (smooth_[i] >> kSmoothShift)) > kJoltMg;
      smooth_[i] += a[i] - (smooth_[i] >> kSmoothShift);
      drift |= std::abs((smooth_[i] - ref_[i]) >> kSmoothShift) > kDriftMg;
    }
    if (jolt) {
      const bool pair = jolted_ && now - lastJolt_ <= kJoltPairMs;
      jolted_ = !pair, lastJolt_ = now;
      jolt = pair;
    }
    faceDown_ = (smooth_[2] >> kSmoothShift) < kFaceDownMg;
    if (!jolt && !drift) return false;
    for (int i = 0; i < 3; ++i) ref_[i] = smooth_[i];   // it settles here next
    return input(jolt ? Why::Jolt : Why::Drift, now);
  }

  // A phone connected or spoke. Wakes the badge unless the debug link asked
  // for low power: that holds until a hand wakes it, so the page that asked
  // does not undo it with its next line.
  void phone(uint32_t now) {
    if (int32_t(now - lastInput_) > 0) lastInput_ = now;
    inputWhy_ = Why::Phone;
    if (low() && why_ != Why::Asked) set(Mode::Full, Why::Phone);
  }

  // Every loop. `held`: something on the screen needs it awake (a duel, an
  // update, a question). True when the mode changed.
  bool update(uint32_t now, bool held) {
    if (held) {
      if (int32_t(now - lastInput_) > 0) lastInput_ = now;
      return low() ? set(Mode::Full, Why::Hold) : false;
    }
    if (!low() && sinceInput(now) >= idleMs_) return set(Mode::Low, Why::Idle);
    if (!low() && faceDown_ && sinceInput(now) >= kFaceDownMs) return set(Mode::Low, Why::FaceDown);
    return false;
  }

  // The debug links: low power now, or awake now (as an input would).
  void sleepNow() {
    if (!low()) set(Mode::Low, Why::Asked);
    else why_ = Why::Asked;
  }
  void wakeNow(uint32_t now) { input(Why::Asked, now); }

 private:
  // Inputs carry their own times (a body sample's, a debug link's now), so one
  // may land a little after the `now` the loop is on: that is no time at all,
  // not four billion milliseconds.
  uint32_t sinceInput(uint32_t now) const {
    const int32_t d = int32_t(now - lastInput_);
    return d < 0 ? 0 : uint32_t(d);
  }
  bool input(Why w, uint32_t now) {
    if (int32_t(now - lastInput_) < 0) now = lastInput_;   // never move it back
    lastInput_ = now;
    inputWhy_ = w;
    if (low()) set(Mode::Full, w);
    return true;
  }
  bool set(Mode m, Why w) {
    mode_ = m, why_ = w, ++changes_;
    return true;
  }

  uint32_t idleMs_;
  Mode mode_ = Mode::Full;
  Why why_ = Why::Boot, inputWhy_ = Why::Boot;
  uint32_t changes_ = 0, lastInput_ = 0;
  bool seeded_ = false, tapped_ = false, jolted_ = false, faceDown_ = false;
  uint32_t lastTap_ = 0, lastJolt_ = 0;
  int32_t smooth_[3] = {}, ref_[3] = {};   // milli-g << kSmoothShift
};

}  // namespace power
