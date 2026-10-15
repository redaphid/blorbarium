#pragma once
// A car ride as the QMI8658 reports it: milli-g at 50 Hz, each axis clipped
// at the +-2 g range the badge runs it at. Seeded, so a failing ride replays.
//
// The car's vertical carries a road buzz (8 tones from 5 to 30 Hz, the level
// wandering over 5 s stretches) and a 1-2 Hz body bounce. Potholes come front
// axle then rear axle 150-400 ms later: a dip, a half-sine lift of 0.5-1.5 g
// over 50-150 ms (1.5-2.5 g in all), a rebound of up to 0.6 g and a ring.
// Turns and braking lean 0.2-0.4 g for 2-6 s. Thrown rides add launches: a
// lift that throws him off the seat for 40-150 ms, a landing, maybe one
// smaller hop. In a hand the road arrives damped, with sway and tremor.
//
// Knocks: nothing here models the tap engine itself, so assume the worst.
// Every lift of 0.75 g or more (the engine's 0.5 g^2 peak, plus margin)
// reports a tap 100-180 ms after its peak, as the engine's quiet window
// would. `taps` counts them, so a test can show the ride offered a storm.
#include <cmath>
#include <cstdint>
#include <vector>
#include "blorb/senses.h"

namespace blorbtest {

enum class Lying : uint8_t { FaceUp, FaceDown, InHand };

struct Road {
  double buzzMg = 60;        // RMS on the car's vertical; across it, half
  double potholeEveryS = 8;  // mean gap
  double turnEveryS = 12;
  double launchEveryS = 0;   // 0: never thrown
};

class Rand {
 public:
  explicit Rand(uint64_t seed) : s_(seed * 0x9E3779B97F4A7C15ull + 1) {}
  double unit() {   // splitmix64, [0, 1)
    uint64_t z = (s_ += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return double((z ^ (z >> 31)) >> 11) / double(1ull << 53);
  }
  double in(double lo, double hi) { return lo + (hi - lo) * unit(); }
  double gauss() { double g = 0; for (int i = 0; i < 12; ++i) g += unit(); return g - 6; }
  double gap(double meanS) { return -std::log(1 - unit()) * meanS; }   // exponential

 private:
  uint64_t s_;
};

struct Vec { double x = 0, y = 0, z = 0; };
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator*(double k, Vec a) { return {k * a.x, k * a.y, k * a.z}; }

inline blorb::BodySample toSample(Vec g) {
  auto axis = [](double v) {
    long r = std::lround(v * 1000);
    return int16_t(r > 1999 ? 1999 : r < -1999 ? -1999 : r);
  };
  blorb::BodySample s;
  s.ax = axis(g.x), s.ay = axis(g.y), s.az = axis(g.z);
  return s;
}

// The device-frame directions of the car's up, across and along.
struct Frame3 { Vec up, across, along; };

inline Frame3 frameFor(Lying lying, Rand& r) {
  const double tilt = (lying == Lying::InHand ? r.in(30, 45) : r.in(5, 15)) * M_PI / 180;
  const double c = std::cos(tilt), s = std::sin(tilt);
  if (lying == Lying::FaceDown) return {{0, s, -c}, {1, 0, 0}, {0, c, s}};
  return {{0, -s, c}, {1, 0, 0}, {0, c, s}};
}

inline double halfSine(double t, double start, double dur) {
  return t < start || t >= start + dur ? 0 : std::sin(M_PI * (t - start) / dur);
}

struct Tone { double a, f, ph; };

inline std::vector<Tone> tones(Rand& r, double rms, int n, double lo, double hi) {
  std::vector<Tone> v;
  for (int i = 0; i < n; ++i) v.push_back({rms / std::sqrt(n / 2.0), r.in(lo, hi), r.in(0, 2 * M_PI)});
  return v;
}
inline double play(const std::vector<Tone>& v, double t) {
  double s = 0;
  for (const Tone& k : v) s += k.a * std::sin(2 * M_PI * k.f * t + k.ph);
  return s;
}

struct Bump {    // one axle over one pothole, in g along the car's up
  double at, dipA, dipD, liftA, liftD, reboundA, reboundD, ringA, ringF, across;
  double peak() const { return at + dipD + liftD / 2; }
  double up(double t) const {
    double v = -dipA * halfSine(t, at, dipD) + liftA * halfSine(t, at + dipD, liftD) -
               reboundA * halfSine(t, at + dipD + liftD, reboundD);
    double ringAt = at + dipD + liftD + reboundD;
    if (t >= ringAt) v += ringA * std::exp(-(t - ringAt) / 0.08) * std::sin(2 * M_PI * ringF * (t - ringAt));
    return v;
  }
};

struct Turn { double at, dur, g; bool across; };
struct Launch { double at, air, landA, hop; };   // hop 0: no second hop

inline double ramp(double t, const Turn& k) {
  constexpr double kEase = 0.7;
  if (t < k.at || t > k.at + k.dur) return 0;
  double in = (t - k.at) / kEase, out = (k.at + k.dur - t) / kEase;
  double e = in < 1 ? in : out < 1 ? out : 1;
  return k.g * (0.5 - 0.5 * std::cos(M_PI * e));
}

struct Ride {
  std::vector<blorb::BodySample> samples;
  uint32_t taps = 0;           // tap reports the road offered
  uint32_t throws = 0;         // times it threw him off the seat
  std::vector<double> lifts;   // when each axle's lift peaked, in seconds from the start
};

inline Ride ride(Lying lying, const Road& road, uint64_t seed, uint32_t seconds) {
  Rand r(seed);
  const Frame3 f = frameFor(lying, r);
  const bool hand = lying == Lying::InHand;
  const double damp = hand ? 0.5 : 1;
  const std::vector<Tone> buzzUp = tones(r, road.buzzMg / 1000, 8, 5, 30);
  const std::vector<Tone> buzzAcross = tones(r, road.buzzMg / 2000, 8, 5, 30);
  const std::vector<Tone> buzzAlong = tones(r, road.buzzMg / 2000, 8, 5, 30);
  const std::vector<Tone> bounce = tones(r, 0.02, 2, 1, 2);
  const std::vector<Tone> sway = tones(r, 0.04, 3, 0.3, 1.5), tremor = tones(r, 0.01, 2, 8, 12);
  std::vector<double> level;
  for (uint32_t i = 0; i <= seconds / 5; ++i) level.push_back(r.in(0.6, 1.4));

  std::vector<Bump> bumps;
  for (double t = r.gap(road.potholeEveryS); t < seconds; t += 0.5 + r.gap(road.potholeEveryS)) {
    Bump b{t, r.in(0.2, 0.5), r.in(0.03, 0.06), r.in(0.5, 1.5), r.in(0.05, 0.15), r.in(0.2, 0.6),
           r.in(0.04, 0.08), r.in(0.1, 0.25), r.in(12, 20), r.in(-0.3, 0.3)};
    bumps.push_back(b);
    const double k = r.in(0.6, 1.0);
    Bump rear = b;
    rear.at += r.in(0.15, 0.4);
    rear.dipA *= k, rear.liftA *= k, rear.reboundA *= k, rear.ringA *= k;
    bumps.push_back(rear);
  }
  std::vector<Turn> turns;
  for (double t = r.gap(road.turnEveryS); t < seconds; t += r.gap(road.turnEveryS)) {
    Turn k{t, r.in(2, 6), r.in(0.2, 0.4) * (r.unit() < 0.5 ? -1 : 1), r.unit() < 0.6};
    turns.push_back(k);
    t += k.dur;
  }
  std::vector<Launch> launches;
  if (road.launchEveryS > 0)
    for (double t = r.gap(road.launchEveryS); t < seconds; t += 1 + r.gap(road.launchEveryS))
      launches.push_back({t, r.in(0.04, 0.15), r.in(0.8, 1.5), r.unit() < 0.5 ? r.in(0.03, 0.06) : 0});

  Ride out;
  out.throws = uint32_t(launches.size());
  const uint32_t n = seconds * 1000 / blorb::kSampleMs;
  out.samples.reserve(n);
  for (uint32_t i = 0; i < n; ++i) {
    const double t = i * blorb::kSampleMs / 1000.0;
    double up = level[size_t(t / 5)] * play(buzzUp, t) * damp + play(bounce, t);
    double across = level[size_t(t / 5)] * play(buzzAcross, t) * damp, along = play(buzzAlong, t) * damp;
    for (const Bump& b : bumps) {
      if (t < b.at - 0.01 || t > b.at + 1) continue;
      const double v = b.up(t) * (hand ? 0.6 : 1);
      up += v;
      across += b.across * (v > 0 ? v : 0);
    }
    for (const Turn& k : turns) (k.across ? across : along) += ramp(t, k);
    Vec g = (1 + up) * f.up + across * f.across + along * f.along;
    if (hand) g = g + Vec{play(sway, t), play(sway, t + 0.37), play(tremor, t)};
    for (const Launch& l : launches) {
      const double land = l.at + l.air, hopAt = land + 0.05;
      if (t >= l.at && t < land) g = Vec{};   // in the air: weightless
      else if (l.hop > 0 && t >= hopAt && t < hopAt + l.hop) g = Vec{};
      else g = g + (l.landA * halfSine(t, land, 0.03) + 0.5 * l.landA * halfSine(t, hopAt + l.hop, 0.03)) * f.up;
    }
    g = g + Vec{0.002 * r.gauss(), 0.002 * r.gauss(), 0.002 * r.gauss()};   // the sensor's own noise
    out.samples.push_back(toSample(g));
  }
  for (const Bump& b : bumps) {
    out.lifts.push_back(b.peak());
    if (b.liftA * (hand ? 0.6 : 1) < 0.75) continue;
    const size_t at = size_t((b.peak() + r.in(0.1, 0.18)) * 1000 / blorb::kSampleMs);
    if (at >= out.samples.size()) continue;
    out.samples[at].tapCode = r.unit() < 0.2 ? 2 : 1;
    ++out.taps;
  }
  return out;
}

}  // namespace blorbtest
