#include "ota/screen.h"

#include <cmath>
#include <cstdio>
#include "paint/text.h"

namespace ota {
namespace {

constexpr uint16_t rgb(int r, int g, int b) { return uint16_t((r >> 3) << 11 | (g >> 2) << 5 | b >> 3); }
constexpr uint16_t kInk = rgb(255, 255, 255);
constexpr uint16_t kAsk = rgb(255, 184, 48);    // amber: something wants an answer
constexpr uint16_t kWork = rgb(64, 224, 200);   // teal: the dish's own colour for doing something
constexpr paint::Clip kAll{0, 0, 240, 240, 0};

// The text sits on a darkened band so it reads over any pet, day or night.
void scrim(paint::Canvas240& cv) {
  for (int y = 82; y < 158; ++y)
    for (int x = 0; x < 240; ++x) cv.px[y * 240 + x] = paint::blend(cv.px[y * 240 + x], 0, 200);
}

void centred(paint::Canvas240& cv, const char* s, int y, uint16_t colour) {
  paint::drawString(cv, paint::kFreeSans9pt7b, s, 120 - paint::textWidth(paint::kFreeSans9pt7b, s) / 2, y, colour, kAll);
}

// Clockwise from twelve o'clock, on the rim.
void ring(paint::Canvas240& cv, float fraction, uint16_t colour) {
  constexpr int kSteps = 1200;   // enough that the outer edge of a 118 px ring has no pinholes
  const int n = int(fraction * kSteps + 0.5f);
  for (int i = 0; i < n; ++i) {
    const float a = float(i) / kSteps * 6.2831853f - 1.5707963f;
    const float c = std::cos(a), s = std::sin(a);
    for (int r = 112; r <= 118; ++r) {
      const int x = 120 + int(std::lround(r * c)), y = 120 + int(std::lround(r * s));
      if (x >= 0 && x < 240 && y >= 0 && y < 240) cv.px[y * 240 + x] = colour;
    }
  }
}

}  // namespace

void drawUpdate(paint::Canvas240& cv, const View& v) {
  scrim(cv);
  switch (v.phase) {
    case View::Phase::Asking:
      ring(cv, float(v.msLeft) / Session::kConfirmMs, kAsk);
      centred(cv, "New version!", 106, kAsk);
      centred(cv, "Tip me on my side!", 134, kInk);   // or press the button (src/hw/turn_yes.h)
      break;
    case View::Phase::Receiving: {
      ring(cv, v.size ? float(v.at) / v.size : 0, kWork);
      char pct[8];
      std::snprintf(pct, sizeof pct, "%u%%", unsigned(v.size ? uint64_t(v.at) * 100 / v.size : 0));
      centred(cv, "Updating", 106, kInk);
      centred(cv, pct, 134, kWork);
      break;
    }
    case View::Phase::Ready:
      ring(cv, 1, kWork);
      centred(cv, "Restarting", 120, kInk);
      break;
  }
}

}  // namespace ota
