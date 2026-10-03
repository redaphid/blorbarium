#include <algorithm>
#include "blorb/genes.h"

namespace blorb {
namespace {

struct Hsv { int h, s, v; };   // h in 1/1536 turn, s and v 0..255

Hsv toHsv(Rgb c) {
  int mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
  Hsv o{0, mx == 0 ? 0 : d * 255 / mx, mx};
  if (d == 0) return o;
  int h = mx == c.r ? (c.g - c.b) * 256 / d : (mx == c.g ? 512 + (c.b - c.r) * 256 / d : 1024 + (c.r - c.g) * 256 / d);
  o.h = (h + 1536) % 1536;
  return o;
}

Rgb toRgb(Hsv c) {
  int sector = c.h / 256, f = c.h % 256;
  int p = c.v * (255 - c.s) / 255;
  int q = c.v * (255 - c.s * f / 256) / 255;
  int t = c.v * (255 - c.s * (256 - f) / 256) / 255;
  switch (sector) {
    case 0: return {c.v, t, p};
    case 1: return {q, c.v, p};
    case 2: return {p, c.v, t};
    case 3: return {p, q, c.v};
    case 4: return {t, p, c.v};
    default: return {c.v, p, q};
  }
}

}  // namespace

Rgb tinted(Rgb c, Tint t, int satScale) {
  if (t.hue == 0 && t.sat == 128 && t.val == 128 && satScale == 128) return c;
  Hsv h = toHsv(c);
  h.h = (h.h + t.hue * 6 + 1536) % 1536;
  h.s = std::min(255, h.s * t.sat / 128 * satScale / 128);
  h.v = std::min(255, h.v * t.val / 128);
  return toRgb(h);
}

}  // namespace blorb
