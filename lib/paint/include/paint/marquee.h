#pragma once
// Text on a strip too narrow for it: claude-notification-screen's marquee
// (src/widgets.h), ported off LovyanGFX onto a Canvas240. Generic drawing,
// nothing about the creature, so it travels with a fork of lib/paint whole:
// bring canvas.h, text.h and a font.
//
// Like the sibling's arc gauge, it is a description plus a few functions that
// draw it: say where the strip is and how fast it moves, and the same element
// does a label that is true for an hour and a quip that is up for twelve seconds.
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

// Where a strip has got to. The caller owns it, one per strip rather than one
// per string: text that changes while it is going round keeps its position,
// because a label whose content ticks is not a new label.
//
// It also holds the text that is ON the strip, which is not always the text the
// caller last passed. What is on screen never changes under the reader: the copy
// going round finishes its lap as it was, and the newest text is what comes round
// behind it.
struct MarqueePhase {
  uint64_t offMilliPx;
  uint32_t lastMs;
  char shown[192];   // the copy crossing the strip now
  char next[192];    // the one following it; frozen once its head is in view
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

// Something that is TRUE: it goes round for as long as it is on screen.
void drawMarquee(Canvas240&, const Marquee&, MarqueePhase&, const Font&, const char*, uint16_t colour, uint32_t nowMs);
// The same for a line that never changes, `ms` after it went up, for a caller
// that keeps no state: its phase is the clock.
void drawMarqueeAt(Canvas240&, const Marquee&, const Font&, const char*, uint16_t colour, uint32_t ms);
// Nothing to say: the strip gives its phase back, so the next line that arrives
// starts from its own beginning instead of wherever the last one had got to.
void marqueeRest(MarqueePhase&);

// Something that is SAID: it arrives from the right, goes round twice, and is
// gone when `age` reaches `life`, so its phase is its age and it needs no state.
// The speed stretches so a long line still gets both passes inside its life.
// One that fits slides in and parks centred.
void drawMarqueeTimed(Canvas240&, const Marquee&, const Font&, const char*, uint16_t colour, uint32_t age,
                      uint32_t life);
// How long those two passes take, for a caller deciding how long to hold a line up.
uint32_t marqueeTimedDuration(const Marquee&, const Font&, const char*);

}  // namespace paint
