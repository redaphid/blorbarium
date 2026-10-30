#pragma once
// Carried about, as the eyes see it. The owner (2026-10-21): "I want the
// character to have some animations if the user is moving it around
// sometimes." The badges ride in pockets, hands and cars, and until now he
// only knew of it through the engine's senses (the motion locus, a shake, a
// drop, a pick-up), which move his mood and never his body on the glass.
//
// This watches the same 50 Hz samples the detectors read and, now and then,
// starts a short reaction for the screen to play:
//
//   Brace    always, a level: he leans and slides against the badge's
//            acceleration (a car taking a turn, a hand jerking him along),
//            past a dead zone, so a still hand shows nothing;
//   Cling    a bumpy ride (a car, a run): he hunkers down, alarmed, shivering,
//            sweat flying off him;
//   Dizzy    after a spin (a full turn of the badge in a hand, without a
//            pause) or a long shake: spiral eyes, stars round his head, a
//            drunk sway, his tongue out;
//   Jelly    after a lone jolt (set down hard, a pothole): he wobbles like
//            jelly, squash and stretch along the jolt;
//   Whee     a smooth swing (on an arm, a lanyard): he rides it, happy,
//            lifted at the ends of the swing, with speed lines and a WHEE!;
//   Glance   carried at a walk: he looks about, nervous (alarmed, a sweat
//            drop) or curious (happy), as his kind is;
//   Settle   set down still after a ride: a big yawn, a stretch and a sigh.
//
// Sometimes, not on every jiggle: each needs its motion to last (a ride,
// steps, a swing), rolls a seeded chance against his temperament (the
// species' character, personalities/*.txt, jittered by his genome, so two
// frogs differ), and has its own cooldown and a shared one. Never in the way
// of a gesture: nothing starts while a knock is under way (KnockSense's
// flags), while he is turned onto a side or upside down (the poses, the side
// hold, the hang's charge), or soon after, and a reaction playing is cut
// short the moment one begins. The detectors' thresholds are untouched: this
// only reads their outputs.
//
// Presentation only, like FeedShow and the Thinker: never saved, never
// hashed, and nothing here reaches him (the engine already feels the motion
// itself, through the motion locus and the shake, drop and pick-up
// stimuli). The Dish samples it; the Appearance carries a CarryView; paint
// (carried.cpp) draws it on the screen's clock. Deterministic in the samples
// and his genome, so a film or a test replays it byte for byte.
#include <cstdint>

#include "blorb/ids.h"
#include "blorb/senses.h"

namespace blorb {

enum class Carry : uint8_t { None, Cling, Dizzy, Jelly, Whee, Glance, Settle };
inline constexpr uint8_t kCarryKinds = 7;
const char* carryName(Carry);
bool carryFromName(const char* name, Carry& out);

struct CarryView {
  static constexpr uint16_t kNever = UINT16_MAX;
  Carry react = Carry::None;
  uint16_t ageMs = kNever;       // since it began (paint/tween.h runs it at the screen's speed)
  uint16_t lenMs = 0;            // how long it plays from its start (a ride's reaction grows while the ride goes on)
  uint8_t flavour = 0;           // seeded: which way his eyes go first, nervous or curious, how many stars
  int8_t dirX = 0, dirY = 0;     // the motion's way when it began, -127..127, the screen's frame (a jolt's axis)
  uint8_t level = 0;             // how hard, 0..255: the jolt, the ride's bumps, the swing
  int16_t braceX = 0, braceY = 0;   // his lean against the acceleration now, 1/16 px, the screen's frame
};

// What the Dish knows on a sample that the sample does not say.
struct CarryContext {
  bool canReact = false;   // an awake creature at home in the dish: no egg, duel, fight mode, sleep or meal
  bool posing = false;     // a pose gesture is under way: turned onto a side or upside down, held or charging
  bool shook = false;      // the detectors fired a shake on this sample
  bool gestured = false;   // they fired a gesture's word on it: a press, a hold, a hang, a knock
  bool poseWord = false;   // ...a pose's word: a side hold's hold or a hang (a spin through them was no spin)
  uint8_t species = 0;     // SpeciesId: his temperament
  uint32_t seed = 0;       // his genome's hash: his own jitter on it, and every roll
};

class CarryShow {
 public:
  // Every 50 Hz sample, in the screen's frame (what the Dish feels).
  void sample(const BodySample& s, uint32_t ms, const CarryContext& c);
  CarryView view(uint32_t nowMs) const;
  // `DEBUG carry <kind>`: that reaction now, whatever the motion (a preview).
  void force(Carry, uint32_t ms, uint32_t seed);
  // How many of each have started since boot (tests, STATUS).
  uint16_t started(Carry k) const { return starts_[uint8_t(k) % kCarryKinds]; }
  Carry playing(uint32_t nowMs) const { return live(nowMs) ? react_ : Carry::None; }
  // What the last sample measured, for `DEBUG carry` and the tests' prints.
  struct Signals {
    int16_t jerk = 0, pull = 0, off = 0, push = 0;   // milli-g: the jerk's EMA, |a| - 1 g, off gravity, the brace's push
    uint16_t rideMs = 0, stillMs = 0;
    uint8_t steps = 0, swings = 0;
    int16_t spinDeg = 0;                              // the run of turning, degrees
    int16_t rough = 0;                                // the jerk averaged over ~1.3 s, milli-g
    uint16_t rattleMs = 0;
    uint8_t bumps = 0;
    bool turning = false, quiet = false;
    uint8_t dizzyWhy = 0;                             // the last dizzy spell's cause: 1 a spin, 2 two shakes, 3 a long shake
  };
  const Signals& signals() const { return sig_; }

  // Tunables, in milli-g and ms. Chosen on the recordings
  // (test/fixtures/hands_2026-10-19.txt) and test/support/road.h's rides.
  static constexpr int32_t kBraceDeadMg = 100;     // a hand's wobble and a road's buzz lean him nowhere
  static constexpr int32_t kBraceFullMg = 400;     // ...this far past it, the full lean
  static constexpr int32_t kBracePxQ4 = 7 * 16;    // the full lean: his stand point this far, his top twice it
  static constexpr int32_t kMoveJerkMg = 70;       // jerk (EMA) past this is a ride, a walk, handling
  static constexpr int32_t kBumpyJerkMg = 95;      // ...past this, averaged over 1.3 s, a bumpy one
  static constexpr int32_t kBumpMg = 450;          // off gravity this far: a bump
  static constexpr int32_t kJoltMg = 650;          // ...this far, in one step of kJoltJerkMg: a jolt
  static constexpr int32_t kJoltJerkMg = 400;
  static constexpr int32_t kStillJerkMg = 14;      // jerk under this, and off gravity under kStillMg: set down
  static constexpr int32_t kStillMg = 45;
  static constexpr uint32_t kQuietAfterGestureMs = 1500;   // nothing starts this soon after a knock or a pose
  static constexpr uint32_t kSpinTurnQ16 = 56000;  // a turn's 360 degrees is 65536: about 310 degrees ...
  static constexpr uint32_t kSpinStallMs = 500;    // ...turning on without a pause this long (a hold is a pause)
  static constexpr int32_t kRattleJerkMg = 220;    // a shake kept up this hard...
  static constexpr uint32_t kRattleMs = 1800;      // ...this long: dizzy
  static constexpr uint32_t kSettleRideMs = 9000;  // a ride this long, then still kSettleStillMs: a settle
  static constexpr uint32_t kSettleStillMs = 1500;

 private:
  bool live(uint32_t ms) const { return react_ != Carry::None && ms - startMs_ < lenMs_; }
  void start(Carry, uint32_t ms, uint16_t lenMs, uint32_t coolMs);
  bool cooled(Carry, uint32_t ms) const;
  uint32_t roll();   // seeded: his genome and the count of rolls so far
  void cut(uint32_t ms);

  struct Vec3 { int32_t x = 0, y = 0, z = 0; };
  static constexpr uint8_t kHist = 8, kTurnRing = 5;
  Vec3 hist_[kHist]{};        // the smoothed sample, the last kHist samples
  uint8_t histAt_ = 0;
  Vec3 turnRing_[kTurnRing]{};   // gravity every 250 ms: where it pointed a second ago
  uint8_t turnAt_ = 0;
  uint32_t turnSlot_ = 0;
  Vec3 joltPre_{};
  bool primed_ = false;
  int32_t slowX_ = 0, slowY_ = 0, slowZ_ = 0;   // gravity, slowly (x16)
  int32_t fastX_ = 0, fastY_ = 0, fastZ_ = 0;   // the sample, a little smoothed (x16): a knock's spike is not motion
  int32_t levelX_ = 0, levelY_ = 0;             // gravity in the plane, slower still (x16): the brace's level
  int16_t prevX_ = 0, prevY_ = 0, prevZ_ = 0;
  int32_t jerkQ4_ = 0;      // sample-to-sample change, EMA (x16)
  int32_t magQ4_ = 0;       // |sample| - 1 g, smoothed (x16): steps, a swing's pull
  int32_t braceX_ = 0, braceY_ = 0;   // 1/16 px
  uint32_t lastQuiet_ = 0;  // the newest sample a knock or a pose held everything off
  bool everQuiet_ = false;

  // The ride: how long it has been moving, and how long still.
  uint32_t rideMs_ = 0, calmMs_ = 0, stillMs_ = 0, lastBump_ = 0, lastRoll_ = 0;
  uint32_t prevBump_ = 0;
  int32_t roughQ4_ = 0;     // the jerk, averaged slower (x16): a road's roughness
  uint8_t bumps_ = 0;
  bool bumped_ = false;
  // Steps: the pull's rises, their rhythm.
  bool stepHigh_ = false;
  uint32_t lastStep_ = 0, stepGap_ = 0;
  uint8_t steps_ = 0;
  // A swing: the pull's heavy and light ends, alternating.
  int8_t swingSide_ = 0;
  uint32_t lastSwing_ = 0;
  uint8_t swings_ = 0;
  // A spin: the turn of gravity in the screen's plane, run on without a stall.
  bool angPrimed_ = false;
  int32_t lastAng_ = 0;     // 1/65536 turn
  int32_t spin_ = 0;        // signed run of turning
  uint32_t lastTurned_ = 0;
  // Shakes, for a dizzy spell: two within kShakesMs.
  uint32_t lastShake_ = 0, rattleMs_ = 0;
  bool hadShake_ = false;
  // A dizzy spell or a jelly wobble waits for the motion to end (and for a
  // knock or a gesture to show itself) before it plays.
  bool dizzyDue_ = false;
  uint32_t dizzyAt_ = 0, calmFor_ = 0;
  bool joltDue_ = false;
  uint32_t joltAt_ = 0;
  uint8_t jolts_ = 0;
  int8_t joltX_ = 0, joltY_ = 0;
  uint8_t joltLevel_ = 0;

  // The reaction playing.
  Carry react_ = Carry::None;
  uint32_t startMs_ = 0;
  uint16_t lenMs_ = 0;
  uint8_t flavour_ = 0, level_ = 0;
  int8_t dirX_ = 0, dirY_ = 0;
  uint32_t coolUntil_[kCarryKinds]{};
  uint32_t anyCoolUntil_ = 0;
  bool cooling_[kCarryKinds]{};
  uint32_t seed_ = 0, rolls_ = 0;
  uint8_t species_ = 0;
  uint16_t starts_[kCarryKinds]{};
  Signals sig_;
};

}  // namespace blorb
