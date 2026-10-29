#pragma once
// A hand on the badge says yes before a phone may give it a Wi-Fi network
// (docs/wifi-ota.md "Security"; owner, 2026-10-21: strangers in Bluetooth range
// must not set one). Improv's own way: the badge starts in "authorization
// required"; when a phone opens its Improv service, the badge asks on its
// screen ("Join Wi-Fi? Tip me on my side!"), and a turn of the badge or a press of
// its button within kAskMs (and after kMinAskMs, so a turn already under way
// is no yes) authorizes that phone for kGrantMs. Then Improv says
// "authorized" and the page sends the network. The cable needs none of this:
// it is a hand on the badge already.
//
// Pure: test/test_wifi_ota drives it. src/wifi_ota.h draws the question and
// hands it the yes (main.cpp's press and TurnYes, as the update's question).
#include <cstdint>
#include <optional>

namespace wifiota {

class Consent {
 public:
  static constexpr uint32_t kAskMs = 30000;
  static constexpr uint32_t kMinAskMs = 1500;    // ota::Session::kMinAskMs, TurnYes::kMinMs
  static constexpr uint32_t kGrantMs = 120000;   // to type the password after saying yes

  enum class Event : uint8_t { None, Granted, Lapsed, Expired };

  // A phone opened the service: ask, unless already asking or granted.
  void ask(uint32_t now) {
    if (asking_ || granted_) return;
    asking_ = now;
  }
  // The button or a turn, while the question shows. True: it was the
  // question's, and the pet must not feel it.
  bool yes(uint32_t now) {
    if (!asking_) return false;
    if (now - *asking_ >= kMinAskMs) {
      asking_.reset();
      granted_ = now;
      event_ = Event::Granted;
    }
    return true;
  }
  void tick(uint32_t now) {
    if (asking_ && now - *asking_ >= kAskMs) asking_.reset(), event_ = Event::Lapsed;
    if (granted_ && now - *granted_ >= kGrantMs) granted_.reset(), event_ = Event::Expired;
  }
  // The phone went, or its network was taken: back to asking next time.
  void end() { asking_.reset(), granted_.reset(); }

  bool asking() const { return asking_.has_value(); }
  bool granted() const { return granted_.has_value(); }
  uint32_t msLeft(uint32_t now) const { return asking_ && now - *asking_ < kAskMs ? kAskMs - (now - *asking_) : 0; }
  // What happened since the last call, once.
  Event take() {
    const Event e = event_;
    event_ = Event::None;
    return e;
  }

 private:
  std::optional<uint32_t> asking_, granted_;
  Event event_ = Event::None;
};

}  // namespace wifiota
