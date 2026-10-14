#include <gtest/gtest.h>
#include <sys/socket.h>

#include <algorithm>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../../sim/serial_air.h"

using simair::Heard;
using simair::SerialAir;
using simair::Weather;

namespace {

const blorb::PeerAddr kMacA{{0x02, 0xb1, 0x0b, 0x00, 0x00, 0x01}};
const blorb::PeerAddr kMacB{{0x3c, 0x84, 0x27, 0xaa, 0x10, 0x5e}};

struct Seen {
  Heard what;
  uint32_t at, seq;
  std::string bytes;
};

// Two airs on one socketpair, as tools/duel_film.py joins two sims.
struct Pair {
  int fds[2];
  SerialAir* a;
  SerialAir* b;
  std::vector<Seen> seenA, seenB;
  std::vector<blorb::Datagram> got;   // everything B has read, in order
  uint32_t now = 0;

  explicit Pair(Weather wb, Weather wa = Weather{}) {
    EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    a = new SerialAir(fds[0], kMacA, wa);
    b = new SerialAir(fds[1], kMacB, wb);
    a->onEvent = [this](const simair::Event& e) { seenA.push_back(seen(e)); };
    b->onEvent = [this](const simair::Event& e) { seenB.push_back(seen(e)); };
    a->open();
    b->open();
  }
  ~Pair() {
    delete a;
    delete b;
    ::close(fds[0]);
    ::close(fds[1]);
  }
  static Seen seen(const simair::Event& e) { return {e.what, e.at, e.seq, std::string((const char*)e.bytes, e.len)}; }

  // Both sides to `until`, a millisecond a step; returns what B read on the way.
  std::vector<blorb::Datagram> runTo(uint32_t until) {
    const size_t from = got.size();
    for (; now <= until; now++) {
      a->begin(now);
      b->begin(now);
      while (auto d = b->receive()) got.push_back(*d);
    }
    return {got.begin() + long(from), got.end()};
  }

  std::vector<uint32_t> seqs(Heard what) const {
    std::vector<uint32_t> out;
    for (const Seen& s : seenB)
      if (s.what == what) out.push_back(s.seq);
    return out;
  }
};

void sendNumbered(Pair& p, int frames) {
  for (int i = 0; i < frames; i++) {
    p.runTo(p.now);   // a millisecond apart, so lateness can overtake
    const std::string s = "frame " + std::to_string(i);
    p.a->send(kMacB, (const uint8_t*)s.data(), s.size());
  }
}

Weather clear(uint32_t latency = 3) {
  Weather w;
  w.latencyMs = latency;
  return w;
}

TEST(SerialAir, FramesCrossIntactAtTheirLatency) {
  Pair p(clear(7));
  p.runTo(10);
  uint8_t full[blorb::kDatagramMax];
  for (size_t i = 0; i < sizeof(full); i++) full[i] = uint8_t(i * 37 + 1);
  p.a->broadcast((const uint8_t*)"beacon", 6);
  p.a->send(kMacB, full, sizeof(full));
  EXPECT_TRUE(p.runTo(16).empty()) << "nothing lands before sent + latency";
  const auto got = p.runTo(17);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(std::string((const char*)got[0].bytes, got[0].len), "beacon");
  ASSERT_EQ(got[1].len, sizeof(full));
  EXPECT_EQ(memcmp(got[1].bytes, full, sizeof(full)), 0);
  EXPECT_TRUE(got[1].from == kMacA);
  EXPECT_EQ(got[1].rssi, clear().rssi);
  EXPECT_EQ(p.seenB.back().at, 17u);
  EXPECT_TRUE(p.a->receive() == std::nullopt) << "a sender does not hear itself";
}

TEST(SerialAir, BothWays) {
  Pair p(clear());
  p.runTo(1);
  p.b->send(kMacA, (const uint8_t*)"pong", 4);
  p.runTo(10);
  const auto d = p.a->receive();
  ASSERT_TRUE(d.has_value());
  EXPECT_EQ(std::string((const char*)d->bytes, d->len), "pong");
  EXPECT_TRUE(d->from == kMacB);
}

TEST(SerialAir, AUnicastForSomebodyElseIsNotHeard) {
  Pair p(clear());
  const blorb::PeerAddr stranger{{1, 2, 3, 4, 5, 6}};
  p.a->send(stranger, (const uint8_t*)"x", 1);
  EXPECT_TRUE(p.runTo(50).empty());
  EXPECT_TRUE(p.seqs(Heard::Received).empty());
}

TEST(SerialAir, AClosedRadioNeitherHearsNorSends) {
  Pair p(clear());
  p.b->close();
  p.a->broadcast((const uint8_t*)"hello?", 6);
  EXPECT_TRUE(p.runTo(20).empty());
  EXPECT_EQ(p.seqs(Heard::Deaf), std::vector<uint32_t>{1});
  p.b->open();
  EXPECT_TRUE(p.runTo(40).empty()) << "opening again does not bring back what arrived while closed";

  p.a->close();
  p.a->broadcast((const uint8_t*)"x", 1);
  p.runTo(60);
  EXPECT_EQ(p.seenB.size(), 1u) << "a closed sender puts nothing on the air";
}

TEST(SerialAir, ArrivalsWaitInTheInboxUntilRead) {
  Pair p(clear());
  p.a->broadcast((const uint8_t*)"1", 1);
  p.runTo(2);
  p.a->broadcast((const uint8_t*)"2", 1);
  for (; p.now <= 30; p.now++) {   // nobody reads B on these steps
    p.a->begin(p.now);
    p.b->begin(p.now);
  }
  const auto first = p.b->receive();
  const auto second = p.b->receive();
  ASSERT_TRUE(first && second);
  EXPECT_EQ(first->bytes[0], '1');
  EXPECT_EQ(second->bytes[0], '2');
}

TEST(SerialAir, TotalLossDeliversNothing) {
  Weather w = clear();
  w.lossPct = 100;
  Pair p(w);
  sendNumbered(p, 50);
  p.runTo(p.now + 50);
  EXPECT_TRUE(p.got.empty());
  EXPECT_EQ(p.seqs(Heard::Lost).size(), 50u);
}

TEST(SerialAir, PartialLossDropsAboutThatShare) {
  Weather w = clear();
  w.lossPct = 30;
  Pair p(w);
  sendNumbered(p, 1000);
  p.runTo(p.now + 50);
  EXPECT_GT(p.got.size(), 620u);
  EXPECT_LT(p.got.size(), 780u);
  EXPECT_EQ(p.got.size() + p.seqs(Heard::Lost).size(), 1000u);
}

TEST(SerialAir, EveryFrameDoubled) {
  Weather w = clear();
  w.dupPct = 100;
  Pair p(w);
  sendNumbered(p, 40);
  p.runTo(p.now + 50);
  ASSERT_EQ(p.got.size(), 80u);
  auto seq = p.seqs(Heard::Received);
  for (uint32_t s = 1; s <= 40; s++) EXPECT_EQ(std::count(seq.begin(), seq.end(), s), 2) << "seq " << s;
}

TEST(SerialAir, ReorderingOvertakesButLosesNothing) {
  Weather w = clear();
  w.reorderPct = 30;
  w.lateMs = 60;
  Pair p(w);
  sendNumbered(p, 300);
  p.runTo(p.now + 100);
  const auto seq = p.seqs(Heard::Received);
  ASSERT_EQ(seq.size(), 300u);
  EXPECT_FALSE(std::is_sorted(seq.begin(), seq.end())) << "some frame arrived after a later one";
  EXPECT_EQ(std::set<uint32_t>(seq.begin(), seq.end()).size(), 300u);
}

std::vector<std::pair<uint32_t, uint32_t>> arrivals(uint32_t seed) {
  Weather w = clear();
  w.lossPct = 30;
  w.dupPct = 10;
  w.reorderPct = 20;
  w.seed = seed;
  Pair p(w);
  sendNumbered(p, 200);
  p.runTo(p.now + 500);
  std::vector<std::pair<uint32_t, uint32_t>> out;
  for (const Seen& s : p.seenB)
    if (s.what == Heard::Received) out.push_back({s.at, s.seq});
  return out;
}

TEST(SerialAir, TheWeatherReplaysFromItsSeed) {
  EXPECT_EQ(arrivals(7), arrivals(7));
  EXPECT_NE(arrivals(7), arrivals(8));
}

// How far ahead a side reads the stream is up to the scheduler, so an event
// told when its bytes are read would land in the log at a different place
// each run. Each one is told on the step it happens, in time order.
TEST(SerialAir, NothingIsToldBeforeItHappens) {
  Weather w = clear(4);
  w.lossPct = 40;
  w.dupPct = 20;
  w.reorderPct = 20;
  w.lateMs = 50;
  Pair p(w);
  std::vector<std::pair<uint32_t, uint32_t>> told;   // (step it was told on, its time)
  p.b->onEvent = [&](const simair::Event& e) { told.push_back({p.now, e.at}); };
  sendNumbered(p, 300);
  p.runTo(p.now + 100);
  ASSERT_GT(told.size(), 250u);
  uint32_t last = 0;
  for (const auto& [step, at] : told) {
    EXPECT_LE(at, step);
    EXPECT_GE(at, last) << "told in time order";
    last = at;
  }
}

TEST(SerialAir, AFriendThatLeavesDoesNotHangUs) {
  Pair p(clear());
  p.runTo(5);
  delete p.b;
  p.b = nullptr;
  ::close(p.fds[1]);
  p.fds[1] = -1;
  p.a->begin(10000);   // would wait forever on a friend still there
  EXPECT_TRUE(p.a->friendGone());
  p.a->broadcast((const uint8_t*)"anyone?", 7);   // no SIGPIPE
  p.b = new SerialAir(-1, kMacB, clear());         // for ~Pair
}

// Two sides on their own threads, as two processes are: at the smallest
// latency, with steps of different sizes on each side, neither waits forever
// and every frame lands exactly at sent + latency.
TEST(SerialAir, TwoClocksInLockstepAtOneMillisecond) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  SerialAir a(fds[0], kMacA, clear(1)), b(fds[1], kMacB, clear(1));
  a.open();
  b.open();
  std::vector<std::pair<uint32_t, uint32_t>> landed;   // (at, sent at)
  b.onEvent = [&](const simair::Event& e) {
    if (e.what == Heard::Received) landed.push_back({e.at, uint32_t(atoi((const char*)e.bytes))});
  };
  std::thread ta([&] {
    for (uint32_t t = 0; t <= 3000; t += 5 + t % 7) {
      a.begin(t);
      const std::string s = std::to_string(t);
      a.broadcast((const uint8_t*)s.c_str(), s.size() + 1);
    }
    shutdown(fds[0], SHUT_WR);   // done, as a sim at the end of its film exits
  });
  std::thread tb([&] {
    for (uint32_t t = 0; t <= 4000; t += 3 + t % 11) b.begin(t);
    shutdown(fds[1], SHUT_WR);
  });
  ta.join();
  tb.join();
  ASSERT_GT(landed.size(), 300u);
  for (const auto& [at, sent] : landed) {
    EXPECT_GE(at, sent + 1);
    EXPECT_LT(at, sent + 1 + 14) << "landed on B's first step at or after sent + latency";
  }
  ::close(fds[0]);
  ::close(fds[1]);
}

TEST(Weather, Parses) {
  Weather w;
  EXPECT_EQ(simair::parseWeather("loss=30,dup=10,reorder=20,late=800,latency=4,rssi=-70,seed=9", w), nullptr);
  EXPECT_EQ(w.lossPct, 30u);
  EXPECT_EQ(w.dupPct, 10u);
  EXPECT_EQ(w.reorderPct, 20u);
  EXPECT_EQ(w.lateMs, 800u);
  EXPECT_EQ(w.latencyMs, 4u);
  EXPECT_EQ(w.rssi, -70);
  EXPECT_EQ(w.seed, 9u);
  EXPECT_NE(simair::parseWeather("latency=0", w), nullptr);
  EXPECT_NE(simair::parseWeather("loss=101", w), nullptr);
  EXPECT_NE(simair::parseWeather("fog=3", w), nullptr);
  EXPECT_NE(simair::parseWeather("loss", w), nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
