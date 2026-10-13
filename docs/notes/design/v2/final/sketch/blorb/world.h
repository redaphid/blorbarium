#pragma once
// v2 FINAL SKETCH. The dish's physical world: what habitat.h becomes.
//
// The marble is a ball in a shallow bowl, moved by the accelerometer's raw
// in-plane reading. A ball in a box feels the box's specific force, which is
// what the IMU measures, so tilting rolls it, a knock flicks it, a shake
// rattles it and a swing sloshes it, all from one rule. It integrates at the
// 50 Hz sample rate: Dish::sample appends to a SampleTrace and World::step
// drains it as substeps inside Dish::tick, so every world write happens in
// tick order and replays bit for bit.
//
// The world owns everything that outlives one grungo: the pieces, the fly
// swarm (an arms race against his line) and the Bond (the dish knows you).
#include <cstdint>
#include "blorb/fixed.h"
#include "blorb/senses.h"

namespace blorb {

struct DishPos { Fx x, y; };

// The tick's raw samples, in order.
struct SampleTrace {
  struct Accel { int16_t ax, ay; };   // milli-g, device frame. Gyro joins when unit 20 confirms it reads.
  Accel s[8]{};
  uint8_t n = 0;
};

// The dish is a unit disc; time in seconds; semi-implicit Euler at 20 ms. A
// 15 degree tilt rolls the marble across in about 0.8 s, and a level dish
// settles it near the centre in about 8 s (candidate B's arithmetic).
struct BallPhysics {
  static constexpr Fx kG = Fx::ratio(24, 1);              // units/s^2 per 1 g in-plane, 5/7 rolling included
  static constexpr Fx kBowl = Fx::ratio(1, 1);            // centring pull per unit radius
  static constexpr Fx kRollDrag = Fx::ratio(3, 10);       // viscous, per s
  static constexpr Fx kRollFriction = Fx::ratio(15, 100); // Coulomb, units/s^2
  static constexpr Fx kRim = Fx::ratio(92, 100);
  static constexpr Fx kRimRestitution = Fx::ratio(6, 10);
  static constexpr Fx kBodyRestitution = Fx::ratio(5, 10);
  static constexpr Fx kBonkSpeed = Fx::ratio(3, 10);      // relative speed for a marble_hit bonk
  static constexpr Fx kDt = Fx::ratio(1, 50);
  static constexpr uint32_t kDeskBaselineMs = 30000;      // a still device reads its own slope as level
};

struct Marble { DishPos at; Fx vx, vy; };
struct Pellet { DishPos at; Fx vx, vy; uint32_t droppedTick; bool present; };   // heavy: slides only past ~10 degrees

struct Fly {
  DishPos at; Fx vx, vy;
  uint8_t speed, jink, wary;      // the fly genome: cruise speed, dodge chance at a strike, flee radius
  uint8_t tint;                   // 0 = swamp green; the camera twist's flies carry a hue
  uint16_t ageMinutes;
  bool alive;
};

struct Swarm {
  static constexpr uint8_t kMax = 6;
  Fly flies[kMax]{};
  uint32_t nextBreedTick = 0;
  uint16_t generation = 0;        // fly generations bred in this dish
  uint8_t alive() const;
  Fx meanJink() const;            // the arms-race readout the e2e scenario measures
};

// The owner print (candidate A's Bond, kept in the world as B kept its hands
// signature). Eight gestures: knock, double knock, shake, cradle, bell, feed
// hold, tilt play, drop. A session is the gestures since the last quiet
// minute; `familiar` is its cosine against the print.
struct Bond {
  Q15 print[8]{};                 // long-run gesture mix
  uint16_t session[8]{};
  uint16_t usualGapMinutes = 240;
  uint32_t lastContactTick = 0;
  // Writes familiar and since_contact; fires reunion (a gap over 1.5x the
  // usual one, powered or caught up) and strange_hands (a session of 6 or
  // more gestures with familiar under 0.4).
  void observe(const SenseOut& routed, uint32_t tick, Fx loci[256], SenseOut& out);
};

// Decoded from the MetabolismGene as today, plus the flies.
struct WorldRules {
  uint8_t pantrySize; uint32_t refillTicks; uint32_t rotTicks; Fx biteSize;
  uint32_t flyFromRotTicks;       // a pellet rotten this long hatches a fly
  uint32_t flyBreedTicks;         // two live flies and food in the dish breed one fly this often
};

class World {
 public:
  static constexpr uint8_t kMaxPellets = 6;
  static constexpr DishPos kBellStation{Fx::zero(), Fx::ratio(-85, 100)};   // 6 o'clock: bell, jar pips, drop point
  Pellet pellets[kMaxPellets]{};
  Marble marble{};
  Swarm swarm{};
  Bond bond{};
  uint8_t pantry = 0;
  uint32_t pantryTick = 0;

  // One tick: the trace's substeps (marble, pellets), then refill, rot, fly
  // life, and the loci food_near, food_rotten, marble_near, marble_coming,
  // fly_near, pantry, tilted. Fires marble_hit, fly_hatched.
  void step(const WorldRules&, const SampleTrace&, DishPos him, Fx hisRadius, uint32_t tick, Rng&, SenseOut&);
  // BOOT hold while a creature lives: a pellet drops by the bell (fires
  // pellet_dropped), or the empty jar rattles (no stimulus) and shows a clock.
  bool dropPellet(const WorldRules&, uint32_t tick, SenseOut&);
  // The camera twist: a fly of that hue buzzes in from the rim.
  bool spawnFly(uint8_t tint, Rng&);
  // His tongue: hits the fly nearest `aim` if within `width` and the fly fails
  // its dodge. Fires fly_caught or fly_missed; the miss comes back for lead learning.
  struct Strike { bool caught; Fx food; uint8_t tint; DishPos miss; };
  Strike strike(DishPos aim, Fx width, Rng&, SenseOut&);
  struct Bite { Fx food; bool rotten; };
  bool bite(const WorldRules&, DishPos him, uint32_t tick, Bite& out);
  uint32_t hash() const;
};

}  // namespace blorb
