#pragma once
// Inside lib/paint: the idle layer. With the engine's pose changing only on
// its 100 ms ticks, a pet at rest held one pose for seconds and read as
// frozen. This is a second, small motion on top of whatever the tick says,
// driven by the screen's clock (Appearance::liveMs, paint/tween.h) and his
// genome (lifeSeed), so it is the same bytes for the same (genome, time) and
// never touches the engine: breathing, a sway from the feet, a bob, his eyes
// drifting, and blinks. Each species has its own tempo and depth, and the pose
// colours it: a waddle walking, a bounce hopping in circles, a chew eating,
// slow deep breaths asleep. Drawn only when Appearance::lively is set.
#include <cstdint>
#include "blorb/appearance.h"

namespace paint {

struct Lively {
  int breathQ4 = 0;   // how much taller he stands this frame, 1/16 px (his breath)
  int lift = 0;       // px above his stand point (a bob, a bounce)
  int swayQ4 = 0;     // how far his top leans toward +x, 1/16 px (sheared from his feet)
  int gazeX = 0;      // his face, a pixel to a side, or 0
  bool blink = false;
};

// The idle layer for `a` this frame. `maxLift` is how high he may rise and stay on the glass.
Lively livelyOf(const blorb::Appearance& a, int maxLift);

// The egg's rock on the screen's clock: the angle of eggShear's swing, 1/64 turn and 1/256s of one.
int livelyRockQ8(const blorb::Appearance& a);

}  // namespace paint
