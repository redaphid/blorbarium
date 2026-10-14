#pragma once
// The duel radio, faked over a byte stream: two simulators joined by a
// socketpair (tools/duel_film.py makes it) hear each other's ESP-NOW frames,
// so a duel can be filmed with no board.
//
// Two sims each run their own fixed clock, so the stream carries time as well
// as frames. Before a step at `now` a sim waits until its friend has said it
// is past `now - latency`: nothing the friend has still to send can then
// arrive by `now`. Every frame lands on the same step of every run, so a
// filmed duel is the same bytes twice however fast either process goes, and
// with a latency of at least 1 ms the two can never both be waiting.
//
// The weather (loss, doubles, reordering, lateness) is drawn by the receiver
// from a seed, so a film can show the protocol shrugging off a bad room and
// still replay exactly.
#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <optional>
#include <vector>

#include "blorb/air.h"

namespace simair {

using blorb::Datagram;
using blorb::PeerAddr;
using blorb::kDatagramMax;

inline const PeerAddr kBroadcast{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

struct Weather {
  uint32_t lossPct = 0, dupPct = 0, reorderPct = 0;
  uint32_t latencyMs = 3;   // an ESP-NOW unicast and its ack take a few ms
  uint32_t lateMs = 400;    // a reordered frame is held back up to this much longer
  int8_t rssi = -45;        // what every frame is heard at: a pair on one table
  uint32_t seed = 1;
};

// "loss=30,dup=10,reorder=20,late=800,latency=3,rssi=-70,seed=7", any subset.
inline const char* parseWeather(const char* spec, Weather& w) {
  char key[16];
  long v;
  int used;
  for (const char* p = spec; *p;) {
    if (sscanf(p, "%15[a-z]=%ld%n", key, &v, &used) != 2) return "want key=number[,key=number...]";
    if (!strcmp(key, "loss") || !strcmp(key, "dup") || !strcmp(key, "reorder")) {
      if (v < 0 || v > 100) return "loss, dup and reorder are percentages";
      (key[0] == 'l' ? w.lossPct : key[0] == 'd' ? w.dupPct : w.reorderPct) = uint32_t(v);
    } else if (!strcmp(key, "latency")) {
      if (v < 1) return "latency is at least 1 ms, or the two clocks can wait on each other forever";
      w.latencyMs = uint32_t(v);
    } else if (!strcmp(key, "late")) {
      if (v < 1) return "late is at least 1 ms";
      w.lateMs = uint32_t(v);
    } else if (!strcmp(key, "rssi")) {
      if (v < -127 || v > 0) return "rssi is dBm, -127 to 0";
      w.rssi = int8_t(v);
    } else if (!strcmp(key, "seed")) {
      w.seed = uint32_t(v);
    } else {
      return "keys are loss, dup, reorder, late, latency, rssi, seed";
    }
    p += used;
    if (*p == ',') p++;
  }
  return nullptr;
}

// What happened to a frame, for a log or a film: Lost is the weather's doing,
// Deaf is a frame that arrived while this radio was closed.
enum class Heard : uint8_t { Sent, Received, Lost, Deaf };

struct Event {
  Heard what;
  uint32_t at;   // the script's clock
  uint32_t seq;  // the sender's count, so a film can pair a send with its arrivals
  PeerAddr peer;
  const uint8_t* bytes;
  size_t len;
};

class SerialAir final : public blorb::Air {
 public:
  SerialAir(int fd, PeerAddr self, Weather w) : fd_(fd), self_(self), w_(w) {
    // A friend that has finished its film closes the stream; the next write
    // must fail quietly, not end this process too.
    signal(SIGPIPE, SIG_IGN);
    rng_ = (uint64_t(w.seed) << 32) ^ 0x9e3779b97f4a7c15ull;
    for (uint8_t b : self.mac) rng_ = (rng_ ^ b) * 0x100000001b3ull;
  }

  std::function<void(const Event&)> onEvent;

  // Before the step at `now`: tells the friend this side is past now - 1,
  // waits until the friend is past now - latency, then hands over what has
  // arrived by now.
  void begin(uint32_t now) {
    now_ = now;
    if (now > 0) {
      uint8_t rec[5] = {'T'};
      put32(rec + 1, now - 1);
      writeAll(rec, sizeof(rec));
    }
    while (!gone_ && peerThrough_ + int64_t(w_.latencyMs) < int64_t(now)) readSome();
    while (!flight_.empty() && flight_.front().arrive <= now) {
      const Flight f = flight_.front();
      flight_.pop_front();
      if (open_ && !f.lost) inbox_.push_back(f.d);
      tell(f.lost ? Heard::Lost : open_ ? Heard::Received : Heard::Deaf, f.arrive, f.seq, f.d.from, f.d.bytes, f.d.len);
    }
  }

  bool friendGone() const { return gone_; }

  bool open() override {
    open_ = true;
    return true;
  }
  void close() override {
    open_ = false;
    inbox_.clear();
  }
  bool isOpen() const override { return open_; }
  PeerAddr self() const override { return self_; }
  void broadcast(const uint8_t* p, size_t n) override { send(kBroadcast, p, n); }

  void send(const PeerAddr& to, const uint8_t* p, size_t n) override {
    if (!open_ || n > kDatagramMax) return;
    uint8_t rec[kHead + kDatagramMax];
    rec[0] = 'D';
    put32(rec + 1, now_);
    put32(rec + 5, ++sent_);
    memcpy(rec + 9, self_.mac, 6);
    memcpy(rec + 15, to.mac, 6);
    rec[21] = uint8_t(n);
    memcpy(rec + kHead, p, n);
    writeAll(rec, kHead + n);
    tell(Heard::Sent, now_, sent_, to, p, n);
  }

  std::optional<Datagram> receive() override {
    if (!open_ || inbox_.empty()) return std::nullopt;
    Datagram d = inbox_.front();
    inbox_.pop_front();
    return d;
  }

 private:
  // 'D', at, seq, from, to, len, then the bytes
  static constexpr size_t kHead = 1 + 4 + 4 + 6 + 6 + 1;

  struct Flight {
    uint32_t arrive;
    uint32_t seq;
    Datagram d;
    bool lost;   // flies anyway, so it is told when it would have landed, not when it was read
  };

  static void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i));
  }
  static uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

  uint32_t draw() {   // splitmix64
    uint64_t z = (rng_ += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return uint32_t((z ^ (z >> 31)) >> 32);
  }

  void tell(Heard what, uint32_t at, uint32_t seq, const PeerAddr& peer, const uint8_t* p, size_t n) {
    if (onEvent) onEvent(Event{what, at, seq, peer, p, n});
  }

  void writeAll(const uint8_t* p, size_t n) {
    while (n && !gone_) {
      const ssize_t k = ::write(fd_, p, n);
      if (k <= 0) { gone_ = true; return; }
      p += k;
      n -= size_t(k);
    }
  }

  void readSome() {
    pollfd pfd{fd_, POLLIN, 0};
    poll(&pfd, 1, -1);
    uint8_t buf[4096];
    const ssize_t k = ::read(fd_, buf, sizeof(buf));
    if (k <= 0) { gone_ = true; return; }
    rx_.insert(rx_.end(), buf, buf + k);
    size_t at = 0;
    while (at < rx_.size()) {
      const uint8_t* r = rx_.data() + at;
      const size_t have = rx_.size() - at;
      if (r[0] == 'T') {
        if (have < 5) break;
        peerThrough_ = get32(r + 1);
        at += 5;
      } else if (r[0] == 'D' && (have < kHead || r[21] <= kDatagramMax)) {
        if (have < kHead || have < kHead + r[21]) break;
        arrived(r);
        at += kHead + r[21];
      } else {
        fprintf(stderr, "serial air: the friend's stream is not records (byte %02x); hanging up\n", r[0]);
        gone_ = true;
        return;
      }
    }
    rx_.erase(rx_.begin(), rx_.begin() + long(at));
  }

  // One frame off the stream. Every frame draws the same five numbers,
  // whatever the weather does with them, so one knob never reshuffles
  // what the others decide.
  void arrived(const uint8_t* r) {
    PeerAddr from, to;
    memcpy(from.mac, r + 9, 6);
    memcpy(to.mac, r + 15, 6);
    if (to != self_ && to != kBroadcast) return;   // a unicast for somebody else is not heard at all
    const uint32_t sentAt = get32(r + 1), seq = get32(r + 5);
    const uint32_t lossRoll = draw() % 100, dupRoll = draw() % 100, reorderRoll = draw() % 100;
    const uint32_t late = 1 + draw() % w_.lateMs, dupGap = 1 + draw() % 20;
    Datagram d;
    d.from = from;
    d.rssi = w_.rssi;
    d.len = r[21];
    memcpy(d.bytes, r + kHead, d.len);
    const uint32_t arrive = sentAt + w_.latencyMs + (reorderRoll < w_.reorderPct ? late : 0);
    const bool lost = lossRoll < w_.lossPct;
    land({arrive, seq, d, lost});
    if (!lost && dupRoll < w_.dupPct) land({arrive + dupGap, seq, d, false});
  }

  // In arrival order; frames due on the same ms keep the order they came off the stream.
  void land(const Flight& f) {
    auto it = flight_.end();
    while (it != flight_.begin() && std::prev(it)->arrive > f.arrive) --it;
    flight_.insert(it, f);
  }

  int fd_;
  PeerAddr self_;
  Weather w_;
  uint64_t rng_;
  bool open_ = false, gone_ = false;
  uint32_t now_ = 0, sent_ = 0;
  int64_t peerThrough_ = -1;
  std::vector<uint8_t> rx_;
  std::deque<Flight> flight_;
  std::deque<Datagram> inbox_;
};

}  // namespace simair
