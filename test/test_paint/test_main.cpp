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
#include "blorb/registry.h"
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

std::unique_ptr<Canvas240> render(const Appearance& a, const paint::SpritePack& pack, uint16_t fill = 0) {
  auto cv = std::make_unique<Canvas240>();
  for (uint16_t& p : cv->px) p = fill;
  paint::draw(a, pack, *cv);
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

// The block with a mouth: its neutral patch tags a 6x4 block centred at frame (30, 52).
// A rotten pellet is a brown chip; every other item is the red one.
struct MouthPack : BlockPack {
  std::vector<uint8_t> mouth = encode(kW, kH, [](int x, int y) { return uint8_t(x >= 27 && x < 33 && y >= 50 && y < 54 ? 3 : 0); });
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
  a.timeUnknown = true;
  a.itemCount = 3;
  a.items[0] = {Appearance::Item::What::Pellet, {Fx::one(), Fx::zero()}};
  a.items[1] = {Appearance::Item::What::RottenPellet, {fx(-0.7), fx(0.7)}};
  a.items[2] = {Appearance::Item::What::Marble, {Fx::zero(), Fx::one()}};
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
      paint::draw(a, paint::placeholderPack(), g->cv);
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

TEST(Halo, RingsTheEyeAnchorsAndLeavesFarPixelsAlone) {
  BlockPack pack;
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
  BlockPack pack;
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
TEST(Items, OneBelowHisFeetIsDrawnWholeAndOneAboveHidesBehindHim) {
  BlockPack pack;
  const uint16_t kChip = rgb565(200, 40, 40);
  for (double dy : {0.14, -0.08}) {
    Appearance a = adult();
    a.items[0] = {Appearance::Item::What::Pellet, {fx(0), fx(dy)}};
    a.itemCount = 1;
    int shown = find(*render(a, pack), kChip).count;
    if (dy > 0) EXPECT_EQ(shown, BlockPack::kItem * BlockPack::kItem) << "below his feet: in front, whole";
    else EXPECT_EQ(shown, 0) << "above his feet: behind him";
  }
}

TEST(Items, OneUnderHimDoesNotShowBetweenHisLegs) {
  LegsPack pack;
  const uint16_t kChip = rgb565(200, 40, 40);
  Appearance a = adult();
  a.items[0] = {Appearance::Item::What::Pellet, {fx(0), fx(-0.06)}};
  a.itemCount = 1;
  EXPECT_EQ(find(*render(a, pack), kChip).count, 0) << "under his body, behind his legs";
  a.items[0].at.y = fx(0.02);
  EXPECT_EQ(find(*render(a, pack), kChip).count, 0) << "under his body, at his toes";
  a.items[0].at.y = fx(0.14);
  EXPECT_EQ(find(*render(a, pack), kChip).count, BlockPack::kItem * BlockPack::kItem) << "in front of his feet";
}

// The pack's marble read as an egg he laid; it is drawn as cobalt glass
// whatever the pack holds.
TEST(Items, TheMarbleIsRoundBlueGlassNotThePacksArt) {
  BlockPack pack;
  Appearance a = adult();
  a.items[0] = {Appearance::Item::What::Marble, {fx(0.5), fx(0.5)}};
  a.itemCount = 1;
  auto cv = render(a, pack);
  a.itemCount = 0;
  auto bare = render(a, pack);
  EXPECT_EQ(find(*cv, rgb565(200, 40, 40)).count, 0) << "not the pack's chip";
  int x0 = kSide, x1 = -1, y0 = kSide, y1 = -1, glass = 0;
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x) {
      uint16_t p = cv->px[y * kSide + x];
      if (p == bare->px[y * kSide + x]) continue;
      int r = (p >> 11) << 3, g = ((p >> 5) & 63) << 2, b = (p & 31) << 3;
      if (b < r + 60 || b < g) continue;
      ++glass;
      x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
    }
  EXPECT_GE(x1 - x0 + 1, 12) << "wide";
  EXPECT_LE(x1 - x0 + 1, 14);
  EXPECT_EQ(x1 - x0, y1 - y0) << "round";
  EXPECT_GE(glass, 100) << "blue pixels in a 13 px disc";
}

// The pellet he bites leaves the dish; while he eats it is drawn at his
// mouth, in front of him, and rides the body's transform.
TEST(Eating, ThePelletHeBitIsInFrontOfHimAtHisMouth) {
  MouthPack pack;
  const uint16_t kChip = rgb565(200, 40, 40);
  Appearance a = adult();
  EXPECT_EQ(find(*render(a, pack), kChip).count, 0) << "not eating: nothing at his mouth";
  a.mouth = blorb::Mouthful::Pellet;
  Box body = find(*render(a, pack), kOlive);
  Box chip = find(*render(a, pack), kChip);
  ASSERT_EQ(chip.count, BlockPack::kItem * BlockPack::kItem) << "whole, in front of him";
  double sy = double(body.y1 - body.y0 + 1) / BlockPack::kH;
  EXPECT_NEAR((chip.x0 + chip.x1) / 2.0, body.x0 + 30, 1.5);
  EXPECT_NEAR((chip.y0 + chip.y1) / 2.0, body.y0 + 52 * sy, 1.5);
  Box up = find(*render(hopping(a, fx(0.45), Fx::one()), pack), kChip);
  EXPECT_EQ(chip.y0 - up.y0, kHopMaxPx) << "it leaps with him";
}

// It used to be the fresh pellet whatever he bit, so the owner never saw him eat rot.
TEST(Eating, ARottenBiteIsTheRottenPelletAtHisMouth) {
  MouthPack pack;
  const uint16_t kFresh = rgb565(200, 40, 40), kRotten = rgb565(120, 88, 32);
  Appearance a = adult();
  a.mouth = blorb::Mouthful::Pellet;
  const Box fresh = find(*render(a, pack), kFresh);
  a.mouth = blorb::Mouthful::RottenPellet;
  auto cv = render(a, pack);
  const Box rotten = find(*cv, kRotten);
  EXPECT_EQ(find(*cv, kFresh).count, 0) << "no fresh pellet anywhere";
  ASSERT_EQ(rotten.count, BlockPack::kItem * BlockPack::kItem) << "the rotten one, whole, in front of him";
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
  auto away = render(adult(-0.8, 0), pack);
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
      int body0 = 0, glow0 = 0;
      measure(0, 0, body0, glow0);
      ASSERT_GT(body0, 0);
      ASSERT_GT(glow0, 0);
      for (int k = 0; k < 64; ++k) {
        int body = 0, glowing = 0;
        measure(std::cos(k * 2 * M_PI / 64), std::sin(k * 2 * M_PI / 64), body, glowing);
        EXPECT_EQ(body, body0) << "rim position " << k << " phase " << phase;
        EXPECT_EQ(glowing, glow0) << "rim position " << k << " phase " << phase;
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
    a.timeUnknown = true;
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
  Appearance empty;
  empty.kind = Appearance::Kind::Clutch;
  empty.timeUnknown = true;
  auto now = render(empty, pack);
  empty.poseTick = 1;
  EXPECT_GT(countChanged(*now, *render(empty, pack)), 0) << "the marquee does not scroll";
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
  idle.itemCount = 2;
  idle.items[0] = {Appearance::Item::What::Pellet, {fx(0.5), fx(0.4)}};
  idle.items[1] = {Appearance::Item::What::Marble, {fx(-0.6), fx(0.5)}};
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

  Appearance unknown = idle;
  unknown.at = {Fx::zero(), Fx::one()};
  unknown.timeUnknown = true;
  unknown.poseTick = 25;
  shots.push_back({"time_unknown", unknown});

  Appearance remains;
  remains.kind = Appearance::Kind::Remains;
  remains.remainsFade = fx(0.25);
  shots.push_back({"remains", remains});

  for (const auto& [name, a] : shots) writePpm(std::string(dir) + "/" + name + ".ppm", *render(a, pack));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
