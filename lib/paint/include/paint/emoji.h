#pragma once
// Emoji drawn in code (there is no emoji font on the badge, and no image
// generation here), and the thought bubble they are shown in. Big and bold so
// a six-year-old reads them at arm's length on the 240 px round panel: about
// 36 px across at size 256, thick dark outlines, emoji colours.
//
// Feeding's empty pantry shows one (issue #18: the food he wants, a red NO
// over it, a sad face); the pet's inner state will too (#19). Pure: a
// function of its arguments, no clock of its own, so a frame is the same
// bytes every run.
#include <cstdint>
#include "paint/canvas.h"

namespace paint {

enum class Emoji : uint8_t {
  Drumstick,    // a turkey leg
  Cookie,       // the dish's food (feeding.cpp drawFood)
  Apple,
  Fish,
  EmptyPlate,   // white, a rim, two crumbs and nothing else
  EmptyJar,     // a cookie jar standing empty: glass, a red lid, crumbs at the bottom
  No,           // a red circle with a slash: drawn over another
  Sad,          // a yellow face, a frown
  Crying,       // the same, a tear running down (on its `ms` clock)
  Count,
};

// One emoji centred at (cx, cy), `size` Q8 of its full ~36 px, at `alpha`
// 0..256. `ms`: how long it has shown, for the ones that move (Crying's tear).
void drawEmoji(Canvas240&, Emoji, int cx, int cy, int size = 256, int alpha = 256, int ms = 0);

// A thought bubble: a cloud of puffs about 80 x 72 px at size 256, centred on
// (cx, cy), white with a bold outline, and two little puffs trailing toward
// (towardX, towardY), the head it comes from. Its emoji is drawn at (cx, cy).
void drawThought(Canvas240&, int cx, int cy, int towardX, int towardY, int size = 256, int alpha = 256);

// A little animation in a thought: beats, each an emoji that pops in at its
// time. A beat `over` the one before is drawn on top of it (NO over the food)
// and `slam`s down, big then its size, jolting the picture; any other beat
// replaces the one before. `ms` into the thought, which lasts `lifeMs` and
// fades out at its end; the bubble pops in over its first 150 ms. The bubble
// is drawn a little over size 256 (about 86 px), its emoji at ~42 px.
struct ThoughtBeat {
  Emoji emoji;
  uint16_t atMs;
  bool over = false;
  bool slam = false;
};
void drawThoughtFrames(Canvas240&, int cx, int cy, int towardX, int towardY, const ThoughtBeat* beats, int count, int ms,
                       int lifeMs);

// How big (Q8) a picture is `ms` after it pops in: out of nothing, past full, settling.
int popIn(int ms);

}  // namespace paint
