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
  void set(LocusId, Fx);   // the last write to a locus in a tick wins
  void fire(StimId);       // a repeat of the same id in one tick merges, so a doubled delivery is a no-op
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
struct MotionDetector : DetectorBase {    // | |a|-1g | -> motion; Shake = 4 jolts >= 0.55 g in 900 ms; free fall then impact -> Dropped
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private:
  enum class Fall : uint8_t { None, Falling, Free, Landing };
  uint32_t jolts_[4]{}; uint8_t joltCount_ = 0; uint32_t quietSince_ = 0; Fx energy_;
  bool rattled_ = false; Fall fall_ = Fall::None; uint32_t fallSince_ = 0;
};
struct HeldDetector : DetectorBase {      // in-plane > 0.30 g for 400 ms -> held; PickedUp/PutDown; Cradle when still 3 s
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private: uint32_t heldSince_ = 0, stillSince_ = 0, lastCradle_ = 0; bool held_ = false, cradled_ = false;
};
struct KnockDetector : DetectorBase {     // tapCode -> Knock / DoubleKnock, 400 ms rate limit
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private: uint32_t last_ = 0;
};
struct ButtonDetector : DetectorBase {    // release < 1.2 s -> Button; reaching 1.2 s -> ButtonHold
  void sample(const BodySample&, uint32_t ms, SenseOut&);
 private: uint32_t downSince_ = 0; bool down_ = false, held_ = false;
};
struct LidDetector : DetectorBase {       // az < -0.8 g for 2 s -> lidded, LidDown; righting -> LidUp
  void sample(const BodySample&, uint32_t ms, SenseOut&);
  bool lidded() const { return lidded_; }
 private: uint32_t since_ = 0; bool lidded_ = false;
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
