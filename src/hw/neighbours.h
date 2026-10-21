// Neighbours (docs/design/NEIGHBOURS.md, issue #33): a pet knows a friend's
// badge is near from the friend's Bluetooth advertisement, which every badge
// already sends while it lives. Nothing new is transmitted to sense a friend:
// a short passive scan now and then listens, and this file decides what the
// sightings mean.
//
// PROTOTYPE. Only test/test_neighbours includes it; main.cpp does not, and
// nothing in lib/blorb knows it exists. Pure, like power.h: no Arduino, no
// NimBLE, no clock. The board glue (phase N1 in the design) feeds it
// sightings from a NimBLE passive scan and does what it says.
//
// Four pieces, each small:
//   Advert     the 8 bytes of manufacturer data a badge adds to its advert
//   Proximity  sightings -> a band per friend (heard, near, together) and events
//   scanEveryMs how often to listen, by power state
//   Ledger     the rarity rules: when a pet may wave, comment or get a gift,
//              decided the same way on both badges, once per (giver, epoch)
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace neighbours {

// A Bluetooth device address, as the scan reports it (the badges' public
// address: the MAC in badges.def plus one, per tools/ble_ota.py).
struct Addr {
  uint8_t b[6]{};
  friend bool operator==(const Addr& x, const Addr& y) { return std::memcmp(x.b, y.b, 6) == 0; }
  friend bool operator!=(const Addr& x, const Addr& y) { return !(x == y); }
  bool none() const { return *this == Addr{}; }
};

// FNV-1a, folded as blorb's fnv1a is: the same bytes give the same number on
// every badge and on the host, which is all the rolls below need.
inline uint32_t fnv(const void* p, size_t n, uint32_t h = 0x811c9dc5u) {
  const auto* s = static_cast<const uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) h = (h ^ s[i]) * 0x01000193u;
  return h;
}

// ---------------------------------------------------------------- the advert
//
// The advertisement holds the flags (3 B) and the NUS service UUID (18 B):
// 21 of its 31 bytes (docs/ble.md). A manufacturer-specific AD structure is a
// length byte, a type byte (0xFF) and its data, so 8 bytes of data fit the 10
// left, exactly. The name stays in the scan response, which a passive scan
// never asks for: the species rides here instead.
//
//   [company u16 LE = 0xFFFF][0xB0 | version][species][flags][epoch][thanks u16 LE]
//
// 0xFFFF is the Bluetooth SIG's company id for tests and internal use: no
// company owns it, so a family's toys may use it. The marker's high nibble
// (0xB) tells our bytes from anyone else's 0xFFFF data.
//
// Compatibility: bytes 2..7 keep these meanings in every later version. A
// newer version may only use the flag bits marked reserved, or bytes past the
// eighth (in a scan response or an extended advert); decode() takes any
// version >= 1 and ignores what it does not know. A badge on older firmware
// sends no manufacturer data and never scans: it is still recognised by the
// NUS UUID and its address (badges.def), but it gives and thanks for nothing.
inline constexpr uint16_t kCompany = 0xFFFF;
inline constexpr uint8_t kMarker = 0xB0;
inline constexpr uint8_t kVersion = 1;
inline constexpr size_t kAdvertBytes = 8;
inline constexpr size_t kAdvertRoom = 31 - (3 + 18) - 2;   // the advert's 31, less flags, the UUID and the AD header
static_assert(kAdvertBytes <= kAdvertRoom, "the manufacturer data must fit beside the flags and the NUS UUID");

namespace flag {
inline constexpr uint8_t hatched = 1 << 0;   // a creature, not an egg or a clutch
inline constexpr uint8_t asleep = 1 << 1;    // the pet sleeps (a friend whispers, never wakes him)
inline constexpr uint8_t low = 1 << 2;       // the badge's screen is dark (low power)
inline constexpr uint8_t dueling = 1 << 3;   // fight mode or a duel: leave him be
inline constexpr uint8_t known = 0x0F;       // bits 4..7 reserved: kept, never read
}  // namespace flag

struct Advert {
  uint8_t version = kVersion;
  uint8_t species = 0;   // blorb::SpeciesId's value; the reader checks it against its own roster
  uint8_t flags = 0;
  uint8_t epoch = 0;     // the sender's gift epoch: its pet ticks / kEpochTicks, mod 256
  uint16_t thanks = 0;   // thanksTag(giver, giver's epoch) of the last gift it got; 0 none
  bool has(uint8_t f) const { return (flags & f) != 0; }
};

inline size_t encode(const Advert& a, uint8_t (&out)[kAdvertBytes]) {
  out[0] = uint8_t(kCompany & 0xFF);
  out[1] = uint8_t(kCompany >> 8);
  out[2] = uint8_t(kMarker | (a.version & 0x0F));
  out[3] = a.species;
  out[4] = a.flags;
  out[5] = a.epoch;
  out[6] = uint8_t(a.thanks & 0xFF);
  out[7] = uint8_t(a.thanks >> 8);
  return kAdvertBytes;
}

// The boundary parse of a scan's manufacturer data. Refuses: fewer than 8
// bytes, another company's id, a marker that is not ours, version 0. Takes a
// newer version and anything past the eighth byte, unread.
inline std::optional<Advert> decode(const uint8_t* p, size_t n) {
  if (!p || n < kAdvertBytes) return std::nullopt;
  if (uint16_t(p[0] | (p[1] << 8)) != kCompany) return std::nullopt;
  if ((p[2] & 0xF0) != kMarker || (p[2] & 0x0F) == 0) return std::nullopt;
  Advert a;
  a.version = uint8_t(p[2] & 0x0F);
  a.species = p[3];
  a.flags = p[4];
  a.epoch = p[5];
  a.thanks = uint16_t(p[6] | (p[7] << 8));
  return a;
}

// "I got your gift of this epoch": what a receiver advertises so the giver
// knows it landed. Anyone can compute it; it says only that someone thanked
// this address, and it changes every epoch. Never 0, which means none.
inline uint16_t thanksTag(const Addr& giver, uint8_t giverEpoch) {
  uint32_t h = fnv(giver.b, 6);
  h = fnv(&giverEpoch, 1, h);
  const uint16_t t = uint16_t(h ^ (h >> 16));
  return t ? t : 1;
}

// ------------------------------------------------------------- proximity
//
// RSSI is a poor ruler: a hand, a body or the case moves it 10-20 dB. So a
// band changes only on the median of the last three rounds' loudest reading,
// with hysteresis, and only after it held for a few rounds. A round is one
// scan window (kWindowMs), long enough to hear every advertiser in range once.
// The thresholds are estimates for +20 dBm advertisers; phase N1 measures them
// (tools/ble_ota.py scan prints RSSI) at 30 cm, 1 m, 3 m and the next room.

enum class Band : uint8_t { Heard, Near, Together };   // Heard: in range, not close; a friend gone is no entry

struct Thresholds {
  int8_t near = -70;               // the same room, about
  int8_t together = -50;           // side by side, in two kids' hands
  uint8_t hysteresisDb = 6;        // a band is left only this far below its threshold
  uint8_t arriveRounds = 2;        // rounds at or above `near` before Arrived: a walk-past is not a visit
  uint8_t togetherRounds = 3;      // rounds at or above `together` before Together
  uint8_t lostRounds = 3;          // rounds unheard before a friend is gone
};

struct Event {
  enum class Kind : uint8_t { Arrived, Together, Apart, Left };
  Kind kind;
  Addr who;
  uint8_t species;
};

struct Peer {
  Addr who{};
  uint8_t species = 0;
  std::optional<Advert> advert;   // the newest; none from a badge on older firmware
  Band band = Band::Heard;
  int8_t loudest[3] = {-127, -127, -127};   // per round, newest first
  uint8_t rounds = 0;             // rounds heard, capped at 255
  int8_t thisRound = -127;        // loudest in the round under way; -127 unheard
  uint8_t missed = 0;             // rounds in a row unheard
  uint8_t aboveNear = 0, aboveTogether = 0;   // rounds in a row at or above each threshold
  uint32_t togetherSinceMs = 0;   // when it became Together
  int8_t level() const {          // the median of the last three rounds heard (fewer at first)
    const uint8_t k = rounds < 3 ? rounds : 3;
    if (k == 0) return -127;
    if (k == 1) return loudest[0];
    if (k == 2) return loudest[0] < loudest[1] ? loudest[0] : loudest[1];   // two: the quieter, to be safe
    int8_t a = loudest[0], b = loudest[1], c = loudest[2];
    if (a > b) { const int8_t t = a; a = b; b = t; }
    if (b > c) { const int8_t t = b; b = c; c = t; }
    if (a > b) { const int8_t t = a; a = b; b = t; }
    return b;
  }
};

class Proximity {
 public:
  static constexpr uint8_t kMaxPeers = 12;   // eight badges and the gift, with room for strangers' ghosts
  static constexpr uint8_t kMaxEvents = 16;

  explicit Proximity(Thresholds t = {}) : t_(t) {}

  // One advertisement heard in the round under way. `speciesGuess` is the
  // reader's guess for a badge with no advert (its badges.def row); an advert's
  // own species wins. A sighting never wakes the badge or counts as a person.
  void see(const Addr& who, int8_t rssi, const std::optional<Advert>& ad, uint8_t speciesGuess) {
    Peer* p = slot(who);
    if (!p) p = admit(who);
    if (!p) return;   // full of friends who are near: a stranger waits
    p->species = ad ? ad->species : (p->advert ? p->advert->species : speciesGuess);
    if (ad) p->advert = ad;
    if (rssi > p->thisRound) p->thisRound = rssi;
  }

  // The scan window closed: each friend's level moves and the bands follow.
  // Events queue in table order, so two runs over the same sightings agree.
  void endRound(uint32_t nowMs) {
    for (uint8_t i = 0; i < count_;) {
      Peer& p = peers_[i];
      if (p.thisRound == -127) {
        p.aboveNear = p.aboveTogether = 0;
        if (++p.missed >= t_.lostRounds) {
          if (p.band != Band::Heard) push(Event::Kind::Left, p);
          drop(i);
          continue;
        }
        ++i;
        continue;
      }
      p.missed = 0;
      p.loudest[2] = p.loudest[1], p.loudest[1] = p.loudest[0], p.loudest[0] = p.thisRound;
      p.thisRound = -127;
      if (p.rounds < 255) ++p.rounds;
      const int8_t lv = p.level();
      p.aboveNear = lv >= t_.near ? uint8_t(p.aboveNear < 255 ? p.aboveNear + 1 : 255) : 0;
      p.aboveTogether = lv >= t_.together ? uint8_t(p.aboveTogether < 255 ? p.aboveTogether + 1 : 255) : 0;
      switch (p.band) {
        case Band::Heard:
          if (p.aboveNear >= t_.arriveRounds) p.band = Band::Near, push(Event::Kind::Arrived, p);
          break;
        case Band::Near:
          if (lv < t_.near - t_.hysteresisDb) p.band = Band::Heard, push(Event::Kind::Left, p);
          break;
        case Band::Together:
          if (lv < t_.near - t_.hysteresisDb) p.band = Band::Heard, push(Event::Kind::Left, p);
          else if (lv < t_.together - t_.hysteresisDb) p.band = Band::Near, push(Event::Kind::Apart, p);
          break;
      }
      if (p.band == Band::Near && p.aboveTogether >= t_.togetherRounds)
        p.band = Band::Together, p.togetherSinceMs = nowMs, push(Event::Kind::Together, p);
      ++i;
    }
  }

  std::optional<Event> next() {
    if (head_ == tail_) return std::nullopt;
    return events_[head_++ % kMaxEvents];
  }

  const Peer* find(const Addr& who) const {
    for (uint8_t i = 0; i < count_; ++i) if (peers_[i].who == who) return &peers_[i];
    return nullptr;
  }
  uint8_t count() const { return count_; }
  const Peer& at(uint8_t i) const { return peers_[i]; }
  uint8_t atLeast(Band b) const {   // the home screen's "friends near" count
    uint8_t n = 0;
    for (uint8_t i = 0; i < count_; ++i) n += peers_[i].band >= b;
    return n;
  }
  // How long `who` has been Together, 0 when it is not.
  uint32_t togetherMs(const Addr& who, uint32_t nowMs) const {
    const Peer* p = find(who);
    return p && p->band == Band::Together ? nowMs - p->togetherSinceMs : 0;
  }
  uint32_t eventsLost() const { return lost_; }

 private:
  Peer* slot(const Addr& who) {
    for (uint8_t i = 0; i < count_; ++i) if (peers_[i].who == who) return &peers_[i];
    return nullptr;
  }
  // A new entry, or in a full table the quietest one only Heard. Friends
  // near are never pushed out by a stranger far away.
  Peer* admit(const Addr& who) {
    if (count_ == kMaxPeers) {
      int8_t worst = 127;
      int victim = -1;
      for (uint8_t i = 0; i < count_; ++i)
        if (peers_[i].band == Band::Heard && peers_[i].level() < worst) worst = peers_[i].level(), victim = i;
      if (victim < 0) return nullptr;
      drop(uint8_t(victim));
    }
    Peer& p = peers_[count_++];
    p = Peer{};
    p.who = who;
    return &p;
  }
  void drop(uint8_t i) {
    for (uint8_t j = i; j + 1 < count_; ++j) peers_[j] = peers_[j + 1];
    --count_;
  }
  void push(Event::Kind k, const Peer& p) {
    if (tail_ - head_ == kMaxEvents) { ++lost_; return; }
    events_[tail_++ % kMaxEvents] = Event{k, p.who, p.species};
  }

  Thresholds t_;
  Peer peers_[kMaxPeers];
  uint8_t count_ = 0;
  Event events_[kMaxEvents]{};
  uint32_t head_ = 0, tail_ = 0, lost_ = 0;
};

// -------------------------------------------------------------- when to listen
//
// One window long enough to hear every badge in range once: the advertising
// interval is 100-150 ms (link_nus.h, 160-240 units of 0.625 ms) plus up to
// 10 ms of the advertiser's own random delay. Never while the air is the
// duel's (ESP-NOW and BLE share one antenna; the lobby already hears
// friends), during an update, on an image still on trial, or in the off state.
inline constexpr uint32_t kWindowMs = 200;

enum class Power : uint8_t { Awake, Low, Off };

inline constexpr uint32_t kAwakeEveryMs = 20000;   // awake, nobody near: 1% of the time listening
inline constexpr uint32_t kSeekEveryMs = 5000;     // awake, someone near: answers within seconds
inline constexpr uint32_t kLowEveryMs = 120000;    // screen dark: 0.17%, a visit is still remembered

// Milliseconds between scan starts; 0 means do not scan.
inline uint32_t scanEveryMs(Power pw, bool anyoneNear, bool blocked) {
  if (blocked || pw == Power::Off) return 0;
  if (pw == Power::Low) return kLowEveryMs;
  return anyoneNear ? kSeekEveryMs : kAwakeEveryMs;
}
// The receiver's share of the time, in parts per 100,000, for the power budget.
inline uint32_t listeningPer100k(Power pw, bool anyoneNear) {
  const uint32_t every = scanEveryMs(pw, anyoneNear, false);
  return every ? kWindowMs * 100000u / every : 0;
}

// ------------------------------------------------------------------ rarity
//
// Time here is the pet's own: dish ticks at 10 a second, saved with him and
// never reset by a reboot, so turning a badge off and on farms nothing. The
// ledger is plain data: the friends file (phase N2) saves it whole, and a
// copy decides exactly as the original does.

inline constexpr uint32_t kTicksPerSecond = 10;
inline constexpr uint32_t kTicksPerMinute = 60 * kTicksPerSecond;
inline constexpr uint32_t kTicksPerHour = 60 * kTicksPerMinute;
inline constexpr uint32_t kEpochTicks = 20 * kTicksPerHour;   // a gift epoch: about a pet day

inline uint8_t epochOf(uint32_t petTicks) { return uint8_t(petTicks / kEpochTicks); }

struct Rules {
  uint32_t noticeGapTicks = 10 * kTicksPerMinute;    // apart this long, and meeting again is a new wave
  uint32_t pairCommentGapTicks = 30 * kTicksPerMinute;
  uint32_t anyCommentGapTicks = 5 * kTicksPerMinute;
  uint32_t pairGiftGapTicks = kEpochTicks;           // from one friend, at most one gift a pet day
  uint8_t giftsPerEpoch = 3;                         // from everyone together
  uint32_t togetherForGiftMs = 60000;                // a minute side by side: a visit, not a walk-past
  uint8_t chanceIn256 = 128;                         // of a visit that may bring a gift, how many do
};

// What a friend gave: an item from the giver's species' own list
// (NEIGHBOURS.md, "Gifts"). `kind` 0 and 1 are common, 2 is the rare keepsake.
struct Gift {
  uint8_t fromSpecies;
  uint8_t kind;
  uint16_t thanks;   // what to advertise now: the giver sees it and shows the giving
};

// The roll both badges make for (giver, receiver, the giver's epoch): the
// giver from its own address, the receiver from the advert's. Same bytes, same
// answer, so no message has to agree it.
inline uint32_t giftRoll(const Addr& giver, const Addr& receiver, uint8_t giverEpoch) {
  uint32_t h = fnv(giver.b, 6);
  h = fnv(receiver.b, 6, h);
  return fnv(&giverEpoch, 1, h);
}
inline uint8_t giftKind(uint32_t roll) {
  const uint32_t r = (roll >> 8) & 7;
  return r == 0 ? 2 : uint8_t(r & 1);   // 1 in 8 rare; the rest split between the two common ones
}

struct Friend {
  Addr who{};
  uint8_t species = 0;
  uint16_t meetings = 0;
  uint32_t firstMet = 0, lastNotice = 0, lastComment = 0, lastGift = 0;   // pet ticks
  uint8_t redeemedEpoch = 0;   // the giver's epoch of the last gift taken from this friend
  uint8_t thankedEpoch = 0;    // my epoch of the last gift this friend thanked me for
  uint8_t flags = 0;           // bit 0: a gift taken; bit 1: a thanks seen; bit 2: met at least once
};

class Ledger {
 public:
  static constexpr uint8_t kMaxFriends = 16;

  explicit Ledger(Rules r = {}) : r_(r) {}

  // A friend Arrived. True when it is worth a wave: the first meeting, or
  // back after noticeGapTicks apart. Counts the meeting either way.
  bool notice(const Addr& who, uint8_t species, uint32_t tick) {
    Friend& f = remember(who, species, tick);
    const bool first = !(f.flags & 4);
    if (!first && tick - f.lastNotice < r_.noticeGapTicks) {
      f.lastNotice = tick;   // still the same visit, a fade and back
      return false;
    }
    f.flags |= 4;
    if (f.meetings < 0xFFFF) ++f.meetings;
    f.lastNotice = tick;
    return true;
  }
  bool firstMeeting(const Addr& who) const {
    const Friend* f = find(who);
    return f && f->meetings == 1;
  }

  // A line about this friend may be said now (the Thinker still picks when).
  bool mayComment(const Addr& who, uint32_t tick) const {
    const Friend* f = find(who);
    if (!f) return false;
    if (anyComment_ && tick - lastAnyComment_ < r_.anyCommentGapTicks) return false;
    return f->lastComment == 0 || tick - f->lastComment >= r_.pairCommentGapTicks;
  }
  void commented(const Addr& who, uint32_t tick) {
    if (Friend* f = slot(who)) f->lastComment = tick ? tick : 1;
    lastAnyComment_ = tick, anyComment_ = true;
  }

  // The receiver's side: `giver` has been Together for `togetherMs` and
  // advertises `ad`. A gift once per (giver, giver's epoch), at most one a pet
  // day from one friend and giftsPerEpoch from all, and only on the roll.
  // Taking it changes the ledger: save the ledger before showing the gift,
  // so a power cut between cannot give it twice.
  std::optional<Gift> receive(const Addr& me, const Addr& giver, const Advert& ad, uint32_t togetherMs, uint32_t tick) {
    Friend* f = slot(giver);
    if (!f || togetherMs < r_.togetherForGiftMs) return std::nullopt;
    if (!ad.has(flag::hatched) || ad.has(flag::dueling)) return std::nullopt;   // an egg has nothing to give
    if ((f->flags & 1) && f->redeemedEpoch == ad.epoch) return std::nullopt;   // this epoch's, already taken
    if ((f->flags & 1) && tick - f->lastGift < r_.pairGiftGapTicks) return std::nullopt;
    if (giftsInEpoch(tick) >= r_.giftsPerEpoch) return std::nullopt;
    const uint32_t roll = giftRoll(giver, me, ad.epoch);
    if ((roll & 0xFF) >= r_.chanceIn256) return std::nullopt;
    f->flags |= 1;
    f->redeemedEpoch = ad.epoch;
    f->lastGift = tick;
    recent_[recentNext_++ % 3] = tick ? tick : 1;
    return Gift{ad.species, giftKind(roll), thanksTag(giver, ad.epoch)};
  }

  // The giver's side: would `receiver` take a gift from me this epoch (the
  // same roll), and has it said so? True once per (receiver, my epoch), when
  // its advert carries my thanks: then the giving is shown. A friend on old
  // firmware never thanks, so nothing is shown as given to it.
  bool thanked(const Addr& me, uint8_t myEpoch, const Addr& receiver, const Advert& ad) {
    Friend* f = slot(receiver);
    if (!f || ad.thanks != thanksTag(me, myEpoch)) return false;
    if ((f->flags & 2) && f->thankedEpoch == myEpoch) return false;
    f->flags |= 2;
    f->thankedEpoch = myEpoch;
    return true;
  }
  // The giver's guess before any thanks: the roll says a gift is on its way
  // (for a wind-up animation). The receiver's caps may still say no.
  static bool mayGive(const Addr& me, uint8_t myEpoch, const Addr& receiver, uint8_t chanceIn256 = Rules{}.chanceIn256) {
    return (giftRoll(me, receiver, myEpoch) & 0xFF) < chanceIn256;
  }

  const Friend* find(const Addr& who) const {
    for (uint8_t i = 0; i < count_; ++i) if (friends_[i].who == who) return &friends_[i];
    return nullptr;
  }
  uint8_t count() const { return count_; }
  const Friend& at(uint8_t i) const { return friends_[i]; }
  uint8_t giftsInEpoch(uint32_t tick) const {
    uint8_t n = 0;
    for (uint32_t t : recent_) n += t != 0 && tick - t < kEpochTicks;
    return n;
  }

 private:
  Friend* slot(const Addr& who) {
    for (uint8_t i = 0; i < count_; ++i) if (friends_[i].who == who) return &friends_[i];
    return nullptr;
  }
  // The friend's record, made on first sight. A full list forgets the one
  // met least recently (the badges are nine; this is for strangers).
  Friend& remember(const Addr& who, uint8_t species, uint32_t tick) {
    if (Friend* f = slot(who)) { f->species = species; return *f; }
    if (count_ == kMaxFriends) {
      uint8_t oldest = 0;
      for (uint8_t i = 1; i < count_; ++i) if (friends_[i].lastNotice < friends_[oldest].lastNotice) oldest = i;
      friends_[oldest] = friends_[--count_];
    }
    Friend& f = friends_[count_++];
    f = Friend{};
    f.who = who, f.species = species, f.firstMet = tick;
    return f;
  }

  Rules r_;
  Friend friends_[kMaxFriends];
  uint8_t count_ = 0;
  uint32_t recent_[3]{};
  uint8_t recentNext_ = 0;
  uint32_t lastAnyComment_ = 0;
  bool anyComment_ = false;
};

}  // namespace neighbours
