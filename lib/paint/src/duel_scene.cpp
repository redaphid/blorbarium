#include <cstring>
#include "blorb/registry.h"
#include "duel/duel_art.h"
#include "figure.h"
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

int imin(int a, int b) { return a < b ? a : b; }
int imax(int a, int b) { return a > b ? a : b; }
int iabs(int a) { return a < 0 ? -a : a; }
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

// 0..256 through [t0, t1).
int ramp(int t, int t0, int t1) { return t <= t0 ? 0 : (t >= t1 ? 256 : (t - t0) * 256 / (t1 - t0)); }
// 0 -> 256 -> 0 through [t0, t1), peaking at the middle.
int bump(int t, int t0, int t1) {
  if (t <= t0 || t >= t1) return 0;
  const int m = (t0 + t1) / 2;
  return t < m ? ramp(t, t0, m) : 256 - ramp(t, m, t1);
}

// ---- the layout: each a one-line change ------------------------------------------------

// Where each pet stands at rest, how big (of his size at home), and how far he
// leans toward the other. Mine is near, so big; the friend's is across the dish.
struct Home { int x, y, scalePct, leanPct; };
constexpr Home kHome[2] = {{84, 208, 54, 8}, {162, 106, 42, -8}};

// The rim arcs, in 1/256 turn: my hearts run down the lower right, the
// friend's up the upper left, the same arc turned half round.
constexpr int kHeartArc = 20, kHeartSpan = 44, kHeartStep = 10, kHeartR = 104;
constexpr int kPipArc = 24, kPipStep = 7, kPipR = 86;
constexpr int kPowerAngle = 40, kPowerR = 62;
constexpr int kFlip = 128;   // the friend's side of the disc

// The three places a tilt points: left, right, toward you.
constexpr int kTiltR = 92;
constexpr P kLeft = {kSide / 2 - kTiltR, kSide / 2}, kRight = {kSide / 2 + kTiltR, kSide / 2},
            kNear = {kSide / 2, kSide / 2 + kTiltR};
constexpr P kStanceAt[3] = {kLeft, kRight, kNear};   // Pounce, Guard, Trick
constexpr P kTierAt[3] = {kLeft, kNear, kRight};     // Spar, Scrap, Mortal

constexpr int kGlyphPct = 125, kAimedPct = 170;
constexpr int kTimerFillOf = 255;

constexpr uint16_t kClashTicks = 30;   // the reveal takes 3 s
constexpr int kPop = 0, kArrowAt = 6, kMoveAt = 8, kImpact = 12, kSettle = 22;

// ---- staging ----------------------------------------------------------------------------

struct Body { int top, h, w; };   // px on screen at the home scale

Body bodyOf(const Fighter& f, const SpritePack& pack, int scalePct) {
  const FrameRef b = pack.body(blorb::pose::idle, f.look.stage, 0);
  const bool small = f.look.stage == blorb::Stage::Baby || f.look.stage == blorb::Stage::Child;
  const int k = f.look.scalePct * scalePct / 100;   // percent
  const int ky = small ? k * 90 / 100 : k, kx = small ? k * 116 / 100 : k;
  return {b.originY * ky / 100, b.h * ky / 100, b.w * kx / 100};
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

Figure atHome(int side) {
  Figure f;
  f.x = int16_t(kHome[side].x);
  f.y = int16_t(kHome[side].y);
  f.scalePct = uint8_t(kHome[side].scalePct);
  f.leanPct = int8_t(kHome[side].leanPct);
  return f;
}
void face(Figure& f, blorb::ExprId e) { f.hasFace = true; f.face = e; }

// Hearts down his arc: full, then the ones he has lost hollow. His assist
// hearts are the last of his maximum and carry his cheer.
void hearts(DuelStaging& s, const Fighter& f, int side, int blinkLost, int t) {
  const int n = imin(f.maxHearts, 8);
  const int step = n > 1 ? imin(kHeartStep, kHeartSpan / (n - 1)) : 0;
  for (int i = 0; i < n; ++i) {
    const P at = rim(kHeartArc + i * step + side * kFlip, kHeartR);
    const bool lost = i >= f.hearts;
    const bool assist = i >= n - f.assist;
    int variant = lost ? 1 : (assist ? 2 : 0);
    int alpha = 255;
    // A heart lost this turn blinks between full and hollow before it settles.
    if (lost && i < f.hearts + blinkLost && (t / 2) % 2 == 0) variant = assist ? 2 : 0;
    add(s, prop(PropKind::Heart, variant, at, 100, alpha));
  }
}

void meter(DuelStaging& s, const Fighter& f, int side, int t) {
  for (int i = 0; i < blorb::kMeterMax; ++i)
    add(s, prop(PropKind::Pip, i < f.sparks ? 0 : 1, rim(kPipArc + i * kPipStep + side * kFlip, kPipR)));
  const blorb::PowerInfo& pw = blorb::powerInfo(blorb::speciesInfo(f.look.species).power);
  const bool ready = f.sparks >= pw.cost;
  // Not yet: small and dim. Ready: full size and breathing. Armed: big, over his head (staged by the caller).
  const int pulse = ready ? 100 + bump(t % 10, 0, 10) * 15 / 256 : 70;
  add(s, prop(PropKind::Power, int(pw.glyph), rim(kPowerAngle + side * kFlip, kPowerR), pulse, ready ? 255 : 120));
}

P headOf(const Figure& f, const Body& b) { return {f.x, f.y - b.top - 6}; }
// Where a pet's stance pops in the clash: beside his head on his outer side,
// so the clash itself has the middle of the dish.
P popAt(P head, int side) { return {head.x + (side ? 28 : -38), head.y + (side ? 18 : 14)}; }

void meeting(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  // Each slides in from his own corner and lands with a bounce.
  const int in = ramp(t, 0, 8);
  const P from[2] = {{-50, 270}, {290, -30}};
  for (int side = 0; side < 2; ++side) {
    Figure& f = s.fig[side];
    f.x = int16_t(from[side].x + (f.x - from[side].x) * in / 256);
    f.y = int16_t(from[side].y + (f.y - from[side].y) * in / 256);
    f.lift = int8_t(bump(t, 6, 12) * 10 / 256);
    face(f, t < 8 ? expr::alarmed : expr::happy);
  }
  if (t >= 8) {
    add(s, prop(PropKind::Burst, 1, {124, 114}, 64 + ramp(t, 8, 11) * 40 / 256 - bump(t, 10, 14) * 10 / 256));
    const blorb::Element e[2] = {blorb::speciesInfo(sc.me.look.species).element,
                                 blorb::speciesInfo(sc.them.look.species).element};
    for (int side = 0; side < 2; ++side)
      add(s, prop(PropKind::Element, int(e[side]), rim(kPowerAngle + side * kFlip, kPowerR), 100, ramp(t, 10, 14)));
  }
  (void)b;
}

void staking(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
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
    if (aimed && sc.me.confirmed) add(s, prop(PropKind::Tick, 0, {at.x + 14, at.y - 14}, 100));
  }
  // Each pet wears the risk of the tier his kid aims at: a feather (play), a
  // plaster on his head (wounds), or a skull over him filled to his forecast.
  for (int side = 0; side < 2; ++side) {
    const Fighter& x = *f[side];
    const P head = headOf(s.fig[side], b[side]);
    const int bob = bump(t % 16, 0, 16) * 3 / 256;
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
    if (side == kThem && x.confirmed) add(s, prop(PropKind::Tick, 0, {head.x + 26, head.y + 12}, 90));
  }
  // Both confirmed: the agreed tier, the lower of the two, in the middle.
  if (sc.me.confirmed && sc.them.confirmed)
    add(s, prop(PropKind::Tier, int(sc.stakes), {120, 128}, 150 + bump(t % 20, 0, 20) * 20 / 256));
}

void choosing(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  // The rim ring is the clock: it runs out round the glass.
  add(s, prop(PropKind::Timer, 0, {120, 120}, 100, 255,
              sc.turnTicks ? sc.ticksLeft * kTimerFillOf / sc.turnTicks : 0));
  for (int st = 0; st < 3; ++st) {
    const bool aimed = st == sc.aim;
    const P at = {kStanceAt[st].x + (aimed ? (kSide / 2 - kStanceAt[st].x) / 6 : 0),
                  kStanceAt[st].y + (aimed ? (kSide / 2 - kStanceAt[st].y) / 6 : 0)};
    // The lean: my pet's pick, a soft light under it, brighter with the assist.
    if (st == sc.lean && !sc.me.locked)
      add(s, prop(PropKind::Glow, 0, at, 100, 90 + sc.whisper * 165 / 255 * (192 + bump(t % 12, 0, 12) / 4) / 256));
    add(s, prop(PropKind::Stance, st, at, aimed ? kAimedPct : kGlyphPct, aimed || sc.aim < 0 ? 255 : 150));
    if (aimed && sc.me.locked) add(s, prop(PropKind::Tick, 0, {at.x + 16, at.y - 16}, 110));
  }
  // My pet leans toward the stance under the tilt, his kid's hand in his body.
  if (sc.aim >= 0) {
    const P to = kStanceAt[sc.aim];
    s.fig[kMe].x = int16_t(s.fig[kMe].x + (to.x - 120) / 12);
    s.fig[kMe].leanPct = int8_t(clampi(s.fig[kMe].leanPct + (to.x - 120) / 6, -24, 24));
  }
  if (sc.me.armed) {
    const blorb::PowerInfo& pw = blorb::powerInfo(blorb::speciesInfo(sc.me.look.species).power);
    const P head = headOf(s.fig[kMe], b[kMe]);
    add(s, prop(PropKind::Power, int(pw.glyph), {head.x, head.y - 14 - bump(t % 10, 0, 10) * 4 / 256}, 130));
  }
  // Over the friend: "..." while choosing, a tick once locked; never the stance.
  const P head = headOf(s.fig[kThem], b[kThem]);
  if (sc.them.locked) add(s, prop(PropKind::Tick, 0, {head.x + 20, head.y - 6}, 110));
  else add(s, prop(PropKind::Dots, (t / 4) % 3, {head.x + 22, head.y - 10}));
}

// A pet plays his own stance: a Pounce lunges at the other, a Guard curls up
// behind his shield, a Trick sidesteps and darts in from the side.
void play(Figure& f, int stance, P toward, int t) {
  const int dx = toward.x - f.x, dy = toward.y - f.y;
  if (stance == int(Stance::Pounce)) {
    const int go = ramp(t, kMoveAt, kImpact) - ramp(t, kImpact + 2, kSettle);
    f.x = int16_t(f.x + dx * 40 / 100 * go / 256);
    f.y = int16_t(f.y + dy * 40 / 100 * go / 256);
    f.widePct = uint8_t(100 - 10 * go / 256);
    f.tallPct = uint8_t(100 + 12 * go / 256);
    f.lift = int8_t(10 * bump(t, kMoveAt, kImpact + 2) / 256);
  } else if (stance == int(Stance::Guard)) {
    const int curl = ramp(t, kMoveAt, kMoveAt + 3) - ramp(t, kImpact + 4, kSettle);
    f.widePct = uint8_t(100 + 18 * curl / 256);
    f.tallPct = uint8_t(100 - 18 * curl / 256);
  } else if (stance == int(Stance::Trick)) {
    const int side = ramp(t, kMoveAt, kMoveAt + 2) - ramp(t, kImpact + 2, kSettle);
    const int dart = bump(t, kMoveAt + 2, kImpact + 3);
    f.x = int16_t(f.x - dy * 22 / 100 * side / 256 + dx * 25 / 100 * dart / 256);
    f.y = int16_t(f.y + dx * 22 / 100 * side / 256 + dy * 25 / 100 * dart / 256);
    f.leanPct = int8_t(f.leanPct + (dx > 0 ? 14 : -14) * side / 256);
  }
}

void clash(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  const Fighter* f[2] = {&sc.me, &sc.them};
  const P home[2] = {{s.fig[kMe].x, s.fig[kMe].y}, {s.fig[kThem].x, s.fig[kThem].y}};
  // Where they meet: between my head and the friend's middle.
  const P mid = {(home[kMe].x + home[kThem].x) / 2, (home[kMe].y - b[kMe].h * 2 / 3 + home[kThem].y - b[kThem].h / 2) / 2};
  for (int side = 0; side < 2; ++side) {
    Figure& g = s.fig[side];
    const Fighter& x = *f[side];
    const P other = home[1 - side];
    if (x.stance < 0) face(g, expr::asleep);   // dozed through it
    else play(g, x.stance, other, t);
    // His stance pops over his head, a power glyph with it when it fired.
    const P head = headOf(Figure{int16_t(home[side].x), int16_t(home[side].y)}, b[side]);
    const int pop = imin(ramp(t, kPop, kPop + 4) * 140 / 256, 140 - ramp(t, kPop + 4, kPop + 6) * 20 / 256);
    if (x.stance >= 0) add(s, prop(PropKind::Stance, x.stance, popAt(head, side), pop));
    if (x.fired) {
      const blorb::PowerInfo& pw = blorb::powerInfo(blorb::speciesInfo(x.look.species).power);
      add(s, prop(PropKind::Power, int(pw.glyph), {head.x, head.y - 16}, 100 + bump(t, 0, 10) * 40 / 256, 255 - ramp(t, 16, 24) * 255 / 256));
      if (t < kImpact) face(g, expr::foresee);
    }
  }
  const int win = sc.won == Side::Me ? kMe : (sc.won == Side::Them ? kThem : -1);
  // The "beats" arrow, from the winner's stance glyph toward the loser's.
  if (win >= 0 && t >= kArrowAt && t < kSettle + 4) {
    const P a = popAt(headOf(Figure{int16_t(home[win].x), int16_t(home[win].y)}, b[win]), win);
    const P z = popAt(headOf(Figure{int16_t(home[1 - win].x), int16_t(home[1 - win].y)}, b[1 - win]), 1 - win);
    Prop arrow = prop(PropKind::Arrow, 0, {a.x + (z.x - a.x) / 4, a.y + (z.y - a.y) / 4}, 100, ramp(t, kArrowAt, kArrowAt + 2));
    arrow.x2 = int16_t(z.x - (z.x - a.x) / 4);
    arrow.y2 = int16_t(z.y - (z.y - a.y) / 4);
    add(s, arrow);
  }
  if (t >= kImpact && t < kSettle + 4) {
    const int grow = 60 + ramp(t, kImpact, kImpact + 3) * 60 / 256;
    if (win < 0) {
      add(s, prop(PropKind::Burst, 3, mid, grow - 10));
      for (int side = 0; side < 2; ++side) add(s, prop(PropKind::Stars, t % 16, headOf(s.fig[side], b[side]), side ? 80 : 100));
    } else {
      const int lose = 1 - win;
      // The burst lands where they meet, a little toward the one it hurt.
      const P loser = {home[lose].x, home[lose].y - b[lose].h / 2};
      add(s, prop(PropKind::Burst, sc.super ? 2 : 0, {mid.x + (loser.x - mid.x) / 5, mid.y + (loser.y - mid.y) / 5}, grow));
      if (sc.super) {
        const blorb::Element e = blorb::speciesInfo(f[win]->look.species).element;
        add(s, prop(PropKind::Element, int(e), {mid.x - 46, mid.y - 30}, 120));
      }
    }
  }
  if (win >= 0) {
    const int lose = 1 - win;
    Figure& g = s.fig[lose];
    if (f[lose]->hit) {
      // Struck: a white flash, knocked back away from the winner, and the face says it.
      g.flash = uint8_t(255 - ramp(t, kImpact, kImpact + 6) * 255 / 256);
      if (t < kImpact) g.flash = 0;
      const int back = bump(t, kImpact, kSettle) * 14 / 256;
      g.x = int16_t(g.x + (home[lose].x - home[win].x) * back / 120);
      g.y = int16_t(g.y + (home[lose].y - home[win].y) * back / 120);
      if (t >= kImpact) face(g, expr::alarmed);
      // The hearts he lost fly up off his arc.
      for (int i = 0; i < f[lose]->hit && t >= kImpact; ++i) {
        const Fighter& x = *f[lose];
        const int n = imin(x.maxHearts, 8), step = n > 1 ? imin(kHeartStep, kHeartSpan / (n - 1)) : 0;
        const P at = rim(kHeartArc + (x.hearts + i) * step + lose * kFlip, kHeartR);
        const int up = ramp(t, kImpact, kImpact + 12);
        add(s, prop(PropKind::Plea, 0, {at.x + (120 - at.x) * up / 1024, at.y - 30 * up / 256}, 140, 255 - up * 255 / 256));
      }
    }
    if (t >= kImpact + 2) face(s.fig[win], expr::happy);
  }
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
  const int win = sc.won == Side::Me ? kMe : kThem, down = 1 - win;
  slump(s.fig[down], s.fig[down].x - s.fig[win].x);
  add(s, prop(PropKind::Stars, t % 16, headOf(s.fig[down], Body{b[down].top * 64 / 100, b[down].h, b[down].w}), down ? 80 : 100));
  // The winner stands over him: a little bigger, leaning in, grim once the thumb is down.
  s.fig[win].scalePct = uint8_t(s.fig[win].scalePct + 6);
  face(s.fig[win], sc.finishHold ? expr::annoyed : expr::neutral);
  add(s, prop(PropKind::Timer, 0, {120, 120}, 100, 255, sc.mercyTicks ? sc.mercyLeft * kTimerFillOf / sc.mercyTicks : 0));
  // The skull over the downed pet fills from the bottom while the winner holds BOOT.
  add(s, prop(PropKind::Tier, 2, kMercySkull[down], 190, 255, imax(sc.finishHold, 1)));
  if (sc.won == Side::Me) {
    // Mine to decide, in the empty lower right: hold BOOT for the skull, or knock for the heart.
    add(s, prop(PropKind::Button, 0, {150, 164}, 100, 230));
    add(s, prop(PropKind::Tier, 2, {178, 164}, 75));
    add(s, prop(PropKind::Button, 1, {150, 198}, 100, 230));
    add(s, prop(PropKind::Plea, 0, {178, 198}, 130));
  }
  // The downed pet's kid can only plead: a heart floats up toward the winner.
  if (sc.pleading) {
    const int up = (t * 256 / 20) % 256;
    const P from = headOf(s.fig[down], b[down]), to = headOf(s.fig[win], b[win]);
    add(s, prop(PropKind::Plea, 0, {from.x + (to.x - from.x) * up / 256, from.y + (to.y - from.y) * up / 256 - 10}, 130,
                255 - up * 200 / 256));
  }
}

void fallen(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  const int win = sc.won == Side::Me ? kMe : kThem, down = 1 - win;
  face(s.fig[win], expr::happy);
  if (sc.verdict == Verdict::Finish) {
    // He is gone: his body fades to his remains, the full skull fades over him.
    Figure& g = s.fig[down];
    slump(g, g.x - s.fig[win].x);
    g.remainsIn = uint8_t(ramp(t, 2, 14) * 255 / 256);
    const P head = headOf(s.fig[down], b[down]);
    add(s, prop(PropKind::Tier, 2, {(head.x + 120) / 2, imax(52, head.y - 22) - ramp(t, 0, 20) * 20 / 256}, 200,
                255 - ramp(t, 10, 24) * 255 / 256));
    // The victor's kid takes the fallen's token: it floats to the victor's rim.
    const int fly = ramp(t, 14, 30);
    const P from = {s.fig[down].x, s.fig[down].y - b[down].h / 2}, to = rim(kHeartArc + 4 + win * kFlip, 82);
    if (t >= 14)
      add(s, prop(PropKind::Trophy, int(f(sc, down).look.species.v),
                  {from.x + (to.x - from.x) * fly / 256, from.y + (to.y - from.y) * fly / 256 - bump(t, 14, 30) * 20 / 256}, 120));
  } else {
    // Spared: he sits up, still dazed, and his line owes the victor's a life.
    Figure& g = s.fig[down];
    const int up = ramp(t, 0, 10);
    g.tallPct = uint8_t(72 + 20 * up / 256);
    g.widePct = uint8_t(118 - 12 * up / 256);
    g.leanPct = int8_t((g.x < s.fig[win].x ? -20 : 20) * (256 - up) / 256);
    face(g, t < 10 ? expr::asleep : expr::sleepy);
    add(s, prop(PropKind::Debt, 0, {120, 120 - ramp(t, 4, 16) * 16 / 256}, 100 + ramp(t, 4, 10) * 60 / 256, ramp(t, 4, 8)));
  }
}

void outcome(const DuelScene& sc, DuelStaging& s, const Body* b) {
  const int t = sc.ticks;
  if (sc.won == Side::Neither) {
    for (int side = 0; side < 2; ++side) face(s.fig[side], expr::sleepy);
  } else {
    const int win = sc.won == Side::Me ? kMe : kThem, down = 1 - win;
    // The winner hops for joy; the loser has fainted, stars round his head.
    Figure& w = s.fig[win];
    const int ph = t % 10;
    w.lift = int8_t(bump(ph, 1, 9) * 16 / 256);
    w.widePct = uint8_t(ph < 1 || ph > 8 ? 112 : 100);
    w.tallPct = uint8_t(ph < 1 || ph > 8 ? 88 : 100);
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
  s.fig[kMe].scarIn = uint8_t(ramp(t, 0, 20) * 255 / 256);
}

}  // namespace

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
  // Hearts and the meter stay on the rim through the fight itself.
  const bool fighting = sc.act == DuelAct::Choosing || sc.act == DuelAct::Clash || sc.act == DuelAct::Meeting;
  if (fighting || sc.act == DuelAct::Outcome) {
    const int t = sc.ticks;
    const bool clashing = sc.act == DuelAct::Clash;
    hearts(s, sc.me, kMe, clashing && t >= kImpact && t < kSettle ? sc.me.hit : 0, t);
    hearts(s, sc.them, kThem, clashing && t >= kImpact && t < kSettle ? sc.them.hit : 0, t);
    if (fighting) {
      meter(s, sc.me, kMe, t);
      meter(s, sc.them, kThem, t);
    }
  }
  return s;
}

namespace {

// ---- painting the list ------------------------------------------------------------------

struct Rgb { int r, g, b; };
uint16_t c565(Rgb c) { return uint16_t(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3)); }

constexpr Rgb kInk{24, 14, 22};          // every prop's outline: a shape reads before its colour does
constexpr Rgb kCream{255, 244, 214};
constexpr Rgb kHeartRed{236, 52, 74}, kAssistGold{255, 200, 60}, kSpark{255, 226, 90};
constexpr Rgb kBurst{255, 225, 74}, kBurstEdge{196, 64, 16};
constexpr Rgb kStrike{200, 30, 40};
constexpr Rgb kGlowLight{255, 240, 170};

bool inDisc(int x, int y) {
  const int dx = 2 * x + 1 - kSide, dy = 2 * y + 1 - kSide;
  return dx * dx + dy * dy <= kSide * kSide;
}

void put(Canvas240& cv, int x, int y, uint16_t c, int alpha) {
  if (x < 0 || y < 0 || x >= kSide || y >= kSide || alpha <= 0) return;
  uint16_t& px = cv.px[y * kSide + x];
  px = alpha >= 256 ? c : blend(px, c, alpha);
}

void disc(Canvas240& cv, int cx, int cy, int r, Rgb c, int alpha) {
  const uint16_t k = c565(c);
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

void heart(Canvas240& cv, const Prop& p) {
  const int a = p.alpha + 1, k = 150 * p.scalePct / 100;
  if (p.variant == 1) return bits(cv, kHeart, p.x, p.y, k, {250, 170, 180}, a, true);
  bits(cv, kHeart, p.x, p.y, k, p.variant == 2 ? kAssistGold : kHeartRed, a, false);
  if (p.variant == 2) sparkle(cv, p.x + 7, p.y - 7, 4, {255, 255, 255}, a);
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
bool spiky(int dx, int dy, int R, int inner, int n, int rot) {
  if (dx * dx + dy * dy <= inner * inner) return true;
  for (int k = 0; k < n; ++k) {
    const int a = rot + k * 256 / n;
    const int along = (dx * cosA(a) + dy * sinA(a)) / 4096, perp = iabs(dx * sinA(a) - dy * cosA(a)) / 4096;
    if (along > 0 && along < R && perp * R <= inner * 6 / 10 * (R - along)) return true;
  }
  return false;
}

void text(Canvas240& cv, const char* s, int cx, int cy, int scale256, Rgb c) {
  const Font& f = kFreeSans9pt7b;
  int w = 0;
  for (const char* q = s; *q; ++q)
    if (*q >= f.first && *q <= f.last) w += f.glyphs[*q - f.first].xAdvance;
  int pen = cx - w * scale256 / 512;
  const int base = cy + 13 * scale256 / 512;
  const uint16_t ink = c565(kInk), k = c565(c);
  static constexpr int kD[8][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}, {-1, -1}, {1, 1}, {-1, 1}, {1, -1}};
  for (const char* q = s; *q; ++q) {
    if (*q < f.first || *q > f.last) continue;
    const Glyph& g = f.glyphs[*q - f.first];
    const int W = g.width * scale256 / 256, H = g.height * scale256 / 256;
    auto on = [&](int ox, int oy) {
      if (ox < 0 || oy < 0 || ox >= W || oy >= H) return false;
      const int sx = ox * 256 / scale256, sy = oy * 256 / scale256, bit = sy * g.width + sx;
      return ((f.bitmap[g.bitmapOffset + bit / 8] >> (7 - bit % 8)) & 1) != 0;
    };
    const int x0 = pen + g.xOffset * scale256 / 256, y0 = base + g.yOffset * scale256 / 256;
    for (int oy = -2; oy < H + 2; ++oy)
      for (int ox = -2; ox < W + 2; ++ox) {
        if (on(ox, oy)) { put(cv, x0 + ox, y0 + oy, k, 256); continue; }
        bool edge = false;
        for (int d = 0; d < 8 && !edge; ++d) edge = on(ox + kD[d][0], oy + kD[d][1]);
        if (edge) put(cv, x0 + ox, y0 + oy, ink, 256);
      }
    pen += g.xAdvance * scale256 / 256;
  }
}

void burst(Canvas240& cv, const Prop& p, uint16_t ticks) {
  static constexpr const char* kWords[] = {"BONK!", "VS", "SUPER!", "BOINK!"};
  // Whole multiples of the font only: a fractional scale drops a row and an E reads as an F.
  static constexpr int kScale[] = {256, 512, 256, 256};
  const int R = 46 * p.scalePct / 100, inner = 30 * p.scalePct / 100, rot = ticks * 2;
  const uint16_t edge = c565(kBurstEdge), body = c565(kBurst);
  for (int y = p.y - R; y <= p.y + R; ++y)
    for (int x = p.x - R; x <= p.x + R; ++x) {
      const int dx = x - p.x, dy = y - p.y;
      if (!spiky(dx, dy, R, inner, 12, rot)) continue;
      put(cv, x, y, spiky(dx, dy, R - 4, inner - 4, 12, rot) ? body : edge, p.alpha + 1);
    }
  const int v = imin(p.variant, 3);
  if (p.scalePct >= 90) text(cv, kWords[v], p.x, p.y, kScale[v], {255, 255, 255});
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

// The rim clock: what is left is a thick lit band, what has gone a thin dim line.
int angle256(int dx, int dy) {
  const int ax = iabs(dx), ay = iabs(dy);
  if (!ax && !ay) return 0;
  int a = ax >= ay ? 32 * ay / ax : 64 - 32 * ax / ay;
  if (dx < 0) a = 128 - a;
  if (dy < 0) a = 256 - a;
  return a & 255;
}

void timer(Canvas240& cv, const Prop& p) {
  const uint16_t lit = c565(kCream), dim = c565({90, 80, 100});
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x) {
      const int dx = 2 * x + 1 - kSide, dy = 2 * y + 1 - kSide, d2 = dx * dx + dy * dy;
      if (d2 > 236 * 236 || d2 < 222 * 222) continue;
      const bool left = ((angle256(dx, dy) - 192) & 255) * 255 / 256 < p.fill;
      if (left) put(cv, x, y, lit, 256);
      else if (d2 >= 230 * 230) put(cv, x, y, dim, 256);
    }
}

void stars(Canvas240& cv, const Prop& p) {
  for (int k = 0; k < 3; ++k) {
    const int a = p.variant * 16 + k * 85, r = 18 * p.scalePct / 100;
    sparkle(cv, p.x + r * cosA(a) / 4096, p.y - 4 + r * sinA(a) / 8192, 4, kBurst, 256);
  }
}

void glow(Canvas240& cv, const Prop& p) {
  const uint16_t k = c565(kGlowLight);
  constexpr int R = 30;
  for (int y = -R; y <= R; ++y)
    for (int x = -R; x <= R; ++x) {
      const int d2 = x * x + y * y;
      if (d2 <= R * R) put(cv, p.x + x, p.y + y, k, p.alpha * (R * R - d2) / (R * R));
    }
}

void backed(Canvas240& cv, const Prop& p, int rPx) { disc(cv, p.x, p.y, rPx * p.scalePct / 100, kInk, 150 * (p.alpha + 1) / 256); }

void art(Canvas240& cv, const FrameRef& f, const Prop& p) { drawArt(cv, f, p.x, p.y, p.scalePct, p.alpha + 1); }

void paintProp(Canvas240& cv, const Prop& p, uint16_t ticks) {
  if (p.scalePct < 8) return;
  switch (p.kind) {
    case PropKind::Heart: return heart(cv, p);
    case PropKind::Pip: return pip(cv, p);
    case PropKind::Stance: backed(cv, p, 15); return art(cv, duel_art::kStances[p.variant % 3], p);
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
    case PropKind::Timer: return timer(cv, p);
    case PropKind::Stars: return stars(cv, p);
    case PropKind::Glow: return glow(cv, p);
    case PropKind::Button:
      disc(cv, p.x, p.y, 13, kInk, 170);
      return bits(cv, p.variant ? kKnock : kButton, p.x, p.y, 200 * p.scalePct / 100, kCream, p.alpha + 1, false);
    case PropKind::Plea: return bits(cv, kHeart, p.x, p.y, p.scalePct, {255, 120, 150}, p.alpha + 1, false);
  }
}

// The arena: a dusky dish with a pool of light under each pet, so each stands
// on his own ground; ordered-dithered so the gradient does not band.
void arena(Canvas240& cv) {
  static constexpr uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  struct Pool { int x, y, rx, ry; };
  constexpr Pool kPools[2] = {{kHome[0].x, kHome[0].y, 50, 13}, {kHome[1].x, kHome[1].y, 36, 9}};
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x) {
      const int dx = 2 * x + 1 - kSide, dy = 2 * y + 1 - kSide, d2 = dx * dx + dy * dy;
      if (d2 > kSide * kSide) { cv.px[y * kSide + x] = 0; continue; }
      const int t = d2 / (kSide * kSide / 256);
      Rgb c{40 - 22 * t / 256, 30 - 16 * t / 256, 56 - 26 * t / 256};
      for (const Pool& p : kPools) {
        const int ex = (x - p.x) * 256 / p.rx, ey = (y - p.y) * 256 / p.ry, e = (ex * ex + ey * ey) >> 8;
        if (e < 256) { c.r += 26 * (256 - e) / 256; c.g += 30 * (256 - e) / 256; c.b += 24 * (256 - e) / 256; }
      }
      const int d = kBayer[y & 3][x & 3];
      cv.px[y * kSide + x] = c565({imin(255, c.r + d / 2), imin(255, c.g + d / 4), imin(255, c.b + d / 2)});
    }
}

// His look for the duel: breathing on the duel's clock in his idle pose; the
// power face glows only while his power fires.
Appearance lookFor(const Fighter& f, const Figure& g, uint16_t ticks) {
  Appearance a = f.look;
  a.poseTick = uint16_t(a.poseTick + ticks);
  a.reflexActive = false;
  a.pose = blorb::pose::idle;
  const bool powered = g.hasFace && g.face == expr::foresee;
  a.foreseeing = powered;
  a.glow = powered ? blorb::Fx::ratio(8, 10) : blorb::Fx::zero();
  a.asleep = g.hasFace && g.face == expr::asleep;
  return a;
}

}  // namespace

void drawDuel(const DuelScene& sc, const SpritePack& mine, const SpritePack& theirs, Canvas240& cv, uint32_t) {
  const DuelStaging s = stageDuel(sc, mine, theirs);
  arena(cv);
  for (int i = 0; i < s.propCount; ++i)
    if (s.props[i].kind == PropKind::Timer || s.props[i].kind == PropKind::Glow) paintProp(cv, s.props[i], sc.ticks);
  // The friend is across the dish, so behind; mine is in front.
  drawFigure(lookFor(sc.them, s.fig[kThem], sc.ticks), theirs, cv, s.fig[kThem]);
  drawFigure(lookFor(sc.me, s.fig[kMe], sc.ticks), mine, cv, s.fig[kMe]);
  for (int i = 0; i < s.propCount; ++i)
    if (s.props[i].kind != PropKind::Timer && s.props[i].kind != PropKind::Glow) paintProp(cv, s.props[i], sc.ticks);
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x)
      if (!inDisc(x, y)) cv.px[y * kSide + x] = 0;
}

}  // namespace paint
