#pragma once
// Plays a scenario timeline into the real Dish through the seams the board
// uses: one BodySample every 20 ms into Dish::sample, Dish::tick on the
// board's millis(), protocol lines through a Link, wall time from a battery
// RTC, and an unplug as the Dish destroyed and rebuilt from its storage (no
// shutdown save, as on the board). Nothing here writes engine state directly.
//
// What it records is what the owner can see: the fields the phone's STATE
// reply carries (phase, stage, action, face, asleep, the drives, life,
// injury, the pellet count) and what the screen shows (a running hop or
// flinch, what is in his mouth).
#include <array>
#include <deque>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "blorb/dish.h"
#include "e2e/scenario.h"
#include "mem_storage.h"
#include "time_sources.h"

namespace e2e {

using blorb::kActionCount;
using blorb::kDriveCount;

// The sim's SimHand (sim/Wire.h), yielding BodySamples instead of registers,
// so a gesture here crosses the detectors' thresholds exactly as it does there.
class Hand {
 public:
  void start(const Gesture& g, uint64_t now) {
    switch (g.what) {
      case Do::Shake: shakeUntil_ = now + g.ms; break;
      case Do::Knock: tap_ = 1; break;
      case Do::DoubleKnock: tap_ = 2; break;
      case Do::Press: buttonUntil_ = now + g.ms; break;
      case Do::Cradle: holdUntil_ = now + g.ms; break;
      case Do::Lid: lidUntil_ = now + g.ms; break;
      case Do::Tilt: tiltUntil_ = now + g.ms; tiltX_ = g.x; tiltY_ = g.y; break;
      default: break;
    }
  }
  void letGo() { *this = Hand{}; }

  blorb::BodySample sample(uint64_t now) {
    blorb::BodySample s;
    const bool holding = now < holdUntil_;
    if (now < lidUntil_) {
      s.az = -1000;
    } else if (holding) {
      s.ay = kHoldY;
      s.az = kHoldZ;
    } else if (now < tiltUntil_) {
      s.ax = tiltX_;
      s.ay = tiltY_;
      s.az = int16_t(isqrt(1000000 - int32_t(tiltX_) * tiltX_ - int32_t(tiltY_) * tiltY_));
    } else {
      s.az = 1000;
    }
    if (now < shakeUntil_) s.ax = int16_t(s.ax + ((now / 60) % 2 ? kShakeMg : -kShakeMg));
    const int32_t dt = int32_t(now - dieAt_);
    dieAt_ = now;
    dieMilliC_ = holding ? std::min(kHandMilliC, dieMilliC_ + dt) : std::max(kRoomMilliC, dieMilliC_ - dt / 4);
    s.tempCx10 = int16_t(dieMilliC_ / 100);
    s.tapCode = tap_;
    tap_ = 0;
    s.buttonDown = now < buttonUntil_;
    return s;
  }

 private:
  static constexpr int16_t kShakeMg = 1500, kHoldY = -500, kHoldZ = 866;
  static constexpr int32_t kRoomMilliC = 25000, kHandMilliC = 29000;
  static int32_t isqrt(int32_t v) {
    int32_t r = 0;
    while ((r + 1) * (r + 1) <= v) ++r;
    return r;
  }
  uint64_t shakeUntil_ = 0, buttonUntil_ = 0, holdUntil_ = 0, lidUntil_ = 0, tiltUntil_ = 0;
  int16_t tiltX_ = 0, tiltY_ = 0;
  uint8_t tap_ = 0;
  int32_t dieMilliC_ = kRoomMilliC;
  uint64_t dieAt_ = 0;
};

// A phone in range for a while, sending its lines once on arrival.
struct PhoneLink : blorb::Link {
  uint64_t until = 0, now = 0;
  std::deque<std::string> inbound;
  std::string current;
  bool connected() override { return now < until; }
  std::optional<std::string_view> readLine() override {
    if (inbound.empty()) return std::nullopt;
    current = std::move(inbound.front());
    inbound.pop_front();
    return std::string_view(current);
  }
  void writeLine(std::string_view) override {}
};

enum class Phase : uint8_t { Egg, Creature, Clutch };   // Occupant's alternatives, as STATE names them

// One 100 ms tick as the owner sees it.
struct Tick {
  Phase phase = Phase::Egg;
  blorb::Stage stage = blorb::Stage::Baby;
  uint8_t action = 0, face = 0, pellets = 0;
  bool asleep = false, hop = false, flinch = false;
  blorb::Mouthful mouth = blorb::Mouthful::Nothing;
  int16_t drive[kDriveCount]{};   // permille, as STATE prints them
  int16_t life = 0, injury = 0;
  bool is(blorb::ActionId a) const { return phase == Phase::Creature && action == a.v; }
};

struct Window {
  uint64_t at = 0, end = 0;
  std::vector<Tick> ticks;                  // one per tick from `at`
  std::vector<blorb::Appearance> frames;    // every kFrameMs, when the run renders
  std::vector<Tick> frameTicks;             // what STATE said at each frame, for captions
};

struct RunLog {
  static constexpr int kDays = 16;
  std::vector<Window> windows;
  // The whole run by wall day and hour, while a creature lives.
  std::array<std::array<std::array<uint32_t, kActionCount>, 24>, kDays> actionTicks{};
  std::array<std::array<uint32_t, 24>, kDays> asleepTicks{}, creatureTicks{};
  std::array<std::array<std::array<int64_t, kDriveCount>, 24>, kDays> driveSum{};
  std::vector<uint64_t> hatches, deaths;    // wall ms of each egg -> creature and creature -> clutch
  std::vector<blorb::DeathCause> causes;    // per death, as STATE reports it in the clutch phase
  uint32_t finalHash = 0;                   // the Dish's replay hash: equal arms never diverged
  uint32_t ticks = 0;

  // Per-hour share of creature ticks spent on `a` over days [first, last].
  double share(blorb::ActionId a, int first, int last, int fromHour = 0, int toHour = 24) const {
    uint64_t on = 0, all = 0;
    for (int d = first; d <= last && d < kDays; ++d)
      for (int h = fromHour; h < toHour; ++h) {
        on += actionTicks[d][h][a.v];
        all += creatureTicks[d][h];
      }
    return all ? double(on) / double(all) : -1;
  }
};

constexpr uint64_t kFrameMs = 80;                 // 12.5 fps for the contact sheets and videos
constexpr uint32_t kEpochUnix = 1790812800;       // 2026-10-01 00:00 UTC: wall midnight at founding
constexpr uint64_t kLineageId = 0xB10Bu;

inline Tick observe(const blorb::Dish& dish) {
  Tick t;
  const blorb::Occupant& o = dish.occupant();
  t.phase = Phase(o.index());
  for (const blorb::Pellet& p : dish.habitat().pellets) t.pellets = uint8_t(t.pellets + p.present);
  const auto* c = std::get_if<blorb::Creature>(&o);
  if (!c) return t;
  auto permille = [](blorb::Fx v) { return int16_t((int64_t(v.raw) * 1000 + blorb::Fx::kOne / 2) >> blorb::Fx::kFrac); };
  t.stage = c->stage();
  t.action = c->action().v;
  t.face = c->face().current.v;
  t.asleep = c->body().asleep;
  t.hop = c->body().reflex && c->body().reflex->kind == blorb::reflex::hop;
  t.flinch = c->body().reflex && c->body().reflex->kind == blorb::reflex::flinch;
  t.mouth = c->body().mouth;
  for (size_t d = 0; d < kDriveCount; ++d) t.drive[d] = permille(c->chemistry().drive(blorb::DRIVES[d].id));
  t.life = permille(c->chemistry().chem[blorb::chem::life.v]);
  t.injury = permille(c->chemistry().chem[blorb::chem::injury.v]);
  return t;
}

inline RunLog run(const std::vector<Row>& rows, uint64_t endAt, Arm arm, uint32_t seed, bool frames = false) {
  const std::vector<Event> events = timeline(rows, arm, seed);
  RunLog log;
  blorbtest::MemStorage store;
  blorbtest::FakeRtc rtc;
  PhoneLink link;
  Hand hand;
  uint64_t wall = 0;
  uint32_t boot = 0;
  rtc.atBoot = kEpochUnix;
  rtc.poweredMs = &boot;
  std::optional<blorb::Dish> dish;
  dish.emplace(store, seed, kLineageId, blorb::DishOptions{&rtc});
  std::vector<size_t> open;
  uint64_t nextFrame = 0;
  Phase was = Phase::Egg;
  Tick last;

  size_t next = 0;
  while (wall < endAt) {
    for (; next < events.size() && events[next].at <= wall; ++next) {
      const Gesture& g = events[next].g;
      if (g.what == Do::Watch) {
        log.windows.push_back(Window{wall, wall + g.ms, {}, {}});
        open.push_back(log.windows.size() - 1);
      } else if (g.what == Do::Phone) {
        link.until = wall + g.ms;
        if (g.line) link.inbound.emplace_back(g.line);
      } else if (g.what == Do::Unplug) {
        dish.reset();
        hand.letGo();
        link.until = 0;
        wall += g.ms;
        boot = 0;
        rtc.atBoot = uint32_t(kEpochUnix + wall / 1000);
        dish.emplace(store, seed, kLineageId, blorb::DishOptions{&rtc});
        while (next + 1 < events.size() && events[next + 1].at < wall) ++next;   // nobody was there
      } else {
        hand.start(g, wall);
      }
    }
    link.now = wall;
    dish->sample(hand.sample(wall), boot);
    const uint32_t before = dish->tickCount();
    dish->tick(boot, link);
    if (dish->tickCount() != before) {
      ++log.ticks;
      const Tick t = last = observe(*dish);
      if (t.phase != was && t.phase == Phase::Creature) log.hatches.push_back(wall);
      if (t.phase != was && t.phase == Phase::Clutch) {
        log.deaths.push_back(wall);
        log.causes.push_back(std::get<blorb::Clutch>(dish->occupant()).cause);
      }
      was = t.phase;
      const uint64_t day = wall / kD, hour = wall % kD / kH;
      if (t.phase == Phase::Creature && day < RunLog::kDays) {
        ++log.creatureTicks[day][hour];
        ++log.actionTicks[day][hour][t.action];
        log.asleepTicks[day][hour] += t.asleep;
        for (size_t d = 0; d < kDriveCount; ++d) log.driveSum[day][hour][d] += t.drive[d];
      }
      for (size_t i = 0; i < open.size();) {
        Window& w = log.windows[open[i]];
        if (wall >= w.end) {
          open.erase(open.begin() + long(i));
          continue;
        }
        w.ticks.push_back(t);
        ++i;
      }
    }
    if (frames && !open.empty() && wall >= nextFrame) {
      for (size_t i : open) {
        log.windows[i].frames.push_back(dish->appearance());
        log.windows[i].frameTicks.push_back(last);
      }
      nextFrame = wall + kFrameMs;
    }
    wall += blorb::kSampleMs;
    boot += blorb::kSampleMs;
  }
  log.finalHash = dish->hash();
  return log;
}

}  // namespace e2e
