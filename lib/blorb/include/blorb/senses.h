#pragma once
// Senses: raw body samples become sense loci (levels) and stimuli (edges).
//
// The shell hands in one BodySample per IMU read at 50 Hz (Dish::sample).
// Detectors are small hysteresis machines; sample() runs at 50 Hz because a
// shake's jolts are brief, and tick() runs at the 10 Hz body tick for things
// that live on the pet clock or the link. None of them knows what a stimulus
// does to the creature: stimulus genes do. Thresholds are constants in
// senses.cpp, measured by claude-notification-screen's orient.h, because they
// describe the board, not the creature.
#include <cstdint>
#include "blorb/fixed.h"
#include "blorb/registry.h"

namespace blorb {

// What the shell reads from the hardware. Plain data; tests script it.
struct BodySample {
  int16_t ax = 0, ay = 0, az = 0;    // milli-g, device frame, +z out of the screen
  int16_t tempCx10 = INT16_MIN;      // IMU die temperature in 0.1 C; INT16_MIN = unknown
  uint8_t tapCode = 0;               // QMI8658 tap engine: 0 none, 1 single, 2 double
  bool buttonDown = false;           // BOOT on the 1.28, PWR on the 1.46
  bool touchDown = false;            // 1.46 only
  int16_t touchX = 0, touchY = 0;
  // The shell's settled panel rotation is 180 from native: the badge is held
  // upside down in its own plane. Only the shell can say so: it rotates the
  // sample into the screen's frame before the Dish sees it (main.cpp feel()),
  // which turns an upside-down badge's gravity into an upright one's, so no
  // detector could ever derive this from ax/ay. A level, like buttonDown;
  // HangDetector gives it its timing.
  bool invertedDown = false;
  // The settled rotation is a quarter turn from native: he is on one side.
  // Hidden by the turn into the screen's frame for the same reason, so it
  // rides the same way; SideHoldDetector gives it its timing.
  bool sideDown = false;
  // A knock burst on a steady badge is under way (blorb::KnockSense, in the
  // shell): the knock's bounce in the hand jolts both ways like a tiny shake,
  // and is not one.
  bool knocking = false;
  // One knock is in and a second could still make a knock knock
  // (KnockSense::waitingSecond, in the shell): GestureCues lights the hint's
  // "one more" while it lasts, so a kid who knocked once sees it was felt.
  bool knockWaiting = false;
  // The shell reads the IMU at 250 Hz while awake and hands on every fifth
  // read; the reads between are summed up here as the one farthest from the
  // last sample handed on, so a knock's brief peak is not lost between two
  // 50 Hz samples (a knock on a badge lying on a table is over in 10 ms).
  // Raw IMU frame, like ax/ay/az; KnockSense reads it, no detector does.
  bool hasPeak = false;
  int16_t peakAx = 0, peakAy = 0, peakAz = 0;
};

// Pet time. There is no RTC and no battery, so the pet lives in powered
// ticks and its day is (petTicks + phaseOffset) mod 24 pet-hours. Night always
// exists, phone or not. Two things move the phase: the phone's TIME snaps it
// to the real day, and with no phone the owner entrains it (a long dark
// stretch, lidded face down during pet day, pulls the phase toward night by at
// most an hour a day, like daylight setting a body clock). Unpowered time is
// stasis: the pet sleeps in its box.
struct PetClock {
  static constexpr uint32_t kDayTicks = 24 * kTicksPerHour;
  uint32_t petTicks = 0;
  int32_t phaseOffsetTicks = 0;
  uint32_t darkRunTicks = 0;                               // consecutive lidded ticks; > 0 means lidded now
  uint32_t entrainedTicks = 0;                             // phase moved by entrainment since pet midnight
  Fx dayFraction() const;                                  // 0 = pet midnight, 0.5 = pet noon
  bool night() const;
  void advance(bool lidded);                               // +1 tick and entrainment
  void alignToWall(uint32_t unixSeconds, int16_t tzMinutes);
};

// Output of one tick's senses. Fixed capacity: no allocation at 50 Hz.
struct SenseOut {
  struct Write { LocusId locus; Fx value; };
  Write loci[24]{};
  uint8_t lociCount = 0;
  StimId stimuli[16]{};
  uint8_t stimCount = 0;
  // Bit s: stimulus s came from the phone (Dish::fire), not a detector. A duel
  // reads only what the body did, so no verb or twist can open one or lock a move.
  uint64_t remote = 0;
  void set(LocusId, Fx);   // the last write to a locus in a tick wins
  void fire(StimId);       // a repeat of the same id in one tick merges, so a doubled delivery is a no-op
  void fireRemote(StimId); // fire, marked as the phone's
  void clear();
};

struct TickContext { const PetClock& clock; bool ownerNear; uint32_t tick; };

// A detector overrides sample() and/or tick(); the base versions do nothing.
struct DetectorBase {
  void sample(const BodySample&, uint32_t, SenseOut&) {}
  void tick(const TickContext&, SenseOut&) {}
};

struct TiltDetector : DetectorBase {      // EMA tilt -> tilt_x/tilt_y, upside_down; Flipped/Righted
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private: Fx sx_, sy_; bool upside_ = false;
};
// Gravity as the body feels it at rest: an average of the samples within
// 0.25 g of 1 g and near the average, so a jolt or a swing never drags it.
// What a sample has beyond it (d) is a hand or the road. A detector that
// needs it keeps its own.
struct Gravity {
  void sample(const BodySample&);
  int32_t dx = 0, dy = 0, dz = 0;   // the last sample minus gravity, milli-g; 0 until a steady sample
  int32_t squared() const { return dx * dx + dy * dy + dz * dz; }
 private:
  int32_t gx_ = 0, gy_ = 0, gz_ = 0;   // milli-g x 16
  int16_t px_ = 0, py_ = 0, pz_ = 0;   // the sample before
  uint8_t rest_ = 0;                   // samples running that barely moved
  bool primed_ = false;
};

struct MotionDetector : DetectorBase {    // | |a|-1g | -> motion; Shake = 5 jolts >= 0.8 g off gravity, each the other way, in 1 s; free fall 200 ms then impact -> Dropped
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private:
  enum class Fall : uint8_t { None, Falling, Free, Landing };
  Gravity grav_;
  uint32_t jolts_[5]{}; uint8_t joltCount_ = 0; uint32_t quietSince_ = 0; Fx energy_;
  int32_t lastX_ = 0, lastY_ = 0, lastZ_ = 0;   // the last jolt's d
  bool rattled_ = false; Fall fall_ = Fall::None; uint32_t fallSince_ = 0;
};
struct HeldDetector : DetectorBase {      // in-plane > 0.30 g for 400 ms -> held; PickedUp/PutDown; Cradle when still 3 s
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private:
  uint32_t heldSince_ = 0, stillSince_ = 0, lastCradle_ = 0, lastDisturb_ = 0;
  int32_t prevX_ = 0, prevY_ = 0, prevZ_ = 0;
  bool held_ = false, cradled_ = false, primed_ = false;
};
// Held upside down -> Hang: the no-button way into (and out of) fight mode,
// for a case that covers BOOT. The shell has already settled the pose
// (orient.h: 600 ms dwell, jolts and flat rejected); this detector adds the
// kid's intent on top, and it is forgiving on purpose: progress accumulates
// while he is upside down and steady, pauses while he is jolted, and drains
// at double speed when he is righted, so a wobbly small hand still lands it
// in about a second and a half instead of restarting from zero at every
// tremor. One Hang per hang: the progress must drain away before another
// can fire, so staying upside down in the lobby he just opened never
// closes it again. The hanging locus is the progress itself, 0 to 1. Only
// on edge (the face within 30 degrees of vertical) and only after he has
// been the right way up since boot: a badge lying slanted on a table, or
// switched on upside down, is no ask.
// Re-arming a pose gesture: after it fires, the same gesture waits until the
// badge has been the right way up (neither on a side nor upside down) and
// steady for kRearmMs. A badge a child lays on its side, or leans upside down
// against something, rests in that pose -- and near the on-edge line it can
// flicker in and out of it for hours -- yet it fires once, not again and again.
struct PoseRearm {
  static constexpr uint32_t kRearmMs = 400;
  bool armed() const { return armed_; }
  void spend() { armed_ = false, homeFor_ = 0; }
  void sample(const BodySample& s, bool steady, uint32_t dt) {
    const bool home = !s.sideDown && !s.invertedDown && steady;
    homeFor_ = home ? homeFor_ + dt : 0;
    if (homeFor_ >= kRearmMs) armed_ = true;
  }
 private:
  uint32_t homeFor_ = 0;
  bool armed_ = false;
};

struct HangDetector : DetectorBase {
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private: uint32_t progress_ = 0, lastMs_ = 0; bool hung_ = false, primed_ = false; PoseRearm rearm_;
};
// Turned onto a side and held there -> the button held (controls.h): the
// no-button twin of a BOOT hold, as the hang is the double press's. Held on
// edge and steady, a quarter turn from upright, it is a level (holding()), as
// BOOT down is: the Dish times the 3 s holds (Finish, the Mortal pick, the
// hug) and fills the rim's ring by it, and once it has lasted kButtonHoldMs
// it fires ButtonHold, once. A turn let go sooner does nothing at all (a
// release is never a press: the press's twin is the knock knock). Like the
// hang, only after he has been the right way up: one hold per turn, and a
// badge switched on sideways, or kept sideways, holds nothing.
struct SideHoldDetector : DetectorBase {
  void sample(const BodySample&, uint32_t ms, SenseOut&);
  bool holding() const { return holding_; }
  uint32_t heldMs(uint32_t ms) const { return holding_ ? ms - since_ : 0; }
 private: uint32_t since_ = 0, offSince_ = 0, lastMs_ = 0; bool holding_ = false, fired_ = false, off_ = false, primed_ = false; PoseRearm rearm_;
};
struct KnockDetector : DetectorBase {     // tapCode -> Knock / DoubleKnock (+ Button, its twin: controls.h), 400 ms rate limit; not just after the road pushed the whole body up
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private: Gravity grav_; uint32_t last_ = 0, bumpAt_ = 0; bool heavy_ = false, bumped_ = false;
};
// A second short press that goes down within this of the first one's release
// is ButtonDouble instead of Button: the duel's "ready" (BATTLES.md, "BOOT,
// everywhere"). Where a double press means nothing, the Dish counts it as a
// press again (boot.h), so tapping through a clutch is not slowed. v2's U3
// replaces this detector with the game wheel's. The first press still feeds
// at once; only the second waits on the window. 400 ms is the owner's
// (2026-10-16: "2 presses with the same duration of 0.4s").
inline constexpr uint32_t kDoublePressMs = 400;
inline constexpr uint32_t kButtonHoldMs = 1200;   // a press held this long is ButtonHold, not a press

// GPIO0 is read raw: a release counts once BOOT has stayed up this long (two
// 50 Hz samples), so a contact bounce caught by one sample never splits a
// press in two. The release is dated from the first up sample.
inline constexpr uint32_t kButtonSettleMs = 15;

struct ButtonDetector : DetectorBase {    // release < 1.2 s -> Button (or ButtonDouble); reaching 1.2 s -> ButtonHold
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private:
  uint32_t downSince_ = 0, lastRelease_ = 0, upAt_ = 0;
  bool down_ = false, held_ = false, second_ = false, released_ = false, upPending_ = false;
};
struct LidDetector : DetectorBase {       // az < -0.8 g and still for 30 s -> lidded, LidDown; righting -> LidUp
  void sample(const BodySample&, uint32_t ms, SenseOut&);
  bool lidded() const { return lidded_; }
 private: Gravity grav_; uint32_t since_ = 0; bool lidded_ = false;
};
struct WarmthDetector : DetectorBase {    // die temperature vs a slow baseline -> warmth; unknown -> 0
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private: int32_t baseline_ = 0; bool known_ = false;
};
struct TouchDetector : DetectorBase {     // touch locus, Touched; silent on the 1.28
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private: bool was_ = false;
};
struct DayDetector : DetectorBase {       // day_sin/day_cos/light from PetClock; Dusk/Dawn edges
  void tick(const TickContext&, SenseOut&);
 private: bool wasNight_ = false, primed_ = false;
};
struct OwnerDetector : DetectorBase {     // owner_near; OwnerArrived/OwnerLeft edges
  void tick(const TickContext&, SenseOut&);
 private: bool was_ = false;
};

// Every detector, generated from defs/senses.def. Adding a sense is a struct
// above plus one row; there is no list to keep in step.
struct Detectors {
#define BLORB_SENSE(member, Type) Type member;
#include "blorb/defs/senses.def"
#undef BLORB_SENSE
  void sample(const BodySample& s, uint32_t ms, SenseOut& out) {
#define BLORB_SENSE(member, Type) member.sample(s, ms, out);
#include "blorb/defs/senses.def"
#undef BLORB_SENSE
  }
  void tick(const TickContext& ctx, SenseOut& out) {
#define BLORB_SENSE(member, Type) member.tick(ctx, out);
#include "blorb/defs/senses.def"
#undef BLORB_SENSE
  }
};

}  // namespace blorb
