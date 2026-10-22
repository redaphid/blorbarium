#include "paint/text.h"

namespace paint {
namespace {

constexpr int kSide = 240;

int imin(int a, int b) { return a < b ? a : b; }
int imax(int a, int b) { return a > b ? a : b; }

// A character the font lacks takes a space's room and draws nothing, as LovyanGFX does.
const Glyph& glyphFor(const Font& f, char c) {
  const uint8_t u = uint8_t(c);
  return f.glyphs[u >= f.first && u <= f.last ? u - f.first : 0];
}

struct VerticalMetric { int ascent, height; };

VerticalMetric metricOf(const Font& f) {
  int ascent = 0, descent = 0;
  for (int i = 0; i <= f.last - f.first; ++i) {
    ascent = imax(ascent, -f.glyphs[i].yOffset);
    descent = imax(descent, f.glyphs[i].height + f.glyphs[i].yOffset);
  }
  return {ascent, ascent + descent};
}

void plot(Canvas240& cv, int x, int y, uint16_t colour, const Clip& clip) {
  if (x < clip.x || x >= clip.x + clip.w || y < clip.y || y >= clip.y + clip.h) return;
  if (x < 0 || x >= kSide || y < 0 || y >= kSide) return;
  int alpha = 256;
  if (clip.feather > 0) alpha = imin(256, imin(x - clip.x, clip.x + clip.w - 1 - x) * 256 / clip.feather);
  uint16_t& px = cv.px[y * kSide + x];
  px = alpha >= 256 ? colour : blend(px, colour, alpha);
}

}  // namespace

int textWidth(const Font& f, const char* s) {
  int left = 0, right = 0;
  for (; *s; ++s) {
    const Glyph& g = glyphFor(f, *s);
    if (left == 0 && right == 0 && g.xOffset < 0) left = right = -g.xOffset;
    right = left + imax(g.xAdvance, g.width + g.xOffset);
    left += g.xAdvance;
  }
  return right;
}

void drawString(Canvas240& cv, const Font& f, const char* s, int x, int y, uint16_t colour, const Clip& clip) {
  if (!*s) return;
  const VerticalMetric m = metricOf(f);
  const int baseline = y - (m.height >> 1) + m.ascent;
  const Glyph& first = glyphFor(f, *s);
  int pen = x + (first.xOffset < 0 ? -first.xOffset : 0);
  // What plot() would keep: inside the clip and the canvas. A marquee's line
  // runs far past its strip, so each glyph is walked only where it can land.
  const int cx0 = imax(clip.x, 0), cx1 = imin(clip.x + clip.w, kSide), cy0 = imax(clip.y, 0), cy1 = imin(clip.y + clip.h, kSide);
  for (; *s; ++s) {
    const Glyph& g = glyphFor(f, *s);
    const uint8_t* bits = f.bitmap + g.bitmapOffset;
    const int x0 = pen + g.xOffset, y0 = baseline + g.yOffset;
    pen += g.xAdvance;
    const int c0 = imax(0, cx0 - x0), c1 = imin(int(g.width), cx1 - x0), r0 = imax(0, cy0 - y0), r1 = imin(int(g.height), cy1 - y0);
    for (int r = r0; r < r1; ++r)
      for (int c = c0, i = r * g.width + c0; c < c1; ++c, ++i)
        if (bits[i >> 3] & (0x80 >> (i & 7))) plot(cv, x0 + c, y0 + r, colour, clip);
  }
}

}  // namespace paint
