#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "blorb/appearance.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "duel_scenes.h"
#include "paint/duel.h"
#include "paint/sprite_pack.h"
#include "../../lib/paint/src/figure.h"

using blorb::Appearance;
using blorb::SpeciesId;
using blorb::Stage;
using paint::Canvas240;
using paint::DuelScene;
using paint::DuelStaging;
using paint::FrameRef;
using paint::PropKind;
using blorbtest::duelScene;
using blorbtest::kDuelMoments;

namespace {

constexpr int kSide = 240;

SpeciesId byName(const char* name) {
  for (const blorb::SpeciesInfo& s : blorb::SPECIES)
    if (std::string(s.name) == name) return s.id;
  ADD_FAILURE() << name << " is not a species.def row";
  return blorb::kGrungo;
}

// Three pairings that between them cover every element, a pale wing, a shell and a lamp.
struct Pairing { const char* mine; const char* theirs; };
constexpr Pairing kPairings[] = {{"grungo", "fizz"}, {"gilly", "mothwick"}, {"tinker", "mudge"}};

std::unique_ptr<Canvas240> shoot(const DuelScene& s) {
  auto cv = std::make_unique<Canvas240>();
  paint::drawDuel(s, paint::packFor(s.me.look.species), paint::packFor(s.them.look.species), *cv, 0);
  return cv;
}

std::string dump(const DuelStaging& st) {
  std::ostringstream o;
  for (int i = 0; i < 2; ++i) {
    const paint::Figure& f = st.fig[i];
    o << (i ? " them " : "me ") << f.x << ',' << f.y << " s" << int(f.scalePct) << ' ' << int(f.widePct) << '/'
      << int(f.tallPct) << " l" << int(f.leanPct) << " up" << int(f.lift) << " fl" << int(f.flash) << " a"
      << int(f.alpha) << " face" << (f.hasFace ? int(f.face.v) : -1) << " new"
      << int(f.newScars) << '@' << int(f.scarIn);
  }
  for (int i = 0; i < st.propCount; ++i) {
    const paint::Prop& p = st.props[i];
    o << " | " << int(p.kind) << '.' << int(p.variant) << ' ' << p.x << ',' << p.y;
    if (p.kind == PropKind::Arrow) o << '>' << p.x2 << ',' << p.y2;
    o << " s" << int(p.scalePct) << " a" << int(p.alpha) << " f" << int(p.fill);
  }
  return o.str();
}

std::string goldenPath() {
  std::string here = __FILE__;
  return here.substr(0, here.find_last_of('/')) + "/../golden/duel_staging.txt";
}

// Luma in 0..255 from RGB565, the way a grey screenshot or a colour-blind eye
// would have it.
int luma(uint16_t p) {
  const int r = ((p >> 11) & 31) * 255 / 31, g = ((p >> 5) & 63) * 255 / 63, b = (p & 31) * 255 / 31;
  return (2 * r + 5 * g + b) / 8;
}
int lumaDiffers(const Canvas240& a, const Canvas240& b) {
  int n = 0;
  for (int i = 0; i < kSide * kSide; ++i) n += std::abs(luma(a.px[i]) - luma(b.px[i])) > 24;
  return n;
}

}  // namespace

// ---- struct goldens: where everything goes, each moment, three pairings --------------

TEST(Staging, EveryMomentForThreePairingsMatchesItsGolden) {
  std::ostringstream got;
  for (const Pairing& p : kPairings)
    for (const blorbtest::DuelMoment& m : kDuelMoments) {
      const DuelScene s = duelScene(m.name, byName(p.mine), byName(p.theirs));
      got << p.mine << '-' << p.theirs << ' ' << m.name << ": "
          << dump(paint::stageDuel(s, paint::packFor(s.me.look.species), paint::packFor(s.them.look.species))) << '\n';
    }
  if (std::getenv("UPDATE_GOLDEN")) {
    std::ofstream(goldenPath()) << got.str();
    GTEST_SKIP() << "blessed " << goldenPath();
  }
  std::ifstream in(goldenPath());
  ASSERT_TRUE(in.good()) << "no golden at " << goldenPath() << " (UPDATE_GOLDEN=1 writes one)";
  std::stringstream want;
  want << in.rdbuf();
  std::istringstream g(got.str()), w(want.str());
  std::string gl, wl;
  int line = 0;
  while (std::getline(w, wl)) {
    ++line;
    ASSERT_TRUE(bool(std::getline(g, gl))) << "the staging lost line " << line << ": " << wl;
    EXPECT_EQ(gl, wl) << "line " << line;
  }
  EXPECT_FALSE(bool(std::getline(g, gl))) << "the staging gained a line: " << gl;
}

TEST(Staging, TheSameSceneIsTheSameBytes) {
  const DuelScene s = duelScene("hit", byName("gilly"), byName("fizz"));
  auto a = shoot(s), b = shoot(s);
  EXPECT_EQ(0, std::memcmp(a->px, b->px, sizeof a->px));
}

// ---- scars ------------------------------------------------------------------------

namespace {

// The pixels his scars change: the same figure drawn with and without them.
struct Mask { std::vector<int> px; int cx = 0, cy = 0; };
Mask scarPixels(const Appearance& a, const paint::Figure& fig) {
  Appearance clean = a;
  clean.scars = {};
  Canvas240 with{}, without{};
  paint::drawFigure(a, paint::packFor(a.species), with, fig);
  paint::drawFigure(clean, paint::packFor(a.species), without, fig);
  Mask m;
  long sx = 0, sy = 0;
  for (int i = 0; i < kSide * kSide; ++i)
    if (with.px[i] != without.px[i]) {
      m.px.push_back(i);
      sx += i % kSide;
      sy += i / kSide;
    }
  if (!m.px.empty()) { m.cx = int(sx / long(m.px.size())); m.cy = int(sy / long(m.px.size())); }
  return m;
}

// Where his body is: the figure on black against the same on white.
std::vector<bool> bodyPixels(const Appearance& a, const paint::Figure& fig) {
  Canvas240 black{}, white{};
  for (uint16_t& p : white.px) p = 0xFFFF;
  paint::drawFigure(a, paint::packFor(a.species), black, fig);
  paint::drawFigure(a, paint::packFor(a.species), white, fig);
  std::vector<bool> body(kSide * kSide);
  for (int i = 0; i < kSide * kSide; ++i) body[i] = black.px[i] == white.px[i];   // a pixel he leaves alone differs
  return body;
}

paint::Figure standing(int lift, int wide, int tall) {
  paint::Figure f;
  f.x = 120;
  f.y = 200;
  f.scalePct = 70;
  f.lift = int8_t(lift);
  f.widePct = uint8_t(wide);
  f.tallPct = uint8_t(tall);
  return f;
}

}  // namespace

TEST(Scars, RideTheBodyThroughTheHopAndEveryStage) {
  for (const blorb::SpeciesInfo& sp : blorb::SPECIES)
    for (Stage stage : {Stage::Baby, Stage::Adult, Stage::Elder}) {
      Appearance a = blorbtest::duelLook(sp.id, 7, 0);
      a.stage = stage;
      a.exprTicks = 100;   // his face settled, not mid-crossfade, so the body mask is solid
      a.scars = blorbtest::veteran();
      const Mask rest = scarPixels(a, standing(0, 100, 100));
      ASSERT_FALSE(rest.px.empty()) << sp.name << " stage " << int(stage) << ": no scar shows";
      // Squashed for the leap, then in the air: the scars go where the body goes.
      for (const paint::Figure& fig : {standing(0, 118, 80), standing(24, 100, 100), standing(12, 92, 112)}) {
        const Mask m = scarPixels(a, fig);
        const std::vector<bool> body = bodyPixels(a, fig);
        ASSERT_FALSE(m.px.empty()) << sp.name << ": a scar vanished mid-hop";
        for (int i : m.px) ASSERT_TRUE(body[i]) << sp.name << ": a scar pixel off his body at " << i % kSide << ',' << i / kSide;
        if (fig.widePct == 100 && fig.tallPct == 100)
          EXPECT_NEAR(m.cy - rest.cy, -fig.lift, 2) << sp.name << ": the scars did not rise with him";
      }
    }
}

// The owner's rule: a scar on a limb, a shoulder, a side or his head, never on
// the front of his chest or belly, and never anything that could read as a
// ring through him. Every species, every stage, scars from every luck.
TEST(Scars, NeverOnTheFrontOfHim) {
  using blorb::Scar;
  for (const blorb::SpeciesInfo& sp : blorb::SPECIES)
    for (Stage stage : {Stage::Baby, Stage::Adult, Stage::Elder}) {
      const paint::SpritePack& pack = paint::packFor(sp.id);
      uint16_t count = 0;
      const paint::PaletteEntry* pal = pack.palette(count);
      const FrameRef body = pack.body(blorb::pose::idle, stage, 0);
      const FrameRef faceF = pack.face(blorb::expr::neutral, stage);
      const int chestTop = faceF.h ? faceF.h - faceF.originY : 0;   // below the face patch, in rows above the feet
      // His hide's extent per row, from the frame itself.
      std::vector<int> lo(body.h, -1), hi(body.h, -1);
      const uint8_t* p = body.rle;
      for (int y = 0; y < body.h; ++y)
        for (int x = 0; x < body.w; x += p[0], p += 2)
          if (p[1] && p[1] < count && paint::isHide(pal[p[1]].region)) {
            if (lo[y] < 0) lo[y] = x;
            hi[y] = x + p[0] - 1;
          }
      for (int scalePct : {25, 60, 100})   // a hatchling in the duel, a pet in the duel, a pet at home
      for (int k = 0; k < 5; ++k)
        for (int lx = 0; lx < 256; lx += 17)
          for (int ly = 0; ly < 256; ly += 17) {
            blorb::Scars s;
            s.mark[0] = {Scar::Kind(k), uint8_t(lx), uint8_t(ly), 255};
            s.count = 1;
            const paint::ScarLayer layer = paint::placeScars(s, body, pal, count, 4096 * scalePct / 100);
            ASSERT_EQ(layer.count, 1);
            for (int y = 0; y < body.h; ++y) {
              if (lo[y] < 0) continue;
              const int ry = y - body.originY;
              const bool chest = -ry < chestTop;   // lower than the bottom of his face
              const int mid = (lo[y] + hi[y]) / 2, half = (hi[y] - lo[y]) / 2;
              for (int x = lo[y]; x <= hi[y]; ++x) {
                if (paint::scarShade(layer, x - body.originX, ry) == 128) continue;
                EXPECT_FALSE(chest && half > 6 && std::abs(x - mid) * 100 < half * 30)
                    << sp.name << " stage " << int(stage) << " at " << scalePct << "% kind " << k << " luck " << lx << ',' << ly
                    << ": a scar on the front of him at " << x << ',' << y;
              }
            }
          }
    }
}

// ---- nothing is colour alone ----------------------------------------------------------

// Each state a kid reads off the screen, against its other value: the two
// frames must differ in light and dark, not only in hue, so the state reads
// on a grey screenshot and to a colour-blind kid.
TEST(Legibility, NoStateIsColourOnly) {
  struct Pair { const char* what; DuelScene a, b; };
  std::vector<Pair> pairs;
  const SpeciesId g = byName("gilly"), f = byName("fizz");
  auto base = [&](const char* m) { return duelScene(m, g, f); };
  {
    DuelScene a = base("choosing"), b = a;
    b.me.hearts = 3;
    pairs.push_back({"a heart lost", a, b});
  }
  {
    DuelScene a = base("choosing");
    a.me.hearts = a.me.maxHearts;   // the assist heart is the last, so the first one lost
    DuelScene b = a;
    b.me.assist = 0;
    pairs.push_back({"an assist heart", a, b});
  }
  {
    DuelScene a = base("choosing"), b = a;
    b.me.sparks = 5;
    pairs.push_back({"a spark pip", a, b});
  }
  {
    DuelScene a = base("choosing"), b = a;
    a.me.sparks = 0;
    b.me.sparks = 6;
    pairs.push_back({"the power ready", a, b});
  }
  {
    DuelScene a = base("choosing"), b = a;
    b.me.locked = true;
    pairs.push_back({"my stance locked", a, b});
  }
  {
    DuelScene a = base("choosing"), b = a;
    b.them.locked = true;
    pairs.push_back({"the friend locked", a, b});
  }
  {
    DuelScene a = base("choosing"), b = a;
    b.aim = int8_t(blorb::Stance::Pounce);
    pairs.push_back({"the aimed stance", a, b});
  }
  {
    DuelScene a = base("choosing"), b = a;
    b.lean = -1;
    pairs.push_back({"the pet's lean", a, b});
  }
  {
    DuelScene a = base("staking"), b = a;
    b.me.ceiling = blorb::Stakes::Spar;
    pairs.push_back({"a forbidden tier", a, b});
  }
  {
    DuelScene a = base("staking"), b = a;
    b.me.confirmed = false;
    pairs.push_back({"my tier confirmed", a, b});
  }
  {
    DuelScene a = base("staking"), b = a;
    b.them.confirmed = false;
    pairs.push_back({"the friend's tier confirmed", a, b});
  }
  {
    DuelScene a = base("choosing"), b = a;
    b.ticksLeft = 20;
    pairs.push_back({"the turn clock", a, b});
  }
  for (const Pair& p : pairs) {
    auto a = shoot(p.a), b = shoot(p.b);
    EXPECT_GE(lumaDiffers(*a, *b), 20) << p.what << " shows only as a colour";
  }
}

// ---- from D4's Outcome ----------------------------------------------------------------

// Side A's screen and side B's screen are one duel seen from either end: what
// is mine on one is theirs on the other, and the winner is the same pet.
TEST(FromOutcome, EachSideSeesTheSameTurnMirrored) {
  blorb::Outcome o;
  o.stakes = blorb::Stakes::Scrap;
  o.side[0].hearts = 4;
  o.side[0].spark = 5;
  o.side[1].hearts = 2;
  o.side[1].spark = 1;
  o.resolved = 3;
  blorb::TurnReport& r = o.turns[2];
  r.shown[0] = blorb::Stance::Guard;
  r.shown[1] = blorb::Stance::Pounce;
  r.clash = blorb::Clash::AWins;
  r.hit[1] = 1;
  r.powered[0] = true;
  r.super[0] = true;
  DuelScene a, b;
  paint::fromOutcome(a, o, blorb::Side::A);
  paint::fromOutcome(b, o, blorb::Side::B);
  EXPECT_EQ(a.won, paint::Side::Me);
  EXPECT_EQ(b.won, paint::Side::Them);
  EXPECT_TRUE(a.super && b.super);
  EXPECT_EQ(a.me.hearts, 4);
  EXPECT_EQ(b.them.hearts, 4);
  EXPECT_EQ(a.them.hit, 1);
  EXPECT_EQ(b.me.hit, 1);
  EXPECT_EQ(a.me.stance, int8_t(blorb::Stance::Guard));
  EXPECT_EQ(b.them.stance, int8_t(blorb::Stance::Guard));
  EXPECT_TRUE(a.me.fired && b.them.fired && !a.them.fired);
  EXPECT_EQ(b.me.sparks, 1);
  r.dozed[1] = true;
  paint::fromOutcome(a, o, blorb::Side::A);
  EXPECT_EQ(a.them.stance, -1) << "a dozing side shows no stance";
  o.ending = blorb::Ending::BWon;
  paint::fromOutcome(a, o, blorb::Side::A);
  EXPECT_EQ(a.won, paint::Side::Them) << "the duel's ending outranks the last clash";
}

// ---- the queued special ------------------------------------------------------------

namespace {
int countProps(const DuelStaging& st, PropKind k) {
  int n = 0;
  for (int i = 0; i < st.propCount; ++i) n += st.props[i].kind == k;
  return n;
}
DuelStaging staged(const DuelScene& s) {
  return paint::stageDuel(s, paint::packFor(s.me.look.species), paint::packFor(s.them.look.species));
}
}  // namespace

// The friend's special winds up on my screen too (so I can read and punish
// it), with its countdown; mine does on his.
TEST(Special, TheTelegraphShowsOnBothScreens) {
  for (const char* m : {"windup_quick", "windup_heavy"}) {
    const DuelStaging st = staged(duelScene(m, byName("gilly"), byName("fizz")));
    EXPECT_EQ(countProps(st, PropKind::Aura), 2) << m << ": a light behind each pet winding up";
    EXPECT_EQ(countProps(st, PropKind::Countdown), 2) << m << ": each telegraph says when it lands";
  }
  DuelScene a = duelScene("windup_quick", byName("gilly"), byName("fizz")), b = a;
  b.me.landsIn = 2;
  EXPECT_GE(lumaDiffers(*shoot(a), *shoot(b)), 20) << "two turns to go reads apart from one, without colour";
  DuelScene none = a;
  none.them.windup = blorb::Windup::None;
  EXPECT_GE(lumaDiffers(*shoot(a), *shoot(none)), 200) << "a friend winding up is plain to see";
}

// The landing stance stays hidden until it lands: a pet bracing in the clash
// shows his telegraph, never a stance, whatever stance the scene carries.
TEST(Special, ABracingPetNeverShowsItsLandingStance) {
  DuelScene s = duelScene("clash", byName("gilly"), byName("fizz"));
  s.them.windup = blorb::Windup::Heavy;
  s.them.landsIn = 1;
  s.them.special = blorb::TurnReport::Special::Committed;
  s.them.stance = int8_t(blorb::Stance::Trick);
  s.ticks = 8;
  const DuelStaging st = staged(s);
  for (int i = 0; i < st.propCount; ++i)
    if (st.props[i].kind == PropKind::Stance)
      EXPECT_LT(st.props[i].x, 120) << "a stance popped on the friend's side while he braced";
  EXPECT_EQ(countProps(st, PropKind::Countdown), 1);
}

TEST(Special, EachFateHasItsOwnBeat) {
  const char* fates[] = {"landing", "riposte", "stagger", "whiff"};
  std::vector<std::unique_ptr<Canvas240>> shots;
  for (int k = 0; k < 4; ++k) {
    const char* m = fates[k];
    DuelScene s = duelScene(m, byName("tinker"), byName("hushwing"));
    s.ticks = 16;
    const DuelStaging st = staged(s);
    int word = -1;
    for (int i = 0; i < st.propCount; ++i)
      if (st.props[i].kind == PropKind::Burst) word = st.props[i].variant;
    EXPECT_EQ(word, 4 + k) << m << ": WHAM, NOPE, OOF, WHIFF in that order";
    shots.push_back(shoot(s));
  }
  for (size_t i = 0; i < shots.size(); ++i)
    for (size_t j = i + 1; j < shots.size(); ++j)
      EXPECT_GE(lumaDiffers(*shots[i], *shots[j]), 100) << fates[i] << " and " << fates[j] << " look alike";
}

// ---- the faces stay clear --------------------------------------------------------------

namespace {
bool inBox(const paint::Box& b, int x, int y) { return x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1; }
}  // namespace

// The reaction faces are the fight on a 1.28" screen: a burst sits between the
// pets and never paints inside either face, at any tick of any beat.
TEST(Faces, NoBurstEverCoversAFace) {
  for (const Pairing& pr : kPairings)
    for (const char* m : {"meeting", "hit", "bonk", "landing", "riposte", "stagger", "whiff"}) {
      int drawn = 0;
      for (uint16_t t = 0; t <= 30; ++t) {
        DuelScene s = duelScene(m, byName(pr.mine), byName(pr.theirs));
        s.ticks = t;
        const paint::SpritePack& mp = paint::packFor(s.me.look.species);
        const paint::SpritePack& tp = paint::packFor(s.them.look.species);
        const DuelStaging st = paint::stageDuel(s, mp, tp);
        DuelStaging bare = st;
        bare.propCount = 0;
        for (int i = 0; i < st.propCount; ++i)
          if (st.props[i].kind != PropKind::Burst) bare.props[bare.propCount++] = st.props[i];
        if (bare.propCount == st.propCount) continue;
        Canvas240 with{}, without{};
        paint::drawStaged(s, st, mp, tp, with);
        paint::drawStaged(s, bare, mp, tp, without);
        for (int i = 0; i < kSide * kSide; ++i) {
          if (with.px[i] == without.px[i]) continue;
          ++drawn;
          for (int side = 0; side < 2; ++side)
            ASSERT_FALSE(inBox(st.face[side], i % kSide, i / kSide))
                << pr.mine << "-" << pr.theirs << " " << m << " tick " << t << ": the burst covers "
                << (side ? "the friend's" : "my") << " face at " << i % kSide << ',' << i / kSide;
        }
      }
      EXPECT_GT(drawn, 500) << pr.mine << "-" << pr.theirs << " " << m << ": no burst drawn";
    }
}

// The keep-out is only as good as the box: each pet's eyes are inside his.
TEST(Faces, EachFaceBoxHoldsHisEyes) {
  for (const blorb::SpeciesInfo& sp : blorb::SPECIES) {
    const DuelScene s = duelScene("choosing", sp.id, sp.id);
    const DuelStaging st = staged(s);
    for (int side = 0; side < 2; ++side) {
      const paint::Figure& f = st.fig[side];
      const blorb::Appearance& a = side ? s.them.look : s.me.look;
      const FrameRef body = paint::packFor(sp.id).body(blorb::pose::idle, a.stage, 0);
      const int k = a.scalePct * f.scalePct;   // x1/10000
      for (int e = 0; e < body.eyeCount; ++e) {
        const int x = f.x + (body.eyes[e].x - body.originX) * k / 10000;
        const int y = f.y + (body.eyes[e].y - body.originY) * k / 10000;
        EXPECT_TRUE(inBox(st.face[side], x, y)) << sp.name << (side ? " (friend)" : " (mine)") << ": eye " << e
                                                << " at " << x << ',' << y << " outside his face box";
      }
    }
  }
}

// ---- the BOOT hint -------------------------------------------------------------------

TEST(BootHint, EveryStateDrawsItsGlyphAndReadsWithoutColour) {
  Canvas240 blank{};
  for (const blorbtest::HintRow& r : blorbtest::kHintRows) {
    Canvas240 cv{};
    paint::drawBootHint(cv, r.hint);
    EXPECT_GE(lumaDiffers(cv, blank), 40) << r.context << ": no hint drawn";
    paint::RimHint grey = r.hint, held = r.hint, twice = r.hint;
    grey.greyed = !grey.greyed;
    grey.pips = 2;
    Canvas240 g{};
    paint::drawBootHint(g, grey);
    EXPECT_GE(lumaDiffers(cv, g), 8) << r.context << ": 'not now' shows only as a colour";
    if (r.hint.hasHold) {
      held.holdFill = uint8_t(255 - r.hint.holdFill);
      Canvas240 h{};
      paint::drawBootHint(h, held);
      EXPECT_GE(lumaDiffers(cv, h), 8) << r.context << ": the hold ring does not fill";
    }
    twice.hasDouble = !twice.hasDouble;
    Canvas240 d{};
    paint::drawBootHint(d, twice);
    EXPECT_GE(lumaDiffers(cv, d), 8) << r.context << ": the double-press bars do not show";
  }
}

// The hint and the home screen's marquee share the top of the disc: no hint,
// in any state, with every extra drawn at once, reaches the strip the letters
// scroll along (measured from the strip itself, not a copied number).
TEST(BootHint, StaysOffTheMarqueeStrip) {
  Appearance a = blorbtest::duelLook(blorb::kGrungo, 7, 0);
  a.thinking = false;
  Canvas240 plain{}, strip{};
  paint::draw(a, paint::packFor(a.species), plain, 0);
  a.thinking = true;
  std::snprintf(a.line, sizeof(a.line), "%s", "I SEE A MARBLE IN YOUR FUTURE");
  a.thoughtPhase = blorb::Fx::ratio(1, 2);
  paint::draw(a, paint::packFor(a.species), strip, 0);
  int top = kSide;
  for (int i = 0; i < kSide * kSide; ++i)
    if (plain.px[i] != strip.px[i] && i / kSide < kSide / 2) top = std::min(top, i / kSide);
  ASSERT_LT(top, kSide / 2) << "no marquee strip found";
  for (const blorbtest::HintRow& r : blorbtest::kHintRows) {
    paint::RimHint h = r.hint;
    h.greyed = true;
    h.pips = 3;
    h.hasHold = h.hasDouble = true;
    h.holdFill = 255;
    Canvas240 cv{};
    paint::drawBootHint(cv, h);
    for (int i = 0; i < kSide * kSide; ++i)
      if (cv.px[i] && i / kSide >= top) { ADD_FAILURE() << r.context << ": the hint reaches the marquee at row " << i / kSide; break; }
  }
}

TEST(FromOutcome, CarriesTheSpecialInFlightAndItsFate) {
  blorb::Outcome o;
  o.resolved = 3;
  o.side[0].queued = blorb::Windup::Heavy;
  o.side[0].landsOn = 5;
  o.turns[2].bracing[0] = true;
  o.turns[2].special[1] = blorb::TurnReport::Special::Riposted;
  DuelScene a, b;
  paint::fromOutcome(a, o, blorb::Side::A);
  paint::fromOutcome(b, o, blorb::Side::B);
  EXPECT_EQ(a.me.windup, blorb::Windup::Heavy);
  EXPECT_EQ(a.me.landsIn, 2);
  EXPECT_EQ(b.them.windup, blorb::Windup::Heavy) << "the friend's wind-up is on my screen too";
  EXPECT_EQ(b.them.landsIn, 2);
  EXPECT_EQ(a.them.special, blorb::TurnReport::Special::Riposted);
  EXPECT_EQ(b.me.special, blorb::TurnReport::Special::Riposted);
}

// ---- from D8's DuelView ----------------------------------------------------------------

namespace {
blorb::DuelView viewOf(blorb::DuelPhase phase) {
  blorb::DuelView v;
  v.phase = phase;
  v.phaseTicks = 14;
  v.me.species = blorb::kGrungo;
  v.them.species = byName("fizz");
  v.them.power = blorb::speciesInfo(byName("gilly")).power;   // a hybrid: fizz's body, gilly's power
  v.them.look.stage = Stage::Child;
  v.them.look.regions[blorb::region::skin.v].hue = 30;
  v.mine.hearts = 4;
  v.mine.spark = 5;
  v.theirs.hearts = 2;
  v.theirs.queued = blorb::Windup::Heavy;
  v.theirs.landsOn = 6;
  v.hasLast = true;
  v.lastTurn = 4;
  v.last.shown[0] = blorb::Stance::Guard;
  v.last.shown[1] = blorb::Stance::Pounce;
  v.last.clash = blorb::Clash::AWins;   // index 0 is me in the view
  v.last.hit[1] = 1;
  return v;
}
}  // namespace

TEST(SceneOf, TheViewBecomesTheScreen) {
  const Appearance mine = blorbtest::duelLook(blorb::kGrungo, 7, 0);
  blorb::DuelView v = viewOf(blorb::DuelPhase::Revealing);
  DuelScene s = paint::sceneOf(v, mine);
  EXPECT_EQ(s.act, paint::DuelAct::Clash);
  EXPECT_EQ(s.ticks, 14);
  EXPECT_EQ(s.won, paint::Side::Me) << "the view's index 0 is me";
  EXPECT_EQ(s.them.hit, 1);
  EXPECT_EQ(s.me.stance, int8_t(blorb::Stance::Guard));
  EXPECT_EQ(s.them.look.species, byName("fizz"));
  EXPECT_EQ(s.them.look.stage, Stage::Child);
  EXPECT_EQ(s.them.look.regions[blorb::region::skin.v].hue, 30) << "the friend in his own colours";
  EXPECT_EQ(s.them.power, int(blorb::speciesInfo(byName("gilly")).power.v)) << "a hybrid's power from his card";
  EXPECT_EQ(s.me.hearts, 4);
  EXPECT_EQ(s.them.windup, blorb::Windup::Heavy);
  EXPECT_EQ(s.them.landsIn, 2) << "turn 4 on screen, landing on 6";
  v.phase = blorb::DuelPhase::Choosing;
  EXPECT_EQ(paint::sceneOf(v, mine).them.landsIn, 1) << "choosing turn 5";

  v = viewOf(blorb::DuelPhase::Staking);
  v.chosen = blorb::Stakes::Scrap;
  v.theyChose = blorb::Stakes::Spar;
  v.skull[0][1] = 77;
  v.skull[1][0] = 190;
  s = paint::sceneOf(v, mine);
  EXPECT_EQ(s.me.skull, 77);
  EXPECT_EQ(s.them.skull, 190) << "each skull at the tier that side aims";
}

TEST(SceneOf, EveryPhaseDrawsBothPets) {
  const Appearance mine = blorbtest::duelLook(blorb::kGrungo, 7, 0);
  for (int p = int(blorb::DuelPhase::Greeting); p <= int(blorb::DuelPhase::Friends); ++p) {
    blorb::DuelView v = viewOf(blorb::DuelPhase(p));
    const DuelScene s = paint::sceneOf(v, mine);
    const DuelStaging st = staged(s);
    EXPECT_TRUE(st.fig[0].alpha > 0 && st.fig[1].alpha > 0) << "phase " << p;
    auto cv = shoot(s);
    int lit = 0;
    for (uint16_t px : cv->px) lit += px != 0;
    EXPECT_GT(lit, 30000) << "phase " << p << " drew nothing";
  }
}

namespace {

// The view D8's session would give for a fixture moment, from my side: what
// the screen shows, said the way the session says it.
blorb::DuelView viewFrom(const DuelScene& fx, blorb::DuelPhase phase) {
  blorb::DuelView v;
  v.phase = phase;
  v.phaseTicks = fx.ticks;
  v.me.species = fx.me.look.species;
  v.them.species = fx.them.look.species;
  v.me.power = blorb::speciesInfo(fx.me.look.species).power;
  v.them.power = blorb::speciesInfo(fx.them.look.species).power;
  v.them.look.stage = fx.them.look.stage;
  v.them.look.scalePct = fx.them.look.scalePct;
  v.them.look.lifeSeed = fx.them.look.lifeSeed;
  for (size_t r = 0; r < blorb::kRegionCount; ++r) v.them.look.regions[r] = fx.them.look.regions[r];
  const bool clash = phase == blorb::DuelPhase::Revealing;
  v.hasLast = true;
  v.lastTurn = 4;
  const int now = clash ? 4 : 5;
  auto side = [&](const paint::Fighter& f, blorb::SideState& s) {
    s.hearts = f.hearts;
    s.maxHearts = f.maxHearts;
    s.spark = f.sparks;
    const bool spent = f.special == blorb::TurnReport::Special::Landed || f.special == blorb::TurnReport::Special::Riposted ||
                       f.special == blorb::TurnReport::Special::Whiffed;
    s.queued = spent ? blorb::Windup::None : f.windup;
    s.landsOn = uint8_t(now + f.landsIn);
  };
  side(fx.me, v.mine);
  side(fx.them, v.theirs);
  v.assistHearts[0] = fx.me.assist;
  v.assistHearts[1] = fx.them.assist;
  v.chosen = fx.me.chose;
  v.theyChose = fx.them.chose;
  v.ceiling[0] = fx.me.ceiling;
  v.ceiling[1] = fx.them.ceiling;
  v.confirmed = fx.me.confirmed;
  v.theyConfirmed = fx.them.confirmed;
  v.skull[0][uint8_t(fx.me.chose)] = fx.me.skull;
  v.skull[1][uint8_t(fx.them.chose)] = fx.them.skull;
  v.agreed = fx.stakes;
  v.aim = fx.aim;
  v.lean = {blorb::Stance(fx.lean < 0 ? 0 : fx.lean), fx.whisper};
  v.locked = fx.me.locked;
  v.theyLocked = fx.them.locked;
  v.chooseTicksLeft = fx.ticksLeft;
  const paint::Fighter* f[2] = {&fx.me, &fx.them};
  for (int i = 0; i < 2; ++i) {
    v.last.dozed[i] = f[i]->stance < 0;
    v.last.shown[i] = blorb::Stance(f[i]->stance < 0 ? 0 : f[i]->stance);
    v.last.hit[i] = f[i]->hit;
    v.last.powered[i] = f[i]->fired;
    v.last.special[i] = f[i]->special;
  }
  v.last.clash = fx.won == paint::Side::Me ? blorb::Clash::AWins : fx.won == paint::Side::Them ? blorb::Clash::BWins : blorb::Clash::Bonk;
  if (fx.won != paint::Side::Neither) v.last.super[fx.won == paint::Side::Me ? 0 : 1] = fx.super;
  v.iWon = fx.won == paint::Side::Me;
  v.draw = fx.won == paint::Side::Neither;
  v.newScars = fx.newScars;
  return v;
}

}  // namespace

// Each fixture moment, said as D8's view of its phase, draws what the fixture
// draws: the same figures and props, to the pixel coordinate.
TEST(SceneOf, EachPhaseDrawsLikeItsFixture) {
  using P = blorb::DuelPhase;
  struct Case { const char* moment; P phase; };
  const Case cases[] = {{"meeting", P::Greeting},      {"staking", P::Staking},     {"choosing", P::Choosing},
                        {"locked", P::Waiting},        {"windup_quick", P::Choosing}, {"windup_heavy", P::Choosing},
                        {"clash", P::Revealing},       {"hit", P::Revealing},       {"bonk", P::Revealing},
                        {"landing", P::Revealing},     {"riposte", P::Revealing},   {"stagger", P::Revealing},
                        {"whiff", P::Revealing},       {"outcome", P::Fainting}};
  for (const Pairing& pr : kPairings)
    for (const Case& c : cases) {
      const DuelScene fx = duelScene(c.moment, byName(pr.mine), byName(pr.theirs));
      const DuelScene got = paint::sceneOf(viewFrom(fx, c.phase), fx.me.look);
      EXPECT_EQ(dump(staged(got)), dump(staged(fx))) << pr.mine << "-" << pr.theirs << " " << c.moment;
    }
}

// One duel seen from either end: what is mine on one screen is theirs on the
// other, and the same pet won.
TEST(SceneOf, EachSideSeesTheSameDuelMirrored) {
  const DuelScene fx = duelScene("stagger", byName("gilly"), byName("fizz"));
  const blorb::DuelView a = viewFrom(fx, blorb::DuelPhase::Revealing);
  blorb::DuelView b = a;
  std::swap(b.me, b.them);
  std::swap(b.mine, b.theirs);
  std::swap(b.assistHearts[0], b.assistHearts[1]);
  std::swap(b.chosen, b.theyChose);
  std::swap(b.confirmed, b.theyConfirmed);
  std::swap(b.locked, b.theyLocked);
  std::swap(b.last.shown[0], b.last.shown[1]);
  std::swap(b.last.dozed[0], b.last.dozed[1]);
  std::swap(b.last.hit[0], b.last.hit[1]);
  std::swap(b.last.powered[0], b.last.powered[1]);
  std::swap(b.last.special[0], b.last.special[1]);
  std::swap(b.last.super[0], b.last.super[1]);
  b.last.clash = a.last.clash == blorb::Clash::AWins ? blorb::Clash::BWins
                 : a.last.clash == blorb::Clash::BWins ? blorb::Clash::AWins : blorb::Clash::Bonk;
  const Appearance them = blorbtest::duelLook(byName("fizz"), 2, 20);
  const DuelScene sa = paint::sceneOf(a, fx.me.look), sb = paint::sceneOf(b, them);
  EXPECT_EQ(sb.won == paint::Side::Me, sa.won == paint::Side::Them);
  EXPECT_EQ(sb.won == paint::Side::Them, sa.won == paint::Side::Me);
  const paint::Fighter* pa[2] = {&sa.me, &sa.them};
  const paint::Fighter* pb[2] = {&sb.them, &sb.me};
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(pa[i]->hearts, pb[i]->hearts);
    EXPECT_EQ(pa[i]->stance, pb[i]->stance);
    EXPECT_EQ(pa[i]->hit, pb[i]->hit);
    EXPECT_EQ(pa[i]->special, pb[i]->special);
    EXPECT_EQ(pa[i]->windup, pb[i]->windup);
    EXPECT_EQ(pa[i]->landsIn, pb[i]->landsIn);
    EXPECT_EQ(pa[i]->look.species, pb[i]->look.species);
  }
  EXPECT_EQ(sa.me.special, blorb::TurnReport::Special::Staggered) << "the fixture's fate survives the trip";
}

// ---- a friend egg in the clutch --------------------------------------------------------

// A friend egg can be another species than the parent whose clutch it joins:
// it is drawn in its own species' art, and the parent's eggs in theirs.
TEST(Clutch, AFriendEggOfAnotherSpeciesIsDrawnInItsArt) {
  auto clutch = [&](SpeciesId first, SpeciesId second) {
    Appearance a;
    a.kind = Appearance::Kind::Clutch;
    a.species = blorb::kGrungo;
    a.eggCount = 2;
    a.cursor = 2;   // no cursor: nothing but the eggs differs
    a.eggs[0].species = first;
    a.eggs[1].species = second;
    auto cv = std::make_unique<Canvas240>();
    paint::draw(a, paint::packFor(a.species), *cv, 0);
    return cv;
  };
  const SpeciesId frog = blorb::kGrungo, fizz = byName("fizz");
  auto mixed = clutch(frog, fizz), frogs = clutch(frog, frog), fizzes = clutch(fizz, fizz);
  int leftMoved = 0, rightVsFrog = 0, rightVsFizz = 0;
  for (int i = 0; i < kSide * kSide; ++i) {
    const bool right = i % kSide >= kSide / 2;
    if (!right) leftMoved += mixed->px[i] != frogs->px[i];
    else {
      rightVsFrog += mixed->px[i] != frogs->px[i];
      rightVsFizz += mixed->px[i] != fizzes->px[i];
    }
  }
  EXPECT_EQ(leftMoved, 0) << "the parent's own egg changed";
  EXPECT_GT(rightVsFrog, 200) << "the friend egg is drawn as the parent's species";
  EXPECT_EQ(rightVsFizz, 0) << "the friend egg is not fizz's egg";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
