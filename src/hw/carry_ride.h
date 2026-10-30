// Motions a badge goes through being carried about, as the IMU would report
// them: what `DEBUG ride <kind>` feeds the real sense path on a board, and
// what test/test_carry and the film (outputs/carry-react/) play through the
// shell and the Dish. Pure: no Arduino, no clock, no randomness beyond a hash
// of the time, so the cable, the tests and the film play the same samples.
//
// Each is drawn in the screen's frame at home (+y down the screen, +z out of
// it; upright in a hand reads about (0, -970, -150) milli-g) and turned into
// the IMU's own frame for the home the badge is built for (toRaw), so it goes
// through orient.h's turn like a hand would.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "blorb/senses.h"

namespace hw {
namespace ride {

enum class Kind : uint8_t { Still, Bumpy, Turn, Spin, Swing, Walk, Jolt, Shake, Settle };
struct Info { Kind kind; const char* name; uint32_t ms; const char* what; };
inline constexpr Info kRides[] = {
    {Kind::Still, "still", 3000, "held still in a hand"},
    {Kind::Bumpy, "bumpy", 9000, "a bumpy car ride in a hand: road buzz and a pothole every 1.3 s"},
    {Kind::Turn, "turn", 5000, "a car taking a turn: 0.35 g sideways for 2.5 s"},
    {Kind::Spin, "spin", 4000, "turned round twice in the screen's plane, 1.1 s a turn, then still"},
    {Kind::Swing, "swing", 6000, "swung on an arm, 50 degrees each way at 0.8 Hz, then still"},
    {Kind::Walk, "walk", 10000, "carried at a walk, 1.8 steps a second"},
    {Kind::Jolt, "jolt", 2500, "brought down by a hand and set down hard: one jolt, then still"},
    {Kind::Shake, "shake", 5000, "shaken hard for 3.2 s (several shakes), then still"},
    {Kind::Settle, "settle", 21000, "carried at a walk 15 s, then laid face up on a table"},
};

inline const Info* find(const char* name) {
  for (const Info& r : kRides)
    if (std::strcmp(r.name, name) == 0) return &r;
  return nullptr;
}

// A small hash of the time: a hand's tremor, the same on every run.
inline int noise(uint32_t t, uint32_t salt, int amp) {
  uint32_t h = t * 0x9E3779B1u ^ salt * 0x85EBCA6Bu;
  h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12;
  return amp ? int(h % uint32_t(2 * amp + 1)) - amp : 0;
}

constexpr double kPi = 3.14159265358979323846;

inline double halfSine(double t, double start, double dur) {
  return t < start || t >= start + dur ? 0 : std::sin(kPi * (t - start) / dur);
}

struct V { double x, y, z; };

// Upright in a hand: gravity up the screen, tipped back a little.
inline V upright() { return {0, -970, -150}; }

// The motion `k` at `t` ms from its start, in the screen's frame, milli-g.
inline V at(Kind k, uint32_t t) {
  const double s = t / 1000.0;
  V v = upright();
  const int tremor = 10;
  switch (k) {
    case Kind::Still:
      break;
    case Kind::Bumpy: {
      const double buzz = 120 * (std::sin(2 * kPi * 7 * s) + 0.7 * std::sin(2 * kPi * 13 * s + 1) +
                                 0.5 * std::sin(2 * kPi * 19 * s + 2)) / 1.6;
      v.y -= buzz;
      v.x += 45 * std::sin(2 * kPi * 11 * s + 0.5);
      const double p = std::fmod(s, 1.3);
      v.y -= 900 * halfSine(p, 0.4, 0.08) - 350 * halfSine(p, 0.48, 0.06);
      v.x += 200 * halfSine(p, 0.4, 0.08);
      break;
    }
    case Kind::Turn: {
      double g = 0;
      if (s < 0.7) g = 0.5 - 0.5 * std::cos(kPi * s / 0.7);
      else if (s < 3.2) g = 1;
      else if (s < 3.9) g = 0.5 + 0.5 * std::cos(kPi * (s - 3.2) / 0.7);
      v.x += 350 * g;
      v.y -= 50 * std::sin(2 * kPi * 9 * s);
      break;
    }
    case Kind::Spin: {
      const double turn = s < 2.2 ? s / 1.1 : 2.0;
      const double th = 2 * kPi * turn;
      v = {1000 * std::sin(th), -1000 * std::cos(th) * 0.97, -150};
      if (s < 2.2) v.y -= 120;   // a hand turning him round pulls him in a little
      break;
    }
    case Kind::Swing: {
      if (s >= 5) break;
      const double A = 50 * kPi / 180, w = 2 * kPi * 0.8, r = 0.6;
      const double phi = A * std::sin(w * s), dphi = A * w * std::cos(w * s);
      const double radial = std::cos(phi) + r * dphi * dphi / 9.81;   // in g, along the arm (up the screen)
      v = {100 * std::sin(phi), -970 * radial, -150};
      break;
    }
    case Kind::Walk:
    case Kind::Settle: {
      if (k == Kind::Settle && s >= 15) {
        const double e = s >= 15.5 ? 1 : 0.5 - 0.5 * std::cos(kPi * (s - 15) / 0.5);   // laid down over 0.5 s
        v = {0, -970 * (1 - e), -150 * (1 - e) + 1000 * e};
        if (s >= 15.5) return {double(noise(t, 1, 2)), double(noise(t, 2, 2)), 1000.0 + noise(t, 3, 2)};
        break;
      }
      const double step = std::fmod(s, 1 / 1.8);
      v.y -= 220 * halfSine(step, 0, 0.12) - 80 * halfSine(step, 0.12, 0.15);
      v.x += 60 * std::sin(2 * kPi * 0.9 * s);
      break;
    }
    case Kind::Jolt:
      // Brought down by a hand (lighter as it starts down, heavier as it
      // slows), then set down hard: a moving badge, so no knock is heard in it.
      v.y += 280 * halfSine(s, 0.1, 0.3) - 300 * halfSine(s, 0.4, 0.3);
      v.x += 60 * std::sin(2 * kPi * 6 * s) * (s < 0.7);
      v.y -= 1300 * halfSine(s, 0.7, 0.04) - 500 * halfSine(s, 0.74, 0.04);
      v.x += 250 * halfSine(s, 0.7, 0.04);
      break;
    case Kind::Shake:
      // A hand starts moving (so no knock is heard in it, as `DEBUG shake`), then shakes hard.
      if (t < 400) v.x += t / 40 % 2 ? -400 : 400;
      else if (t < 3600) v.x += (t - 400) / 110 % 2 ? -1100 : 1100;
      break;
  }
  return {v.x + noise(t, 1, tremor), v.y + noise(t, 2, tremor), v.z + noise(t, 3, tremor)};
}

inline int16_t clip(double v) {
  const long r = std::lround(v);
  return int16_t(r > 1999 ? 1999 : r < -1999 ? -1999 : r);
}

// The screen's frame at `home` back to the IMU's (orient::Up::toFrame's inverse; r0 is ORIENT_R0).
inline blorb::BodySample toRaw(V v, uint8_t home, int r0) {
  blorb::BodySample s;
  const int16_t x = clip(v.x), y = clip(v.y);
  s.az = clip(v.z);
  switch ((home - r0 + 1) & 3) {
    case 0: s.ax = x, s.ay = y; break;
    case 1: s.ax = int16_t(-y), s.ay = x; break;
    case 2: s.ax = int16_t(-x), s.ay = int16_t(-y); break;
    default: s.ax = y, s.ay = int16_t(-x); break;
  }
  return s;
}

}  // namespace ride
}  // namespace hw
