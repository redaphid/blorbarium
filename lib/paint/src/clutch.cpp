// Choosing an egg, drawn (blorb/clutch_show.h has what each step means). The
// owner (2026-10-21): "Use the same mechanism as we do in fights: tilt the
// badge. Make the egg choices next to the selection egg." So the eggs lie as
// the duel's stances do, one to the left, one to the right and one toward the
// kid, and the one the tilt points at grows toward the middle with the duel's
// own reticle round it; a knock closes the ring as a knock locks a stance.
//
//   picker   the eggs at their places, the aimed one big, rocking, ringed,
//            sliding to its size the frame the tilt reaches it (an eased
//            slide of kSlideMs, drawn every frame); level, the last egg aimed
//            at (the one the timeout would keep) glows as the duel's lean does;
//   hints    level: a little badge leaning toward each egg in turn, the
//            chevron toward that egg lit, and TILT TO PICK; aimed: the duel's
//            knock (Mercy's) tapping beside the egg, and KNOCK TO KEEP;
//   knock    the knuckle landed: the ring fills, closing, as the knock is decided;
//   chosen   the ring closes lime, sparks fly, the other eggs fade, and the
//            kept egg glides to the middle and becomes the egg screen's egg;
//   vigil    the eggs on their way, small under the remains, clearer as it ends.
//
// Everything is a function of the ClutchView's ages in ms, so it moves at any
// frame rate, and the picker's clock moves every frame, so the frame memo
// (main.cpp) draws every one.
#include <cstdint>

#include "blorb/clutch_show.h"
#include "figure.h"
#include "hud.h"
#include "paint/text.h"

namespace paint {
namespace {

using blorb::Appearance;
using blorb::ClutchView;
using blorb::EggPreview;

constexpr int kSide = 240, kMid = 120;
constexpr int kNever = ClutchView::kNever;

// ---- timing, ms: each a one-line change -------------------------------------
constexpr int kSlideMs = 140;     // the aimed egg growing to its place: fast, but every frame of it drawn
constexpr int kRockMs = 900;      // the aimed egg's rock, one sway
constexpr int kTapMs = 1000;      // the knock hint: one tap a second
constexpr int kLeanMs = 1100;     // the tilt hint: one egg's lean
constexpr int kLockMs = 600;      // a knock's ring closing (a lone knock is decided ~0.6 s after it lands)
constexpr int kFadeMs = 500;      // chosen: the other eggs fade
constexpr int kHoldMs = 800;      // chosen: the closed ring and the sparks, before the glide
constexpr int kGlideMs = 600;     // chosen: the kept egg to the middle
constexpr int kWordsMs = 1500;    // before he has browsed, the two lines take turns this long each
constexpr int kQuietMs = 350;     // just off an egg, no tilt hint yet: passing level between two eggs

// ---- where and how big --------------------------------------------------------
constexpr int kRestPx = 44, kAimPx = 74;     // an egg's height, at rest and aimed (the duel's glyphs: 125% -> 200%)
constexpr int kPullDiv = 4;                  // the aimed one moves this share of the way to the middle
constexpr int kLeanQ12 = 4096 * 15 / 100;    // its rock at full sway (draw.cpp kEggLean is 16%)
constexpr int kTextY = 50;                   // the hint's words, under the rim hint's glyph
constexpr int kBadgeR = 18;                  // the tilt hint's little badge
constexpr int kChevronR = 34;                // its chevrons, toward the eggs
struct P { int x, y; };
// The duel's left, right and near, drawn in toward the middle so a big egg
// stays on the glass; two eggs lie level across it.
constexpr P kSlot3[3] = {{48, 112}, {192, 112}, {120, 186}};
constexpr P kSlot2[2] = {{50, 120}, {190, 120}};
constexpr P kBadgeAt = {kMid, 116};

constexpr uint16_t rgb(int r, int g, int b) { return hud::rgb(r, g, b); }
constexpr uint16_t kInk = rgb(24, 14, 22);
constexpr uint16_t kCream = rgb(255, 244, 214);
constexpr uint16_t kWords = rgb(255, 206, 96);   // the thought marquee's gold
constexpr uint16_t kDim = rgb(120, 130, 140);

constexpr int imin(int a, int b) { return a < b ? a : b; }
constexpr int imax(int a, int b) { return a > b ? a : b; }
constexpr int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 0..256 through [0, of), eased out (fast, then settling).
int easeOut(int t, int of) {
  if (t <= 0) return 0;
  if (t >= of) return 256;
  const int u = 256 - t * 256 / of;
  return 256 - u * u / 256 * u / 256;
}
int lerp(int a, int b, int t256) { return a + (b - a) * t256 / 256; }
// sin of a share of a turn, t256 of 256, as Q12.
int sinQ12(int t256) { return hud::sin1024(t256 * 4) / 4; }

P slotOf(int i, int count) {
  if (count <= 1) return {kMid, kMid};   // one egg: no choice, laid in the middle
  return count >= 3 ? kSlot3[imin(i, 2)] : kSlot2[imin(i, 1)];
}
P pulled(P s) { return {s.x + (kMid - s.x) / kPullDiv, s.y + (kMid - s.y) / kPullDiv}; }

void put(Canvas240& cv, int x, int y, uint16_t c, int alpha) {
  if (x < 0 || y < 0 || x >= kSide || y >= kSide) return;
  uint16_t& px = cv.px[y * kSide + x];
  px = alpha >= 256 ? c : blend(px, c, alpha);
}

void disc(Canvas240& cv, int cx, int cy, int r, uint16_t c, int alpha = 256) {
  if (r > 0) hud::arc(cv, 2 * cx + 1, 2 * cy + 1, -1, r, 0, hud::kTurn, c, alpha);
}

// A thick segment, its ends round.
void stroke(Canvas240& cv, int x0, int y0, int x1, int y1, int r, uint16_t c, int alpha) {
  const int minX = imin(x0, x1) - r, maxX = imax(x0, x1) + r, minY = imin(y0, y1) - r, maxY = imax(y0, y1) + r;
  const int64_t vx = x1 - x0, vy = y1 - y0, vv = vx * vx + vy * vy;
  for (int y = minY; y <= maxY; ++y)
    for (int x = minX; x <= maxX; ++x) {
      const int64_t wx = x - x0, wy = y - y0;
      int64_t t = vv ? (wx * vx + wy * vy) * 256 / vv : 0;
      t = t < 0 ? 0 : (t > 256 ? 256 : t);
      const int64_t dx = wx * 256 - t * vx, dy = wy * 256 - t * vy;
      if (dx * dx + dy * dy <= int64_t(r) * r * 65536) put(cv, x, y, c, alpha);
    }
}

// Words in the font, centred, inked round so they read over anything.
void words(Canvas240& cv, const char* s, int y, uint16_t c, int alpha) {
  if (alpha <= 0) return;
  const int x = kMid - textWidth(kFreeSans9pt7b, s) / 2;
  const Clip all{0, 0, kSide, kSide, 0};
  if (alpha < 200) return drawString(cv, kFreeSans9pt7b, s, x, y, blend(kDim, c, alpha), all);
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      if (dx || dy) drawString(cv, kFreeSans9pt7b, s, x + dx, y + dy, kInk, all);
  drawString(cv, kFreeSans9pt7b, s, x, y, c, all);
}

// A four-pointed spark.
void spark(Canvas240& cv, int x, int y, int r, int alpha) {
  if (r <= 0 || alpha <= 0) return;
  stroke(cv, x - r, y, x + r, y, 1, kInk, alpha);
  stroke(cv, x, y - r, x, y + r, 1, kInk, alpha);
  for (int d = -r + 1; d < r; ++d) put(cv, x + d, y, kCream, alpha), put(cv, x, y + d, kCream, alpha);
  put(cv, x, y, rgb(255, 255, 255), alpha);
}

// The duel's props, as the duel draws them.
void reticle(Canvas240& cv, P at, int heightPx, int variant, int fill) {
  Prop p{PropKind::Reticle};
  p.variant = uint8_t(variant);
  p.x = int16_t(at.x), p.y = int16_t(at.y);
  p.scalePct = uint8_t(clampi((heightPx / 2 + 4 - 2) * 100 / 15, 60, 255));   // hud::reticle: r = 15 * scale / 100 + 2
  p.fill = uint8_t(fill);
  drawPickerProp(cv, p);
}
void leanGlow(Canvas240& cv, P at, int heightPx, int alpha) {
  Prop p{PropKind::Glow};
  p.x = int16_t(at.x), p.y = int16_t(at.y);
  p.scalePct = uint8_t(clampi((heightPx * 2 / 3) * 100 / 30, 40, 255));
  p.alpha = uint8_t(clampi(alpha, 0, 255));
  drawPickerProp(cv, p);
}
void knock(Canvas240& cv, P at, int alpha) {
  Prop p{PropKind::Button};
  p.variant = 1;   // the knock, Mercy's
  p.x = int16_t(at.x), p.y = int16_t(at.y);
  p.alpha = uint8_t(clampi(alpha, 0, 255));
  drawPickerProp(cv, p);
}

// ---- the picker -----------------------------------------------------------------

// How far toward aimed each egg is now, 0..256: the aimed one sliding up,
// the one it left sliding back, over kSlideMs from the aim's change.
int focusOf(const ClutchView& v, int i) {
  if (v.aimMs == kNever) return i == v.aim ? 256 : 0;
  const int e = easeOut(v.aimMs, kSlideMs);
  int f = 0;
  if (i == v.aim) f += e;
  if (i == v.aimWas) f += 256 - e;
  return imin(f, 256);
}

struct EggAt { P at; int h; };
EggAt eggAt(int i, int count, int focus) {
  const P s = slotOf(i, count), q = pulled(s);
  return {{lerp(s.x, q.x, focus), lerp(s.y, q.y, focus)}, lerp(kRestPx, kAimPx, focus)};
}

// Where the hints sit for an egg: between it and the middle.
P inward(P egg, int h, int i, int count) {
  if (count >= 3 && i == 2) return {egg.x, egg.y - h / 2 - 16};
  return {egg.x + (i == 0 ? 1 : -1) * (h / 2 + 14), egg.y + h / 4};
}

// The little badge, leaning toward egg `toward` by lean256 of a lean, and
// the chevrons to every egg, that one lit. The plug's tab is on its top edge,
// as on the kid's own (home is USB-C up), so the lean shows.
void tiltHint(Canvas240& cv, int count, int toward, int lean256) {
  for (int i = 0; i < count; ++i) {
    const P s = slotOf(i, count);
    const int dx = s.x - kBadgeAt.x, dy = s.y - kBadgeAt.y;
    const int len = imax(1, dx < 0 ? -dx : dx) + imax(0, dy < 0 ? -dy : dy);
    const int ux = dx * 256 / len, uy = dy * 256 / len;   // roughly the way to it
    // The lit one slides out toward its egg as the badge leans.
    const int r0 = kChevronR + (i == toward ? 6 * lean256 / 256 : 0);
    const P tip = {kBadgeAt.x + ux * (r0 + 9) / 256, kBadgeAt.y + uy * (r0 + 9) / 256};
    const P base = {kBadgeAt.x + ux * r0 / 256, kBadgeAt.y + uy * r0 / 256};
    const int lit = i == toward ? 140 + lean256 * 116 / 256 : 90;
    const uint16_t c = i == toward ? kWords : kCream;
    const P a = {base.x - uy * 8 / 256, base.y + ux * 8 / 256}, b = {base.x + uy * 8 / 256, base.y - ux * 8 / 256};
    stroke(cv, a.x, a.y, tip.x, tip.y, 3, kInk, lit);
    stroke(cv, b.x, b.y, tip.x, tip.y, 3, kInk, lit);
    stroke(cv, a.x, a.y, tip.x, tip.y, 2, c, lit);
    stroke(cv, b.x, b.y, tip.x, tip.y, 2, c, lit);
  }
  // Toward the near egg the badge tips its top away: drawn lower, its face
  // low on it. Left and right it rolls, the tab swinging that way.
  const bool near = count >= 3 && toward == 2;
  const int roll = near ? 0 : (toward == 0 ? -1 : 1) * 110 * lean256 / 256;   // 1/1024 turn: about 39 degrees
  const P c = {kBadgeAt.x + (near ? 0 : (toward == 0 ? -4 : 4) * lean256 / 256), kBadgeAt.y + (near ? 4 * lean256 / 256 : 0)};
  const int cx2 = 2 * c.x + 1, cy2 = 2 * c.y + 1;
  const int up = 3 * hud::kTurn / 4 + roll;   // 12 o'clock, turned
  hud::arc(cv, cx2, cy2, kBadgeR, kBadgeR + 6, up - 50, up + 50, kInk);
  hud::arc(cv, cx2, cy2, kBadgeR + 1, kBadgeR + 5, up - 42, up + 42, kDim);
  disc(cv, c.x, c.y, kBadgeR + 1, kInk);
  disc(cv, c.x, c.y, kBadgeR - 1, rgb(32, 72, 70));
  hud::arc(cv, cx2, cy2, kBadgeR - 2, kBadgeR, 0, hud::kTurn, kCream);
  // Two eyes, turned with it (and lower when it tips away). `up` as a
  // vector on the glass (y down), and its right hand.
  const int ux = hud::cos1024(up), uy = hud::sin1024(up), rx = -uy, ry = ux;   // Q14
  const int drop = near ? 5 * lean256 / 256 : 0;
  for (int side = -1; side <= 1; side += 2) {
    const int ex = c.x + (ux * 3 + rx * side * 6) / 16384, ey = c.y + (uy * 3 + ry * side * 6) / 16384 + drop;
    disc(cv, ex, ey, 2, kCream);
  }
}

}  // namespace

const char* const kClutchWords[2] = {"TILT TO PICK", "KNOCK TO KEEP"};

void drawClutchPicker(const Appearance& a, Canvas240& cv) {
  const ClutchView& v = a.clutch;
  const int n = imin(a.eggCount, blorb::kMaxClutch);
  if (!n) return;
  const int t = v.shownMs;
  const int aim = v.picking && v.aim < n ? v.aim : -1;
  // Level: the last egg aimed at (the timeout's) glows, as the duel's lean does.
  if (aim < 0 && n > 1) {
    const EggAt e = eggAt(a.cursor, n, 0);
    leanGlow(cv, e.at, e.h, 110 + sinQ12(t % 1600 * 256 / 1600) * 50 / 4096);
  }
  // The eggs, the aimed one last so it lies over the rest as it grows.
  for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < n; ++i) {
      if ((i == aim) != (pass == 1)) continue;
      const int f = focusOf(v, i);
      const EggAt e = eggAt(i, n, f);
      const int shear = i == aim ? kLeanQ12 * sinQ12(t % kRockMs * 256 / kRockMs) / 4096 : 0;
      drawEggPreview(cv, a.eggs[i], e.at.x, e.at.y, e.h, shear, aim >= 0 && i != aim ? 160 : 256);
    }
  // Until he has tilted from one egg to another, the words take turns, so
  // a kid holding the badge upright (which already leans toward the near
  // egg) learns the tilt before he knocks. Just off an egg, a moment of no
  // hint, so passing level between two eggs does not flash the tilt's.
  const bool browsed = v.aimChanges >= 3;
  const bool teachTilt = !browsed && t % (2 * kWordsMs) >= kWordsMs;
  if (aim >= 0) {
    const EggAt e = eggAt(aim, n, focusOf(v, aim));
    // The reticle at once, the frame the tilt reached it; a knock fills it closed.
    const bool knocked = v.knockMs != kNever;
    reticle(cv, e.at, e.h, 0, knocked ? imin(254, v.knockMs * 255 / kLockMs) : 255);
    if (!knocked) {
      // The duel's knock tapping beside it: in toward the egg, back, once a second.
      const int tap = t % kTapMs, in = tap < 90 ? tap * 256 / 90 : (tap < 200 ? 256 - (tap - 90) * 256 / 110 : 0);
      const P h = inward(e.at, e.h, aim, n);
      const P to = {e.at.x - h.x, e.at.y - h.y};
      knock(cv, {h.x + to.x * in / 256 / 6, h.y + to.y * in / 256 / 6}, 255);
      if (tap >= 90 && tap < 400) {   // the ripple where it landed
        const int r = 4 + (tap - 90) / 20, al = 200 - (tap - 90) * 200 / 310;
        const P hit = {h.x + to.x / 6, h.y + to.y / 6};
        hud::arc(cv, 2 * hit.x + 1, 2 * hit.y + 1, r, r + 2, 0, hud::kTurn, kCream, al);
      }
    }
    words(cv, knocked || !teachTilt ? kClutchWords[1] : kClutchWords[0], kTextY, kWords, 256);
  } else if (n > 1 && (v.aimWas < 0 || v.aimMs == kNever || v.aimMs >= kQuietMs)) {
    const int phase = t / kLeanMs % n, in = t % kLeanMs;
    const int lean = in < 300 ? easeOut(in, 300) : (in < 800 ? 256 : 256 - easeOut(in - 800, 300));
    tiltHint(cv, n, phase, lean);
    words(cv, kClutchWords[0], kTextY, kWords, 256);
  }
}

bool drawChosen(const Appearance& a, Canvas240& cv) {
  const ClutchView& v = a.clutch;
  if (v.chosen < 0 || v.chosenMs == kNever) return false;
  const int n = imin(v.count, blorb::kMaxClutch), c = v.chosen, t = v.chosenMs;
  if (!n || c >= n) return false;
  const EggAt from = eggAt(c, n, n > 1 ? 256 : 0);
  if (t >= kHoldMs + kGlideMs) {
    // Handed over: the egg screen's own egg is drawn now; the last sparks go out round it.
    return false;
  }
  // The others fade and shrink away.
  for (int i = 0; i < n; ++i) {
    if (i == c) continue;
    const int gone = easeOut(t, kFadeMs);
    const EggAt e = eggAt(i, n, 0);
    if (gone < 256) drawEggPreview(cv, a.eggs[i], e.at.x, e.at.y + gone * 10 / 256, e.h * (256 - gone / 3) / 256, 0, 256 - gone);
  }
  const int glide = t < kHoldMs ? 0 : easeOut(t - kHoldMs, kGlideMs);
  const int toH = eggArtHeight(a.eggs[c].species);
  const P at = {lerp(from.at.x, kMid, glide), lerp(from.at.y, kMid, glide)};
  const int h = lerp(from.h, toH, glide);
  // A hop of joy as it is kept.
  const int hop = t < 360 ? sinQ12(t * 128 / 360) * 10 / 4096 : 0;
  drawEggPreview(cv, a.eggs[c], at.x, at.y - hop, h, 0, 256);
  // The ring closed, lime, as a locked stance's; it fades on the glide.
  if (glide < 200) {
    Prop p{PropKind::Reticle};
    p.variant = 1;
    p.x = int16_t(at.x), p.y = int16_t(at.y - hop);
    p.scalePct = uint8_t(clampi((h / 2 + 2) * 100 / 15, 60, 255));
    drawPickerProp(cv, p);
  }
  // Sparks flying out of it.
  const int out = easeOut(t, kHoldMs + kGlideMs);
  for (int k = 0; k < 8; ++k) {
    const int ang = k * hud::kTurn / 8 + 40;
    const int r = h / 2 + 8 + out * 40 / 256;
    const int x = at.x + hud::cos1024(ang) * r / 16384, y = at.y + hud::sin1024(ang) * r / 16384;
    spark(cv, x, y, k % 2 ? 4 : 6, 256 - out);
  }
  return true;
}

void drawVigilEggs(const Appearance& a, Canvas240& cv) {
  const int n = imin(a.eggCount, blorb::kMaxClutch);
  if (!n) return;
  // Clearer as the vigil runs out: a promise, not a choice yet.
  const int fade = blorb::clamp01(a.remainsFade).raw >> (blorb::Fx::kFrac - 8);   // 0..256
  const int alpha = 50 + fade * 150 / 256;
  for (int i = 0; i < n; ++i) drawEggPreview(cv, a.eggs[i], kMid + (2 * i - (n - 1)) * 17, 206, 22, 0, alpha);
}

}  // namespace paint
