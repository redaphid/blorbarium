#include "hud.h"

namespace paint {
namespace hud {
namespace {

constexpr int kSide = 240;
constexpr int kMaxR = 127;   // the largest radius a band may have, px (the glass is 120)

// floor(sqrt(n)) for n below 2^18, at compile time.
constexpr int isqrtC(int n) {
  int r = 0;
  for (int b = 1 << 9; b; b >>= 1)
    if ((r + b) * (r + b) <= n) r += b;
  return r;
}

// The half-chords: for a circle of radius r and a row whose middle is k half
// pixels from its centre's, floor(sqrt(4r^2 - k^2)), the half-chord in half
// pixels, for k in 0..2r. Radius r's row starts at r*r. 16 KB, in flash.
struct Half { uint8_t v[(kMaxR + 1) * (kMaxR + 1)]; };
constexpr Half makeHalf() {
  Half h{};
  for (int r = 0; r <= kMaxR; ++r)
    for (int k = 0; k <= 2 * r; ++k) h.v[r * r + k] = uint8_t(isqrtC(4 * r * r - k * k));
  return h;
}
constexpr Half kHalf = makeHalf();
static_assert(kHalf.v[120 * 120 + 0] == 240 && kHalf.v[120 * 120 + 240] == 0, "the half-chord table");

// A quarter of a sine wave, Q14, 1/1024 turn a step: a Taylor series run by the
// compiler, never by the badge.
constexpr double sinTaylor(double x) {
  double term = x, sum = x;
  for (int i = 1; i < 12; ++i) {
    term *= -x * x / double((2 * i) * (2 * i + 1));
    sum += term;
  }
  return sum;
}
struct Quarter { int16_t v[257]; };
constexpr Quarter makeQuarter() {
  Quarter q{};
  for (int i = 0; i <= 256; ++i) q.v[i] = int16_t(sinTaylor(i * 3.14159265358979323846 / 512.0) * 16384.0 + 0.5);
  return q;
}
constexpr Quarter kQuarter = makeQuarter();
static_assert(kQuarter.v[0] == 0 && kQuarter.v[256] == 16384 && kQuarter.v[128] == 11585, "the sine table");

constexpr int floorDiv(int n, int d) {
  const int q = n / d;
  return (n % d != 0 && ((n < 0) != (d < 0))) ? q - 1 : q;
}
constexpr int ceilDiv(int n, int d) { return -floorDiv(-n, d); }
constexpr int imin(int a, int b) { return a < b ? a : b; }
constexpr int imax(int a, int b) { return a > b ? a : b; }

void run(uint16_t* row, int x0, int x1, uint16_t c, int alpha) {
  if (alpha >= 256)
    for (int x = x0; x <= x1; ++x) row[x] = c;
  else
    for (int x = x0; x <= x1; ++x) row[x] = blend(row[x], c, alpha);
}

// One piece of a band, inside one quadrant (b - a at most a quarter turn), so
// its rows lie between its four corners'. Half-open: [a, b), so the pieces of
// one band tile it with no pixel drawn twice.
void piece(Canvas240& cv, int cx2, int cy2, int r0, int r1, int a, int b, uint16_t c, int alpha) {
  const int c0 = cos1024(a), s0 = sin1024(a), c1 = cos1024(b), s1 = sin1024(b);
  int ylo = cy2 + 2 * r0 * s0 / 16384, yhi = ylo;
  for (int y2 : {cy2 + 2 * r1 * s0 / 16384, cy2 + 2 * r0 * s1 / 16384, cy2 + 2 * r1 * s1 / 16384}) {
    ylo = imin(ylo, y2);
    yhi = imax(yhi, y2);
  }
  const int y0 = imax(0, floorDiv(ylo - 3, 2)), y1 = imin(kSide - 1, ceilDiv(yhi + 1, 2));
  for (int y = y0; y <= y1; ++y) {
    const int dy2 = 2 * y + 1 - cy2, k = dy2 < 0 ? -dy2 : dy2;
    if (k > 2 * r1) continue;
    const int so = kHalf.v[r1 * r1 + k];
    int lo = -so, hi = so;   // dx2, the column's middle from the centre in half pixels
    // Clockwise of the first edge: c0*dy - s0*dx >= 0.
    const int n0 = c0 * dy2;
    if (s0 > 0) hi = imin(hi, floorDiv(n0, s0));
    else if (s0 < 0) lo = imax(lo, ceilDiv(n0, s0));
    else if (n0 < 0) continue;
    // Anticlockwise of the second: c1*dy - s1*dx < 0.
    const int n1 = c1 * dy2;
    if (s1 > 0) lo = imax(lo, floorDiv(n1, s1) + 1);
    else if (s1 < 0) hi = imin(hi, ceilDiv(n1, s1) - 1);
    else if (n1 >= 0) continue;
    if (lo > hi) continue;
    const int x0 = imax(0, ceilDiv(lo + cx2 - 1, 2)), x1 = imin(kSide - 1, floorDiv(hi + cx2 - 1, 2));
    if (x0 > x1) continue;
    uint16_t* row = &cv.px[y * kSide];
    if (r0 < 0 || k > 2 * r0) {
      run(row, x0, x1, c, alpha);
      continue;
    }
    const int si = kHalf.v[r0 * r0 + k];   // the hole inside r0
    const int h0 = ceilDiv(-si + cx2 - 1, 2), h1 = floorDiv(si + cx2 - 1, 2);
    run(row, x0, imin(x1, h0 - 1), c, alpha);
    run(row, imax(x0, h1 + 1), x1, c, alpha);
  }
}

// A whole ring (or a disc, r0 < 0): no wedge, so no edges and no pieces.
void ring(Canvas240& cv, int cx2, int cy2, int r0, int r1, uint16_t c, int alpha) {
  const int y0 = imax(0, floorDiv(cy2 - 2 * r1 - 1, 2)), y1 = imin(kSide - 1, ceilDiv(cy2 + 2 * r1 - 1, 2));
  for (int y = y0; y <= y1; ++y) {
    const int dy2 = 2 * y + 1 - cy2, k = dy2 < 0 ? -dy2 : dy2;
    if (k > 2 * r1) continue;
    const int so = kHalf.v[r1 * r1 + k];
    const int x0 = imax(0, ceilDiv(-so + cx2 - 1, 2)), x1 = imin(kSide - 1, floorDiv(so + cx2 - 1, 2));
    if (x0 > x1) continue;
    uint16_t* row = &cv.px[y * kSide];
    if (r0 < 0 || k > 2 * r0) {
      run(row, x0, x1, c, alpha);
      continue;
    }
    const int si = kHalf.v[r0 * r0 + k];
    run(row, x0, imin(x1, ceilDiv(-si + cx2 - 1, 2) - 1), c, alpha);
    run(row, imax(x0, floorDiv(si + cx2 - 1, 2) + 1), x1, c, alpha);
  }
}

}  // namespace

int sin1024(int a) {
  a &= kTurn - 1;
  if (a < 256) return kQuarter.v[a];
  if (a < 512) return kQuarter.v[512 - a];
  if (a < 768) return -kQuarter.v[a - 512];
  return -kQuarter.v[1024 - a];
}

int span(int px, int r) { return r <= 0 ? kTurn : imax(1, px * 163 / r); }

void arc(Canvas240& cv, int cx2, int cy2, int r0, int r1, int a0, int a1, uint16_t c, int alpha) {
  if (alpha <= 0 || r1 <= 0 || r1 <= r0) return;
  r1 = imin(r1, kMaxR);
  int sweep = a1 - a0;
  if (sweep <= 0) return;
  if (sweep >= kTurn) return ring(cv, cx2, cy2, r0, r1, c, alpha);
  int a = a0 & (kTurn - 1);
  const int end = a + sweep;
  while (a < end) {
    const int b = imin(end, (a / 256 + 1) * 256);
    piece(cv, cx2, cy2, r0, r1, a, b, c, alpha);
    a = b;
  }
}

// ---- the duel ------------------------------------------------------------------------

namespace {

constexpr int kMid2 = 240;          // the panel's middle, in half pixels
constexpr int kTwelve = 768;        // 12 o'clock
// Where the gauges run, in 1/1024 turn: mine from 22 to 71 degrees (clear of
// the stances at 3 and 6 o'clock), the friend's the same turned half round.
constexpr int kGaugeA0 = 64, kGaugeA1 = 202, kGap = 5;
// Their radii, px: the hearts the heaviest band on the disc, the meter a
// thinner one inside it, a dark well under both, the clock round the glass.
constexpr int kHeartIn = 98, kHeartOut = 112, kLostIn = 103, kLostOut = 107;
constexpr int kMeterIn = 90, kMeterOut = 96;
constexpr int kWellIn = 87, kWellOut = 114;
constexpr int kClockIn = 112, kClockTrack = 115, kClockOut = 119;

// Segment i of n over [a0, a1): its start and end, a gap after each.
struct Seg { int a0, a1; };
Seg segment(int i, int n, int a0, int a1, int gap) {
  const int step = a1 - a0 + gap;
  return {a0 + i * step / n, a0 + (i + 1) * step / n - gap};
}

// A short straight stroke `w` px wide, steeper than 45 degrees or near it: a
// run on each row.
void line(Canvas240& cv, int x0, int y0, int x1, int y1, int w, uint16_t c) {
  if (y0 > y1) {
    int t = x0; x0 = x1; x1 = t;
    t = y0; y0 = y1; y1 = t;
  }
  for (int y = y0; y <= y1; ++y) {
    if (y < 0 || y >= kSide) continue;
    const int x = y1 == y0 ? x0 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
    run(&cv.px[y * kSide], imax(0, x - w / 2), imin(kSide - 1, x + (w - 1) / 2), c, 256);
  }
}

// A tick in a circle of radius about 6 round (x, y).
void check(Canvas240& cv, int x, int y, uint16_t c) {
  line(cv, x - 5, y, x - 2, y + 4, 3, c);
  line(cv, x - 2, y + 4, x + 5, y - 5, 3, c);
}

}  // namespace

void gauges(Canvas240& cv, const Gauges& g, int side) {
  if (!g.shown || !g.maxHearts || !g.sweep) return;
  const int turn = side ? kTurn / 2 : 0;
  const int a0 = kGaugeA0 + turn, a1 = kGaugeA1 + turn;
  const int reach = a0 + (a1 - a0) * g.sweep / 255;   // swept in this far
  auto band = [&](int r0, int r1, Seg sg, uint16_t c, int alpha = 256) {
    arc(cv, kMid2, kMid2, r0, r1, sg.a0, imin(sg.a1, reach), c, alpha);
  };
  band(g.meter ? kWellIn : kHeartIn - 2, kWellOut, {a0 - 4, a1 + 4}, kShadow);
  // Hearts: a band of segments, the colour of how many are left; a lost one an
  // empty track; one being lost full till the hit lands, then flashing and
  // draining from the far end; an assist heart in the pet's cheer with a spoke.
  const int n = g.maxHearts, alive = g.hearts + (g.drain ? 0 : g.losing);
  const uint16_t life = alive >= 3 ? kLife : (alive == 2 ? kYou : kHurt);
  const Seg first = segment(g.hearts, n, a0, a1, kGap), last = segment(g.hearts + g.losing - 1, n, a0, a1, kGap);
  const int cut = last.a1 - (last.a1 - first.a0) * g.drain / 255;
  for (int i = 0; i < n; ++i) {
    const Seg sg = segment(i, n, a0, a1, kGap);
    const bool cheer = i >= n - g.assist;
    if (i < alive) {
      if (alive == 1 && g.pulse) band(kHeartIn - 3, kHeartOut + 1, {sg.a0 - 3, sg.a1 + 3}, kHurt, 40 + g.pulse * 120 / 256);
      band(kHeartIn, kHeartOut, sg, cheer ? kCheer : life);
      if (cheer) {
        const int mid = (sg.a0 + sg.a1) / 2, w = span(3, kHeartOut);
        band(kHeartIn - 2, kHeartOut + 2, {mid - w / 2 - 2, mid + w / 2 + 2}, kShadow);
        band(kHeartIn - 1, kHeartOut + 1, {mid - w / 2, mid + w / 2}, kSpark);
      }
      continue;
    }
    band(kLostIn, kLostOut, sg, kDust);
    if (i < g.hearts + g.losing && cut > sg.a0)
      band(kHeartIn, kHeartOut, {sg.a0, imin(sg.a1, cut)}, mix(kHurt, kSpark, g.flash));
  }
  // The hit's spark rides out off the glass as the hearts drain.
  if (g.losing && g.drain && g.drain < 255) {
    const int r = kHeartOut + 1 + g.drain * 12 / 255;
    band(r, r + 3, {first.a0, last.a1}, kHurt, 256 - g.drain);
  }
  if (!g.meter) return;
  // The meter: six segments of spark, a spoke where the power is paid for.
  const bool ready = g.cost && g.sparks >= g.cost;
  for (int i = 0; i < blorb::kMeterMax; ++i) {
    const Seg sg = segment(i, blorb::kMeterMax, a0, a1, kGap);
    band(kMeterIn, kMeterOut, sg, i < g.sparks ? (ready ? mix(kPower, kSpark, 64 + g.pulse / 2) : kPower) : kDust);
  }
  if (g.cost) {
    const int at = segment(g.cost - 1, blorb::kMeterMax, a0, a1, kGap).a1 + kGap / 2, w = span(3, kMeterOut);
    band(kMeterIn - 3, kMeterOut + 2, {at - w / 2, at - w / 2 + w}, ready ? kSpark : kText);
  }
}

void clock(Canvas240& cv, int left) {
  left = imax(0, imin(255, left));
  const int gone = (255 - left) * kTurn / 255, head = kTwelve + gone;
  const uint16_t c = left > 77 ? kText : (left > 38 ? kYou : kHurt);
  arc(cv, kMid2, kMid2, kClockTrack, kClockOut, kTwelve, head, kDust);
  arc(cv, kMid2, kMid2, kClockIn, kClockOut, head, kTwelve + kTurn, c);
  // The hand: a spoke at the edge that is running out, sweeping clockwise.
  if (left > 0 && left < 255) {
    const int w = span(3, kClockOut);
    arc(cv, kMid2, kMid2, kClockIn - 3, kClockOut + 1, head - w / 2, head - w / 2 + w, kSpark);
  }
}

void reticle(Canvas240& cv, const Prop& p) {
  const int cx2 = 2 * p.x + 1, cy2 = 2 * p.y + 1;
  const int r = 15 * p.scalePct / 100 + 2;
  if (p.variant == 1) {   // made: the ring closes, heavier
    arc(cv, cx2, cy2, r - 1, r + 6, 0, kTurn, kShadow);
    arc(cv, cx2, cy2, r, r + 5, 0, kTurn, kLocked);
    return;
  }
  if (p.fill < 255) {     // a hold: a ring filling clockwise from 12, as cyber-puck's hold does
    const int to = kTwelve + p.fill * kTurn / 255;
    arc(cv, cx2, cy2, r - 1, r + 6, 0, kTurn, kShadow);
    arc(cv, cx2, cy2, r, r + 5, to, kTwelve + kTurn, kDust);
    arc(cv, cx2, cy2, r, r + 5, kTwelve, to, p.variant == 2 ? kHurt : kYou);
    return;
  }
  if (p.variant == 2) return;
  // Aiming: four brackets, open at 3, 6, 9 and 12 o'clock.
  for (int k = 0; k < 4; ++k) {
    const int mid = 128 + k * 256;
    arc(cv, cx2, cy2, r, r + 4, mid - 80, mid + 80, kShadow);
    arc(cv, cx2, cy2, r + 1, r + 3, mid - 78, mid + 78, kSpark);
  }
}

void lock(Canvas240& cv, const Prop& p) {
  const int cx2 = 2 * p.x + 1, cy2 = 2 * p.y + 1;
  arc(cv, cx2, cy2, -1, 13, 0, kTurn, kShadow, 210);
  if (p.variant >= 3) {
    arc(cv, cx2, cy2, 8, 12, 0, kTurn, kLocked);
    check(cv, p.x, p.y, kLocked);
    return;
  }
  // Still choosing: three arcs, one lit, going round.
  for (int k = 0; k < 3; ++k) {
    const int a = kTwelve + k * kTurn / 3;
    arc(cv, cx2, cy2, 7, 11, a + 24, a + kTurn / 3 - 24, k == p.variant ? kSpark : kDust);
  }
}

void countdown(Canvas240& cv, const Prop& p) {
  const int cx2 = 2 * p.x + 1, cy2 = 2 * p.y + 1;
  const int r = 14 * p.scalePct / 100 + 2;
  arc(cv, cx2, cy2, r - 1, r + 6, 0, kTurn, kShadow);
  const int n = p.variant;
  if (n == 0) {   // it lands now: the ring closed, hot, beating
    arc(cv, cx2, cy2, r - 1, r + 5, 0, kTurn, mix(kHurt, kSpark, p.fill / 2));
    return;
  }
  // A turn an arc, the gaps at 12 and round from it.
  for (int k = 0; k < n; ++k) {
    const int a = kTwelve + k * kTurn / n;
    arc(cv, cx2, cy2, r, r + 4, a + 36, a + kTurn / n - 36, kYou);
  }
}

}  // namespace hud
}  // namespace paint
