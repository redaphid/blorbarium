// Feeding, drawn (blorb/feed_show.h has what each step means). Everything
// here is a function of the FeedView's ages in ms and where draw.cpp put him
// (DishSpots), so it moves at any frame rate: big, warm and over in a couple
// of seconds, and readable at the ~5 frames a second the badge drew before
// the framerate work, where an effect gets two or three frames.
//
//   food      a fat cookie (drawFood, the one seam for food art), on the
//             floor, in the air, at his mouth and in the bubbles;
//   heard     the rim's pellet glyph pops and a cookie springs out of it;
//   flight    the cookie, big, arcs from the rim's pellet glyph down to its
//             spot beside him, its shadow waiting, and lands in a puff;
//   refused   a bubble beside his head says why, in words: I'M FULL!,
//             ZZZ..., or ALL GONE! (the pantry's slots shaking at the top,
//             the next one filling);
//   chomp     a burst and crumbs at his mouth, YUM! and hearts rising (a
//             rotten one: green crumbs and YUCK);
//   belly     a meter up the left rim, its level his fullness (1 - hunger),
//             rising as he eats; there after any of the above, then gone;
//   pantry    its slots along the rim (#57): a cookie in each full one, the
//             next one filling like a clock, and while he is hungry with
//             none left, the time until it is back, in a dark pill;
//   found     a snack he dug up: a ring and a twinkle where it lands.
#include <cstdint>
#include <cstdio>

#include "duel/duel_art.h"
#include "figure.h"
#include "blorb/thoughts.h"
#include "paint/text.h"

namespace paint {
namespace {

using blorb::FeedRefusal;
using blorb::FeedView;
using blorb::Fx;

constexpr int kSide = 240;
constexpr int kNever = FeedView::kNever;

// ---- timing, ms: each a one-line change -------------------------------------
constexpr int kPopMs = 300;        // the ring round the pantry's glyph on the second knock
constexpr int kSpringMs = 160;     // a cookie springing out of the glyph
constexpr int kWaitMs = 1600;      // ...and back in if no pellet came of the knocks by then
constexpr int kFlightMs = 700;     // the pellet's flight from the rim to the dish
constexpr int kPuffMs = 450;       // and its landing
constexpr int kRefusedMs = 2600;   // a refusal's bubble
constexpr int kBurstMs = 260;      // the bite's burst
constexpr int kCrumbsMs = 900;
constexpr int kWordMs = 1500;      // YUM!
constexpr int kHeartMs = 1600;     // one heart's rise...
constexpr int kHeartGapMs = 260;   // ...three of them, this far apart
constexpr int kRiseMs = 900;       // the belly meter rising to its new level
constexpr int kMeterMs = 3600;     // the meter, after the last event
constexpr int kFadeMs = 300;
constexpr int kFoundMs = 1400;     // a snack he dug up twinkles this long
constexpr int kToppedMs = 700;     // coming home: the slots it filled pop in
constexpr int kWaitPulseMs = 1200; // the next slot breathes while he waits on it
constexpr int kSpoilsMs = 900;     // a battle's spoils: the cookie's flight over the rim

// ---- where -------------------------------------------------------------------
constexpr int kGlyphX = 146, kGlyphY = 18;    // the rim's pellet glyph (duel_scene.cpp kBootAt), drawn over the dish
constexpr int kFromX = 142, kFromY = 68;      // just under it, where the cookie waits and is tossed from (its stand point)
constexpr int kArcPx = 34;                    // how high the toss rises over the straight line
constexpr int kMeterTop = 62, kMeterBottom = 150, kMeterIn = 100, kMeterOut = 114;   // up the left rim
// The pantry's slots along the rim at radius 108, from just left of the
// pellet glyph (7 degrees left of the top) round toward the belly meter, 4.2
// degrees apart; twelve at most (food::kMaxPantry), clear of the meter's top.
constexpr int kSlotAt[kPantrySlots][2] = {{107, 13}, {99, 14}, {91, 16}, {84, 18}, {76, 21}, {69, 25},
                                          {62, 29}, {56, 33}, {50, 38}, {44, 43}, {38, 49}, {34, 55}};
constexpr int kFoodR = kFoodFloorR;          // a cookie on the floor: 19 px across (the pack's pellet was 15 x 13)

struct Rgb { int r, g, b; };
constexpr Rgb kWarm{255, 214, 90};
constexpr Rgb kPelletOrange{214, 150, 70};
constexpr Rgb kRotGreen{120, 170, 60};
constexpr Rgb kWhite{255, 255, 255};
constexpr Rgb kInk{52, 36, 24};
constexpr Rgb kNo{226, 64, 52};
constexpr Rgb kTrack{4, 10, 10};
constexpr Rgb kFoodBody{236, 168, 76}, kFoodEdge{96, 52, 22}, kFoodChip{78, 42, 20};
constexpr Rgb kFoodRot{150, 158, 88}, kFoodRotEdge{66, 74, 36};

constexpr int imin(int a, int b) { return a < b ? a : b; }
constexpr int imax(int a, int b) { return a > b ? a : b; }
constexpr int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
uint16_t to565(Rgb c) { return uint16_t(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }
int unit256(Fx f) { return blorb::clamp01(f).raw >> 16; }

int isqrt(int v) {
  if (v <= 0) return 0;
  int r = 0, bit = 1 << 30;
  while (bit > v) bit >>= 2;
  for (; bit; bit >>= 2) {
    if (v >= r + bit) v -= r + bit, r = (r >> 1) + bit;
    else r >>= 1;
  }
  return r;
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

// Pixels whose centres lie between r0 and r1 from (cx, cy), squashed to `flat`/256 tall.
void ring(Canvas240& cv, int cx, int cy, int r0, int r1, Rgb c, int alpha, int flat = 256) {
  const int ry = imax(1, r1 * flat / 256);
  for (int y = cy - ry; y <= cy + ry; ++y)
    for (int x = cx - r1; x <= cx + r1; ++x) {
      const int dy = (y - cy) * 256 / flat, d2 = (x - cx) * (x - cx) + dy * dy;
      if (d2 >= r0 * r0 && d2 <= r1 * r1) put(cv, x, y, c, alpha);
    }
}

// A thick line, as discs along it.
void stroke(Canvas240& cv, int x0, int y0, int x1, int y1, int r, Rgb c, int alpha) {
  const int n = imax(1, imax(x1 > x0 ? x1 - x0 : x0 - x1, y1 > y0 ? y1 - y0 : y0 - y1));
  for (int i = 0; i <= n; ++i) disc(cv, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, r, c, alpha);
}

}  // namespace

// ---- the food: one seam ---------------------------------------------------------
// People who played with the badges said the pellet looked like he was
// pooping and they could not tell if he ate: a small dark blob on the floor
// by his feet. So the food is drawn here, everywhere it shows, as a fat round
// cookie, golden with chocolate chips, a dark outline and a bright highlight,
// that reads as food at arm's length. Procedural, in code: when art comes
// for it, this function is the one place to swap (a sprite blit at (cx, cy),
// `r` its radius), and nothing in the feeding changes.
void drawFood(Canvas240& cv, int cx, int cy, int r, const Food& food, int alpha) {
  if (r < 2 || alpha <= 0) return;
  const Rgb body = food.grey ? Rgb{150, 150, 150} : food.rotten ? kFoodRot : kFoodBody;
  const Rgb edge = food.grey ? Rgb{80, 80, 80} : food.rotten ? kFoodRotEdge : kFoodEdge;
  const Rgb chip = food.grey ? Rgb{96, 96, 96} : food.rotten ? Rgb{60, 70, 36} : kFoodChip;
  // Bites, taken from his side of it, one after another.
  constexpr int kBite[3][2] = {{12, -4}, {10, 8}, {2, 12}};   // x16 of r, from the centre
  const int bites = clampi(food.bites, 0, 3);
  auto bitten = [&](int x, int y) {
    for (int b = 0; b < bites; ++b) {
      const int bx = cx + (food.biteLeft ? -1 : 1) * kBite[b][0] * r / 16, by = cy + kBite[b][1] * r / 16;
      if ((x - bx) * (x - bx) + (y - by) * (y - by) <= r * r * 5 / 16) return true;
    }
    return false;
  };
  for (int y = cy - r - 1; y <= cy + r + 1; ++y)
    for (int x = cx - r - 1; x <= cx + r + 1; ++x) {
      const int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      if (d2 > (r + 1) * (r + 1) || bitten(x, y)) continue;
      if (d2 > (r - 1) * (r - 1)) {
        put(cv, x, y, edge, alpha);
        continue;
      }
      // Lit from the top left: lighter there, darker toward the bottom right.
      const int lit = 256 + 40 * ((cx - x) + (cy - y)) / r;
      put(cv, x, y, {imin(255, body.r * lit / 256), imin(255, body.g * lit / 256), imin(255, body.b * lit / 256)}, alpha);
    }
  // Chocolate chips, and a highlight: a cookie, not a pebble.
  constexpr int kChip[5][2] = {{-7, -1}, {1, -7}, {7, -2}, {-2, 4}, {5, 7}};   // scattered: never two eyes and a mouth
  for (const auto& c : kChip) {
    const int x = cx + c[0] * r / 16, y = cy + c[1] * r / 16;
    if (!bitten(x, y)) disc(cv, x, y, imax(1, r / 6), chip, alpha);
  }
  if (!food.rotten && !food.grey)
    for (int k = 0; k < 3; ++k) put(cv, cx - r / 2 + k, cy - r / 2 - 1 + (k == 1 ? -1 : 0), {255, 244, 214}, alpha);
}

namespace {

// 0..256 at the start, fading out over the last kFadeMs of `life`.
int fadeOut(int t, int life) { return t < life - kFadeMs ? 256 : imax(0, 256 * (life - t) / kFadeMs); }
// A smooth 0..256 over `span`: fast out, soft landing.
int easeOut(int t, int span) {
  const int u = clampi(256 * t / span, 0, 256);
  return 256 - (256 - u) * (256 - u) / 256;
}
// A triangle wave, -amp..amp over `period` ms: a wobble with no trig.
int wave(int t, int period, int amp) {
  const int p = ((t % period) + period) % period;
  const int h = period / 2;
  return amp * (p < h ? 2 * p - h : 3 * h - 2 * p) / h;
}

// A speech bubble: a white rounded box with a tail toward (tx, ty).
void bubble(Canvas240& cv, int cx, int cy, int w, int h, int tx, int ty, int alpha) {
  const int r = h / 2;
  for (int y = cy - h / 2; y <= cy + h / 2; ++y)
    for (int x = cx - w / 2; x <= cx + w / 2; ++x) {
      const int ex = x < cx - w / 2 + r ? cx - w / 2 + r - x : x > cx + w / 2 - r ? x - (cx + w / 2 - r) : 0;
      const int ey = y - cy;
      if (ex * ex + ey * ey <= r * r) put(cv, x, y, kWhite, alpha);
    }
  // The tail: a narrowing wedge from the bubble's bottom toward his head.
  const int bx = cx + clampi(tx - cx, -w / 4, w / 4), by = cy + h / 2;
  const int n = imax(1, ty - by);
  for (int i = 0; i <= imin(n, 9); ++i) {
    const int half = imax(0, 5 - i * 5 / 9);
    const int x = bx + (tx - bx) * i / n;
    for (int dx = -half; dx <= half; ++dx) put(cv, x + dx, by + i, kWhite, alpha);
  }
}

// Beside his head, clear of his eyes and the rim: where a bubble goes (to
// his right, or his left when he stands far right), and the point on him its
// tail points at. At home he fills the panel, and over his head is the rim.
void beside(const DishSpots& s, int& x, int& y, int& tx, int& ty) {
  const int hx = s.him ? s.x : kSide / 2, top = s.him ? s.top : 60;
  const int side = hx + 64 <= kSide - 50 ? 1 : -1;
  x = clampi(hx + side * 64, 50, kSide - 50);
  y = clampi(top + 6, 38, kSide - 50);
  tx = hx + side * 18, ty = top + 18;
}

// ---- heard: a cookie springs out of the pantry's glyph ------------------------
// At the second knock (KnockSense::knockKnock, ~0.1 s after it lands), before
// the knock knock is even decided: a small ring round the rim's pellet glyph,
// and a cookie springs out of it and bobs below it, the first thing he will
// be tossed (pelletAloft starts there, that size). With the pantry empty no
// cookie comes: its slots say so. If no pellet follows (the knocks ran on
// into hammering) it goes back in.
void glyphPop(Canvas240& cv, int t) {
  if (t >= kPopMs) return;
  const int r = 22 + 5 * easeOut(t, kPopMs) / 256;
  ring(cv, kGlyphX, kGlyphY, r - 2, r, kWarm, 170 * (kPopMs - t) / kPopMs);
}

void cookieOut(Canvas240& cv, const FeedView& f, uint8_t pantry) {
  const int t = f.heardMs;
  if (t == kNever || pantry == 0) return;
  if (f.droppedMs != kNever && f.droppedMs <= t) return;   // tossed: the flight has it (drawItems)
  if (f.refused != FeedRefusal::None && f.refusedMs <= t) return;
  int x = kFromX, y = kFromY, r = kFoodR * 2;
  if (t < kSpringMs) {   // out of the glyph, growing
    const int u = easeOut(t, kSpringMs);
    x = kGlyphX + (kFromX - kGlyphX) * u / 256, y = kGlyphY + (kFromY - kGlyphY) * u / 256, r = 3 + (r - 3) * u / 256;
  } else if (t >= kWaitMs) {   // nothing came of it: back in
    if (t >= kWaitMs + kSpringMs) return;
    const int u = 256 - easeOut(t - kWaitMs, kSpringMs);
    x = kGlyphX + (kFromX - kGlyphX) * u / 256, y = kGlyphY + (kFromY - kGlyphY) * u / 256, r = 3 + (r - 3) * u / 256;
  } else {
    y += wave(t, 500, 2);   // waiting to be tossed
  }
  drawFood(cv, x, y - r, r, Food{});
}

// ---- the landing: a puff of dust round the pellet -----------------------------
void puff(Canvas240& cv, const DishSpots& s, int t) {
  if (!s.landing || t < kFlightMs || t >= kFlightMs + kPuffMs) return;
  const int u = t - kFlightMs;
  const int r = 6 + 16 * easeOut(u, kPuffMs) / 256;
  const int alpha = 200 * (kPuffMs - u) / kPuffMs;
  ring(cv, s.landX, s.landY - 2, r - 3, r, kPelletOrange, alpha, 110);
  for (int k = 0; k < 4; ++k) {
    const int dir = k < 2 ? -1 : 1, up = (k & 1) ? 10 : 5;
    disc(cv, s.landX + dir * (r + 2 + (k & 1) * 3), s.landY - 3 - up * u / kPuffMs, 1, kWhite, alpha);
  }
}

// ---- the pantry -------------------------------------------------------------------
// A pie of radius r at (x, y), `filled` of 256 of it, clockwise from 12 o'clock.
void pie(Canvas240& cv, int x, int y, int r, int filled, Rgb c, int alpha) {
  for (int py = -r; py <= r; ++py)
    for (int px = -r; px <= r; ++px) {
      if (px * px + py * py > r * r) continue;
      // How far round from 12 o'clock, clockwise, 0..256: a quarter per quadrant, linear within it.
      const int ax = px < 0 ? -px : px, ay = py < 0 ? -py : py, sum = imax(1, ax + ay);
      const int turn = px >= 0 && py < 0 ? 64 * ax / sum
                       : px > 0 && py >= 0 ? 64 + 64 * ay / sum
                       : px <= 0 && py > 0 ? 128 + 64 * ax / sum
                                           : 192 + 64 * ay / sum;
      if (turn <= filled) put(cv, x + px, y + py, c, alpha);
    }
}

// The slots to draw: the pantry's size, or its count where no size was given.
int slotsOf(const blorb::Appearance& a) { return imin(kPantrySlots, imax(a.feed.pantryMax, a.pantry)); }

// "m:ss" until the next pellet, at most 99:59.
void countdown(char (&out)[8], uint16_t secs) {
  const int s = imin(secs, 99 * 60 + 59);
  std::snprintf(out, sizeof out, "%d:%02d", s / 60, s % 60);
}

// ---- refused ------------------------------------------------------------------
// The pantry's slots, ringed in red and shaking: none left. The next fills
// like a clock as it comes back (drawPantry draws that).
void emptyPantry(Canvas240& cv, const blorb::Appearance& a, int t) {
  const int n = slotsOf(a);
  const int alpha = fadeOut(t, kRefusedMs) * 230 / 256;
  const int shake = t < 900 ? wave(t, 140, 3) : 0;
  for (int k = 0; k < n; ++k) ring(cv, kSlotAt[k][0] + shake, kSlotAt[k][1], 4, 6, kNo, alpha);
}

// What each refusal says, in words: the owner read the first pictures (a grey
// cookie and an hourglass) as "two emojis, a globe and something else".
// Short, so a six-year-old reads them, and in the font (thoughts.h inFont).
constexpr const char* kSaysFull = "I'M FULL!";
constexpr const char* kSaysAsleep = "ZZZ...";
constexpr const char* kSaysEmpty = "ALL GONE!";
constexpr const char* kSaysMore = "MORE IN";   // + " 2:05": the empty pantry's answer while a pellet is coming back
static_assert(blorb::writable(kSaysFull, blorb::kMaxThoughtText, "") && blorb::writable(kSaysAsleep, blorb::kMaxThoughtText, "") &&
                  blorb::writable(kSaysEmpty, blorb::kMaxThoughtText, "") && blorb::writable("YUM!", blorb::kMaxThoughtText, "") &&
                  blorb::writable("MORE IN 99:59", blorb::kMaxThoughtText, "") &&
                  blorb::writable("YUCK", blorb::kMaxThoughtText, ""),
              "what he says when fed is in the font");

void refused(Canvas240& cv, const blorb::Appearance& a, const DishSpots& s) {
  const FeedView& f = a.feed;
  if (f.refused == FeedRefusal::None || f.refusedMs >= kRefusedMs) return;
  const int t = f.refusedMs;
  if (f.refused == FeedRefusal::Empty) emptyPantry(cv, a, t);
  // None left: when the next one comes, in words (#57).
  char more[20];
  const char* says = f.refused == FeedRefusal::Full ? kSaysFull : f.refused == FeedRefusal::Asleep ? kSaysAsleep : kSaysEmpty;
  if (f.refused == FeedRefusal::Empty && f.refillSecs) {
    char clock[8];
    countdown(clock, f.refillSecs);
    std::snprintf(more, sizeof more, "%s %s", kSaysMore, clock);
    says = more;
  }
  int x = 0, y = 0, tx = 0, ty = 0;
  beside(s, x, y, tx, ty);
  y += wave(t, 1200, 2);
  const int pop = t < 160 ? 150 + 106 * t / 160 : 256;   // it pops in
  const int alpha = fadeOut(t, kRefusedMs) * 235 / 256;
  const int w = (textWidth(kFreeSans9pt7b, says) + 16) * pop / 256, h = 24 * pop / 256;
  x = clampi(x, 30 + w / 2, kSide - 30 - w / 2);
  bubble(cv, x, y, w, h, tx, ty, alpha);
  if (pop == 256 && alpha > 128)
    drawString(cv, kFreeSans9pt7b, says, x - w / 2 + 8, y, to565(f.refused == FeedRefusal::Asleep ? Rgb{60, 80, 160} : kInk),
               Clip{0, 0, kSide, kSide, 0});
}

// ---- the chomp ----------------------------------------------------------------
// Each crumb's throw (px per second, up is negative), fixed so a frame is the same bytes every run.
constexpr int kCrumbs[10][2] = {{-90, -150}, {80, -170}, {-140, -60}, {150, -90}, {-40, -200},
                                {30, -210}, {-170, -120}, {120, -40}, {-70, -30}, {60, -130}};

void chomp(Canvas240& cv, const FeedView& f, const DishSpots& s) {
  if (f.chompMs == kNever || !s.him) return;
  const int t = f.chompMs;
  const int mx = s.mouth ? s.mouthX : s.x, my = s.mouth ? s.mouthY : s.top + 24;
  const Rgb crumb = f.rotten ? kRotGreen : kPelletOrange;
  // The bite: a white burst of short rays.
  if (t < kBurstMs) {
    const int alpha = 240 * (kBurstMs - t) / kBurstMs, r0 = 6 + 8 * t / kBurstMs, r1 = r0 + 7;
    constexpr int kRay[8][2] = {{0, -16}, {11, -11}, {16, 0}, {11, 11}, {0, 16}, {-11, 11}, {-16, 0}, {-11, -11}};
    for (const auto& d : kRay) stroke(cv, mx + d[0] * r0 / 16, my + d[1] * r0 / 16, mx + d[0] * r1 / 16, my + d[1] * r1 / 16, 1, kWhite, alpha);
  }
  // Crumbs flung out of his mouth, falling.
  if (t < kCrumbsMs) {
    const int alpha = fadeOut(t, kCrumbsMs);
    for (int k = 0; k < 10; ++k) {
      const int x = mx + kCrumbs[k][0] * t / 1000;
      const int y = my + kCrumbs[k][1] * t / 1000 + 300 * t / 1000 * t / 1000;
      disc(cv, x, y, 1 + (k & 1), crumb, alpha);
    }
  }
  int wx = 0, wy = 0, tx = 0, ty = 0;
  beside(s, wx, wy, tx, ty);
  if (f.rotten) {   // yuck: green wisps rising, and the word
    for (int k = 0; k < 3; ++k) {
      const int u = t - 150 * k;
      if (u < 0 || u >= kHeartMs) continue;
      disc(cv, s.x - 50 + 14 * k + wave(u, 500, 4), s.top + 40 - 40 * u / kHeartMs, 3, kRotGreen, fadeOut(u, kHeartMs) * 200 / 256);
    }
  } else {   // yum: hearts rising beside his head, on the side the word is not
    constexpr int kHeartAt[3][2] = {{-44, 44}, {-64, 30}, {-36, 22}};
    const int side = wx >= s.x ? 1 : -1;
    for (int k = 0; k < 3; ++k) {
      const int u = t - 200 - kHeartGapMs * k;
      if (u < 0 || u >= kHeartMs) continue;
      const int x = s.x + side * kHeartAt[k][0] + wave(u, 700, 3), y = s.top + kHeartAt[k][1] - 44 * easeOut(u, kHeartMs) / 256;
      drawArt(cv, duel_art::kBootHeart, clampi(x, 30, kSide - 30), clampi(y, 30, kSide), 160, fadeOut(u, kHeartMs));
    }
  }
  if (t < kWordMs) {
    const char* word = f.rotten ? "YUCK" : "YUM!";
    const int alpha = fadeOut(t, kWordMs) * 240 / 256;
    const int w = textWidth(kFreeSans9pt7b, word) + 14;
    const int bx = clampi(wx, 30 + w / 2, kSide - 30 - w / 2), by = wy - 6 * easeOut(t, 300) / 256;
    bubble(cv, bx, by, w, 22, tx, ty, alpha);
    if (alpha > 128) drawString(cv, kFreeSans9pt7b, word, bx - w / 2 + 7, by, to565(f.rotten ? Rgb{70, 110, 30} : kInk), Clip{0, 0, kSide, kSide, 0});
  }
}

// ---- the belly: a meter up the left rim ----------------------------------------
void belly(Canvas240& cv, const FeedView& f) {
  if (f.meterMs >= kMeterMs || f.pantryMax == 0) return;
  const int alpha = fadeOut(f.meterMs, kMeterMs);
  int level = unit256(f.fullness);
  int rising = -1;
  if (f.chompMs < kRiseMs) {   // he just ate: it rises from where it was
    const int from = unit256(f.fullnessFrom);
    rising = from;
    level = from + (level - from) * easeOut(f.chompMs, kRiseMs) / 256;
  }
  const int span = kMeterBottom - kMeterTop;
  const int yLevel = kMeterBottom - span * level / 256, yFrom = rising < 0 ? yLevel : kMeterBottom - span * rising / 256;
  for (int y = kMeterTop; y <= kMeterBottom; ++y) {
    const int dy = 2 * y + 1 - kSide;
    const int xo = (kSide - 1 - isqrt(4 * kMeterOut * kMeterOut - dy * dy)) / 2, xi = (kSide - 1 - isqrt(4 * kMeterIn * kMeterIn - dy * dy)) / 2;
    // The ends rounded off: a row near either end is narrower.
    const int end = imin(y - kMeterTop, kMeterBottom - y), trim = end < 3 ? 3 - end : 0;
    for (int x = xo + 1 + trim; x <= xi - trim; ++x) {
      Rgb c = kTrack;
      int a = alpha * 210 / 256;
      if (y >= yLevel) {
        c = y < yFrom ? Rgb{255, 236, 170} : kPelletOrange;   // the part he just ate glows as it fills
        a = alpha;
        if (y <= yLevel + 1) c = kWhite;                      // the surface
      }
      put(cv, x, y, c, a);
    }
  }
  drawFood(cv, 22, kMeterBottom + 11, 6, Food{}, alpha);   // food goes in here
}

}  // namespace

bool pelletAloft(const FeedView& f, int landX, int landY, int& x, int& y, int& scalePct) {
  if (f.flying < 0 || f.droppedMs >= kFlightMs) return false;
  const int t = f.droppedMs;
  x = kFromX + (landX - kFromX) * t / kFlightMs;
  y = kFromY + (landY - kFromY) * t / kFlightMs - kArcPx * 4 * t / kFlightMs * (kFlightMs - t) / kFlightMs;
  scalePct = 200 - 100 * t / kFlightMs;
  return true;
}

void foodAloft(Canvas240& cv, const FeedView& f, int landX, int landY, bool rotten) {
  // Where it was a moment ago, fading: a trail, so the toss reads as one.
  for (int k = 3; k >= 1; --k) {
    FeedView then = f;
    if (f.droppedMs < 50 * k) continue;
    then.droppedMs = uint16_t(f.droppedMs - 50 * k);
    int x = 0, y = 0, pct = 100;
    if (pelletAloft(then, landX, landY, x, y, pct)) disc(cv, x, y - kFoodR * pct / 100, kFoodR * pct / 100 * (4 - k) / 4, kWarm, 80 - 20 * k);
  }
  int x = 0, y = 0, pct = 100;
  if (!pelletAloft(f, landX, landY, x, y, pct)) return;
  // Its shadow waits where it will land, growing as it comes down.
  const int near = 256 * f.droppedMs / kFlightMs;
  ring(cv, landX, landY, 0, kFoodR * (4 + 6 * near / 256) / 10, {0, 0, 0}, 40 + 60 * near / 256, 90);
  drawFood(cv, x, y - kFoodR * pct / 100, kFoodR * pct / 100, Food{0, rotten}, 256);
}

void pantrySlot(int k, int& x, int& y) {
  k = clampi(k, 0, kPantrySlots - 1);
  x = kSlotAt[k][0], y = kSlotAt[k][1];
}

void drawPantry(Canvas240& cv, const blorb::Appearance& a, uint32_t nowMs) {
  const FeedView& f = a.feed;
  const int n = slotsOf(a), full = imin(a.pantry, n);
  for (int k = 0; k < n; ++k) {
    const int x = kSlotAt[k][0], y = kSlotAt[k][1];
    if (k < full) {
      // Coming home: the newest slots it filled pop in, one after another.
      int r = 4;
      const int first = full - f.topped;
      if (f.topped && k >= first && f.toppedMs != kNever) {
        const int u = int(f.toppedMs) - 150 * (k - first);
        if (u < 0) continue;
        if (u < kToppedMs) {
          r = 1 + 5 * easeOut(u, kToppedMs) / 256;   // overshoots to 6, then settles
          if (u > kToppedMs / 2) r = imax(4, r - 2 * (u - kToppedMs / 2) / (kToppedMs / 2));
          ring(cv, x, y, r + 2, r + 4, kWarm, 200 * (kToppedMs - u) / kToppedMs);
        }
      }
      drawFood(cv, x, y, r, Food{});
      continue;
    }
    ring(cv, x, y, 3, 4, kTrack, 220);   // a hollow slot, on any floor
    ring(cv, x, y, 4, 5, kPelletOrange, k == full ? 150 : 70);
    if (k != full) continue;
    // The next one: a pie clockwise from the top as it comes back, and while
    // he waits on it, a warm ring breathing round it.
    pie(cv, x, y, 3, unit256(f.refill), kPelletOrange, 230);
    if (f.waiting) {
      const int w = wave(int(nowMs % kWaitPulseMs), kWaitPulseMs, 128);
      const int breathe = 128 + w;   // 0..256
      ring(cv, x, y, 6, 8, kWarm, 90 + breathe * 140 / 256);
    }
  }
}

// He waits on an empty pantry: when the next one comes, as m:ss in a dark
// pill just inside the rim below its slot, over whatever else is there.
void waitingClock(Canvas240& cv, const blorb::Appearance& a) {
  const FeedView& f = a.feed;
  if (!f.waiting || !f.refillSecs || a.pantry != 0 || slotsOf(a) == 0) return;
  char clock[8];
  countdown(clock, f.refillSecs);
  const int w = textWidth(kFreeSans9pt7b, clock) + 12, h = 20;
  const int cx = clampi(kSlotAt[0][0] - 4, 30 + w / 2, kSide - 30 - w / 2), cy = kSlotAt[0][1] + 20;
  const int r = h / 2;
  for (int y = cy - h / 2; y <= cy + h / 2; ++y)
    for (int x = cx - w / 2; x <= cx + w / 2; ++x) {
      const int ex = x < cx - w / 2 + r ? cx - w / 2 + r - x : x > cx + w / 2 - r ? x - (cx + w / 2 - r) : 0, ey = y - cy;
      if (ex * ex + ey * ey <= r * r) put(cv, x, y, kInk, 225);
    }
  drawString(cv, kFreeSans9pt7b, clock, cx - w / 2 + 6, cy, to565(kWarm), Clip{0, 0, kSide, kSide, 0});
}

// A snack he dug up: a ring and four twinkles opening round it.
void foundSnack(Canvas240& cv, const FeedView& f, const DishSpots& s) {
  if (!s.found || f.foundMs >= kFoundMs) return;
  const int t = f.foundMs, alpha = fadeOut(t, kFoundMs) * 230 / 256;
  const int y = s.foundY - kFoodR;   // round the cookie, not its stand point
  const int r = kFoodR + 2 + 10 * easeOut(t, kFoundMs) / 256;
  ring(cv, s.foundX, y, r - 2, r, kWarm, alpha * (kFoundMs - t) / kFoundMs);
  constexpr int kRay[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
  for (const auto& d : kRay) {
    const int x = s.foundX + d[0] * (r + 4), yy = y + d[1] * (r + 4);
    stroke(cv, x - d[1] * 3, yy - d[0] * 3, x + d[1] * 3, yy + d[0] * 3, 0, kWhite, alpha);
    stroke(cv, x - d[0] * 3, yy - d[1] * 3, x + d[0] * 3, yy + d[1] * 3, 0, kWhite, alpha);
  }
}

// A battle's spoils (#57): won, a cookie flies in over the rim, dips into
// the dish and settles in the slot it filled; lost, one hops out of the slot
// it left, into the dish a little, and away over the rim toward the friend,
// whose screen shows it arrive.
void spoilsFlight(Canvas240& cv, const blorb::Appearance& a) {
  const FeedView& f = a.feed;
  if (!f.spoils || f.spoilsMs >= kSpoilsMs + kPuffMs || slotsOf(a) == 0) return;
  const bool won = f.spoils > 0;
  int x = 0, y = 0;
  pantrySlot(won ? imax(0, a.pantry - 1) : a.pantry, x, y);
  // In from the slot toward the middle, and out past the glass straight from it.
  const int dx = x - kSide / 2, dy = y - kSide / 2, r = imax(1, isqrt(dx * dx + dy * dy));
  const int ix = x - dx * 22 / r, iy = y - dy * 22 / r, ox = kSide / 2 + dx * 140 / r, oy = kSide / 2 + dy * 140 / r;
  const int t = f.spoilsMs;
  if (t < kSpoilsMs) {
    // p runs slot -> in -> out for a loss, and the other way for a win.
    const int u = easeOut(t, kSpoilsMs), p = won ? 256 - u : u;
    int fx = 0, fy = 0;
    if (p < 100) fx = x + (ix - x) * p / 100, fy = y + (iy - y) * p / 100;
    else fx = ix + (ox - ix) * (p - 100) / 156, fy = iy + (oy - iy) * (p - 100) / 156;
    drawFood(cv, fx, fy, 4 + 3 * imin(p, 100) / 100, Food{});
  }
  // Where it landed or left from: a ring, warm for a win, red for a loss.
  const int u = won ? t - kSpoilsMs : t;
  if (u >= 0 && u < kPuffMs) ring(cv, x, y, 5 + 6 * u / kPuffMs, 7 + 6 * u / kPuffMs, won ? kWarm : kNo, 220 * (kPuffMs - u) / kPuffMs);
}

void drawFeeding(Canvas240& cv, const blorb::Appearance& a, const DishSpots& s, uint32_t) {
  const FeedView& f = a.feed;
  if (a.kind != blorb::Appearance::Kind::Creature || a.sealed) {
    if (f.heardMs < kPopMs) glyphPop(cv, f.heardMs);   // a knock knock is heard on any screen the dish draws
    return;
  }
  belly(cv, f);
  if (f.droppedMs != kNever) puff(cv, s, f.droppedMs);
  foundSnack(cv, f, s);
  refused(cv, a, s);
  waitingClock(cv, a);
  spoilsFlight(cv, a);
  chomp(cv, f, s);
  glyphPop(cv, f.heardMs);
  cookieOut(cv, f, a.pantry);
}

}  // namespace paint
