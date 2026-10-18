// A bad room for the duel radio, on demand over the cable (`DEBUG net ...`).
// Sits between RadioGate and the real ESP-NOW Air, so the session, the wire
// and the radio are all the shipped ones and only the room is made worse:
//
//   rx drop %   - a heard frame is thrown away (both directions, if set on both badges)
//   tx drop %   - a frame is never handed to the radio
//   dup %       - a heard frame is delivered twice
//   delay ms + jitter ms - a heard frame waits that long (jitter reorders them)
//   blackout ms - nothing in or out for that long: out of range, a hand over the antenna
//
// Counters say what was done, so a soak log proves the faults were live.
// Pure: no Arduino; the clock and the dice are handed in.
#pragma once

#include <cstdint>
#include <cstring>
#include <optional>

#include "blorb/air.h"

namespace hw {

class FaultAir : public blorb::Air {
 public:
  struct Config {
    uint8_t rxDrop = 0, txDrop = 0, dup = 0;   // percent
    uint16_t delayMs = 0, jitterMs = 0;
    uint32_t blackoutUntil = 0;                // ms; 0 = none
  };
  struct Counts {
    uint32_t rx = 0, rxDropped = 0, tx = 0, txDropped = 0, duped = 0, delayed = 0, blackedOut = 0, overflow = 0;
  };

  FaultAir(blorb::Air& inner, uint32_t (*clock)(), uint32_t (*dice)()) : inner_(inner), clock_(clock), dice_(dice) {}

  Config cfg;
  Counts n;
  // Every badge heard since boot (or the last clear), so a soak log shows who else is on the air.
  struct Heard { blorb::PeerAddr from; uint32_t frames = 0; int8_t rssi = 0; };
  static constexpr uint8_t kHeardMax = 8;
  Heard heard[kHeardMax];
  uint8_t heardCount = 0;

  void clear() {
    cfg = Config{};
    for (Held& h : held_) h.used = false;
  }
  bool active() const {
    return cfg.rxDrop || cfg.txDrop || cfg.dup || cfg.delayMs || cfg.jitterMs || blackedOut();
  }
  bool blackedOut() const { return cfg.blackoutUntil && int32_t(cfg.blackoutUntil - clock_()) > 0; }

  bool open() override { return inner_.open(); }
  void close() override {
    for (Held& h : held_) h.used = false;
    inner_.close();
  }
  bool isOpen() const override { return inner_.isOpen(); }
  blorb::PeerAddr self() const override { return inner_.self(); }
  const char* refusal() const override { return inner_.refusal(); }

  void broadcast(const uint8_t* p, size_t len) override {
    if (dropTx()) return;
    inner_.broadcast(p, len);
  }
  void send(const blorb::PeerAddr& to, const uint8_t* p, size_t len) override {
    if (dropTx()) return;
    inner_.send(to, p, len);
  }

  std::optional<blorb::Datagram> receive() override {
    // Take everything the radio has into the held set, applying the faults on the way in.
    while (std::optional<blorb::Datagram> d = inner_.receive()) {
      ++n.rx;
      tally(*d);
      if (blackedOut()) { ++n.blackedOut; continue; }
      if (roll(cfg.rxDrop)) { ++n.rxDropped; continue; }
      const uint8_t copies = roll(cfg.dup) ? 2 : 1;
      if (copies == 2) ++n.duped;
      for (uint8_t c = 0; c < copies; ++c) hold(*d);
    }
    // Hand out the earliest one that is due.
    const uint32_t now = clock_();
    Held* best = nullptr;
    for (Held& h : held_)
      if (h.used && int32_t(now - h.due) >= 0 && (!best || int32_t(h.due - best->due) < 0)) best = &h;
    if (!best) return std::nullopt;
    best->used = false;
    return best->d;
  }

 private:
  struct Held { blorb::Datagram d; uint32_t due = 0; bool used = false; };
  static constexpr uint8_t kHeld = 24;

  blorb::Air& inner_;
  uint32_t (*clock_)();
  uint32_t (*dice_)();
  Held held_[kHeld];

  void tally(const blorb::Datagram& d) {
    for (uint8_t i = 0; i < heardCount; ++i)
      if (heard[i].from == d.from) { ++heard[i].frames, heard[i].rssi = d.rssi; return; }
    if (heardCount < kHeardMax) heard[heardCount++] = Heard{d.from, 1, d.rssi};
  }
  bool roll(uint8_t pct) { return pct && dice_() % 100 < pct; }
  bool dropTx() {
    ++n.tx;
    if (blackedOut()) { ++n.blackedOut; return true; }
    if (roll(cfg.txDrop)) { ++n.txDropped; return true; }
    return false;
  }
  void hold(const blorb::Datagram& d) {
    uint32_t wait = cfg.delayMs + (cfg.jitterMs ? dice_() % (uint32_t(cfg.jitterMs) + 1) : 0);
    if (wait) ++n.delayed;
    Held* slot = nullptr;
    for (Held& h : held_)
      if (!h.used) { slot = &h; break; }
    if (!slot) {   // full: the oldest goes, as a real radio queue overflows
      ++n.overflow;
      slot = &held_[0];
      for (Held& h : held_)
        if (int32_t(h.due - slot->due) < 0) slot = &h;
    }
    slot->d = d;
    slot->due = clock_() + wait;
    slot->used = true;
  }
};

}  // namespace hw
