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
//
// Playing an idle (Appearance::idle, blorb/idle.h), the idle's own motion
// takes over from the sway and the bob: a dance's wiggle, a dig's squash, a
// stretch up tall, a roll onto his back, his eyes up at a daydream. Its prop
// (the notes, the pebble, the dirt) is idle_props.cpp's.
#include <cstdint>
#include "blorb/appearance.h"

namespace paint {

struct Lively {
  int breathQ4 = 0;   // how much taller he stands this frame, 1/16 px (his breath)
  int lift = 0;       // px above his stand point (a bob, a bounce)
  int swayQ4 = 0;     // how far his top leans toward +x, 1/16 px (sheared from his feet)
  int gazeX = 0;      // his face, a pixel to a side, or 0
  int gazeY = 0;      // his face, a pixel up (-1, a daydream, the stars) or down (+1, nose to the floor)
  int wide = 0;       // 1/1024s of his width more (an idle's squash, a lean in at the glass)
  int tall = 0;       // 1/1024s of his height more (a stretch), or less (a dig, on his back)
  bool blink = false;
};

// The idle layer for `a` this frame. `maxLift` is how high he may rise and stay on the glass.
Lively livelyOf(const blorb::Appearance& a, int maxLift);

// The egg's rock on the screen's clock: the angle of eggShear's swing, 1/64 turn and 1/256s of one.
int livelyRockQ8(const blorb::Appearance& a);

}  // namespace paint
