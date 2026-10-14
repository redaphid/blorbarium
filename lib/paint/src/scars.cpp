#include "duel/duel_art.h"
#include "figure.h"

namespace paint {
namespace {

constexpr int kMaxRows = 320;
// Scars sit between these shares of his hide's height (of 256), so none lands on
// the soles of his feet or the crown of his head where the art is a pixel thick.
constexpr int kTopShare = 16, kSpanShare = 224;
// How far in from the edge of his hide a scar's centre sits, as a share of the
// scar's width (of 256): most of it on him, its outer edge breaking his outline
// a little, the way a nick in an ear or a scratch down a flank does.
constexpr int kInShare = 128;
// The art's crease (g below 128) and ridge (above) are deepened by this much
// (x/2) so a scar reads at the duel's size; at home it is the same scar, clearer.
constexpr int kDepth = 3;

int imin(int a, int b) { return a < b ? a : b; }
int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int imax(int a, int b) { return a > b ? a : b; }

bool hideAt(const PaletteEntry* pal, uint16_t count, uint8_t i) { return i && i < count && isHide(pal[i].region); }

// His hide's leftmost and rightmost pixel in each row (-1 where a row has none).
struct Extents {
  int16_t left[kMaxRows], right[kMaxRows];
  int top = -1, bottom = -1, wide = 0;
};

void extents(const FrameRef& f, const PaletteEntry* pal, uint16_t count, Extents& e) {
  const uint8_t* p = f.rle;
  const int rows = imin(f.h, kMaxRows);
  int x0 = f.w, x1 = -1;
  for (int y = 0; y < f.h; ++y) {
    int x = 0, lo = -1, hi = -1;
    while (x < f.w) {
      if (hideAt(pal, count, p[1])) {
        if (lo < 0) lo = x;
        hi = imin(x + p[0], f.w) - 1;
      }
      x += p[0];
      p += 2;
    }
    if (y >= rows) continue;
    e.left[y] = int16_t(lo);
    e.right[y] = int16_t(hi);
    if (lo < 0) continue;
    if (e.top < 0) e.top = y;
    e.bottom = y;
    x0 = imin(x0, lo);
    x1 = imax(x1, hi);
  }
  e.wide = x1 >= x0 ? x1 - x0 + 1 : 0;
}

// The nearest row to `y` with hide in it.
int skinRow(const Extents& e, int y) {
  for (int d = 0; d <= e.bottom - e.top; ++d) {
    if (y - d >= e.top && e.left[y - d] >= 0) return y - d;
    if (y + d <= e.bottom && e.left[y + d] >= 0) return y + d;
  }
  return e.top;
}

// Small on a hatchling's narrow body, large on a broad adult, by his width on
// screen, so a scar is about the same share of whoever wears it.
int sizeFor(int widePx) { return widePx < 60 ? 0 : (widePx < 100 ? 1 : 2); }
constexpr int kOne = 4096;

ScarStamp stampOf(const FrameRef& art, int x, int y, uint8_t strength) {
  ScarStamp s{};
  s.art = &art;
  s.x = int16_t(x);
  s.y = int16_t(y);
  s.strength = strength;
  return s;
}

// The palette index at an art pixel, or 0 (clear).
uint8_t artPixel(const FrameRef& f, int ax, int ay) {
  const uint8_t* p = f.rle;
  for (int y = 0; y < ay; ++y)
    for (int x = 0; x < f.w; x += p[0], p += 2) {}
  for (int x = 0; x < f.w; x += p[0], p += 2)
    if (ax < x + p[0]) return p[1];
  return 0;
}

}  // namespace

ScarLayer placeScars(const blorb::Scars& scars, const FrameRef& body, const PaletteEntry* pal, uint16_t palCount,
                     int scale, uint8_t newest, uint8_t newStrength) {
  ScarLayer out;
  if (!scars.count || !body.rle || !pal) return out;
  Extents e;
  extents(body, pal, palCount, e);
  if (e.top < 0) return out;
  scale = imax(scale, kOne / 16);
  const int size = sizeFor(e.wide * scale / kOne);
  out.zoom = uint16_t(imax(256, kOne * 256 / scale));
  const int n = imin(scars.count, blorb::kMaxScars);
  for (int i = 0; i < n; ++i) {
    const blorb::Scar& s = scars.mark[i];
    const FrameRef& art = duel_art::kScars[imin(uint8_t(s.kind), 4)][size];
    const int span = e.bottom - e.top;
    const int row = skinRow(e, e.top + span * (kTopShare + s.y * kSpanShare / 255) / 256);
    const int in = imax(2, art.w * out.zoom / 256 * kInShare / 256);
    const bool left = s.x < 128;
    const int edge = left ? e.left[row] : e.right[row];
    // A narrow part (a leg, an ear) keeps the scar on it rather than reaching across.
    const int mid = (e.left[row] + e.right[row]) / 2;
    const int x = left ? imin(edge + in, mid) : imax(edge - in, mid);
    const int fade = newest && i >= n - newest ? s.fade * newStrength / 255 : s.fade;
    out.stamp[out.count++] = stampOf(art, x - body.originX, row - body.originY, uint8_t(fade));
  }
  return out;
}

int scarShade(const ScarLayer& layer, int rx, int ry) {
  int q = 128;
  for (int i = 0; i < layer.count; ++i) {
    const ScarStamp& s = layer.stamp[i];
    const FrameRef& art = *s.art;
    const int ax = floorDiv((rx - s.x) * 256, layer.zoom) + art.originX;
    const int ay = floorDiv((ry - s.y) * 256, layer.zoom) + art.originY;
    if (ax < 0 || ay < 0 || ax >= art.w || ay >= art.h) continue;
    const uint8_t idx = artPixel(art, ax, ay);
    if (!idx) continue;
    const int g = duel_art::kPalette[idx].r;
    q = q * imax(16, 128 + (g - 128) * kDepth / 2 * s.strength / 255) / 128;
  }
  return q;
}

}  // namespace paint
