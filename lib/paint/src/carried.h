#pragma once
// Inside lib/paint: carried about (blorb/carry_show.h), drawn. What the
// CarryView asks of his body (a lean, a slide, squash and stretch, a lift,
// where his eyes look, the face he wears) for draw.cpp to fold into the frame
// with the idle layer's, and the props drawn over him (stars, spiral eyes,
// sweat, speed lines, WHEE!, a sigh). Every bit of it is a function of the
// Appearance: the CarryView's ages run on the screen's clock (paint/tween.h),
// so the same samples and genome draw the same bytes.
#include <cstdint>
#include "blorb/appearance.h"
#include "paint/canvas.h"

namespace paint {

struct Carried {
  int dxQ4 = 0, dyQ4 = 0;       // his stand point moved, 1/16 px
  int shearQ4 = 0;              // his top leaning toward +x, 1/16 px (on top of the idle sway)
  int wide = 4096, tall = 4096; // squash and stretch, Q12
  int lift = 0;                 // px above his stand point
  int gaze = 0;                 // his face this many px to a side
  bool face = false;            // he wears `expr` for it, unless his eyes glow (a foresight wins)
  blorb::ExprId expr{};
  bool active = false;          // anything at all: draw.cpp skips the props otherwise
};

// What the CarryView asks of him this frame. Nothing unless he is a creature
// drawn on the screen's clock (Appearance::lively), awake, and no reflex runs
// (a hop or a flinch is the engine's own reaction, and wins).
Carried carriedOf(const blorb::Appearance& a);

// Where draw.cpp put him this frame, for the props.
struct CarrySpots {
  int x = 0, y = 0;     // his stand point, canvas px
  int top = 0;          // the top of his body as drawn
  int halfW = 0;        // half his body's width as drawn
  uint8_t eyes = 0;     // the eyes of the face shown, canvas px, and their radius
  int ex[2]{}, ey[2]{}, er[2]{};
  bool glowing = false; // his eyes glow (foresee): no spirals over them
};

// The props, over him and before the round mask.
void drawCarried(Canvas240&, const blorb::Appearance&, const CarrySpots&);

}  // namespace paint
