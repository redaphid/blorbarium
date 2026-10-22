#include <cmath>
#include <climits>
#include <cstdio>
#include <cstring>
#include "blorb/registry.h"
#include "carried.h"
#include "duel/duel_art.h"
#include "figure.h"
#include "lively.h"
#include "paint/marquee.h"
#include "paint/parallel.h"
#include "paint/phases.h"
#include "paint/sprite_pack.h"

namespace paint {
namespace {

using blorb::Appearance;
using blorb::ExprId;
using blorb::Fx;
using blorb::Stage;
using blorb::Tint;
using Kind = Appearance::Kind;
namespace expr = blorb::expr;

constexpr int kSide = 240;
constexpr int kOne = 4096;   // Q12: scale and shear factors
constexpr int kMaxFrameW = 320;

// ---- tuning: each a one-line change ------------------------------------------
constexpr Fx kFaceThreshold = Fx::ratio(1, 4);   // a weaker face shows neutral
constexpr Fx kGlowingEyes = Fx::ratio(1, 4);     // a fainter glow (fading after a foresight) shows no ring and no teal eyes
constexpr uint16_t kCrossfadeTicks = 3;
constexpr uint16_t kBlinkCycleTicks = 40;        // a blink about every 4 s ...
constexpr uint16_t kBlinkJitterTicks = 24;       // ... at a lifeSeed-chosen tick within each cycle
constexpr uint16_t kYawnCycleTicks = 600;
constexpr uint16_t kYawnTicks = 18;
constexpr uint16_t kBreathTicks = 26, kSleepBreathTicks = 44;
constexpr int kBreathPx = 2;                     // one more at intensity >= 3/4
constexpr int kHopMaxPx = 36;                    // leap height at strength 1, scalePct 100
constexpr int kShadowAlpha = 150;               // the contact shadow's darkest, standing (of 256)
constexpr int kShadowWidePct = 30;               // its half-width, as a share of the body frame's width
constexpr int kHatchlingWide = kOne * 116 / 100, kHatchlingTall = kOne * 90 / 100;
constexpr int kElderSat = 96;                    // x/128
constexpr int kMinScale = kOne / 16;
constexpr int kEggLean = kOne * 16 / 100;        // shear at full wobble
constexpr int kClutchGapPx = 66;
constexpr int kClutchEggPx = 72;                 // three eggs side by side, each this tall
constexpr int kAwayPct = 82;                     // his size at the rim, of his size at home
constexpr int kCarrySpillQ4 = 4 * 16;            // a carried lean may take him this far past his travel circle
constexpr uint8_t kHaloTeal[3] = {64, 236, 214};
constexpr uint8_t kSparkWhite[3] = {200, 255, 248};
constexpr uint16_t kPulseTicks = 8;              // 1.25 Hz at 10 ticks a second
constexpr int kPulse[kPulseTicks] = {256, 242, 208, 174, 160, 174, 208, 242};
constexpr uint16_t kSparkOrbitTicks = 30;
constexpr int kSparkMax = 6;
constexpr Fx kSparkGlow = Fx::ratio(6, 10);
constexpr int kMaxPips = 12;
constexpr uint8_t kPipColour[3] = {214, 150, 70};
constexpr uint8_t kHintColour[3] = {240, 226, 190};
constexpr uint8_t kMarqueeColour[3] = {255, 206, 96};

// The empty band above his head, under the pantry pips: the sibling's label
// strip (38 px/s, FreeSans 9pt) on the disc's chord there, the ink fading out
// over the last pixels toward the rim.
constexpr int kMarqueeFeather = 20;
constexpr Marquee kMarqueeStrip = {-76, 0, 20, 38, kMarqueeFeather};

constexpr int floorDiv(int64_t a, int64_t b) { return int(a >= 0 ? a / b : -((-a + b - 1) / b)); }
constexpr int ceilDiv(int64_t a, int64_t b) { return -floorDiv(-a, b); }
constexpr int imin(int a, int b) { return a < b ? a : b; }
constexpr int imax(int a, int b) { return a > b ? a : b; }
constexpr int iabs(int a) { return a < 0 ? -a : a; }

// The same root in 32 bits, starting at the value's own top bit: on the
// board's 32-bit core the 64-bit loop costs a few hundred cycles a call, and
// a frame makes tens of thousands of them.
uint32_t isqrt32(uint32_t v) {
  if (!v) return 0;
  uint32_t r = 0, bit = uint32_t(1) << ((31 - __builtin_clz(v)) & ~1);
  while (bit) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
    bit >>= 2;
  }
  return r;
}

uint32_t isqrt(uint64_t v) {
  if (v <= 0xFFFFFFFFu) return isqrt32(uint32_t(v));
  uint64_t r = 0, bit = uint64_t(1) << 62;
  while (bit > v) bit >>= 2;
  while (bit) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
    bit >>= 2;
  }
  return uint32_t(r);
}

uint32_t mix32(uint32_t a, uint32_t b) {
  uint32_t h = a ^ (b * 0x9E3779B1u);
  h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
  return h;
}

// sin of i/64 turn, Q12.
int sin64(int i) {
  static constexpr int16_t kQuarter[17] = {0, 401, 799, 1189, 1567, 1931, 2276, 2598, 2896,
                                           3166, 3406, 3612, 3784, 3920, 4017, 4076, 4096};
  i &= 63;
  if (i < 16) return kQuarter[i];
  if (i < 32) return kQuarter[32 - i];
  if (i < 48) return -kQuarter[i - 32];
  return -kQuarter[64 - i];
}

// sin of q/256 of a 1/64 turn, Q12: sin64's steps joined by straight lines, so
// a swing drawn between ticks moves every frame. sin64q(i * 256) == sin64(i).
int sin64q(int q) {
  const int i = q >> 8, f = q & 255, s0 = sin64(i);
  return f ? s0 + (sin64(i + 1) - s0) * f / 256 : s0;
}

int unit256(Fx f) { return blorb::clamp01(f).raw >> 16; }   // 0..256

struct Rgb { int r, g, b; };
struct Hsv { int h, s, v; };   // h in 1/1536 turn, s and v 0..255

uint16_t to565(Rgb c) { return uint16_t(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }

Hsv toHsv(Rgb c) {
  int mx = imax(c.r, imax(c.g, c.b)), mn = imin(c.r, imin(c.g, c.b)), d = mx - mn;
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

Tint clampTint(Tint t, RegionBand b) {
  t.hue = int8_t(imax(b.hueMin, imin(b.hueMax, t.hue)));
  t.sat = uint8_t(imax(b.satMin, imin(b.satMax, t.sat)));
  t.val = uint8_t(imax(b.valMin, imin(b.valMax, t.val)));
  return t;
}

// The tint moves the authored colour in HSV, so shading ramps keep their steps.
Rgb tinted(Rgb c, Tint t, int satScale) {
  if (t.hue == 0 && t.sat == 128 && t.val == 128 && satScale == 128) return c;
  Hsv h = toHsv(c);
  h.h = (h.h + t.hue * 6 + 1536) % 1536;
  h.s = imin(255, h.s * t.sat / 128 * satScale / 128);
  h.v = imin(255, h.v * t.val / 128);
  return toRgb(h);
}

// Additive light in the panel's own channel depths, so whether a pixel moves
// depends only on the light, not on the colour under it.
void addLight(Canvas240& cv, int x, int y, const Rgb& c, int intensity) {
  if (x < 0 || y < 0 || x >= kSide || y >= kSide || intensity <= 0) return;
  uint16_t& px = cv.px[y * kSide + x];
  int r = ((px >> 11) & 31) + (c.r * intensity >> 11);
  int g = ((px >> 5) & 63) + (c.g * intensity >> 10);
  int b = (px & 31) + (c.b * intensity >> 11);
  px = uint16_t((imin(r, 31) << 11) | (imin(g, 63) << 5) | imin(b, 31));
}

// The LUT for one draw: each palette entry with its region's tint, clamped to
// the pack's band, plus the mottling shade of every skin entry.
struct Colours {
  uint16_t base[256];
  uint16_t spot[256];
  uint8_t region[256];   // the entry's palette region, 255 invariant
};

// The one LUT every draw builds into. It is 1,280 B, too much for each frame
// that draws to carry on the loop task's 8 KB stack, and nothing draws from two
// tasks or builds one LUT while another is in use, so they share it.
Colours& scratchColours() {
  static Colours c;
  return c;
}

void buildColours(const SpritePack& pack, const Tint* tints, bool elder, int8_t spotHue, Colours& out) {
  std::memset(&out, 0, sizeof(out));
  Tint clamped[blorb::kRegionCount];
  for (size_t r = 0; r < blorb::kRegionCount; ++r)
    clamped[r] = clampTint(tints[r], pack.band(blorb::RegionId{uint8_t(r)}));
  uint16_t count = 0;
  const PaletteEntry* pal = pack.palette(count);
  for (int i = 0; i < imin(count, 256); ++i) {
    const PaletteEntry& e = pal[i];
    Rgb c{e.r, e.g, e.b};
    if (e.region < blorb::kRegionCount) c = tinted(c, clamped[e.region], elder ? kElderSat : 128);
    out.base[i] = out.spot[i] = to565(c);
    out.region[i] = e.region;
    if (e.region == blorb::region::skin.v) out.spot[i] = to565(tinted(c, Tint{spotHue, 128, 92}, 128));
  }
}

Rgb haloColour(const SpritePack& pack, const Appearance& a) {
  Tint t = clampTint(a.regions[blorb::region::glow.v], pack.band(blorb::region::glow));
  return tinted({kHaloTeal[0], kHaloTeal[1], kHaloTeal[2]}, t, 128);
}

// How a frame lands: Q12 scale, a lift above the stand point and a shear (the
// egg's rock) about it. One transform moves pixels and eye anchors alike.
struct Xf { int kx = kOne, ky = kOne, lift = 0, shear = 0; };
struct Place { int x = 0, y = 0; Xf xf; };   // x, y: the stand point's canvas pixel
struct Pt { int x, y; };                     // Q4

// A frame point (Q4, frame pixels) relative to the stand point (Q4, canvas).
Pt project(const Xf& v, const FrameRef& f, int sx, int sy) {
  int64_t ry = int64_t(sy - f.originY * 16) * v.ky / kOne - int64_t(v.lift) * 16;
  int64_t rx = int64_t(sx - f.originX * 16) * v.kx / kOne - ry * v.shear / kOne;
  return {int(rx), int(ry)};
}

// An eye's halo radius (Q4, canvas). Small eyes (the egg's froglet) get a
// floor so their halo still reads.
constexpr int kMinHaloEyePx = 5;
int haloEyeQ4(const EyeAnchor& e, int kx) { return imax(kMinHaloEyePx * 16, e.r * 16 * kx / kOne); }
// Ring radius r * (1.5 + 0.5 glow), falling off over this much outside it.
int haloOuterQ4(int rc) { return imax(rc / 4, 16); }
// Sparks orbit just outside the ring, so they read as separate points.
int sparkGapQ4(int rc) { return imax(rc / 3, 24); }
// The farthest a halo or its sparks reach from an eye of radius rc (Q4), at full glow.
int haloReachQ4(int rc) { return 2 * rc + imax(haloOuterQ4(rc), sparkGapQ4(rc) + 24); }

// Walks the rows of a run-length frame forward, decoding the ones asked for.
class RowReader {
 public:
  explicit RowReader(const FrameRef& f) : p_(f.rle), w_(f.w) {}
  const uint8_t* seek(int y) {
    while (row_ < y) {
      ++row_;
      int x = 0;
      while (x < w_) {
        if (row_ == y)   // runs are a few pixels: a loop, not a memset call each
          for (uint8_t *d = buf_ + x, *e = buf_ + x + imin(p_[0], w_ - x), v = p_[1]; d < e;) *d++ = v;
        x += p_[0];
        p_ += 2;
      }
    }
    return buf_;
  }

 private:
  const uint8_t* p_;
  int w_;
  int row_ = -1;
  uint8_t buf_[kMaxFrameW];
};

bool drawable(const FrameRef& f) { return f.rle && f.w > 0 && f.h > 0 && f.w <= kMaxFrameW; }

// Every point a figure can light, as (point, reach) pairs: each opaque row's
// end corners under each transform, and each eye's widest halo.
struct Outline {
  const FrameRef* frames; int frameCount;
  const FrameRef* eyed; int eyedCount;
  const Xf* variants; int variantCount;
};

bool sameFrame(const FrameRef& a, const FrameRef& b) {
  if (a.rle != b.rle || a.w != b.w || a.h != b.h || a.originX != b.originX || a.originY != b.originY ||
      a.eyeCount != b.eyeCount)
    return false;
  for (int e = 0; e < imin(a.eyeCount, 2); ++e)
    if (a.eyes[e].x != b.eyes[e].x || a.eyes[e].y != b.eyes[e].y || a.eyes[e].r != b.eyes[e].r) return false;
  return true;
}

// Each frame's rows are walked once and every transform visits that row's
// corners. The callers keep only a max or a min of each coordinate or of the
// distance from a point, so the order is free and a frame met twice (his idle
// body, and the pose's when it is the same) is walked once. With no lean, a
// run of rows whose left (or right) edge stays at one column is a vertical
// segment: every corner on it projects to the same x, and y only climbs down
// it, so its two ends are the only corners that can hold the max or min.
template <class Visit>
void visitOutline(const Outline& o, Visit&& visit) {
  bool lean = false;
  for (int v = 0; v < o.variantCount; ++v) lean |= o.variants[v].shear != 0;
  for (int i = 0; i < o.frameCount; ++i) {
    const FrameRef& f = o.frames[i];
    if (!drawable(f)) continue;
    bool seen = false;
    for (int j = 0; j < i && !seen; ++j) seen = sameFrame(o.frames[j], f);
    if (seen) continue;
    const uint8_t* p = f.rle;
    // The open segments: each edge's column and the row its run began on (-1: none open).
    int loX = -1, loFrom = 0, hiX = -1, hiFrom = 0;
    auto close = [&](int x, int from, int to) {   // the ends of a segment at column x, rows [from, to)
      for (int v = 0; v < o.variantCount; ++v)
        for (int sy : {from * 16, to * 16}) visit(project(o.variants[v], f, x * 16, sy), 0);
    };
    for (int y = 0; y < f.h; ++y) {
      int x = 0, lo = -1, hi = -1;
      while (x < f.w) {
        if (p[1]) { if (lo < 0) lo = x; hi = imin(x + p[0], f.w); }
        x += p[0];
        p += 2;
      }
      if (lean) {
        if (lo < 0) continue;
        for (int v = 0; v < o.variantCount; ++v)
          for (int sy : {y * 16, (y + 1) * 16})
            for (int sx : {lo * 16, hi * 16}) visit(project(o.variants[v], f, sx, sy), 0);
        continue;
      }
      if (loX >= 0 && lo != loX) close(loX, loFrom, y), loX = -1;
      if (hiX >= 0 && hi != hiX) close(hiX, hiFrom, y), hiX = -1;
      if (lo < 0) continue;
      if (loX < 0) loX = lo, loFrom = y;
      if (hiX < 0) hiX = hi, hiFrom = y;
    }
    if (loX >= 0) close(loX, loFrom, f.h);
    if (hiX >= 0) close(hiX, hiFrom, f.h);
  }
  for (int v = 0; v < o.variantCount; ++v) {
    const Xf& xf = o.variants[v];
    for (int i = 0; i < o.eyedCount; ++i) {
      const FrameRef& f = o.eyed[i];
      for (int e = 0; e < imin(f.eyeCount, 2); ++e)
        visit(project(xf, f, f.eyes[e].x * 16 + 8, f.eyes[e].y * 16 + 8),
              haloReachQ4(haloEyeQ4(f.eyes[e], xf.kx)));
    }
  }
}

// The middle of the box around a frame's opaque pixels, relative to its stand point.
Pt middle(const FrameRef& f, const Xf& xf) {
  int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
  visitOutline({&f, 1, nullptr, 0, &xf, 1}, [&](Pt p, int) {
    x0 = imin(x0, p.x); x1 = imax(x1, p.x);
    y0 = imin(y0, p.y); y1 = imax(y1, p.y);
  });
  if (x0 > x1) return {0, 0};
  return {(x0 + x1) / 2, (y0 + y1) / 2};
}

// The disc about `centre` (his body's middle at rest), relative to the stand
// point, holding all of it standing: halo, squash and every face. A dish
// position puts the centre there, so home is his body in the middle of the
// panel and his travel is the same every way from it.
struct Disc { int x = 0, y = 0, r = 0; };   // Q4
Disc envelope(const Outline& o, Pt centre, int marginQ4) {
  Disc d{centre.x, centre.y, 0};
  // The root is monotonic, so the outline's points (no reach) need only the
  // farthest one's: one root, not one a corner.
  uint64_t far2 = 0;
  bool any = false;
  visitOutline(o, [&](Pt p, int reach) {
    int64_t dx = p.x - d.x, dy = p.y - d.y;
    const uint64_t d2 = uint64_t(dx * dx + dy * dy);
    if (!reach) { far2 = d2 > far2 ? d2 : far2, any = true; return; }
    d.r = imax(d.r, int(isqrt(d2)) + reach);
  });
  if (any) d.r = imax(d.r, int(isqrt(far2)));
  d.r += marginQ4 + 16;   // + the stand point's rounding
  return d;
}

// How far the figure standing at `p` can rise before any of it would leave
// the panel (px). The leap is all upward, so it is capped here rather than
// kept free everywhere, which would push home off the middle or shrink travel.
int headroom(const Outline& o, const Place& p, int marginQ4) {
  int64_t room = kSide * 16;
  visitOutline(o, [&](Pt q, int reach) {
    const int64_t x = int64_t(p.x) * 16 + q.x - kSide * 8, y = int64_t(p.y) * 16 + q.y - kSide * 8;
    const int64_t r = kSide * 8 - reach - marginQ4 - 16, ax = x < 0 ? -x : x;
    // The root is at least r - |x|, so a point that clears the lowest room so far even with that needs no root.
    if (r > ax && y + (r - ax) >= room) return;
    const int64_t up = r > ax ? y + int64_t(isqrt(uint64_t(r * r - x * x))) : 0;
    room = up < room ? up : room;
  });
  return room > 0 ? int(room / 16) : 0;
}

// A dish position (the unit disc) to a stand point whose envelope stays inside
// the round panel: the travel radius is the dish radius less the envelope's.
// The same stand point before it is rounded to a pixel (Q4, canvas), so a
// frame between ticks can move him by less than one (drawCreature).
Pt standQ4(blorb::DishPos at, const Disc& d) {
  int64_t x = at.x.raw, y = at.y.raw;
  uint64_t n2 = uint64_t(x * x) + uint64_t(y * y);
  if (n2 > (uint64_t(1) << 48)) {
    int64_t n = isqrt(n2);
    x = x * Fx::kOne / n;
    y = y * Fx::kOne / n;
  }
  int64_t travel = imax(0, kSide * 8 - d.r);
  int cx = kSide * 8 + int(x * travel / Fx::kOne), cy = kSide * 8 + int(y * travel / Fx::kOne);
  return {cx - d.x, cy - d.y};
}

Place placeAt(blorb::DishPos at, const Disc& d, const Xf& xf) {
  const Pt q = standQ4(at, d);
  Place p;
  p.x = floorDiv(q.x + 8, 16);
  p.y = floorDiv(q.y + 8, 16);
  p.xf = xf;
  return p;
}

// Mottling (mark layer 0): a seeded spot in some 7 px cells of the skin, in
// frame coordinates from the feet, so spots ride the body and siblings differ.
struct Mottle { uint32_t seed = 0; int density = 0; };
// floorDiv(a, 7) in 32 bits for |a| < 7 * 4096 (a frame is at most 320 px):
// biased non-negative, so the compiler divides by multiplying, where
// floorDiv's int64_t called __divdi3 twice a skin pixel on the board.
constexpr int floorDiv7(int a) { return int(unsigned(a + 7 * 4096) / 7u) - 4096; }
static_assert(floorDiv7(-1) == -1 && floorDiv7(-7) == -1 && floorDiv7(-8) == -2 && floorDiv7(6) == 0 && floorDiv7(7) == 1 &&
                  floorDiv7(-7 * 4096) == -4096,
              "floorDiv7 is floorDiv(a, 7)");
bool spotted(const Mottle& m, int rx, int ry) {
  constexpr int kCell = 7;
  int cx = floorDiv7(rx), cy = floorDiv7(ry);
  uint32_t h = mix32(m.seed ^ (uint32_t(cx) * 0x27D4EB2Du), uint32_t(cy));
  if (int(h & 255) >= m.density) return false;
  int sx = 2 + int((h >> 8) % 3), sy = 2 + int((h >> 10) % 3), r2 = 2 + int((h >> 12) % 3);
  int lx = rx - cx * kCell - sx, ly = ry - cy * kCell - sy;
  return lx * lx + ly * ly <= r2;
}

// spotted() along a row of the frame: the row's cell row once, and each 7 px
// cell's hash once as the columns cross into it, not once a pixel.
class SpotRow {
 public:
  explicit SpotRow(const Mottle& m) : m_(m) {}
  void row(int ry) { ry_ = ry, cy_ = floorDiv7(ry), cx_ = INT_MIN; }
  bool spotted(int rx) {
    const int cx = floorDiv7(rx);
    if (cx != cx_) {
      cx_ = cx;
      const uint32_t h = mix32(m_.seed ^ (uint32_t(cx) * 0x27D4EB2Du), uint32_t(cy_));
      live_ = int(h & 255) < m_.density;
      if (live_) {
        const int sx = 2 + int((h >> 8) % 3), sy = 2 + int((h >> 10) % 3), r2 = 2 + int((h >> 12) % 3);
        const int ly = ry_ - cy_ * 7 - sy;
        centre_ = cx * 7 + sx, room_ = r2 - ly * ly;
      }
    }
    const int lx = rx - centre_;
    return live_ && lx * lx <= room_;
  }

 private:
  const Mottle& m_;
  int ry_ = 0, cy_ = 0, cx_ = INT_MIN, centre_ = 0, room_ = 0;
  bool live_ = false;
};

// A skin pixel under his scars, x/128 in each channel.
uint16_t scarred(uint16_t c, int q) {
  const int r = imin(31, ((c >> 11) & 31) * q / 128), g = imin(63, ((c >> 5) & 63) * q / 128), b = imin(31, (c & 31) * q / 128);
  return uint16_t((r << 11) | (g << 5) | b);
}

// One blit's rows, [top, split) in half 1 and [split, bottom] in half 0.
struct BlitJob {
  Canvas240& cv;
  const FrameRef& f;
  const Place& p;
  const Colours& col;
  int alpha;
  const Mottle* mottle;
  const ScarLayer* scars;
  int top, bottom, left, right, split;
  void rows(int half);
};

void blit(Canvas240& cv, const FrameRef& f, const Place& p, const Colours& col, int alpha, const Mottle* mottle,
          const ScarLayer* scars = nullptr) {
  const Xf& v = p.xf;
  // Every caller scales by at least 1 (imax(1, ...), kMinScale): the stepped
  // columns below and the rows' floorDiv both divide by these.
  if (!drawable(f) || alpha <= 0 || v.kx <= 0 || v.ky <= 0) return;
  int lean = iabs(v.shear) * (f.h * v.ky / kOne + v.lift + 2) / kOne + 1;
  int top = imax(0, p.y - v.lift + floorDiv(int64_t(-f.originY) * v.ky, kOne) - 1);
  int bottom = imin(kSide - 1, p.y - v.lift + ceilDiv(int64_t(f.h - f.originY) * v.ky, kOne) + 1);
  int left = imax(0, p.x + floorDiv(int64_t(-f.originX) * v.kx, kOne) - lean - 1);
  int right = imin(kSide - 1, p.x + ceilDiv(int64_t(f.w - f.originX) * v.kx, kOne) + lean + 1);
  BlitJob job{cv, f, p, col, alpha, mottle, scars, top, bottom, left, right, top};
  // A big sprite is drawn in two halves at once when a second core is lent
  // (paint/parallel.h): the rows above `split` here, the rest there.
  constexpr int kSplitPx = 4096;
  if (parallel.run && (bottom - top + 1) * (right - left + 1) >= kSplitPx) {
    job.split = top + (bottom - top + 1) / 2;
    return bothHalves([](void* j, int half) { static_cast<BlitJob*>(j)->rows(half); }, &job);
  }
  job.split = bottom + 1;
  job.rows(1);
}

void BlitJob::rows(int half) {
  const Xf& v = p.xf;
  const int y0 = half ? top : split, y1 = half ? split - 1 : bottom;
  if (y0 > y1) return;
  RowReader rows(f);
  // Each pixel's source column is floorDiv((2 (x - p.x) + 1 + shift2) kOne, 2 kx).
  // Along a row the numerator grows by 2 kOne a pixel, so the quotient and its
  // remainder are stepped rather than divided: the same columns, with no
  // 64-bit division a pixel (a library call on the board).
  const int den = 2 * v.kx;   // kx >= 1 (blit), so den > 0
  const int stepQ = 2 * kOne / den, stepR = 2 * kOne % den;
  const int fw = f.w, ox = f.originX, oy = f.originY;
  const bool scarred_ = scars && scars->count;
  // Each half's own, shared from blit to blit (one blit at a time, off the
  // loop task's stack): the scars' rows, and each canvas column's source
  // column. Along a row those depend on the row only through the lean, so
  // with none they are worked out once a blit; and since they only climb,
  // the columns that land inside the frame are one span, [xa, xb].
  static ScarRows scarRowsOf[2];
  static int16_t colsOf[2][kSide];
  ScarRows& scarRows = scarRowsOf[half];
  int16_t* const cols = colsOf[half];
  if (scarred_) scarRows.begin(*scars);
  static const Mottle kNone;
  SpotRow spots(mottle ? *mottle : kNone);
  int lastShift = INT_MIN, xa = 0, xb = -1;
  for (int y = y0; y <= y1; ++y) {
    int dy2 = 2 * (y - p.y) + 1;   // twice the pixel centre's offset from the stand point
    int sy = oy + floorDiv(int64_t(dy2 + 2 * v.lift) * kOne, 2 * v.ky);
    if (sy < 0) continue;
    if (sy >= f.h) break;
    const uint8_t* row = rows.seek(sy);
    const int shift2 = v.shear ? floorDiv(int64_t(dy2) * v.shear, kOne) : 0;
    if (shift2 != lastShift) {
      lastShift = shift2;
      const int64_t num = int64_t(2 * (left - p.x) + 1 + shift2) * kOne;
      int q = floorDiv(num, den), rem = int(num - int64_t(q) * den);   // 0 <= rem < den
      xa = right + 1, xb = left - 1;
      for (int x = left; x <= right; ++x, q += stepQ, rem += stepR) {
        if (rem >= den) rem -= den, ++q;
        const int sx = ox + q;
        cols[x] = int16_t(sx);
        if (sx >= 0 && sx < fw) xa = imin(xa, x), xb = x;
      }
    }
    uint16_t* const out = &cv.px[y * kSide];
    if (alpha >= 256 && !mottle && !scarred_) {   // most of what is drawn: a palette lookup a pixel
      const uint16_t* const base = col.base;
      for (int x = xa; x <= xb; ++x)
        if (const uint8_t i = row[cols[x]]) out[x] = base[i];
      continue;
    }
    if (scarred_) scarRows.row(sy - oy);
    if (mottle) spots.row(sy - oy);
    for (int x = xa; x <= xb; ++x) {
      const int sx = cols[x];
      const uint8_t i = row[sx];
      if (!i) continue;
      const bool spot = mottle && col.region[i] == blorb::region::skin.v && spots.spotted(sx - ox);
      uint16_t c = spot ? col.spot[i] : col.base[i];
      if (scarred_ && isHide(col.region[i])) {
        const int q = scarRows.shade(sx - ox);
        if (q != 128) c = scarred(c, q);   // 128 leaves it as it is
      }
      uint16_t& dst = out[x];
      dst = alpha >= 256 ? c : blend(dst, c, alpha);
    }
  }
}

// The foresee halo around one eye (centre and radius Q4, canvas): an additive
// ring at r * (1.5 + 0.5 glow) that glows inward over the brow and cheeks.
void ring(Canvas240& cv, Pt c, int rc, int g8, int amp, const Rgb& tint, int sparks, int sparkTurn) {
  const int R = rc * (384 + 128 * g8 / 256) / 256;
  const int inner = imax(rc * 7 / 10, 16), outer = haloOuterQ4(rc);
  const int reach = R + outer;
  // Light lands only where -inner < t < outer, t = isqrt(d2) - R: d2 in
  // [lo2, hi2). The rest of the box is tested by its d2 alone, with no root.
  const int64_t loR = imax(0, R - inner + 1), hiR = R + outer;
  const int64_t lo2 = loR * loR, hi2 = hiR * hiR;
  for (int y = imax(0, floorDiv(c.y - reach, 16)); y <= imin(kSide - 1, (c.y + reach) / 16); ++y) {
    for (int x = imax(0, floorDiv(c.x - reach, 16)); x <= imin(kSide - 1, (c.x + reach) / 16); ++x) {
      int64_t dx = x * 16 + 8 - c.x, dy = y * 16 + 8 - c.y;
      const int64_t d2 = dx * dx + dy * dy;
      if (d2 < lo2 || d2 >= hi2) continue;
      int t = int(isqrt(uint64_t(d2))) - R;
      int i = t < 0 ? (-t >= inner ? 0 : amp * (inner + t) / inner) : (t >= outer ? 0 : amp * (outer - t) / outer);
      addLight(cv, x, y, tint, i);
    }
  }
  const Rgb white{kSparkWhite[0], kSparkWhite[1], kSparkWhite[2]};
  for (int k = 0; k < sparks; ++k) {
    int a = sparkTurn + k * 64 / sparks, orbit = R + sparkGapQ4(rc);
    int sx = floorDiv(c.x + orbit * sin64(a + 16) / kOne, 16), sy = floorDiv(c.y + orbit * sin64(a) / kOne, 16);
    addLight(cv, sx, sy, white, amp);
    addLight(cv, sx - 1, sy, white, amp / 2);
    addLight(cv, sx + 1, sy, white, amp / 2);
    addLight(cv, sx, sy - 1, white, amp / 2);
    addLight(cv, sx, sy + 1, white, amp / 2);
  }
}

// The ring and the teal eyes go together: while he foresees, and while the glow fades after.
bool eyesGlow(const Appearance& a) { return a.foreseeing || a.glow >= kGlowingEyes; }

void halo(Canvas240& cv, const Appearance& a, const SpritePack& pack, const FrameRef& eyes, const Place& p) {
  int g8 = unit256(a.glow);
  if (g8 == 0 || !eyesGlow(a)) return;
  // The pulse and the sparks run on poseTick and its 1/256s (paint/tween.h).
  const int p0 = kPulse[a.poseTick % kPulseTicks], p1 = kPulse[(a.poseTick + 1) % kPulseTicks];
  int amp = imin(256, g8 * 3 / 2) * (p0 + (p1 - p0) * a.poseSub / 256) / 256;
  int sparks = 0;
  if (a.glow > kSparkGlow) {
    int64_t span = Fx::one().raw - kSparkGlow.raw;
    sparks = int(imin(kSparkMax, int((int64_t(blorb::clamp01(a.glow).raw - kSparkGlow.raw) * kSparkMax + span - 1) / span)));
  }
  Rgb tint = haloColour(pack, a);
  int turn = int((uint32_t(a.poseTick) * 256 + a.poseSub) * 64 / (kSparkOrbitTicks * 256) + (a.lifeSeed & 63));
  for (int e = 0; e < imin(eyes.eyeCount, 2); ++e) {
    Pt c = project(p.xf, eyes, eyes.eyes[e].x * 16 + 8, eyes.eyes[e].y * 16 + 8);
    c.x += p.x * 16;
    c.y += p.y * 16;
    ring(cv, c, haloEyeQ4(eyes.eyes[e], p.xf.kx), g8, amp, tint, sparks, turn + e * 32);
  }
}

struct Motion { int wide = kOne, tall = kOne, lift = 0; };

// Squash (phase 0 to 0.2), leap (0.2 to 0.7, a parabola peaking at strength x
// the max), land and settle (0.7 to 1).
Motion hop(Fx phase, Fx strength, int maxLift) {
  const int64_t P = blorb::clamp01(phase).raw, a = Fx::ratio(2, 10).raw, b = Fx::ratio(7, 10).raw;
  Motion m;
  if (P < a) {
    int s = int(P * kOne / a);
    m.wide = kOne + 737 * s / kOne;
    m.tall = kOne - 819 * s / kOne;
  } else if (P < b) {
    int64_t h8 = int64_t(maxLift) * unit256(strength);
    int64_t num = (4 * (P - a) * (b - P)) >> 16, den = ((b - a) * (b - a)) >> 16;
    m.lift = int(h8 * num / den >> 8);
  } else {
    const int64_t settle = Fx::ratio(8, 10).raw;
    int s = P < settle ? int((P - b) * kOne / (settle - b)) : int((Fx::one().raw - P) * kOne / (Fx::one().raw - settle));
    m.wide = kOne + 573 * s / kOne;
    m.tall = kOne - 655 * s / kOne;
  }
  return m;
}

Motion flinch(Fx, Fx, int) { return {kOne * 104 / 100, kOne * 90 / 100, 0}; }

struct ReflexMotion {
  blorb::ReflexId id;
  Motion (*at)(Fx phase, Fx strength, int maxLift);
  Motion widest;   // for the envelope; the hop's height is covered by kHopMaxPx
};
const ReflexMotion kReflexMotions[] = {
    {blorb::reflex::hop, hop, {kOne * 118 / 100, kOne * 80 / 100, 0}},
    {blorb::reflex::flinch, flinch, {kOne * 104 / 100, kOne * 90 / 100, 0}},
};
constexpr int kReflexMotionCount = int(sizeof(kReflexMotions) / sizeof(kReflexMotions[0]));

const blorb::ReflexInfo* reflexInfo(blorb::ReflexId id) {
  for (const blorb::ReflexInfo& r : blorb::REFLEXES)
    if (r.id == id) return &r;
  return nullptr;
}

Motion motionOf(const Appearance& a, int maxLift) {
  if (!a.reflexActive) return {};
  for (const ReflexMotion& m : kReflexMotions)
    if (m.id == a.reflex) return m.at(a.reflexPhase, a.reflexStrength, maxLift);
  return {};
}

int breathPx(const Appearance& a) {
  int period = a.asleep ? kSleepBreathTicks : kBreathTicks, half = period / 2;
  int t = int((a.poseTick + mix32(a.lifeSeed, 0xB0B)) % uint32_t(period));
  int tri = t < half ? t : period - t;
  int amp = kBreathPx + (a.intensity >= Fx::ratio(3, 4) ? 1 : 0);
  return (amp * tri * 2 + half) / (2 * half);
}

struct FaceShown { ExprId now, from; int mix; };   // mix: 0..256 of `now` over `from`

bool blinking(const Appearance& a) {
  if (a.lively) return livelyOf(a, 0).blink;   // on the screen's clock, at his species' tempo
  uint32_t t = a.poseTick + a.lifeSeed % kBlinkCycleTicks;
  return t % kBlinkCycleTicks == mix32(a.lifeSeed, t / kBlinkCycleTicks) % kBlinkJitterTicks;
}

bool yawning(const Appearance& a) {
  uint32_t t = a.poseTick + mix32(a.lifeSeed, 0x7A3) % kYawnCycleTicks;
  uint32_t start = mix32(a.lifeSeed ^ 0x59A3u, t / kYawnCycleTicks) % (kYawnCycleTicks - kYawnTicks);
  uint32_t within = t % kYawnCycleTicks;
  return within >= start && within < start + kYawnTicks;
}

FaceShown faceFor(const Appearance& a) {
  // Glowing, he wears the foresee face whatever his genes or a reflex pick, with no blink or yawn.
  if (eyesGlow(a)) return {expr::foresee, expr::foresee, 256};
  if (a.reflexActive) {
    const blorb::ReflexInfo* r = reflexInfo(a.reflex);
    ExprId f = r ? r->face : expr::alarmed;
    return {f, f, 256};
  }
  // Asleep, his eyes are shut whatever his genes feel. Awake, shut eyes become lidded.
  if (a.asleep) return {expr::asleep, expr::asleep, 256};
  auto awake = [&](ExprId e) { return e == expr::asleep ? expr::sleepy : e; };
  ExprId now = awake(a.intensity >= kFaceThreshold ? a.expression : expr::neutral);
  bool resting = now == expr::asleep || now == expr::sleepy || now == expr::yawn;
  if (!a.asleep && now == expr::neutral && yawning(a)) return {expr::yawn, expr::yawn, 256};
  if (!a.asleep && !resting && blinking(a)) return {expr::asleep, expr::asleep, 256};
  if (a.exprTicks < kCrossfadeTicks && awake(a.previous) != now)
    return {now, awake(a.previous), (a.exprTicks * 256 + a.exprSub + 256) / (kCrossfadeTicks + 1)};
  return {now, now, 256};
}

// A shadow on the dish floor under the stand point. It stays down while he
// leaps, smaller and fainter the higher he goes, so a still frame reads as airborne.
void shadow(Canvas240& cv, const Place& p, int halfW, int lift, int maxLift) {
  const int span = 2 * imax(1, maxLift);
  const int hw = halfW - halfW * imin(lift, span) / (2 * span), hh = imax(1, hw / 4);
  const int alpha = kShadowAlpha * (span - imin(lift, span)) / span;
  if (hw <= 0 || alpha <= 0) return;
  const int64_t r2 = int64_t(hw) * hw * hh * hh;
  for (int y = imax(0, p.y - hh); y <= imin(kSide - 1, p.y + hh); ++y)
    for (int x = imax(0, p.x - hw); x <= imin(kSide - 1, p.x + hw); ++x) {
      const int64_t dx = x - p.x, dy = y - p.y;
      if (dx * dx * hh * hh + dy * dy * hw * hw > r2) continue;
      uint16_t& px = cv.px[y * kSide + x];
      px = blend(px, 0, alpha);
    }
}

// Where this frame put him and the newest pellet, for feeding.cpp (reset by draw()).
DishSpots spots_;

// The dish's food, as feeding.cpp's cookie (drawFood): the pack's small dark
// pellet on the floor by his feet read as a dropping to the people who played
// with him. Depth by the floor: food standing higher in the dish than his feet
// is behind him, level with them or lower in front. But food that would sit
// on, under or behind his body, or by his feet, is set down beside him, clear
// of it, where it reads as his dinner and never as something he left.
void drawItems(const Appearance& a, Canvas240& cv, const Disc& d, const Place& him, int foot, bool front) {
  const int clear = foot * 5 / 3 + kFoodFloorR + 10;   // his body's half width, the cookie's, and a gap
  for (int i = 0; i < imin(a.itemCount, 8); ++i) {
    const Place q = placeAt(a.items[i].at, d, Xf{});
    int x = q.x;
    const int dx = q.x - him.x;
    const bool byHim = spots_.him && iabs(dx) < clear && q.y >= spots_.top && q.y <= him.y + 2 * foot;
    if (byHim) x = imax(kSide / 2 - 100, imin(kSide / 2 + 100, him.x + (dx < 0 ? -clear : clear)));
    const bool rotten = a.items[i].what == Appearance::Item::What::RottenPellet;
    if (i == a.feed.flying) {   // the newest: in the air until it lands (feeding.cpp), over him
      spots_.landing = true, spots_.landX = x, spots_.landY = q.y;
      int ax = 0, ay = 0, pct = 100;
      if (pelletAloft(a.feed, x, q.y, ax, ay, pct)) {
        if (front) foodAloft(cv, a.feed, x, q.y, rotten);
        continue;
      }
    }
    const bool inFront = byHim || q.y >= him.y;
    if (inFront != front) continue;
    Place under = q;
    under.x = x;
    shadow(cv, under, kFoodFloorR, 0, 1);
    drawFood(cv, x, q.y - kFoodFloorR, kFoodFloorR, Food{0, rotten});
  }
}

int footHalfW(const FrameRef& f, int kx) { return f.w * kx / kOne * kShadowWidePct / 100; }

// The mouth's centre relative to the stand point, in frame pixels (Q4): the
// mouth-region pixels of the face shown, or of every face of the stage when
// that one has none (a closed mouth). False when the pack tags no mouth.
bool mouthOfFrames(const SpritePack& pack, const FrameRef& shown, blorb::Stage stage, Pt& out) {
  uint16_t count = 0;
  const PaletteEntry* pal = pack.palette(count);
  int64_t sx = 0, sy = 0, n = 0;
  auto add = [&](const FrameRef& f) {
    if (!drawable(f)) return;
    RowReader rows(f);
    for (int y = 0; y < f.h; ++y) {
      const uint8_t* row = rows.seek(y);
      for (int x = 0; x < f.w; ++x)
        if (row[x] && row[x] < count && pal[row[x]].region == blorb::region::mouth.v) {
          sx += (x - f.originX) * 16 + 8;
          sy += (y - f.originY) * 16 + 8;
          ++n;
        }
    }
  };
  add(shown);
  for (size_t i = 0; n == 0 && i < blorb::countOf(blorb::EXPRESSIONS); ++i) add(pack.face(blorb::EXPRESSIONS[i].id, stage));
  if (n == 0) return false;
  out = {int(sx / n), int(sy / n)};
  return true;
}

// The same, kept from frame to frame while he eats: it reads every pixel of
// the face (and of every face of his stage when that one shows no mouth), and
// it is pure in the pack, the face and the stage.
bool mouthOf(const SpritePack& pack, const FrameRef& shown, blorb::Stage stage, Pt& out) {
  static const SpritePack* lastPack = nullptr;
  static FrameRef lastFace{};
  static blorb::Stage lastStage{};
  static bool lastFound = false;
  static Pt lastAt{};
  if (lastPack != &pack || lastStage != stage || !sameFrame(lastFace, shown)) {
    lastPack = &pack, lastFace = shown, lastStage = stage;
    lastFound = mouthOfFrames(pack, shown, stage, lastAt);
  }
  if (lastFound) out = lastAt;
  return lastFound;
}

// While he eats, the food he bit is in his mouth, in front of him, fresh or
// rotten as it lay, and goes as he chews: a bite out of it each third of the
// chew (Eat holds the eat pose for 15 ticks). The bite took it out of the
// dish, so it is drawn here rather than among the items.
constexpr int kChewTicks = 15;   // actions.cpp kChewTicks
// Never low on him, though: a mouth tagged under the body (the octopus's
// beak) holds it up by his eyes, where food reads as food, not as something
// he is leaving behind.
void pelletInMouth(Canvas240& cv, const SpritePack& pack, const Appearance& a, const FrameRef& face, const FrameRef& eyed,
                   const Place& p) {
  Pt m{};
  if (a.mouth == blorb::Mouthful::Nothing || !mouthOf(pack, face, a.stage, m)) return;
  const FrameRef origin{};
  Pt c = project(p.xf, origin, m.x, m.y);
  const int x = p.x + floorDiv(c.x + 8, 16);
  int y = p.y + floorDiv(c.y + 8, 16);
  if (eyed.eyeCount) {
    int eyes = 0;
    for (int e = 0; e < eyed.eyeCount; ++e) eyes += eyed.eyes[e].y;
    const Pt ce = project(p.xf, eyed, eyed.originX * 16, (eyes / eyed.eyeCount) * 16 + 8);
    y = imin(y, p.y + floorDiv(ce.y + 8, 16) + eyed.h * p.xf.ky / kOne * 3 / 10);
  }
  Food food;
  food.rotten = a.mouth == blorb::Mouthful::RottenPellet;
  food.bites = uint8_t(a.pose == blorb::pose::eat ? imin(3, a.poseTick * 4 / kChewTicks) : 0);
  food.biteLeft = a.facing < Fx::zero();
  drawFood(cv, x, y, kFoodFloorR - 1, food);
  spots_.mouth = true, spots_.mouthX = x, spots_.mouthY = y;
}

// His scars, placed on his stage's resting body so they sit still on him
// whatever pose or squash the frame is drawn in.
ScarLayer scarsOn(const Appearance& a, const SpritePack& pack, int scale, uint8_t newest = 0, uint8_t newStrength = 255) {
  uint16_t count = 0;
  const PaletteEntry* pal = pack.palette(count);
  return placeScars(a.scars, pack.body(blorb::pose::idle, a.stage, 0), pal, count, scale, newest, newStrength);
}

void drawCreature(const Appearance& a, const SpritePack& pack, Canvas240& cv) {
  Mottle mottle{a.lifeSeed, 0};
  int8_t spotHue = 0;
  for (int i = 0; i < imin(a.markCount, 8); ++i)
    if (a.marks[i].layer == 0) { mottle.density = a.marks[i].variant; spotHue = a.marks[i].tint.hue; }
  Colours& col = scratchColours();
  buildColours(pack, a.regions, a.stage == Stage::Elder, spotHue, col);

  const blorb::ReflexInfo* reflex = a.reflexActive ? reflexInfo(a.reflex) : nullptr;
  FrameRef bodies[2] = {pack.body(blorb::pose::idle, a.stage, a.poseTick),
                        pack.body(reflex ? reflex->pose : a.pose, a.stage, a.poseTick)};
  constexpr int kFaces = int(blorb::countOf(blorb::EXPRESSIONS));
  FrameRef eyed[2 + kFaces] = {bodies[0], bodies[1]};
  for (int i = 0; i < kFaces; ++i) eyed[2 + i] = pack.face(blorb::EXPRESSIONS[i].id, a.stage);
  constexpr int kMarginQ4 = (kBreathPx + 1) * 16;

  // The envelope, the stand point and the headroom walk every row of both
  // bodies under every transform: a fifth of his frame on the board. They are
  // pure in his bodies, his faces (the pack's, for his stage), his size and
  // where he stands, so they are worked out again only when one of those
  // changes. Two are kept: between ticks he is drawn on the way from where he
  // stood a tick ago to where he stands now (paint/tween.h), and each end is
  // worked out once, the tick it is new.
  struct Memo {
    const SpritePack* pack = nullptr;
    Stage stage{};
    Xf base;
    FrameRef bodies[2]{};
    int32_t atX = 0, atY = 0;
    Disc d;
    Place p;
    Pt q{};   // p before rounding (Q4)
    int maxLift = 0;
  };
  static Memo memos[2];
  static uint8_t newest = 0;
  auto same = [](const FrameRef& x, const FrameRef& y) {
    if (x.rle != y.rle || x.w != y.w || x.h != y.h || x.originX != y.originX || x.originY != y.originY ||
        x.eyeCount != y.eyeCount)
      return false;
    for (int e = 0; e < imin(x.eyeCount, 2); ++e)
      if (x.eyes[e].x != y.eyes[e].x || x.eyes[e].y != y.eyes[e].y || x.eyes[e].r != y.eyes[e].r) return false;
    return true;
  };
  auto placed = [&](blorb::DishPos at) -> const Memo& {
    // Away from home he shrinks a little, easing with his distance from it, so
    // he fits beside the dish's items; at home he fills the panel.
    const int64_t ax = at.x.raw, ay = at.y.raw;
    const int64_t away = imin(Fx::kOne, int(isqrt(uint64_t(ax * ax + ay * ay))));
    const int64_t pct = 100 * int64_t(Fx::kOne) - (100 - kAwayPct) * away;   // x100, Q16
    int k = imax(kMinScale, int(int64_t(a.scalePct) * kOne * pct / (100 * 100 * int64_t(Fx::kOne))));
    Xf base{k, k, 0, 0};
    if (a.stage == Stage::Baby || a.stage == Stage::Child) {
      base.kx = k * kHatchlingWide / kOne;
      base.ky = k * kHatchlingTall / kOne;
    }
    for (int i = 0; i < 2; ++i) {
      const Memo& m = memos[i];
      if (m.pack == &pack && m.stage == a.stage && m.base.kx == base.kx && m.base.ky == base.ky &&
          same(m.bodies[0], bodies[0]) && same(m.bodies[1], bodies[1]) && m.atX == at.x.raw && m.atY == at.y.raw) {
        newest = uint8_t(i);
        return m;
      }
    }
    Xf variants[1 + kReflexMotionCount] = {base};
    for (int i = 0; i < kReflexMotionCount; ++i) {
      const Motion& w = kReflexMotions[i].widest;
      variants[1 + i] = {base.kx * w.wide / kOne, base.ky * w.tall / kOne, 0, 0};
    }
    const Outline figure{bodies, 2, eyed, 2 + kFaces, variants, 1 + kReflexMotionCount};
    newest ^= 1;   // over the one used longer ago
    Memo& m = memos[newest];
    m.pack = &pack, m.stage = a.stage, m.base = base, m.bodies[0] = bodies[0], m.bodies[1] = bodies[1];
    m.atX = at.x.raw, m.atY = at.y.raw;
    m.d = envelope(figure, middle(bodies[0], base), kMarginQ4);
    m.p = placeAt(at, m.d, base);
    m.q = standQ4(at, m.d);
    m.maxLift = imin(kHopMaxPx * base.ky / kOne, headroom(figure, m.p, kMarginQ4));
    return m;
  };
  const Memo& here = placed(a.at);
  const Disc d = here.d;
  Place p = here.p;
  Xf base = here.base;
  const int maxLift = here.maxLift;
  if ((a.behind || a.ahead) && (a.from.x != a.at.x || a.from.y != a.at.y)) {
    // `behind`/256 of the way back to where he stood a tick ago, or walking,
    // `ahead`/256 of a step on past where he stands at the step he took: his
    // feet and his size together (a size snapped to the tick's would flicker
    // his edge at 10 Hz as he walks), his room (d, maxLift) now's.
    // Eased on the stand points before they were rounded to pixels, so he
    // glides by sub-pixel steps rather than stopping and starting on whole
    // ones, and a guess ahead is a straight line, not a pixel's rounding run on.
    const Pt here_q = here.q;
    const Memo& was = placed(a.from);
    const int w = a.ahead ? -imin(a.ahead, 256) : imin(a.behind, 256);
    auto ease = [w](int to, int from) { return to + floorDiv(int64_t(from - to) * w + 128, 256); };
    int64_t qx = ease(here_q.x, was.q.x), qy = ease(here_q.y, was.q.y);
    base.kx = ease(base.kx, was.base.kx), base.ky = ease(base.ky, was.base.ky);
    if (a.ahead) {
      // A step ahead can run past the travel circle his envelope fits in at
      // the rim: held to it, so a guess never puts any of him off the glass.
      const int64_t cx = qx + d.x - kSide * 8, cy = qy + d.y - kSide * 8;
      const int64_t travel = imax(0, kSide * 8 - d.r), r = isqrt(uint64_t(cx * cx + cy * cy));
      if (r > travel) qx = kSide * 8 + cx * travel / r - d.x, qy = kSide * 8 + cy * travel / r - d.y;
    }
    p.x = floorDiv(qx + 8, 16), p.y = floorDiv(qy + 8, 16);
  }
  const FrameRef& body = bodies[1];
  const int foot = footHalfW(body, base.kx);
  spots_.him = true, spots_.x = p.x, spots_.y = p.y, spots_.top = p.y - body.originY * base.ky / kOne;
  drawItems(a, cv, d, p, foot, false);

  Motion m = motionOf(a, maxLift);
  const Lively live = livelyOf(a, maxLift - m.lift);
  // Carried about (carried.cpp): a slide and lean against the badge's
  // acceleration, and now and then a reaction to the ride, on top of the idle layer.
  const Carried carry = carriedOf(a);
  const int carryLift = imin(carry.lift, imax(0, maxLift - m.lift - live.lift));
  if (carry.dxQ4 || carry.dyQ4) {
    // Held to the travel circle his envelope fits in, give or take a few
    // pixels (a lean is a moment), so a slide never puts him off the glass.
    int64_t qx = int64_t(p.x) * 16 + carry.dxQ4, qy = int64_t(p.y) * 16 + carry.dyQ4;
    const int64_t cx = qx + d.x - kSide * 8, cy = qy + d.y - kSide * 8;
    const int64_t travel = imax(0, kSide * 8 - d.r) + kCarrySpillQ4, r = isqrt(uint64_t(cx * cx + cy * cy));
    if (r > travel) qx = kSide * 8 + cx * travel / r - d.x, qy = kSide * 8 + cy * travel / r - d.y;
    p.x = floorDiv(qx + 8, 16), p.y = floorDiv(qy + 8, 16);
  }
  shadow(cv, p, foot, m.lift + live.lift + carryLift, maxLift);
  // An idle's squash (lively.h) and the ride's (carried.h), both on his motion's.
  p.xf = {imax(1, base.kx * m.wide / kOne * (1024 + live.wide) / 1024 * carry.wide / kOne),
          imax(1, base.ky * m.tall / kOne * (1024 + live.tall) / 1024 * carry.tall / kOne),
          m.lift + live.lift + carryLift, 0};
  if (body.h && a.lively) {
    // His breath and sway, from his feet: the height and lean are a share of the frame as drawn.
    const int tallPx = imax(1, body.h * p.xf.ky / kOne);
    p.xf.ky += live.breathQ4 * kOne / (16 * body.h);
    p.xf.shear = (live.swayQ4 + carry.shearQ4) * kOne / (16 * tallPx);
  } else if (body.h) {
    p.xf.ky += breathPx(a) * kOne / body.h;
  }
  const Mottle* spots = mottle.density ? &mottle : nullptr;
  const ScarLayer scars = scarsOn(a, pack, base.kx);
  blit(cv, body, p, col, 256, spots, &scars);
  for (int i = 0; i < imin(a.markCount, 8); ++i)
    if (a.marks[i].layer != 0) blit(cv, pack.mark(a.marks[i].layer, a.marks[i].variant), p, col, 256, nullptr);

  // His face, a pixel to the side when his eyes wander (the idle layer).
  Place look = p;
  look.x += live.gazeX + carry.gaze;
  look.y += live.gazeY;
  FaceShown face = faceFor(a);
  if (carry.face && !eyesGlow(a)) face = {carry.expr, carry.expr, 256};
  FrameRef now = pack.face(face.now, a.stage);
  if (face.mix < 256) blit(cv, pack.face(face.from, a.stage), look, col, 256, spots, &scars);
  blit(cv, now, look, col, face.mix, spots, &scars);
  halo(cv, a, pack, now.eyeCount ? now : body, look);
  pelletInMouth(cv, pack, a, now, now.eyeCount ? now : body, look);
  drawItems(a, cv, d, p, foot, true);
  if (carry.active) {
    CarrySpots at;
    at.x = p.x, at.y = p.y - p.xf.lift, at.top = p.y - p.xf.lift - body.originY * p.xf.ky / kOne;
    at.halfW = body.w * p.xf.kx / kOne / 2;
    const FrameRef& eyes = now.eyeCount ? now : body;
    at.eyes = uint8_t(imin(eyes.eyeCount, 2));
    for (int e = 0; e < at.eyes; ++e) {
      const Pt c = project(look.xf, eyes, eyes.eyes[e].x * 16 + 8, eyes.eyes[e].y * 16 + 8);
      at.ex[e] = look.x + floorDiv(c.x + 8, 16), at.ey[e] = look.y + floorDiv(c.y + 8, 16);
      at.er[e] = eyes.eyes[e].r * look.xf.kx / kOne;
    }
    at.glowing = eyesGlow(a);
    drawCarried(cv, a, at);
  }
}

// The egg's rock: 8/64 of a turn a tick on poseTick (which an egg never moves,
// so a still egg), or on the screen's clock when drawn lively (lively.h).
int eggShear(const Appearance& a, Fx wobble, int lean) {
  const int angleQ8 = a.lively ? livelyRockQ8(a) : int(a.poseTick) * 8 * 256;
  return lean * unit256(wobble) / 256 * sin64q(angleQ8) / kOne;
}

void drawEgg(const Appearance& a, const SpritePack& pack, Canvas240& cv) {
  Colours& col = scratchColours();
  buildColours(pack, a.regions, false, 0, col);
  FrameRef f = pack.egg(a.eggProgress);
  const Xf variants[2] = {{kOne, kOne, 0, kEggLean}, {kOne, kOne, 0, -kEggLean}};
  const Disc d = envelope({&f, 1, &f, 1, variants, 2}, middle(f, Xf{}), 0);
  Place p = placeAt(a.at, d, Xf{});
  drawItems(a, cv, d, p, footHalfW(f, kOne), false);
  Fx shake = a.wobble + (a.eggProgress >= Fx::ratio(2, 3) ? Fx::ratio(3, 10) : Fx::zero());
  p.xf.shear = eggShear(a, shake, kEggLean);
  blit(cv, f, p, col, 256, nullptr);
  halo(cv, a, pack, f, p);
  drawItems(a, cv, d, p, footHalfW(f, kOne), true);
}

void drawRemains(const Appearance& a, const SpritePack& pack, Canvas240& cv) {
  Colours& col = scratchColours();
  buildColours(pack, a.regions, false, 0, col);
  FrameRef f = pack.remains();
  const Xf rest{};
  const Disc d = envelope({&f, 1, nullptr, 0, &rest, 1}, middle(f, rest), 0);
  Place p = placeAt(a.at, d, rest);
  drawItems(a, cv, d, p, footHalfW(f, kOne), false);
  blit(cv, f, p, col, 256 - unit256(a.remainsFade), nullptr);
  drawItems(a, cv, d, p, footHalfW(f, kOne), true);
}

void fill(Canvas240& cv, int x0, int y0, int x1, int y1, const Rgb& c, int alpha) {   // half-open
  uint16_t c565 = to565(c);
  for (int y = imax(0, y0); y < imin(kSide, y1); ++y)
    for (int x = imax(0, x0); x < imin(kSide, x1); ++x) {
      uint16_t& px = cv.px[y * kSide + x];
      px = alpha >= 256 ? c565 : blend(px, c565, alpha);
    }
}

// Each egg in its own species' art: a friend egg can be another species' than
// the parent whose clutch it joins.
void drawClutch(const Appearance& a, Canvas240& cv) {
  const int n = imin(a.eggCount, blorb::kMaxClutch);
  const Rgb cream{kHintColour[0], kHintColour[1], kHintColour[2]};
  for (int i = 0; i < n; ++i) {
    const SpritePack& pack = packFor(a.eggs[i].species);
    const FrameRef f = pack.egg(Fx::zero());
    Tint tints[blorb::kRegionCount]{};
    tints[blorb::region::skin.v] = a.eggs[i].skin;
    tints[blorb::region::cloak.v] = a.eggs[i].cloak;
    tints[blorb::region::shell.v] = a.eggs[i].shell;
    Colours& col = scratchColours();
    buildColours(pack, tints, false, 0, col);
    Place p;
    p.xf.kx = p.xf.ky = imin(kOne, kOne * kClutchEggPx / imax(1, f.h));
    p.x = kSide / 2 + (2 * i - (n - 1)) * kClutchGapPx / 2;
    p.y = kSide / 2 + (f.originY - f.h / 2) * p.xf.ky / kOne;
    bool chosen = i == a.cursor;
    if (chosen) p.xf.shear = eggShear(a, Fx::ratio(1, 3), kEggLean);
    blit(cv, f, p, col, 256, nullptr);
    if (chosen)
      for (int row = 0; row < 6; ++row) fill(cv, p.x - row, p.y + 5 + row, p.x + row + 1, p.y + 6 + row, cream, 256);
  }
}

bool inDish(int x, int y) {
  int dx = 2 * x + 1 - kSide, dy = 2 * y + 1 - kSide;
  return dx * dx + dy * dy <= kSide * kSide;
}

// The first column of row y inside the dish (inDish), and so, by symmetry,
// the last is kSide - 1 - it. Every pixel left of it and right of the last is outside.
int dishFrom(int y) { return kDish.from[y]; }

// A dark bog-teal dish, lighter at the middle, with a faint glassy rim,
// ordered-dithered so the gradient does not band in RGB565.
constexpr uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
constexpr int kBgStep = kSide * kSide / 256;   // d2 per step of t
constexpr int kRimFrom = 228 * 228;            // d2 below this has no rim: |isqrt(d2) - 233| >= 6
constexpr int kInnerT = (kRimFrom - 1) / kBgStep + 1;   // the steps of t inside the rim: 0..231

// d2 / kBgStep as a multiply and a shift (no divide) for every d2 inside the
// rim; checked for each at compile time.
constexpr uint32_t kBgRecip = (uint32_t(1) << 24) / kBgStep + 1;
constexpr uint32_t bgT(uint32_t d2) { return d2 * kBgRecip >> 24; }
constexpr bool bgTExact() {
  for (uint32_t d2 = 0; d2 < uint32_t(kRimFrom); ++d2)
    if (bgT(d2) != d2 / kBgStep) return false;
  return true;
}
static_assert(bgTExact(), "bgT is d2 / kBgStep inside the rim");

// Row y's first column with d2 < kRimFrom (kSide / 2 when none): the dish
// inside the rim, where no pixel needs a root or a clamp.
constexpr DishSpans rimSpans() {
  DishSpans s{};
  for (int y = 0; y < kSide; ++y) {
    const int dy = 2 * y + 1 - kSide;
    int x = 0;
    while (x < kSide / 2 && (2 * x + 1 - kSide) * (2 * x + 1 - kSide) + dy * dy >= kRimFrom) ++x;
    s.from[y] = uint8_t(x);
  }
  return s;
}
constexpr DishSpans kRimSpans = rimSpans();

// The rim's band: each pixel left of the middle (its mirror has the same d2)
// as its step of t from the band's first (231) and its glass (0..6, the
// light the glass adds: 6 - |isqrt(d2) - 233| when positive), at compile
// time: t * 7 + glass. 2.2 KB of flash, row after row from the dish's edge.
constexpr int kRimT0 = kRimFrom / kBgStep, kRimTs = kSide * kSide / kBgStep - kRimT0 + 1, kRimGlass = 7;
constexpr int rimPixels() {
  int n = 0;
  for (int y = 0; y < kSide; ++y) n += kRimSpans.from[y] - kDish.from[y];
  return n;
}
struct RimCodes { uint16_t rowAt[kSide]; uint8_t code[rimPixels()]; };
constexpr int isqrtC(int v) {
  int lo = 0, hi = 256;   // the root is in [lo, hi): v is at most 240 * 240
  while (hi - lo > 1) {
    const int mid = (lo + hi) / 2;
    if (mid * mid <= v) lo = mid; else hi = mid;
  }
  return lo;
}
constexpr RimCodes rimCodes() {
  RimCodes r{};
  int n = 0;
  for (int y = 0; y < kSide; ++y) {
    r.rowAt[y] = uint16_t(n);
    const int dy = 2 * y + 1 - kSide;
    for (int x = kDish.from[y]; x < kRimSpans.from[y]; ++x) {
      const int dx = 2 * x + 1 - kSide, d2 = dx * dx + dy * dy, rim = 6 - iabs(isqrtC(d2) - 233);
      r.code[n++] = uint8_t((d2 / kBgStep - kRimT0) * kRimGlass + (rim > 0 ? rim : 0));
    }
  }
  return r;
}
constexpr RimCodes kRimCodes = rimCodes();
static_assert(kRimTs * kRimGlass <= 256, "a rim code is a byte");

// The dish lit, at each step out from the middle. The rim's glass and the
// dither land per pixel; inside the rim no channel comes near 255, so the
// clamp is the rim's alone.
uint8_t litR[257], litG[257], litB[257];
// The rim's colour, lit, before the dither, for each code (kRimCodes).
uint8_t rimR[kRimTs * kRimGlass], rimG[kRimTs * kRimGlass], rimB[kRimTs * kRimGlass];
void backgroundRows(Canvas240& cv, int lit, int half);

void background(Canvas240& cv, Fx night) {
  const int lit = 256 - (blorb::clamp01(night).raw >> 17);
  for (int t = kRimT0; t < kRimT0 + kRimTs; ++t)
    for (int glass = 0; glass < kRimGlass; ++glass) {
      Rgb c{22 - 10 * t / 256 + 4 * glass, 58 - 22 * t / 256 + 8 * glass, 56 - 20 * t / 256 + 7 * glass};
      const int i = (t - kRimT0) * kRimGlass + glass;
      rimR[i] = uint8_t(c.r * lit / 256), rimG[i] = uint8_t(c.g * lit / 256), rimB[i] = uint8_t(c.b * lit / 256);
    }
  for (int t = 0; t <= 256; ++t) {
    litR[t] = uint8_t((22 - 10 * t / 256) * lit / 256);
    litG[t] = uint8_t((58 - 22 * t / 256) * lit / 256);
    litB[t] = uint8_t((56 - 20 * t / 256) * lit / 256);
  }
  // Inside the rim a pixel is its step of t and its dither, nothing else: one
  // row of the dither at a time, every pixel for each step and each of the
  // row's four dithers is worked out once (1.9 KB), and the rows that share
  // that dither look theirs up. A row is symmetric about the middle, so each
  // step is found once for a pixel and its mirror (whose dither column is 3 - its own).
  struct Job { Canvas240& cv; int lit; };
  Job job{cv, lit};
  // Two halves, dither rows 0 and 1 in one and 2 and 3 in the other, each with its table.
  bothHalves([](void* j, int half) { backgroundRows(static_cast<Job*>(j)->cv, static_cast<Job*>(j)->lit, half); }, &job);
}

void backgroundRows(Canvas240& cv, int lit, int half) {
  static uint16_t lutOf[2][kInnerT][4];
  auto& lut = lutOf[half];
  for (int by = 2 * half; by < 2 * half + 2; ++by) {
    for (int t = 0; t < kInnerT; ++t)
      for (int k = 0; k < 4; ++k) {
        const int d = kBayer[by][k];
        lut[t][k] = uint16_t(((litR[t] + d / 2) >> 3) << 11 | ((litG[t] + d / 4) >> 2) << 5 | (litB[t] + d / 2) >> 3);
      }
    for (int y = by; y < kSide; y += 4) {
      uint16_t* row = &cv.px[y * kSide];
      const int from = kDish.from[y], to = kSide - from;   // [from, to) is inside
      const int in = kRimSpans.from[y];                    // [in, kSide - in) is inside the rim
      std::memset(row, 0, sizeof(uint16_t) * size_t(from));
      std::memset(row + to, 0, sizeof(uint16_t) * size_t(kSide - to));
      const int dy = 2 * y + 1 - kSide;
      const uint8_t* bayer = kBayer[by];
      // The rim's band, a pixel and its mirror (same code, its own dither).
      const uint8_t* code = &kRimCodes.code[kRimCodes.rowAt[y]];
      for (int x = from; x < in; ++x) {
        const int c = code[x - from];
        for (const int at : {x, kSide - 1 - x}) {
          const int d = bayer[at & 3];
          row[at] = to565({imin(255, rimR[c] + d / 2), imin(255, rimG[c] + d / 4), imin(255, rimB[c] + d / 2)});
        }
      }
      // Inside the rim, from its left edge to the middle, each pixel and its mirror.
      uint32_t d2 = uint32_t((2 * in + 1 - kSide) * (2 * in + 1 - kSide) + dy * dy);
      uint32_t step = uint32_t(4 * (2 * in + 1 - kSide) + 4);   // d2 at x + 1 less d2 at x, a 2's complement step
      int x = in;
      auto pair = [&](int k) {
        const uint16_t* l = lut[bgT(d2)];
        row[x] = l[k];
        row[kSide - 1 - x] = l[3 - k];
        d2 += step, step += 8, ++x;
      };
      while (x < kSide / 2 && (x & 3)) pair(x & 3);
      while (x < kSide / 2) pair(0), pair(1), pair(2), pair(3);
    }
  }
}

void mask(Canvas240& cv) {
  for (int y = 0; y < kSide; ++y) {
    const int from = dishFrom(y);
    std::memset(&cv.px[y * kSide], 0, sizeof(uint16_t) * size_t(from));
    std::memset(&cv.px[y * kSide + kSide - from], 0, sizeof(uint16_t) * size_t(from));
  }
}

// Pantry pellets as pips along the top of the rim.
void pips(Canvas240& cv, uint8_t pantry) {
  const int n = imin(pantry, kMaxPips);
  const Rgb c{kPipColour[0], kPipColour[1], kPipColour[2]};
  for (int k = 0; k < n; ++k) {
    int dx = (2 * k - (n - 1)) * 9 / 2;
    int x = kSide / 2 + dx, y = kSide / 2 - int(isqrt(uint64_t(108 * 108 - dx * dx)));
    fill(cv, x - 1, y - 2, x + 1, y + 2, c, 256);
    fill(cv, x - 2, y - 1, x + 2, y + 1, c, 256);
  }
}

// 9x9 gesture glyphs, drawn at 2x: what the body should do, and how many
// knocks it takes -- a dot per knock under the glyph (controls.h), so a child
// can learn knock once versus knock knock by counting, not reading.
// The button's action is pressed no more (the cases cover it): its mark is its
// twin, the knock knock, two dots.
struct Glyph9 { blorb::StimId gesture; uint8_t knocks; uint16_t rows[9]; };
constexpr Glyph9 kGestureGlyphs[] = {
    {blorb::stim::button, 2, {0x111, 0x092, 0x000, 0x183, 0x000, 0x07C, 0x0FE, 0x0FE, 0x07C}},
    {blorb::stim::cradle, 0, {0x0C6, 0x1FF, 0x1FF, 0x1FF, 0x0FE, 0x07C, 0x038, 0x010, 0x000}},
    {blorb::stim::knock, 1, {0x111, 0x092, 0x000, 0x183, 0x000, 0x07C, 0x0FE, 0x0FE, 0x07C}},
    {blorb::stim::lid_down, 0, {0x038, 0x060, 0x0C0, 0x0C0, 0x0C0, 0x0C0, 0x060, 0x038, 0x000}},
};

void hint(Canvas240& cv, const Appearance& a) {
  if (!a.hasHint) return;
  for (const blorb::CareInfo& care : blorb::CARES) {
    if (care.id != a.hint) continue;
    for (const Glyph9& g : kGestureGlyphs) {
      if (g.gesture != care.gesture) continue;
      int alpha = 112 + unit256(a.hintUrgency) * 144 / 256;
      const Rgb c{kHintColour[0], kHintColour[1], kHintColour[2]};
      for (int r = 0; r < 9; ++r)
        for (int b = 0; b < 9; ++b)
          if (g.rows[r] & (0x100 >> b)) fill(cv, 111 + 2 * b, 205 + 2 * r, 113 + 2 * b, 207 + 2 * r, c, alpha);
      // One 4 px dot per knock, 6 px apart, centred under the glyph (whose
      // columns are 111..128): the count reads at a glance.
      for (int k = 0; k < g.knocks; ++k) {
        const int x = 118 - 3 * (g.knocks - 1) + 6 * k;
        fill(cv, x, 227, x + 4, 230, c, alpha);
      }
    }
  }
}

// The marquee: a scrolling line across the dish above his head, on a dim
// band that runs rim to rim, each row to its own chord, and fades out toward
// the glass, so neither the band nor a letter ends on a straight edge.
int chordHalf(int y) { return int(isqrt(uint64_t(kSide * kSide - (2 * y + 1 - kSide) * (2 * y + 1 - kSide)) / 4)); }

void dimBand(Canvas240& cv, const Marquee& strip) {
  const int top = kSide / 2 + strip.dy - strip.clipH / 2;
  for (int y = top; y < top + strip.clipH; ++y) {
    const int half = chordHalf(y);
    for (int x = kSide / 2 - half; x < kSide / 2 + half; ++x) {
      const int toRim = imin(x - (kSide / 2 - half), kSide / 2 + half - 1 - x);
      uint16_t& px = cv.px[y * kSide + x];
      px = blend(px, 0, 140 * imin(256, toRim * 256 / kMarqueeFeather) / 256);
    }
  }
}

// A thought crosses once: thoughtPhase 0..1 carries it in from the right rim
// and out past the left, on the strip above his head.
void thoughtLine(Canvas240& cv, const blorb::Appearance& a, bool low) {
  Marquee strip = kMarqueeStrip;
  if (low) strip.dy = -strip.dy;
  dimBand(cv, strip);
  const int width = 2 * marqueeHalf(strip);
  const int64_t span = width + textWidth(kFreeSans9pt7b, a.line);
  const int off = int(int64_t(a.thoughtPhase.raw) * span / Fx::kOne);
  // Placed by lead at zero offset: the ring's offset wraps at text + half, a
  // strip short of the crossing, and would bring the line back in from the right.
  drawMarqueeRing(cv, strip, kFreeSans9pt7b, a.line, "", to565({kMarqueeColour[0], kMarqueeColour[1], kMarqueeColour[2]}),
                  width - off, 0);
}

// Why a pet in fight mode is not up for a fight, in words a kid reads.
const char* notReady(blorb::Decline d) {
  using D = blorb::Decline;
  switch (d) {
    case D::Asleep: case D::Sleepy: return "TOO SLEEPY TO FIGHT";
    case D::Hungry: return "TOO HUNGRY - KNOCK KNOCK ON ME TO FEED";
    case D::Resting: return "RESTING";
    case D::WantsACuddle: return "NEEDS A CUDDLE";
    default: return "NOT READY YET";
  }
}

// Why a double press opened no fight mode at all.
const char* noFightLine(blorb::NoFight n) {
  using N = blorb::NoFight;
  switch (n) {
    case N::Egg: return "STILL AN EGG - HATCHING SOON";
    case N::Nest: return "PICK AN EGG FIRST";
    case N::Remains: return "NOT NOW";
    case N::TooSleepy: return "ZZZ... TOO SLEEPY TO FIGHT";
    case N::Updating: return "UPDATE FINISHING - FIGHT MODE SOON";
    case N::None: break;
  }
  return "";
}

void noFightStrip(Canvas240& cv, const blorb::Appearance& a, uint32_t nowMs) {
  Marquee strip = kMarqueeStrip;
  if (a.at.y < Fx::zero()) strip.dy = -strip.dy;
  dimBand(cv, strip);
  char s[64];
  std::snprintf(s, sizeof s, "%s  ", noFightLine(a.noFight));
  drawMarqueeAt(cv, strip, kFreeSans9pt7b, s, to565({kMarqueeColour[0], kMarqueeColour[1], kMarqueeColour[2]}), nowMs);
}

// Fight mode keeps the dish, so it has to say what it is doing: a line saying
// it is looking for a fight (or connecting, or why not yet), the time until it
// gives up, how to stop, and a sonar ring pulsing out to the rim.
void lobbyLine(Canvas240& cv, const blorb::Appearance& a, uint32_t nowMs) {
  const uint16_t colour = to565({kMarqueeColour[0], kMarqueeColour[1], kMarqueeColour[2]});
  constexpr uint32_t kPulseMs = 1200;
  const int r = 70 + int(nowMs % kPulseMs) * 48 / int(kPulseMs);
  const int fade = 200 - int(nowMs % kPulseMs) * 180 / int(kPulseMs);
  // The pixels with (r - 3)^2 <= d2 <= (r + 3)^2 (from the pixel at 120,120):
  // on each row, |dx| from n to m by two roots, not a test of every pixel.
  const int lo2 = (r - 3) * (r - 3), hi2 = (r + 3) * (r + 3);
  for (int y = 0; y < kSide; ++y) {
    const int dy = y - kSide / 2, a = hi2 - dy * dy, b = lo2 - dy * dy;
    if (a < 0) continue;
    const int m = int(isqrt32(uint32_t(a)));                    // |dx| <= m
    const int n = b <= 0 ? 0 : int(isqrt32(uint32_t(b - 1))) + 1;   // |dx| >= n: the least n with n * n >= b
    uint16_t* row = &cv.px[y * kSide];
    auto run = [&](int dx0, int dx1) {   // dx0..dx1, clipped to the canvas
      for (int x = imax(0, kSide / 2 + dx0); x <= imin(kSide - 1, kSide / 2 + dx1); ++x) row[x] = blend(row[x], colour, fade);
    };
    if (n > m) continue;
    if (n == 0) run(-m, m);
    else run(-m, -n), run(n, m);
  }
  Marquee strip = kMarqueeStrip;
  if (a.at.y < Fx::zero()) strip.dy = -strip.dy;
  dimBand(cv, strip);
  char s[112];
  const unsigned secs = unsigned((a.lobbyTicksLeft + 9) / 10);
  if (a.lobbyConnecting)
    std::snprintf(s, sizeof s, "FIGHT FOUND! GET READY...  ");
  else if (a.lobbyDecline == blorb::Decline::Resting) {
    const unsigned rest = unsigned((a.restTicksLeft + 9) / 10);
    std::snprintf(s, sizeof s, "FIGHT MODE - RESTING %u:%02u  HEARD %u  HOLD ME UPSIDE DOWN TO STOP  ", rest / 60, rest % 60,
                  unsigned(a.lobbyFriends));
  } else if (a.lobbyDecline != blorb::Decline::None)
    std::snprintf(s, sizeof s, "FIGHT MODE - %s  HEARD %u  %u:%02u  HOLD ME UPSIDE DOWN TO STOP  ", notReady(a.lobbyDecline),
                  unsigned(a.lobbyFriends), secs / 60, secs % 60);
  else
    std::snprintf(s, sizeof s, "FIGHT MODE - LOOKING FOR A FIGHT...  HEARD %u  %u:%02u  HOLD ME UPSIDE DOWN TO STOP  ",
                  unsigned(a.lobbyFriends), secs / 60, secs % 60);
  drawMarqueeAt(cv, strip, kFreeSans9pt7b, s, colour, nowMs);
}

// The red ring's band, 7 px round the rim with a 1 px dark inner edge, in
// half pixels from the centre so it is round to the pixel: on row y, from the
// left, [from, inner) is red, [inner, edge) the dark edge, and the right is
// its mirror. Compile-time spans, so a ring is fills, not a test a pixel.
constexpr int kRingWidth = 7;
constexpr int kRingOuter = kSide * kSide, kRingInner = (kSide - 2 * kRingWidth) * (kSide - 2 * kRingWidth),
              kRingEdge = (kSide - 2 * kRingWidth - 2) * (kSide - 2 * kRingWidth - 2);
// Row y's first column with d2 below `limit` (kSide / 2 when none).
constexpr DishSpans spansBelow(int limit) {
  DishSpans s{};
  for (int y = 0; y < kSide; ++y) {
    const int dy = 2 * y + 1 - kSide;
    int x = 0;
    while (x < kSide / 2 && (2 * x + 1 - kSide) * (2 * x + 1 - kSide) + dy * dy >= limit) ++x;
    s.from[y] = uint8_t(x);
  }
  return s;
}
constexpr DishSpans kRingFrom = spansBelow(kRingOuter), kRingRed = spansBelow(kRingInner), kRingDark = spansBelow(kRingEdge);
constexpr int ringPixels() {
  int n = 0;
  for (int y = 0; y < kSide; ++y) n += 2 * (kRingDark.from[y] - kRingFrom.from[y]);
  return n;
}
constexpr int kRingPixels = ringPixels();

// Fight mode, still unpaired (resting included: he is still searching): a
// solid red ring round the whole rim, 7 px, readable across a room, its
// brightness breathing but always red. Over everything the dish draws; BOOT's
// hint is drawn after it (main.cpp), so it stays legible. A dark inner edge
// keeps it apart from a red pet.
void searchingRing(Canvas240& cv, uint32_t nowMs) {
  constexpr uint32_t kBreathMs = 1600;
  const int phase = int(nowMs % kBreathMs), half = int(kBreathMs / 2);
  const int lift = phase < half ? phase : int(kBreathMs) - phase;   // 0..800
  const uint16_t red = to565({170 + lift * 85 / half, 0, 0}), edge = to565({50, 0, 0});
  for (int y = 0; y < kSide; ++y) {
    uint16_t* row = &cv.px[y * kSide];
    const int from = kRingFrom.from[y], inner = kRingRed.from[y], dark = kRingDark.from[y];
    for (int x = from; x < inner; ++x) row[x] = row[kSide - 1 - x] = red;
    for (int x = inner; x < dark; ++x) row[x] = row[kSide - 1 - x] = edge;
  }
}

// A hang filling up: the searching ring's band, swept clockwise from the
// top by exactly the hang's charge, in the same steady red -- the kid
// holding him upside down watches the ask land, and at full the ring
// completes and fight mode opens (the lobby's breathing ring takes over).
// It is a gauge, not a pulse: no breathing, so what fills is what he holds.
//
// Each band pixel's turn from the top is the single-precision atan2's (the
// FPU's), worked out once, the first time a hang is drawn: 23 KB, from the
// heap (PSRAM on the board). The double atan2 is soft float on the board,
// thousands of cycles a pixel, and the float is within a millionth of a turn
// of it, so only a pixel within kSure of the edge of the fill needs the double
// to say which side it falls: the same pixels as a double a pixel.
void hangRing(Canvas240& cv, Fx charge) {
  const uint16_t red = to565({220, 0, 0}), edge = to565({50, 0, 0});
  const double filled = double(blorb::clamp01(charge).raw) / double(Fx::one().raw);
  constexpr float kSure = 1e-4f;
  const float filledF = float(filled);
  // The band in the order drawn: each row's left run from the rim inward, then its mirror's.
  auto band = [](auto&& visit) {
    for (int y = 0; y < kSide; ++y) {
      const int from = kRingFrom.from[y], dark = kRingDark.from[y];
      for (int x = from; x < dark; ++x) visit(x, y);
      for (int x = kSide - dark; x < kSide - from; ++x) visit(x, y);
    }
  };
  static float* turn = nullptr;
  if (!turn) {
    turn = new float[kRingPixels];
    int k = 0;
    band([&](int x, int y) {
      float rough = std::atan2(float(2 * x + 1 - kSide), float(-(2 * y + 1 - kSide))) / 6.2831853f;
      if (rough < 0) rough += 1.0f;
      turn[k++] = rough;
    });
  }
  int k = 0;
  band([&](int x, int y) {
    const float rough = turn[k++];
    if (rough > filledF + kSure) return;
    const int dx = 2 * x + 1 - kSide, dy = 2 * y + 1 - kSide;
    if (rough >= filledF - kSure) {
      double sweep = std::atan2(double(dx), double(-dy)) / 6.283185307179586;   // 0 at the top, clockwise
      if (sweep < 0) sweep += 1.0;
      if (sweep > filled) return;
    }
    cv.px[y * kSide + x] = dx * dx + dy * dy >= kRingInner ? red : edge;
  });
}

// Boot::Sealed: a keepsake is on the flash and this boot could not open it.
// The pet sleeps in a drop of amber, safe and whole, and the line under him is
// for whoever can plug him into a computer. Warm and still on purpose: nothing
// here may read as the vigil, or as a new egg that replaced him.
constexpr uint8_t kAmber[3] = {255, 150, 20};
constexpr int kAmberBehind = 150, kAmberOver = 60;   // the drop deep behind him, a light tint over him
constexpr char kSealedMarquee[] = "SAFE BUT STUCK - ASK A GROWN-UP";

void amber(Canvas240& cv, int alpha) {
  const uint16_t c = to565({kAmber[0], kAmber[1], kAmber[2]});
  for (int y = 0; y < kSide; ++y)
    for (int x = dishFrom(y); x < kSide - dishFrom(y); ++x) cv.px[y * kSide + x] = blend(cv.px[y * kSide + x], c, alpha);
}

// Over him: the tint, two glints where light would catch a polished drop, the
// z's a six-year-old reads as sleeping before any word, and the line.
void sealedInAmber(Canvas240& cv, uint32_t nowMs) {
  amber(cv, kAmberOver);
  const Rgb cream{255, 236, 180};
  const uint16_t light = to565(cream);
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x) {
      const int diag = x + y, across = x - y;
      if (inDish(x, y) && iabs(across) < 70 && ((diag > 92 && diag < 104) || (diag > 112 && diag < 117)))
        cv.px[y * kSide + x] = blend(cv.px[y * kSide + x], light, 70);
    }
  const int rise = int(nowMs / 250 % 6);
  const Clip all{0, 0, kSide, kSide, 0};
  drawString(cv, kFreeSans9pt7b, "z", 158, 60 - rise, light, all);
  drawString(cv, kFreeSans9pt7b, "Z", 172, 42 - rise, light, all);
  Marquee strip = kMarqueeStrip;
  strip.dy = -strip.dy;
  dimBand(cv, strip);
  drawMarqueeAt(cv, strip, kFreeSans9pt7b, kSealedMarquee, to565({kMarqueeColour[0], kMarqueeColour[1], kMarqueeColour[2]}),
                nowMs);
}

// The battle art's colours are fixed, so one LUT serves every frame of it.
const Colours& artColours() {
  static const Colours c = [] {
    Colours o;
    std::memset(&o, 0, sizeof(o));
    std::memset(o.region, 255, sizeof(o.region));
    for (int i = 0; i < imin(duel_art::kPaletteCount, 256); ++i)
      o.base[i] = o.spot[i] = to565({duel_art::kPalette[i].r, duel_art::kPalette[i].g, duel_art::kPalette[i].b});
    return o;
  }();
  return c;
}

// What his line's duel book puts on the rim at home: the newest trophies down
// the lower right, the debt and vendetta marks down the lower left, clear of
// the care hint at the bottom.
constexpr int kTokenPct = 75;
constexpr int kTokenAt[3][2] = {{207, 170}, {191, 191}, {170, 207}};
constexpr int kDebtAt[2] = {33, 170}, kVendettaAt[2] = {49, 191};

void rimTokens(Canvas240& cv, const Appearance& a) {
  for (int i = 0; i < imin(a.trophyCount, Appearance::kRimTrophies); ++i)
    drawArt(cv, duel_art::kTrophies[a.trophies[i].v % blorb::kSpeciesCount], kTokenAt[i][0], kTokenAt[i][1], kTokenPct, 256);
  if (a.owesDebt) drawArt(cv, duel_art::kDebt, kDebtAt[0], kDebtAt[1], kTokenPct, 256);
  if (a.vendetta) drawArt(cv, duel_art::kVendetta, kVendettaAt[0], kVendettaAt[1], kTokenPct, 256);
}

}  // namespace

void drawArt(Canvas240& cv, const FrameRef& f, int x, int y, int scalePct, int alpha, int fillRows) {
  if (!drawable(f)) return;
  FrameRef g = f;
  if (fillRows < 255) {
    const int skip = f.h - f.h * imax(0, fillRows) / 255;
    for (int row = 0; row < skip; ++row)
      for (int px = 0; px < f.w; px += g.rle[0], g.rle += 2) {}
    g.h = uint16_t(f.h - skip);
    g.originY = int16_t(f.originY - skip);
    if (!g.h) return;
  }
  Place p;
  p.x = x;
  p.y = y;
  p.xf.kx = p.xf.ky = imax(kMinScale, kOne * scalePct / 100);
  blit(cv, g, p, artColours(), alpha, nullptr);
}

void drawFigure(const Appearance& a, const SpritePack& pack, Canvas240& cv, const Figure& fig) {
  Mottle mottle{a.lifeSeed, 0};
  int8_t spotHue = 0;
  for (int i = 0; i < imin(a.markCount, 8); ++i)
    if (a.marks[i].layer == 0) { mottle.density = a.marks[i].variant; spotHue = a.marks[i].tint.hue; }
  Colours& col = scratchColours();
  buildColours(pack, a.regions, a.stage == Stage::Elder, spotHue, col);
  if (fig.flash)
    for (int i = 0; i < 256; ++i) {
      col.base[i] = blend(col.base[i], 0xFFFF, fig.flash);
      col.spot[i] = blend(col.spot[i], 0xFFFF, fig.flash);
    }
  const int k = imax(kMinScale, int(int64_t(a.scalePct) * fig.scalePct * kOne / 10000));
  Xf base{k, k, 0, 0};
  if (a.stage == Stage::Baby || a.stage == Stage::Child) {
    base.kx = k * kHatchlingWide / kOne;
    base.ky = k * kHatchlingTall / kOne;
  }
  Place p;
  p.x = fig.x;
  p.y = fig.y;
  p.xf = base;
  if (fig.remainsIn) blit(cv, pack.remains(), p, col, (fig.alpha + 1) * (fig.remainsIn + 1) / 256, nullptr);
  if (fig.remainsIn == 255) return;

  const FrameRef body = pack.body(a.pose, a.stage, a.poseTick);
  shadow(cv, p, footHalfW(body, base.kx), fig.lift, kHopMaxPx * base.ky / kOne);
  p.xf = {imax(1, base.kx * fig.widePct / 100), imax(1, base.ky * fig.tallPct / 100), fig.lift, fig.leanPct * kOne / 100};
  // The staging owns his motion here; of the idle layer only his breath goes on (and the blinks, in faceFor).
  if (body.h && a.lively) p.xf.ky += livelyOf(a, 0).breathQ4 * kOne / (16 * body.h);
  else if (body.h) p.xf.ky += breathPx(a) * kOne / body.h;
  const Mottle* spots = mottle.density ? &mottle : nullptr;
  const ScarLayer scars = scarsOn(a, pack, base.kx, fig.newScars, fig.scarIn);
  const int alpha = (fig.alpha + 1) * (256 - fig.remainsIn) / 256;
  blit(cv, body, p, col, alpha, spots, &scars);
  for (int i = 0; i < imin(a.markCount, 8); ++i)
    if (a.marks[i].layer != 0) blit(cv, pack.mark(a.marks[i].layer, a.marks[i].variant), p, col, alpha, nullptr);
  const FaceShown face = fig.hasFace ? FaceShown{fig.face, fig.face, 256} : faceFor(a);
  const FrameRef now = pack.face(face.now, a.stage);
  if (face.mix < 256) blit(cv, pack.face(face.from, a.stage), p, col, alpha, spots, &scars);
  blit(cv, now, p, col, face.mix * alpha / 256, spots, &scars);
  halo(cv, a, pack, now.eyeCount ? now : body, p);
}

PhaseClock phaseClock;
Parallel parallel;

void draw(const blorb::Appearance& a, const SpritePack& pack, Canvas240& cv, uint32_t nowMs) {
  phaseStart();
  spots_ = DishSpots{};
  background(cv, a.night);
  phaseEnd(Phase::Background);
  if (a.sealed) {
    amber(cv, kAmberBehind);
    phaseEnd(Phase::Background);
    drawCreature(a, pack, cv);
    phaseEnd(Phase::Figure);
    sealedInAmber(cv, nowMs);
    phaseEnd(Phase::Text);
    mask(cv);
    return phaseEnd(Phase::Mask);
  }
  switch (a.kind) {
    case Kind::Creature:
      drawCreature(a, pack, cv);
      drawIdleProps(cv, a, spots_);   // idle_props.cpp: what he plays with
      break;
    case Kind::Egg: drawEgg(a, pack, cv); break;
    case Kind::Remains: drawRemains(a, pack, cv); break;
    case Kind::Clutch: drawClutch(a, cv); break;
  }
  phaseEnd(Phase::Figure);
  pips(cv, a.pantry);
  rimTokens(cv, a);
  hint(cv, a);
  phaseEnd(Phase::Rim);
  // Low when he stands high in the dish, so the line never crosses his face.
  if (a.lobby) lobbyLine(cv, a, nowMs);
  else if (a.noFight != blorb::NoFight::None) noFightStrip(cv, a, nowMs);
  else if (a.thinking && a.line[0]) thoughtLine(cv, a, a.at.y < Fx::zero());
  phaseEnd(Phase::Text);
  if (a.lobby && !a.lobbyConnecting) searchingRing(cv, nowMs);   // gone the moment a pair locks
  else if (a.hangCharge > Fx::zero()) hangRing(cv, a.hangCharge);   // a hang filling up, short of the lobby
  phaseEnd(Phase::Rings);
  drawFeeding(cv, a, spots_);   // feeding.cpp: the knock knock heard, the pellet, the chomp, his belly
  mask(cv);
  phaseEnd(Phase::Mask);
}

namespace {

// The idle bar's ring: in half pixels from the centre, the pixels whose centres
// sit between 117 and 118 px out, the outermost the round glass shows whole.
constexpr int kIdleOuter = (kSide - 2) * (kSide - 2), kIdleInner = (kSide - 4) * (kSide - 4);
constexpr bool inIdleRing(int x, int y) {
  const int dx = 2 * x + 1 - kSide, dy = 2 * y + 1 - kSide, d2 = dx * dx + dy * dy;
  return d2 < kIdleOuter && d2 >= kIdleInner;
}
constexpr int idleRingCount() {
  int n = 0;
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x) n += inIdleRing(x, y);
  return n;
}
constexpr int kIdleRing = 764;
static_assert(idleRingCount() == kIdleRing, "the idle bar's band holds kIdleRing pixels");

// Each ring pixel and the least permille that lights it, worked out once at
// start-up rather than inside the first frame: a double atan2 is soft float on
// the board, thousands of cycles a pixel. 764 pixels, 3 KB.
struct IdleLit { uint16_t at, from; };
struct IdleRing { IdleLit px[kIdleRing]; };
IdleRing buildIdleRing() {
  IdleRing r{};
  int n = 0;
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x) {
      if (!inIdleRing(x, y)) continue;
      const int dx = 2 * x + 1 - kSide, dy = 2 * y + 1 - kSide;
      double sweep = std::atan2(double(dx), double(-dy)) / 6.283185307179586;   // 0 at the top, clockwise
      if (sweep < 0) sweep += 1.0;
      // Lit while sweep < permille / 1000.0, exactly as that test reads.
      int from = int(sweep * 1000.0);
      while (from > 0 && sweep < (from - 1) / 1000.0) --from;
      while (from <= 1000 && !(sweep < from / 1000.0)) ++from;
      r.px[n++] = {uint16_t(y * kSide + x), uint16_t(from)};
    }
  return r;
}
const IdleRing kIdleLit = buildIdleRing();

}  // namespace

void drawIdleBar(Canvas240& cv, uint16_t permille) {
  if (permille > 1000) permille = 1000;
  const uint16_t c = to565({120, 150, 150});
  for (const IdleLit& p : kIdleLit.px)
    if (permille >= p.from) cv.px[p.at] = c;
}

}  // namespace paint
