#pragma once
// Jumps through a life, shared by the serial `DEBUG stage` (src/debug_verbs.h)
// and the debug link (console.h). Each one runs real ticks of the Dish and
// nudges only what his own genes then act on, so the state it leaves is one
// the engine itself could have reached, and it saves cleanly.
#include <cstdint>
#include <functional>
#include "blorb/dish.h"

namespace debugble {

// Runs that many 100 ms ticks of the Dish now, faster than real time.
using Warp = std::function<void(uint32_t ticks)>;

enum class Jump : uint8_t {
  Done,
  NoEgg,         // HATCH with no egg in the dish
  NoCreature,    // a clutch waits for a pick
  PastStage,     // he is already past that stage
  Died,          // his life ran out on the way (the clutch is in the dish now)
};

// The egg hatches now: it incubates tick by tick until it does.
Jump hatch(blorb::Dish&, const Warp&);
// Hatches any egg, then ages him into `stage` by lowering life a little at a
// time and letting his own stage receptors fire, whatever thresholds his
// genes carry. Then every drive is settled (fed, rested, unhurt), a few ticks
// let his face catch up, and the Dish saves.
Jump growTo(blorb::Dish&, blorb::Stage, const Warp&);

}  // namespace debugble
