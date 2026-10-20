#pragma once
// When the badge brings Wi-Fi up, and when it must drop it (docs/wifi-ota.md
// "When Wi-Fi is on"). Pure: the loop gathers the facts each pass, asks, and
// does what it is told (src/wifi_ota.h).
//
// Wi-Fi is off unless a window is open. A window is one of:
//   - Check: on external power, idle (low power: no user input for the idle
//     time), nobody there, no duel or fight mode, and a check is due. Due at
//     once after boot, then every kCheckEveryMs after a manifest was read;
//     after a failure (no network, no answer) it backs off from kFirstRetryMs,
//     doubling to kMaxRetryMs, so on power it keeps trying all night;
//   - Forced: `DEBUG ota check`, now, whatever the power and whoever is there;
//   - Confirm: an image on trial after an update over Wi-Fi, which must reach
//     the site over Wi-Fi within the trial's 5 minutes to keep itself;
//   - Provision: a phone sent credentials over Improv, to be tried at once.
// A Check window closes the moment someone is there again (an input, a
// phone, a question). Every window but Confirm closes for a duel or fight
// mode, which needs the radio on its own channel. On battery no Check window
// opens; one that is open finishes (the power reading may sag a moment).
#include <cstdint>
#include <optional>

namespace wifiota {

struct Facts {
  bool enabled;        // networks remembered, and the badge's own switch is on
  bool onPower;        // PowerSense
  bool idle;           // low power: no user input for the idle time
  bool someoneThere;   // a phone connected, or a question or a Bluetooth update on the screen
  bool radioBusy;      // fight mode, a duel, or ESP-NOW open: they need the radio on channel 1
};

enum class Window : uint8_t { None, Check, Forced, Confirm, Provision };
const char* windowName(Window);

// Why a Check window is not open (for `DEBUG wifi`), or Open.
enum class Hold : uint8_t { Open, Disabled, OnBattery, NotIdle, SomeoneThere, RadioBusy, NotDue };
const char* holdName(Hold);

class Schedule {
 public:
  static constexpr uint32_t kCheckEveryMs = 6u * 60 * 60 * 1000;   // the owner: "every few hours"
  static constexpr uint32_t kFirstRetryMs = 60u * 1000;
  static constexpr uint32_t kMaxRetryMs = 30u * 60 * 1000;

  // Whether a Check window may open now.
  Hold checkHold(const Facts& f, uint32_t now) const {
    if (!f.enabled) return Hold::Disabled;
    if (!f.onPower) return Hold::OnBattery;
    if (f.radioBusy) return Hold::RadioBusy;
    if (f.someoneThere) return Hold::SomeoneThere;
    if (!f.idle) return Hold::NotIdle;
    if (!due(now)) return Hold::NotDue;
    return Hold::Open;
  }

  // Whether an open window carries on.
  static bool keep(Window w, const Facts& f) {
    switch (w) {
      case Window::Check: return f.enabled && f.idle && !f.someoneThere && !f.radioBusy;
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

  // A manifest was read: the next check is a while away, and failures start over.
  void checked(uint32_t now) {
    next_ = now + kCheckEveryMs;
    retry_ = kFirstRetryMs;
  }
  // No network, no answer, a broken manifest: try again later, later each time.
  void failed(uint32_t now) {
    next_ = now + retry_;
    retry_ = retry_ >= kMaxRetryMs / 2 ? kMaxRetryMs : retry_ * 2;
  }
  uint32_t retryMs() const { return retry_; }

 private:
  std::optional<uint32_t> next_;   // unset: due at once (after boot)
  uint32_t retry_ = kFirstRetryMs;
};

}  // namespace wifiota
