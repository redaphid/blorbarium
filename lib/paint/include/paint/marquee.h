#pragma once
// Text on a strip too narrow for it: claude-notification-screen's marquee
// (src/widgets.h), ported off LovyanGFX onto a Canvas240. Generic drawing,
// nothing about the creature, so it travels with a fork of lib/paint whole:
// bring canvas.h, text.h and a font.
//
// Like the sibling's arc gauge, it is a description plus a few functions that
// draw it: say where the strip is and how fast it moves. The sibling's stateful
// and timed strips never found a caller here and were left behind; the ring and
// the clock-driven strip are what the dish draws with.
//
// The text is a ring. It goes round with its head following its tail half a
// strip behind, so there is always something moving and the end of a long line
// is as readable as its start. A line that fits does not move at all.
//
// Every time here is elapsed real milliseconds from the caller. Hang it off a
// simulation counter instead and the line freezes whenever that counter does.
#include <cstdint>
#include "paint/canvas.h"
#include "paint/text.h"

namespace paint {

constexpr int kMarqueeCX = 120, kMarqueeCY = 120;
// The sibling clips to its session ring's inner edge. Nothing rings this
// glass, so the inner edge is the glass.
constexpr int kRingIn = 120;

struct Marquee {
  int dy;           // the strip's row, as an offset from the centre
  int half;         // half its width; 0 means the chord of the ring at that row
  int clipH;
  int pxPerS;
  int feather = 0;  // px at either end over which the ink fades in; 0 is the sibling's hard clip
};

// The usable half-width of a row `dy` from the centre on a round display.
int ringHalfWidth(int dy);
int marqueeHalf(const Marquee&);
bool marqueeFits(const Marquee&, const Font&, const char*);

// The ring itself, `off` pixels round. `lead` is where the head sits at zero:
// 0 starts it flush left, already readable; a full strip width starts it off
// the right edge, so the line arrives rather than appears. `follow` is what
// comes round behind `s`: the same string, unless the text is changing between laps.
void drawMarqueeRing(Canvas240&, const Marquee&, const Font&, const char* s, const char* follow, uint16_t colour,
                     int lead, uint32_t off);

// A line that never changes, `ms` after it went up, for a caller that keeps no
// state: its phase is the clock.
void drawMarqueeAt(Canvas240&, const Marquee&, const Font&, const char*, uint16_t colour, uint32_t ms);

}  // namespace paint
