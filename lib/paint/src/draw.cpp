#include <cstring>
#include "blorb/registry.h"
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

// The user may reword this; the font covers A-Z, 0-9, space and . , ! ? - ' :
constexpr char kTimeUnknownMarquee[] = "TAP ME WITH YOUR PHONE";
// The empty band above his head, under the pantry pips; the text runs the
// disc's chord there and fades out over the last pixels at each end.
constexpr int kBandTop = 34, kBandRows = 18;
constexpr int kMarqueeFeather = 20;
constexpr int kMarqueePxPerTick = 3, kMarqueeGap = 48;

constexpr int floorDiv(int64_t a, int64_t b) { return int(a >= 0 ? a / b : -((-a + b - 1) / b)); }
constexpr int ceilDiv(int64_t a, int64_t b) { return -floorDiv(-a, b); }
constexpr int imin(int a, int b) { return a < b ? a : b; }
constexpr int imax(int a, int b) { return a > b ? a : b; }
constexpr int iabs(int a) { return a < 0 ? -a : a; }

uint32_t isqrt(uint64_t v) {
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

int unit256(Fx f) { return blorb::clamp01(f).raw >> 16; }   // 0..256

struct Rgb { int r, g, b; };
struct Hsv { int h, s, v; };   // h in 1/1536 turn, s and v 0..255

uint16_t to565(Rgb c) { return uint16_t(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }
Rgb from565(uint16_t p) {
  int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
  return {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
}

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

uint16_t blend(uint16_t under, uint16_t over, int alpha) {
  Rgb a = from565(under), b = from565(over);
  return to565({a.r + (b.r - a.r) * alpha / 256, a.g + (b.g - a.g) * alpha / 256, a.b + (b.b - a.b) * alpha / 256});
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
  bool skin[256];
};

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
    out.skin[i] = e.region == blorb::region::skin.v;
    if (out.skin[i]) out.spot[i] = to565(tinted(c, Tint{spotHue, 128, 92}, 128));
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
        if (row_ == y) std::memset(buf_ + x, p_[1], size_t(imin(p_[0], w_ - x)));
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

template <class Visit>
void visitOutline(const Outline& o, Visit&& visit) {
  for (int v = 0; v < o.variantCount; ++v) {
    const Xf& xf = o.variants[v];
    for (int i = 0; i < o.frameCount; ++i) {
      const FrameRef& f = o.frames[i];
      if (!drawable(f)) continue;
      const uint8_t* p = f.rle;
      for (int y = 0; y < f.h; ++y) {
        int x = 0, lo = -1, hi = -1;
        while (x < f.w) {
          if (p[1]) { if (lo < 0) lo = x; hi = imin(x + p[0], f.w); }
          x += p[0];
          p += 2;
        }
        if (lo < 0) continue;
        for (int sy : {y * 16, (y + 1) * 16})
          for (int sx : {lo * 16, hi * 16}) visit(project(xf, f, sx, sy), 0);
      }
    }
    for (int i = 0; i < o.eyedCount; ++i) {
      const FrameRef& f = o.eyed[i];
      for (int e = 0; e < imin(f.eyeCount, 2); ++e)
        visit(project(xf, f, f.eyes[e].x * 16 + 8, f.eyes[e].y * 16 + 8),
              haloReachQ4(haloEyeQ4(f.eyes[e], xf.kx)));
    }
  }
}

// The smallest-ish disc, relative to the stand point, holding all of it.
struct Disc { int x = 0, y = 0, r = 0; };   // Q4
Disc envelope(const Outline& o, int marginQ4) {
  int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
  visitOutline(o, [&](Pt p, int reach) {
    x0 = imin(x0, p.x - reach); x1 = imax(x1, p.x + reach);
    y0 = imin(y0, p.y - reach); y1 = imax(y1, p.y + reach);
  });
  if (x0 > x1) return {};
  Disc d{(x0 + x1) / 2, (y0 + y1) / 2, 0};
  visitOutline(o, [&](Pt p, int reach) {
    int64_t dx = p.x - d.x, dy = p.y - d.y;
    d.r = imax(d.r, int(isqrt(uint64_t(dx * dx + dy * dy))) + reach);
  });
  d.r += marginQ4 + 16;   // + the stand point's rounding
  return d;
}

// A dish position (the unit disc) to a stand point whose envelope stays inside
// the round panel: the travel radius is the dish radius less the envelope's.
Place placeAt(blorb::DishPos at, const Disc& d, const Xf& xf) {
  int64_t x = at.x.raw, y = at.y.raw;
  uint64_t n2 = uint64_t(x * x) + uint64_t(y * y);
  if (n2 > (uint64_t(1) << 48)) {
    int64_t n = isqrt(n2);
    x = x * Fx::kOne / n;
    y = y * Fx::kOne / n;
  }
  int64_t travel = imax(0, kSide * 8 - d.r);
  int cx = kSide * 8 + int(x * travel / Fx::kOne), cy = kSide * 8 + int(y * travel / Fx::kOne);
  Place p;
  p.x = floorDiv(cx - d.x + 8, 16);
  p.y = floorDiv(cy - d.y + 8, 16);
  p.xf = xf;
  return p;
}

// Mottling (mark layer 0): a seeded spot in some 7 px cells of the skin, in
// frame coordinates from the feet, so spots ride the body and siblings differ.
struct Mottle { uint32_t seed = 0; int density = 0; };
bool spotted(const Mottle& m, int rx, int ry) {
  constexpr int kCell = 7;
  int cx = floorDiv(rx, kCell), cy = floorDiv(ry, kCell);
  uint32_t h = mix32(m.seed ^ (uint32_t(cx) * 0x27D4EB2Du), uint32_t(cy));
  if (int(h & 255) >= m.density) return false;
  int sx = 2 + int((h >> 8) % 3), sy = 2 + int((h >> 10) % 3), r2 = 2 + int((h >> 12) % 3);
  int lx = rx - cx * kCell - sx, ly = ry - cy * kCell - sy;
  return lx * lx + ly * ly <= r2;
}

void blit(Canvas240& cv, const FrameRef& f, const Place& p, const Colours& col, int alpha, const Mottle* mottle) {
  if (!drawable(f) || alpha <= 0) return;
  const Xf& v = p.xf;
  int lean = iabs(v.shear) * (f.h * v.ky / kOne + v.lift + 2) / kOne + 1;
  int top = imax(0, p.y - v.lift + floorDiv(int64_t(-f.originY) * v.ky, kOne) - 1);
  int bottom = imin(kSide - 1, p.y - v.lift + ceilDiv(int64_t(f.h - f.originY) * v.ky, kOne) + 1);
  int left = imax(0, p.x + floorDiv(int64_t(-f.originX) * v.kx, kOne) - lean - 1);
  int right = imin(kSide - 1, p.x + ceilDiv(int64_t(f.w - f.originX) * v.kx, kOne) + lean + 1);
  RowReader rows(f);
  for (int y = top; y <= bottom; ++y) {
    int dy2 = 2 * (y - p.y) + 1;   // twice the pixel centre's offset from the stand point
    int sy = f.originY + floorDiv(int64_t(dy2 + 2 * v.lift) * kOne, 2 * v.ky);
    if (sy < 0) continue;
    if (sy >= f.h) break;
    const uint8_t* row = rows.seek(sy);
    int shift2 = floorDiv(int64_t(dy2) * v.shear, kOne);
    for (int x = left; x <= right; ++x) {
      int sx = f.originX + floorDiv(int64_t(2 * (x - p.x) + 1 + shift2) * kOne, 2 * v.kx);
      if (sx < 0 || sx >= f.w) continue;
      uint8_t i = row[sx];
      if (!i) continue;
      bool spot = mottle && col.skin[i] && spotted(*mottle, sx - f.originX, sy - f.originY);
      uint16_t c = spot ? col.spot[i] : col.base[i];
      uint16_t& dst = cv.px[y * kSide + x];
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
  for (int y = imax(0, floorDiv(c.y - reach, 16)); y <= imin(kSide - 1, (c.y + reach) / 16); ++y) {
    for (int x = imax(0, floorDiv(c.x - reach, 16)); x <= imin(kSide - 1, (c.x + reach) / 16); ++x) {
      int64_t dx = x * 16 + 8 - c.x, dy = y * 16 + 8 - c.y;
      int t = int(isqrt(uint64_t(dx * dx + dy * dy))) - R;
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

void halo(Canvas240& cv, const Appearance& a, const SpritePack& pack, const FrameRef& eyes, const Place& p) {
  int g8 = unit256(a.glow);
  if (g8 == 0) return;
  int amp = imin(256, g8 * 3 / 2) * kPulse[a.poseTick % kPulseTicks] / 256;
  int sparks = 0;
  if (a.glow > kSparkGlow) {
    int64_t span = Fx::one().raw - kSparkGlow.raw;
    sparks = int(imin(kSparkMax, int((int64_t(blorb::clamp01(a.glow).raw - kSparkGlow.raw) * kSparkMax + span - 1) / span)));
  }
  Rgb tint = haloColour(pack, a);
  int turn = int(uint32_t(a.poseTick) * 64 / kSparkOrbitTicks + (a.lifeSeed & 63));
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
  if (a.reflexActive) {
    const blorb::ReflexInfo* r = reflexInfo(a.reflex);
    ExprId f = r ? r->face : expr::alarmed;
    return {f, f, 256};
  }
  ExprId now = a.intensity >= kFaceThreshold ? a.expression : expr::neutral;
  bool resting = now == expr::asleep || now == expr::sleepy || now == expr::yawn;
  // No blink or yawn while asleep, or while foreseeing (the halo would jump to the blink's anchors).
  bool idleLife = !a.asleep && !a.foreseeing;
  if (idleLife && now == expr::neutral && yawning(a)) return {expr::yawn, expr::yawn, 256};
  if (idleLife && !resting && blinking(a)) return {expr::asleep, expr::asleep, 256};
  if (a.exprTicks < kCrossfadeTicks && a.previous != now)
    return {now, a.previous, (a.exprTicks + 1) * 256 / (kCrossfadeTicks + 1)};
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

// Depth by the floor: an item standing higher in the dish than his feet is
// behind him, one level with them or lower is in front. One behind him and
// inside his footprint is under his body, which hides it; the art's gap
// between the feet would otherwise show it.
void drawItems(const Appearance& a, const SpritePack& pack, Canvas240& cv, const Disc& d, const Colours& col,
               const Place& him, int foot, bool front) {
  for (int i = 0; i < imin(a.itemCount, 8); ++i) {
    Place q = placeAt(a.items[i].at, d, Xf{});
    bool inFront = q.y >= him.y;
    if (inFront != front) continue;
    if (!inFront && iabs(q.x - him.x) <= foot && him.y - q.y <= foot) continue;
    blit(cv, pack.item(a.items[i].what), q, col, 256, nullptr);
  }
}

int footHalfW(const FrameRef& f, int kx) { return f.w * kx / kOne * kShadowWidePct / 100; }

void drawCreature(const Appearance& a, const SpritePack& pack, Canvas240& cv) {
  Mottle mottle{a.lifeSeed, 0};
  int8_t spotHue = 0;
  for (int i = 0; i < imin(a.markCount, 8); ++i)
    if (a.marks[i].layer == 0) { mottle.density = a.marks[i].variant; spotHue = a.marks[i].tint.hue; }
  Colours col;
  buildColours(pack, a.regions, a.stage == Stage::Elder, spotHue, col);

  int k = imax(kMinScale, a.scalePct * kOne / 100);
  Xf base{k, k, 0, 0};
  if (a.stage == Stage::Baby || a.stage == Stage::Child) {
    base.kx = k * kHatchlingWide / kOne;
    base.ky = k * kHatchlingTall / kOne;
  }
  const blorb::ReflexInfo* reflex = a.reflexActive ? reflexInfo(a.reflex) : nullptr;
  FrameRef bodies[2] = {pack.body(blorb::pose::idle, a.stage, a.poseTick),
                        pack.body(reflex ? reflex->pose : a.pose, a.stage, a.poseTick)};
  constexpr int kFaces = int(blorb::countOf(blorb::EXPRESSIONS));
  FrameRef eyed[2 + kFaces] = {bodies[0], bodies[1]};
  for (int i = 0; i < kFaces; ++i) eyed[2 + i] = pack.face(blorb::EXPRESSIONS[i].id, a.stage);

  const int maxLift = kHopMaxPx * base.ky / kOne;
  Xf variants[2 + kReflexMotionCount] = {base, {base.kx, base.ky, maxLift, 0}};
  for (int i = 0; i < kReflexMotionCount; ++i) {
    const Motion& w = kReflexMotions[i].widest;
    variants[2 + i] = {base.kx * w.wide / kOne, base.ky * w.tall / kOne, 0, 0};
  }
  const Disc d = envelope({bodies, 2, eyed, 2 + kFaces, variants, 2 + kReflexMotionCount}, (kBreathPx + 1) * 16);
  Place p = placeAt(a.at, d, base);
  const FrameRef& body = bodies[1];
  const int foot = footHalfW(body, base.kx);
  drawItems(a, pack, cv, d, col, p, foot, false);

  Motion m = motionOf(a, maxLift);
  shadow(cv, p, foot, m.lift, maxLift);
  p.xf = {imax(1, base.kx * m.wide / kOne), imax(1, base.ky * m.tall / kOne), m.lift, 0};
  if (body.h) p.xf.ky += breathPx(a) * kOne / body.h;
  const Mottle* spots = mottle.density ? &mottle : nullptr;
  blit(cv, body, p, col, 256, spots);
  for (int i = 0; i < imin(a.markCount, 8); ++i)
    if (a.marks[i].layer != 0) blit(cv, pack.mark(a.marks[i].layer, a.marks[i].variant), p, col, 256, nullptr);

  FaceShown face = faceFor(a);
  FrameRef now = pack.face(face.now, a.stage);
  if (face.mix < 256) blit(cv, pack.face(face.from, a.stage), p, col, 256, spots);
  blit(cv, now, p, col, face.mix, spots);
  halo(cv, a, pack, now.eyeCount ? now : body, p);
  drawItems(a, pack, cv, d, col, p, foot, true);
}

int eggShear(Fx wobble, uint16_t tick, int lean) {
  return lean * unit256(wobble) / 256 * sin64(tick * 8) / kOne;
}

void drawEgg(const Appearance& a, const SpritePack& pack, Canvas240& cv) {
  Colours col;
  buildColours(pack, a.regions, false, 0, col);
  FrameRef f = pack.egg(a.eggProgress);
  const Xf variants[2] = {{kOne, kOne, 0, kEggLean}, {kOne, kOne, 0, -kEggLean}};
  const Disc d = envelope({&f, 1, &f, 1, variants, 2}, 0);
  Place p = placeAt(a.at, d, Xf{});
  drawItems(a, pack, cv, d, col, p, footHalfW(f, kOne), false);
  Fx shake = a.wobble + (a.eggProgress >= Fx::ratio(2, 3) ? Fx::ratio(3, 10) : Fx::zero());
  p.xf.shear = eggShear(shake, a.poseTick, kEggLean);
  blit(cv, f, p, col, 256, nullptr);
  halo(cv, a, pack, f, p);
  drawItems(a, pack, cv, d, col, p, footHalfW(f, kOne), true);
}

void drawRemains(const Appearance& a, const SpritePack& pack, Canvas240& cv) {
  Colours col;
  buildColours(pack, a.regions, false, 0, col);
  FrameRef f = pack.remains();
  const Xf rest{};
  const Disc d = envelope({&f, 1, nullptr, 0, &rest, 1}, 0);
  Place p = placeAt(a.at, d, rest);
  drawItems(a, pack, cv, d, col, p, footHalfW(f, kOne), false);
  blit(cv, f, p, col, 256 - unit256(a.remainsFade), nullptr);
  drawItems(a, pack, cv, d, col, p, footHalfW(f, kOne), true);
}

void fill(Canvas240& cv, int x0, int y0, int x1, int y1, const Rgb& c, int alpha) {   // half-open
  uint16_t c565 = to565(c);
  for (int y = imax(0, y0); y < imin(kSide, y1); ++y)
    for (int x = imax(0, x0); x < imin(kSide, x1); ++x) {
      uint16_t& px = cv.px[y * kSide + x];
      px = alpha >= 256 ? c565 : blend(px, c565, alpha);
    }
}

void drawClutch(const Appearance& a, const SpritePack& pack, Canvas240& cv) {
  const int n = imin(a.eggCount, blorb::kMaxClutch);
  const FrameRef f = pack.egg(Fx::zero());
  const Rgb cream{kHintColour[0], kHintColour[1], kHintColour[2]};
  for (int i = 0; i < n; ++i) {
    Tint tints[blorb::kRegionCount]{};
    tints[blorb::region::skin.v] = a.eggs[i].skin;
    tints[blorb::region::cloak.v] = a.eggs[i].cloak;
    tints[blorb::region::shell.v] = a.eggs[i].shell;
    Colours col;
    buildColours(pack, tints, false, 0, col);
    Place p;
    p.x = kSide / 2 + (2 * i - (n - 1)) * kClutchGapPx / 2;
    p.y = kSide / 2 - f.h / 2 + f.originY;
    bool chosen = i == a.cursor;
    if (chosen) p.xf.shear = eggShear(Fx::ratio(1, 3), a.poseTick, kEggLean);
    blit(cv, f, p, col, 256, nullptr);
    if (chosen)
      for (int row = 0; row < 6; ++row) fill(cv, p.x - row, p.y + 5 + row, p.x + row + 1, p.y + 6 + row, cream, 256);
  }
}

bool inDish(int x, int y) {
  int dx = 2 * x + 1 - kSide, dy = 2 * y + 1 - kSide;
  return dx * dx + dy * dy <= kSide * kSide;
}

// A dark bog-teal dish, lighter at the middle, with a faint glassy rim,
// ordered-dithered so the gradient does not band in RGB565.
void background(Canvas240& cv, Fx night) {
  static constexpr uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const int lit = 256 - (blorb::clamp01(night).raw >> 17);
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x) {
      int dx = 2 * x + 1 - kSide, dy = 2 * y + 1 - kSide, d2 = dx * dx + dy * dy;
      if (d2 > kSide * kSide) { cv.px[y * kSide + x] = 0; continue; }
      int t = d2 / (kSide * kSide / 256);
      Rgb c{22 - 10 * t / 256, 58 - 22 * t / 256, 56 - 20 * t / 256};
      int rim = 6 - iabs(int(isqrt(uint64_t(d2))) - 233);
      if (rim > 0) { c.r += 4 * rim; c.g += 8 * rim; c.b += 7 * rim; }
      int d = kBayer[y & 3][x & 3];
      cv.px[y * kSide + x] = to565({imin(255, c.r * lit / 256 + d / 2), imin(255, c.g * lit / 256 + d / 4),
                                    imin(255, c.b * lit / 256 + d / 2)});
    }
}

void mask(Canvas240& cv) {
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x)
      if (!inDish(x, y)) cv.px[y * kSide + x] = 0;
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

// 9x9 gesture glyphs, drawn at 2x: what the owner's hands should do.
struct Glyph9 { blorb::StimId gesture; uint16_t rows[9]; };
constexpr Glyph9 kGestureGlyphs[] = {
    {blorb::stim::button, {0x07C, 0x082, 0x139, 0x139, 0x139, 0x082, 0x07C, 0x000, 0x1FF}},
    {blorb::stim::cradle, {0x0C6, 0x1FF, 0x1FF, 0x1FF, 0x0FE, 0x07C, 0x038, 0x010, 0x000}},
    {blorb::stim::knock, {0x111, 0x092, 0x000, 0x183, 0x000, 0x07C, 0x0FE, 0x0FE, 0x07C}},
    {blorb::stim::lid_down, {0x038, 0x060, 0x0C0, 0x0C0, 0x0C0, 0x0C0, 0x060, 0x038, 0x000}},
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
    }
  }
}

// 5x7 capitals, digits and a little punctuation.
struct Glyph5 { char c; uint8_t rows[7]; };
constexpr Glyph5 kFont[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'.', {0, 0, 0, 0, 0, 0x0C, 0x0C}},                {',', {0, 0, 0, 0, 0x0C, 0x04, 0x08}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0, 0x04}},    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0, 0x04}},
    {'-', {0, 0, 0, 0x1F, 0, 0, 0}},                   {'\'', {0x0C, 0x04, 0x08, 0, 0, 0, 0}},
    {':', {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0}},
};
constexpr uint8_t kMissingGlyph[7] = {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F};   // a reworded text shows its gaps

const uint8_t* glyphFor(char c) {
  if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
  for (const Glyph5& g : kFont)
    if (g.c == c) return g.rows;
  return kMissingGlyph;
}

// "Time unknown": a scrolling line across the dish above his head, on a dim
// band that fades out toward the rim rather than ending in a box edge.
void marquee(Canvas240& cv, uint16_t tick) {
  const int half = int(isqrt(uint64_t(kSide * kSide / 4 - (kSide / 2 - kBandTop) * (kSide / 2 - kBandTop))));
  const int left = kSide / 2 - half, right = kSide / 2 + half;   // the chord at the band's narrower, top row
  auto fade = [&](int x) { return imax(0, imin(256, imin(x - left, right - 1 - x) * 256 / kMarqueeFeather)); };
  for (int y = kBandTop; y < kBandTop + kBandRows; ++y)
    for (int x = left; x < right; ++x) {
      uint16_t& px = cv.px[y * kSide + x];
      px = blend(px, 0, 140 * fade(x) / 256);
    }
  constexpr int kScale = 2, kAdvance = 6 * kScale;
  const int len = int(sizeof(kTimeUnknownMarquee)) - 1, period = len * kAdvance + kMarqueeGap;
  const int start = right - int(uint32_t(tick) * kMarqueePxPerTick % uint32_t(period));
  const uint16_t c = to565({kMarqueeColour[0], kMarqueeColour[1], kMarqueeColour[2]});
  const int top = kBandTop + (kBandRows - 7 * kScale) / 2;
  for (int copy = -1; copy <= 1; ++copy)
    for (int i = 0; i < len; ++i) {
      int x0 = start + copy * period + i * kAdvance;
      if (x0 + kAdvance <= left || x0 >= right) continue;
      const uint8_t* rows = glyphFor(kTimeUnknownMarquee[i]);
      for (int r = 0; r < 7; ++r)
        for (int b = 0; b < 5; ++b) {
          if (!(rows[r] & (0x10 >> b))) continue;
          for (int dy = 0; dy < kScale; ++dy)
            for (int dx = 0; dx < kScale; ++dx) {
              int x = x0 + b * kScale + dx, y = top + r * kScale + dy;
              if (x < left || x >= right) continue;
              uint16_t& px = cv.px[y * kSide + x];
              px = blend(px, c, fade(x));
            }
        }
    }
}

}  // namespace

void draw(const blorb::Appearance& a, const SpritePack& pack, Canvas240& cv) {
  background(cv, a.night);
  switch (a.kind) {
    case Kind::Creature: drawCreature(a, pack, cv); break;
    case Kind::Egg: drawEgg(a, pack, cv); break;
    case Kind::Remains: drawRemains(a, pack, cv); break;
    case Kind::Clutch: drawClutch(a, pack, cv); break;
  }
  pips(cv, a.pantry);
  hint(cv, a);
  if (a.timeUnknown) marquee(cv, a.poseTick);
  mask(cv);
}

}  // namespace paint
