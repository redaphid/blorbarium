#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include "blorb/appearance.h"
#include "blorb/registry.h"
#include "paint/sprite_pack.h"
#include "grungo_pack.h"

// Grungo as the panel shows him, measured on the drawn pixels: at home he is
// in the middle of the round panel, whatever his stage, size, pose or face,
// and the face shown fits what he is doing.
using namespace blorb;
using paint::Canvas240;

namespace {

constexpr int kSide = 240;
constexpr double kMiddle = kSide / 2.0;
constexpr double kOffBy = 1.5;   // px his body's middle may sit from the panel's

std::unique_ptr<Canvas240> render(const Appearance& a) {
  auto cv = std::make_unique<Canvas240>();
  paint::draw(a, grungoPack(), *cv);
  return cv;
}

int r5(uint16_t p) { return p >> 11; }
int g6(uint16_t p) { return (p >> 5) & 63; }
int b5(uint16_t p) { return p & 31; }

// The floor his contact shadow darkens: every channel of the empty dish scaled
// alike. Anything else that differs from the empty dish is him.
bool shadowedFloor(uint16_t got, uint16_t floor) {
  auto near = [](int got, int floor) { return std::abs(got * 256 - floor * 106) <= 256 + 128; };
  return near(r5(got), r5(floor)) && near(g6(got), g6(floor)) && near(b5(got), b5(floor));
}

struct Middle { double x, y; int pixels, w, h; };

// The middle of the box around every pixel he lights, against the same dish without him.
Middle middleOf(const Appearance& a) {
  Appearance none = a;
  none.kind = Appearance::Kind::Remains;
  none.remainsFade = Fx::one();
  auto him = render(a), empty = render(none);
  int x0 = kSide, y0 = kSide, x1 = -1, y1 = -1, n = 0;
  for (int y = 0; y < kSide; ++y)
    for (int x = 0; x < kSide; ++x) {
      uint16_t got = him->px[y * kSide + x], floor = empty->px[y * kSide + x];
      if (got == floor || shadowedFloor(got, floor)) continue;
      x0 = std::min(x0, x); x1 = std::max(x1, x);
      y0 = std::min(y0, y); y1 = std::max(y1, y);
      ++n;
    }
  return {(x0 + x1 + 1) / 2.0, (y0 + y1 + 1) / 2.0, n, x1 - x0 + 1, y1 - y0 + 1};
}

Appearance atHome(Stage stage, uint8_t scalePct) {
  Appearance a;
  a.kind = Appearance::Kind::Creature;
  a.stage = stage;
  a.scalePct = scalePct;
  a.lifeSeed = 0x5EED;
  return a;
}

const char* stageName(Stage s) {
  static const char* names[] = {"baby", "child", "adult", "elder"};
  return names[uint8_t(s)];
}

void expectInTheMiddle(const Appearance& a, const std::string& what) {
  Middle m = middleOf(a);
  ASSERT_GT(m.pixels, 200) << what << ": he is not drawn";
  EXPECT_NEAR(m.x, kMiddle, kOffBy) << what << ": his middle is " << m.x - kMiddle << " px right of the panel's";
  EXPECT_NEAR(m.y, kMiddle, kOffBy) << what << ": his middle is " << m.y - kMiddle << " px below the panel's";
  if (std::getenv("FRAMING_REPORT"))
    std::printf("%-34s middle %+6.1f %+6.1f\n", what.c_str(), m.x - kMiddle, m.y - kMiddle);
}

}  // namespace

// What he does in place (rest, sleep, foresee) he does in the middle; the
// poses that go somewhere (walk, chase, curl at the rim) are free to leave it.
TEST(Framing, AtHomeHeRestsInTheMiddleAtEveryStage) {
  struct InPlace { PoseId pose; const char* name; bool asleep, foreseeing; };
  const InPlace poses[] = {{blorb::pose::idle, "idle", false, false},
                           {blorb::pose::sleep, "asleep", true, false},
                           {blorb::pose::foresee, "foresee", false, true}};
  for (uint8_t s = 0; s < kStageCount; ++s)
    for (uint8_t scale : {55, 80, 100, 103, 110})
      for (const InPlace& p : poses)
        for (uint16_t tick : {0, 7, 13, 20}) {
          Appearance a = atHome(Stage(s), scale);
          a.pose = p.pose;
          a.poseTick = tick;
          a.asleep = p.asleep;
          a.foreseeing = p.foreseeing;
          expectInTheMiddle(a, std::string(stageName(Stage(s))) + " " + std::to_string(scale) + "% " + p.name +
                                   " t" + std::to_string(tick));
        }
}

TEST(Framing, AtHomeEveryFaceLeavesHimInTheMiddle) {
  for (uint8_t s = 0; s < kStageCount; ++s)
    for (const ExprInfo& e : EXPRESSIONS) {
      Appearance a = atHome(Stage(s), 100);
      a.expression = a.previous = e.id;
      a.intensity = Fx::one();
      a.exprTicks = 20;
      expectInTheMiddle(a, std::string(stageName(Stage(s))) + " face " + e.name);
    }
}

TEST(Framing, HisTravelIsTheSameEveryWayFromHome) {
  for (uint8_t s = 0; s < kStageCount; ++s) {
    Appearance a = atHome(Stage(s), 103);
    const Fx out[4][2] = {{Fx::one(), Fx::zero()}, {-Fx::one(), Fx::zero()}, {Fx::zero(), Fx::one()}, {Fx::zero(), -Fx::one()}};
    double reach[4];
    for (int i = 0; i < 4; ++i) {
      a.at = {out[i][0], out[i][1]};
      Middle m = middleOf(a);
      reach[i] = std::hypot(m.x - kMiddle, m.y - kMiddle);
    }
    if (std::getenv("FRAMING_REPORT"))
      std::printf("%s travel %.1f %.1f %.1f %.1f\n", stageName(Stage(s)), reach[0], reach[1], reach[2], reach[3]);
    EXPECT_GT(reach[0], 5) << stageName(Stage(s)) << " cannot leave home";
    for (int i = 1; i < 4; ++i) EXPECT_NEAR(reach[i], reach[0], 2.0) << stageName(Stage(s)) << " way " << i;
  }
}

// The round panel is small: at home he and the egg fill most of it.
TEST(Framing, AtHomeHeAndTheEggFillThePanel) {
  Middle adult = middleOf(atHome(Stage::Adult, 103));
  EXPECT_GE(adult.h, 150);
  EXPECT_LE(adult.h, 176);
  Appearance egg;
  egg.kind = Appearance::Kind::Egg;
  Middle e = middleOf(egg);
  EXPECT_GE(e.h, 140);
  EXPECT_LE(e.h, 170);
  EXPECT_NEAR(e.x, kMiddle, kOffBy);
  EXPECT_NEAR(e.y, kMiddle, kOffBy);
  if (std::getenv("FRAMING_REPORT")) std::printf("adult %dx%d egg %dx%d\n", adult.w, adult.h, e.w, e.h);
}

// The converter's glow rule, on the panel's colours.
bool teal(uint16_t p) {
  int r = r5(p) * 255 / 31, g = g6(p) * 255 / 63, b = b5(p) * 255 / 31;
  return g > 150 && b > 120 && r * 10 < g * 7;
}

// Whatever face his genes pick, foreseeing shows the foresee face: its eyes
// are the glow region, so they are the pixels a glow tint moves. Found with
// the halo off, then held teal under the ring while he foresees and while
// the glow fades after; a glow too faint for teal eyes draws no ring either.
TEST(Foresee, TealRingsGoOnlyAroundTealEyes) {
  for (Stage s : {Stage::Baby, Stage::Adult, Stage::Elder}) {
    Appearance a = atHome(s, 100);
    a.pose = blorb::pose::foresee;
    a.foreseeing = true;
    a.glow = Fx::zero();
    Appearance tinted = a;
    tinted.regions[region::glow.v] = Tint{20, 128, 128};
    auto plain = render(a), moved = render(tinted);
    a.glow = kForeseeGlowFloor;
    auto haloed = render(a);
    Appearance after = atHome(s, 100);
    after.glow = Fx::ratio(8, 10);
    auto fading = render(after);
    int eyes = 0, tealPlain = 0, tealHaloed = 0, tealFading = 0;
    for (int i = 0; i < kSide * kSide; ++i) {
      if (plain->px[i] == moved->px[i]) continue;
      ++eyes;
      tealPlain += teal(plain->px[i]);
      tealHaloed += teal(haloed->px[i]);
      tealFading += teal(fading->px[i]);
    }
    EXPECT_GE(eyes, 30) << stageName(s) << ": no glow-region pixels, so not the foresee face's eyes";
    EXPECT_GE(tealPlain * 10, eyes * 8) << stageName(s) << ": " << tealPlain << " of " << eyes << " eye pixels are teal";
    EXPECT_GE(tealHaloed * 10, eyes * 8) << stageName(s) << ": under the halo " << tealHaloed << " of " << eyes;
    EXPECT_GE(tealFading * 10, eyes * 8) << stageName(s) << ": as the glow fades " << tealFading << " of " << eyes;

    Appearance faint = atHome(s, 100), dark = faint;
    faint.glow = Fx::ratio(2, 10);
    auto lit = render(faint), unlit = render(dark);
    EXPECT_TRUE(std::equal(lit->px, lit->px + kSide * kSide, unlit->px))
        << stageName(s) << ": a ring around eyes that do not glow";
  }
}

// His genes may pick the shut-eyed face for a sleepy mix while he is awake.
TEST(Faces, ShutEyesOnlyWhileHeIsAsleep) {
  auto face = [](ExprId e, bool asleep) {
    Appearance a = atHome(Stage::Adult, 100);
    a.expression = a.previous = e;
    a.intensity = Fx::one();
    a.exprTicks = 20;
    a.asleep = asleep;
    return render(a);
  };
  auto same = [](const Canvas240& x, const Canvas240& y) { return std::equal(x.px, x.px + kSide * kSide, y.px); };
  EXPECT_TRUE(same(*face(expr::asleep, false), *face(expr::sleepy, false))) << "awake, the asleep face shows lidded";
  EXPECT_FALSE(same(*face(expr::asleep, true), *face(expr::sleepy, true))) << "asleep, his eyes are shut";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
