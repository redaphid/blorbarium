#include <cstring>
#include "blorb/registry.h"
#include "duel/duel_art.h"
#include "figure.h"
#include "hud.h"
#include "paint/phases.h"
#include "paint/duel.h"
#include "paint/text.h"

namespace paint {
namespace {

using blorb::Appearance;
using blorb::Stakes;
using blorb::Stance;
using blorb::Verdict;
namespace expr = blorb::expr;

constexpr int kSide = 240;
constexpr int kMe = 0, kThem = 1;
using Special = blorb::TurnReport::Special;

// The burst's word for a clash, the special's fate first (paint's burst table).
constexpr int kBonk = 0, kSuper = 2, kWham = 4, kNope = 5, kOof = 6, kWhiff = 7;
int beatWord(const Fighter& winner, const Fighter& loser, bool super) {
  if (winner.special == Special::Landed) return kWham;
  if (loser.special == Special::Riposted) return kNope;
  if (loser.special == Special::Staggered) return kOof;
  if (loser.special == Special::Whiffed) return kWhiff;
  return super ? kSuper : kBonk;
}

constexpr int imin(int a, int b) { return a < b ? a : b; }
constexpr int imax(int a, int b) { return a > b ? a : b; }
constexpr int iabs(int a) { return a < 0 ? -a : a; }
int clampi(int v, int lo, int hi) { return imax(lo, imin(hi, v)); }

// sin of a/256 turn, Q12. Angles run clockwise from 3 o'clock, as y runs down.
int sinA(int a) {
  static constexpr int16_t kQ[65] = {
      0,    101,  201,  301,  401,  501,  601,  700,  799,  897,  995,  1092, 1189, 1285, 1380, 1474, 1567,
      1660, 1751, 1842, 1931, 2019, 2106, 2191, 2276, 2359, 2440, 2520, 2598, 2675, 2751, 2824, 2896, 2967,
      3035, 3102, 3166, 3229, 3290, 3349, 3406, 3461, 3513, 3564, 3612, 3659, 3703, 3745, 3784, 3822, 3857,
      3889, 3920, 3948, 3973, 3996, 4017, 4036, 4052, 4065, 4076, 4085, 4091, 4095, 4096};
  a &= 255;
  if (a < 64) return kQ[a];
  if (a < 128) return kQ[128 - a];
  if (a < 192) return -kQ[a - 128];
  return -kQ[256 - a];
}
int cosA(int a) { return sinA(a + 64); }

struct P { int x, y; };
const Fighter& f(const DuelScene& sc, int side) { return side ? sc.them : sc.me; }

P rim(int angle, int r) { return {kSide / 2 + r * cosA(angle) / 4096, kSide / 2 + r * sinA(angle) / 4096}; }

// An act's clock in 1/256 ticks: its ticks and the share of a tick since
// (DuelScene::sub, paint/tween.h), so every ramp and bump moves each frame.
// At a whole tick each is exactly what it was on whole ticks alone.
struct Tq {
  int q;
  int whole() const { return q >> 8; }
};
Tq clockOf(const DuelScene& sc) { return {int(sc.ticks) * 256 + int(sc.sub & 255)}; }
Tq operator%(Tq t, int n) { return {t.q % (n * 256)}; }

// 0..256 through [t0, t1).
int ramp(Tq t, int t0, int t1) {
  return t.q <= t0 * 256 ? 0 : (t.q >= t1 * 256 ? 256 : (t.q - t0 * 256) / (t1 - t0));
}
// 0 -> 256 -> 0 through [t0, t1), peaking at the middle.
int bump(Tq t, int t0, int t1) {
  if (t.q <= t0 * 256 || t.q >= t1 * 256) return 0;
  const int m = (t0 + t1) / 2;
  return t.q < m * 256 ? ramp(t, t0, m) : 256 - ramp(t, m, t1);
}

// ---- the layout: each a one-line change ------------------------------------------------

// Where each pet stands at rest, how big (of his size at home), and how far he
// leans toward the other. Mine is near, so big; the friend's is across the dish.
struct Home { int x, y, scalePct, leanPct; };
constexpr Home kHome[2] = {{84, 208, 54, 8}, {162, 106, 42, -8}};

// The rim, in 1/256 turn: my gauges run down the lower right, the friend's up
// the upper left, the same arc turned half round (hud.cpp draws them). The
// trophy's flight still aims at where the hearts used to start.
constexpr int kHeartArc = 20;
constexpr int kPowerAngle = 40, kPowerR = 62;
constexpr int kFlip = 128;   // the friend's side of the disc

// The three places a tilt points: left, right, toward you.
constexpr int kTiltR = 92;
constexpr P kLeft = {kSide / 2 - kTiltR, kSide / 2}, kRight = {kSide / 2 + kTiltR, kSide / 2},
            kNear = {kSide / 2, kSide / 2 + kTiltR};
constexpr P kStanceAt[3] = {kLeft, kRight, kNear};   // Pounce, Guard, Trick
constexpr P kTierAt[3] = {kLeft, kNear, kRight};     // Spar, Scrap, Mortal

constexpr int kGlyphPct = 125, kAimedPct = 170;
// The stance under the tilt: twice the 24 px art, its 48 px twin drawn 1:1, so
// a kid sees at a glance which one his hand points at.
constexpr int kAimedStancePct = 200;
constexpr int kTimerFillOf = 255;

constexpr uint16_t kClashTicks = 30;   // the reveal takes 3 s
constexpr int kBurstR = 34;       // a burst's radius at full size, px: room between two faces
constexpr int kPop = 0, kArrowAt = 6, kMoveAt = 8, kImpact = 12, kSettle = 22;

// ---- staging ----------------------------------------------------------------------------

// px on screen at the home scale; the face patch's box from the stand point.
struct Body { int top, h, w; int fx0, fy0, fx1, fy1; };

Body bodyOf(const Fighter& f, const SpritePack& pack, int scalePct) {
  const FrameRef b = pack.body(blorb::pose::idle, f.look.stage, 0);
  const FrameRef face = pack.face(expr::neutral, f.look.stage);
  const bool small = f.look.stage == blorb::Stage::Baby;   // squat as draw.cpp draws a hatchling
  const int k = f.look.scalePct * scalePct / 100;   // percent
  const int ky = small ? k * 90 / 100 : k, kx = small ? k * 116 / 100 : k;
  return {b.originY * ky / 100, b.h * ky / 100, b.w * kx / 100,
          -face.originX * kx / 100, -face.originY * ky / 100, (face.w - face.originX) * kx / 100,
          (face.h - face.originY) * ky / 100};
}

bool discClear(const Box& b, int x, int y, int r) {
  if (b.x0 > b.x1) return true;
  const int nx = clampi(x, b.x0, b.x1), ny = clampi(y, b.y0, b.y1);
  return (nx - x) * (nx - x) + (ny - y) * (ny - y) > r * r;
}

// A burst moves to the nearest spot, in rings out from where the pets meet,
// whose whole disc clears both faces and stays on the glass, so it never sits
// on a reaction face and its word stays whole. Where nothing clears it stays
// put and the faces mask it.
void clearOfFaces(Prop& p, const Box* face) {
  const int r = kBurstR * p.scalePct / 100 + 3;
  for (int d = 0; d <= 96; d += 4)
    for (int k = 0; k < (d ? 16 : 1); ++k) {
      const int x = p.x + d * cosA(k * 16) / 4096, y = p.y + d * sinA(k * 16) / 4096;
      const int dx = x - kSide / 2, dy = y - kSide / 2;
      if (dx * dx + dy * dy > (kSide / 2 - r) * (kSide / 2 - r)) continue;
      if (discClear(face[0], x, y, r) && discClear(face[1], x, y, r)) {
        p.x = int16_t(x);
        p.y = int16_t(y);
        return;
      }
    }
}

// His face where the staging left him: squashed, lifted, leaning, grown.
// A little margin, so a burst's ink edge stays off his cheek too.
Box faceBox(const Figure& g, const Body& b, int homePct) {
  Box r;
  if (g.remainsIn == 255 || g.alpha < 64) return r;
  constexpr int kMargin = 3;
  const int k = g.scalePct * 100 / homePct;   // percent of his home size
  const int wide = g.widePct * k / 100, tall = g.tallPct * k / 100;
  const int lean = (iabs(g.leanPct) * (-b.fy0) * tall / 100 + 99) / 100;
  r.x0 = int16_t(g.x + b.fx0 * wide / 100 - lean - kMargin);
  r.x1 = int16_t(g.x + b.fx1 * wide / 100 + lean + kMargin);
  r.y0 = int16_t(g.y - g.lift + b.fy0 * tall / 100 - kMargin);
  r.y1 = int16_t(g.y - g.lift + b.fy1 * tall / 100 + kMargin);
  return r;
}

void add(DuelStaging& s, const Prop& p) {
  if (s.propCount < DuelStaging::kMaxProps) s.props[s.propCount++] = p;
}
Prop prop(PropKind k, int variant, P at, int scalePct = 100, int alpha = 255, int fill = 255) {
  Prop p{};
  p.kind = k;
  p.variant = uint8_t(variant);
  p.x = int16_t(at.x);
  p.y = int16_t(at.y);
  p.scalePct = uint8_t(clampi(scalePct, 0, 255));
  p.alpha = uint8_t(clampi(alpha, 0, 255));
  p.fill = uint8_t(clampi(fill, 0, 255));
  return p;
}

const blorb::PowerInfo& powerFor(const Fighter& f) {
  return blorb::powerInfo(f.power >= 0 ? blorb::PowerId{uint8_t(f.power)} : blorb::speciesInfo(f.look.species).power);
}
blorb::PowerGlyph powerOf(const Fighter& f) { return powerFor(f).glyph; }

Figure atHome(int side) {
  Figure f;
  f.x = int16_t(kHome[side].x);
  f.y = int16_t(kHome[side].y);
  f.scalePct = uint8_t(kHome[side].scalePct);
  f.leanPct = int8_t(kHome[side].leanPct);
  return f;
}
void face(Figure& f, blorb::ExprId e) { f.hasFace = true; f.face = e; }

// His gauges: hearts, and while they fight the spark meter. A hit landing
// (`losing` of them) keeps those hearts full until the impact, then flashes
// and drains them.
Gauges gaugesOf(const Fighter& f, bool meter, int losing, Tq q, int sweep) {
  Gauges g;
  g.shown = true;
  g.meter = meter;
  g.maxHearts = uint8_t(clampi(f.maxHearts, 1, 8));
  g.hearts = uint8_t(imin(f.hearts, g.maxHearts));
  g.assist = uint8_t(imin(f.assist, g.maxHearts));
  g.losing = uint8_t(clampi(losing, 0, g.maxHearts - g.hearts));
  if (g.losing && q.whole() >= kImpact) {
    g.drain = uint8_t(imax(1, ramp(q, kImpact + 1, kImpact + 9) * 255 / 256));
    g.flash = uint8_t(255 - ramp(q, kImpact, kImpact + 4) * 255 / 256);
  }
  g.sparks = uint8_t(imin(f.sparks, blorb::kMeterMax));
  g.cost = powerFor(f).cost;
  g.pulse = uint8_t(imin(255, bump(q % 10, 0, 10)));
  g.sweep = uint8_t(sweep);
  return g;
}

// The power's glyph at the meter's hub: small and dim until it is paid for.
void meter(DuelStaging& s, const Fighter& f, int side, Tq q) {
  const blorb::PowerInfo& pw = powerFor(f);
  const bool ready = f.sparks >= pw.cost;
  // Not yet: small and dim. Ready: full size and breathing. Armed: big, over his head (staged by the caller).
  const int pulse = ready ? 100 + bump(q % 10, 0, 10) * 15 / 256 : 70;
  add(s, prop(PropKind::Power, int(pw.glyph), rim(kPowerAngle + side * kFlip, kPowerR), pulse, ready ? 255 : 120));
}

P headOf(const Figure& f, const Body& b) { return {f.x, f.y - b.top - 6}; }
// Where a pet's stance pops in the clash: beside his head on his outer side,
// so the clash itself has the middle of the dish.
P popAt(P head, int side) { return {head.x + (side ? 28 : -38), head.y + (side ? 18 : 14)}; }


// A special winding up, the same on both screens: he crouches into it, light
// in his power's colour gathers behind him, his power's glyph hangs over him
// and pips under it count the turns to the landing. On the turn it lands he
// shakes and the glyph swells. What stance it lands with is never shown.
void telegraph(const Fighter& x, Figure& g, const Body& b, int side, Tq q, DuelStaging& s) {
  if (x.windup == blorb::Windup::None) return;
  const int t = q.whole();
  const bool now = x.landsIn == 0;
  const int heavy = x.windup == blorb::Windup::Heavy;
  g.widePct = uint8_t(g.widePct + 6 + 4 * heavy);
  g.tallPct = uint8_t(g.tallPct - 6 - 4 * heavy);
  if (now) g.x = int16_t(g.x + ((t & 1) ? 1 : -1));
  const int pulse = bump(q % (now ? 4 : 10), 0, now ? 4 : 10);
  const P mid = {g.x, g.y - b.h / 2};
  add(s, prop(PropKind::Aura, int(powerOf(x)), mid, (side ? 70 : 100) + 15 * heavy, 120 + pulse * (now ? 135 : 70) / 256));
  const P head = headOf(g, b);
  const P over = side ? P{head.x - 42, head.y + 34} : P{head.x + 30, head.y - 8};   // the friend's clear of the top rim and the hint
  const int glyphPct = (now ? 150 : 115) + pulse * 20 / 256;
  add(s, prop(PropKind::Power, int(powerOf(x)), over, glyphPct));
  add(s, prop(PropKind::Countdown, x.landsIn, over, glyphPct, 255, now ? pulse : 0));
}

void meeting(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  const Tq q = clockOf(sc);
  // Each drops into his own spot from above and lands with a bounce, so the
  // first frame already shows both of them arriving.
  const int drop[2] = {44, 22};   // the friend's head is near the top of the glass
  for (int side = 0; side < 2; ++side) {
    Figure& f = s.fig[side];
    f.lift = int8_t(drop[side] * (256 - ramp(q, 0, 7)) / 256 + bump(q, 7, 12) * 8 / 256);
    if (t >= 6 && t < 8) {   // the landing squash
      f.widePct = 114;
      f.tallPct = 86;
    }
    face(f, t < 8 ? expr::alarmed : expr::happy);
  }
  if (t >= 8) {
    add(s, prop(PropKind::Burst, 1, {124, 114}, 64 + ramp(q, 8, 11) * 40 / 256 - bump(q, 10, 14) * 10 / 256));
    const blorb::Element e[2] = {blorb::speciesInfo(sc.me.look.species).element,
                                 blorb::speciesInfo(sc.them.look.species).element};
    for (int side = 0; side < 2; ++side)
      add(s, prop(PropKind::Element, int(e[side]), rim(kPowerAngle + side * kFlip, kPowerR), 100, ramp(q, 10, 14)));
  }
  (void)b;
}

void staking(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  const Tq q = clockOf(sc);
  const Fighter* f[2] = {&sc.me, &sc.them};
  // The rim: the three tiers, mine big, a struck bar over any above my ceiling.
  for (int tier = 0; tier < 3; ++tier) {
    const bool aimed = tier == int(sc.me.chose);
    const bool barred = tier > int(sc.me.ceiling);
    const int scale = aimed ? kAimedPct : kGlyphPct;
    const P at = {kTierAt[tier].x + (aimed ? (kSide / 2 - kTierAt[tier].x) / 8 : 0),
                  kTierAt[tier].y + (aimed ? (kSide / 2 - kTierAt[tier].y) / 8 : 0)};
    const int fill = tier == int(Stakes::Mortal) && aimed ? sc.me.hold : 255;
    add(s, prop(PropKind::Tier, tier, at, scale, barred ? 110 : 255, fill));
    if (barred) add(s, prop(PropKind::Strike, 0, at, scale));
    // The reticle on the tier under the tilt: closed once confirmed, a ring
    // filling while the Mortal confirm is held.
    if (aimed) add(s, prop(PropKind::Reticle, sc.me.confirmed ? 1 : 0, at, scale, 255, sc.me.confirmed ? 255 : fill));
  }
  // Each pet wears the risk of the tier his kid aims at: a feather (play), a
  // plaster on his head (wounds), or a skull over him filled to his forecast.
  for (int side = 0; side < 2; ++side) {
    const Fighter& x = *f[side];
    const P head = headOf(s.fig[side], b[side]);
    const int bob = bump(q % 16, 0, 16) * 3 / 256;
    // Over my head; beside the friend's, whose head is near the top of the glass.
    const P over = side ? P{head.x - 40, head.y + 16} : P{head.x, head.y - 14};
    switch (x.chose) {
      case Stakes::Spar: add(s, prop(PropKind::Tier, 0, {over.x, over.y - bob}, side ? 90 : 100)); break;
      case Stakes::Scrap:
        add(s, prop(PropKind::Plaster, 0, {head.x + (side ? -6 : 8), head.y + 14}, side ? 90 : 120));
        face(s.fig[side], expr::annoyed);
        break;
      case Stakes::Mortal:
        add(s, prop(PropKind::Tier, 2, {over.x, over.y - bob}, side ? 120 : 140, 255, imax(x.skull, 1)));
        face(s.fig[side], expr::alarmed);
        break;
    }
    if (side == kThem && x.confirmed) add(s, prop(PropKind::Lock, 3, {head.x + 26, head.y + 12}));
  }
  // Both confirmed: the agreed tier, the lower of the two, in the middle.
  if (sc.me.confirmed && sc.them.confirmed)
    add(s, prop(PropKind::Tier, int(sc.stakes), {120, 128}, 150 + bump(q % 20, 0, 20) * 20 / 256));
}

void choosing(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  const Tq q = clockOf(sc);
  // The rim ring is the clock: it runs out round the glass.
  add(s, prop(PropKind::Timer, 0, {120, 120}, 100, 255,
              sc.turnTicks ? sc.ticksLeft * kTimerFillOf / sc.turnTicks : 0));
  for (int st = 0; st < 3; ++st) {
    const bool aimed = st == sc.aim;
    const P at = {kStanceAt[st].x + (aimed ? (kSide / 2 - kStanceAt[st].x) / 6 : 0),
                  kStanceAt[st].y + (aimed ? (kSide / 2 - kStanceAt[st].y) / 6 : 0)};
    // The lean: my pet's pick, a soft light under it, brighter with the assist.
    // Wider under the big one, so it still shows round its backing.
    if (st == sc.lean && !sc.me.locked)
      add(s, prop(PropKind::Glow, 0, at, aimed ? 120 : 100, 90 + sc.whisper * 165 / 255 * (192 + bump(q % 12, 0, 12) / 4) / 256));
    add(s, prop(PropKind::Stance, st, at, aimed ? kAimedStancePct : kGlyphPct, aimed || sc.aim < 0 ? 255 : 150));
    if (aimed) add(s, prop(PropKind::Reticle, sc.me.locked ? 1 : 0, at, kAimedStancePct));
  }
  // My pet leans toward the stance under the tilt, his kid's hand in his body.
  if (sc.aim >= 0) {
    const P to = kStanceAt[sc.aim];
    s.fig[kMe].x = int16_t(s.fig[kMe].x + (to.x - 120) / 12);
    s.fig[kMe].leanPct = int8_t(clampi(s.fig[kMe].leanPct + (to.x - 120) / 6, -24, 24));
  }
  telegraph(sc.me, s.fig[kMe], b[kMe], kMe, q, s);
  telegraph(sc.them, s.fig[kThem], b[kThem], kThem, q, s);
  // Over the friend: a spinner while he chooses, the ring closed once he has
  // locked; never the stance.
  const P head = headOf(s.fig[kThem], b[kThem]);
  add(s, prop(PropKind::Lock, sc.them.locked ? 3 : (t / 4) % 3, {head.x + 26, head.y + 12}));
}

// A pet plays his own stance: a Pounce lunges at the other, a Guard curls up
// behind his shield, a Trick sidesteps and darts in from the side.
void play(Figure& f, int stance, P toward, Tq q) {
  const int dx = toward.x - f.x, dy = toward.y - f.y;
  if (stance == int(Stance::Pounce)) {
    const int go = ramp(q, kMoveAt, kImpact) - ramp(q, kImpact + 2, kSettle);
    f.x = int16_t(f.x + dx * 40 / 100 * go / 256);
    f.y = int16_t(f.y + dy * 40 / 100 * go / 256);
    f.widePct = uint8_t(100 - 10 * go / 256);
    f.tallPct = uint8_t(100 + 12 * go / 256);
    f.lift = int8_t(10 * bump(q, kMoveAt, kImpact + 2) / 256);
  } else if (stance == int(Stance::Guard)) {
    const int curl = ramp(q, kMoveAt, kMoveAt + 3) - ramp(q, kImpact + 4, kSettle);
    f.widePct = uint8_t(100 + 18 * curl / 256);
    f.tallPct = uint8_t(100 - 18 * curl / 256);
  } else if (stance == int(Stance::Trick)) {
    const int side = ramp(q, kMoveAt, kMoveAt + 2) - ramp(q, kImpact + 2, kSettle);
    const int dart = bump(q, kMoveAt + 2, kImpact + 3);
    f.x = int16_t(f.x - dy * 22 / 100 * side / 256 + dx * 25 / 100 * dart / 256);
    f.y = int16_t(f.y + dx * 22 / 100 * side / 256 + dy * 25 / 100 * dart / 256);
    f.leanPct = int8_t(f.leanPct + (dx > 0 ? 14 : -14) * side / 256);
  }
}

void clash(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  const Tq q = clockOf(sc);
  const Fighter* f[2] = {&sc.me, &sc.them};
  const P home[2] = {{s.fig[kMe].x, s.fig[kMe].y}, {s.fig[kThem].x, s.fig[kThem].y}};
  // Where they meet: between my head and the friend's middle.
  const P mid = {(home[kMe].x + home[kThem].x) / 2, (home[kMe].y - b[kMe].h * 2 / 3 + home[kThem].y - b[kThem].h / 2) / 2};
  for (int side = 0; side < 2; ++side) {
    Figure& g = s.fig[side];
    const Fighter& x = *f[side];
    const P other = home[1 - side];
    const bool bracing = x.windup != blorb::Windup::None && x.special != Special::Landed &&
                         x.special != Special::Riposted && x.special != Special::Whiffed;
    if (x.stance < 0) face(g, expr::asleep);   // dozed through it
    else if (bracing) telegraph(x, g, b[side], side, q, s);
    else play(g, x.special == Special::Landed || x.special == Special::Whiffed ? int(Stance::Pounce) : x.stance, other, q);
    // His stance pops over his head, a power glyph with it when it fired.
    const P head = headOf(Figure{int16_t(home[side].x), int16_t(home[side].y)}, b[side]);
    const int pop = imin(ramp(q, kPop, kPop + 4) * 140 / 256, 140 - ramp(q, kPop + 4, kPop + 6) * 20 / 256);
    if (x.stance >= 0 && !bracing) add(s, prop(PropKind::Stance, x.stance, popAt(head, side), pop));
    if (x.fired) {
      const blorb::PowerInfo& pw = powerFor(x);
      add(s, prop(PropKind::Power, int(pw.glyph), {head.x, head.y - 16}, 100 + bump(q, 0, 10) * 40 / 256, 255 - ramp(q, 16, 24) * 255 / 256));
      if (t < kImpact) face(g, expr::foresee);
    }
  }
  const int win = sc.won == Side::Me ? kMe : (sc.won == Side::Them ? kThem : -1);
  // The "beats" arrow, from the winner's stance glyph toward the loser's.
  if (win >= 0 && t >= kArrowAt && t < kImpact + 2) {   // gone by the hit, off the faces
    const P a = popAt(headOf(Figure{int16_t(home[win].x), int16_t(home[win].y)}, b[win]), win);
    const P z = popAt(headOf(Figure{int16_t(home[1 - win].x), int16_t(home[1 - win].y)}, b[1 - win]), 1 - win);
    Prop arrow = prop(PropKind::Arrow, 0, {a.x + (z.x - a.x) / 4, a.y + (z.y - a.y) / 4}, 100, ramp(q, kArrowAt, kArrowAt + 2));
    arrow.x2 = int16_t(z.x - (z.x - a.x) / 4);
    arrow.y2 = int16_t(z.y - (z.y - a.y) / 4);
    add(s, arrow);
  }
  if (t >= kImpact && t < kSettle + 4) {
    const int grow = 60 + ramp(q, kImpact, kImpact + 3) * 60 / 256;
    // The burst holds its word for under a second and goes, so the pets read again.
    const int fade = 255 - ramp(q, kImpact + 6, kImpact + 9) * 255 / 256;
    if (win < 0) {
      if (fade) add(s, prop(PropKind::Burst, 3, mid, grow - 10, fade));
      for (int side = 0; side < 2; ++side) add(s, prop(PropKind::Stars, t % 16, headOf(s.fig[side], b[side]), side ? 80 : 100));
    } else {
      const int lose = 1 - win;
      // The burst lands where they meet, a little toward the one it hurt.
      const P loser = {home[lose].x, home[lose].y - b[lose].h / 2};
      const P at = {mid.x + (loser.x - mid.x) / 8, mid.y + (loser.y - mid.y) / 8};
      const int word = beatWord(*f[win], *f[lose], sc.super);
      if (fade) add(s, prop(PropKind::Burst, word, at, grow, fade));
      // A landing is big: both pets jolt with it.
      if (f[win]->special == Special::Landed && t < kImpact + 5)
        for (Figure& g : s.fig) g.y = int16_t(g.y + ((t & 1) ? 2 : -2));
    }
  }
  if (win >= 0) {
    const int lose = 1 - win;
    Figure& g = s.fig[lose];
    if (f[lose]->hit) {
      // Struck: a white flash, knocked back away from the winner, and the face says it.
      g.flash = uint8_t(255 - ramp(q, kImpact, kImpact + 6) * 255 / 256);
      if (t < kImpact) g.flash = 0;
      const int back = bump(q, kImpact, kSettle) * 14 / 256;
      g.x = int16_t(g.x + (home[lose].x - home[win].x) * back / 120);
      g.y = int16_t(g.y + (home[lose].y - home[win].y) * back / 120);
      if (t >= kImpact) face(g, expr::alarmed);
      // The hearts he lost flash and drain out of his gauge (DuelStaging::gauge).
    }
    if (t >= kImpact + 2) face(s.fig[win], expr::happy);
    // Staggered mid wind-up: knocked out of it, stars round his head.
    if (f[lose]->special == Special::Staggered && t >= kImpact)
      add(s, prop(PropKind::Stars, t % 16, headOf(s.fig[lose], b[lose]), lose ? 80 : 100));
  }
  // A dodge: the special whiffs through where he was (Hush folds away into the dark).
  for (int side = 0; side < 2; ++side)
    if (f[1 - side]->special == Special::Whiffed)
      s.fig[side].alpha = uint8_t(255 - bump(q, kMoveAt, kSettle) * 170 / 256);
}

// The downed pet: slumped, leaning away, eyes shut, stars round his head.
void slump(Figure& f, int awayX) {
  f.tallPct = 64;
  f.widePct = 124;
  f.leanPct = int8_t(awayX < 0 ? -20 : 20);
  face(f, expr::asleep);
}

// The skull over the downed pet, clear of both pets and the rim: [me down, them down].
constexpr P kMercySkull[2] = {{100, 64}, {108, 58}};

void mercy(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  const Tq q = clockOf(sc);
  const int win = sc.won == Side::Me ? kMe : kThem, down = 1 - win;
  slump(s.fig[down], s.fig[down].x - s.fig[win].x);
  add(s, prop(PropKind::Stars, t % 16, headOf(s.fig[down], Body{b[down].top * 64 / 100, b[down].h, b[down].w}), down ? 80 : 100));
  // The winner stands over him: a little bigger, leaning in, grim once the thumb is down.
  s.fig[win].scalePct = uint8_t(s.fig[win].scalePct + 6);
  face(s.fig[win], sc.finishHold ? expr::annoyed : expr::neutral);
  add(s, prop(PropKind::Timer, 0, {120, 120}, 100, 255, sc.mercyTicks ? sc.mercyLeft * kTimerFillOf / sc.mercyTicks : 0));
  // The skull over the downed pet fills from the bottom while the winner holds BOOT.
  add(s, prop(PropKind::Tier, 2, kMercySkull[down], 190, 255, imax(sc.finishHold, 1)));
  add(s, prop(PropKind::Reticle, 2, kMercySkull[down], 190, 255, imin(sc.finishHold, 254)));
  if (sc.won == Side::Me) {
    // Mine to decide, in the empty lower right: hold BOOT for the skull, or knock for the heart.
    add(s, prop(PropKind::Button, 0, {150, 164}, 100, 230));
    add(s, prop(PropKind::Tier, 2, {178, 164}, 75));
    add(s, prop(PropKind::Button, 1, {150, 198}, 100, 230));
    add(s, prop(PropKind::Plea, 0, {178, 198}, 130));
  }
  // The downed pet's kid can only plead: a heart floats up toward the winner.
  if (sc.pleading) {
    const int up = q.q / 20 % 256;
    const P from = headOf(s.fig[down], b[down]), to = headOf(s.fig[win], b[win]);
    add(s, prop(PropKind::Plea, 0, {from.x + (to.x - from.x) * up / 256, from.y + (to.y - from.y) * up / 256 - 10}, 130,
                255 - up * 200 / 256));
  }
}

void fallen(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  const Tq q = clockOf(sc);
  const int win = sc.won == Side::Me ? kMe : kThem, down = 1 - win;
  face(s.fig[win], expr::happy);
  if (sc.verdict == Verdict::Finish) {
    // He is gone: his body fades to his remains, the full skull fades over him.
    Figure& g = s.fig[down];
    slump(g, g.x - s.fig[win].x);
    g.remainsIn = uint8_t(ramp(q, 2, 14) * 255 / 256);
    const P head = headOf(s.fig[down], b[down]);
    add(s, prop(PropKind::Tier, 2, {(head.x + 120) / 2, imax(52, head.y - 22) - ramp(q, 0, 20) * 20 / 256}, 200,
                255 - ramp(q, 10, 24) * 255 / 256));
    // The victor's kid takes the fallen's token: it floats to the victor's rim.
    const int fly = ramp(q, 14, 30);
    const P from = {s.fig[down].x, s.fig[down].y - b[down].h / 2}, to = rim(kHeartArc + 4 + win * kFlip, 82);
    if (t >= 14)
      add(s, prop(PropKind::Trophy, int(f(sc, down).look.species.v),
                  {from.x + (to.x - from.x) * fly / 256, from.y + (to.y - from.y) * fly / 256 - bump(q, 14, 30) * 20 / 256}, 120));
  } else {
    // Spared: he sits up, still dazed, and his line owes the victor's a life.
    Figure& g = s.fig[down];
    const int up = ramp(q, 0, 10);
    g.tallPct = uint8_t(72 + 20 * up / 256);
    g.widePct = uint8_t(118 - 12 * up / 256);
    g.leanPct = int8_t((g.x < s.fig[win].x ? -20 : 20) * (256 - up) / 256);
    face(g, t < 10 ? expr::asleep : expr::sleepy);
    add(s, prop(PropKind::Debt, 0, {120, 120 - ramp(q, 4, 16) * 16 / 256}, 100 + ramp(q, 4, 10) * 60 / 256, ramp(q, 4, 8)));
  }
}

void outcome(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  const Tq q = clockOf(sc);
  if (sc.won == Side::Neither) {
    for (int side = 0; side < 2; ++side) face(s.fig[side], expr::sleepy);
  } else {
    const int win = sc.won == Side::Me ? kMe : kThem, down = 1 - win;
    // The winner hops for joy; the loser has fainted, stars round his head.
    Figure& w = s.fig[win];
    const Tq ph = q % 10;
    w.lift = int8_t(bump(ph, 1, 9) * 16 / 256);
    w.widePct = uint8_t(ph.whole() < 1 || ph.whole() > 8 ? 112 : 100);
    w.tallPct = uint8_t(ph.whole() < 1 || ph.whole() > 8 ? 88 : 100);
    face(w, expr::happy);
    if (sc.stakes == Stakes::Mortal && sc.verdict == Verdict::Finish) {
      s.fig[down].remainsIn = 255;
      add(s, prop(PropKind::Trophy, int(f(sc, down).look.species.v), rim(kHeartArc + 4 + win * kFlip, 82), 120));
    } else {
      slump(s.fig[down], s.fig[down].x - w.x);
      s.fig[down].tallPct = 82;
      add(s, prop(PropKind::Stars, t % 16, headOf(s.fig[down], Body{b[down].top * 82 / 100, b[down].h, b[down].w}),
                  down ? 80 : 100));
    }
    if (sc.stakes == Stakes::Mortal && sc.verdict == Verdict::Spare) add(s, prop(PropKind::Debt, 0, {120, 104}, 140));
  }
  // My new scars come in, darkening into his skin.
  s.fig[kMe].newScars = sc.newScars;
  s.fig[kMe].scarIn = uint8_t(ramp(q, 0, 20) * 255 / 256);
}

}  // namespace

namespace {

// One side's state between turns onto a fighter; `now` is the turn on screen.
void sideOnto(Fighter& f, const blorb::SideState& s, int now) {
  f.hearts = s.hearts;
  f.maxHearts = s.maxHearts;
  f.sparks = s.spark;
  f.windup = s.queued;
  f.landsIn = s.queued != blorb::Windup::None && s.landsOn > now ? uint8_t(s.landsOn - now) : 0;
}

// A resolved turn as the clash plays it; side `m` of the report is mine.
void turnOnto(DuelScene& sc, const blorb::TurnReport& r, int m) {
  Fighter* f[2] = {&sc.me, &sc.them};
  for (int i = 0; i < 2; ++i) {
    const int s = i ? 1 - m : m;
    f[i]->stance = r.dozed[s] ? int8_t(-1) : int8_t(r.shown[s]);
    f[i]->hit = r.hit[s];
    f[i]->fired = r.powered[s];
    f[i]->special = r.special[s];
    // Braced through that turn with nothing queued after it: knocked out of it, or it was the commit.
    if (r.bracing[s] && f[i]->windup == blorb::Windup::None) f[i]->windup = blorb::Windup::Quick;
  }
  const int winner = r.clash == blorb::Clash::AWins ? 0 : 1;
  sc.won = r.clash == blorb::Clash::Bonk ? Side::Neither : (winner == m ? Side::Me : Side::Them);
  sc.super = sc.won != Side::Neither && r.super[sc.won == Side::Me ? m : 1 - m];
}

// The friend's pet from his card: his species' art in his colours, his size, stage and marks.
blorb::Appearance lookOf(const blorb::BattleCard& c) {
  blorb::Appearance a;
  a.species = c.species;
  a.stage = c.look.stage;
  a.scalePct = c.look.scalePct;
  a.lifeSeed = c.look.lifeSeed;
  for (size_t r = 0; r < blorb::kRegionCount; ++r) a.regions[r] = c.look.regions[r];
  a.markCount = uint8_t(imin(c.look.markCount, 4));
  for (int i = 0; i < a.markCount; ++i) a.marks[i] = {c.look.marks[i].layer, c.look.marks[i].variant, blorb::Tint{}};
  a.exprTicks = 100;
  return a;
}

DuelAct actOf(blorb::DuelPhase p) {
  using P = blorb::DuelPhase;
  switch (p) {
    case P::Staking: return DuelAct::Staking;
    case P::Choosing: case P::Waiting: return DuelAct::Choosing;
    case P::Revealing: return DuelAct::Clash;
    case P::Mercy: return DuelAct::Mercy;
    case P::Fallen: return DuelAct::Fallen;
    case P::Fainting: case P::Friends: return DuelAct::Outcome;
    case P::Closed: case P::Lobby: case P::Greeting: return DuelAct::Meeting;
  }
  return DuelAct::Meeting;
}

}  // namespace

DuelScene sceneOf(const blorb::DuelView& v, const blorb::Appearance& mine) {
  DuelScene sc;
  sc.act = actOf(v.phase);
  sc.ticks = v.phaseTicks;
  sc.me.look = mine;
  sc.them.look = lookOf(v.them);
  sc.me.power = int16_t(v.me.power.v);
  sc.them.power = int16_t(v.them.power.v);
  // The turn on screen: the last one while it plays out, else the one being chosen.
  const int now = v.hasLast ? v.lastTurn + (sc.act == DuelAct::Clash ? 0 : 1) : 0;
  sideOnto(sc.me, v.mine, now);
  sideOnto(sc.them, v.theirs, now);
  sc.me.assist = v.assistHearts[0];
  sc.them.assist = v.assistHearts[1];
  // Staking: each side's tier, ceiling, confirm and forecast at the tier it aims.
  sc.me.chose = v.chosen;
  sc.them.chose = v.theyChose;
  sc.me.ceiling = v.ceiling[0];
  sc.them.ceiling = v.ceiling[1];
  sc.me.confirmed = v.confirmed;
  sc.them.confirmed = v.theyConfirmed;
  sc.me.skull = v.skull[0][uint8_t(v.chosen) % 3];
  sc.them.skull = v.skull[1][uint8_t(v.theyChose) % 3];
  sc.stakes = v.agreed;
  // Choosing
  sc.aim = v.aim;
  sc.lean = v.locked ? int8_t(-1) : int8_t(v.lean.stance);
  sc.whisper = v.lean.whisper;
  sc.me.locked = v.locked;
  sc.them.locked = v.theyLocked;
  sc.ticksLeft = v.chooseTicksLeft;
  sc.turnTicks = v.chooseTicks;   // shorter each turn (DuelTimings::chooseTicks)
  if (v.hasLast && sc.act == DuelAct::Clash) turnOnto(sc, v.last, 0);   // the view mirrors it: index 0 is me
  // Mercy and the end: the downed side lost; in Mercy the hold is the winner's Finish.
  if (sc.act != DuelAct::Clash) sc.won = v.draw ? Side::Neither : (v.iWon ? Side::Me : Side::Them);
  if (v.iAmDown) sc.won = Side::Them;
  if (v.theyAreDown) sc.won = Side::Me;
  if (sc.act == DuelAct::Staking) {
    sc.me.hold = v.confirmHold;
    sc.them.hold = v.theirHold;
  }
  sc.finishHold = v.iAmDown ? v.theirHold : v.confirmHold;
  sc.mercyLeft = v.mercyTicksLeft;
  sc.mercyTicks = blorb::kMercyTicks;
  sc.verdict = v.verdict;
  sc.pleading = v.emoteIn == blorb::Emote::Plead;
  sc.newScars = v.newScars;
  return sc;
}

void fromOutcome(DuelScene& sc, const blorb::Outcome& o, blorb::Side me) {
  const int m = int(me), t = 1 - m;
  sideOnto(sc.me, o.side[m], o.resolved);
  sideOnto(sc.them, o.side[t], o.resolved);
  sc.stakes = o.stakes;
  auto sideOf = [&](int s) { return s == m ? Side::Me : Side::Them; };
  if (o.resolved) turnOnto(sc, o.turns[o.resolved - 1], m);
  if (o.ending != blorb::Ending::Ongoing)
    sc.won = o.ending == blorb::Ending::Draw ? Side::Neither : sideOf(o.ending == blorb::Ending::AWon ? 0 : 1);
  sc.verdict = o.verdict;
}

DuelStaging stageDuel(const DuelScene& sc, const SpritePack& mine, const SpritePack& theirs) {
  DuelStaging s;
  s.fig[kMe] = atHome(kMe);
  s.fig[kThem] = atHome(kThem);
  const Body b[2] = {bodyOf(sc.me, mine, kHome[kMe].scalePct), bodyOf(sc.them, theirs, kHome[kThem].scalePct)};
  switch (sc.act) {
    case DuelAct::Meeting: meeting(sc, s, b); break;
    case DuelAct::Staking: staking(sc, s, b); break;
    case DuelAct::Choosing: choosing(sc, s, b); break;
    case DuelAct::Clash: clash(sc, s, b); break;
    case DuelAct::Mercy: mercy(sc, s, b); break;
    case DuelAct::Fallen: fallen(sc, s, b); break;
    case DuelAct::Outcome: outcome(sc, s, b); break;
  }
  // The gauges stay on the rim through the fight itself, sweeping in as they meet.
  const bool fighting = sc.act == DuelAct::Choosing || sc.act == DuelAct::Clash || sc.act == DuelAct::Meeting;
  if (fighting || sc.act == DuelAct::Outcome) {
    const Tq q = clockOf(sc);
    const bool clashing = sc.act == DuelAct::Clash;
    const int sweep = sc.act == DuelAct::Meeting ? ramp(q, 6, 14) * 255 / 256 : 255;
    s.gauge[kMe] = gaugesOf(sc.me, fighting, clashing ? sc.me.hit : 0, q, sweep);
    s.gauge[kThem] = gaugesOf(sc.them, fighting, clashing ? sc.them.hit : 0, q, sweep);
    if (fighting) {
      meter(s, sc.me, kMe, q);
      meter(s, sc.them, kThem, q);
    }
  }
  for (int side = 0; side < 2; ++side) s.face[side] = faceBox(s.fig[side], b[side], kHome[side].scalePct);
  for (int i = 0; i < s.propCount; ++i)
    if (s.props[i].kind == PropKind::Burst) clearOfFaces(s.props[i], s.face);
  return s;
}

namespace {

// ---- painting the list ------------------------------------------------------------------

struct Rgb { int r, g, b; };
uint16_t c565(Rgb c) { return uint16_t(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }

constexpr Rgb kInk{24, 14, 22};          // every prop's outline: a shape reads before its colour does
constexpr Rgb kCream{255, 244, 214};
constexpr Rgb kSpark{255, 226, 90};
constexpr Rgb kBurst{255, 225, 74}, kBurstEdge{196, 64, 16};
constexpr Rgb kStrike{200, 30, 40};
constexpr Rgb kGlowLight{255, 240, 170};

// Where a burst may not paint: the two faces (drawStaged sets it while bursts draw).
const Box* gKeepOut = nullptr;
bool keptOut(int x, int y) {
  if (!gKeepOut) return false;
  for (int i = 0; i < 2; ++i)
    if (x >= gKeepOut[i].x0 && x <= gKeepOut[i].x1 && y >= gKeepOut[i].y0 && y <= gKeepOut[i].y1) return true;
  return false;
}

void put(Canvas240& cv, int x, int y, uint16_t c, int alpha) {
  if (keptOut(x, y)) return;
  if (x < 0 || y < 0 || x >= kSide || y >= kSide || alpha <= 0) return;
  uint16_t& px = cv.px[y * kSide + x];
  px = alpha >= 256 ? c : blend(px, c, alpha);
}

void disc(Canvas240& cv, int cx, int cy, int r, Rgb c, int alpha) {
  const uint16_t k = c565(c);
  // The same pixels as the test below, a run a row from hud's half-chords
  // (x^2 + y^2 <= r^2 is the half-pixel grid's (2x)^2 + (2y)^2 <= (2r)^2).
  if (!gKeepOut && r > 0 && r <= 120) return hud::arc(cv, 2 * cx + 1, 2 * cy + 1, -1, r, 0, hud::kTurn, k, alpha);
  for (int y = cy - r; y <= cy + r; ++y)
    for (int x = cx - r; x <= cx + r; ++x)
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) put(cv, x, y, k, alpha);
}

// Whether a pixel is within r of a segment, for every stroke here.
bool nearSeg(int px, int py, int x0, int y0, int x1, int y1, int r) {
  const int64_t vx = x1 - x0, vy = y1 - y0, wx = px - x0, wy = py - y0;
  const int64_t len2 = vx * vx + vy * vy;
  int64_t t = len2 ? (wx * vx + wy * vy) * 256 / len2 : 0;
  t = t < 0 ? 0 : (t > 256 ? 256 : t);
  const int64_t dx = wx * 256 - t * vx, dy = wy * 256 - t * vy;
  return dx * dx + dy * dy <= int64_t(r) * r * 65536;
}

void stroke(Canvas240& cv, int x0, int y0, int x1, int y1, int r, Rgb c, int alpha) {
  const uint16_t k = c565(c);
  for (int y = imin(y0, y1) - r - 1; y <= imax(y0, y1) + r + 1; ++y)
    for (int x = imin(x0, x1) - r - 1; x <= imax(x0, x1) + r + 1; ++x)
      if (nearSeg(x, y, x0, y0, x1, y1, r)) put(cv, x, y, k, alpha);
}
// A stroke with an ink edge round it.
void inked(Canvas240& cv, int x0, int y0, int x1, int y1, int r, Rgb c, int alpha) {
  stroke(cv, x0, y0, x1, y1, r + 2, kInk, alpha);
  stroke(cv, x0, y0, x1, y1, r, c, alpha);
}

// Small bitmaps, one string a row, drawn at a scale with an ink edge.
struct Bits { int w, h; const char* const* rows; };
constexpr const char* kHeartRows[] = {".XXX...XXX.", "XXXXX.XXXXX", "XXXXXXXXXXX", "XXXXXXXXXXX", "XXXXXXXXXXX",
                                      ".XXXXXXXXX.", "..XXXXXXX..", "...XXXXX...", "....XXX....", ".....X....."};
constexpr Bits kHeart{11, 10, kHeartRows};
// The two gestures a kid's hands make in Mercy: hold BOOT, or knock.
constexpr const char* kButtonRows[] = {"..XXXXX..", ".X.....X.", "X..XXX..X", "X..XXX..X", "X..XXX..X",
                                       ".X.....X.", "..XXXXX..", ".........", "XXXXXXXXX"};
constexpr const char* kKnockRows[] = {"X...X...X", ".X..X..X.", ".........", "XX.....XX", ".........",
                                      "..XXXXX..", ".XXXXXXX.", ".XXXXXXX.", "..XXXXX.."};
constexpr Bits kButton{9, 9, kButtonRows}, kKnock{9, 9, kKnockRows};

bool bitAt(const Bits& b, int x, int y) { return x >= 0 && y >= 0 && x < b.w && y < b.h && b.rows[y][x] == 'X'; }

// `hollow`: only the shape's edge, its inside dark: a lost heart.
void bits(Canvas240& cv, const Bits& b, int cx, int cy, int scalePct, Rgb fill, int alpha, bool hollow) {
  const int W = imax(1, b.w * scalePct / 100), H = imax(1, b.h * scalePct / 100);
  const int x0 = cx - W / 2, y0 = cy - H / 2;
  auto in = [&](int ox, int oy) { return ox >= 0 && oy >= 0 && ox < W && oy < H && bitAt(b, ox * b.w / W, oy * b.h / H); };
  const uint16_t ink = c565(kInk), body = c565(fill);
  for (int oy = -2; oy < H + 2; ++oy)
    for (int ox = -2; ox < W + 2; ++ox) {
      const bool inside = in(ox, oy);
      bool near = inside;
      for (int dy = -2; dy <= 2 && !near; ++dy)
        for (int dx = -2; dx <= 2 && !near; ++dx) near = (dx * dx + dy * dy <= 5) && in(ox + dx, oy + dy);
      if (!near) continue;
      const bool edge = inside && !(in(ox - 2, oy) && in(ox + 2, oy) && in(ox, oy - 2) && in(ox, oy + 2));
      if (!inside) put(cv, x0 + ox, y0 + oy, ink, alpha);
      else if (hollow) put(cv, x0 + ox, y0 + oy, edge ? body : ink, edge ? alpha : alpha * 200 / 256);
      else put(cv, x0 + ox, y0 + oy, body, alpha);
    }
}

// A four-point sparkle: the pet's cheer on an assist heart, and a dizzy star.
void sparkle(Canvas240& cv, int cx, int cy, int r, Rgb c, int alpha) {
  stroke(cv, cx - r, cy, cx + r, cy, 1, kInk, alpha);
  stroke(cv, cx, cy - r, cx, cy + r, 1, kInk, alpha);
  const uint16_t k = c565(c);
  for (int d = -r + 1; d < r; ++d) {
    put(cv, cx + d, cy, k, alpha);
    put(cv, cx, cy + d, k, alpha);
  }
}

void pip(Canvas240& cv, const Prop& p) {
  const uint16_t ink = c565(kInk), lit = c565(kSpark);
  for (int dy = -5; dy <= 5; ++dy)
    for (int dx = -5; dx <= 5; ++dx) {
      const int d = iabs(dx) + iabs(dy);
      if (d > 5) continue;
      if (p.variant == 0) put(cv, p.x + dx, p.y + dy, d >= 5 ? ink : lit, 256);
      else put(cv, p.x + dx, p.y + dy, d == 4 ? lit : ink, d >= 4 ? 256 : 120);
    }
}

// A spiky comic burst with a word in it. The burst is the picture; the word
// is for whoever can read it.
// Its spikes' directions, looked up once a burst, not once a pixel a spike.
struct Spikes {
  int n, c[12], s[12];
  Spikes(int count, int rot) : n(count) {
    for (int k = 0; k < n; ++k) c[k] = cosA(rot + k * 256 / n), s[k] = sinA(rot + k * 256 / n);
  }
};
bool spiky(int dx, int dy, int R, int inner, const Spikes& sp) {
  if (dx * dx + dy * dy <= inner * inner) return true;
  const int waist = inner * 6 / 10;
  for (int k = 0; k < sp.n; ++k) {
    const int along = (dx * sp.c[k] + dy * sp.s[k]) / 4096;
    if (along <= 0 || along >= R) continue;
    const int perp = iabs(dx * sp.s[k] - dy * sp.c[k]) / 4096;
    if (perp * R <= waist * (R - along)) return true;
  }
  return false;
}

int textW(const char* s, int scale256) {
  const Font& f = kFreeSans9pt7b;
  int w = 0;
  for (const char* q = s; *q; ++q)
    if (*q >= f.first && *q <= f.last) w += f.glyphs[*q - f.first].xAdvance;
  return w * scale256 / 256;
}

// Comic lettering: the font at a whole multiple, thickened a pixel to the
// right, with a 3 px ink edge, so a word reads at arm's length.
void text(Canvas240& cv, const char* s, int cx, int cy, int scale256, Rgb c) {
  const Font& f = kFreeSans9pt7b;
  int pen = cx - textW(s, scale256) / 2;
  const int base = cy + 13 * scale256 / 512;
  const uint16_t ink = c565(kInk), k = c565(c);
  static constexpr int kD[12][2] = {{-3, 0}, {3, 0}, {0, -3}, {0, 3}, {-2, -2}, {2, 2}, {-2, 2}, {2, -2},
                                    {-1, -1}, {1, 1}, {-1, 1}, {1, -1}};
  for (const char* q = s; *q; ++q) {
    if (*q < f.first || *q > f.last) continue;
    const Glyph& g = f.glyphs[*q - f.first];
    const int W = g.width * scale256 / 256, H = g.height * scale256 / 256;
    auto lit = [&](int ox, int oy) {
      if (ox < 0 || oy < 0 || ox >= W || oy >= H) return false;
      const int sx = ox * 256 / scale256, sy = oy * 256 / scale256, bit = sy * g.width + sx;
      return ((f.bitmap[g.bitmapOffset + bit / 8] >> (7 - bit % 8)) & 1) != 0;
    };
    auto on = [&](int ox, int oy) { return lit(ox, oy) || lit(ox - 1, oy); };
    const int x0 = pen + g.xOffset * scale256 / 256, y0 = base + g.yOffset * scale256 / 256;
    for (int oy = -3; oy < H + 3; ++oy)
      for (int ox = -3; ox < W + 4; ++ox) {
        if (on(ox, oy)) { put(cv, x0 + ox, y0 + oy, k, 256); continue; }
        bool edge = false;
        for (int d = 0; d < 12 && !edge; ++d) edge = on(ox + kD[d][0], oy + kD[d][1]);
        if (edge) put(cv, x0 + ox, y0 + oy, ink, 256);
      }
    pen += g.xAdvance * scale256 / 256;
  }
}

void burst(Canvas240& cv, const Prop& p, uint16_t ticks) {
  static constexpr const char* kWords[] = {"BONK!", "VS", "SUPER!", "BOINK!", "WHAM!", "NOPE!", "OOF!", "WHIFF"};
  const int v = imin(p.variant, 7);
  // Small enough to sit between two faces: the reactions are the show. The
  // word is twice the font where it fits (VS, OOF!), else the font bold; whole
  // multiples only, since a fractional scale drops a row and an E reads as an F.
  const int R = kBurstR * p.scalePct / 100, inner = R * 65 / 100, rot = ticks * 2;
  const int kScale = textW(kWords[v], 512) <= 2 * inner + 8 ? 512 : 256;
  // A whiff is a grey puff of air, not a hit.
  const bool whiff = p.variant == 7;
  const uint16_t edge = c565(whiff ? Rgb{90, 90, 110} : kBurstEdge), body = c565(whiff ? Rgb{200, 200, 215} : kBurst);
  const Spikes spikes(whiff ? 7 : 12, rot);
  for (int y = p.y - R; y <= p.y + R; ++y)
    for (int x = p.x - R; x <= p.x + R; ++x) {
      const int dx = x - p.x, dy = y - p.y;
      if (!spiky(dx, dy, R, inner, spikes)) continue;
      put(cv, x, y, spiky(dx, dy, R - 4, inner - 4, spikes) ? body : edge, p.alpha + 1);
    }
  if (p.scalePct >= 90 && p.alpha > 200) text(cv, kWords[v], p.x, p.y, kScale, {255, 255, 255});
}

void arrow(Canvas240& cv, const Prop& p) {
  const int a = p.alpha + 1;
  const int dx = p.x2 - p.x, dy = p.y2 - p.y;
  const int len = iabs(dx) + iabs(dy);
  if (!len) return;
  const int hx = p.x2 - dx * 12 / len, hy = p.y2 - dy * 12 / len;   // the head's base, about 12 px back
  const int nx = -dy * 9 / len, ny = dx * 9 / len;
  stroke(cv, p.x, p.y, hx, hy, 5, kInk, a);
  stroke(cv, hx + nx, hy + ny, p.x2, p.y2, 5, kInk, a);
  stroke(cv, hx - nx, hy - ny, p.x2, p.y2, 5, kInk, a);
  stroke(cv, p.x, p.y, hx, hy, 3, kCream, a);
  stroke(cv, hx + nx, hy + ny, p.x2, p.y2, 3, kCream, a);
  stroke(cv, hx - nx, hy - ny, p.x2, p.y2, 3, kCream, a);
}

void tick(Canvas240& cv, const Prop& p) {
  const int k = p.scalePct;
  const int ax = p.x - 7 * k / 100, ay = p.y, bx = p.x - 2 * k / 100, by = p.y + 6 * k / 100, cx = p.x + 8 * k / 100,
            cy = p.y - 7 * k / 100;
  disc(cv, p.x, p.y, 13 * k / 100, kInk, 200);
  stroke(cv, ax, ay, bx, by, 4, kInk, p.alpha + 1);
  stroke(cv, bx, by, cx, cy, 4, kInk, p.alpha + 1);
  stroke(cv, ax, ay, bx, by, 2, {150, 255, 150}, p.alpha + 1);
  stroke(cv, bx, by, cx, cy, 2, {150, 255, 150}, p.alpha + 1);
}

void dots(Canvas240& cv, const Prop& p) {
  const uint16_t ink = c565(kInk), paper = c565({250, 250, 250});
  stroke(cv, p.x - 9, p.y + 8, p.x - 14, p.y + 16, 2, kInk, 256);   // the bubble's tail, toward him
  for (int y = -11; y <= 11; ++y)
    for (int x = -15; x <= 15; ++x) {
      if (x * x * 81 + y * y * 169 <= 169 * 81) put(cv, p.x + x, p.y + y, paper, 256);
      else if (x * x * 121 + y * y * 225 <= 225 * 121) put(cv, p.x + x, p.y + y, ink, 256);
    }
  for (int i = 0; i < 3; ++i) disc(cv, p.x - 7 + 7 * i, p.y - (i == p.variant ? 2 : 0), 2, kInk, 256);
}

void stars(Canvas240& cv, const Prop& p) {
  for (int k = 0; k < 3; ++k) {
    const int a = p.variant * 16 + k * 85, r = 18 * p.scalePct / 100;
    sparkle(cv, p.x + r * cosA(a) / 4096, p.y - 4 + r * sinA(a) / 8192, 4, kBurst, 256);
  }
}

void glow(Canvas240& cv, const Prop& p) {
  const uint16_t k = c565(kGlowLight);
  const int R = 30 * p.scalePct / 100;
  for (int y = -R; y <= R; ++y)
    for (int x = -R; x <= R; ++x) {
      const int d2 = x * x + y * y;
      if (d2 <= R * R) put(cv, p.x + x, p.y + y, k, p.alpha * (R * R - d2) / (R * R));
    }
}

// Each power's colour, for the light a special gathers (by PowerGlyph).
constexpr Rgb kPowerLight[9] = {{64, 236, 214}, {210, 160, 90}, {190, 120, 240}, {110, 230, 90}, {255, 220, 90},
                                {80, 170, 255}, {255, 245, 90}, {235, 235, 255}, {190, 200, 210}};

void aura(Canvas240& cv, const Prop& p) {
  const uint16_t c = c565(kPowerLight[p.variant % 9]);
  const int R = 46 * p.scalePct / 100;
  for (int y = -R; y <= R; ++y)
    for (int x = -R; x <= R; ++x) {
      const int d2 = x * x + y * y * 3 / 2;
      if (d2 >= R * R) continue;
      // Brightest a little inside its edge, so it reads as light round him, not a disc.
      const int t = d2 * 256 / (R * R), lit = t < 160 ? t * 256 / 160 : (256 - t) * 256 / 96;
      put(cv, p.x + x, p.y + y, c, p.alpha * lit / 256);
    }
}

void backed(Canvas240& cv, const Prop& p, int rPx) { disc(cv, p.x, p.y, rPx * p.scalePct / 100, kInk, 150 * (p.alpha + 1) / 256); }

void art(Canvas240& cv, const FrameRef& f, const Prop& p) { drawArt(cv, f, p.x, p.y, p.scalePct, p.alpha + 1); }

void paintProp(Canvas240& cv, const Prop& p, uint16_t ticks) {
  if (p.scalePct < 8) return;
  switch (p.kind) {
    case PropKind::Heart: return;   // the gauges draw hearts now (hud.cpp)
    case PropKind::Pip: return pip(cv, p);
    case PropKind::Stance:
      backed(cv, p, 15);
      // At twice its size or more the 48 px twin, at half the scale: sharp, not blocks.
      if (p.scalePct >= 200)
        return drawArt(cv, duel_art::kStancesBig[p.variant % 3], p.x, p.y, p.scalePct / 2, p.alpha + 1);
      return art(cv, duel_art::kStances[p.variant % 3], p);
    case PropKind::Power: backed(cv, p, 14); return art(cv, duel_art::kPowers[p.variant % 9], p);
    case PropKind::Tier:
      backed(cv, p, 15);
      if (p.variant == 2 && p.fill < 255) {
        drawArt(cv, duel_art::kSkullEmpty, p.x, p.y, p.scalePct, p.alpha + 1);
        return drawArt(cv, duel_art::kTiers[2], p.x, p.y, p.scalePct, p.alpha + 1, p.fill);
      }
      return art(cv, duel_art::kTiers[p.variant % 3], p);
    case PropKind::Element: backed(cv, p, 14); return art(cv, duel_art::kElements[p.variant % 3], p);
    case PropKind::Trophy: return art(cv, duel_art::kTrophies[p.variant % 9], p);
    case PropKind::Debt: return art(cv, duel_art::kDebt, p);
    case PropKind::Plaster: return art(cv, duel_art::kWounded, p);
    case PropKind::Burst: return burst(cv, p, ticks);
    case PropKind::Arrow: return arrow(cv, p);
    case PropKind::Tick: return tick(cv, p);
    case PropKind::Dots: return dots(cv, p);
    case PropKind::Strike: {
      const int r = 15 * p.scalePct / 100;
      return inked(cv, p.x - r, p.y + r, p.x + r, p.y - r, 3, kStrike, 256);
    }
    case PropKind::Timer: return hud::clock(cv, p.fill);
    case PropKind::Stars: return stars(cv, p);
    case PropKind::Glow: return glow(cv, p);
    case PropKind::Aura: return aura(cv, p);
    case PropKind::Countdown: return hud::countdown(cv, p);
    case PropKind::Button:
      disc(cv, p.x, p.y, 13, kInk, 170);
      return bits(cv, p.variant ? kKnock : kButton, p.x, p.y, 200 * p.scalePct / 100, kCream, p.alpha + 1, false);
    case PropKind::Plea: return bits(cv, kHeart, p.x, p.y, p.scalePct, {255, 120, 150}, p.alpha + 1, false);
    case PropKind::Reticle: return hud::reticle(cv, p);
    case PropKind::Lock: return hud::lock(cv, p);
  }
}

// The arena: a dusky dish with a pool of light under each pet, so each stands
// on his own ground; ordered-dithered so the gradient does not band.
struct Pool { int x, y, rx, ry; };
constexpr Pool kPools[2] = {{kHome[0].x, kHome[0].y, 50, 13}, {kHome[1].x, kHome[1].y, 36, 9}};
constexpr uint8_t kArenaBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// One arena pixel, from scratch: the gradient, the pools' light, the dither.
constexpr uint16_t arenaPixel(int x, int y) {
  const int dx = 2 * x + 1 - kSide, dy = 2 * y + 1 - kSide, d2 = dx * dx + dy * dy;
  const int t = d2 / (kSide * kSide / 256);
  int r = 40 - 22 * t / 256, g = 30 - 16 * t / 256, b = 56 - 26 * t / 256;
  for (const Pool& p : kPools) {
    // A whole radius off on either axis puts |ex| or |ey| at 256 or more: no light, and no divisions to learn it.
    if (iabs(x - p.x) >= p.rx || iabs(y - p.y) >= p.ry) continue;
    const int ex = (x - p.x) * 256 / p.rx, ey = (y - p.y) * 256 / p.ry, e = (ex * ex + ey * ey) >> 8;
    if (e < 256) { r += 26 * (256 - e) / 256; g += 30 * (256 - e) / 256; b += 24 * (256 - e) / 256; }
  }
  const int d = kArenaBayer[y & 3][x & 3];
  r = imin(255, r + d / 2), g = imin(255, g + d / 4), b = imin(255, b + d / 2);
  return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Away from the pools an arena pixel is its step of t (d2 / 225, 0..256) and
// its dither alone: the 257 steps x 16 dithers, at compile time (8 KB of flash).
struct ArenaLut { uint16_t px[4][257][4]; };   // [dither row][t][dither column]
constexpr ArenaLut arenaLut() {
  ArenaLut l{};
  for (int by = 0; by < 4; ++by)
    for (int t = 0; t <= 256; ++t)
      for (int k = 0; k < 4; ++k) {
        int r = 40 - 22 * t / 256, g = 30 - 16 * t / 256, b = 56 - 26 * t / 256;
        const int d = kArenaBayer[by][k];
        r = imin(255, r + d / 2), g = imin(255, g + d / 4), b = imin(255, b + d / 2);
        l.px[by][t][k] = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      }
  return l;
}
constexpr ArenaLut kArenaLut = arenaLut();
// d2 / 225 as a multiply and a shift, for every d2 of the dish (checked).
constexpr uint32_t arenaT(uint32_t d2) { return d2 * 37283u >> 23; }
constexpr bool arenaTExact() {
  for (uint32_t d2 = 0; d2 <= uint32_t(kSide * kSide); ++d2)
    if (arenaT(d2) != d2 / 225) return false;
  return true;
}
static_assert(arenaTExact(), "arenaT is d2 / 225 across the dish");

// The pools never move, so their boxes are pixels at compile time too (7 KB of flash).
template <int P>
struct PoolBox {
  static constexpr int kW = 2 * kPools[P].rx - 1, kH = 2 * kPools[P].ry - 1;
  uint16_t px[kH][kW];
};
template <int P>
constexpr PoolBox<P> poolBox() {
  PoolBox<P> b{};
  for (int r = 0; r < PoolBox<P>::kH; ++r)
    for (int c = 0; c < PoolBox<P>::kW; ++c)
      b.px[r][c] = arenaPixel(kPools[P].x - kPools[P].rx + 1 + c, kPools[P].y - kPools[P].ry + 1 + r);
  return b;
}
constexpr PoolBox<0> kPool0 = poolBox<0>();
constexpr PoolBox<1> kPool1 = poolBox<1>();

// The arena: a dusky dish with a pool of light under each pet, so each stands
// on his own ground; ordered-dithered so the gradient does not band. Each row
// from the table, a pixel and its mirror at once (the gradient is symmetric),
// then the pools' boxes worked out pixel by pixel over it.
void arena(Canvas240& cv) {
  for (int y = 0; y < kSide; ++y) {
    uint16_t* row = &cv.px[y * kSide];
    const int from = kDish.from[y];
    std::memset(row, 0, sizeof(uint16_t) * size_t(from));
    std::memset(row + kSide - from, 0, sizeof(uint16_t) * size_t(from));
    const int dy = 2 * y + 1 - kSide;
    const auto& lut = kArenaLut.px[y & 3];
    uint32_t d2 = uint32_t((2 * from + 1 - kSide) * (2 * from + 1 - kSide) + dy * dy);
    uint32_t step = uint32_t(4 * (2 * from + 1 - kSide) + 4);   // d2 at x + 1 less d2 at x, a 2's complement step
    for (int x = from; x < kSide / 2; ++x, d2 += step, step += 8) {
      const uint16_t* l = lut[arenaT(d2)];
      row[x] = l[x & 3];
      row[kSide - 1 - x] = l[3 - (x & 3)];
    }
    auto pool = [&](const Pool& p, const uint16_t* box, int w) {
      if (iabs(y - p.y) >= p.ry) return;
      const int x0 = p.x - p.rx + 1, a = imax(from, x0), b = imin(kSide - 1 - from, p.x + p.rx - 1);
      if (a <= b) std::memcpy(row + a, box + (y - (p.y - p.ry + 1)) * w + (a - x0), sizeof(uint16_t) * size_t(b - a + 1));
    };
    pool(kPools[0], &kPool0.px[0][0], PoolBox<0>::kW);
    pool(kPools[1], &kPool1.px[0][0], PoolBox<1>::kW);
  }
}

// Light and the clock go under the pets; everything else over them.
bool behind(PropKind k) { return k == PropKind::Timer || k == PropKind::Glow || k == PropKind::Aura; }

// His look for the duel: breathing on the duel's clock in his idle pose; the
// power face glows only while his power fires.
Appearance lookFor(const Fighter& f, const Figure& g, const DuelScene& sc) {
  Appearance a = f.look;
  // His own clock run on by the act's, 1/256s and all; both breathe on the
  // screen's clock when mine is drawn lively (paint/tween.h), each at his own tempo.
  const uint32_t sub = uint32_t(a.poseSub) + (sc.sub & 255);
  a.poseTick = uint16_t(a.poseTick + sc.ticks + (sub >> 8));
  a.poseSub = uint16_t(sub & 255);
  a.lively = sc.me.look.lively;
  a.liveMs = sc.me.look.liveMs;
  a.reflexActive = false;
  a.pose = blorb::pose::idle;
  const bool powered = g.hasFace && g.face == expr::foresee;
  a.foreseeing = powered;
  a.glow = powered ? blorb::Fx::ratio(8, 10) : blorb::Fx::zero();
  a.asleep = g.hasFace && g.face == expr::asleep;
  return a;
}

}  // namespace

namespace {

// boot.h's BootGlyph values that ART4 leaves to ART3 (their kBootGlyphs slots are empty).
constexpr uint8_t kGlyphTier = 12, kGlyphSkull = 13, kGlyphQuick = 14, kGlyphHeavy = 15, kGlyphCountdown = 21;
constexpr int kBootGlyphCount = int(sizeof(duel_art::kBootGlyphs) / sizeof(duel_art::kBootGlyphs[0]));
static_assert(kBootGlyphCount == kGlyphCountdown + 1, "kBootGlyphs is indexed by boot.h's BootGlyph");

// Beside the BOOT button on the rim, clear of both heart arcs and above the
// home screen's marquee strip (rows 34 to 53), which scrolls under nothing.
constexpr P kBootAt = {146, 18};
constexpr Rgb kDoubleGlow{255, 205, 60};
constexpr int kHintPct = 125;

}  // namespace

void drawPickerProp(Canvas240& cv, const Prop& p) { paintProp(cv, p, 0); }

RimHint rimHint(const blorb::BootHint& b) {
  RimHint r;
  r.glyph = uint8_t(b.glyph);
  r.art = b.art;
  r.greyed = b.greyed;
  r.pips = b.pips;
  r.hasHold = b.hasHold;
  r.holdFill = b.holdFill;
  r.hasDouble = b.hasDouble;
  r.doubleFill = b.doubleFill;
  r.holdVoid = b.holdVoid;
  r.knockWait = b.knockWait;
  return r;
}

namespace {

void drawBootHintArt(Canvas240& cv, const RimHint& h) {
  if (!h.glyph || h.glyph >= kBootGlyphCount) return;
  const P at = kBootAt;
  // Something waits for its second, so a kid sees "once more" without a
  // word: a warm glow behind the glyph. A first knock of a knock knock (the
  // press's twin) glows steadily while its window is open. A first BOOT press,
  // or a hang on its way up (the double's twin), glows with the big double
  // bars below, at doubleFill.
  const bool doubleLit = h.hasDouble && h.doubleFill;
  if (doubleLit || h.knockWait) {
    const int glow = h.knockWait ? imax(h.doubleFill, 160) : h.doubleFill;
    disc(cv, at.x, at.y, 21, kDoubleGlow, 80 + glow * 176 / 255);
  }
  disc(cv, at.x, at.y, 15, kInk, 190);
  const int alpha = h.greyed ? 150 : 256;
  if (h.glyph == kGlyphTier) drawArt(cv, duel_art::kTiers[h.art % 3], at.x, at.y, 70, alpha);
  else if (h.glyph == kGlyphSkull) drawArt(cv, duel_art::kTiers[2], at.x, at.y, 70, alpha);
  else if (h.glyph == kGlyphQuick || h.glyph == kGlyphHeavy) drawArt(cv, duel_art::kPowers[h.art % 9], at.x, at.y, 70, alpha);
  else drawArt(cv, (h.greyed ? duel_art::kBootGlyphsGrey : duel_art::kBootGlyphs)[h.glyph], at.x, at.y, kHintPct, 256);
  // A hold that means something else: the ring round it, filling clockwise from the top.
  if (h.hasHold) drawArt(cv, duel_art::kHoldRing[h.holdFill * 8 / 255], at.x, at.y, kHintPct, 256);
  // A hold felt where a hold does nothing: the same ring, faded. The grip
  // was felt, and it does nothing here.
  else if (h.holdVoid) drawArt(cv, duel_art::kHoldRing[h.holdFill * 8 / 255], at.x, at.y, kHintPct, 150);
  // The double-press bars sit beside it, toward the middle: under it is the marquee's strip.
  if (h.hasDouble) drawArt(cv, duel_art::kDoublePress, at.x - (doubleLit ? 30 : 24), at.y + 2, doubleLit ? 200 : kHintPct, 256);
  // Pips toward the middle of the dish: how far short a greyed one is, or the turns a wind-up has left.
  if (h.greyed || h.glyph == kGlyphCountdown)
    for (int i = 0; i < imin(h.pips, 6); ++i) {
      Prop q = prop(PropKind::Pip, 0, {at.x - (h.hasDouble ? 40 : 22) - 9 * i, at.y + 6});
      q.variant = h.greyed ? 1 : 0;
      pip(cv, q);
    }
}

}  // namespace

void drawBootHint(Canvas240& cv, const RimHint& h) {
  phaseStart();
  drawBootHintArt(cv, h);
  phaseEnd(Phase::Hint);
}

void drawStaged(const DuelScene& sc, const DuelStaging& s, const SpritePack& mine, const SpritePack& theirs, Canvas240& cv) {
  phaseStart();
  arena(cv);
  phaseEnd(Phase::Background);
  // The clock goes under everything (the infographics are the rim phase).
  for (int i = 0; i < s.propCount; ++i)
    if (s.props[i].kind == PropKind::Timer) paintProp(cv, s.props[i], sc.ticks);
  phaseEnd(Phase::Rim);
  for (int i = 0; i < s.propCount; ++i)
    if (behind(s.props[i].kind) && s.props[i].kind != PropKind::Timer) paintProp(cv, s.props[i], sc.ticks);
  // The friend is across the dish, so behind; mine is in front.
  drawFigure(lookFor(sc.them, s.fig[kThem], sc), theirs, cv, s.fig[kThem]);
  drawFigure(lookFor(sc.me, s.fig[kMe], sc), mine, cv, s.fig[kMe]);
  phaseEnd(Phase::Figure);
  // The gauges over the pets, the props over the gauges.
  for (int side = 0; side < 2; ++side) hud::gauges(cv, s.gauge[side], side);
  phaseEnd(Phase::Rim);
  for (int i = 0; i < s.propCount; ++i) {
    if (behind(s.props[i].kind)) continue;
    gKeepOut = s.props[i].kind == PropKind::Burst ? s.face : nullptr;
    paintProp(cv, s.props[i], sc.ticks);
    gKeepOut = nullptr;
  }
  phaseEnd(Phase::Figure);
  drawBootHint(cv, sc.boot);
  phaseStart();
  for (int y = 0; y < kSide; ++y) {
    const int from = kDish.from[y];
    std::memset(&cv.px[y * kSide], 0, sizeof(uint16_t) * size_t(from));
    std::memset(&cv.px[y * kSide + kSide - from], 0, sizeof(uint16_t) * size_t(from));
  }
  phaseEnd(Phase::Mask);
}

void drawDuel(const DuelScene& sc, const SpritePack& mine, const SpritePack& theirs, Canvas240& cv, uint32_t) {
  drawStaged(sc, stageDuel(sc, mine, theirs), mine, theirs, cv);
}

}  // namespace paint
