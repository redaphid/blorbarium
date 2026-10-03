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

struct Middle { double x, y; int pixels; };

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
  return {(x0 + x1 + 1) / 2.0, (y0 + y1 + 1) / 2.0, n};
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
    for (int i = 1; i < 4; ++i) EXPECT_NEAR(reach[i], reach[0], 1.5) << stageName(Stage(s)) << " way " << i;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
