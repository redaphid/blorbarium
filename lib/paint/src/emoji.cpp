// Emoji drawn in code, and the thought bubble they show in (paint/emoji.h).
#include "paint/emoji.h"

#include "figure.h"

namespace paint {
namespace {

struct Rgb { int r, g, b; };
constexpr Rgb kWhite{255, 255, 255}, kCrumb{214, 150, 70};
constexpr int imin(int a, int b) { return a < b ? a : b; }
constexpr int imax(int a, int b) { return a > b ? a : b; }
uint16_t to565(Rgb c) { return uint16_t(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }

void put(Canvas240& cv, int x, int y, Rgb c, int alpha) {
  if (x < 0 || y < 0 || x >= 240 || y >= 240 || alpha <= 0) return;
  uint16_t& px = cv.px[y * 240 + x];
  px = alpha >= 256 ? to565(c) : blend(px, to565(c), alpha);
}
void disc(Canvas240& cv, int cx, int cy, int r, Rgb c, int alpha) {
  for (int y = cy - r; y <= cy + r; ++y)
    for (int x = cx - r; x <= cx + r; ++x)
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r + r) put(cv, x, y, c, alpha);
}
void ring(Canvas240& cv, int cx, int cy, int r0, int r1, Rgb c, int alpha) {
  for (int y = cy - r1; y <= cy + r1; ++y)
    for (int x = cx - r1; x <= cx + r1; ++x) {
      const int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      if (d2 >= r0 * r0 && d2 <= r1 * r1) put(cv, x, y, c, alpha);
    }
}
void stroke(Canvas240& cv, int x0, int y0, int x1, int y1, int r, Rgb c, int alpha) {
  const int n = imax(1, imax(x1 > x0 ? x1 - x0 : x0 - x1, y1 > y0 ? y1 - y0 : y0 - y1));
  for (int i = 0; i <= n; ++i) disc(cv, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, r, c, alpha);
}
int fadeOut(int t, int life) { return t < life - 300 ? 256 : imax(0, 256 * (life - t) / 300); }
int wave(int t, int period, int amp) {
  const int p = ((t % period) + period) % period, h = period / 2;
  return amp * (p < h ? 2 * p - h : 3 * h - 2 * p) / h;
}

constexpr Rgb kLine{24, 30, 52}, kNoRed{232, 36, 36}, kSadYellow{255, 204, 40}, kTear{80, 170, 255};

bool inOval(int x, int y, int cx, int cy, int rx, int ry) {
  return int64_t(x - cx) * (x - cx) * ry * ry + int64_t(y - cy) * (y - cy) * rx * rx <= int64_t(rx) * rx * ry * ry;
}
void oval(Canvas240& cv, int cx, int cy, int rx, int ry, Rgb c, int alpha) {
  for (int y = cy - ry; y <= cy + ry; ++y)
    for (int x = cx - rx; x <= cx + rx; ++x)
      if (inOval(x, y, cx, cy, rx, ry)) put(cv, x, y, c, alpha);
}
// A filled shape with a bold outline: the outline drawn `w` px bigger first.
void bold(Canvas240& cv, int cx, int cy, int r, Rgb c, int alpha, int w = 2) {
  disc(cv, cx, cy, r + w, kLine, alpha);
  disc(cv, cx, cy, r, c, alpha);
}
// A size `k`/256 of an icon's px.
constexpr int sz(int px, int k) { return px * k / 256; }

// The pictures, each about 32-40 px, centred on (cx, cy), at size k/256.
void drumstick(Canvas240& cv, int cx, int cy, int k, int a) {
  // Outlines first, then fills, so the parts read as one.
  stroke(cv, cx + sz(1, k), cy + sz(1, k), cx + sz(11, k), cy + sz(11, k), sz(4, k) + 2, kLine, a);
  disc(cv, cx + sz(13, k), cy + sz(8, k), sz(4, k) + 2, kLine, a);
  disc(cv, cx + sz(8, k), cy + sz(13, k), sz(4, k) + 2, kLine, a);
  disc(cv, cx - sz(4, k), cy - sz(4, k), sz(13, k) + 2, kLine, a);
  stroke(cv, cx + sz(1, k), cy + sz(1, k), cx + sz(11, k), cy + sz(11, k), sz(4, k), kWhite, a);
  disc(cv, cx + sz(13, k), cy + sz(8, k), sz(4, k), kWhite, a);
  disc(cv, cx + sz(8, k), cy + sz(13, k), sz(4, k), kWhite, a);
  disc(cv, cx - sz(4, k), cy - sz(4, k), sz(13, k), {196, 104, 40}, a);
  disc(cv, cx - sz(8, k), cy - sz(9, k), sz(4, k), {240, 164, 96}, a);   // the shine
}
void apple(Canvas240& cv, int cx, int cy, int k, int a) {
  stroke(cv, cx, cy - sz(11, k), cx + sz(3, k), cy - sz(17, k), sz(2, k) + 2, kLine, a);
  oval(cv, cx + sz(7, k), cy - sz(15, k), sz(6, k) + 2, sz(3, k) + 2, kLine, a);
  bold(cv, cx, cy + sz(2, k), sz(14, k), {226, 40, 44}, a);
  stroke(cv, cx, cy - sz(11, k), cx + sz(3, k), cy - sz(17, k), sz(2, k), {110, 64, 30}, a);
  oval(cv, cx + sz(7, k), cy - sz(15, k), sz(6, k), sz(3, k), {80, 200, 70}, a);
  disc(cv, cx - sz(6, k), cy - sz(4, k), sz(3, k), {255, 160, 160}, a);
}
void fish(Canvas240& cv, int cx, int cy, int k, int a) {
  for (int pass = 0; pass < 2; ++pass) {
    const int w = pass ? 0 : 2;
    const Rgb c = pass ? Rgb{70, 160, 240} : kLine;
    for (int x = 0; x <= sz(9, k) + w; ++x)   // the tail, a fan to the right
      for (int y = -x - w; y <= x + w; ++y) put(cv, cx + sz(10, k) + x, cy + y, c, a);
    oval(cv, cx - sz(3, k), cy, sz(15, k) + w, sz(10, k) + w, c, a);
  }
  disc(cv, cx - sz(10, k), cy - sz(3, k), sz(3, k) + 1, kWhite, a);
  disc(cv, cx - sz(10, k), cy - sz(3, k), sz(1, k) + 1, kLine, a);
}
void cookie(Canvas240& cv, int cx, int cy, int k, int a) { drawFood(cv, cx, cy, sz(15, k), Food{}, a); }
void food(Canvas240& cv, int which, int cx, int cy, int k, int a) {
  switch (which & 3) {
    case 0: return drumstick(cv, cx, cy, k, a);
    case 1: return cookie(cv, cx, cy, k, a);
    case 2: return apple(cv, cx, cy, k, a);
    default: return fish(cv, cx, cy, k, a);
  }
}
// An empty plate: white, a rim, two crumbs and nothing else.
void plate(Canvas240& cv, int cx, int cy, int k, int a) {
  oval(cv, cx, cy + sz(4, k), sz(18, k) + 2, sz(9, k) + 2, kLine, a);
  oval(cv, cx, cy + sz(4, k), sz(18, k), sz(9, k), kWhite, a);
  oval(cv, cx, cy + sz(4, k), sz(11, k), sz(5, k), {206, 210, 222}, a);
  disc(cv, cx - sz(4, k), cy + sz(4, k), sz(2, k), kCrumb, a);
  disc(cv, cx + sz(5, k), cy + sz(5, k), sz(1, k) + 1, kCrumb, a);
}
// An empty cookie jar, standing: glass, a red lid, crumbs at the bottom.
void jar(Canvas240& cv, int cx, int cy, int k, int a) {
  for (int pass = 0; pass < 2; ++pass) {
    const int w = pass ? 0 : 2;
    const Rgb glass = pass ? Rgb{186, 232, 250} : kLine, lid = pass ? Rgb{228, 50, 46} : kLine;
    for (int y = cy - sz(10, k) - w; y <= cy + sz(16, k) + w; ++y)
      for (int x = cx - sz(13, k) - w; x <= cx + sz(13, k) + w; ++x) put(cv, x, y, glass, a);
    for (int y = cy - sz(17, k) - w; y <= cy - sz(10, k) + w; ++y)
      for (int x = cx - sz(15, k) - w; x <= cx + sz(15, k) + w; ++x) put(cv, x, y, lid, a);
  }
  for (int y = cy - sz(7, k); y <= cy + sz(10, k); ++y) put(cv, cx - sz(9, k), y, kWhite, a);   // the glass's shine
  for (int c = -1; c <= 1; ++c) disc(cv, cx + c * sz(6, k), cy + sz(13, k), sz(2, k), kCrumb, a);
}
// NO: a red circle with a slash through it.
void no(Canvas240& cv, int cx, int cy, int k, int a) {
  const int r = sz(19, k), w = imax(3, sz(5, k));
  ring(cv, cx, cy, r - w - 2, r + 2, kWhite, a);
  stroke(cv, cx - sz(13, k), cy - sz(13, k), cx + sz(13, k), cy + sz(13, k), w / 2 + 2, kWhite, a);
  ring(cv, cx, cy, r - w, r, kNoRed, a);
  stroke(cv, cx - sz(13, k), cy - sz(13, k), cx + sz(13, k), cy + sz(13, k), w / 2, kNoRed, a);
}
// A sad face: yellow, two eyes, a frown, a tear running down from tearAt.
void sad(Canvas240& cv, int cx, int cy, int k, int a, int tearMs) {
  bold(cv, cx, cy, sz(17, k), kSadYellow, a);
  disc(cv, cx - sz(6, k), cy - sz(4, k), sz(3, k), kLine, a);
  disc(cv, cx + sz(6, k), cy - sz(4, k), sz(3, k), kLine, a);
  const int fy = cy + sz(13, k), r0 = sz(7, k), r1 = sz(9, k) + 1;   // the frown: the top of a ring below the mouth
  for (int y = fy - r1; y <= fy; ++y)
    for (int x = cx - r1; x <= cx + r1; ++x) {
      const int d2 = (x - cx) * (x - cx) + (y - fy) * (y - fy);
      if (d2 >= r0 * r0 && d2 <= r1 * r1 && y <= fy - sz(3, k)) put(cv, x, y, kLine, a);
    }
  if (tearMs >= 0) {
    const int ty = cy - sz(1, k) + imin(sz(12, k), tearMs * sz(12, k) / 600);
    disc(cv, cx + sz(7, k), ty, sz(3, k), kTear, a);
    disc(cv, cx + sz(7, k), ty - sz(3, k), sz(1, k) + 1, kTear, a);
  }
}

// A thought bubble: a cloud of puffs with a bold outline, and two little
// puffs trailing to his head at (tx, ty).
void thoughtBubble(Canvas240& cv, int cx, int cy, int tx, int ty, int k, int a) {
  constexpr int kPuffs[8][3] = {{0, 0, 26}, {-22, 3, 17}, {22, 3, 17}, {-12, -15, 16}, {12, -15, 16},
                                {-11, 15, 15}, {11, 15, 15}, {0, -20, 13}};
  for (int pass = 0; pass < 2; ++pass)
    for (const auto& q : kPuffs)
      disc(cv, cx + sz(q[0], k), cy + sz(q[1], k), sz(q[2], k) + (pass ? 0 : 3), pass ? kWhite : kLine, a);
  // The trail: two smaller puffs, a third and two thirds of the way to his head.
  const int ex = cx + (tx - cx) * 2 / 5, ey = cy + sz(26, k) + 4;
  for (int i = 0; i < 2; ++i) {
    const int x = ex + (tx - ex) * i * 45 / 100, y = ey + (ty - ey) * i * 45 / 100, r = sz(i ? 4 : 7, k);
    disc(cv, x, y, r + 2, kLine, a);
    disc(cv, x, y, r, kWhite, a);
  }
}

}  // namespace

// How big a picture is `u` ms after it pops in: out of nothing, past full, settling.
int popIn(int u) {
  if (u < 0) return 0;
  if (u < 140) return 300 * u / 140;
  if (u < 220) return 300 - 44 * (u - 140) / 80;
  return 256;
}

void drawEmoji(Canvas240& cv, Emoji e, int cx, int cy, int size, int alpha, int ms) {
  if (size <= 0 || alpha <= 0) return;
  switch (e) {
    case Emoji::Drumstick: return drumstick(cv, cx, cy, size, alpha);
    case Emoji::Cookie: return cookie(cv, cx, cy, size, alpha);
    case Emoji::Apple: return apple(cv, cx, cy, size, alpha);
    case Emoji::Fish: return fish(cv, cx, cy, size, alpha);
    case Emoji::EmptyPlate: return plate(cv, cx, cy, size, alpha);
    case Emoji::EmptyJar: return jar(cv, cx, cy, size, alpha);
    case Emoji::No: return no(cv, cx, cy, size, alpha);
    case Emoji::Sad: return sad(cv, cx, cy, size, alpha, -1);
    case Emoji::Crying: return sad(cv, cx, cy, size, alpha, ms - 300);
    default: return;
  }
}

void drawThought(Canvas240& cv, int cx, int cy, int towardX, int towardY, int size, int alpha) {
  thoughtBubble(cv, cx, cy, towardX, towardY, size, alpha);
}

void drawThoughtFrames(Canvas240& cv, int cx, int cy, int towardX, int towardY, const ThoughtBeat* beats, int count, int ms,
                       int lifeMs) {
  constexpr int kBubble = 272, kInside = 296;   // the bubble a little over its full size, the emoji in it bigger still
  if (ms < 0 || ms >= lifeMs) return;
  const int a = fadeOut(ms, lifeMs);
  drawThought(cv, cx, cy, towardX, towardY, imin(kBubble, 120 + (kBubble - 120) * ms / 150), a);
  // The beat showing now, and the one it is drawn over.
  int now = -1;
  for (int i = 0; i < count; ++i)
    if (ms >= beats[i].atMs) now = i;
  if (now < 0) return;
  const ThoughtBeat& b = beats[now];
  const int u = ms - b.atMs;
  const int jolt = b.slam && u < 200 ? wave(u, 60, 3) : 0;
  if (b.over && now > 0) drawEmoji(cv, beats[now - 1].emoji, cx + jolt, cy, kInside, a, ms - beats[now - 1].atMs);
  const int k = (b.slam ? (u < 120 ? 460 - 204 * u / 120 : 256) : popIn(u)) * kInside / 256;
  drawEmoji(cv, b.emoji, cx + jolt, cy, k, a, u);
}

}  // namespace paint
