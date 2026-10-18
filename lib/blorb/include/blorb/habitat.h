#pragma once
// The habitat: what is in the dish besides the creature. It is what makes the
// toy complete without a phone. BOOT drops a pellet from a pantry that refills
// on the pet clock, pellets roll with tilt, and a pellet left too
// long rots. Simulated, not drawn: it exposes positions for the presentation
// and writes loci and stimuli for the senses.
//
// Dish coordinates: a unit disc, Fx x/y in [-1, 1], radius 1 = the rim.
#include <cstdint>
#include <optional>
#include "blorb/fixed.h"
#include "blorb/senses.h"

namespace blorb {

struct DishPos { Fx x, y; };
struct Pellet { DishPos at; uint32_t droppedTick; bool present; };
// Format ballast only: format 1's habitat chunk carries a marble's position
// and velocity, and format 1 still writes what it read, so the words ride
// along in the save untouched. Nothing simulates, chases or draws a marble
// any more; a format 2 retires this.
struct Marble { DishPos at; Fx vx, vy; };

// Decoded from the MetabolismGene so the habitat never reads genes.
struct HabitatRules {
  uint8_t pantrySize;
  uint32_t refillTicks;    // one pellet back every N ticks
  uint32_t rotTicks;       // older than this, eating it fires FedBad
  Fx biteSize;             // food added per bite
};

class Habitat {
 public:
  static constexpr uint8_t kMaxPellets = 6;
  Pellet pellets[kMaxPellets]{};
  Marble marble{};   // format ballast (above); never simulated
  uint8_t pantry = 0;
  uint32_t pantryTick = 0;

  // Each tick: refill, rot, roll by tilt; writes food_near, food_rotten,
  // pantry.
  void step(const HabitatRules&, Fx tiltX, Fx tiltY, DishPos creature, uint32_t tick, SenseOut&);
  bool dropPellet(const HabitatRules&, Rng&, uint32_t tick, SenseOut&);   // false when the pantry is empty
  struct Bite { Fx food; bool rotten; };
  std::optional<Bite> bite(const HabitatRules&, DishPos creature, uint32_t tick);
  std::optional<DishPos> nearestPellet(DishPos from) const;
  uint32_t hash() const;
};

}  // namespace blorb
