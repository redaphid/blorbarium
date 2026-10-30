// The whoosh a hard shake makes as it dismisses something (#35): a ring
// that sweeps out from the middle of the dish to the rim and fades.
#include <cmath>
#include <cstdint>

#include "paint/canvas.h"
#include "paint/duel.h"

namespace paint {

void drawDismissed(Canvas240& cv, uint16_t ageMs) {
  if (ageMs >= kDismissedMs) return;
  const float t = float(ageMs) / float(kDismissedMs);
  const float r = 30.0f + t * 88.0f;             // out from the middle to the rim
  const uint32_t alpha = uint32_t((1.0f - t) * 220.0f);
  constexpr int kSide = 240;
  const int x0 = kSide / 2, y0 = kSide / 2;
  for (int y = 0; y < kSide; ++y) {
    const float dy = float(y - y0);
    for (int w = -2; w <= 2; ++w) {   // the ring's band, a pixel row of it at a time
      const float rr = r + float(w), dx2 = rr * rr - dy * dy;
      if (dx2 < 0) continue;
      const int dx = int(std::sqrt(dx2) + 0.5f);
      for (int x : {x0 - dx, x0 + dx}) {
        if (x < 0 || x >= kSide) continue;
        uint16_t& p = cv.px[y * kSide + x];
        const uint32_t pr = p >> 11, pg = p >> 5 & 63, pb = p & 31;
        const uint32_t a = alpha * uint32_t(3 - (w < 0 ? -w : w)) / 3;
        p = uint16_t((pr + ((31 - pr) * a >> 8)) << 11 | (pg + ((63 - pg) * a >> 8)) << 5 | (pb + ((31 - pb) * a >> 8)));
      }
    }
  }
}

}  // namespace paint
