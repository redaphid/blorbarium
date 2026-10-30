#include "blorb/carry_show.h"

#include <cstring>

namespace blorb {
namespace {

// Each species' temperament for a ride, from personalities/*.txt: how much
// he loves being whirled about (thrill), how easily it frightens him (nerve),
// and how loose his body is (wobbly: how often a jolt sets him wobbling).
// His genome moves each by up to 32 either way, so two pets of a kind differ.
struct Temper { uint8_t thrill, nerve, wobbly; };
constexpr Temper kTemper[] = {
    {70, 220, 150},    // grungo: a skittish oracle; shaking is what he fears most
    {40, 190, 230},    // mossby: hates hurry and things that are fast; a soft snail
    {230, 50, 255},    // puffkin: loves bouncing; a puffball wobbles at anything
    {190, 60, 200},    // gilly: fears honestly not much
    {150, 120, 120},   // mothwick: a flier, a little fussy about it
    {250, 30, 110},    // mudge: races everything, fears sitting still
    {140, 150, 70},    // fizz: a hard shell, and fears getting scuffed
    {50, 230, 90},     // hushwing: fears being startled; an owl holds very still
    {170, 90, 130},    // tinker: curious, looks at everything
    {120, 100, 240},   // octopus: dramatic, and all jelly
};
constexpr uint8_t kTemperCount = uint8_t(sizeof kTemper / sizeof kTemper[0]);

// How long each plays (ms) and its own cooldown, low and high (the seed picks between).
struct Timing { uint16_t lenMs; uint16_t coolLoS, coolHiS; };
constexpr Timing kTiming[kCarryKinds] = {
    {0, 0, 0},          // None
    {3000, 25, 45},     // Cling
    {3200, 15, 30},     // Dizzy
    {1100, 6, 12},      // Jelly
    {2400, 12, 25},     // Whee
    {2600, 20, 40},     // Glance
    {2800, 90, 150},    // Settle
};
constexpr uint16_t kClingMaxMs = 7000, kWheeMaxMs = 6000;
constexpr uint32_t kSharedCoolMs = 3000;   // after any, at least this (plus up to as much again) before the next
constexpr uint32_t kShakesMs = 5000;       // two shakes within this: dizzy
constexpr uint32_t kDizzyWaitMs = 4000;    // a spin's dizzy spell waits this long for the badge to calm, then is dropped
constexpr uint32_t kCalmForMs = 300;       // ...calm this long
constexpr uint32_t kJoltConfirmMs = 160;   // a jolt waits this long for a knock or a shake to show itself
constexpr uint32_t kCutMs = 150;           // a reaction cut short by a gesture fades out this fast

constexpr const char* kNames[kCarryKinds] = {"none", "cling", "dizzy", "jelly", "whee", "glance", "settle"};

uint32_t mix32(uint32_t a, uint32_t b) {
  uint32_t h = a ^ (b * 0x9E3779B1u);
  h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
  return h;
}

int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

uint32_t isqrt(uint32_t v) {
  uint32_t r = 0, bit = uint32_t(1) << 30;
  while (bit > v) bit >>= 2;
  for (; bit != 0; bit >>= 2) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
  }
  return r;
}

int32_t norm3(int32_t x, int32_t y, int32_t z) {
  const uint64_t sq = uint64_t(int64_t(x) * x) + uint64_t(int64_t(y) * y) + uint64_t(int64_t(z) * z);
  return int32_t(isqrt(sq > 0xFFFFFFFFull ? 0xFFFFFFFFu : uint32_t(sq)));
}

// The angle of (x, y) in 1/65536 turn, integer only (the same on the host and
// the badge): an octant, then atan of the ratio by a rational fit (under half
// a degree off), plenty for counting a spin.
int32_t angleOf(int32_t x, int32_t y) {
  const int32_t ax = iabs(x), ay = iabs(y);
  if (ax == 0 && ay == 0) return 0;
  const bool steep = ay > ax;
  const int64_t num = steep ? ax : ay, den = steep ? ay : ax;   // 0..1 in Q16
  const int64_t t = (num << 16) / den;
  // atan(t) / (2 pi) * 65536 for t in [0, 1]: t * (8192 + 2272 (1 - t)) / 65536 turn-units.
  int32_t a = int32_t(t * (8192 + (2272 * ((1 << 16) - t) >> 16)) >> 16);
  if (steep) a = 16384 - a;
  if (x < 0) a = 32768 - a;
  if (y < 0) a = -a;
  return a;
}

// a and b point within the angle whose cosine is pct/100 of each other (either way when `either`).
bool near(int32_t ax, int32_t ay, int32_t az, int32_t bx, int32_t by, int32_t bz, int32_t pct, bool either = false) {
  int64_t dot = int64_t(ax) * bx + int64_t(ay) * by + int64_t(az) * bz;
  if (either && dot < 0) dot = -dot;
  return dot * 100 >= int64_t(pct) * norm3(ax, ay, az) * norm3(bx, by, bz);
}

int16_t wrap16(int32_t d) { return int16_t(uint16_t(uint32_t(d))); }

uint8_t jitter(uint8_t base, uint32_t bits) {
  const int32_t v = int32_t(base) + int32_t(bits & 63) - 32;
  return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v);
}

}  // namespace

const char* carryName(Carry k) { return kNames[uint8_t(k) % kCarryKinds]; }

bool carryFromName(const char* name, Carry& out) {
  for (uint8_t i = 1; i < kCarryKinds; ++i)
    if (std::strcmp(name, kNames[i]) == 0) return out = Carry(i), true;
  return false;
}

uint32_t CarryShow::roll() { return mix32(seed_ ^ 0xCA77u, ++rolls_); }

bool CarryShow::cooled(Carry k, uint32_t ms) const {
  const uint8_t i = uint8_t(k);
  return !cooling_[i] || int32_t(ms - coolUntil_[i]) >= 0;
}

void CarryShow::start(Carry k, uint32_t ms, uint16_t lenMs, uint32_t coolMs) {
  react_ = k, startMs_ = ms, lenMs_ = lenMs;
  flavour_ = uint8_t(roll());
  coolUntil_[uint8_t(k)] = ms + coolMs, cooling_[uint8_t(k)] = true;
  anyCoolUntil_ = ms + lenMs + kSharedCoolMs + roll() % kSharedCoolMs;
  ++starts_[uint8_t(k)];
}

void CarryShow::cut(uint32_t ms) {
  const uint32_t age = ms - startMs_;
  if (age + kCutMs < lenMs_) lenMs_ = uint16_t(age + kCutMs);
}

void CarryShow::force(Carry k, uint32_t ms, uint32_t seed) {
  if (k == Carry::None) return void(react_ = Carry::None);
  seed_ = seed;
  dirX_ = 127, dirY_ = 0, level_ = 200;
  start(k, ms, kTiming[uint8_t(k)].lenMs, 0);
}

void CarryShow::sample(const BodySample& s, uint32_t ms, const CarryContext& c) {
  species_ = c.species, seed_ = c.seed;
  if (!primed_) {
    slowX_ = fastX_ = levelX_ = s.ax * 16, slowY_ = fastY_ = levelY_ = s.ay * 16, slowZ_ = fastZ_ = s.az * 16;
    prevX_ = s.ax, prevY_ = s.ay, prevZ_ = s.az;
    primed_ = true;
    return;
  }
  const Temper& t = kTemper[species_ < kTemperCount ? species_ : 0];
  const uint8_t thrill = jitter(t.thrill, seed_), nerve = jitter(t.nerve, seed_ >> 6), wobbly = jitter(t.wobbly, seed_ >> 12);

  // What holds everything off: a knock under way (its spike and ringing are
  // no ride), a pose gesture, a gesture's word.
  const bool knock = s.knocking || s.knockWaiting || s.knockKnock;
  const bool quiet = knock || c.posing || c.gestured;
  if (quiet) lastQuiet_ = ms, everQuiet_ = true;

  // Gravity, slowly (0.64 s); the sample a little smoothed (80 ms), so the
  // brace follows a hand's push and not a buzz; the jerk; the pull (|a| - 1 g).
  const int32_t sx = s.ax, sy = s.ay, sz = s.az;
  if (!knock) {
    auto ema = [](int32_t& v, int32_t x, int32_t k) { v += (16 * x - v) / k; };   // x16
    ema(slowX_, sx, 32), ema(slowY_, sy, 32), ema(slowZ_, sz, 32);
    ema(fastX_, sx, 4), ema(fastY_, sy, 4), ema(fastZ_, sz, 4);
    ema(levelX_, sx, 128), ema(levelY_, sy, 128);
  }
  const int32_t jerkMg = knock ? 0 : norm3(sx - prevX_, sy - prevY_, sz - prevZ_);
  prevX_ = int16_t(sx), prevY_ = int16_t(sy), prevZ_ = int16_t(sz);
  jerkQ4_ += ((jerkMg > 1500 ? 1500 : jerkMg) * 16 - jerkQ4_) / 16;
  const int32_t jerk = jerkQ4_ / 16;
  if (!knock) magQ4_ += ((norm3(sx, sy, sz) - 1000) * 16 - magQ4_) / 3;
  const int32_t pull = magQ4_ / 16;
  const int32_t gx = slowX_ / 16, gy = slowY_ / 16, gz = slowZ_ / 16;
  const int32_t offX = sx - gx, offY = sy - gy, offZ = sz - gz;
  const int32_t off = norm3(offX, offY, offZ);
  const int32_t dx = fastX_ / 16 - gx, dy = fastY_ / 16 - gy;
  const int32_t plane = norm3(dx, dy, 0);
  // A road and a table push him along gravity; a hand jolts him every which way.
  const bool alongGravity = off > 0 && near(offX, offY, offZ, gx, gy, gz, 70, true);
  // The smoothed sample ~120 ms ago (a jolt's before), and gravity about a
  // second ago: a hand turning him about is handling, not a ride.
  const Vec3 before = hist_[(histAt_ + kHist - 6) % kHist];
  hist_[histAt_] = {fastX_ / 16, fastY_ / 16, fastZ_ / 16};
  histAt_ = uint8_t((histAt_ + 1) % kHist);
  if (ms / 250 != turnSlot_) {
    turnSlot_ = ms / 250;
    turnRing_[turnAt_] = {gx, gy, gz};
    turnAt_ = uint8_t((turnAt_ + 1) % kTurnRing);
  }
  const Vec3& ago = turnRing_[turnAt_];
  const bool turning = !near(gx, gy, gz, ago.x, ago.y, ago.z, 82);

  // The brace: against the acceleration in the screen's plane, past a dead
  // zone. Gravity for it is slower still (2.5 s), so a car's turn, which leans
  // the felt gravity for seconds, leans him for most of it.
  const int32_t bx = fastX_ / 16 - levelX_ / 16, by = fastY_ / 16 - levelY_ / 16;
  const int32_t push = norm3(bx, by, 0);
  int32_t tx = 0, ty = 0;
  if (c.canReact && !c.posing && !knock && push > kBraceDeadMg) {
    const int32_t span = kBraceFullMg - kBraceDeadMg;
    const int32_t past = push - kBraceDeadMg > span ? span : push - kBraceDeadMg;
    const int32_t gain = 192 + int32_t(mix32(seed_, 0xB2AC) % 128);   // 0.75..1.25 of the lean: some slide more
    const int32_t q4 = kBracePxQ4 * past / span * gain / 256;
    tx = -bx * q4 / push, ty = -by * q4 / push;
  }
  braceX_ += (tx - braceX_) / 4, braceY_ += (ty - braceY_) / 4;
  if (tx == 0 && iabs(braceX_) <= 3) braceX_ = 0;
  if (ty == 0 && iabs(braceY_) <= 3) braceY_ = 0;

  // Steps: the pull rising past +60 mg after it fell under -20, in a rhythm.
  if (!stepHigh_ && pull > 60) {
    stepHigh_ = true;
    const uint32_t gap = ms - lastStep_;
    const bool even = steps_ == 0 || (gap * 100 >= stepGap_ * 65 && gap * 100 <= stepGap_ * 135);
    steps_ = gap >= 280 && gap <= 800 && even ? uint8_t(steps_ < 255 ? steps_ + 1 : 255) : 1;
    stepGap_ = gap, lastStep_ = ms;
  } else if (stepHigh_ && pull < -20) {
    stepHigh_ = false;
  }
  if (ms - lastStep_ > 900 || iabs(pull) > 350 || quiet) steps_ = 0;
  const bool walking = steps_ >= 6 && jerk < 260 && !turning;

  // A swing: the pull heavy at the bottom, light at the ends, alternating.
  const int8_t end = pull >= 300 ? 1 : pull <= -250 ? -1 : 0;
  if (end && end != swingSide_) {
    const uint32_t gap = ms - lastSwing_;
    swings_ = swingSide_ && gap >= 150 && gap <= 1200 ? uint8_t(swings_ < 255 ? swings_ + 1 : 255) : 1;
    swingSide_ = end, lastSwing_ = ms;
  }
  if (ms - lastSwing_ > 1200 || quiet) swings_ = 0, swingSide_ = quiet ? 0 : swingSide_;
  const bool swinging = swings_ >= 3 && jerk < 300;

  // The ride: moving, calm, still; its bumps.
  const bool moving = (jerk >= kMoveJerkMg || walking || swinging) && !quiet && (!turning || swinging);
  if (moving) {
    rideMs_ = rideMs_ + kSampleMs > 120000 ? 120000 : rideMs_ + kSampleMs;
    calmMs_ = 0;
  } else if ((calmMs_ += kSampleMs) >= 4000) {
    rideMs_ = 0;
  }
  if (c.gestured) rideMs_ = 0;   // a kid at his gestures is playing, not riding
  const bool still = jerk < kStillJerkMg && off < kStillMg && !quiet;
  stillMs_ = still ? stillMs_ + kSampleMs : 0;
  // Bumps along gravity, each a new one once 150 ms past the last; a bumpy
  // ride is two within 3 s, rough all the while (2 s), on a ride of 4 s and
  // more, the badge pointing one way (a car, a run; not a hand turning him).
  if (!knock && off >= kBumpMg && alongGravity) {
    if (!bumped_ || ms - lastBump_ >= 150) prevBump_ = bumped_ ? lastBump_ : ms, bumps_ = uint8_t(bumped_ ? 2 : 1);
    lastBump_ = ms, bumped_ = true;
  }
  // Rough: the jerk averaged over about 1.3 s, the badge pointing one way.
  roughQ4_ += ((jerkMg > 1500 ? 1500 : jerkMg) * 16 - roughQ4_) / 64;
  const bool bumpy = rideMs_ >= 4000 && roughQ4_ / 16 >= kBumpyJerkMg && !turning && bumped_ && bumps_ >= 2 &&
                     ms - lastBump_ <= 1500 && ms - prevBump_ <= 3000;

  // A spin: gravity turning round the screen's plane, on and on, no pause.
  const int32_t fx = fastX_ / 16, fy = fastY_ / 16;
  if (!knock && norm3(fx, fy, 0) >= 600) {
    const int32_t ang = angleOf(fx, fy);
    if (angPrimed_) {
      const int32_t d = wrap16(ang - lastAng_);
      if (iabs(d) >= 300) {
        spin_ = (spin_ == 0 || (d > 0) == (spin_ > 0)) ? spin_ + d : d;
        lastTurned_ = ms;
      }
    }
    lastAng_ = ang, angPrimed_ = true;
  } else {
    angPrimed_ = false;
  }
  if (ms - lastTurned_ > kSpinStallMs) spin_ = 0;
  if (uint32_t(iabs(spin_)) >= kSpinTurnQ16) dizzyDue_ = true, dizzyAt_ = ms, spin_ = 0, calmFor_ = 0, sig_.dizzyWhy = 1;
  // A long shake: the jerk past kRattleJerkMg for kRattleMs (the shake
  // detector fires once for a shake kept up, so it alone cannot say how long).
  const bool shaking = hadShake_ && ms - lastShake_ <= 3000;   // the shake detector heard a shake begin
  // Shaken in place: gravity keeps its way through it (a badge picked up and
  // turned over jolts as hard, and turns).
  if (!knock && shaking && !turning && jerk >= kRattleJerkMg) rattleMs_ += kSampleMs;   // a lull in it is still the shake
  else rattleMs_ = rattleMs_ > 2 * kSampleMs ? rattleMs_ - 2 * kSampleMs : 0;
  if (rattleMs_ >= kRattleMs) dizzyDue_ = true, dizzyAt_ = ms, calmFor_ = 0, rattleMs_ = 0, sig_.dizzyWhy = 3;
  if (c.shook) {
    if (hadShake_ && ms - lastShake_ <= kShakesMs && !turning) dizzyDue_ = true, dizzyAt_ = ms, calmFor_ = 0, sig_.dizzyWhy = 2;
    hadShake_ = true, lastShake_ = ms;
  }

  auto sat16 = [](int32_t v) { return int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v); };
  sig_.jerk = sat16(jerk), sig_.pull = sat16(pull), sig_.off = sat16(off), sig_.push = sat16(push);
  sig_.rideMs = uint16_t(rideMs_ > 65535 ? 65535 : rideMs_), sig_.stillMs = uint16_t(stillMs_ > 65535 ? 65535 : stillMs_);
  sig_.steps = steps_, sig_.swings = swings_, sig_.spinDeg = sat16(spin_ * 360 / 65536);
  sig_.rough = sat16(roughQ4_ / 16), sig_.rattleMs = uint16_t(rattleMs_ > 65535 ? 65535 : rattleMs_);
  sig_.bumps = bumps_, sig_.turning = turning, sig_.quiet = quiet;

  // A reaction playing: cut short by a gesture; one that lasts as long as its motion grows.
  const bool playing = live(ms);
  if (playing && (quiet || !c.canReact)) cut(ms);
  if (playing && !quiet && c.canReact) {
    const uint32_t age = ms - startMs_;
    const uint16_t cap = react_ == Carry::Cling ? kClingMaxMs : react_ == Carry::Whee ? kWheeMaxMs : 0;
    const bool goesOn = react_ == Carry::Cling ? bumpy || swinging : react_ == Carry::Whee ? swinging || bumpy : false;
    if (cap && goesOn && age + 600 >= lenMs_) lenMs_ = uint16_t(age + 1200 > cap ? cap : age + 1200);
  }

  const bool free = c.canReact && !quiet && !live(ms) && int32_t(ms - anyCoolUntil_) >= 0 &&
                    (!everQuiet_ || ms - lastQuiet_ >= kQuietAfterGestureMs);

  // A dizzy spell: once the spin or the shaking stops, unless it was a gesture.
  if (dizzyDue_) {
    calmFor_ = jerk < 90 && !c.posing && !knock ? calmFor_ + kSampleMs : 0;
    if (c.poseWord || ms - dizzyAt_ > kDizzyWaitMs) {
      dizzyDue_ = false;
    } else if (calmFor_ >= kCalmForMs) {
      dizzyDue_ = false;
      const bool ok = c.canReact && !live(ms) && cooled(Carry::Dizzy, ms);
      if (ok && roll() % 256 < 200u + (255u - thrill) / 5u) {
        dirX_ = 127, dirY_ = 0, level_ = 200;
        const Timing& k = kTiming[uint8_t(Carry::Dizzy)];
        start(Carry::Dizzy, ms, k.lenMs, (k.coolLoS + roll() % (k.coolHiS - k.coolLoS + 1)) * 1000u);
      }
    }
  }

  // A jolt: confirmed when no knock, shake or gesture shows itself within kJoltConfirmMs.
  // A jolt: along gravity as it was just before (a table, a pothole; a knock
  // on his face is across it when he is held up), and he points the same way
  // after (a hand turning him jolts him too).
  const bool jolt = !knock && off >= kJoltMg && jerkMg >= kJoltJerkMg;
  const bool joltAlong = jolt && near(offX, offY, offZ, before.x, before.y, before.z, 80, true);
  if (joltDue_) {
    const int32_t fz = fastZ_ / 16, mag = norm3(fx, fy, fz);
    if (quiet || c.shook) {
      joltDue_ = false;
    } else if (ms - joltAt_ >= kJoltConfirmMs &&
               (!near(fx, fy, fz, joltPre_.x, joltPre_.y, joltPre_.z, 90) || mag < 700 || mag > 1300)) {
      joltDue_ = false;   // turned, or still moving: handling
    } else if (jolt && ms - joltAt_ >= 60 && ++jolts_ > 3) {
      joltDue_ = false;   // jolt after jolt: a rattle or a shake, not one bump
    } else if (ms - joltAt_ >= kJoltConfirmMs) {
      joltDue_ = false;
      const uint32_t chance = 96u + wobbly / 2u;
      const bool riding = rideMs_ >= 1500 && roughQ4_ / 16 >= kBumpyJerkMg;   // a bumpy ride is the cling's
      if (free && !riding && cooled(Carry::Jelly, ms) && roll() % 256 < chance) {
        dirX_ = joltX_, dirY_ = joltY_, level_ = joltLevel_;
        const Timing& k = kTiming[uint8_t(Carry::Jelly)];
        start(Carry::Jelly, ms, k.lenMs, (k.coolLoS + roll() % (k.coolHiS - k.coolLoS + 1)) * 1000u);
      }
    }
  } else if (joltAlong && free) {
    joltDue_ = true, joltAt_ = ms, jolts_ = 1, joltPre_ = before;
    joltX_ = int8_t(offX * 127 / off), joltY_ = int8_t(offY * 127 / off);
    const int32_t lv = 64 + (off - kJoltMg) * 191 / 1200;
    joltLevel_ = uint8_t(lv > 255 ? 255 : lv);
  }

  auto begin = [&](Carry k, int8_t ddx, int8_t ddy, uint8_t level) {
    dirX_ = ddx, dirY_ = ddy, level_ = level;
    const Timing& tm = kTiming[uint8_t(k)];
    start(k, ms, tm.lenMs, (tm.coolLoS + roll() % (tm.coolHiS - tm.coolLoS + 1)) * 1000u);
  };
  auto way = [&](int32_t v) { return int8_t(plane ? v * 127 / plane : 0); };
  auto level = [](int32_t v, int32_t lo, int32_t hi) {
    const int32_t l = v <= lo ? 0 : v >= hi ? 255 : (v - lo) * 255 / (hi - lo);
    return uint8_t(l);
  };
  const bool canStart = free && !joltDue_ && !dizzyDue_;

  // Set down still after a ride: a yawn and a settle.
  if (rideMs_ >= kSettleRideMs && stillMs_ >= kSettleStillMs) {
    rideMs_ = 0;
    if (canStart && cooled(Carry::Settle, ms) && roll() % 256 < 180u) begin(Carry::Settle, 0, 0, 128);
  }
  if (!canStart || ms - lastRoll_ < 1000) return;
  // A smooth swing: the thrill-seekers ride it, the rest cling on.
  if (swinging) {
    lastRoll_ = ms;
    const uint32_t r = roll() % 256;
    const uint8_t lv = level(iabs(pull), 250, 900);
    if (r < thrill + 40u && cooled(Carry::Whee, ms)) begin(Carry::Whee, way(dx), way(dy), lv);
    else if (r >= thrill + 40u && cooled(Carry::Cling, ms)) begin(Carry::Cling, way(dx), way(dy), lv);
    return;
  }
  // A bumpy ride: the nervous cling, the bold bounce along.
  if (bumpy && ms - lastRoll_ >= 1500) {
    lastRoll_ = ms;
    const uint32_t r = roll() % 256;
    const uint8_t lv = level(roughQ4_ / 16, kBumpyJerkMg, 300);
    if (r < nerve && cooled(Carry::Cling, ms)) begin(Carry::Cling, 0, -127, lv);
    else if (r >= nerve && r < nerve + thrill / 2u && cooled(Carry::Whee, ms)) begin(Carry::Whee, 0, -127, lv);
    return;
  }
  // Carried at a walk: now and then he looks about.
  if (walking && rideMs_ >= 3000 && ms - lastRoll_ >= 4000) {
    lastRoll_ = ms;
    if (roll() % 256 < 50u + nerve / 3u && cooled(Carry::Glance, ms)) {
      begin(Carry::Glance, 0, 0, level(jerk, kMoveJerkMg, 260));
      flavour_ = uint8_t((flavour_ & ~2u) | (nerve > thrill ? 2u : 0u));   // bit 1: nervous, else curious
    }
  }
}

CarryView CarryShow::view(uint32_t nowMs) const {
  CarryView v;
  v.braceX = int16_t(braceX_), v.braceY = int16_t(braceY_);
  if (!live(nowMs)) return v;
  v.react = react_;
  const int32_t age = int32_t(nowMs - startMs_);
  v.ageMs = uint16_t(age < 0 ? 0 : age);
  v.lenMs = lenMs_;
  v.flavour = flavour_;
  v.dirX = dirX_, v.dirY = dirY_;
  v.level = level_;
  return v;
}

}  // namespace blorb
