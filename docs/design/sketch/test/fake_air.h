#pragma once
// SKETCH (docs/design/BATTLES.md) of test/support/fake_air.h. A shared "ether" that any
// number of FakeAirs join. Each datagram is delivered, dropped, doubled or
// held back by a seeded schedule, so a failing seed replays exactly. No
// clock of its own: the test advances it with the dish tick.
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>
#include "blorb/air.h"
#include "blorb/fixed.h"

namespace blorbtest {

struct Weather {               // permille per datagram
  uint16_t drop = 0, dup = 0, reorder = 0;
  uint8_t maxDelayTicks = 0;   // a held datagram arrives up to this many ticks late
  int8_t rssi = -40;           // what every datagram reads as: near by default; a test moves a device away
};

class Ether {
 public:
  Ether(uint64_t seed, Weather w) : rng_(blorb::Rng::seeded(seed)), weather_(w) {}
  void advance(uint32_t tick);   // releases held datagrams due by `tick`
  struct InFlight { blorb::PeerAddr from, to; bool broadcast; uint32_t dueTick; blorb::Datagram d; };
 private:
  friend class FakeAir;
  blorb::Rng rng_;
  Weather weather_;
  std::vector<InFlight> held_;
  uint32_t tick_ = 0;
  uint32_t sent_ = 0, dropped_ = 0, doubled_ = 0;
};

class FakeAir : public blorb::Air {
 public:
  FakeAir(Ether&, blorb::PeerAddr self);
  bool open() override;              // refusable: refuseOpen simulates a running OTA
  void close() override;
  bool isOpen() const override { return open_; }
  blorb::PeerAddr self() const override { return self_; }
  void broadcast(const uint8_t*, size_t) override;
  void send(const blorb::PeerAddr&, const uint8_t*, size_t) override;
  std::optional<blorb::Datagram> receive() override;   // nothing while closed: the consent test reads this
  bool refuseOpen = false;
  uint32_t framesSent = 0;           // the consent test asserts 0 for a device that never opened
 private:
  Ether& ether_;
  blorb::PeerAddr self_;
  std::deque<blorb::Datagram> inbox_;
  bool open_ = false;
};

}  // namespace blorbtest
