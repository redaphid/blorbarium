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
  for (; *s; ++s) {
    const Glyph& g = glyphFor(f, *s);
    const uint8_t* bits = f.bitmap + g.bitmapOffset;
    const int x0 = pen + g.xOffset, y0 = baseline + g.yOffset;
    for (int i = 0; i < g.width * g.height; ++i)
      if (bits[i >> 3] & (0x80 >> (i & 7))) plot(cv, x0 + i % g.width, y0 + i / g.width, colour, clip);
    pen += g.xAdvance;
  }
}

namespace {
// Floor division by 256, for pen positions left of the canvas too.
int floorQ8(int64_t v) { return int(v >= 0 ? v >> 8 : -((-v + 255) >> 8)); }
}  // namespace

int textWidthScaled(const Font& f, const char* s, int scaleQ8) {
  return int((int64_t(textWidth(f, s)) * scaleQ8 + 255) >> 8);
}

void drawStringScaled(Canvas240& cv, const Font& f, const char* s, int x, int y, uint16_t colour, const Clip& clip,
                      int scaleQ8) {
  if (!*s || scaleQ8 <= 0) return;
  const VerticalMetric m = metricOf(f);
  const int64_t baseline = int64_t(y) * 256 - int64_t(m.height) * scaleQ8 / 2 + int64_t(m.ascent) * scaleQ8;
  const Glyph& first = glyphFor(f, *s);
  int64_t pen = int64_t(x) * 256 + (first.xOffset < 0 ? -int64_t(first.xOffset) * scaleQ8 : 0);
  const int cx0 = imax(clip.x, 0), cx1 = imin(clip.x + clip.w, kSide);
  const int cy0 = imax(clip.y, 0), cy1 = imin(clip.y + clip.h, kSide);
  for (; *s; ++s) {
    const Glyph& g = glyphFor(f, *s);
    const int64_t x0 = pen + int64_t(g.xOffset) * scaleQ8, y0 = baseline + int64_t(g.yOffset) * scaleQ8;
    pen += int64_t(g.xAdvance) * scaleQ8;
    if (floorQ8(x0 + int64_t(g.width) * scaleQ8) < cx0 || floorQ8(x0) >= cx1) continue;   // off the glass
    const uint8_t* bits = f.bitmap + g.bitmapOffset;
    for (int i = 0; i < g.width * g.height; ++i) {
      if (!(bits[i >> 3] & (0x80 >> (i & 7)))) continue;
      const int ix = i % g.width, iy = i / g.width;
      const int xa = imax(floorQ8(x0 + int64_t(ix) * scaleQ8), cx0), xb = imin(floorQ8(x0 + int64_t(ix + 1) * scaleQ8), cx1);
      const int ya = imax(floorQ8(y0 + int64_t(iy) * scaleQ8), cy0), yb = imin(floorQ8(y0 + int64_t(iy + 1) * scaleQ8), cy1);
      for (int py = ya; py < yb; ++py)
        for (int px = xa; px < xb; ++px) plot(cv, px, py, colour, clip);
    }
  }
}

}  // namespace paint
