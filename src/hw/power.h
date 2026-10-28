// When the badge may save its battery (docs/power.md). Pure: no Arduino, no
// clock, no board; the loop feeds it and does what it says (main.cpp).
//
// With no user input the badge steps down (docs/power.md):
//
//   full --dimMs (half the timeout)--> dim --kIdleMs--> low (dark) --kNapMs dark--> nap
//
// Dim: the backlight at a quarter. Low: the panel sleeps with its backlight
// off, no frame is drawn, the CPU runs slower and Bluetooth advertises slowly.
// The nap (napDue, main.cpp decides when the radio and the cable allow it):
// the chip light-sleeps a second at a time, Bluetooth off, the IMU watching
// for motion. The pet keeps living through all of it: his ticks run on the
// same clock, so he ages exactly as he would awake. Anything a person does
// wakes it:
//
//   - the button (BOOT);
//   - a knock knock: the IMU's double tap, or two taps within kKnockPairMs.
//     One tap alone is a desk being bumped or a keyboard being typed on;
//   - moving the badge: the smoothed gravity drifting kDriftMg from where it
//     last settled (picked up, tilted, turned over), or a shake: two samples
//     jolting kJoltMg off the smoothed one within kJoltPairMs. One jolt alone
//     is the table, not a hand;
//   - a hand on it (2026-10-28: a level pick-up, or a dark badge held still,
//     moved neither of those, so a nap woken by the IMU's pin went back to
//     sleep and only BOOT woke it): a stir, the badge moving for a fifth of
//     a second (kStirMg), or a tremor, the jitter of a hand holding it for a
//     second (kTremorMg), from dark only. A knock or a typed-on desk does
//     neither; a car seat's hum looks like a tremor, so two untouched tremor
//     wakes in a row mute it for half an hour (kTremorUnanswered);
//   - a phone: a new Bluetooth connection or a protocol line from it.
//
// Face down (the screen to the floor) is bedtime: it goes low kFaceDownMs
// after the last input instead of the full idle timeout.
//
// A badge left on a table drifts by a few mg with temperature; the reference
// follows every wake, so only a real move crosses the threshold. While a
// hold is up (a duel, an update, a question on the screen) it never sleeps.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "blorb/senses.h"

// Seconds with no user input before the screen goes dark: one minute (the
// owner, 2026-10-27: "the screen times out earlier if it isn't being used";
// it was ten). It dims at half that. A test build may change it without
// touching the code:
//   PLATFORMIO_BUILD_FLAGS="-DBLORB_IDLE_SECONDS=30" pio run -e badge128
// (At run time, `DEBUG power idle <s>` / the debug link's `POWER idle <s>`
// change it until reset; `idle default` goes back to this.)
#ifndef BLORB_IDLE_SECONDS
#define BLORB_IDLE_SECONDS 60
#endif
// Seconds dark before the nap: long enough for a phone to find it after it
// was put down, short enough that a badge left alone spends its day asleep.
#ifndef BLORB_NAP_SECONDS
#define BLORB_NAP_SECONDS 120
#endif
static_assert(BLORB_NAP_SECONDS >= 0 && BLORB_NAP_SECONDS <= 86400, "BLORB_NAP_SECONDS: 0 s to a day");
static_assert(BLORB_IDLE_SECONDS >= 1 && BLORB_IDLE_SECONDS <= 86400, "BLORB_IDLE_SECONDS: 1 s to a day");

namespace power {

constexpr uint32_t kIdleMs = uint32_t(BLORB_IDLE_SECONDS) * 1000u;
constexpr uint32_t kNapMs = uint32_t(BLORB_NAP_SECONDS) * 1000u;
constexpr int kDriftMg = 120;                     // about 7 degrees of tilt
constexpr int kJoltMg = 350;                      // past hand tremor and a car's hum
constexpr int kSmoothShift = 3;                   // 1/8 per 50 Hz sample: about 160 ms
constexpr uint32_t kKnockPairMs = 1500;           // two single taps this close are a knock knock
constexpr uint32_t kJoltPairMs = 300;             // two jolts this close are a hand, not a bump
constexpr uint32_t kFaceDownMs = 15000;           // face down is bedtime: dark after 15 s, not the full idle
constexpr int kFaceDownMg = -800;                 // smoothed az below this: the screen faces the floor
// A hand moving it (Why::Stir): the quick average (kSmoothShift) apart from a
// slow one (kStirSlowShift) by kStirMg, for kStirScore samples more than not.
// The quick one averages the median of the last three samples, so a knock's
// one-sample spike never shows, and a vibration averages out. Each move that
// counts brings the slow one to the quick: a turn counts while it goes on.
constexpr int kStirMg = 30;
constexpr int kStirScore = 10;                    // 200 ms of 50 Hz samples
constexpr int kStirSlowShift = 6;                 // 1/64 per 50 Hz sample: about 1.3 s
// A hand holding it still (Why::Tremor): a hand trembles, 15-30 mg from one
// 50 Hz sample to the next; a table is 2-5 mg. The average of that change
// (each clamped to kTremorClampMg, so a knock adds little) past kTremorMg for
// kTremorScore samples more than not. Measured on the real-hands recording
// (test_controls NapWake): held still 16-35 mg, the desk typed on and bumped
// (T2) 3 mg at the median and briefer bursts.
constexpr int kTremorMg = 15;
constexpr int kTremorScore = 50;                  // a second of 50 Hz samples
constexpr int kTremorClampMg = 60;
constexpr int kTremorShift = 4;                   // 1/16 per sample: about 320 ms
// The tremor only wakes it from dark; lit, it keeps nothing lit. The owner's
// badges ride in cars, and a seat's hum can look like a hand: a tremor wake
// left untouched until it dims (no press, knock knock, tilt, turn, shake,
// phone) is unanswered, and kTremorUnanswered in a row stop the tremor
// waking it for kTremorMuteMs. A stir, BOOT, a knock knock and the nap's
// timer still wake it, and any of those but the stir ends the backoff.
constexpr uint8_t kTremorUnanswered = 2;
constexpr uint32_t kTremorMuteMs = 30u * 60 * 1000;
constexpr uint32_t kGapMs = 250;                 // samples further apart (the nap's listens) start the counts over

enum class Mode : uint8_t { Full, Low };

// What woke it, or sent it to sleep; for the log and the debug link.
enum class Why : uint8_t { Boot, Button, Tap, Drift, Jolt, Phone, Hold, Idle, Asked, FaceDown, Reminder, Stir, Tremor };
inline const char* whyName(Why w) {
  constexpr const char* kNames[] = {"boot",  "button", "tap",      "drift",   "jolt",    "phone",
                                    "hold",  "idle",   "asked",    "facedown", "reminder", "stir", "tremor"};
  return kNames[uint8_t(w)];
}

class Policy {
 public:
  explicit Policy(uint32_t idleMs = kIdleMs, uint32_t napMs = kNapMs) : idleMs_(idleMs), napMs_(napMs) {}

  Mode mode() const { return mode_; }
  bool low() const { return mode_ == Mode::Low; }
  uint32_t idleMs() const { return idleMs_; }
  void setIdleMs(uint32_t ms) { idleMs_ = ms; }   // RAM only: the debug links' shorter timeout
  uint32_t napMs() const { return napMs_; }
  void setNapMs(uint32_t ms) { napMs_ = ms; }     // RAM only, as setIdleMs
  // Awake with no input for half the timeout: the backlight dims. Any input
  // (or a hold, which counts as one) brings it straight back.
  uint32_t dimMs() const { return idleMs_ / 2; }
  bool dim(uint32_t now) const { return !low() && !nudging_ && sinceInput(now) >= dimMs(); }
  // Dark for napMs: the chip may nap (light sleep between seconds).
  bool napDue(uint32_t now) const { return low() && int32_t(now - lowSince_) >= int32_t(napMs_); }
  uint32_t lowSince() const { return lowSince_; }
  // Every real interaction (an input, a phone, a hold) counts one; a reminder
  // does not. The reminder's clock starts over when this moves.
  uint32_t interactions() const { return interactions_; }
  bool nudging() const { return nudging_; }

  // The reminder (Reminder, below): lit for `litMs` from dark, full
  // brightness, then dark again and the nap due at once. Any input meanwhile
  // makes it an ordinary wake with the whole timeout. False when not dark.
  bool nudge(uint32_t now, uint32_t litMs) {
    if (!low()) return false;
    set(Mode::Full, Why::Reminder, now);
    lastInput_ = now - (litMs < idleMs_ ? idleMs_ - litMs : 0);
    nudging_ = true;
    return true;
  }
  // Tremors wake nothing now (the backoff, update()); how long more, ms.
  bool tremorMuted(uint32_t now) const { return tremorMutedMs(now) > 0; }
  uint32_t tremorMutedMs(uint32_t now) const {
    return tremorMuted_ && int32_t(tremorMutedUntil_ - now) > 0 ? tremorMutedUntil_ - now : 0;
  }
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
      settle(a);
      seeded_ = true, lastMs_ = now;
      return false;
    }
    // A gap in the samples (the nap reads none between its listens): the
    // stir and the tremor start counting again.
    if (int32_t(now - lastMs_) > int32_t(kGapMs)) stir_ = tremor_ = 0;
    lastMs_ = now;
    bool jolt = false, drift = false, stirred = false, trembling = false;
    for (int i = 0; i < 3; ++i) {
      jolt |= std::abs(a[i] - (smooth_[i] >> kSmoothShift)) > kJoltMg;
      smooth_[i] += a[i] - (smooth_[i] >> kSmoothShift);
      drift |= std::abs((smooth_[i] - ref_[i]) >> kSmoothShift) > kDriftMg;
      // Stirred, and trembling (above, at kStirMg and kTremorMg).
      const int32_t m = median(a[i], prev_[i], prev2_[i]);
      quick_[i] += m - (quick_[i] >> kSmoothShift);
      slow_[i] += (quick_[i] >> kSmoothShift) - (slow_[i] >> kStirSlowShift);
      stirred |= std::abs((quick_[i] >> kSmoothShift) - (slow_[i] >> kStirSlowShift)) > kStirMg;
      jitter_[i] += std::min<int32_t>(std::abs(a[i] - prev_[i]), kTremorClampMg) - (jitter_[i] >> kTremorShift);
      trembling |= (jitter_[i] >> kTremorShift) > kTremorMg;
      prev2_[i] = prev_[i], prev_[i] = a[i];
    }
    stir_ = stirred ? stir_ + 1 : std::max(0, stir_ - 1);
    tremor_ = trembling ? tremor_ + 1 : std::max(0, tremor_ - 1);
    if (jolt) {
      const bool pair = jolted_ && now - lastJolt_ <= kJoltPairMs;
      jolted_ = !pair, lastJolt_ = now;
      jolt = pair;
    }
    faceDown_ = (smooth_[2] >> kSmoothShift) < kFaceDownMg;
    Why w;
    if (jolt || drift) w = jolt ? Why::Jolt : Why::Drift;
    else if (stir_ >= kStirScore) w = Why::Stir;
    else if (tremor_ >= kTremorScore && low() && !tremorMuted(now)) w = Why::Tremor;
    else return tremor_ = std::min(tremor_, kTremorScore), false;   // lit, a tremor keeps nothing lit
    // It settles here next: a move counts while it goes on, not after.
    for (int i = 0; i < 3; ++i) {
      if (w != Why::Stir && w != Why::Tremor) ref_[i] = smooth_[i];
      slow_[i] = (quick_[i] >> kSmoothShift) << kStirSlowShift;
    }
    stir_ = tremor_ = 0;
    return input(w, now);
  }

  // The nap's look at gravity each time it wakes (minutes apart, so no
  // smoothing): a sample kDriftMg off where the badge last settled is a move,
  // one the IMU's wake-on-motion missed (lifted too gently). True when it is input.
  bool glance(const blorb::BodySample& s, uint32_t now) {
    if (!seeded_) return sample(s, now);
    const int32_t a[3] = {s.ax, s.ay, s.az};
    bool moved = false;
    for (int i = 0; i < 3; ++i) moved |= std::abs(a[i] - (ref_[i] >> kSmoothShift)) > kDriftMg;
    if (!moved) return false;
    settle(a);   // it settles here next
    return input(Why::Drift, now);
  }

  // A phone connected or spoke. Wakes the badge unless the debug link asked
  // for low power: that holds until a hand wakes it, so the page that asked
  // does not undo it with its next line.
  void phone(uint32_t now) {
    interacted();
    if (int32_t(now - lastInput_) > 0) lastInput_ = now;
    inputWhy_ = Why::Phone;
    ++interactions_, nudging_ = false;
    if (low() && why_ != Why::Asked) set(Mode::Full, Why::Phone, now);
  }

  // Every loop. `held`: something on the screen needs it awake (a duel, an
  // update, a question). True when the mode changed.
  bool update(uint32_t now, bool held) {
    // The tremor's backoff: a tremor wake nobody did anything with before
    // it dimmed is one unanswered; kTremorUnanswered in a row and tremors
    // wake nothing for kTremorMuteMs (a car seat's hum, not a hand).
    if (!held && !low() && tremorWake_ && sinceInput(now) >= dimMs()) {
      tremorWake_ = false;
      if (++unanswered_ >= kTremorUnanswered) tremorMuted_ = true, tremorMutedUntil_ = now + kTremorMuteMs, unanswered_ = 0;
    }
    if (held) {
      interacted();
      if (int32_t(now - lastInput_) > 0) lastInput_ = now;
      ++interactions_, nudging_ = false;
      return low() ? set(Mode::Full, Why::Hold, now) : false;
    }
    if (!low() && sinceInput(now) >= idleMs_) {
      set(Mode::Low, Why::Idle, now);
      if (nudging_) lowSince_ = now - napMs_, nudging_ = false;   // a reminder ends straight in the nap
      return true;
    }
    if (!low() && faceDown_ && sinceInput(now) >= std::min(kFaceDownMs, idleMs_)) return set(Mode::Low, Why::FaceDown, now);
    return false;
  }

  // The debug links (and a quiet restart): low power now, or awake now (as an
  // input would). `nap`: the nap is due at once, not napMs from now.
  void sleepNow(uint32_t now, bool nap = false) {
    if (!low()) set(Mode::Low, Why::Asked, now);
    else why_ = Why::Asked;
    if (nap) lowSince_ = now - napMs_;
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
    if (w == Why::Tremor) tremorWake_ = true;   // only from dark: sample() asks nothing else of it
    else if (w != Why::Stir) interacted();
    if (int32_t(now - lastInput_) < 0) now = lastInput_;   // never move it back
    lastInput_ = now;
    inputWhy_ = w;
    ++interactions_, nudging_ = false;
    if (low()) set(Mode::Full, w, now);
    return true;
  }
  // A press, a knock knock, a tilt, a shake, a phone, a hold: the tremor's backoff starts over.
  void interacted() { tremorWake_ = false, unanswered_ = 0, tremorMuted_ = false; }
  static int32_t median(int32_t a, int32_t b, int32_t c) { return std::max(std::min(a, b), std::min(std::max(a, b), c)); }
  // Every average at `a`, and nothing stirring.
  void settle(const int32_t (&a)[3]) {
    for (int i = 0; i < 3; ++i) {
      smooth_[i] = ref_[i] = quick_[i] = a[i] << kSmoothShift;
      slow_[i] = a[i] << kStirSlowShift;
      prev_[i] = prev2_[i] = a[i], jitter_[i] = 0;
    }
    stir_ = tremor_ = 0;
  }
  bool set(Mode m, Why w, uint32_t now) {
    mode_ = m, why_ = w, ++changes_;
    if (m == Mode::Low) lowSince_ = now;
    return true;
  }

  uint32_t idleMs_, napMs_, lowSince_ = 0, interactions_ = 0;
  bool nudging_ = false;
  Mode mode_ = Mode::Full;
  Why why_ = Why::Boot, inputWhy_ = Why::Boot;
  uint32_t changes_ = 0, lastInput_ = 0;
  bool seeded_ = false, tapped_ = false, jolted_ = false, faceDown_ = false;
  uint32_t lastTap_ = 0, lastJolt_ = 0;
  int32_t smooth_[3] = {}, ref_[3] = {};   // milli-g << kSmoothShift
  int32_t quick_[3] = {}, slow_[3] = {};   // the stir's: << kSmoothShift, << kStirSlowShift
  int stir_ = 0;                           // samples stirred, less samples still
  int32_t prev_[3] = {}, prev2_[3] = {};   // the last two samples
  int32_t jitter_[3] = {};                 // the tremor's, << kTremorShift
  int tremor_ = 0;                         // samples trembling, less samples steady
  uint32_t lastMs_ = 0;                    // the last sample's time
  bool tremorWake_ = false;                // lit by a tremor, and nothing done with it since
  bool tremorMuted_ = false;               // the tremor wakes nothing until tremorMutedUntil_
  uint8_t unanswered_ = 0;                 // tremor wakes in a row that dimmed untouched
  uint32_t tremorMutedUntil_ = 0;
};

// One nap sleep: to the next scheduled Wi-Fi check, or kNapCapMs, whichever
// comes first. The check opens on the pass that wakes at its due time, so it
// is never later than its schedule (the owner: automatic updates never switch
// off). `checkInMs`: until the next check is due, UINT32_MAX when no check
// can open (no network remembered, Wi-Fi switched off). A check already due
// that the pass did not open is held by something time does not change while
// napping (a low battery): the cap, and it is asked again then.
constexpr uint32_t kNapCapMs = 60000;
inline uint32_t napSleepMs(uint32_t checkInMs, uint32_t capMs = kNapCapMs) {
  return checkInMs == 0 ? capMs : std::min(checkInMs, capMs);
}

// The reminder (the owner, 2026-10-27: "have the screen turn on occasionally
// to remind the children that the badge exists"): kReminderMinS to
// kReminderMaxS after the last real interaction, at any hour (a dark bedroom
// included), the screen lights for kReminderLitMs and he says hello. A badge
// played with never needs one. It runs on a countdown the board keeps in NVS
// (remainingS, resume), so naps and restarts do not reset or lose it; time
// with the battery flat does not count (whoever charges it has seen it).
constexpr uint32_t kReminderMinS = 3u * 24 * 3600;
constexpr uint32_t kReminderMaxS = 5u * 24 * 3600;
constexpr uint32_t kReminderLitMs = 60000;   // the owner: a whole minute, full brightness, never dimmed

class Reminder {
 public:
  // A real interaction, or a reminder just shown: the next is 3 to 5 days off.
  // `rnd`: any random number.
  void restart(uint32_t nowMs, uint32_t rnd) {
    at(nowMs, kReminderMinS + rnd % (kReminderMaxS - kReminderMinS + 1));
  }
  // At boot, the countdown NVS kept (clamped to the longest wait).
  void resume(uint32_t nowMs, uint32_t remainingS) { at(nowMs, std::min(remainingS, kReminderMaxS)); }
  uint32_t remainingS(uint32_t nowMs) const { return dueIn(nowMs) / 1000; }
  uint32_t dueIn(uint32_t nowMs) const { return int32_t(dueAt_ - nowMs) > 0 ? dueAt_ - nowMs : 0; }
  bool due(uint32_t nowMs) const { return dueIn(nowMs) == 0; }

 private:
  void at(uint32_t nowMs, uint32_t inS) { dueAt_ = nowMs + inS * 1000; }   // 5 days is 432,000,000 ms: well inside int32
  uint32_t dueAt_ = kReminderMinS * 1000;
};

// What the board knows that the policy does not, for the nap. Each true one
// keeps it from napping: the nap stops the radio and the cable's ears.
struct NapFacts {
  bool board = true;    // this board's IMU pin is known
  bool imu = true;      // an IMU answered: without it only BOOT could wake it
  bool held = false;    // a duel, an update's question...: powerHeld()
  bool phone = false;   // a phone is connected
  bool wifi = false;    // a Wi-Fi window is open (the scheduled check, a provision, a trial's confirm)
  bool air = false;     // ESP-NOW is open
  bool trial = false;   // this image is on its OTA trial
  bool update = false;  // an update session runs
  bool cable = false;   // the cable spoke in the last few seconds
};

// Why it may not nap now (for `nap_block=`); null when it may.
inline const char* napBlocker(const Policy& p, uint32_t now, const NapFacts& f) {
  if (!p.low()) return "awake";
  if (!p.napDue(now)) return "not_due";
  if (!f.board) return "board";
  if (!f.imu) return "no_imu";
  if (f.held) return "held";
  if (f.phone) return "phone";
  if (f.wifi) return "wifi";
  if (f.air) return "air";
  if (f.trial) return "trial";
  if (f.update) return "update";
  if (f.cable) return "cable";
  return nullptr;
}

}  // namespace power
