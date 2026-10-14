#include <gtest/gtest.h>
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
      << int(f.alpha) << " r" << int(f.remainsIn) << " face" << (f.hasFace ? int(f.face.v) : -1) << " new"
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
    b.me.ceiling = blorb::Stakes::Mortal;
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
    DuelScene a = base("staking_mortal"), b = a;
    b.me.hold = 250;
    pairs.push_back({"the mortal confirm filling", a, b});
  }
  {
    DuelScene a = base("mercy"), b = a;
    b.finishHold = 250;
    pairs.push_back({"the finish skull filling", a, b});
  }
  {
    DuelScene a = base("mercy"), b = a;
    b.mercyLeft = 20;
    pairs.push_back({"the mercy clock", a, b});
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

// ---- the rim at home ------------------------------------------------------------------

TEST(Rim, TrophiesAndMarksSitOnTheRimAtHome) {
  Appearance a = blorbtest::duelLook(blorb::kGrungo, 7, 0);
  Canvas240 plain{}, decorated{};
  paint::draw(a, paint::packFor(a.species), plain, 0);
  a.trophies[0] = byName("fizz");
  a.trophies[1] = byName("mudge");
  a.trophyCount = 2;
  a.owesDebt = a.vendetta = true;
  paint::draw(a, paint::packFor(a.species), decorated, 0);
  int lower = 0, upper = 0;
  for (int i = 0; i < kSide * kSide; ++i)
    if (plain.px[i] != decorated.px[i]) (i / kSide > 150 ? lower : upper)++;
  EXPECT_GT(lower, 200) << "the tokens did not show";
  EXPECT_EQ(upper, 0) << "a token strayed off the lower rim";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
