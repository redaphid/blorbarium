// A bad room for the duel radio, on demand over the cable (`DEBUG net ...`).
// Sits between RadioGate and the real ESP-NOW Air, so the session, the wire
// and the radio are all the shipped ones and only the room is made worse:
//
//   rx drop %   - a heard frame is thrown away (both directions, if set on both badges)
//   tx drop %   - a frame is never handed to the radio
//   dup %       - a heard frame is delivered twice
//   delay ms + jitter ms - a heard frame waits that long (jitter reorders them)
//   blackout ms - nothing in or out for that long: out of range, a hand over the antenna
//   ghosts n mode - n made-up badges in the room, each beaconing a copy of a real friend's
//                 beacon under its own MAC, nonce and seq: mode 0 searches but never hears
//                 anyone (a one-way link), 1 claims me and never sends a State (a phantom
//                 lock), 2 is already paired with another ghost. `low` MACs sort below
//                 every real badge, `high` above.
//
// Counters say what was done, so a soak log proves the faults were live.
// Pure: no Arduino; the clock and the dice are handed in.
#pragma once

#include <cstdint>
#include <cstring>
#include <optional>

#include "blorb/air.h"
#include "blorb/duel_wire.h"

namespace hw {

class FaultAir : public blorb::Air {
 public:
  struct Config {
    uint8_t rxDrop = 0, txDrop = 0, dup = 0;   // percent
    uint16_t delayMs = 0, jitterMs = 0;
    uint32_t blackoutUntil = 0;                // ms; 0 = none
    bool deaf = false;                         // blacked out until lifted
    uint8_t ghosts = 0, ghostMode = 0;
    bool ghostsLow = true;
  };
  struct Counts {
    uint32_t rx = 0, rxDropped = 0, tx = 0, txDropped = 0, duped = 0, delayed = 0, blackedOut = 0, overflow = 0,
             ghostBeacons = 0;
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
    return cfg.ghosts || cfg.rxDrop || cfg.txDrop || cfg.dup || cfg.delayMs || cfg.jitterMs || blackedOut();
  }
  bool blackedOut() const { return cfg.deaf || (cfg.blackoutUntil && int32_t(cfg.blackoutUntil - clock_()) > 0); }

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
      learnBeacon(*d);
      if (blackedOut()) { ++n.blackedOut; continue; }
      if (roll(cfg.rxDrop)) { ++n.rxDropped; continue; }
      const uint8_t copies = roll(cfg.dup) ? 2 : 1;
      if (copies == 2) ++n.duped;
      for (uint8_t c = 0; c < copies; ++c) hold(*d);
    }
    const uint32_t now = clock_();
    haunt(now);
    // Hand out the earliest one that is due.
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

  std::optional<blorb::BeaconPacket> template_;
  uint32_t lastGhostMs_ = 0;
  uint16_t ghostSeq_ = 0;

  blorb::PeerAddr ghostAddr(uint8_t i) const {
    blorb::PeerAddr a;
    const uint8_t mac[6] = {0xd4, 0x05, 0x92, uint8_t(cfg.ghostsLow ? 0x00 : 0xfe), 0x60, uint8_t(0x10 + i)};
    std::memcpy(a.mac, mac, 6);
    return a;
  }
  void learnBeacon(const blorb::Datagram& d) {
    if (!cfg.ghosts) return;
    const std::optional<blorb::Packet> p = blorb::decode(d.bytes, d.len);
    if (const blorb::BeaconPacket* b = p ? std::get_if<blorb::BeaconPacket>(&*p) : nullptr) template_ = *b;
  }
  // Each ghost beacons about every 300 ms, as a real lobby does.
  void haunt(uint32_t now) {
    if (!cfg.ghosts || !template_ || !inner_.isOpen() || blackedOut() || now - lastGhostMs_ < 300) return;
    lastGhostMs_ = now;
    ++ghostSeq_;
    for (uint8_t i = 0; i < cfg.ghosts; ++i) {
      blorb::BeaconPacket b = *template_;
      b.nonce = template_->nonce ^ (0x9e3779b9u * (i + 1));
      b.seq = ghostSeq_;
      b.decline = blorb::Decline::None;
      b.wants = cfg.ghostMode == 1 ? inner_.self() : cfg.ghostMode == 2 ? ghostAddr(uint8_t(i ^ 1)) : blorb::PeerAddr{};
      blorb::Datagram d;
      d.from = ghostAddr(i);
      d.rssi = -60;
      d.len = uint8_t(blorb::encode(blorb::Packet{b}, d.bytes));
      ++n.ghostBeacons;
      hold(d);
    }
  }
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
