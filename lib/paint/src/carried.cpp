// Carried about, drawn (blorb/carry_show.h says when each starts and why).
// No new art: every reaction is the pack's own faces, moved, squashed and
// stretched, with small props drawn in code over him.
//
//   brace    always: his feet slide and his top leans against the badge's
//            acceleration (twice as far as his feet), and a push up or down
//            the screen squashes or stretches him;
//   cling    alarmed, hunkered down (shorter and wider), shivering, sweat
//            drops flying off his head;
//   dizzy    his tongue out (blep), spiral eyes turning, stars circling over
//            his head, a slow drunk sway that dies away;
//   jelly    a damped wobble, squash and stretch along the jolt (a sideways
//            jolt sways him too), at his species' own tempo and softness;
//   whee     happy, lifted and bobbing with the swing, speed lines trailing
//            and a WHEE! bubble;
//   glance   his eyes dart one way, the other, and back; nervous (alarmed, a
//            sweat drop sliding down) or curious (happy), as he is;
//   settle   a big yawn and a stretch, then he settles (squashed a little,
//            sleepy-eyed) with a sigh.
//
// Everything is a function of the Appearance (the CarryView's ages run on the
// screen's clock: paint/tween.h), so a frame is the same bytes run after run.
#include "carried.h"

#include "blorb/species.h"
#include "paint/text.h"

namespace paint {
namespace {

using blorb::Appearance;
using blorb::Carry;
using blorb::CarryView;
namespace expr = blorb::expr;

constexpr int kSide = 240;
constexpr int kOne = 4096;

// Each species' jelly: how fast it wobbles (tenths of a hertz) and how far
// at its biggest (percent of his size). A puffball and an octopus are all
// jelly; a beetle's shell and an owl's feathers hardly give.
struct Jelly { uint8_t hz10, depthPct; };
constexpr Jelly kJelly[] = {
    {50, 10},   // grungo
    {32, 14},   // mossby
    {70, 16},   // puffkin
    {45, 13},   // gilly
    {60, 9},    // mothwick
    {60, 8},    // mudge
    {80, 6},    // fizz
    {50, 7},    // hushwing
    {60, 9},    // tinker
    {35, 16},   // octopus
    {55, 9},    // puck
};
constexpr int kJellyCount = int(sizeof kJelly / sizeof kJelly[0]);
static_assert(kJellyCount >= int(blorb::kSpeciesCount), "a jelly per species.def row");

// ---- timing, screen ms: each a one-line change ---------------------------------
constexpr int kAttackMs = 140;      // every reaction eases in this fast...
constexpr int kReleaseMs = 320;     // ...and out over this, at its end
constexpr int kShiverMs = 40;       // cling: a shiver's half
constexpr int kDropEveryMs = 520;   // cling: a sweat drop flies off this often...
constexpr int kDropMs = 620;        // ...for this long
constexpr int kSwayMs = 760;        // dizzy: a sway, there and back
constexpr int kOrbitMs = 1100;      // dizzy: the stars' turn
constexpr int kSpiralMs = 600;      // dizzy: a spiral eye's turn
constexpr int kSwingMs = 1250;      // whee: the swing he rides, there and back
constexpr int kWordMs = 1500;       // whee: WHEE! shows this long
constexpr int kYawnMs = 1500;       // settle: the yawn and stretch...
constexpr int kSighMs = 900;        // ...then the settle and sigh

struct Rgb { int r, g, b; };
constexpr Rgb kWhite{255, 255, 255};
constexpr Rgb kInk{52, 36, 24};
constexpr Rgb kStar{255, 222, 80};
constexpr Rgb kStarEdge{200, 140, 30};
constexpr Rgb kSweat{150, 214, 255};
constexpr Rgb kSweatEdge{60, 120, 190};
constexpr Rgb kSpiral{40, 30, 50};
constexpr Rgb kLines{235, 245, 255};

constexpr int imin(int a, int b) { return a < b ? a : b; }
constexpr int imax(int a, int b) { return a > b ? a : b; }
constexpr int iabs(int a) { return a < 0 ? -a : a; }
constexpr int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
uint16_t to565(Rgb c) { return uint16_t(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }

// sin of phase/65536 turn, Q12, linear between 64 steps (lively.cpp's).
int sinQ12(uint32_t phase) {
  static constexpr int16_t kQuarter[17] = {0, 401, 799, 1189, 1567, 1931, 2276, 2598, 2896,
                                           3166, 3406, 3612, 3784, 3920, 4017, 4076, 4096};
  auto at = [](int i) {
    i &= 63;
    if (i < 16) return int(kQuarter[i]);
    if (i < 32) return int(kQuarter[32 - i]);
    if (i < 48) return -int(kQuarter[i - 32]);
    return -int(kQuarter[64 - i]);
  };
  const int i = int((phase >> 10) & 63), f = int(phase & 1023);
  const int s0 = at(i), s1 = at(i + 1);
  return s0 + (s1 - s0) * f / 1024;
}
int cosQ12(uint32_t phase) { return sinQ12(phase + 16384); }
// Where `ms` is in a cycle of `period`, as 1/65536 turn.
uint32_t turnOf(int ms, int period) { return uint32_t(uint32_t(ms % period) * 65536u / uint32_t(period)); }

// 0..256: in over kAttackMs, out over the last kReleaseMs of `len`.
int envelope(int u, int len) {
  if (u < 0 || u >= len) return 0;
  const int in = u < kAttackMs ? u * 256 / kAttackMs : 256;
  const int left = len - u;
  const int out = left < kReleaseMs ? left * 256 / kReleaseMs : 256;
  return imin(in, out);
}
// 0..256 eased in and out (smoothstep) over `span` from `from`.
int ease(int u, int from, int span) {
  if (u <= from) return 0;
  if (u >= from + span) return 256;
  const int t = (u - from) * 256 / span;
  return t * t * (768 - 2 * t) / 65536;
}

void put(Canvas240& cv, int x, int y, Rgb c, int alpha) {
  if (x < 0 || y < 0 || x >= kSide || y >= kSide || alpha <= 0) return;
  uint16_t& px = cv.px[y * kSide + x];
  px = alpha >= 256 ? to565(c) : blend(px, to565(c), alpha);
}
void disc(Canvas240& cv, int cx, int cy, int r, Rgb c, int alpha) {
  for (int y = cy - r; y <= cy + r; ++y)
    for (int x = cx - r; x <= cx + r; ++x)
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r + r) put(cv, x, y, c, alpha);
}
void stroke(Canvas240& cv, int x0, int y0, int x1, int y1, int r, Rgb c, int alpha) {
  const int n = imax(1, imax(iabs(x1 - x0), iabs(y1 - y0)));
  for (int i = 0; i <= n; ++i) disc(cv, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, r, c, alpha);
}

// A little five-pointed star, turning: its points as strokes from the middle.
void star(Canvas240& cv, int cx, int cy, int r, uint32_t turn, int alpha) {
  for (int k = 0; k < 5; ++k) {
    const uint32_t a = turn + uint32_t(k) * 65536u / 5u;
    stroke(cv, cx, cy, cx + r * sinQ12(a) / kOne, cy - r * cosQ12(a) / kOne, 0, kStarEdge, alpha);
  }
  disc(cv, cx, cy, imax(1, r / 2), kStar, alpha);
  for (int k = 0; k < 5; ++k) {
    const uint32_t a = turn + uint32_t(k) * 65536u / 5u;
    put(cv, cx + (r - 1) * sinQ12(a) / kOne, cy - (r - 1) * cosQ12(a) / kOne, kStar, alpha);
  }
}

// A sweat drop, its tip up, at (cx, cy).
void drop(Canvas240& cv, int cx, int cy, int r, int alpha) {
  disc(cv, cx, cy, r + 1, kSweatEdge, alpha);
  for (int i = 1; i <= r + 2; ++i) {
    const int half = imax(0, (r + 2 - i) * (r + 1) / (r + 2));
    for (int dx = -half; dx <= half; ++dx) put(cv, cx + dx, cy - r - i + 1, iabs(dx) == half ? kSweatEdge : kSweat, alpha);
  }
  disc(cv, cx, cy, r, kSweat, alpha);
  put(cv, cx - r / 2, cy - r / 2, kWhite, alpha);
}

// A spiral over an eye: a white disc over the eye, a dark line winding out from its middle.
void spiralEye(Canvas240& cv, int cx, int cy, int r, uint32_t turn, int alpha) {
  r = imax(5, r);
  disc(cv, cx, cy, r, kWhite, alpha * 230 / 256);
  constexpr int kSteps = 40, kTurns = 2;
  int px = cx, py = cy;
  for (int i = 1; i <= kSteps; ++i) {
    const uint32_t a = turn + uint32_t(i) * 65536u * kTurns / kSteps;
    const int rr = (r - 1) * i / kSteps;
    const int x = cx + rr * sinQ12(a) / kOne, y = cy - rr * cosQ12(a) / kOne;
    stroke(cv, px, py, x, y, 0, kSpiral, alpha);
    px = x, py = y;
  }
}

// A white rounded bubble with a tail toward (tx, ty) and dark words in it.
void say(Canvas240& cv, const char* word, int cx, int cy, int tx, int ty, int alpha) {
  const int w = textWidth(kFreeSans9pt7b, word) + 14, h = 22, r = h / 2;
  cx = clampi(cx, 34 + w / 2, kSide - 34 - w / 2);
  for (int y = cy - h / 2; y <= cy + h / 2; ++y)
    for (int x = cx - w / 2; x <= cx + w / 2; ++x) {
      const int ex = x < cx - w / 2 + r ? cx - w / 2 + r - x : x > cx + w / 2 - r ? x - (cx + w / 2 - r) : 0;
      if (ex * ex + (y - cy) * (y - cy) <= r * r) put(cv, x, y, kWhite, alpha);
    }
  const int bx = cx + clampi(tx - cx, -w / 4, w / 4), by = cy + h / 2;
  const int n = imax(1, ty - by);
  for (int i = 0; i <= imin(n, 8); ++i) {
    const int half = imax(0, 4 - i * 4 / 8), x = bx + (tx - bx) * i / n;
    for (int dx = -half; dx <= half; ++dx) put(cv, x + dx, by + i, kWhite, alpha);
  }
  if (alpha > 128) drawString(cv, kFreeSans9pt7b, word, cx - w / 2 + 7, cy, to565(kInk), Clip{0, 0, kSide, kSide, 0});
}

bool reacting(const Appearance& a) {
  return a.lively && a.kind == Appearance::Kind::Creature && !a.reflexActive && !a.asleep && !a.sealed;
}

}  // namespace

Carried carriedOf(const Appearance& a) {
  Carried c;
  if (!reacting(a)) return c;
  const CarryView& v = a.carry;
  // The brace: his feet a share of it, his top all of it.
  c.dxQ4 = v.braceX, c.dyQ4 = v.braceY / 2, c.shearQ4 = v.braceX;
  c.tall -= v.braceY * 4, c.wide += v.braceY * 2;
  c.active = v.braceX || v.braceY;
  if (v.react == Carry::None || v.ageMs == CarryView::kNever) return c;
  const int u = v.ageMs, len = v.lenMs, env = envelope(u, len);
  if (env == 0) return c;
  c.active = true;
  switch (v.react) {
    case Carry::Cling: {
      c.face = true, c.expr = expr::alarmed;
      c.tall -= 330 * env / 256, c.wide += 240 * env / 256;
      const int shake = (8 + v.level / 16) * env / 256;
      c.dxQ4 += (u / kShiverMs) % 2 ? shake : -shake;
      break;
    }
    case Carry::Dizzy: {
      c.face = true, c.expr = expr::blep;
      const int fade = env * (2 * len - u) / (2 * len);   // the sway dies away
      c.shearQ4 += 56 * sinQ12(turnOf(u, kSwayMs)) / kOne * fade / 256;
      c.dxQ4 += 20 * sinQ12(turnOf(u + kSwayMs / 4, kSwayMs)) / kOne * fade / 256;
      c.gaze = sinQ12(turnOf(u, kSwayMs * 2)) > 0 ? 1 : -1;
      break;
    }
    case Carry::Jelly: {
      const Jelly& j = kJelly[a.species.v < kJellyCount ? a.species.v : 0];
      const int period = 10000 / imax(10, j.hz10);
      // Damped: the depth falls off with the square of the time left.
      const int left = imax(0, len - u), depth = kOne * j.depthPct / 100 * (128 + v.level / 2) / 256;
      const int amp = depth * left / len * left / len * imin(256, u * 256 / 60 + 1) / 256;
      const int s = sinQ12(turnOf(u, period));
      const bool sideways = iabs(v.dirX) > iabs(v.dirY);
      c.wide += amp * s / kOne * (sideways ? 1 : 2) / 2;
      c.tall -= amp * s / kOne;
      if (sideways) c.shearQ4 += (v.dirX > 0 ? -1 : 1) * 48 * s / kOne * left / len;
      if ((v.flavour & 4) && u < 260) c.face = true, c.expr = expr::alarmed;
      break;
    }
    case Carry::Whee: {
      c.face = true, c.expr = expr::happy;
      const int bob = iabs(sinQ12(turnOf(u, kSwingMs)));   // up at each end of the swing
      c.lift = (3 * 256 + 4 * bob * 256 / kOne) * env / (256 * 256);
      c.tall += (260 * bob / kOne) * env / 256;
      c.wide -= (120 * bob / kOne) * env / 256;
      c.shearQ4 += 40 * sinQ12(turnOf(u + kSwingMs / 4, kSwingMs)) / kOne * env / 256;
      break;
    }
    case Carry::Glance: {
      const bool nervous = v.flavour & 2;
      const int first = (v.flavour & 1) ? 1 : -1, px = 2;
      c.gaze = u < 300 ? 0 : u < 900 ? first * px : u < 1500 ? -first * px : u < 2000 ? first * px : 0;
      if (u >= 250 && u < len - 250) c.face = true, c.expr = nervous ? expr::alarmed : expr::happy;
      if (nervous) c.dxQ4 += (u / 90) % 2 ? 4 : -4;   // a nervous little tremble
      break;
    }
    case Carry::Settle: {
      const int up = ease(u, 0, 400) - ease(u, kYawnMs - 300, 300);    // the stretch
      const int down = ease(u, kYawnMs, 300) * (256 - ease(u, kYawnMs + kSighMs, 600)) / 256;   // the settle
      c.tall += 330 * up / 256 - 250 * down / 256;
      c.wide += -150 * up / 256 + 220 * down / 256;
      c.face = true, c.expr = u < kYawnMs ? expr::yawn : expr::sleepy;
      break;
    }
    default:
      break;
  }
  c.tall = clampi(c.tall, kOne * 82 / 100, kOne * 118 / 100);
  c.wide = clampi(c.wide, kOne * 85 / 100, kOne * 118 / 100);
  return c;
}

void drawCarried(Canvas240& cv, const Appearance& a, const CarrySpots& s) {
  if (!reacting(a)) return;
  const CarryView& v = a.carry;
  if (v.react == Carry::None || v.ageMs == CarryView::kNever) return;
  const int u = v.ageMs, len = v.lenMs, env = envelope(u, len);
  if (env == 0) return;
  const int alpha = imin(256, env * 5 / 4);
  const int hw = imax(16, s.halfW);
  switch (v.react) {
    case Carry::Cling: {
      // Drops fly off the top of his head, to either side in turn, and fall away.
      for (int k = 0; k < 3; ++k) {
        const int born = (u / kDropEveryMs - k) * kDropEveryMs, t = u - born;
        if (born < 0 || t >= kDropMs) continue;
        const int side = ((born / kDropEveryMs) & 1) ? 1 : -1;
        const int x = s.x + side * (hw * 6 / 10 + 22 * t / kDropMs), y = s.top + 8 - 10 * t / kDropMs + 22 * t * t / (kDropMs * kDropMs);
        drop(cv, x, y, 3, alpha * (kDropMs - t) / kDropMs);
      }
      break;
    }
    case Carry::Dizzy: {
      const int n = 3 + (v.flavour & 1);
      const int cy = s.top + 4, rx = imax(22, hw * 7 / 10), ry = 7;
      for (int k = 0; k < n; ++k) {
        const uint32_t a0 = turnOf(u, kOrbitMs) + uint32_t(k) * 65536u / uint32_t(n);
        const int x = s.x + rx * sinQ12(a0) / kOne, y = cy - ry * cosQ12(a0) / kOne;
        star(cv, x, y, 5, turnOf(u * 2, kOrbitMs), alpha);
      }
      if (!s.glowing)
        for (int e = 0; e < s.eyes; ++e) spiralEye(cv, s.ex[e], s.ey[e], s.er[e], turnOf(u, kSpiralMs) * (e ? 1u : 65535u), alpha);
      break;
    }
    case Carry::Whee: {
      // Speed lines trailing the way he came, and WHEE!
      const int way = sinQ12(turnOf(u + kSwingMs / 4, kSwingMs)) >= 0 ? -1 : 1;
      const int mid = (s.top + s.y) / 2;
      for (int k = 0; k < 3; ++k) {
        const int y = mid - 20 + 18 * k, x0 = s.x + way * (hw + 6 + 3 * (k & 1)), len2 = 14 + 6 * ((k + u / 120) % 2);
        stroke(cv, x0, y, x0 + way * len2, y, 0, kLines, alpha * 200 / 256);
      }
      if (u < kWordMs && !a.thinking) {
        const int pop = imin(256, u * 256 / 160), wa = alpha * imin(256, (kWordMs - u) * 256 / 300) / 256;
        const int side = s.x > kSide / 2 ? -1 : 1;
        say(cv, "WHEE!", s.x + side * (hw * 7 / 10 + 18), s.top + 2 - 6 * pop / 256, s.x + side * hw / 3, s.top + 16, wa);
      }
      break;
    }
    case Carry::Glance: {
      if (!(v.flavour & 2)) break;   // only the nervous sweat
      const int side = (v.flavour & 1) ? -1 : 1;
      const int t = imin(u, len);
      drop(cv, s.x + side * (hw * 7 / 10), s.top + 16 + 8 * t / imax(1, len), 3, alpha);
      break;
    }
    case Carry::Settle: {
      // The sigh: three puffs drifting up and away from beside his mouth.
      const int t = u - kYawnMs - 100;
      if (t < 0 || t >= kSighMs) break;
      const int side = s.x > kSide / 2 ? -1 : 1, mid = (s.top + s.y) / 2;
      for (int k = 0; k < 3; ++k) {
        const int tk = t - 120 * k;
        if (tk < 0) continue;
        const int x = s.x + side * (hw * 6 / 10 + 6 + 14 * tk / kSighMs + 6 * k), y = mid - 8 - 18 * tk / kSighMs - 5 * k;
        disc(cv, x, y, 2 + k, kWhite, (kSighMs - tk) * 180 / kSighMs * alpha / 256);
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace paint
