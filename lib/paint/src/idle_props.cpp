// What he plays with while he plays an idle (blorb/idles.def): small props
// drawn in code over the dish, on the idle's own clock (Appearance::idleTicks
// and idleSub, at the screen's speed), placed from where draw.cpp stood him
// (DishSpots). No art: notes, a pebble, dirt, a ball, dust, sparkles, stars,
// a daydream's bubble, "?", "!" and "HI!", each a few discs and strokes. The
// round mask is drawn after, so nothing here can leave the glass.
#include "blorb/idle.h"
#include "figure.h"
#include "paint/text.h"

namespace paint {
namespace {

using blorb::Appearance;
namespace idles = blorb::idles;

constexpr int kSide = 240;
struct Rgb { int r, g, b; };
constexpr Rgb kInk{40, 30, 24}, kWhite{255, 255, 255}, kNote{255, 226, 92}, kPebble{170, 166, 160},
    kPebbleLit{226, 222, 214}, kDirt{120, 82, 46}, kDirtDark{84, 56, 30}, kDust{196, 190, 180}, kStar{255, 236, 120},
    kBall{226, 70, 64}, kBroom{150, 104, 54}, kStraw{228, 196, 110}, kHeart{240, 84, 120}, kPow{255, 170, 60},
    kMoon{250, 236, 170}, kStem{80, 170, 70}, kPetal{250, 150, 200}, kBeetle{70, 120, 220},
    // #61
    kSun{255, 214, 70}, kFirefly{210, 255, 110}, kQueasy{140, 210, 90}, kRain{120, 170, 240}, kCloud{160, 164, 178},
    kPlaster{236, 204, 168}, kBowl{110, 170, 230}, kSweat{160, 215, 255}, kShell{246, 240, 222};
constexpr Rgb kSpark[] = {{255, 120, 200}, {120, 220, 255}, {255, 240, 120}, {170, 255, 150}, {220, 160, 255}};

constexpr int imin(int a, int b) { return a < b ? a : b; }
constexpr int imax(int a, int b) { return a > b ? a : b; }
constexpr int iabs(int a) { return a < 0 ? -a : a; }
uint16_t to565(Rgb c) { return uint16_t(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }

uint32_t mix32(uint32_t a, uint32_t b) {
  uint32_t h = a ^ (b * 0x9E3779B1u);
  h ^= h >> 16, h *= 0x85EBCA6Bu, h ^= h >> 13, h *= 0xC2B2AE35u, h ^= h >> 16;
  return h;
}

// sin of `phase`/1024 turn, -256..256, linear between 32 steps.
int sin256(int phase) {
  static constexpr int16_t kQ[9] = {0, 50, 98, 142, 181, 213, 237, 251, 256};
  auto at = [](int i) {
    i &= 31;
    if (i < 8) return int(kQ[i]);
    if (i < 16) return int(kQ[16 - i]);
    if (i < 24) return -int(kQ[i - 16]);
    return -int(kQ[32 - i]);
  };
  phase &= 1023;
  const int i = phase >> 5, f = phase & 31;
  return at(i) + (at(i + 1) - at(i)) * f / 32;
}
int cos256(int phase) { return sin256(phase + 256); }

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
// A disc with a dark rim, so it reads on any dish colour.
void blob(Canvas240& cv, int cx, int cy, int r, Rgb c, int alpha) {
  disc(cv, cx, cy, r + 1, kInk, alpha * 3 / 4);
  disc(cv, cx, cy, r, c, alpha);
}
void stroke(Canvas240& cv, int x0, int y0, int x1, int y1, int r, Rgb c, int alpha) {
  const int n = imax(1, imax(iabs(x1 - x0), iabs(y1 - y0)));
  for (int i = 0; i <= n; ++i) disc(cv, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, r, c, alpha);
}
// A four-point twinkle, `r` long.
void twinkle(Canvas240& cv, int cx, int cy, int r, Rgb c, int alpha) {
  for (int d = -r; d <= r; ++d) {
    const int a = alpha * (r + 1 - iabs(d)) / (r + 1);
    put(cv, cx + d, cy, c, a), put(cv, cx, cy + d, c, a);
  }
  put(cv, cx, cy, kWhite, alpha);
}
// A point pulled in toward the middle until it is at least `margin` inside the glass.
void inside(int& x, int& y, int margin) {
  const int dx = x - kSide / 2, dy = y - kSide / 2, lim = kSide / 2 - margin;
  int d2 = dx * dx + dy * dy;
  if (d2 <= lim * lim) return;
  int d = 1;
  while (d * d < d2) ++d;
  x = kSide / 2 + dx * lim / d, y = kSide / 2 + dy * lim / d;
}

// A music note: a head, a stem, a flag.
void note(Canvas240& cv, int x, int y, int alpha) {
  inside(x, y, 16);
  stroke(cv, x + 4, y, x + 4, y - 14, 1, kInk, alpha);
  stroke(cv, x + 4, y - 14, x + 9, y - 10, 1, kInk, alpha);
  blob(cv, x, y, 4, kNote, alpha);
  stroke(cv, x + 4, y - 1, x + 4, y - 13, 0, kNote, alpha);
}
void heart(Canvas240& cv, int cx, int cy, int alpha) {
  disc(cv, cx - 3, cy - 2, 3, kHeart, alpha), disc(cv, cx + 3, cy - 2, 3, kHeart, alpha);
  for (int y = 0; y < 6; ++y)
    for (int x = -6 + y; x <= 6 - y; ++x) put(cv, cx + x, cy + y, kHeart, alpha);
}
void word(Canvas240& cv, const char* s, int cx, int cy, Rgb c) {
  const Clip all{0, 0, kSide, kSide, 0};
  inside(cx, cy, 22);
  const int x = cx - textWidth(kFreeSans9pt7b, s) / 2;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      if (dx || dy) drawString(cv, kFreeSans9pt7b, s, x + dx, cy + dy, to565(kInk), all);
  drawString(cv, kFreeSans9pt7b, s, x, cy, to565(c), all);
}

// Where he is this frame, from draw.cpp.
struct Him {
  int x, y, top, h, w;   // stand point, the top of him, his height, about half his width
  int headY;             // about his eyes
  int side;              // +1 facing right, -1 left
};

// The idle's clock and how far through it, as lively.cpp reads them.
struct Clock {
  int ms, pc;            // since it began; 0..1024 through it
  int alpha;             // in over the first 200 ms, out over the last
};

void notes(Canvas240& cv, const Him& m, const Clock& c, uint32_t seed, int spread) {
  for (int i = 0; i < 3; ++i) {
    const int life = 1500, t = (c.ms + i * life / 3) % life;
    const int side = (mix32(seed, uint32_t((c.ms + i * life / 3) / life * 3 + i)) & 1) ? 1 : -1;
    const int x = m.x + side * (m.w + 6 + spread * t / life) + 4 * sin256(t * 2) / 256;
    const int y = m.headY - 4 - 34 * t / life;
    note(cv, x, y, c.alpha * imin(256, (life - t) / 3) / 256);
  }
}

void dirt(Canvas240& cv, const Him& m, const Clock& c, uint32_t seed) {
  // A mound at his feet, growing as he digs.
  const int mound = 3 + 4 * c.pc / 1024;
  for (int y = 0; y <= mound; ++y)
    for (int x = -mound * 2 + y; x <= mound * 2 - y; ++x) put(cv, m.x + m.side * m.w / 2 + x, m.y - y + 2, kDirtDark, c.alpha);
  for (int i = 0; i < 5; ++i) {
    const int life = 600, t = (c.ms + i * 120) % life, n = (c.ms + i * 120) / life;
    const int side = (mix32(seed, uint32_t(n * 5 + i)) & 1) ? 1 : -1;
    const int x = m.x + side * (m.w / 2 + 26 * t / life);
    const int y = m.y - 2 - (40 * t / life - 50 * t / life * t / life);   // up and back down
    blob(cv, x, y, 2 + (i & 1), i & 1 ? kDirt : kDirtDark, c.alpha);
  }
}

void dust(Canvas240& cv, const Him& m, const Clock& c, int at) {
  for (int i = 0; i < 3; ++i) {
    const int life = 900, t = (c.ms + i * 300) % life;
    const int r = 2 + 5 * t / life;
    disc(cv, at + (i - 1) * 7 + i * t / 90, m.y - 2 - 8 * t / life, r, kDust, c.alpha * (life - t) / life * 3 / 4);
  }
}

// A little egg: what an old one remembers being.
void egg(Canvas240& cv, int cx, int cy, int alpha) {
  for (int y = -7; y <= 7; ++y)
    for (int x = -6; x <= 6; ++x)
      if (x * x * 64 / 36 + (y < 0 ? y * y * 64 / 49 : y * y * 64 / 36) <= 64) put(cv, cx + x, cy + y, kShell, alpha);
  disc(cv, cx - 2, cy - 1, 1, kStem, alpha), disc(cv, cx + 2, cy + 3, 1, kStem, alpha);
}

void bubbleCloud(Canvas240& cv, const Him& m, const Clock& c, uint32_t seed, bool memory = false) {
  const int side = m.x < kSide / 2 ? 1 : -1;
  int cx = m.x + side * (m.w + 22), cy = m.top + 4;
  inside(cx, cy, 30);
  const int grow = imin(256, c.ms * 256 / 700);
  const int a = c.alpha * grow / 256;
  // Two little puffs from his head, then the cloud.
  blob(cv, m.x + side * (m.w / 2 + 4), m.headY - 8, 2, kWhite, a);
  blob(cv, m.x + side * (m.w / 2 + 12), m.headY - 16, 3, kWhite, a);
  const int r = 9 * grow / 256 + 4;
  disc(cv, cx - 11, cy + 2, r + 1, kInk, a / 2), disc(cv, cx + 11, cy + 2, r + 1, kInk, a / 2),
      disc(cv, cx, cy - 5, r + 3, kInk, a / 2), disc(cv, cx, cy + 5, r + 1, kInk, a / 2);
  disc(cv, cx - 11, cy + 2, r, kWhite, a), disc(cv, cx + 11, cy + 2, r, kWhite, a), disc(cv, cx, cy - 5, r + 2, kWhite, a),
      disc(cv, cx, cy + 5, r, kWhite, a);
  if (grow < 256) return;
  if (memory) return egg(cv, cx, cy, c.alpha);   // an elder's: the egg he was
  // What he dreams of, his own: a cookie, a heart, the moon, a star.
  switch (seed % 4) {
    case 0: drawFood(cv, cx, cy, 6, Food{}, c.alpha); break;
    case 1: heart(cv, cx, cy, c.alpha); break;
    case 2: disc(cv, cx, cy, 6, kMoon, c.alpha), disc(cv, cx + 3, cy - 2, 5, kWhite, c.alpha); break;
    default: twinkle(cv, cx, cy, 6, kStar, c.alpha); break;
  }
}

}  // namespace

void drawIdleProps(Canvas240& cv, const Appearance& a, const DishSpots& s) {
  const blorb::IdleInfo* row = blorb::idleInfo(a.idle);
  if (!row || !s.him || a.asleep || a.reflexActive) return;
  Him m;
  m.x = s.x, m.y = s.y, m.top = s.top;
  m.h = imax(8, s.y - s.top);
  m.w = m.h * 45 / 100;
  m.headY = m.top + m.h / 3;
  m.side = iabs(a.facing.raw) < blorb::Fx::kOne / 2 ? 1 : -1;
  const int tq = int(a.idleTicks) * 256 + a.idleSub;
  Clock c;
  c.ms = tq * int(blorb::kTickMs) / 256 * int(a.idlePace ? a.idlePace : 100) / 100;   // at his pace (lively.cpp)
  c.pc = a.idleLength ? imin(1024, tq * 4 / a.idleLength) : 1024;
  const int realMs = tq * int(blorb::kTickMs) / 256, lengthMs = int(a.idleLength) * int(blorb::kTickMs);
  c.alpha = imax(0, imin(256, imin(realMs * 256 / 200, (lengthMs - realMs) * 256 / 200)));
  if (!c.alpha) return;
  const uint32_t seed = a.lifeSeed;
  const bool walking = a.pose == blorb::pose::walk;

  switch (a.idle.v) {
    case idles::chase_tail.v:   // whirl lines round him
      for (int i = 0; i < 3; ++i) {
        const int ph = c.ms * 1024 / 560 + i * 341;
        const int x0 = m.x + (m.w + 8) * cos256(ph) / 256, y0 = m.y - m.h / 3 + (m.h / 4) * sin256(ph) / 256;
        const int x1 = m.x + (m.w + 8) * cos256(ph - 90) / 256, y1 = m.y - m.h / 3 + (m.h / 4) * sin256(ph - 90) / 256;
        stroke(cv, x0, y0, x1, y1, 1, kWhite, c.alpha * 2 / 3);
      }
      break;
    case idles::dance.v: notes(cv, m, c, seed, 10); break;
    case idles::sing.v: notes(cv, m, c, seed ^ 0x51u, 4); break;
    case idles::juggle.v: {   // a pebble up over his head and down to the other paw, and back
      for (int i = 0; i < 2; ++i) {
        const int period = 1440, t = (c.ms + i * period / 2) % period;
        const int u = t * 1024 / period;   // 0..1024: right to left and back
        const int dir = u < 512 ? 1 : -1, v = u < 512 ? u : u - 512;   // 0..512 across one arc
        const int x = m.x + dir * (m.w * (256 - v) / 256);
        const int y = m.headY - 10 - (m.h / 2) * sin256(v) / 256;
        int px = x, py = y;
        inside(px, py, 10);
        blob(cv, px, py, 5, i ? kBall : kNote, c.alpha);
        disc(cv, px - 2, py - 2, 1, kWhite, c.alpha);
      }
      break;
    }
    case idles::dig.v: dirt(cv, m, c, seed); break;
    case idles::stretch.v:   // effort lines over him at the top of the stretch
      if (c.pc > 300 && c.pc < 640)
        for (int i = -1; i <= 1; ++i) stroke(cv, m.x + i * 9, m.top - 6 - (i ? 0 : 3), m.x + i * 12, m.top - 12 - (i ? 0 : 3), 0, kWhite, c.alpha);
      break;
    case idles::look_around.v:
      if (c.pc >= 450 && c.pc < 600) word(cv, "!", m.x, m.top - 8, kNote);
      else word(cv, "?", m.x + (c.pc < 450 ? -1 : 1) * 10, m.top - 8, kWhite);
      break;
    case idles::peek.v:   // at the glass: hi!
      if (walking) break;
      word(cv, "HI!", m.x + (m.x < kSide / 2 ? 1 : -1) * (m.w + 14), m.headY, kWhite);
      twinkle(cv, m.x - m.w - 4, m.headY - 6, 4 + (c.ms / 300) % 3, kStar, c.alpha);
      break;
    case idles::wave.v: {   // a paw up beside him, swinging
      if (walking) break;
      const int side = m.x < kSide / 2 ? 1 : -1;
      const int ph = c.ms * 1024 / 700;
      int sx = m.x + side * (m.w - 4), sy = m.headY + 10;
      inside(sx, sy, 24);
      const int px = sx + side * (8 + 8 * sin256(ph) / 256), py = sy - 16 + iabs(sin256(ph)) / 64;
      stroke(cv, sx, sy, px, py, 2, kInk, c.alpha);
      blob(cv, px, py, 6, kWhite, c.alpha);
      for (int i = 1; i <= 2; ++i) stroke(cv, px + side * (5 + 3 * i), py - 6, px + side * (7 + 3 * i), py - 1, 0, kWhite, c.alpha / 2);
      break;
    }
    case idles::shadow_box.v: {   // a pow at the end of each jab
      const int side = (c.ms / 500) & 1 ? 1 : -1;
      if (c.ms % 500 < 320) {
        int x = m.x + side * (m.w + 8), y = m.headY + 8;
        inside(x, y, 18);
        const int r = 6 + (c.ms % 500) / 40;
        for (int k = 0; k < 8; ++k) {
          const int ph = k * 128;
          stroke(cv, x + r * cos256(ph) / 512, y + r * sin256(ph) / 512, x + r * cos256(ph) / 256, y + r * sin256(ph) / 256, 1, kPow, c.alpha);
        }
        blob(cv, x, y, 3, kWhite, c.alpha);
      }
      break;
    }
    case idles::sniff.v:   // a sniff's puff at his nose, between steps
      if (!walking) dust(cv, m, c, m.x + m.side * (m.w / 2));
      break;
    case idles::tidy.v: {   // a broom at his side, sweeping, and the dust it lifts
      const int ph = c.ms * 1024 / 600, bx = m.x + m.side * (m.w + 4) + 6 * sin256(ph) / 256, by = m.y;
      stroke(cv, bx - m.side * 6, by - m.h / 2, bx, by - 6, 1, kBroom, c.alpha);
      for (int k = -3; k <= 3; ++k) stroke(cv, bx, by - 6, bx + k * 2, by, 0, kStraw, c.alpha);
      if (!walking) dust(cv, m, c, bx + m.side * 6);
      break;
    }
    case idles::roll_over.v:   // little stars as he goes over
      if (c.pc > 300 && c.pc < 700)
        for (int i = 0; i < 3; ++i) twinkle(cv, m.x + (i - 1) * 14, m.top - 6 - (i == 1 ? 5 : 0), 3 + (c.ms / 150 + i) % 2, kStar, c.alpha);
      break;
    case idles::daydream.v: bubbleCloud(cv, m, c, mix32(seed, a.idleLength)); break;
    case idles::stargaze.v:   // the stars he looks at, twinkling over him
      for (int i = 0; i < 5; ++i) {
        const uint32_t h = mix32(seed, uint32_t(i));
        const int x = m.x - 50 + int(h % 100), y = imax(24, m.top - 10 - int((h >> 8) % 30));
        const int tw = (c.ms / 200 + int(h >> 16)) % 5;
        twinkle(cv, x, y, 2 + (tw < 2 ? tw : 4 - tw), kStar, c.alpha);
      }
      break;
    case idles::toy.v: {   // his ball, rolling just ahead of him
      int bx = m.x + m.side * (m.w + 8), by = m.y - 8;
      inside(bx, by, 12);
      blob(cv, bx, by, 8, kBall, c.alpha);
      const int ph = c.ms * 1024 / 500 * m.side;
      stroke(cv, bx + 6 * cos256(ph) / 256, by + 6 * sin256(ph) / 256, bx - 6 * cos256(ph) / 256, by - 6 * sin256(ph) / 256, 1, kWhite, c.alpha);
      put(cv, bx - 2, by - 3, kWhite, c.alpha);
      break;
    }
    case idles::sneeze.v:   // CHOO: a burst from his face (a puffball's, sparkles)
      if (c.pc >= 450 && c.pc < 900) {
        const int t = (c.pc - 450) * 256 / 450;
        for (int i = 0; i < 9; ++i) {
          const uint32_t h = mix32(seed, uint32_t(i));
          const int ph = 768 + int(h % 512) - 256, d = 6 + (m.w + 20) * t / 256;
          const int x = m.x + d * cos256(ph) / 256 * (i & 1 ? 1 : -1), y = m.headY + 4 + d * sin256(ph) / 512;
          twinkle(cv, x, y, 2 + int(h >> 20) % 2, kSpark[i % 5], c.alpha * (256 - t) / 256);
        }
      }
      break;
    case idles::discover.v: {   // a flower comes up at his feet (or a beetle trundles by), and "!"
      int fx = m.x + m.side * (m.w + 6), fy = m.y - 2;
      inside(fx, fy, 14);
      if (seed & 1) {
        const int grow = imin(256, c.ms * 256 / 1500), top = fy - 4 - 10 * grow / 256;
        stroke(cv, fx, fy, fx, top, 1, kStem, c.alpha);
        if (grow > 128) {
          for (int k = 0; k < 5; ++k) blob(cv, fx + 4 * cos256(k * 205) / 256, top + 4 * sin256(k * 205) / 256, 2, kPetal, c.alpha);
          disc(cv, fx, top, 2, kNote, c.alpha);
        }
      } else {
        const int bx = fx + 10 * sin256(c.ms * 1024 / 4000) / 256;
        blob(cv, bx, fy - 3, 4, kBeetle, c.alpha);
        stroke(cv, bx - 3, fy - 3, bx + 3, fy - 3, 0, kInk, c.alpha);
        put(cv, bx - 1, fy - 5, kWhite, c.alpha);
      }
      if (c.pc >= 250 && c.pc < 450) word(cv, "!", m.x, m.top - 8, kNote);
      break;
    }
    case idles::boo.v:
      if (c.pc >= 550 && c.pc < 760) word(cv, "BOO!", m.x, m.headY - 4, kWhite);
      else if (c.pc >= 760) twinkle(cv, m.x + m.w, m.headY - 8, 3 + (c.ms / 200) % 2, kStar, c.alpha);
      break;
    case idles::hide.v:
      if (c.pc >= 700 && c.pc < 850) word(cv, "!", m.x + 8, m.top - 8, kNote);
      break;
    case idles::hiccup.v:
      if (c.pc % 348 < 140) word(cv, "HIC!", m.x + (m.x < kSide / 2 ? 1 : -1) * (m.w + 12), m.headY - 6, kWhite);
      break;
    case idles::zoomies.v:   // speed lines behind him
      for (int k = -1; k <= 1; ++k) {
        const int y = m.y - m.h / 3 + k * 8, x0 = m.x - m.side * (m.w + 4), len = 10 + ((c.ms / 80 + k) & 3) * 3;
        stroke(cv, x0, y, x0 - m.side * len, y, 0, kWhite, c.alpha * 3 / 4);
      }
      break;
    // ---- #61: his day, how he feels, his age ----
    case idles::wash.v: {   // a paw round his face, and a bubble or two
      const int ph = c.ms * 1024 / 520;
      blob(cv, m.x + m.side * (m.w / 3) + 5 * cos256(ph) / 256, m.headY + 4 + 4 * sin256(ph) / 256, 5, kWhite, c.alpha);
      for (int i = 0; i < 3; ++i) {
        const int life = 1200, t = (c.ms + i * 400) % life;
        disc(cv, m.x - m.side * (m.w / 2) + i * 5, m.headY - 4 - 22 * t / life, 2 + i % 2, kSweat, c.alpha * (life - t) / life);
      }
      break;
    }
    case idles::sunbathe.v: {   // the sun over him, its rays turning
      int sx = m.x + (m.x < kSide / 2 ? 1 : -1) * (m.w + 26), sy = m.top - 14;
      inside(sx, sy, 26);
      const int turn = c.ms * 1024 / 6000;
      for (int k = 0; k < 8; ++k) {
        const int ph = turn + k * 128;
        stroke(cv, sx + 13 * cos256(ph) / 256, sy + 13 * sin256(ph) / 256, sx + 18 * cos256(ph) / 256,
               sy + 18 * sin256(ph) / 256, 1, kSun, c.alpha);
      }
      blob(cv, sx, sy, 9, kSun, c.alpha);
      break;
    }
    case idles::fireflies.v:   // a few fireflies about him, blinking on and off
      for (int i = 0; i < 4; ++i) {
        const uint32_t h = mix32(seed, uint32_t(i + 40));
        const int px = 3000 + int(h % 2000), py = 2300 + int((h >> 11) % 1500);
        int x = m.x + (m.w + 18) * sin256(c.ms * 1024 / px + int(h >> 20)) / 256;
        int y = m.top - 4 + 18 * sin256(c.ms * 1024 / py + int(h >> 8)) / 256;
        inside(x, y, 12);
        const int glow = 96 + iabs(sin256(c.ms * 1024 / (900 + int(h % 500)) + int(h))) * 160 / 256;
        disc(cv, x, y, 4, kFirefly, c.alpha * glow / 256 / 3);
        disc(cv, x, y, 2, kFirefly, c.alpha * glow / 256);
      }
      break;
    case idles::exercise.v: {   // each rep counted at the top, a drop of sweat
      const int rep = c.ms / 900 + 1, within = c.ms % 900;
      if (within < 260 || within > 700) {
        char n[3] = {char('0' + (rep > 9 ? 9 : rep)), '!', 0};
        word(cv, n, m.x + (m.x < kSide / 2 ? 1 : -1) * (m.w + 14), m.headY - 6, kWhite);
      }
      if (rep >= 3) disc(cv, m.x - m.side * (m.w / 2 + 2), m.headY - 2 + (within * 10 / 900), 2, kSweat, c.alpha);
      break;
    }
    case idles::tummy.v:   // the rumble, zigzags at his tummy
      if (c.ms % 1600 < 1100) {
        const int jig = (c.ms / 90) & 1;
        for (int side = -1; side <= 1; side += 2) {
          const int x = m.x + side * (m.w + 3), y = m.y - m.h / 3 + jig;
          for (int k = 0; k < 3; ++k) {
            const int x0 = x + side * k * 5, y0 = y + (k & 1 ? 4 : -4), x1 = x + side * (k + 1) * 5, y1 = y + (k & 1 ? -4 : 4);
            stroke(cv, x0, y0, x1, y1, 1, kInk, c.alpha * 3 / 4);
            stroke(cv, x0, y0, x1, y1, 0, kWhite, c.alpha);
          }
        }
      }
      break;
    case idles::beg.v: {   // his bowl held up, empty
      if (walking) break;
      int bx = m.x, by = m.y - m.h / 4 + c.ms / 600 % 2;
      inside(bx, by, 16);
      for (int y = 0; y <= 6; ++y)
        for (int x = -11 + y; x <= 11 - y; ++x) put(cv, bx + x, by + y, kBowl, c.alpha);
      stroke(cv, bx - 12, by, bx + 12, by, 0, kInk, c.alpha);
      break;
    }
    case idles::nod_off.v: {   // a z as he droops, a ! as he jerks up
      const int ph = c.pc % 338;
      if (ph < 268) word(cv, "z", m.x + m.side * (m.w / 2 + 8), m.top - 6 - ph / 30, kWhite);
      else word(cv, "!", m.x, m.top - 8, kNote);
      break;
    }
    case idles::nurse.v: {   // a plaster on his side, and his paw patting it
      int px = m.x - m.side * (m.w / 2), py = m.y - m.h / 3;
      stroke(cv, px - 6, py - 6, px + 6, py + 6, 3, kInk, c.alpha * 3 / 4);
      stroke(cv, px - 6, py + 6, px + 6, py - 6, 3, kInk, c.alpha * 3 / 4);
      stroke(cv, px - 6, py - 6, px + 6, py + 6, 2, kPlaster, c.alpha);
      stroke(cv, px - 6, py + 6, px + 6, py - 6, 2, kPlaster, c.alpha);
      put(cv, px, py, kDirt, c.alpha);
      blob(cv, px + m.side * 4, py - 6 + iabs(sin256(c.ms * 1024 / 900)) / 64, 4, kWhite, c.alpha);
      break;
    }
    case idles::queasy.v: {   // a green swirl over his head, bubbles off him
      const int turn = c.ms * 1024 / 1500;
      for (int k = 0; k < 12; ++k) {
        const int ph = turn + k * 96, r = 2 + k * 3 / 2;
        int x = m.x + r * cos256(ph) / 256, y = m.top - 12 + r * sin256(ph) / 512;
        disc(cv, x, y, 2, kInk, c.alpha / 2);
        disc(cv, x, y, 1, kQueasy, c.alpha);
      }
      for (int i = 0; i < 2; ++i) {
        const int life = 1400, t = (c.ms + i * 700) % life;
        disc(cv, m.x + (i ? 1 : -1) * (m.w / 2 + 4), m.headY - 20 * t / life, 2, kQueasy, c.alpha * (life - t) / life);
      }
      break;
    }
    case idles::mope.v: {   // a little raincloud over him
      int cx = m.x, cy = m.top - 16;
      inside(cx, cy, 18);
      for (int i = 0; i < 5; ++i) {
        const int life = 700, t = (c.ms + i * 140) % life;
        stroke(cv, cx - 10 + i * 5, cy + 7 + 18 * t / life, cx - 11 + i * 5, cy + 10 + 18 * t / life, 0, kRain, c.alpha);
      }
      blob(cv, cx - 8, cy + 1, 6, kCloud, c.alpha), blob(cv, cx + 8, cy + 1, 6, kCloud, c.alpha), blob(cv, cx, cy - 3, 8, kCloud, c.alpha);
      disc(cv, cx - 8, cy + 1, 6, kCloud, c.alpha), disc(cv, cx + 8, cy + 1, 6, kCloud, c.alpha);
      break;
    }
    case idles::beam.v:   // hearts float up from him
      for (int i = 0; i < 3; ++i) {
        const int life = 1600, t = (c.ms + i * life / 3) % life;
        const int side = (i & 1) ? 1 : -1;
        heart(cv, m.x + side * (m.w / 2 + 6 + 6 * t / life) + 3 * sin256(t * 2) / 256, m.headY - 4 - 30 * t / life,
              c.alpha * imin(256, (life - t) / 3) / 256);
      }
      break;
    case idles::tumble.v:   // a puff where he fell, stars round his head after
      if (c.pc >= 380 && c.pc < 600) dust(cv, m, c, m.x + m.w / 2);
      if (c.pc >= 450 && c.pc < 900)
        for (int k = 0; k < 3; ++k) {
          const int ph = c.ms * 1024 / 900 + k * 341;
          twinkle(cv, m.x + (m.w / 2 + 4) * cos256(ph) / 256, m.top - 2 + 4 * sin256(ph) / 256, 4, kStar, c.alpha);
        }
      break;
    case idles::reminisce.v: bubbleCloud(cv, m, c, seed, /*memory=*/true); break;
    default: break;   // explore: his walk round the rim is the show; a yawn is all his face
  }
}

}  // namespace paint
