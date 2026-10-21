#pragma once
// A badge's whole body path for the feeding tests, from the IMU to the dish:
// 250 Hz reads summed into 50 Hz samples with their peak (main.cpp
// bodyq::sense), the shell (src/hw/feel.h: KnockSense, the power policy, the
// turn into the screen's frame at either home), then the Dish, ticked once a
// frame as the loop does. test_feeding plays knocks through it;
// test_feeding_film films a feeding with it.
#include <cstdio>
#include <deque>
#include <optional>
#include <vector>
#include "blorb/dish.h"
#include "blorb/species.h"
#include "fake_air.h"
#include "links.h"
#include "mem_storage.h"
#include "../../src/debug_verbs.h"
#include "../../src/hw/feel.h"

namespace feedingtest {

using namespace blorb;
using blorbtest::Ether;
using blorbtest::FakeAir;
using blorbtest::MemStorage;
using blorbtest::NullLink;
using blorbtest::Weather;

constexpr uint8_t kHomeUp = 2;     // ORIENT_HOME: USB-C up (badge128)
constexpr uint8_t kHomeDown = 0;   // USB-C down (badge128_flip, the owner's badges)
constexpr int kR0 = 1;             // ORIENT_R0 on the 1.28

// How the badge lies or is held, in the IMU's raw frame (milli-g; gravity
// reads +1 g along whichever way is up). Held: on edge, upright at its home,
// tipped back a little, with a hand's tremor. Upside down: the hang.
// Tilted: held in a hand like a tray, face up, leaning one way as a kid aims
// in a duel (dish_duel.h kAimTilt: left, right, toward him, away). Side:
// turned onto a side and held there (the hold's twin), either side.
enum class Pose : uint8_t { Held, Table, UpsideDown, TiltLeft, TiltRight, TiltNear, TiltAway, Flat, SideLeft, SideRight };

struct Read { int16_t ax, ay, az; };

// The reads a badge gives at `ms` (every 4 ms awake), with knocks: a knuckle
// through the face jolts az one 20 ms step up and the next down (DEBUG knock's
// shape), or on a table a 300 mg blip in one read.
struct Hand {
  uint8_t home = kHomeUp;
  Pose pose = Pose::Held;
  std::vector<uint32_t> knocks;
  Rng rng = Rng::seeded(7);
  // turnMs > 0: a change of pose takes that long, the way a hand turns it,
  // gravity swinging round at 1 g (0: at once, as the feeding tests have it).
  uint32_t turnMs = 0;
  Read target(Pose p) const {
    const int16_t up = home == 2 ? 1000 : -1000;   // raw +y is up at home 2 (orient.h)
    // The tilts in the screen's frame at home, turned back into the IMU's
    // (orient.h toFrame with ORIENT_R0 1: home 2 is a half turn, home 0 none).
    const int16_t s = home == 2 ? -1 : 1;
    if (p == Pose::Table) return {0, 0, 1000};
    if (p == Pose::UpsideDown) return {0, int16_t(-up), 0};
    if (p == Pose::TiltLeft) return {int16_t(s * 500), 0, 866};
    if (p == Pose::TiltRight) return {int16_t(s * -500), 0, 866};
    if (p == Pose::TiltNear) return {0, int16_t(s * -500), 866};
    if (p == Pose::TiltAway) return {0, int16_t(s * 500), 866};
    if (p == Pose::Flat) return {0, 0, 1000};
    if (p == Pose::SideLeft) return {int16_t(up), 0, -60};
    if (p == Pose::SideRight) return {int16_t(-up), 0, -60};
    return {0, int16_t(up * 97 / 100), -150};
  }
  Pose was_ = Pose::Held, from_ = Pose::Held;
  uint32_t turnedAt_ = 0;
  Read at(uint32_t ms) {
    if (pose != was_) from_ = was_, was_ = pose, turnedAt_ = ms;
    Read r = target(pose);
    if (turnMs && ms - turnedAt_ < turnMs) {
      const Read a = target(from_);
      const int64_t t = int64_t(ms - turnedAt_) * 1024 / turnMs;
      const int64_t x = a.ax + (r.ax - a.ax) * t / 1024, y = a.ay + (r.ay - a.ay) * t / 1024, z = a.az + (r.az - a.az) * t / 1024;
      int64_t m = 1;
      while ((m + 1) * (m + 1) <= x * x + y * y + z * z) ++m;
      r = {int16_t(x * 1000 / m), int16_t(y * 1000 / m), int16_t(z * 1000 / m)};
    }
    const int tremor = pose == Pose::Table ? 2 : 12;
    r.ax = int16_t(r.ax + int(rng.below(2 * tremor + 1)) - tremor);
    r.ay = int16_t(r.ay + int(rng.below(2 * tremor + 1)) - tremor);
    r.az = int16_t(r.az + int(rng.below(2 * tremor + 1)) - tremor);
    for (uint32_t k : knocks) {
      if (ms < k) continue;
      const uint32_t off = ms - k;
      if (pose == Pose::Table) {
        if (off < 4) r.az = int16_t(r.az + 300);
      } else if (off < 20) {
        r.az = int16_t(r.az + 1800);
      } else if (off < 40) {
        r.az = int16_t(r.az - 1700);
      }
    }
    return r;
  }
};

struct Count {
  int pellets = 0;            // pellets that left the pantry
  int pelletsInTheDark = 0;   // ...while the panel was dark
  int presses = 0;            // stim::button the Dish took (a knock knock, as a press)
  bool oneMore = false;       // the rim's "one more" lit
};

// One badge: the IMU, the sense task, the shell and the Dish, on one clock.
struct Badge {
  Ether ether{1, Weather{}};
  FakeAir air{ether, PeerAddr{{2, 0, 0, 0, 0, 1}}};
  MemStorage store;
  std::optional<Dish> dish;
  NullLink link;
  ::power::Policy policy;   // src/hw/power.h (blorb has a namespace power too: the duel powers)
  hw::Feel shell;
  Hand hand;
  uint32_t ms = 0;            // the reads' clock
  uint32_t frameMs = 40;      // how often the loop takes the queue and ticks (main.cpp loop)
  uint32_t nextFrame = 0;
  bool asking = false;        // a question owns BOOT (the update's, the debug unlock's)
  bool keepPantryFull = true;
  Count count;
  // The sense task's queue (main.cpp bodyq::ring) and its peak.
  std::deque<std::pair<BodySample, uint32_t>> ring;
  BodySample handed{};
  bool primed = false;
  int64_t best = -1;
  int16_t pk[3] = {};
  uint8_t sub = 0;

  Genome founder;
  explicit Badge(uint8_t home, std::optional<SpeciesId> species = std::nullopt, const char* stage = "adult")
      : shell(kR0, home, policy), founder(species ? starterGenomeOf(*species, 33) : starterGenome(33)) {
    hand.home = home;
    DishOptions o;
    o.founder = &founder;
    o.air = &air;
    dish.emplace(store, 33, 0xFEEDull, o);
    static Badge* self;
    self = this;
    const char* why = debugverbs::growTo(*dish, stage, [](uint32_t ticks) {
      for (uint32_t t = 0; t < ticks; ++t) self->dish->tick(self->ms += 100, self->link);
    });
    EXPECT_EQ(why, nullptr) << why;
    PetClock& c = dish->clock();   // pet noon: awake
    c.phaseOffsetTicks = int32_t(PetClock::kDayTicks / 2) - int32_t(c.petTicks % PetClock::kDayTicks);
    hungry(Fx::ratio(7, 10));
    for (int i = 0; i < 20 && creature().body().asleep; ++i) dish->tick(ms += 100, link);
    nextFrame = ms;
    run(1500);   // held a moment: the shell's history, the orientation settled
  }

  Creature& creature() { return std::get<Creature>(dish->occupant()); }
  void hungry(Fx h) {
    for (const auto& d : DRIVES) creature().inject(driveChem(d.id), Fx::zero());
    creature().inject(driveChem(drive::hunger), h);
  }

  // The loop: the queue through the shell into the Dish, then the ticks due.
  void frame() {
    for (; !ring.empty(); ring.pop_front()) {
      const hw::Felt f = shell.sample(ring.front().first, ring.front().second, asking);
      dish->sample(f.screen, ring.front().second);
      count.oneMore = count.oneMore || f.screen.knockWaiting;
      if (dish->firedLastSample() >> stim::button.v & 1) ++count.presses;
    }
    const uint8_t before = dish->habitat().pantry;
    dish->tick(ms, link);
    const uint8_t after = dish->habitat().pantry;
    if (after < before) {
      count.pellets += before - after;
      if (policy.low()) count.pelletsInTheDark += before - after;
      if (keepPantryFull) dish->habitat().pantry = before;
    }
    policy.update(ms, false);
  }

  // forMs of reads: 250 Hz awake, one per sample while dark (main.cpp bodyq::sense).
  void run(uint32_t forMs) {
    for (const uint32_t end = ms + forMs; ms < end;) {
      const bool fast = !policy.low();
      ms += fast ? 4 : kSampleMs;
      const Read r = hand.at(ms);
      BodySample s;
      s.ax = r.ax, s.ay = r.ay, s.az = r.az;
      const int64_t dx = s.ax - handed.ax, dy = s.ay - handed.ay, dz = s.az - handed.az;
      if (dx * dx + dy * dy + dz * dz > best) best = dx * dx + dy * dy + dz * dz, pk[0] = s.ax, pk[1] = s.ay, pk[2] = s.az;
      if (fast && ++sub < kSampleMs / 4) continue;
      sub = 0;
      s.hasPeak = primed;
      s.peakAx = pk[0], s.peakAy = pk[1], s.peakAz = pk[2];
      ring.push_back({s, ms});
      handed = s, primed = true, best = -1;
      if (int32_t(ms - nextFrame) >= 0) frame(), nextFrame = ms + frameMs;
    }
  }

  // Knocks at these offsets from now, then long enough for any burst to be decided and a pellet to drop.
  Count knock(std::initializer_list<uint32_t> at, uint32_t settleMs = 2500) {
    count = {};
    for (uint32_t a : at) hand.knocks.push_back(ms + 50 + a);
    run(50 + (at.size() ? *(at.end() - 1) : 0) + settleMs);
    hand.knocks.clear();
    return count;
  }
};

// Runs 20 ms at a time until `done` or `limitMs`; true when done.
template <class Pred>
bool runUntil(Badge& b, Pred done, uint32_t limitMs) {
  for (uint32_t end = b.ms + limitMs; b.ms < end;) {
    b.run(20);
    if (done()) return true;
  }
  return false;
}

}  // namespace feedingtest
