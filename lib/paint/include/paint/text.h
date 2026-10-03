#pragma once
// Bitmap text on a Canvas240, the way LovyanGFX draws an Adafruit GFX font at
// text size 1, so a widget ported from a LovyanGFX sprite lands on the same
// pixels. Plain C++: the native suite and the sim build it too.
#include <cstdint>
#include "paint/canvas.h"

namespace paint {

// Adafruit GFX's font format, field for field, so fontconvert output and the
// fonts LovyanGFX ships paste in unchanged.
struct Glyph {
  uint16_t bitmapOffset;
  uint8_t width, height, xAdvance;
  int8_t xOffset, yOffset;   // from the pen and the baseline to the bitmap's top left
};
struct Font {
  const uint8_t* bitmap;     // each glyph width * height bits, row-major, MSB first
  const Glyph* glyphs;
  uint8_t first, last, yAdvance;
};

extern const Font kFreeSans9pt7b;

// Where text may land. Pixels outside are left alone; `feather` px at either
// end of the rect ramp the ink in from nothing, so a line running into the
// round glass fades there instead of ending on a straight cut.
struct Clip { int x, y, w, h, feather; };

int textWidth(const Font&, const char*);
// LovyanGFX's middle_left datum: x is the pen's start, y the middle of the
// font's full height (tallest ascent to deepest descent).
void drawString(Canvas240&, const Font&, const char*, int x, int y, uint16_t colour, const Clip&);

}  // namespace paint
