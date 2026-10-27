#include "paint/banner.h"

namespace paint {

const Banner kMarqueeBanner = {&kFreeSansBold24pt7b, 640, 110, kBannerGround, kBannerInk};

namespace {
constexpr int kSide = 240;
constexpr Clip kWholeCanvas = {0, 0, kSide, kSide, 0};

int64_t travel(int w, int passes) { return int64_t(kSide) + w + int64_t(passes - 1) * bannerPeriod(w); }
}  // namespace

size_t clampBannerText(char (&out)[kBannerMaxText + 1], const char* in) {
  size_t n = 0;
  bool space = false;
  for (const char* s = in ? in : ""; *s && n < kBannerMaxText; ++s) {
    char c = *s;
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    if (uint8_t(c) < 0x20 || uint8_t(c) > 0x7E) continue;   // not in the font: UTF-8, controls
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c == ' ') { space = n > 0; continue; }
    if (space && n + 1 < kBannerMaxText) out[n++] = ' ';
    else if (space) break;   // the space would be the last character: stop at the word
    space = false;
    out[n++] = c;
  }
  out[n] = '\0';
  return n;
}

int bannerPasses(int w, int pxPerS) {
  int passes = kBannerPasses;
  const int64_t least = int64_t(kBannerMinMs) * pxPerS / 1000, most = int64_t(kBannerMaxMs) * pxPerS / 1000;
  while (travel(w, passes) < least) ++passes;
  if (travel(w, passes) > most) passes = 1;   // a line so wide its two passes would outstay a minute: once
  return passes;
}

uint32_t bannerShowMsForWidth(int w, int pxPerS) {
  if (pxPerS <= 0) return 0;
  return uint32_t((travel(w, bannerPasses(w, pxPerS)) * 1000 + pxPerS - 1) / pxPerS);
}

uint32_t bannerShowMs(const Banner& b, const char* s) {
  return bannerShowMsForWidth(textWidthScaled(*b.font, s, b.scaleQ8), b.pxPerS);
}

void drawBanner(Canvas240& cv, const Banner& b, const char* s, uint32_t ageMs) {
  for (uint16_t& p : cv.px) p = b.ground;
  if (!*s) return;
  const int w = textWidthScaled(*b.font, s, b.scaleQ8);
  const int passes = bannerPasses(w, b.pxPerS);
  const int64_t off = int64_t(ageMs) * b.pxPerS / 1000;
  for (int k = 0; k < passes; ++k) {
    const int64_t x = kSide - off + int64_t(k) * bannerPeriod(w);
    if (x >= kSide) break;          // this copy and every one after it is still off the right edge
    if (x + w <= 0) continue;       // gone off the left
    drawStringScaled(cv, *b.font, s, int(x), kSide / 2, b.ink, kWholeCanvas, b.scaleQ8);
  }
}

}  // namespace paint
