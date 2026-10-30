#include "lively.h"

#include "blorb/idle.h"
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
    {1800, 28, 2400, 14, 0, 0, 4400, 4},     // puck: wound tight, eyes wide, every sound a glance
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

// ---- the idles (blorb/idles.def) ----------------------------------------------------
// Each idle's motion, on its own clock (Appearance::idleTicks, at the screen's
// speed, so it runs smooth between ticks) and how far through it he is.

constexpr int kSquashMax = 110;   // of 1024: the most an idle stretches or squashes him

struct IdleClock {
  uint32_t ms;   // since it began
  int pc;        // 0..1024 through it
  int osc(uint32_t period) const { return sinQ12(uint32_t(ms % period * 65536u / period)); }   // -4096..4096
  int hump(uint32_t period) const { return swell(uint32_t(ms % period * 65536u / period)); }   // 0..4096
  // 0..4096 across [from, to) of the act, eased in.
  int ramp(int from, int to) const {
    if (pc <= from) return 0;
    if (pc >= to) return 4096;
    return swell(uint32_t((pc - from) * 32768 / (to - from)));
  }
};

void idleMotion(const Appearance& a, Lively& l) {
  namespace idles = blorb::idles;
  const uint32_t tq = uint32_t(a.idleTicks) * 256 + a.idleSub;
  const uint32_t through = a.idleLength ? tq * 4 / a.idleLength : 1024;
  // At his pace (#61): brisk when young, small or happy; slow when old, big,
  // sleepy, hurt or sick. Fixed for the act, so its motion never jumps.
  const uint32_t pace = a.idlePace ? a.idlePace : 100;
  const IdleClock c{tq * blorb::kTickMs / 256 * pace / 100, int(through > 1024 ? 1024 : through)};
  const bool walking = a.pose == pose::walk;
  switch (a.idle.v) {
    case idles::explore.v:   // round the rim, looking out one way then the other
      l.gazeX = (c.ms / 1500) & 1 ? 1 : -1;
      break;
    case idles::chase_tail.v:   // whirling: a lean round with each lap, a hop off each foot
      l.swayQ4 = 44 * c.osc(560) / 4096;
      l.lift = 3 * absi(c.osc(560)) / 4096;
      l.gazeX = c.osc(560) > 0 ? 1 : -1;
      break;
    case idles::dance.v:   // a wiggle from the feet, a bounce on the beat, squash and stretch
      l.swayQ4 = 48 * c.osc(900) / 4096;
      l.lift = 3 * c.hump(450) / 4096;
      l.wide = 40 * c.osc(450) / 4096, l.tall = -l.wide;
      l.gazeX = c.osc(900) > 0 ? 1 : -1;
      break;
    case idles::juggle.v:   // a bat up with each throw, eyes on the pebble
      l.lift = 2 * c.hump(720) / 4096;
      l.tall = 30 * c.hump(720) / 4096;
      l.gazeY = -1;
      break;
    case idles::dig.v:   // down at the floor and up, quick
      l.tall = -60 * c.hump(400) / 4096, l.wide = 40 * c.hump(400) / 4096;
      l.swayQ4 = 12 * c.osc(400) / 4096;
      l.lift = 0, l.gazeY = 1;
      break;
    case idles::stretch.v: {   // up as tall as he goes, held, and down into the yawn
      const int up = c.ramp(0, 450) - c.ramp(600, 1000);
      l.tall = kSquashMax * up / 4096, l.wide = -40 * up / 4096;
      l.lift = up / 4096;
      l.gazeY = up > 2048 ? -1 : 0;
      l.swayQ4 = 0;
      break;
    }
    case idles::look_around.v:   // this way, that way, what was that, and again
      l.gazeX = c.pc < 300 ? -1 : c.pc < 450 ? 1 : c.pc < 600 ? 0 : c.pc < 800 ? -1 : 1;
      l.swayQ4 = 16 * l.gazeX;
      if (c.pc >= 450 && c.pc < 600) l.lift = 2;
      break;
    case idles::peek.v:   // up close to the glass, leaning in
      if (walking) break;
      l.wide = l.tall = 50 + 20 * c.osc(1600) / 4096;
      l.gazeX = 0, l.swayQ4 = 0;
      break;
    case idles::sing.v:   // head up, a slow sway, a swell on each note
      l.swayQ4 = 24 * c.osc(1400) / 4096;
      l.tall = 30 * c.hump(700) / 4096;
      l.gazeY = -1;
      break;
    case idles::shadow_box.v: {   // jab, jab, a bounce on his toes
      const int side = (c.ms / 500) & 1 ? 1 : -1;
      const bool jab = c.ms % 500 < 170;
      l.swayQ4 = side * (jab ? 56 : 8);
      l.wide = jab ? 30 : 0;
      l.lift = int((c.ms / 250) & 1);
      l.gazeX = side;
      break;
    }
    case idles::sniff.v:   // nose to the floor between steps
      if (walking) break;
      l.tall = -40 * c.hump(500) / 4096, l.wide = 20 * c.hump(500) / 4096;
      l.gazeY = 1, l.gazeX = (c.ms / 700) & 1 ? 1 : -1;
      break;
    case idles::tidy.v:   // brisk sweeps between steps
      if (walking) break;
      l.swayQ4 = 30 * c.osc(600) / 4096;
      l.gazeY = 1;
      break;
    case idles::wave.v:   // all of him in the wave
      if (walking) break;
      l.swayQ4 = 36 * c.osc(700) / 4096;
      l.lift = 1, l.gazeX = 0;
      break;
    case idles::roll_over.v: {   // over to one side, flat on his back, up the other side
      const int s = sinQ12(uint32_t(c.pc) * 64);      // a whole turn of sin across the act
      const int back = sinQ12(uint32_t(c.pc) * 32);   // half a turn: 0 -> 1 -> 0
      l.swayQ4 = 90 * s / 4096;
      l.tall = -2 * kSquashMax * (back > 0 ? back : 0) / 4096;
      l.wide = 60 * (back > 0 ? back : 0) / 4096;
      l.lift = 0, l.gazeX = s > 0 ? 1 : -1;
      break;
    }
    case idles::daydream.v:   // eyes up and away
      l.gazeY = -1, l.gazeX = 1;
      l.swayQ4 = 10 * c.osc(3000) / 4096;
      l.lift = 0;
      break;
    case idles::stargaze.v:   // up at the sky, from one star to the next
      l.gazeY = -1, l.gazeX = (c.ms / 2500) & 1 ? 1 : -1;
      l.tall = 20;
      l.lift = 0;
      break;
    case idles::sneeze.v:   // ah... ah... CHOO, and a shake of the head
      if (c.pc < 450) l.tall = 80 * c.pc / 450, l.gazeY = -1;
      else if (c.pc < 560) l.tall = -100, l.wide = 80, l.lift = 0, l.swayQ4 = 20 * c.osc(160) / 4096;
      break;
    case idles::discover.v:   // eyes down at what he found, leaning to it; a bounce of delight
      l.gazeY = 1, l.swayQ4 = 18;
      if (c.pc >= 450) l.lift = 2 * c.hump(500) / 4096, l.tall = 30;
      break;
    case idles::boo.v:   // low as he sneaks up, then up big: BOO
      if (c.pc < 550) l.tall = -70, l.wide = 30, l.gazeX = 0;
      else if (c.pc < 750) l.tall = 90, l.wide = 70, l.lift = 3;
      else l.swayQ4 = 20 * c.osc(500) / 4096;   // giggling
      break;
    case idles::hide.v:   // scrunched up small, peeking out once, then out
      if (c.pc < 700) l.tall = -2 * kSquashMax * c.ramp(0, 150) / 4096, l.wide = 40 * c.ramp(0, 150) / 4096,
                      l.swayQ4 = 0, l.gazeX = (c.ms / 1200) & 1 ? 1 : 0;
      else l.tall = 60 * (1024 - c.pc) / 324, l.lift = 2;
      break;
    case idles::hiccup.v:   // hic: up off the floor, three times
      if (c.pc % 348 < 120) l.lift = 3, l.tall = 40;
      break;
    case idles::zoomies.v:   // leaning into the bend, flat out
      l.swayQ4 = 30;
      l.lift = int((c.ms / 120) & 1);
      break;
    // ---- #61: his day, how he feels, his age ----
    case idles::wash.v:   // a paw round and round his face, a scrub at each pass
      l.swayQ4 = 14 * c.osc(520) / 4096;
      l.wide = 12 * c.hump(520) / 4096, l.tall = -15;
      l.lift = 0, l.gazeX = 0;
      break;
    case idles::sunbathe.v: {   // back he lies, belly to the sun, and up again at the end
      const int back = c.ramp(0, 160) - c.ramp(880, 1024);
      l.tall = -90 * back / 4096, l.wide = 50 * back / 4096;
      l.swayQ4 = 0, l.lift = 0, l.gazeY = back > 2048 ? -1 : 0, l.gazeX = 0;
      break;
    }
    case idles::fireflies.v:   // a swipe up at one, between his steps
      if (walking) { l.gazeY = -1; break; }
      l.lift = 3 * c.hump(700) / 4096;
      l.tall = 40 * c.hump(700) / 4096;
      l.gazeY = -1, l.gazeX = (c.ms / 1400) & 1 ? 1 : -1;
      break;
    case idles::exercise.v: {   // squats: down low, up tall, counted
      const int squat = c.hump(900);
      l.tall = 40 - 110 * squat / 4096, l.wide = 50 * squat / 4096;
      l.lift = squat < 600 ? 2 : 0;
      l.swayQ4 = 0, l.gazeX = 0;
      break;
    }
    case idles::yawn.v: {   // up and up into the yawn, and down
      const int up = c.ramp(150, 450) - c.ramp(700, 1000);
      l.tall = 80 * up / 4096, l.wide = -20 * up / 4096;
      l.gazeY = up > 2048 ? -1 : 0, l.lift = 0, l.swayQ4 = 0;
      break;
    }
    case idles::tummy.v:   // hunched over his tummy, a rumble through him now and then
      l.tall = -30, l.gazeY = 1, l.lift = 0;
      l.swayQ4 = (c.ms % 1600 < 500) ? 10 * c.osc(110) / 4096 : 0;
      break;
    case idles::beg.v:   // up to the glass, leaning in, bowl held up
      if (walking) break;
      l.wide = l.tall = 40 + 15 * c.osc(1200) / 4096;
      l.lift = c.hump(1200) / 4096, l.gazeX = 0, l.swayQ4 = 0;
      break;
    case idles::nod_off.v: {   // his head droops lower and lower, then up with a start
      const int ph = c.pc % 338;
      if (ph < 268) l.tall = -70 * ph / 268, l.gazeY = 1, l.swayQ4 = 0, l.lift = 0;
      else l.tall = 20, l.lift = 2, l.gazeY = 0;
      break;
    }
    case idles::nurse.v:   // hunched, rocking slowly, rubbing the sore spot
      l.tall = -40, l.gazeY = 1, l.lift = 0;
      l.swayQ4 = 20 * c.osc(1800) / 4096;
      l.gazeX = c.osc(1800) > 0 ? 1 : -1;
      break;
    case idles::queasy.v:   // a slow, green wobble
      l.swayQ4 = 40 * c.osc(1400) / 4096;
      l.wide = 30 * c.osc(700) / 4096, l.tall = -20;
      l.lift = 0;
      break;
    case idles::mope.v: {   // low, still, eyes down, and a sigh
      const int sigh = c.hump(3000);
      l.tall = -50 - 30 * sigh / 4096, l.wide = 10 * sigh / 4096;
      l.swayQ4 = 6 * c.osc(4000) / 4096, l.gazeY = 1, l.lift = 0;
      break;
    }
    case idles::beam.v:   // a happy bounce
      l.lift = 3 * c.hump(600) / 4096;
      l.wide = 30 * c.osc(600) / 4096, l.tall = -l.wide;
      l.gazeX = 0;
      break;
    case idles::tumble.v:   // a wobble, whoops, over he goes, and up
      if (c.pc < 300) l.swayQ4 = 30 * c.osc(400) / 4096;
      else if (c.pc < 800) {
        const int over = c.ramp(300, 450);
        l.swayQ4 = 100 * over / 4096, l.tall = -80 * over / 4096, l.wide = 40 * over / 4096, l.lift = 0;
        l.gazeX = 1;
      } else {
        l.lift = 2 * c.hump(400) / 4096, l.tall = 20;
      }
      break;
    case idles::reminisce.v:   // a slow sway, eyes up and back
      l.swayQ4 = 10 * c.osc(3500) / 4096;
      l.gazeY = -1, l.gazeX = -1, l.tall = 10, l.lift = 0;
      break;
    default: break;   // the toy: a walk, his waddle; the ball is the prop's
  }
}

// His posture shows how he feels (Appearance::moods), each mood in
// proportion: perky with a hop now and then when content, slumped and eyes to
// the floor when bored, drooping and watching the glass when lonely, heavy
// when sleepy, fidgety when hungry, hunched and still when hurting.
void posture(const Appearance& a, Lively& l, uint32_t ms, uint32_t seed) {
  const blorb::Moods& m = a.moods;
  auto of = [](int amount, uint8_t level) { return amount * int(level) / 255; };
  l.tall += of(12, m.content) - of(30, m.bored) - of(18, m.lonely) - of(40, m.sleepy) - of(10, m.hungry) - of(50, m.hurting);
  const int calm = 255 - (m.bored / 2 > m.lonely / 2 ? m.bored / 2 : m.lonely / 2);
  l.swayQ4 = l.swayQ4 * calm / 255 * (255 - m.hurting) / 255 * (255 - m.sleepy * 2 / 3) / 255;
  l.swayQ4 += of(10, m.hungry) * sinQ12(phaseOf(ms, 700, seed)) / 4096;
  l.lift = l.lift * (255 - (m.sleepy > m.hurting ? m.sleepy : m.hurting)) / 255;
  const uint32_t beat = 5200 + mix32(seed, 9) % 2400, t = (ms + seed % beat) % beat;
  if (t < 360) l.lift += of(2 * swell(t * 65536u / 360), m.content) / 4096;   // a little hop of good cheer
  if (m.lonely > 128) l.gazeX = 0;                                          // watching the glass for someone
  else if (m.bored > 128 && ((ms + seed % 4000) / 4000) & 1) l.gazeY = 1;     // sighing at the floor
}

// How much an idle's motion shows over his own: in over its first quarter
// second, out over its last eighth, so its first frame and its last are his
// own and an idle never starts or stops with a jump. 0..4096.
int idleEnvelope(const Appearance& a) {
  const uint32_t tq = uint32_t(a.idleTicks) * 256 + a.idleSub;
  const uint32_t ms = tq * blorb::kTickMs / 256, through = a.idleLength ? tq * 4 / a.idleLength : 1024;
  const int up = ms >= 250 ? 4096 : int(ms * 4096 / 250);
  const int down = through >= 1024 ? 0 : through <= 896 ? 4096 : int((1024 - through) * 4096 / 128);
  return up < down ? up : down;
}

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
  // His size gene (#61): a big one breathes and sways slower, a small one
  // quicker; it is his for life, so the rhythm never jumps.
  const int bulk = int(a.bulk) - 128;
  auto slower = [bulk](uint32_t period) { return uint32_t(int(period) * (512 + bulk) / 512); };

  uint32_t breathMs = slower(t.breathMs);
  int breath = t.breathQ4;
  if (a.intensity >= Fx::ratio(3, 4)) breath += breath / 4;   // stirred, he breathes deeper
  if (asleep) breathMs = breathMs * 7 / 4, breath = breath * 4 / 3;
  if (a.pose == pose::curl) breath /= 2;
  l.breathQ4 = breath * swell(phaseOf(ms, breathMs, seed)) / 4096;
  if (asleep) return l;   // only his breath: eyes shut, still

  l.swayQ4 = t.swayQ4 * sinQ12(phaseOf(ms, slower(t.swayMs), mix32(seed, 1))) / 4096;
  if (t.bobPx) l.lift = (t.bobPx * swell(phaseOf(ms, slower(t.bobMs), mix32(seed, 2))) + 2048) / 4096;

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
  // His eyes, now and then off to a side for a while; ahead, walking (a gaze
  // after his heading flipped with each tick's wander and read as a twitch).
  if (t.gaze && a.pose != pose::curl && a.pose != pose::walk && a.pose != pose::flee) {
    const uint32_t h = mix32(seed ^ 0x6A2Eu, (ms + seed % kGlanceMs) / kGlanceMs);
    if ((h & 7) < t.gaze) l.gazeX = (h & 8) ? 1 : -1;
  }
  if (a.pose == pose::idle || a.idle != blorb::idles::none) posture(a, l, ms, seed);
  if (a.idle != blorb::idles::none) {
    // The idle's motion, blended over his own by its envelope.
    Lively his = l;
    idleMotion(a, l);
    // How big he plays it (#61): his squash gene stretches and squashes him the
    // more or the less; at a brisk pace he sways and bounces the more.
    const int squash = 128 + int(a.squash ? a.squash : 128), pace = a.idlePace ? a.idlePace : 100;
    l.wide = l.wide * squash / 256, l.tall = l.tall * squash / 256;
    l.swayQ4 = l.swayQ4 * (50 + pace / 2) / 100, l.lift = l.lift * (50 + pace / 2) / 100;
    const int e = idleEnvelope(a);
    auto mixed = [e](int own, int idle) { return own + (idle - own) * e / 4096; };
    l.breathQ4 = mixed(his.breathQ4, l.breathQ4), l.lift = mixed(his.lift, l.lift), l.swayQ4 = mixed(his.swayQ4, l.swayQ4);
    l.wide = mixed(his.wide, l.wide), l.tall = mixed(his.tall, l.tall);
    if (e < 2048) l.gazeX = his.gazeX, l.gazeY = his.gazeY;
  }
  // Taller only as far as the room over his head: the stand point keeps a few
  // px clear above him, and a stretch at the rim must stay on the glass.
  if (l.tall > 0) l.tall = l.tall * (maxLift < 8 ? (maxLift > 0 ? maxLift : 0) : 8) / 8;
  if (l.tall > kSquashMax) l.tall = kSquashMax;
  if (l.wide > kSquashMax) l.wide = kSquashMax;
  if (l.lift > maxLift) l.lift = maxLift > 0 ? maxLift : 0;
  l.blink = blinkAt(ms, t.blinkMs, mix32(seed, 7));
  return l;
}

int livelyRockQ8(const Appearance& a) {
  // eggShear swings 8/64 of a turn a tick: a whole rock every 800 ms.
  return int((a.liveMs + a.lifeSeed % 800) % 800 * 2048 / 100);
}

}  // namespace paint
