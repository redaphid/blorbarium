#include "paint/marquee.h"

namespace paint {
namespace {

constexpr Clip kWholeCanvas = {0, 0, 240, 240, 0};

int isqrt(uint32_t v) {
  uint32_t r = 0, bit = uint32_t(1) << 30;
  while (bit > v) bit >>= 2;
  while (bit) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
    bit >>= 2;
  }
  return int(r);
}

Clip stripClip(const Marquee& m, int half) {
  return {kMarqueeCX - half, kMarqueeCY + m.dy - m.clipH / 2, half * 2, m.clipH, m.feather};
}

void drawCentred(Canvas240& cv, const Marquee& m, const Font& f, const char* s, uint16_t colour) {
  drawString(cv, f, s, kMarqueeCX - (textWidth(f, s) >> 1), kMarqueeCY + m.dy, colour, kWholeCanvas);
}

}  // namespace

int ringHalfWidth(int dy) { return isqrt(uint32_t(kRingIn * kRingIn - dy * dy)) - 6; }

int marqueeHalf(const Marquee& m) { return m.half ? m.half : ringHalfWidth(m.dy); }

bool marqueeFits(const Marquee& m, const Font& f, const char* s) { return textWidth(f, s) <= marqueeHalf(m) * 2; }

void drawMarqueeRing(Canvas240& cv, const Marquee& m, const Font& f, const char* s, const char* follow,
                     uint16_t colour, int lead, uint32_t off) {
  const int half = marqueeHalf(m);
  const int period = textWidth(f, s) + half;
  const int x = kMarqueeCX - half + lead - int(off % uint32_t(period));
  const Clip clip = stripClip(m, half);
  drawString(cv, f, s, x, kMarqueeCY + m.dy, colour, clip);
  drawString(cv, f, follow, x + period, kMarqueeCY + m.dy, colour, clip);
}

void drawMarqueeAt(Canvas240& cv, const Marquee& m, const Font& f, const char* s, uint16_t colour, uint32_t ms) {
  if (marqueeFits(m, f, s)) {
    drawCentred(cv, m, f, s, colour);
    return;
  }
  const uint64_t period = uint64_t(textWidth(f, s) + marqueeHalf(m));
  drawMarqueeRing(cv, m, f, s, s, colour, 0, uint32_t(uint64_t(ms) * uint32_t(m.pxPerS) / 1000 % period));
}

}  // namespace paint
