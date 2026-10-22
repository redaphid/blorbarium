#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "blorb/appearance.h"
#include "blorb/dish_duel.h"
#include "blorb/registry.h"
#include "blorb/thoughts.h"
#include "paint/duel.h"
#include "paint/marquee.h"
#include "paint/text.h"
#include "../../lib/paint/src/figure.h"   // pantryColour
#include "paint/sprite_pack.h"

using blorb::Appearance;
using blorb::Fx;
using paint::Canvas240;
using paint::FrameRef;
using paint::PaletteEntry;
using paint::RegionBand;

namespace {

constexpr int kSide = 240;
constexpr int kHopMaxPx = 36;          // draw.cpp's named leap height at strength 1, scalePct 100
constexpr int kTealerMin = 40;         // green gain (0..255) a halo ring pixel must show at the floor glow
constexpr Fx kGlowFloor = blorb::kForeseeGlowFloor;

uint16_t rgb565(int r, int g, int b) { return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
int green(uint16_t p) { return ((p >> 5) & 63) * 255 / 63; }
bool outsideDish(int x, int y) {
  double dx = x + 0.5 - 120, dy = y + 0.5 - 120;
  return dx * dx + dy * dy > 120.0 * 120.0;
}
Fx fx(double v) { return Fx{int32_t(std::lround(v * Fx::kOne))}; }

std::unique_ptr<Canvas240> render(const Appearance& a, const paint::SpritePack& pack, uint16_t fill = 0,
                                  uint32_t nowMs = 0) {
  auto cv = std::make_unique<Canvas240>();
  for (uint16_t& p : cv->px) p = fill;
  paint::draw(a, pack, *cv, nowMs);
  return cv;
}

std::vector<uint8_t> encode(int w, int h, const std::function<uint8_t(int, int)>& at) {
  std::vector<uint8_t> out;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w;) {
      int run = 1;
      while (x + run < w && run < 255 && at(x + run, y) == at(x, y)) ++run;
      out.push_back(uint8_t(run));
      out.push_back(at(x, y));
      x += run;
    }
  return out;
}

// A solid olive block with two eye anchors, so geometry can be measured from
// the canvas. Its foresee face is an empty patch whose anchors sit low on the body.
constexpr uint16_t kOlive = (12 << 11) | (32 << 5) | 6;   // (96, 128, 48)
struct BlockPack : paint::SpritePack {
  static constexpr int kW = 60, kH = 80, kItem = 10;
  static constexpr paint::EyeAnchor kBodyEyes[2] = {{18, 16, 6}, {42, 16, 6}};
  static constexpr paint::EyeAnchor kPatchEyes[2] = {{12, 60, 5}, {48, 60, 5}};
  std::vector<uint8_t> solid = encode(kW, kH, [](int, int) { return uint8_t(1); });
  std::vector<uint8_t> clear = encode(kW, kH, [](int, int) { return uint8_t(0); });
  std::vector<uint8_t> chip = encode(kItem, kItem, [](int, int) { return uint8_t(2); });
  PaletteEntry pal[3] = {{0, 0, 0, 255}, {96, 128, 48, 255}, {200, 40, 40, 255}};

  FrameRef frame(const std::vector<uint8_t>& rle, const paint::EyeAnchor* eyes) const {
    return FrameRef{rle.data(), kW, kH, kW / 2, kH - 1, {eyes[0], eyes[1]}, 2};
  }
  const PaletteEntry* palette(uint16_t& n) const override { n = 3; return pal; }
  RegionBand band(blorb::RegionId) const override { return {-128, 127, 0, 255, 0, 255}; }
  FrameRef body(blorb::PoseId, blorb::Stage, uint16_t) const override { return frame(solid, kBodyEyes); }
  FrameRef face(blorb::ExprId e, blorb::Stage) const override {
    return e == blorb::expr::foresee ? frame(clear, kPatchEyes) : FrameRef{};
  }
  FrameRef mark(uint8_t, uint8_t) const override { return FrameRef{}; }
  FrameRef egg(Fx) const override { return frame(solid, kBodyEyes); }
  FrameRef remains() const override { return frame(solid, kBodyEyes); }
  FrameRef item(Appearance::Item::What) const override {
    return FrameRef{chip.data(), kItem, kItem, kItem / 2, kItem - 1, {}, 0};
  }
};

// The block on two legs: the bottom rows are open between them, as grungo's are.
struct LegsPack : BlockPack {
  std::vector<uint8_t> legs = encode(kW, kH, [](int x, int y) { return uint8_t(y >= kH - 20 && x >= 20 && x < 40 ? 0 : 1); });
  FrameRef body(blorb::PoseId, blorb::Stage, uint16_t) const override { return frame(legs, kBodyEyes); }
};

// The block with a mouth: its neutral patch tags a 6x4 block centred at frame (30, 36).
// A rotten pellet is a brown chip; every other item is the red one.
struct MouthPack : BlockPack {
  std::vector<uint8_t> mouth = encode(kW, kH, [](int x, int y) { return uint8_t(x >= 27 && x < 33 && y >= 34 && y < 38 ? 3 : 0); });
  std::vector<uint8_t> rot = encode(kItem, kItem, [](int, int) { return uint8_t(4); });
  PaletteEntry withMouth[5] = {{0, 0, 0, 255}, {96, 128, 48, 255}, {200, 40, 40, 255}, {250, 250, 0, blorb::region::mouth.v},
                               {120, 88, 32, 255}};
  const PaletteEntry* palette(uint16_t& n) const override { n = 5; return withMouth; }
  FrameRef face(blorb::ExprId e, blorb::Stage) const override {
    return e == blorb::expr::neutral ? frame(mouth, kBodyEyes) : FrameRef{};
  }
  FrameRef item(Appearance::Item::What w) const override {
    if (w != Appearance::Item::What::RottenPellet) return BlockPack::item(w);
    return FrameRef{rot.data(), kItem, kItem, kItem / 2, kItem - 1, {}, 0};
  }
};

struct Box { int x0 = kSide, y0 = kSide, x1 = -1, y1 = -1; int count = 0; };
Box find(const Canvas240& cv, uint16_t colour) {
  Box b;
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x)
      if (cv.px[y * kSide + x] == colour) {
        b.x0 = std::min(b.x0, x); b.x1 = std::max(b.x1, x);
        b.y0 = std::min(b.y0, y); b.y1 = std::max(b.y1, y);
        ++b.count;
      }
  return b;
}

struct Spot { double x, y, r; };
// Where a frame-space anchor lands, measured from the block's canvas box.
Spot onCanvas(const Box& b, const paint::EyeAnchor& e) {
  double sx = double(b.x1 - b.x0 + 1) / BlockPack::kW, sy = double(b.y1 - b.y0 + 1) / BlockPack::kH;
  return {b.x0 + (e.x + 0.5) * sx, b.y0 + (e.y + 0.5) * sy, e.r * sx};
}

Appearance adult(double x = 0, double y = 0) {
  Appearance a;
  a.stage = blorb::Stage::Adult;
  a.at = {fx(x), fx(y)};
  return a;
}

Appearance hopping(Appearance a, Fx phase, Fx strength) {
  a.reflexActive = true;
  a.reflex = blorb::reflex::hop;
  a.reflexPhase = phase;
  a.reflexStrength = strength;
  return a;
}

int countChanged(const Canvas240& a, const Canvas240& b) {
  int n = 0;
  for (int i = 0; i < kSide * kSide; ++i) n += a.px[i] != b.px[i];
  return n;
}

void expectRingTealer(const Canvas240& off, const Canvas240& on, const Spot& eye, const char* what) {
  for (int k = 0; k < 16; ++k) {
    double t = k * 2 * M_PI / 16;
    int x = int(std::floor(eye.x + 1.75 * eye.r * std::cos(t))), y = int(std::floor(eye.y + 1.75 * eye.r * std::sin(t)));
    int gain = green(on.px[y * kSide + x]) - green(off.px[y * kSide + x]);
    EXPECT_GE(gain, kTealerMin) << what << " at (" << x << ", " << y << ")";
  }
}

std::set<uint16_t> coloursOf(const paint::SpritePack& pack, uint8_t region) {
  uint16_t n = 0;
  const PaletteEntry* pal = pack.palette(n);
  std::set<uint16_t> out;
  for (int i = 1; i < n; ++i)
    if (pal[i].region == region) out.insert(rgb565(pal[i].r, pal[i].g, pal[i].b));
  return out;
}

// One busy appearance per Kind, at the rim, with every overlay on.
std::vector<Appearance> everyKind() {
  std::vector<Appearance> out;
  Appearance a = hopping(adult(0.71, -0.71), fx(0.45), Fx::one());
  a.glow = Fx::one();
  a.foreseeing = true;
  a.pantry = 255;
  a.hasHint = true;
  a.hint = blorb::care::feed;
  a.thinking = true;
  std::snprintf(a.line, sizeof(a.line), "%s", "THE POND SAYS: YES. NO. YES.");
  a.thoughtPhase = Fx::ratio(1, 2);
  a.itemCount = 2;
  a.items[0] = {Appearance::Item::What::Pellet, {Fx::one(), Fx::zero()}};
  a.items[1] = {Appearance::Item::What::RottenPellet, {fx(-0.7), fx(0.7)}};
  a.markCount = 2;
  a.marks[0] = {0, 200, blorb::Tint{20, 128, 128}};
  a.marks[1] = {3, 9, blorb::Tint{}};
  out.push_back(a);
  a.kind = Appearance::Kind::Egg;
  a.eggProgress = fx(0.9);
  a.wobble = Fx::one();
  out.push_back(a);
  a.kind = Appearance::Kind::Remains;
  a.remainsFade = fx(0.4);
  out.push_back(a);
  a.kind = Appearance::Kind::Clutch;
  a.eggCount = 3;
  a.cursor = 2;
  a.eggs[1].cloak = blorb::Tint{40, 160, 100};
  out.push_back(a);
  return out;
}

TEST(Draw, IsPureAndOverwritesEveryPixel) {
  for (const Appearance& a : everyKind()) {
    auto first = render(a, paint::placeholderPack(), 0x0000);
    auto again = render(a, paint::placeholderPack(), 0xA5A5);
    EXPECT_EQ(std::memcmp(first->px, again->px, sizeof(first->px)), 0) << "kind " << int(a.kind);
  }
}

TEST(Draw, NothingOutsideTheRoundPanel) {
  for (const Appearance& a : everyKind()) {
    auto cv = render(a, paint::placeholderPack(), 0xFFFF);
    for (int y = 0; y < kSide; ++y)
      for (int x = 0; x < kSide; ++x)
        if (outsideDish(x, y)) {
          ASSERT_EQ(cv->px[y * kSide + x], 0) << "kind " << int(a.kind) << " at " << x << "," << y;
        }
  }
}

TEST(Draw, EveryKindStaysInsideTheCanvasMemory) {
  struct Guarded { uint32_t before[512]; Canvas240 cv; uint32_t after[512]; };
  auto g = std::make_unique<Guarded>();
  for (Appearance a : everyKind()) {
    for (uint8_t scale : {uint8_t(0), uint8_t(255)}) {
      a.scalePct = scale;
      a.at = {Fx{INT32_MAX}, Fx{INT32_MIN}};
      a.hint = blorb::CareId{200};
      a.reflex = blorb::ReflexId{200};
      a.expression = blorb::ExprId{250};
      a.previous = blorb::ExprId{251};
      for (uint32_t& w : g->before) w = 0xDEADBEEF;
      for (uint32_t& w : g->after) w = 0xDEADBEEF;
      paint::draw(a, paint::placeholderPack(), g->cv, 0);
      for (uint32_t w : g->before) ASSERT_EQ(w, 0xDEADBEEFu);
      for (uint32_t w : g->after) ASSERT_EQ(w, 0xDEADBEEFu);
    }
  }
}

TEST(Colour, CloakTintMovesCloakPixelsAndNeverTheOutline) {
  const auto& pack = paint::placeholderPack();
  Appearance a = adult();
  Appearance tinted = a;
  for (blorb::Tint& t : tinted.regions) t = blorb::Tint{-12, 150, 110};
  tinted.regions[blorb::region::cloak.v] = blorb::Tint{30, 140, 120};
  auto before = render(a, pack), after = render(tinted, pack);
  auto invariant = coloursOf(pack, 255), cloak = coloursOf(pack, blorb::region::cloak.v);
  int outline = 0, cloakMoved = 0, cloakSeen = 0;
  for (int i = 0; i < kSide * kSide; ++i) {
    if (invariant.count(before->px[i])) {
      ++outline;
      EXPECT_EQ(after->px[i], before->px[i]) << "invariant pixel " << i;
    }
    if (cloak.count(before->px[i])) { ++cloakSeen; cloakMoved += after->px[i] != before->px[i]; }
  }
  EXPECT_GT(outline, 300);
  EXPECT_GT(cloakSeen, 1000);
  EXPECT_EQ(cloakMoved, cloakSeen);
}

TEST(Colour, MottlingSpotsOnlySkinAndDiffersBetweenSiblings) {
  const auto& pack = paint::placeholderPack();
  auto skin = coloursOf(pack, blorb::region::skin.v);
  std::vector<int> spotsOf[2];
  for (uint32_t seed : {1u, 2u}) {
    Appearance plain = adult();
    plain.lifeSeed = seed;
    Appearance mottled = plain;
    mottled.markCount = 1;
    mottled.marks[0] = {0, 160, blorb::Tint{}};
    auto a = render(plain, pack), b = render(mottled, pack);
    for (int i = 0; i < kSide * kSide; ++i)
      if (a->px[i] != b->px[i]) {
        EXPECT_TRUE(skin.count(a->px[i])) << "a spot landed off the skin at " << i;
        spotsOf[seed - 1].push_back(i);
      }
    EXPECT_GT(spotsOf[seed - 1].size(), 100u);
  }
  EXPECT_NE(spotsOf[0], spotsOf[1]);
}

TEST(Face, WeakExpressionShowsNeutral) {
  const auto& pack = paint::placeholderPack();
  int strongDiffers = 0;
  for (uint16_t t = 0; t < 8; ++t) {
    Appearance weak = adult();
    weak.poseTick = t;
    weak.exprTicks = 100;
    weak.expression = weak.previous = blorb::expr::alarmed;
    weak.intensity = fx(0.1);
    Appearance neutral = weak;
    neutral.expression = neutral.previous = blorb::expr::neutral;
    EXPECT_EQ(countChanged(*render(weak, pack), *render(neutral, pack)), 0) << t;
    weak.intensity = neutral.intensity = Fx::one();
    strongDiffers += countChanged(*render(weak, pack), *render(neutral, pack)) > 0;
  }
  EXPECT_GE(strongDiffers, 7);   // a blink can take one tick
}

TEST(Face, CrossfadesFromPreviousThenSettles) {
  const auto& pack = paint::placeholderPack();
  int midwayDiffers = 0;
  for (uint16_t t = 0; t < 4; ++t) {
    Appearance a = adult();
    a.poseTick = t;
    a.intensity = Fx::one();
    a.expression = blorb::expr::alarmed;
    a.previous = blorb::expr::neutral;
    Appearance settled = a;
    settled.previous = blorb::expr::alarmed;
    a.exprTicks = 3;
    EXPECT_EQ(countChanged(*render(a, pack), *render(settled, pack)), 0) << t;
    a.exprTicks = 1;
    midwayDiffers += countChanged(*render(a, pack), *render(settled, pack)) > 0;
  }
  EXPECT_GE(midwayDiffers, 3);   // a blink can take one tick
}

// Seen on the board: STATE said asleep=1 face=happy and the screen showed him
// happy, eyes open. Asleep, his eyes are shut whatever his genes feel.
TEST(Face, AsleepAlwaysShowsShutEyes) {
  const auto& pack = paint::placeholderPack();
  for (blorb::ExprId felt : {blorb::expr::happy, blorb::expr::alarmed, blorb::expr::neutral}) {
    Appearance a = adult();
    a.asleep = true;
    a.intensity = Fx::one();
    a.exprTicks = 100;
    a.expression = a.previous = felt;
    Appearance shut = a;
    shut.expression = shut.previous = blorb::expr::asleep;
    EXPECT_EQ(countChanged(*render(a, pack), *render(shut, pack)), 0) << int(felt.v);
  }
}

// The block with no face patches, so the halo has only the body's anchors.
struct FacelessPack : BlockPack {
  FrameRef face(blorb::ExprId, blorb::Stage) const override { return FrameRef{}; }
};

TEST(Halo, RingsTheEyeAnchorsAndLeavesFarPixelsAlone) {
  FacelessPack pack;
  for (uint16_t t = 0; t < 8; ++t) {   // every step of the 1.25 Hz pulse, trough included
    Appearance a = adult();
    a.foreseeing = true;
    a.poseTick = t;
    auto off = render(a, pack);
    a.glow = kGlowFloor;
    auto on = render(a, pack);
    Box b = find(*off, kOlive);
    ASSERT_GT(b.count, 0);
    Spot eyes[2] = {onCanvas(b, BlockPack::kBodyEyes[0]), onCanvas(b, BlockPack::kBodyEyes[1])};
    for (const Spot& e : eyes) expectRingTealer(*off, *on, e, "body anchor");
    for (int y = 0; y < kSide; ++y)
      for (int x = 0; x < kSide; ++x) {
        bool far = true;
        for (const Spot& e : eyes) far = far && std::hypot(x + 0.5 - e.x, y + 0.5 - e.y) > 3 * e.r;
        if (far) {
          ASSERT_EQ(on->px[y * kSide + x], off->px[y * kSide + x]) << x << "," << y;
        }
      }
  }
}

TEST(Halo, FollowsTheAnchorsThroughTheHop) {
  FacelessPack pack;
  Appearance rest = adult();
  rest.foreseeing = true;
  Box restBox = find(*render(rest, pack), kOlive);
  Appearance a = hopping(rest, fx(0.45), Fx::one());
  auto off = render(a, pack);
  a.glow = kGlowFloor;
  auto on = render(a, pack);
  Box b = find(*off, kOlive);
  ASSERT_LT(b.y1, restBox.y1);
  for (const auto& anchor : BlockPack::kBodyEyes) {
    Spot lifted = onCanvas(b, anchor), stayed = onCanvas(restBox, anchor);
    expectRingTealer(*off, *on, lifted, "lifted anchor");
    for (int k = 0; k < 16; ++k) {
      double t = k * 2 * M_PI / 16;
      double x = stayed.x + 1.75 * stayed.r * std::cos(t), y = stayed.y + 1.75 * stayed.r * std::sin(t);
      bool nearLifted = false;
      for (const auto& other : BlockPack::kBodyEyes) {
        Spot l = onCanvas(b, other);
        nearLifted = nearLifted || std::hypot(x - l.x, y - l.y) < 2.5 * l.r + 3;
      }
      int i = int(std::floor(y)) * kSide + int(std::floor(x));
      if (!nearLifted) {
        EXPECT_EQ(on->px[i], off->px[i]) << "the ring stayed behind at " << x << "," << y;
      }
    }
  }
}

TEST(Halo, UsesTheFacePatchAnchorsWhenItHasThem) {
  BlockPack pack;
  Appearance a = adult();
  a.foreseeing = true;
  a.expression = a.previous = blorb::expr::foresee;
  a.intensity = Fx::one();
  a.exprTicks = 100;
  auto off = render(a, pack);
  a.glow = kGlowFloor;
  auto on = render(a, pack);
  Box b = find(*off, kOlive);
  for (const auto& e : BlockPack::kPatchEyes) expectRingTealer(*off, *on, onCanvas(b, e), "patch anchor");
  for (const auto& e : BlockPack::kBodyEyes) {
    Spot s = onCanvas(b, e);
    int i = int(s.y) * kSide + int(s.x + 1.75 * s.r);
    EXPECT_EQ(on->px[i], off->px[i]) << "a halo at the body anchor the patch overrides";
  }
}

TEST(Hop, LiftsTheSpriteByStrengthTimesTheMax) {
  BlockPack pack;
  struct Case { double strength; uint8_t scale; int rows; };
  for (Case c : {Case{1.0, 100, kHopMaxPx}, Case{0.5, 100, kHopMaxPx / 2}, Case{1.0, 50, kHopMaxPx / 2}}) {
    Appearance rest = adult();
    rest.scalePct = c.scale;
    Box down = find(*render(rest, pack), kOlive);
    Box up = find(*render(hopping(rest, fx(0.45), fx(c.strength)), pack), kOlive);
    EXPECT_EQ(down.y1 - up.y1, c.rows) << "strength " << c.strength << " scale " << int(c.scale);
    EXPECT_EQ(up.x0, down.x0);
  }
  Appearance rest = adult();
  Box down = find(*render(rest, pack), kOlive);
  Box squash = find(*render(hopping(rest, fx(0.19), Fx::one()), pack), kOlive);
  EXPECT_EQ(squash.y1, down.y1);
  EXPECT_GT(squash.y0, down.y0);
  EXPECT_LT(squash.x0, down.x0);
}

// Items used to draw behind him whatever their depth, so one at his feet
// peeked out between them; the floor now decides what is in front.
// The food is feeding.cpp's cookie (drawFood), found by its outline.
constexpr uint16_t kCookieEdge = (96 >> 3) << 11 | (52 >> 2) << 5 | (22 >> 3);
constexpr uint16_t kRotEdge = (66 >> 3) << 11 | (74 >> 2) << 5 | (36 >> 3);

// Food far from him lies where it is, in front of him or behind him by the floor.
TEST(Items, FoodAwayFromHimLiesWhereItIs) {
  BlockPack pack;
  Appearance a = adult(-0.5, -0.5);
  a.items[0] = {Appearance::Item::What::Pellet, {fx(0.5), fx(0.5)}};
  a.itemCount = 1;
  const Box food = find(*render(a, pack), kCookieEdge);
  ASSERT_GT(food.count, 20);
  const Box body = find(*render(a, pack), kOlive);
  EXPECT_GT(food.x0, body.x1) << "off to his right";
  EXPECT_GT(food.y0, body.y1) << "and below him in the dish";
}

// Food by his feet, under him or behind his body is set down beside him,
// clear of it: on the floor by his rear or between his feet it read as a
// dropping to the people who played with him.
TEST(Items, FoodNearHimIsSetBesideHimNeverUnderOrBehind) {
  LegsPack pack;
  for (double dy : {0.14, 0.02, -0.06, -0.08, -0.2})
    for (double dx : {-0.03, 0.03}) {
      Appearance a = adult();
      a.items[0] = {Appearance::Item::What::Pellet, {fx(dx), fx(dy)}};
      a.itemCount = 1;
      const auto cv = render(a, pack);
      const Box food = find(*cv, kCookieEdge), body = find(*cv, kOlive);
      ASSERT_GT(food.count, 20) << dx << "," << dy << ": always seen";
      EXPECT_TRUE(food.x0 > body.x1 || food.x1 < body.x0) << dx << "," << dy << ": beside him, not on him";
      EXPECT_EQ(food.x0 > body.x1, dx > 0) << "on its own side";
    }
}

// The food he bit leaves the dish; while he eats it is at his mouth, in
// front of him, rides the body's transform, and goes as he chews.
TEST(Eating, TheFoodHeBitIsAtHisMouthAndGoesAsHeChews) {
  MouthPack pack;
  Appearance a = adult();
  EXPECT_EQ(find(*render(a, pack), kCookieEdge).count, 0) << "not eating: nothing at his mouth";
  a.mouth = blorb::Mouthful::Pellet;
  a.pose = blorb::pose::eat;
  a.poseTick = 0;
  const Box body = find(*render(a, pack), kOlive);
  const auto whole = render(a, pack);
  const Box food = find(*whole, kCookieEdge);
  ASSERT_GT(food.count, 20) << "in front of him";
  const double sy = double(body.y1 - body.y0 + 1) / BlockPack::kH;
  EXPECT_NEAR((food.x0 + food.x1) / 2.0, body.x0 + 30, 2.0);
  EXPECT_NEAR((food.y0 + food.y1) / 2.0, body.y0 + 36 * sy, 2.0);
  const Box up = find(*render(hopping(a, fx(0.45), Fx::one()), pack), kCookieEdge);
  EXPECT_EQ(food.y0 - up.y0, kHopMaxPx) << "it leaps with him";
  a.poseTick = 13;
  const Box chewed = find(*render(a, pack), kCookieEdge);
  EXPECT_LT(chewed.count, food.count) << "bitten";
}

// Every species at every stage has a mouth the food goes to: up at his face
// (mossby's is low, under his shell, and still his face), never down by his
// feet (food low on him read as something he left).
TEST(Eating, EverySpeciesEatsAtItsMouthAtEveryStage) {
  for (uint8_t sp = 0; sp < blorb::kSpeciesCount; ++sp)
    for (blorb::Stage st : {blorb::Stage::Baby, blorb::Stage::Adult, blorb::Stage::Elder}) {
      const paint::SpritePack& pack = paint::packFor(blorb::SpeciesId{sp});
      Appearance a = adult();
      a.stage = st;
      a.scalePct = st == blorb::Stage::Baby ? 55 : 100;
      a.species = blorb::SpeciesId{sp};
      a.pose = blorb::pose::eat;
      a.mouth = blorb::Mouthful::Pellet;
      const Box food = find(*render(a, pack), kCookieEdge);
      ASSERT_GT(food.count, 20) << "species " << int(sp) << " stage " << int(st) << ": no mouth tagged";
      Appearance shut = a;
      shut.mouth = blorb::Mouthful::Nothing;
      shut.pose = blorb::pose::idle;
      auto plain = render(shut, pack);
      int top = kSide, bottom = -1;   // his body, from the dish's background: the first and last rows he covers at the centre column
      Appearance empty;
      empty.kind = Appearance::Kind::Clutch;   // a clutch of none: the dish alone
      auto bg = render(empty, pack);
      for (int y = 0; y < kSide; ++y)
        for (int x = 100; x < 140; ++x)
          if (plain->px[y * kSide + x] != bg->px[y * kSide + x]) top = std::min(top, y), bottom = std::max(bottom, y);
      const int mid = (food.y0 + food.y1) / 2;
      EXPECT_LT(mid, top + (bottom - top) * 7 / 10) << "species " << int(sp) << " stage " << int(st)
                                                    << ": the food is low on him, not at his face";
    }
}

// A mouth tagged low on the body (the octopus's beak, under him) holds the
// food up by his eyes: food low on him read as something he was leaving.
TEST(Eating, AMouthLowOnHimHoldsTheFoodUpByHisEyes) {
  struct LowMouth : MouthPack {
    std::vector<uint8_t> low = encode(kW, kH, [](int x, int y) { return uint8_t(x >= 27 && x < 33 && y >= 68 && y < 72 ? 3 : 0); });
    FrameRef face(blorb::ExprId e, blorb::Stage) const override {
      return e == blorb::expr::neutral ? frame(low, kBodyEyes) : FrameRef{};
    }
  } pack;
  Appearance a = adult();
  a.mouth = blorb::Mouthful::Pellet;
  const Box body = find(*render(a, pack), kOlive), food = find(*render(a, pack), kCookieEdge);
  ASSERT_GT(food.count, 20);
  const double sy = double(body.y1 - body.y0 + 1) / BlockPack::kH;
  EXPECT_NEAR((food.y0 + food.y1) / 2.0, body.y0 + (16 + 24) * sy, 2.5) << "the eyes, and 3/10 of him below them";
}

// It used to be the fresh pellet whatever he bit, so the owner never saw him eat rot.
TEST(Eating, ARottenBiteIsTheRottenFoodAtHisMouth) {
  MouthPack pack;
  Appearance a = adult();
  a.mouth = blorb::Mouthful::Pellet;
  const Box fresh = find(*render(a, pack), kCookieEdge);
  a.mouth = blorb::Mouthful::RottenPellet;
  auto cv = render(a, pack);
  const Box rotten = find(*cv, kRotEdge);
  EXPECT_EQ(find(*cv, kCookieEdge).count, 0) << "no fresh food anywhere";
  ASSERT_GT(rotten.count, 20);
  EXPECT_EQ(rotten.x0, fresh.x0) << "where the fresh one would be";
  EXPECT_EQ(rotten.y0, fresh.y0);
}

// A still frame of a leap only reads as one if something stays on the floor.
TEST(Hop, LeavesADarkShadowOnTheFloorUnderHim) {
  BlockPack pack;
  Appearance rest = adult();
  Box down = find(*render(rest, pack), kOlive);
  auto up = render(hopping(rest, fx(0.45), Fx::one()), pack);
  ASSERT_LT(find(*up, kOlive).y1, down.y1 - 20) << "he is in the air";
  Appearance gone = rest;
  gone.kind = Appearance::Kind::Remains;
  gone.remainsFade = Fx::one();
  auto away = render(gone, pack);
  auto luma = [&](const Canvas240& cv, int x, int y) {
    uint16_t p = cv.px[y * kSide + x];
    return ((p >> 11) & 31) * 2 + ((p >> 5) & 63) + (p & 31) * 2;
  };
  int cx = (down.x0 + down.x1) / 2;
  ASSERT_NE(away->px[down.y1 * kSide + cx], kOlive) << "the reference frame shows bare floor there";
  EXPECT_LE(luma(*up, cx, down.y1) * 5, luma(*away, cx, down.y1) * 4) << "the floor under his feet is a fifth darker";
}

TEST(Placement, NeverClipsTheHoppingCreatureOrItsHaloAtTheRim) {
  BlockPack pack;
  for (blorb::Stage stage : {blorb::Stage::Adult, blorb::Stage::Baby}) {
    for (double phase : {0.1, 0.45, 0.8}) {
      auto measure = [&](double x, double y, int& body, int& glowing) {
        Appearance a = hopping(adult(x, y), fx(phase), Fx::one());
        a.stage = stage;
        a.foreseeing = true;
        auto dark = render(a, pack);
        a.glow = Fx::one();
        auto lit = render(a, pack);
        body = find(*dark, kOlive).count;
        glowing = countChanged(*dark, *lit);
      };
      // At the rim he is one size all the way round (smaller than at home), so
      // a clipped position lights fewer pixels than the most any position lights.
      int body[64], glowing[64], body0 = 0, glow0 = 0;
      for (int k = 0; k < 64; ++k) {
        measure(std::cos(k * 2 * M_PI / 64), std::sin(k * 2 * M_PI / 64), body[k], glowing[k]);
        body0 = std::max(body0, body[k]);
        glow0 = std::max(glow0, glowing[k]);
      }
      ASSERT_GT(body0, 0);
      ASSERT_GT(glow0, 0);
      for (int k = 0; k < 64; ++k) {
        EXPECT_EQ(body[k], body0) << "rim position " << k << " phase " << phase;
        EXPECT_EQ(glowing[k], glow0) << "rim position " << k << " phase " << phase;
      }
    }
  }
}

// It once sat over his legs in a box narrower than the dish, cutting letters
// at its hard edges.
TEST(Marquee, RunsTheDishAboveHisHeadAndNeverCoversHisFace) {
  const auto& pack = paint::placeholderPack();
  struct Rows { int top = kSide, bottom = -1, left = kSide, right = -1; };
  auto changed = [](const Canvas240& a, const Canvas240& b) {
    Rows r;
    for (int i = 0; i < kSide * kSide; ++i)
      if (a.px[i] != b.px[i]) {
        r.top = std::min(r.top, i / kSide); r.bottom = std::max(r.bottom, i / kSide);
        r.left = std::min(r.left, i % kSide); r.right = std::max(r.right, i % kSide);
      }
    return r;
  };
  Appearance nobody;
  nobody.kind = Appearance::Kind::Clutch;
  for (auto at : {std::pair{0.0, 0.0}, {0.0, -1.0}, {0.0, 1.0}, {0.7, 0.7}, {-0.7, -0.7}}) {
    Appearance a = adult(at.first, at.second);
    auto plain = render(a, pack);
    a.thinking = true;
    std::snprintf(a.line, sizeof(a.line), "%s", "I SEE A MARBLE IN YOUR FUTURE");
    a.thoughtPhase = Fx::ratio(1, 2);
    Rows band = changed(*plain, *render(a, pack));
    ASSERT_LE(band.top, band.bottom) << "no marquee drawn";
    EXPECT_LE(band.bottom - band.top, 24);
    EXPECT_GE(band.right - band.left, 150) << "the text runs the dish's width";
    Rows face;
    for (uint16_t t = 0; t < 4; ++t) {
      Appearance calm = adult(at.first, at.second), startled = calm;
      calm.poseTick = startled.poseTick = t;
      calm.intensity = startled.intensity = Fx::one();
      startled.expression = startled.previous = blorb::expr::alarmed;
      Rows f = changed(*render(calm, pack), *render(startled, pack));
      face.top = std::min(face.top, f.top);
      face.bottom = std::max(face.bottom, f.bottom);
    }
    ASSERT_LE(face.top, face.bottom);
    EXPECT_TRUE(face.top > band.bottom || face.bottom < band.top)
        << "the marquee covers his face at " << at.first << "," << at.second;
    if (at.first == 0.0 && at.second == 0.0) {
      EXPECT_LT(band.bottom, changed(*plain, *render(nobody, pack)).top) << "above his head";
    }
  }
}

// A thought crosses once, right to left: in at phase 0, gone by phase 1, and
// never back in from the right on the way out.
TEST(Marquee, AThoughtCrossesOnceAndIsGoneBeforeItsPhaseEnds) {
  const auto& pack = paint::placeholderPack();
  Appearance a = adult();
  a.thinking = true;
  std::snprintf(a.line, sizeof(a.line), "%s", "I SEE A MARBLE IN YOUR FUTURE");
  const int half = paint::marqueeHalf({-76, 0, 20, 38, 20});
  const int w = paint::textWidth(paint::kFreeSans9pt7b, a.line), span = 2 * half + w;
  auto at = [&](int off) {
    a.thoughtPhase = Fx::ratio(off, span);
    return render(a, pack);
  };
  auto gone = at(span);
  auto changed = [&](const Canvas240& cv, int fromX, int toX) {
    int n = 0;
    for (int y = 0; y < kSide; ++y)
      for (int x = fromX; x < toX; ++x) n += cv.px[y * kSide + x] != gone->px[y * kSide + x];
    return n;
  };
  EXPECT_EQ(changed(*at(0), 0, kSide), 0) << "the thought starts already on the strip";
  EXPECT_GT(changed(*at(span / 2), 0, kSide), 100) << "the thought never shows";
  // Past w + half its tail is left of the middle: the right half is empty and the left is not.
  for (int off = w + half + 4; off < span - 12; off += 8) {
    auto cv = at(off);
    EXPECT_EQ(changed(*cv, kSide / 2, kSide), 0) << "the thought comes back from the right at " << off << " of " << span;
    EXPECT_GT(changed(*cv, 0, kSide / 2), 0) << "the thought's tail vanished early at " << off << " of " << span;
  }
}

// The widget, off the creature: a blank canvas and a strip.
constexpr paint::Marquee kStrip = {-76, 0, 20, 38, 20};
constexpr char kLine[] = "TAP ME WITH YOUR PHONE";

std::unique_ptr<Canvas240> blank() { return std::make_unique<Canvas240>(Canvas240{}); }

TEST(MarqueeWidget, AKeptPhaseGoesRoundAsTheClockDoes) {
  ASSERT_FALSE(paint::marqueeFits(kStrip, paint::kFreeSans9pt7b, kLine));
  paint::MarqueePhase phase{};
  const uint32_t t0 = 5000;
  uint32_t t = t0;
  for (uint32_t dt : {0u, 33u, 47u, 33u, 1000u, 4000u, 2500u, 333u, 3000u, 41u}) {   // past one lap
    t += dt;
    auto kept = blank(), clock = blank();
    paint::drawMarquee(*kept, kStrip, phase, paint::kFreeSans9pt7b, kLine, 0xFFFF, t);
    paint::drawMarqueeAt(*clock, kStrip, paint::kFreeSans9pt7b, kLine, 0xFFFF, t - t0);
    EXPECT_GT(countChanged(*kept, *blank()), 0) << "nothing drawn at " << t - t0;
    EXPECT_EQ(countChanged(*kept, *clock), 0) << "the kept phase and the clock part at " << t - t0;
  }
}

TEST(MarqueeWidget, ANewLineComesRoundBehindNeverUnderTheReader) {
  const char* other = "SOMETHING ELSE ENTIRELY, LATER";
  const auto& f = paint::kFreeSans9pt7b;
  paint::MarqueePhase phase{};
  auto draw = [&](const char* s, uint32_t ms) {
    auto cv = blank();
    paint::drawMarquee(*cv, kStrip, phase, f, s, 0xFFFF, 1000 + ms);
    return cv;
  };
  draw(kLine, 0);
  auto swapped = draw(other, 1000), unchanged = blank();
  paint::drawMarqueeAt(*unchanged, kStrip, f, kLine, 0xFFFF, 1000);
  EXPECT_EQ(countChanged(*swapped, *unchanged), 0) << "the strip changed text under the reader";
  draw(other, 5000);
  auto lapped = draw(other, 9000), want = blank();
  const uint32_t lap = uint32_t(paint::textWidth(f, kLine) + paint::marqueeHalf(kStrip));
  ASSERT_GT(9000u * 38 / 1000, lap) << "the first line has not finished its lap";
  paint::drawMarqueeRing(*want, kStrip, f, other, other, 0xFFFF, 0, (9000u * 38 - lap * 1000) / 1000);
  EXPECT_EQ(countChanged(*lapped, *want), 0) << "the new line is not the one going round after the lap";
}

TEST(MarqueeWidget, ASaidLineArrivesFromTheRightAndAShortOneParksCentred) {
  const auto& f = paint::kFreeSans9pt7b;
  const uint32_t life = paint::marqueeTimedDuration(kStrip, f, kLine);
  auto arriving = blank(), going = blank();
  paint::drawMarqueeTimed(*arriving, kStrip, f, kLine, 0xFFFF, 0, life);
  paint::drawMarqueeTimed(*going, kStrip, f, kLine, 0xFFFF, life / 4, life);
  EXPECT_EQ(countChanged(*arriving, *blank()), 0) << "a said line starts off the right edge";
  EXPECT_GT(countChanged(*going, *blank()), 0);
  auto parked = blank(), centred = blank();
  paint::drawMarqueeTimed(*parked, kStrip, f, "HI", 0xFFFF, 60000, 60000);
  const int w = paint::textWidth(f, "HI");
  paint::drawString(*centred, f, "HI", 120 - w / 2, 120 + kStrip.dy, 0xFFFF, {0, 0, 240, 240, 0});
  EXPECT_GT(countChanged(*parked, *blank()), 0);
  EXPECT_EQ(countChanged(*parked, *centred), 0) << "a short said line does not park centred";
}

// ---- feeding (feed_show.h, feeding.cpp) ------------------------------------------------

// Nothing heard, dropped, refused or eaten lately: no meter, no bubble, no
// cookie; only the pantry bar, which is always there.
TEST(Feeding, NoNewsDrawsNothingButThePantry) {
  const auto& pack = paint::placeholderPack();
  Appearance a = adult(0.1, 0.1);
  a.pantry = 2;
  a.feed.pantryMax = 4;
  a.feed.fullness = fx(0.5);
  Appearance hungry = a;
  hungry.feed.fullness = fx(0.1);
  EXPECT_EQ(countChanged(*render(a, pack), *render(hungry, pack)), 0) << "no meter";
  Appearance none = a;
  none.feed.pantryMax = 0;
  EXPECT_GT(countChanged(*render(a, pack), *render(none, pack)), 200) << "the pantry bar";
}

// The second knock says FOOD at once: a cookie springs out of the rim's
// pellet glyph (top right) and waits there; nothing else on the screen moves.
// With the pantry empty, no cookie comes.
TEST(Feeding, TheKnockKnockBringsACookieOutOfThePantryGlyph) {
  const auto& pack = paint::placeholderPack();
  Appearance plain = adult();
  plain.pantry = 3;
  Appearance heard = plain;
  heard.feed.heardMs = 400;
  auto a = render(plain, pack), b = render(heard, pack);
  const Box food = find(*b, kCookieEdge);
  ASSERT_GT(food.count, 40) << "a cookie, big";
  EXPECT_GT(food.x0, 110) << "under the glyph at the top right";
  EXPECT_LT(food.y1, 80);
  int far = 0;
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x)
      far += a->px[y * kSide + x] != b->px[y * kSide + x] && (x < 100 || y > 90);
  EXPECT_EQ(far, 0) << "nothing round the whole screen: no ring, no meter yet";
  heard.pantry = 0;
  EXPECT_EQ(find(*render(heard, pack), kCookieEdge).count, 0) << "none left: no cookie";
  heard.pantry = 3;
  heard.feed.heardMs = 2400;
  EXPECT_EQ(countChanged(*render(plain, pack), *render(heard, pack)), 0) << "nothing came of the knocks: it went back in";
}

// Everything the pet says, and the feeding's own words, draws with glyphs the
// badge's font has (thoughts.h inFont is what the lines are checked against):
// a character outside it would draw as nothing, or as some other symbol.
TEST(Font, HasEveryCharacterTheLinesMayUse) {
  const paint::Font& f = paint::kFreeSans9pt7b;
  for (int c = 1; c < 128; ++c) {
    if (!blorb::inFont(char(c))) continue;
    ASSERT_GE(c, f.first) << char(c);
    ASSERT_LE(c, f.last) << char(c);
    const paint::Glyph& g = f.glyphs[c - f.first];
    EXPECT_GT(g.xAdvance, 0) << char(c);
    if (c != ' ') EXPECT_GT(g.width * g.height, 0) << "'" << char(c) << "' has no ink";
  }
}

// The pantry empty is a thought of emoji, not words, for a six-year-old: a
// thought bubble with the food he wants, a red NO over it, then a sad face.
// I'M FULL! and ZZZ... stay words.
TEST(Feeding, AnEmptyPantryIsAThoughtOfEmojiNotWords) {
  const auto& pack = paint::placeholderPack();
  constexpr uint16_t kWords = (52 >> 3) << 11 | (36 >> 2) << 5 | (24 >> 3);   // feeding.cpp kInk
  constexpr uint16_t kNoRed = (232 >> 3) << 11 | (36 >> 2) << 5 | (36 >> 3), kSad = (255 >> 3) << 11 | (204 >> 2) << 5 | (40 >> 3);
  Appearance a = adult();
  a.feed.pantryMax = 4;
  a.feed.refused = blorb::FeedRefusal::Empty;
  auto at = [&](int ms, uint8_t tries = 0) {
    Appearance b = a;
    b.feed.refusedMs = uint16_t(ms);
    b.feed.emptyTries = tries;
    return render(b, pack);
  };
  auto first = at(450);
  EXPECT_GT(find(*first, 0xFFFF).count, 2500) << "a thought bubble, big";
  EXPECT_EQ(find(*first, kWords).count, 0) << "no words";
  EXPECT_GT(find(*at(1000), kNoRed).count, 80) << "NO slams over the food";
  EXPECT_GT(find(*at(1900), kSad).count, 300) << "then a sad face";
  EXPECT_GT(countChanged(*at(450, 0), *at(450, 2)), 200) << "another try, another picture";
  EXPECT_EQ(countChanged(*at(5000), *render(a, pack)), 0) << "and then it is gone";
  a.feed.refused = blorb::FeedRefusal::Full;
  a.feed.refusedMs = 600;
  EXPECT_GT(find(*render(a, pack), kWords).count, 20) << "I'M FULL! is still words";
}

// ---- the pantry bar -----------------------------------------------------------------

// A pixel in the bar's band at `turn` (1/1024 turn clockwise from three o'clock).
uint16_t barAt(const Canvas240& cv, int turn) {
  const double t = turn * 6.283185307179586 / 1024;
  const int x = int(std::lround(119.5 + 108 * std::cos(t))), y = int(std::lround(119.5 + 108 * std::sin(t)));
  return cv.px[y * kSide + x];
}
int red(uint16_t p) { return ((p >> 11) & 31) * 255 / 31; }
int greenOf(uint16_t p) { return ((p >> 5) & 63) * 255 / 63; }

// Green full, through yellow and amber, to red with one left or none.
TEST(Pantry, ItsColourGoesFromGreenToRed) {
  EXPECT_GT(greenOf(paint::pantryColour(256)), red(paint::pantryColour(256)) + 60) << "full: green";
  EXPECT_GT(red(paint::pantryColour(64)), greenOf(paint::pantryColour(64)) + 120) << "one left: red";
  const uint16_t half = paint::pantryColour(128);
  EXPECT_GT(red(half), 200);
  EXPECT_GT(greenOf(half), 100) << "half: amber, between";
  for (int f = 64; f < 256; f += 16) EXPECT_LE(greenOf(paint::pantryColour(f)), greenOf(paint::pantryColour(f + 16)) + 24) << f;
}

// Its fill is the pellets left: at each level the bar is lit that far along,
// in that level's colour, and past it is the dark track.
TEST(Pantry, TheBarIsFilledToThePelletsLeftInTheirColour) {
  const auto& pack = paint::placeholderPack();
  constexpr int kA0 = 668, kA1 = 762;   // feeding.cpp kBarA0, kBarA1
  for (int left = 0; left <= 4; ++left) {
    Appearance a = adult();
    a.pantry = uint8_t(left);
    a.feed.pantryMax = 4;
    auto cv = render(a, pack);
    const uint16_t want = paint::pantryColour(256 * left / 4);
    const int lit = kA0 + (kA1 - kA0) * left / 4;
    if (left > 0) EXPECT_EQ(barAt(*cv, kA0 + 4), want) << left << " left: lit from the start";
    if (left > 0) EXPECT_EQ(barAt(*cv, lit - 4), want) << left << " left: up to its level";
    if (left < 4) EXPECT_NE(barAt(*cv, lit + 6), want) << left << " left: dark past it";
  }
}

// A knock knock on an empty pantry shakes it hard; nothing else does.
TEST(Pantry, ItShakesOnlyWhenAKnockKnockFindsItEmpty) {
  const auto& pack = paint::placeholderPack();
  Appearance a = adult();
  a.feed.pantryMax = 4;
  auto bar = [&](const Canvas240& cv) {   // the bar's corner of the panel, clear of the thought bubble
    std::vector<uint16_t> v;
    for (int y = 4; y < 50; ++y)
      for (int x = 30; x < 122; ++x) v.push_back(cv.px[y * kSide + x]);
    return v;
  };
  Appearance still = a;
  still.feed.refused = blorb::FeedRefusal::Empty;
  still.feed.refusedMs = 2000;
  Appearance shaken = still;
  shaken.feed.refusedMs = 40;
  EXPECT_NE(bar(*render(shaken, pack)), bar(*render(still, pack))) << "empty: it shakes";
  Appearance full = a;
  full.pantry = 3;
  full.feed.refused = blorb::FeedRefusal::Full;
  full.feed.refusedMs = 40;
  Appearance fullLater = full;
  fullLater.feed.refusedMs = 2000;
  EXPECT_EQ(bar(*render(full, pack)), bar(*render(fullLater, pack))) << "a refusal with food in it: no shake";
}

// The belly meter up the left rim: fuller, more of it lit.
TEST(Feeding, TheBellyMeterShowsHowFullHeIs) {
  const auto& pack = paint::placeholderPack();
  Appearance plain = adult();
  plain.feed.pantryMax = 4;
  auto lit = [&](double full) {
    Appearance a = plain;
    a.feed.meterMs = 100;
    a.feed.fullness = fx(full);
    auto cv = render(a, pack);
    auto base = render(plain, pack);
    int n = 0;
    for (int y = 60; y < 152; ++y)
      for (int x = 0; x < 30; ++x) n += cv->px[y * kSide + x] != base->px[y * kSide + x] && (((cv->px[y * kSide + x] >> 11) & 31) > 20);
    return n;
  };
  EXPECT_GT(lit(0.8), lit(0.2) + 200);
}

// In the air, the newest pellet is not at its spot in the dish; landed, it is.
TEST(Feeding, TheNewPelletIsTossedThenLands) {
  const BlockPack pack;
  Appearance a = adult(-0.6, -0.6);
  a.itemCount = 1;
  a.items[0] = {Appearance::Item::What::Pellet, {fx(0.5), fx(0.5)}};
  Appearance aloft = a;
  aloft.feed.flying = 0;
  aloft.feed.droppedMs = 200;
  Appearance landed = aloft;
  landed.feed.droppedMs = 3000;
  const Box spot = find(*render(a, pack), kCookieEdge);
  ASSERT_GT(spot.count, 0);
  const Box air = find(*render(aloft, pack), kCookieEdge);
  ASSERT_GT(air.count, spot.count) << "bigger in the air";
  EXPECT_LT(air.y1, spot.y0) << "above its spot, on its way down";
  EXPECT_EQ(countChanged(*render(landed, pack), *render(a, pack)), 0) << "landed and old news: an item like any";
}

// PAINT_DUMP=<dir> writes a PPM per state for review.
void writePpm(const std::string& path, const Canvas240& cv) {
  FILE* f = std::fopen(path.c_str(), "wb");
  ASSERT_NE(f, nullptr) << path;
  std::fprintf(f, "P6\n%d %d\n255\n", kSide, kSide);
  for (uint16_t p : cv.px) {
    int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    uint8_t rgb[3] = {uint8_t(r << 3 | r >> 2), uint8_t(g << 2 | g >> 4), uint8_t(b << 3 | b >> 2)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
}

TEST(Previews, DumpWhenAsked) {
  const char* dir = std::getenv("PAINT_DUMP");
  if (!dir) GTEST_SKIP() << "set PAINT_DUMP to a directory to write previews";
  const auto& pack = paint::placeholderPack();
  Appearance idle = adult(0.1, -0.1);
  idle.lifeSeed = 0x5EED;
  idle.poseTick = 7;
  idle.pantry = 4;
  idle.markCount = 1;
  idle.marks[0] = {0, 110, blorb::Tint{}};
  idle.itemCount = 1;
  idle.items[0] = {Appearance::Item::What::Pellet, {fx(0.5), fx(0.4)}};
  std::vector<std::pair<std::string, Appearance>> shots;
  shots.push_back({"idle", idle});

  Appearance foresee = idle;
  foresee.expression = foresee.previous = blorb::expr::foresee;
  foresee.intensity = Fx::one();
  foresee.exprTicks = 20;
  foresee.foreseeing = true;
  foresee.glow = fx(0.85);
  foresee.pose = blorb::pose::foresee;
  shots.push_back({"foresee", foresee});

  shots.push_back({"hop", hopping(idle, fx(0.45), Fx::one())});

  Appearance asleep = idle;
  asleep.asleep = true;
  asleep.expression = asleep.previous = blorb::expr::asleep;
  asleep.intensity = Fx::one();
  asleep.exprTicks = 50;
  asleep.night = fx(0.8);
  asleep.hasHint = true;
  asleep.hint = blorb::care::tuck_in;
  asleep.hintUrgency = fx(0.7);
  shots.push_back({"asleep", asleep});

  Appearance fight = idle;
  fight.dueling = fight.lobby = true;
  fight.lobbyTicksLeft = 2873;
  shots.push_back({"fight_searching", fight});

  Appearance egg;
  egg.kind = Appearance::Kind::Egg;
  egg.eggProgress = fx(0.5);
  egg.wobble = fx(0.6);
  egg.poseTick = 2;
  egg.glow = fx(0.7);
  shots.push_back({"egg", egg});

  Appearance hatchling = idle;
  hatchling.stage = blorb::Stage::Baby;
  hatchling.scalePct = 55;
  hatchling.expression = hatchling.previous = blorb::expr::happy;
  hatchling.intensity = Fx::one();
  hatchling.exprTicks = 20;
  hatchling.hasHint = true;
  hatchling.hint = blorb::care::feed;
  hatchling.hintUrgency = Fx::one();
  shots.push_back({"hatchling", hatchling});

  Appearance clutch;
  clutch.kind = Appearance::Kind::Clutch;
  clutch.eggCount = 3;
  clutch.cursor = 1;
  clutch.poseTick = 3;
  clutch.eggs[0].cloak = blorb::Tint{-12, 150, 110};
  clutch.eggs[1].shell = blorb::Tint{20, 150, 128};
  clutch.eggs[2].cloak = blorb::Tint{36, 120, 120};
  clutch.eggs[2].skin = blorb::Tint{-20, 140, 128};
  shots.push_back({"clutch", clutch});

  Appearance remains;
  remains.kind = Appearance::Kind::Remains;
  remains.remainsFade = fx(0.25);
  shots.push_back({"remains", remains});

  for (const auto& [name, a] : shots) writePpm(std::string(dir) + "/" + name + ".ppm", *render(a, pack));

  // The rim's BOOT hint as the firmware draws it over the dish (main.cpp
  // drawFrame), at rest and with each gesture in flight (gesture_cues.h).
  std::vector<std::pair<std::string, blorb::BootHint>> hints;
  blorb::BootHint feed = blorb::bootHint(blorb::BootContext::Awake, nullptr);
  hints.push_back({"hint_feed", feed});
  blorb::BootHint knock = feed;
  knock.knockWait = true;
  hints.push_back({"hint_one_more_knock", knock});
  blorb::BootHint side = feed;
  side.holdFill = 128;
  hints.push_back({"hint_side_hold_half", side});
  blorb::BootHint hang = feed;
  hang.doubleFill = 160;
  hints.push_back({"hint_hang_rising", hang});
  blorb::BootHint lobbyHold = blorb::bootHint(blorb::BootContext::Lobby, nullptr);
  lobbyHold.holdVoid = true, lobbyHold.holdFill = 128;
  hints.push_back({"hint_lobby_hold_does_nothing", lobbyHold});
  for (const auto& [name, h] : hints) {
    auto cv = render(idle, pack);
    paint::drawBootHint(*cv, paint::rimHint(h));
    writePpm(std::string(dir) + "/" + name + ".ppm", *cv);
  }
}

}  // namespace

// The duel's lobby keeps the dish, so it must say it is looking: a line on
// the strip above him (and a different one once a friend is near), and a
// pulse that moves with the clock.
TEST(Lobby, SaysItIsLookingAndPulses) {
  Appearance home = adult(0.0, 0.3);   // low in the dish, so the strip stays above him
  Appearance looking = home;
  looking.dueling = looking.lobby = true;
  looking.lobbyTicksLeft = 420;
  Appearance near = looking;
  near.lobbyDecline = blorb::Decline::Resting;
  auto strip = [](const Canvas240& a, const Canvas240& b) {
    int n = 0;
    for (int y = 34; y < 54; ++y)
      for (int x = 0; x < kSide; ++x) n += a.px[y * kSide + x] != b.px[y * kSide + x];
    return n;
  };
  const auto plain = render(home, paint::placeholderPack(), 0, 500);
  const auto shown = render(looking, paint::placeholderPack(), 0, 500);
  const auto friendly = render(near, paint::placeholderPack(), 0, 500);
  const auto later = render(looking, paint::placeholderPack(), 0, 1100);
  EXPECT_GT(strip(*plain, *shown), 300) << "the lobby writes on the strip";
  EXPECT_GT(strip(*shown, *friendly), 100) << "a resting pet's fight mode reads differently";
  EXPECT_GT(countChanged(*shown, *later) - strip(*shown, *later), 300) << "the pulse moves";
}

// Fight mode, unpaired: a solid red ring round the whole rim, over the pet,
// breathing in brightness but always red; gone once a pair locks or fight
// mode ends; kept while he rests in fight mode (still unpaired).
TEST(Lobby, SearchingIsARedRingRoundTheWholeRim) {
  Appearance home = adult(0.0, 0.3);
  Appearance searching = home, resting = home, connecting = home;
  searching.dueling = searching.lobby = true;
  searching.lobbyTicksLeft = 2400;
  resting = searching, resting.lobbyDecline = blorb::Decline::Resting;
  connecting = searching, connecting.lobbyConnecting = true;
  auto red = [](uint16_t p) { return ((p >> 11) & 31) * 255 / 31 >= 160 && ((p >> 5) & 63) <= 4 && (p & 31) <= 2; };
  const auto rimRed = [&](const Canvas240& cv) {
    int n = 0, of = 0;
    for (int deg = 0; deg < 360; deg += 5) {
      const double rad = deg * 3.14159265 / 180;
      for (double r : {115.0, 116.5, 118.0}) {
        const int x = int(119.5 + r * std::cos(rad)), y = int(119.5 + r * std::sin(rad));
        n += red(cv.px[y * kSide + x]), ++of;
      }
    }
    return n * 100 / of;
  };
  for (uint32_t ms : {0u, 400u, 800u, 1200u}) {
    EXPECT_GE(rimRed(*render(searching, paint::placeholderPack(), 0, ms)), 98) << ms << " ms";
    EXPECT_GE(rimRed(*render(resting, paint::placeholderPack(), 0, ms)), 98) << ms << " ms";
  }
  EXPECT_LT(rimRed(*render(home, paint::placeholderPack(), 0, 0)), 5);
  EXPECT_LT(rimRed(*render(connecting, paint::placeholderPack(), 0, 0)), 5) << "a locked pair has no ring";
  const auto dim = render(searching, paint::placeholderPack(), 0, 0), bright = render(searching, paint::placeholderPack(), 0, 800);
  EXPECT_GT(bright->px[119 * kSide + 2], dim->px[119 * kSide + 2]) << "it breathes";
}

// A hang filling up draws the same band as a fraction of the ring, swept
// clockwise from the top by the hang's charge: the kid sees the ask land
// before the lobby's full ring takes over. Nothing hangs, no arc.
TEST(Lobby, AHangFillsTheRingFromTheTop) {
  Appearance home = adult(0.0, 0.3);
  Appearance half = home, full = home;
  half.hangCharge = Fx::ratio(1, 2);
  full.hangCharge = Fx::one();
  auto red = [](uint16_t p) { return ((p >> 11) & 31) * 255 / 31 >= 160 && ((p >> 5) & 63) <= 4 && (p & 31) <= 2; };
  const auto h = render(half, paint::placeholderPack(), 0, 0), f = render(full, paint::placeholderPack(), 0, 0),
             n = render(home, paint::placeholderPack(), 0, 0);
  auto at = [](const Canvas240& cv, int x, int y) { return cv.px[y * kSide + x]; };
  EXPECT_TRUE(red(at(*h, 120, 3))) << "the top fills first";
  EXPECT_TRUE(red(at(*h, 236, 120))) << "half full reaches the right side";
  EXPECT_FALSE(red(at(*h, 3, 120))) << "but not the left: that is the half he has not held yet";
  EXPECT_TRUE(red(at(*f, 3, 120))) << "a full hang completes the ring";
  EXPECT_FALSE(red(at(*n, 120, 3))) << "no hang, no arc";
}

// A double press that opened no fight mode says why on the strip, and each
// reason reads differently.
TEST(Lobby, ARefusedFightSaysWhy) {
  Appearance home = adult(0.0, 0.3);
  auto strip = [](const Canvas240& a, const Canvas240& b) {
    int n = 0;
    for (int y = 34; y < 54; ++y)
      for (int x = 0; x < kSide; ++x) n += a.px[y * kSide + x] != b.px[y * kSide + x];
    return n;
  };
  const auto plain = render(home, paint::placeholderPack(), 0, 500);
  Appearance egg = home, update = home;
  egg.noFight = blorb::NoFight::Egg;
  update.noFight = blorb::NoFight::Updating;
  const auto e = render(egg, paint::placeholderPack(), 0, 500);
  const auto u = render(update, paint::placeholderPack(), 0, 500);
  EXPECT_GT(strip(*plain, *e), 300);
  EXPECT_GT(strip(*e, *u), 100);
}

// The idle bar: one pixel wide, on the rim, clockwise from the top by exactly its share.
TEST(IdleBar, OnePixelOnTheRimClockwiseFromTheTop) {
  auto lit = [](uint16_t permille) {
    auto cv = std::make_unique<paint::Canvas240>();
    std::memset(cv->px, 0, sizeof cv->px);
    paint::drawIdleBar(*cv, permille);
    return cv;
  };
  auto count = [](const paint::Canvas240& cv, int x0, int x1, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) n += cv.px[y * 240 + x] != 0;
    return n;
  };
  EXPECT_EQ(count(*lit(0), 0, 240, 0, 240), 0) << "asleep: nothing";
  const auto full = lit(1000);
  const int ring = count(*full, 0, 240, 0, 240);
  EXPECT_NEAR(ring, int(2 * 3.14159 * 117.5), 60) << "one pixel wide all the way round";
  for (int r = 0; r < 116; ++r) EXPECT_EQ(full->px[120 * 240 + 120 + r], 0) << "inside, " << r;
  const auto half = lit(500);
  EXPECT_NEAR(count(*half, 120, 240, 0, 240), ring / 2, 8) << "the right half: clockwise from the top";
  EXPECT_EQ(count(*half, 0, 119, 0, 240), 0) << "and not the left";
  const auto quarter = lit(250);
  EXPECT_EQ(count(*quarter, 0, 240, 121, 240), 0) << "a quarter stops at 3 o'clock";
  EXPECT_GT(count(*quarter, 120, 240, 0, 120), ring / 4 - 8);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
