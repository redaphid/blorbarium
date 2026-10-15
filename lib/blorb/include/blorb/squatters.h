// Who may keep the dish's one Bluetooth connection. A central that has said
// nothing the protocol knows within 10 s is dropped and its address refused
// for 15 s, so a stranger, or another program that only knows this
// board's address (a badge daemon), cannot hold a child's dish hostage. A
// page always speaks at once (TIME, HELLO, OTA), and a connection with an
// update in progress is never dropped this way. Free of NimBLE so the native
// suite can drive it; NusLink calls it from the loop only.
#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

#include "blorb/protocol.h"

namespace blorb {

struct BleAddr {
  uint8_t b[6] = {};
  uint8_t type = 0;
  bool operator==(const BleAddr& o) const { return type == o.type && std::memcmp(b, o.b, sizeof b) == 0; }
};

// A line the protocol would run: an optional `#<1..6 hex>` id, then a known
// verb, or OTA. Parsed by hand: Arduino.h's word() macro breaks Args here.
inline bool speaksBlorb(std::string_view line) {
  if (!line.empty() && line[0] == '#') {
    size_t i = 1;
    while (i < line.size() && i <= 7 && std::isxdigit(static_cast<unsigned char>(line[i]))) ++i;
    if (i == 1 || i > 7 || i >= line.size() || line[i] != ' ') return false;
    line.remove_prefix(i + 1);
  }
  const size_t end = line.find(' ');
  const std::string_view verb = line.substr(0, end);
  if (verb == "OTA") return true;
  for (const CommandInfo& c : COMMANDS)
    if (verb == c.verb) return true;
  return false;
}

class Squatters {
 public:
  static constexpr uint32_t kSpeakWithinMs = 10000;
  // 15 s, not a minute: a real page that lost its first lines to a busy link
  // (Android runs one GATT operation at a time) must get back in quickly.
  static constexpr uint32_t kDenyMs = 15000;
  enum class Act : uint8_t { None, Refuse, RefuseQuietly, Evict };

  // A new connection from `a`. Refuse: drop it and say so; RefuseQuietly:
  // drop it, already said once this refusal.
  Act connected(const BleAddr& a, uint32_t now) {
    current_ = a;
    since_ = now;
    spoke_ = false;
    for (Deny& d : deny_) {
      if (!d.used || !(d.addr == a) || int32_t(d.until - now) <= 0) continue;
      current_.reset();
      if (d.logged) return Act::RefuseQuietly;
      d.logged = true;
      return Act::Refuse;
    }
    return Act::None;
  }

  void spoke() { spoke_ = true; }

  // Each loop: Evict once when the connection has had its chance; its
  // address is then refused for kDenyMs.
  Act poll(uint32_t now, bool sessionActive) {
    if (!current_ || spoke_ || sessionActive || now - since_ < kSpeakWithinMs) return Act::None;
    deny(*current_, now);
    current_.reset();
    return Act::Evict;
  }

 private:
  struct Deny {
    BleAddr addr;
    uint32_t until = 0;
    bool used = false, logged = false;
  };

  void deny(const BleAddr& a, uint32_t now) {
    Deny* slot = &deny_[0];
    for (Deny& d : deny_) {
      if (d.used && d.addr == a) { slot = &d; break; }
      if (!d.used || int32_t(d.until - slot->until) < 0) slot = &d;   // a free slot, or the soonest to lapse
    }
    *slot = Deny{a, now + kDenyMs, true, false};
  }

  std::array<Deny, 4> deny_{};
  std::optional<BleAddr> current_;
  uint32_t since_ = 0;
  bool spoke_ = false;
};

}  // namespace blorb
