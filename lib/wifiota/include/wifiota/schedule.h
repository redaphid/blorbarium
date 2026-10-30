#pragma once
// When the badge brings Wi-Fi up, and when it must drop it (docs/wifi-ota.md
// "When Wi-Fi is on"). Pure: the loop gathers the facts each pass, asks, and
// does what it is told (src/wifi_ota.h).
//
// The owner (2026-10-21): "the use case is primarily offline car". So with no
// known network in range the badge must behave as if Wi-Fi did not exist,
// and Wi-Fi is off unless a window is open. A window is one of:
//   - Check: the badge has been dark (low power: no input for the idle time)
//     for kDarkMs, no phone is connected, no question is on the screen, no
//     duel or fight mode, the battery is not low, and a check is due. One scan
//     looks for a remembered network; none in range closes the window at once.
//     Due once dark after boot, then kCheckEveryMs after a manifest was read.
//     A failure waits longer each time: kAbsentFirstMs doubling to
//     kAbsentMaxMs while no remembered network is in range (a car: a few
//     scans a day, each a couple of seconds), kServerFirstMs doubling to
//     kServerMaxMs while the network is there but the site is not;
//   - Forced: `DEBUG ota check`, now, whatever the power and whoever is there;
//   - Confirm: an image on trial after an update over Wi-Fi, which must reach
//     the site over Wi-Fi within the trial's 5 minutes to keep itself;
//   - Provision: a phone sent credentials over Improv (with a turn of the
//     badge to say yes), to be tried at once.
// A Check window closes the moment someone is there again (an input, a
// phone, a question). Every window but Confirm closes for a duel or fight
// mode, which needs the radio on its own channel.
//
// No power sensing decides anything: the 1.28 board has no VBUS or charge pin,
// and the battery's voltage is only a guess at a charger (power_sense.h). It
// is read, logged, and holds a Check only when the cell is clearly low.
#include <cstdint>
#include <optional>

namespace wifiota {

struct Facts {
  bool enabled;        // a network remembered (there is no switch: checks are never turned off)
  uint32_t darkMs;     // how long low power has lasted: 0 while awake
  bool someoneThere;   // a phone connected, or a question or a Bluetooth update on the screen
  bool radioBusy;      // fight mode, a duel, or ESP-NOW open: they need the radio on channel 1
  bool lowBattery;     // a real battery reading under kLowBatteryMv, for under kLowBatteryMostMs (holdForBattery)
};

enum class Window : uint8_t { None, Check, Forced, Confirm, Provision };
const char* windowName(Window);

// Why a Check window is not open (for `DEBUG wifi`), or Open.
enum class Hold : uint8_t { Open, Disabled, NotIdle, SomeoneThere, RadioBusy, LowBattery, NotDue };
const char* holdName(Hold);

// How a Check failed, which sets how long until the next.
enum class Failure : uint8_t {
  Absent,   // no remembered network in the scan, or it would not be joined: a car, a trip
  Server,   // joined, but the site, its certificate or the manifest failed
};

class Schedule {
 public:
  static constexpr uint32_t kMinute = 60u * 1000;
  static constexpr uint32_t kDarkMs = 1 * kMinute;           // dark this long first: not the moment it dims
  static constexpr uint32_t kCheckEveryMs = 6 * 60 * kMinute;   // the owner: "every few hours"
  static constexpr uint32_t kAbsentFirstMs = 15 * kMinute;
  static constexpr uint32_t kAbsentMaxMs = 8 * 60 * kMinute;
  static constexpr uint32_t kServerFirstMs = 15 * kMinute;
  static constexpr uint32_t kServerMaxMs = 6 * 60 * kMinute;
  static constexpr uint16_t kLowBatteryMv = 3550;
  // The owner (2026-10-27): automatic updates are never switched off; they are
  // the only way to reach the pets from afar. So nothing holds checks for
  // good: a restart inside a window backs off kCrashFirstMs doubling to
  // kCrashMostMs (a few hours, never forever; a crashing new image rolls
  // back on its own), and a battery that reads low holds them at most
  // kLowBatteryMostMs at a stretch (a reading stuck low must not end updates).
  static constexpr uint32_t kCrashFirstMs = 30 * kMinute;
  static constexpr uint32_t kCrashMostMs = 4 * 60 * kMinute;
  static constexpr uint32_t kLowBatteryMostMs = 12 * 60 * kMinute;

  // Whether a Check window may open now.
  Hold checkHold(const Facts& f, uint32_t now) const {
    if (!f.enabled) return Hold::Disabled;
    if (f.radioBusy) return Hold::RadioBusy;
    if (f.someoneThere) return Hold::SomeoneThere;
    if (f.darkMs < kDarkMs) return Hold::NotIdle;
    if (f.lowBattery) return Hold::LowBattery;
    if (!due(now)) return Hold::NotDue;
    return Hold::Open;
  }

  // Whether an open window carries on.
  static bool keep(Window w, const Facts& f) {
    switch (w) {
      case Window::Check: return f.enabled && f.darkMs && !f.someoneThere && !f.radioBusy;
      case Window::Forced:
      case Window::Provision: return !f.radioBusy;
      case Window::Confirm: return true;   // on trial no duel opens (docs/ota.md "Other users of the radio")
      case Window::None: return false;
    }
    return false;
  }

  bool due(uint32_t now) const { return !next_ || int32_t(now - *next_) >= 0; }
  // Milliseconds until the next check is due, 0 when it is.
  uint32_t dueIn(uint32_t now) const { return due(now) ? 0 : *next_ - now; }

  // A manifest was read: the next check is a while away, and the waits start over.
  void checked(uint32_t now) {
    next_ = now + everyMs_;
    absent_ = server_ = 0;
  }
  // A Check failed: the next waits longer the more there were in a row.
  void failed(Failure why, uint32_t now) {
    uint8_t& streak = why == Failure::Absent ? absent_ : server_;
    const uint32_t first = why == Failure::Absent ? kAbsentFirstMs : kServerFirstMs;
    const uint32_t most = why == Failure::Absent ? kAbsentMaxMs : kServerMaxMs;
    uint32_t wait = first;
    for (uint8_t i = 0; i < streak && wait < most; ++i) wait *= 2;
    if (wait > most) wait = most;
    if (streak < 255) ++streak;
    if (why == Failure::Server) absent_ = 0;   // the network was there
    next_ = now + wait;
  }
  // The last boot ended inside a window (a crash, a watchdog, a brownout), the
  // `inARow`th time running: kCrashFirstMs doubling to kCrashMostMs, so a fault
  // in Wi-Fi never becomes a loop of restarts, and never stops the checks.
  void crashed(uint32_t now, uint32_t inARow = 1) {
    uint32_t wait = kCrashFirstMs;
    for (uint32_t i = 1; i < inARow && wait < kCrashMostMs; ++i) wait *= 2;
    next_ = now + (wait < kCrashMostMs ? wait : kCrashMostMs);
  }
  // Whether a low battery reading (`low`) holds a check now: only for the
  // first kLowBatteryMostMs of an unbroken stretch of low readings.
  bool holdForBattery(bool low, uint32_t now) {
    if (!low) return lowSince_.reset(), false;
    if (!lowSince_) lowSince_ = now;
    return now - *lowSince_ < kLowBatteryMostMs;
  }
  // `DEBUG wifi due`: the next check is due now (the schedule runs on the real
  // clock, which DEBUG warp does not move). It still waits to be dark and
  // alone, and the streaks stand: a check that fails again backs off as far
  // as it would have.
  void dueNow() { next_.reset(); }
  // The remote debug's `interval` (docs/remote-debug.md): the time between
  // checks from now until a restart, within kEveryMinMs..kEveryMaxMs (false,
  // and nothing changed, outside them). A check further off than the new
  // interval comes in to it. Checks can be made rarer, never stopped.
  static constexpr uint32_t kEveryMinMs = 30 * kMinute;
  static constexpr uint32_t kEveryMaxMs = 12 * 60 * kMinute;
  bool setEvery(uint32_t ms, uint32_t now) {
    if (ms < kEveryMinMs || ms > kEveryMaxMs) return false;
    everyMs_ = ms;
    if (next_ && int32_t(*next_ - (now + ms)) > 0) next_ = now + ms;
    return true;
  }
  uint32_t everyMs() const { return everyMs_; }

  uint8_t absentStreak() const { return absent_; }
  uint8_t serverStreak() const { return server_; }

 private:
  std::optional<uint32_t> next_;   // unset: due at once (after boot, once dark)
  std::optional<uint32_t> lowSince_;   // when the battery started reading low
  uint8_t absent_ = 0, server_ = 0;
  uint32_t everyMs_ = kCheckEveryMs;
};

}  // namespace wifiota
