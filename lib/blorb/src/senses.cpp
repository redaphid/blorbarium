#include "blorb/senses.h"

namespace blorb {
namespace {

// Measured on the 1.28 by claude-notification-screen's orient.h.
constexpr int32_t kOneG = 1000;                    // milli-g
constexpr int32_t kShakeJoltMg = 550;              // SHAKE_G
constexpr uint32_t kShakeGapMs = 60;               // SHAKE_GAP_MS
constexpr uint32_t kShakeWindowMs = 900;           // SHAKE_WINDOW_MS
constexpr uint8_t kShakeJolts = 4;                 // SHAKE_JOLTS
constexpr uint32_t kShakeRearmMs = 600;            // SHAKE_REARM_MS
constexpr int32_t kHeldTiltMg = 300;               // HELD_TILT_G
constexpr uint32_t kHeldOnMs = 400;                // HELD_ON_MS
constexpr uint32_t kHeldOffMs = 1500;              // HELD_OFF_MS
constexpr int32_t kSteadyMg = 250;                 // TILT_MAX_SHAKE_G: further off 1 g is a jolt, not gravity
constexpr uint32_t kKnockGapMs = 400;

// Chosen here, not measured.
constexpr int32_t kStillMg = 120;                  // hand tremor still counts as still for a cradle
constexpr uint32_t kCradleAfterMs = 3000;
constexpr uint32_t kCradleEveryMs = 10000;
constexpr int32_t kFaceDownMg = -600;              // flipped below this, righted above kFaceUpMg
constexpr int32_t kFaceUpMg = 600;
constexpr int32_t kLidMg = -800;
constexpr uint32_t kLidDownMs = 2000;
constexpr int32_t kLidReleaseMg = -500;
constexpr uint32_t kLidUpMs = 200;                 // a knock on a face-down dish is not a righting
constexpr int32_t kFreeFallMg = 300;
constexpr uint32_t kFreeFallMs = 100;              // about 5 cm of fall; a shorter dip is a shake jolt
constexpr uint32_t kLandWindowMs = 500;
constexpr uint32_t kButtonHoldMs = 1200;
constexpr int32_t kWarmSpanCx10 = 30;              // 3 C above the baseline is warmth 1
constexpr int32_t kWarmBaselineSamples = 32768;    // about 11 minutes at 50 Hz
constexpr int32_t kMotionFullMg = 500;
constexpr Fx kTiltSmooth = Fx::ratio(1, 16);       // orient.h's 0.25 per 100 ms, at 50 Hz
constexpr Fx kMotionSmooth = Fx::ratio(1, 8);
constexpr Fx kHalf = Fx::ratio(1, 2);
constexpr Fx kHalfPi{26353589};                    // pi/2 in Q8.24

// Pet day: night is 21:00 to 07:00 of pet time.
constexpr uint32_t kNightStartTick = 21 * kTicksPerHour;
constexpr uint32_t kNightEndTick = 7 * kTicksPerHour;
constexpr uint32_t kMidDayTick = (kNightStartTick + kNightEndTick) / 2;
constexpr uint32_t kEntrainAfterTicks = 30 * kTicksPerMinute;   // a nap or a pocket is not a night
constexpr uint32_t kEntrainMaxTicks = kTicksPerHour;            // per pet day

Fx flag(bool b) { return b ? Fx::one() : Fx::zero(); }

uint32_t isqrt(uint32_t v) {
  uint32_t r = 0, bit = uint32_t(1) << 30;
  while (bit > v) bit >>= 2;
  for (; bit != 0; bit >>= 2) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
  }
  return r;
}

int32_t magnitudeMg(const BodySample& s) {
  uint32_t sq = uint32_t(int32_t(s.ax) * s.ax) + uint32_t(int32_t(s.ay) * s.ay) + uint32_t(int32_t(s.az) * s.az);
  return int32_t(isqrt(sq));
}

int32_t absMg(int32_t v) { return v < 0 ? -v : v; }

// A boolean that flips only once the opposite condition has held for its
// dwell. Returns true on the sample it flips.
bool settle(bool cond, bool& state, uint32_t& since, uint32_t ms, uint32_t onMs, uint32_t offMs) {
  if (cond == state) { since = ms; return false; }
  if (ms - since < (cond ? onMs : offMs)) return false;
  state = cond;
  since = ms;
  return true;
}

uint32_t timeOfDay(const PetClock& c) {
  int64_t t = (int64_t(c.petTicks) + c.phaseOffsetTicks) % PetClock::kDayTicks;
  return uint32_t(t < 0 ? t + PetClock::kDayTicks : t);
}

void shiftPhase(PetClock& c, int32_t by) {
  int64_t day = PetClock::kDayTicks;
  c.phaseOffsetTicks = int32_t(((int64_t(c.phaseOffsetTicks) + by) % day + day) % day);
}

// sin x for 0 <= x <= pi/2, Taylor to x^7: error below 2e-4.
Fx sinSeries(Fx x) {
  Fx x2 = x * x, term = x, sum = x;
  const int32_t div[3] = {6, 20, 42};
  for (int i = 0; i < 3; ++i) {
    term = -(term * x2 * Fx::ratio(1, div[i]));
    sum += term;
  }
  return sum;
}

struct SinCos { Fx s, c; };

SinCos sinCosOfTurn(uint32_t part, uint32_t whole) {
  uint64_t scaled = uint64_t(part) * 4;
  uint32_t quadrant = uint32_t(scaled / whole);
  Fx x = kHalfPi * Fx::ratio(int32_t(scaled % whole), int32_t(whole));
  Fx s = sinSeries(x), c = sinSeries(kHalfPi - x);
  switch (quadrant & 3) {
    case 0: return {s, c};
    case 1: return {c, -s};
    case 2: return {-s, -c};
    default: return {-c, s};
  }
}

Fx toUnit(Fx v) { return clamp01((v + Fx::one()) * kHalf); }

}  // namespace

// ---- pet clock -------------------------------------------------------------------

Fx PetClock::dayFraction() const { return Fx::ratio(int32_t(timeOfDay(*this)), int32_t(kDayTicks)); }

bool PetClock::night() const {
  uint32_t t = timeOfDay(*this);
  return t >= kNightStartTick || t < kNightEndTick;
}

// Lidded through pet day, past the nap threshold, pet time runs at double
// speed in the afternoon (night comes sooner) and stands still in the morning
// (dawn comes later), until an hour of phase is spent for this pet day.
void PetClock::advance(bool lidded) {
  ++petTicks;
  darkRunTicks = lidded ? darkRunTicks + 1 : 0;
  uint32_t t = timeOfDay(*this);
  if (t == 0) entrainedTicks = 0;
  if (!lidded || night() || darkRunTicks < kEntrainAfterTicks || entrainedTicks >= kEntrainMaxTicks) return;
  shiftPhase(*this, t >= kMidDayTick ? 1 : -1);
  ++entrainedTicks;
}

void PetClock::alignToWall(uint32_t unixSeconds, int16_t tzMinutes) {
  constexpr int64_t kSecondsPerDay = 86400;
  int64_t local = int64_t(unixSeconds) + int64_t(tzMinutes) * 60;
  int64_t secondOfDay = (local % kSecondsPerDay + kSecondsPerDay) % kSecondsPerDay;
  int64_t want = secondOfDay * (1000 / kTickMs);
  int64_t have = petTicks % kDayTicks;
  phaseOffsetTicks = int32_t(((want - have) % kDayTicks + kDayTicks) % kDayTicks);
  entrainedTicks = 0;
}

// ---- sense output -------------------------------------------------------------------

void SenseOut::set(LocusId id, Fx value) {
  for (uint8_t i = 0; i < lociCount; ++i) {
    if (loci[i].locus == id) { loci[i].value = value; return; }
  }
  if (lociCount < sizeof(loci) / sizeof(loci[0])) loci[lociCount++] = Write{id, value};
}

void SenseOut::fire(StimId id) {
  for (uint8_t i = 0; i < stimCount; ++i) {
    if (stimuli[i] == id) return;
  }
  if (stimCount < sizeof(stimuli) / sizeof(stimuli[0])) stimuli[stimCount++] = id;
}

void SenseOut::clear() {
  lociCount = 0;
  stimCount = 0;
}

// ---- detectors -------------------------------------------------------------------

// tilt_x above 0.5 means the +x edge is raised, so things roll toward -x.
void TiltDetector::sample(const BodySample& s, uint32_t, SenseOut& out) {
  if (absMg(magnitudeMg(s) - kOneG) <= kSteadyMg) {
    sx_ += (Fx::ratio(s.ax, kOneG) - sx_) * kTiltSmooth;
    sy_ += (Fx::ratio(s.ay, kOneG) - sy_) * kTiltSmooth;
    if (!upside_ && s.az < kFaceDownMg) { upside_ = true; out.fire(stim::flipped); }
    else if (upside_ && s.az > kFaceUpMg) { upside_ = false; out.fire(stim::righted); }
  }
  out.set(locus::tilt_x, clamp01(kHalf + sx_ * kHalf));
  out.set(locus::tilt_y, clamp01(kHalf + sy_ * kHalf));
  out.set(locus::upside_down, flag(upside_));
}

void MotionDetector::sample(const BodySample& s, uint32_t ms, SenseOut& out) {
  int32_t mag = magnitudeMg(s);
  int32_t dev = absMg(mag - kOneG);
  energy_ += (Fx::ratio(dev < kOneG ? dev : kOneG, kOneG) - energy_) * kMotionSmooth;
  out.set(locus::motion, clamp01(energy_ * Fx::ratio(kOneG, kMotionFullMg)));

  bool weightless = mag < kFreeFallMg;
  if (weightless) {
    if (fall_ == Fall::None || fall_ == Fall::Landing) { fall_ = Fall::Falling; fallSince_ = ms; }
    if (fall_ == Fall::Falling && ms - fallSince_ >= kFreeFallMs) { fall_ = Fall::Free; joltCount_ = 0; }
    if (fall_ == Fall::Free) return;
  } else if (fall_ == Fall::Falling) {
    fall_ = Fall::None;
  } else if (fall_ == Fall::Free) {
    fall_ = Fall::Landing;
    fallSince_ = ms;
  }
  if (fall_ == Fall::Landing) {
    if (dev >= kShakeJoltMg) { out.fire(stim::dropped); fall_ = Fall::None; }
    else if (ms - fallSince_ > kLandWindowMs) fall_ = Fall::None;
  }

  // Once a shake fires it takes kShakeRearmMs without a jolt before another
  // can start, so a shake kept up for a second is one shake (orient.h).
  if (dev < kShakeJoltMg) return;
  if (joltCount_ > 0 && ms - quietSince_ < kShakeGapMs) return;
  bool rested = ms - quietSince_ >= kShakeRearmMs;
  quietSince_ = ms;
  if (rattled_ && !rested) return;
  rattled_ = false;
  if (joltCount_ == kShakeJolts) {
    for (uint8_t i = 1; i < kShakeJolts; ++i) jolts_[i - 1] = jolts_[i];
    --joltCount_;
  }
  jolts_[joltCount_++] = ms;
  if (joltCount_ == kShakeJolts && ms - jolts_[0] <= kShakeWindowMs) {
    out.fire(stim::shake);
    rattled_ = true;
    joltCount_ = 0;
  }
}

void HeldDetector::sample(const BodySample& s, uint32_t ms, SenseOut& out) {
  bool tilted = int32_t(s.ax) * s.ax + int32_t(s.ay) * s.ay > kHeldTiltMg * kHeldTiltMg;
  if (settle(tilted, held_, heldSince_, ms, kHeldOnMs, kHeldOffMs)) out.fire(held_ ? stim::picked_up : stim::put_down);
  out.set(locus::held, flag(held_));

  bool still = held_ && s.az > 0 && absMg(magnitudeMg(s) - kOneG) <= kStillMg;
  if (!still) {
    stillSince_ = ms;
    cradled_ = false;
  } else if (cradled_ ? ms - lastCradle_ >= kCradleEveryMs : ms - stillSince_ >= kCradleAfterMs) {
    cradled_ = true;
    lastCradle_ = ms;
    out.fire(stim::cradle);
  }
  out.set(locus::cradled, flag(cradled_));
}

void KnockDetector::sample(const BodySample& s, uint32_t ms, SenseOut& out) {
  if ((s.tapCode != 1 && s.tapCode != 2) || ms - last_ < kKnockGapMs) return;
  last_ = ms;
  out.fire(s.tapCode == 2 ? stim::double_knock : stim::knock);
}

void ButtonDetector::sample(const BodySample& s, uint32_t ms, SenseOut& out) {
  if (s.buttonDown && !down_) {
    down_ = true;
    held_ = false;
    downSince_ = ms;
  } else if (s.buttonDown && !held_ && ms - downSince_ >= kButtonHoldMs) {
    held_ = true;
    out.fire(stim::button_hold);
  } else if (!s.buttonDown && down_) {
    down_ = false;
    if (!held_) out.fire(stim::button);
  }
}

void LidDetector::sample(const BodySample& s, uint32_t ms, SenseOut& out) {
  bool faceDown = s.az < (lidded_ ? kLidReleaseMg : kLidMg);
  if (settle(faceDown, lidded_, since_, ms, kLidDownMs, kLidUpMs)) out.fire(lidded_ ? stim::lid_down : stim::lid_up);
}

// Held in a hand, the IMU die warms above where it has been sitting.
void WarmthDetector::sample(const BodySample& s, uint32_t, SenseOut& out) {
  if (s.tempCx10 == INT16_MIN) { out.set(locus::warmth, Fx::zero()); return; }
  int32_t t = int32_t(s.tempCx10) * 65536;
  if (!known_) { baseline_ = t; known_ = true; }
  baseline_ += int32_t((int64_t(t) - baseline_) / kWarmBaselineSamples);
  out.set(locus::warmth, clamp01(Fx::ratio(int32_t((int64_t(t) - baseline_) / 65536), kWarmSpanCx10)));
}

void TouchDetector::sample(const BodySample& s, uint32_t, SenseOut& out) {
  if (s.touchDown == was_) return;
  was_ = s.touchDown;
  out.set(locus::touch, flag(was_));
  if (was_) out.fire(stim::touched);
}

void DayDetector::tick(const TickContext& ctx, SenseOut& out) {
  bool night = ctx.clock.night();
  out.set(locus::light, flag(!night && ctx.clock.darkRunTicks == 0));
  SinCos sc = sinCosOfTurn(timeOfDay(ctx.clock), PetClock::kDayTicks);
  out.set(locus::day_sin, toUnit(sc.s));
  out.set(locus::day_cos, toUnit(sc.c));
  if (primed_ && night != wasNight_) out.fire(night ? stim::dusk : stim::dawn);
  wasNight_ = night;
  primed_ = true;
}

void OwnerDetector::tick(const TickContext& ctx, SenseOut& out) {
  out.set(locus::owner_near, flag(ctx.ownerNear));
  if (ctx.ownerNear != was_) out.fire(ctx.ownerNear ? stim::owner_arrived : stim::owner_left);
  was_ = ctx.ownerNear;
}

}  // namespace blorb
