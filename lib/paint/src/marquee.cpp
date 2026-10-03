#include "paint/marquee.h"

#include <cstdio>

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

void drawMarquee(Canvas240& cv, const Marquee& m, MarqueePhase& phase, const Font& f, const char* s,
                 uint16_t colour, uint32_t nowMs) {
  if (marqueeFits(m, f, s)) {
    marqueeRest(phase);
    drawCentred(cv, m, f, s, colour);
    return;
  }
  const int half = marqueeHalf(m);
  if (!phase.shown[0]) std::snprintf(phase.shown, sizeof(phase.shown), "%s", s);
  const uint32_t dt = phase.lastMs ? nowMs - phase.lastMs : 0;
  phase.lastMs = nowMs;
  phase.offMilliPx += uint64_t(dt) * uint32_t(m.pxPerS);
  uint64_t period = uint64_t(textWidth(f, phase.shown) + half) * 1000;
  if (phase.offMilliPx >= period) {   // the lap is over: the follower is now the line
    phase.offMilliPx -= period;
    std::snprintf(phase.shown, sizeof(phase.shown), "%s", phase.next);
    period = uint64_t(textWidth(f, phase.shown) + half) * 1000;
    if (phase.offMilliPx >= period) phase.offMilliPx = 0;   // a frame so late it skipped a lap
  }
  // The follower takes the newest text only while it is still off the right edge.
  const bool followerInView = period - phase.offMilliPx < uint64_t(half * 2) * 1000;
  if (!followerInView) std::snprintf(phase.next, sizeof(phase.next), "%s", s);
  drawMarqueeRing(cv, m, f, phase.shown, phase.next, colour, 0, uint32_t(phase.offMilliPx / 1000));
}

void drawMarqueeAt(Canvas240& cv, const Marquee& m, const Font& f, const char* s, uint16_t colour, uint32_t ms) {
  if (marqueeFits(m, f, s)) {
    drawCentred(cv, m, f, s, colour);
    return;
  }
  const uint64_t period = uint64_t(textWidth(f, s) + marqueeHalf(m));
  drawMarqueeRing(cv, m, f, s, s, colour, 0, uint32_t(uint64_t(ms) * uint32_t(m.pxPerS) / 1000 % period));
}

void marqueeRest(MarqueePhase& phase) {
  phase.offMilliPx = 0;
  phase.lastMs = 0;
  phase.shown[0] = phase.next[0] = '\0';
}

void drawMarqueeTimed(Canvas240& cv, const Marquee& m, const Font& f, const char* s, uint16_t colour, uint32_t age,
                      uint32_t life) {
  const int half = marqueeHalf(m);
  const int w = textWidth(f, s);
  if (w <= half * 2) {
    const int travelled = int(uint64_t(age) * uint32_t(m.pxPerS) / 1000);
    const int x = kMarqueeCX - w / 2 > kMarqueeCX + half - travelled ? kMarqueeCX - w / 2 : kMarqueeCX + half - travelled;
    drawString(cv, f, s, x, kMarqueeCY + m.dy, colour, stripClip(m, half));
    return;
  }
  const uint32_t trip = uint32_t(2 * half + w + (w + half));   // two passes
  const uint32_t stretched = trip * 1000 / (life > 1 ? life : 1);
  const uint32_t speed = stretched > uint32_t(m.pxPerS) ? stretched : uint32_t(m.pxPerS);
  drawMarqueeRing(cv, m, f, s, s, colour, half * 2, uint32_t(uint64_t(age) * speed / 1000));
}

uint32_t marqueeTimedDuration(const Marquee& m, const Font& f, const char* s) {
  const int half = marqueeHalf(m);
  const int w = textWidth(f, s);
  return uint32_t(2 * half + w + w + half) * 1000 / uint32_t(m.pxPerS);
}

}  // namespace paint
