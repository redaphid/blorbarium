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

// Decoded from the MetabolismGene so the habitat never reads genes (food.h
// has the scales).
struct HabitatRules {
  uint8_t pantrySize;      // 1..food::kMaxPantry
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
  // pantry. food_near is the nearest pellet he would eat: a rotten one only
  // when `rotIsFood` (he is starving, creature.h kEatsRotAt), so a pet that
  // turns rot down never learns that food near him fails to feed him.
  // food_rotten is 1 while the nearest pellet of all is rotten.
  void step(const HabitatRules&, Fx tiltX, Fx tiltY, DishPos creature, uint32_t tick, SenseOut&,
            bool rotIsFood = true);
  bool dropPellet(const HabitatRules&, Rng&, uint32_t tick, SenseOut&);   // false when the pantry is empty
  // A snack he turned up himself (food.h, foraging): a fresh pellet at `at`,
  // not from the pantry. A full dish gives up its oldest, as a drop does.
  void placeFound(DishPos at, uint32_t tick);
  // The pantry gains up to `n`, as far as it has room; how many it took. The
  // way in for coming home and, later, a fight's spoils or a friend's gift.
  uint8_t give(const HabitatRules&, uint8_t n);
  // The pantry loses up to `n`, as far as it has them; how many it lost (a
  // lost fight's spoils, #46). The refill's clock runs on as it was.
  uint8_t take(uint8_t n);
  // Ticks until the next pellet comes back; 0 while the pantry is full.
  uint32_t refillLeft(const HabitatRules&, uint32_t tick) const;
  struct Bite { Fx food; bool rotten; };
  // The nearest pellet in reach, eaten; a rotten one only when `rotIsFood`.
  std::optional<Bite> bite(const HabitatRules&, DishPos creature, uint32_t tick, bool rotIsFood = true);
  std::optional<DishPos> nearestPellet(DishPos from) const;
  // The nearest he would eat (fresh, or any when `rotIsFood`), and the nearest rotten one.
  std::optional<DishPos> nearestFood(const HabitatRules&, DishPos from, uint32_t tick, bool rotIsFood) const;
  std::optional<DishPos> nearestRotten(const HabitatRules&, DishPos from, uint32_t tick) const;
  uint32_t hash() const;
};

}  // namespace blorb
