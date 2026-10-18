#pragma once
// A shared ether that any number of FakeAirs join (docs/design/BATTLES.md,
// testing item 4). Each datagram is delivered, dropped, doubled or held back
// on a seeded schedule, so a failing seed replays exactly. No clock of its
// own: the test advances it once a tick, before the sessions run, so a frame
// sent on tick t arrives on t + 1 at the soonest.
#include <algorithm>
#include <cstring>
#include <deque>
#include <optional>
#include <vector>
#include "blorb/air.h"
#include "blorb/fixed.h"

namespace blorbtest {

struct Weather {               // permille per datagram
  uint16_t drop = 0, dup = 0, reorder = 0;
  uint8_t maxDelayTicks = 0;   // a held datagram arrives up to this many ticks late
};

class FakeAir;

class Ether {
 public:
  Ether(uint64_t seed, Weather w) : weather(w), rng_(blorb::Rng::seeded(seed)) {}
  void advance(uint32_t tick);   // delivers what is due by `tick` to every air open now
  Weather weather;               // a test may change it mid-run (drop = 1000 cuts the link)
  uint32_t sent = 0, dropped = 0, doubled = 0, delayed = 0;

 private:
  friend class FakeAir;
  struct InFlight { uint32_t due, seq; FakeAir* to; blorb::Datagram d; };
  void post(const FakeAir& from, FakeAir* to, const uint8_t*, size_t);
  blorb::Rng rng_;
  std::vector<FakeAir*> airs_;
  std::vector<InFlight> held_;
  uint32_t tick_ = 0, seq_ = 0;
};

class FakeAir : public blorb::Air {
 public:
  FakeAir(Ether& e, blorb::PeerAddr self) : ether_(e), self_(self) { e.airs_.push_back(this); }
  ~FakeAir() override { ether_.airs_.erase(std::find(ether_.airs_.begin(), ether_.airs_.end(), this)); }
  FakeAir(const FakeAir&) = delete;
  FakeAir& operator=(const FakeAir&) = delete;

  bool open() override {   // refusable: refuseOpen stands for a running OTA or an image on trial
    if (refuseOpen) return false;
    open_ = true;
    ++opens;
    return true;
  }
  void close() override {
    open_ = false;
    inbox_.clear();
  }
  bool isOpen() const override { return open_; }
  blorb::PeerAddr self() const override { return self_; }
  void broadcast(const uint8_t* p, size_t n) override {
    if (!open_) return;
    ++framesSent;
    for (FakeAir* a : ether_.airs_)
      if (a != this) ether_.post(*this, a, p, n);
  }
  void send(const blorb::PeerAddr& to, const uint8_t* p, size_t n) override {
    if (!open_ || unicastLost) return;
    ++framesSent;
    for (FakeAir* a : ether_.airs_)
      if (a != this && a->self_ == to) ether_.post(*this, a, p, n);
  }
  std::optional<blorb::Datagram> receive() override {   // nothing while closed: the consent test reads this
    if (!open_ || inbox_.empty()) return std::nullopt;
    blorb::Datagram d = inbox_.front();
    inbox_.pop_front();
    return d;
  }

  // A frame recorded earlier, heard again now (a late packet from another bout).
  void inject(const blorb::Datagram& d) {
    if (open_) inbox_.push_back(d);
  }

  bool refuseOpen = false;
  uint16_t muted = 0;         // permille of my frames lost on top of the weather: one direction of a link
  bool deaf = false;          // I hear nobody, but everyone hears me: the other direction, from every badge at once
  bool unicastLost = false;   // my beacons arrive, my battle frames never do: a phantom who claims and never greets
  int8_t heardAt = -40;       // the dBm every other device hears me at: a test moves me away
  uint32_t framesSent = 0, opens = 0;   // the consent test asserts 0 for a device that never opened

 private:
  friend class Ether;
  Ether& ether_;
  blorb::PeerAddr self_;
  std::deque<blorb::Datagram> inbox_;
  bool open_ = false;
};

inline void Ether::post(const FakeAir& from, FakeAir* to, const uint8_t* p, size_t n) {
  ++sent;
  const bool lost = rng_.below(1000) < weather.drop;
  if (lost || rng_.below(1000) < from.muted) {
    ++dropped;
    return;
  }
  const uint8_t copies = rng_.below(1000) < weather.dup ? 2 : 1;
  doubled += copies - 1u;
  for (uint8_t c = 0; c < copies; ++c) {
    uint32_t late = 0;
    if (weather.maxDelayTicks && rng_.below(1000) < weather.reorder) {
      late = 1 + rng_.below(weather.maxDelayTicks);
      ++delayed;
    }
    InFlight f{tick_ + 1 + late, seq_++, to, {}};
    f.d.from = from.self_;
    f.d.rssi = from.heardAt;
    f.d.len = uint8_t(n);
    std::memcpy(f.d.bytes, p, n);
    held_.push_back(f);
  }
}

inline void Ether::advance(uint32_t tick) {
  tick_ = tick;
  std::stable_sort(held_.begin(), held_.end(),
                   [](const InFlight& a, const InFlight& b) { return a.due != b.due ? a.due < b.due : a.seq < b.seq; });
  size_t i = 0;
  for (; i < held_.size() && held_[i].due <= tick; ++i)
    if (held_[i].to->open_ && !held_[i].to->deaf) held_[i].to->inbox_.push_back(held_[i].d);   // a closed radio hears nothing
  held_.erase(held_.begin(), held_.begin() + long(i));
}

}  // namespace blorbtest
