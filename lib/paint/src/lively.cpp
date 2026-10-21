#include "lively.h"

#include "blorb/species.h"

namespace paint {
namespace {

using blorb::Appearance;
using blorb::Fx;
namespace pose = blorb::pose;

// Each species' idle, in the screen's ms (they run at kScreenSpeed) and 1/16 px.
// The depths stay inside the 3 px the stand point keeps clear for his breath
// (draw.cpp's kMarginQ4), so the layer never pushes him off the glass.
struct Tempo {
  uint16_t breathMs; uint8_t breathQ4;   // a breath's length, and how much taller it makes him
  uint16_t swayMs; uint8_t swayQ4;       // a sway from the feet: its length, and how far his top goes each way
  uint16_t bobMs; uint8_t bobPx;         // a bob off the floor; 0 px: he stays down
  uint16_t blinkMs;                      // about how often he blinks
  uint8_t gaze;                          // how often, of 8, his eyes are off to a side
};
constexpr Tempo kTempo[] = {
    {2400, 36, 3800, 20, 0, 0, 3600, 3},     // grungo: a frog's throat, a skittish oracle's glances
    {3600, 28, 6400, 12, 0, 0, 5200, 1},     // mossby: no rush, ever
    {2000, 44, 3000, 16, 1800, 1, 3000, 3},  // puffkin: puffs up and bobs
    {2600, 28, 3200, 28, 0, 0, 4000, 2},     // gilly: sways as if the water moved him
    {2200, 24, 2600, 20, 1400, 2, 4200, 2},  // mothwick: hovers on his wings
    {1600, 24, 2000, 16, 0, 0, 2600, 4},     // mudge: quick and twitchy
    {1800, 24, 2200, 14, 0, 0, 3000, 3},     // fizz: crackles
    {3200, 28, 5200, 20, 0, 0, 5600, 1},     // hushwing: an owl's stillness, slow blinks
    {2400, 28, 2800, 22, 0, 0, 3200, 4},     // tinker: fidgets, looks about
    {3000, 32, 4400, 28, 2600, 1, 4800, 1},  // octopus: one long undulation
};
constexpr int kTempoCount = int(sizeof kTempo / sizeof kTempo[0]);
static_assert(kTempoCount >= blorb::kSpeciesCount, "an idle tempo per species.def row");

constexpr uint32_t kWalkStepMs = 560, kFleeStepMs = 360;   // a waddle's step, either foot
constexpr uint32_t kBounceMs = 700;                        // hopping in circles: a hop
constexpr int kBouncePx = 7;
constexpr uint32_t kChewMs = 340;
constexpr int kChewQ4 = 20;
constexpr uint32_t kHoverMs = 2200;                        // foreseeing, he rises a little
constexpr int kHoverPx = 2;
constexpr uint32_t kGlanceMs = 1700;                       // his eyes hold a way this long
constexpr uint32_t kBlinkShutMs = 130;

uint32_t mix32(uint32_t a, uint32_t b) {
  uint32_t h = a ^ (b * 0x9E3779B1u);
  h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
  return h;
}

// sin of phase/65536 turn, Q12, linear between 64 steps: smooth at any frame rate.
int sinQ12(uint32_t phase) {
  static constexpr int16_t kQuarter[17] = {0, 401, 799, 1189, 1567, 1931, 2276, 2598, 2896,
                                           3166, 3406, 3612, 3784, 3920, 4017, 4076, 4096};
  auto at = [](int i) {
    i &= 63;
    if (i < 16) return int(kQuarter[i]);
    if (i < 32) return int(kQuarter[32 - i]);
    if (i < 48) return -int(kQuarter[i - 32]);
    return -int(kQuarter[64 - i]);
  };
  const int i = int((phase >> 10) & 63), f = int(phase & 1023);
  const int s0 = at(i), s1 = at(i + 1);
  return s0 + (s1 - s0) * f / 1024;
}
int cosQ12(uint32_t phase) { return sinQ12(phase + 16384); }
int absi(int v) { return v < 0 ? -v : v; }

// Where `ms` is in a cycle of `period` ms (as 1/65536 turn), started at a seeded point.
uint32_t phaseOf(uint32_t ms, uint32_t period, uint32_t seed) {
  return uint32_t((ms + seed % period) % period * 65536u / period);
}
// 0 -> 1 -> 0 over the cycle, Q12: a breath in and out, a bob up and down.
int swell(uint32_t phase) { return (4096 - cosQ12(phase)) / 2; }

bool blinkAt(uint32_t ms, uint32_t cycle, uint32_t seed) {
  const uint32_t t = ms + seed % cycle, n = t / cycle, within = t % cycle;
  const uint32_t h = mix32(seed ^ 0xB11Cu, n);
  const uint32_t at = h % (cycle - 3 * kBlinkShutMs - 200);
  if (within - at < kBlinkShutMs) return true;
  return (h >> 28) == 0 && within - at - 2 * kBlinkShutMs < kBlinkShutMs;   // now and then, twice
}

}  // namespace

Lively livelyOf(const Appearance& a, int maxLift) {
  Lively l;
  if (!a.lively || a.reflexActive || a.kind != Appearance::Kind::Creature) return l;
  const Tempo& t = kTempo[a.species.v < kTempoCount ? a.species.v : 0];
  const uint32_t ms = a.liveMs, seed = a.lifeSeed;
  const bool asleep = a.asleep || a.pose == pose::sleep;

  uint32_t breathMs = t.breathMs;
  int breath = t.breathQ4;
  if (a.intensity >= Fx::ratio(3, 4)) breath += breath / 4;   // stirred, he breathes deeper
  if (asleep) breathMs = breathMs * 7 / 4, breath = breath * 4 / 3;
  if (a.pose == pose::curl) breath /= 2;
  l.breathQ4 = breath * swell(phaseOf(ms, breathMs, seed)) / 4096;
  if (asleep) return l;   // only his breath: eyes shut, still

  l.swayQ4 = t.swayQ4 * sinQ12(phaseOf(ms, t.swayMs, mix32(seed, 1))) / 4096;
  if (t.bobPx) l.lift = (t.bobPx * swell(phaseOf(ms, t.bobMs, mix32(seed, 2))) + 2048) / 4096;

  if (a.pose == pose::walk || a.pose == pose::flee) {
    // A waddle: his weight from foot to foot, a little up off each.
    const uint32_t step = a.pose == pose::walk ? kWalkStepMs : kFleeStepMs;
    const int s = sinQ12(phaseOf(ms, 2 * step, mix32(seed, 3)));
    l.swayQ4 = (t.swayQ4 + 10) * s / 4096;
    l.lift = (absi(s) + 2048) / 4096;
  } else if (a.pose == pose::bounce) {
    l.lift = kBouncePx * absi(sinQ12(phaseOf(ms, 2 * kBounceMs, mix32(seed, 4)))) / 4096;
  } else if (a.pose == pose::eat) {
    l.breathQ4 += kChewQ4 * swell(phaseOf(ms, kChewMs, mix32(seed, 5))) / 4096;
    l.swayQ4 /= 2;
  } else if (a.pose == pose::foresee) {
    l.lift = (kHoverPx * swell(phaseOf(ms, kHoverMs, mix32(seed, 6))) + 2048) / 4096;
    l.swayQ4 = 0;
  } else if (a.pose == pose::curl) {
    l.swayQ4 = 0, l.lift = 0;
  }
  if (l.lift > maxLift) l.lift = maxLift > 0 ? maxLift : 0;

  // His eyes, now and then off to a side for a while; ahead, walking (a gaze
  // after his heading flipped with each tick's wander and read as a twitch).
  if (t.gaze && a.pose != pose::curl && a.pose != pose::walk && a.pose != pose::flee) {
    const uint32_t h = mix32(seed ^ 0x6A2Eu, (ms + seed % kGlanceMs) / kGlanceMs);
    if ((h & 7) < t.gaze) l.gazeX = (h & 8) ? 1 : -1;
  }
  l.blink = blinkAt(ms, t.blinkMs, mix32(seed, 7));
  return l;
}

int livelyRockQ8(const Appearance& a) {
  // eggShear swings 8/64 of a turn a tick: a whole rock every 800 ms.
  return int((a.liveMs + a.lifeSeed % 800) % 800 * 2048 / 100);
}

}  // namespace paint
