// Frames between ticks (paint/tween.h): what the screen draws while the pet's
// next 100 ms tick has not come, and the idle layer on top.
#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <variant>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "links.h"
#include "mem_storage.h"
#include "paint/sprite_pack.h"
#include "paint/tween.h"
#include "traces.h"

using blorb::Appearance;
using blorb::Fx;
using paint::Canvas240;

namespace {

// A pet hatched and lived a while, then `n` ticks of him in a row, one Appearance a tick.
std::vector<Appearance> ticksOfHim(int n, uint32_t seed = 7) {
  blorbtest::MemStorage store;
  blorbtest::NullLink link;
  blorb::Dish dish(store, seed, 1, blorb::DishOptions{});
  uint32_t ms = 0;
  for (int s = 0; s < 6 * 3600 && !std::holds_alternative<blorb::Creature>(dish.occupant()); ++s)
    blorbtest::runFor(dish, link, ms, 1000);
  blorbtest::runFor(dish, link, ms, 60 * 1000);
  std::vector<Appearance> out;
  for (int i = 0; i < n; ++i) {
    blorbtest::runFor(dish, link, ms, blorb::kTickMs);
    out.push_back(dish.appearance());
  }
  return out;
}

std::unique_ptr<Canvas240> drawn(const Appearance& a, uint32_t nowMs = 0) {
  auto cv = std::make_unique<Canvas240>();
  paint::draw(a, paint::packFor(a.species), *cv, nowMs);
  return cv;
}

bool same(const Canvas240& a, const Canvas240& b) { return std::memcmp(a.px, b.px, sizeof a.px) == 0; }

}  // namespace

namespace {

// Where on the canvas he is: the middle of the pixels he (and his shadow)
// change over an empty dish, in px. The idle layer off, so only his place moves.
struct Spot { double x, y; };
Spot spotOf(Appearance a) {
  a.lively = false;
  a.itemCount = 0, a.pantry = 0, a.hasHint = false, a.thinking = false, a.trophyCount = 0;
  Appearance empty = a;
  empty.kind = Appearance::Kind::Clutch;
  empty.eggCount = 0;
  const auto him = drawn(a), none = drawn(empty);
  double sx = 0, sy = 0, n = 0;
  for (int y = 0; y < 240; ++y)
    for (int x = 0; x < 240; ++x)
      if (him->px[y * 240 + x] != none->px[y * 240 + x]) sx += x, sy += y, ++n;
  return {n ? sx / n : -1, n ? sy / n : -1};
}

// Him, walking, at `x` across the dish.
Appearance walkingAt(const Appearance& him, Fx x, blorb::PoseId pose = blorb::pose::walk) {
  Appearance a = him;
  a.reflexActive = false;
  a.asleep = false;
  a.pose = pose;
  a.at = {x, Fx::ratio(1, 10)};
  return a;
}

}  // namespace

TEST(Tween, TheScreenRunsAFifthFasterThanHisLife) {
  EXPECT_EQ(paint::kScreenSpeedQ8, 307u);   // kScreenSpeed = 1.2, the owner's knob
  EXPECT_EQ(paint::tickShare(0), 0);
  EXPECT_EQ(paint::tickShare(50), 128);
  EXPECT_EQ(paint::tickShare(100), 256);
  EXPECT_EQ(paint::tickShare(4000), 256);   // a late tick holds the frame it reached
}

// The last frame of a tick is the first frame of the next, pixel for pixel:
// the clocks run on to where the next tick starts them, and what is eased
// behind arrives at the tick it was eased toward.
TEST(Tween, EachTickPicksUpWhereTheFramesBeforeItLeftOff) {
  const std::vector<Appearance> t = ticksOfHim(80);   // 80: a waking hatchling yawns and stretches (#61), whose faces move mid-act, so fewer ticks hold one face
  int compared = 0, turned = 0;
  for (size_t i = 1; i + 1 < t.size(); ++i) {
    if (t[i + 1].pose != t[i].pose || t[i].reflexActive || t[i + 1].reflexActive || t[i].thinking) continue;
    if (t[i + 1].expression != t[i].expression) continue;
    // A chemical-bound tint steps a count at a tick, discretely, as a face does.
    if (std::memcmp(t[i].regions, t[i + 1].regions, sizeof t[i].regions) != 0) continue;
    const uint32_t tick = 5000 + uint32_t(i) * 100;
    const Appearance end = paint::between(t[i - 1], t[i], tick, tick + 100);
    const Appearance start = paint::between(t[i], t[i + 1], tick + 100, tick + 100);
    EXPECT_EQ(end.poseTick, start.poseTick) << i;
    EXPECT_EQ(end.poseSub, start.poseSub) << i;
    // Walking, his feet ran ahead at last tick's step: exact when he walks on
    // straight, and a turn of his wander (a tenth of a half turn at most)
    // moves the guess by a fraction of a step: a pixel at most, as it rounds.
    const bool walked = t[i].pose == blorb::pose::walk || t[i].pose == blorb::pose::flee;
    const bool straight = t[i + 1].at.x - t[i].at.x == t[i].at.x - t[i - 1].at.x &&
                          t[i + 1].at.y - t[i].at.y == t[i].at.y - t[i - 1].at.y;
    if (walked && !straight) {
      const Spot a = spotOf(end), b = spotOf(start);
      EXPECT_LE(std::abs(a.x - b.x) + std::abs(a.y - b.y), 1.5) << "a jump at tick " << i;
      ++turned;
      continue;
    }
    // Straight on toward home or away (a rest's walk home after an idle out
    // at the rim, sleep-idle), his size eases with his distance from home, a
    // curve, while the step ahead guesses it on a line: the frames differ by a
    // few edge pixels, his middle by under a tenth of a pixel.
    if (walked && t[i].at.x * t[i].at.x + t[i].at.y * t[i].at.y != t[i + 1].at.x * t[i + 1].at.x + t[i + 1].at.y * t[i + 1].at.y) {
      const Spot a = spotOf(end), b = spotOf(start);
      EXPECT_LE(std::abs(a.x - b.x) + std::abs(a.y - b.y), 0.1) << "a jump at tick " << i;
      ++turned;
      continue;
    }
    EXPECT_TRUE(same(*drawn(end, tick + 100), *drawn(start, tick + 100))) << "a jump at tick " << i;
    ++compared;
  }
  EXPECT_GT(compared + turned, 10);
}

// Between ticks an awake pet is a new frame every frame; one asleep is his
// tick's own until the next, so the loop can skip the frames that repeat.
TEST(Tween, AwakeHeMovesEveryFrameAsleepOnlyOnTicks) {
  const std::vector<Appearance> t = ticksOfHim(3);
  Appearance was = t[1], now = t[2];
  ASSERT_FALSE(now.asleep);
  const Appearance a = paint::between(was, now, 1000, 1020), b = paint::between(was, now, 1000, 1040);
  EXPECT_NE(std::memcmp(&a, &b, sizeof a), 0);
  EXPECT_FALSE(same(*drawn(a, 1020), *drawn(paint::between(was, now, 1000, 1060), 1060)));
  was.asleep = now.asleep = true;
  was.pose = now.pose = blorb::pose::sleep;
  const Appearance c = paint::between(was, now, 1000, 1020), d = paint::between(was, now, 1000, 1080);
  EXPECT_EQ(std::memcmp(&c, &d, sizeof c), 0);
  EXPECT_TRUE(paint::stillBetweenTicks(now));
}

// The same genome at the same moment is the same frame; the idle layer runs on
// whatever the engine does, and two species keep their own tempo.
TEST(Tween, TheIdleLayerIsAFunctionOfHisGenomeAndTheTime) {
  const std::vector<Appearance> t = ticksOfHim(2);
  const Appearance a = paint::between(t[0], t[1], 1000, 1030);
  EXPECT_TRUE(same(*drawn(a, 1030), *drawn(paint::between(t[0], t[1], 1000, 1030), 1030)));
  Appearance later = a;
  later.liveMs += 700;   // the layer alone moves on
  EXPECT_FALSE(same(*drawn(a), *drawn(later)));
  Appearance other = a;
  other.lifeSeed ^= 0x5A5A5A5Au;
  EXPECT_FALSE(same(*drawn(a), *drawn(other)));
}

// A hop's arc runs on its own clock at the screen's speed: 1.2 times the phase
// the engine says, and landed (1) for the last sixth of the reflex.
TEST(Tween, AHopPlaysAtTheScreensSpeed) {
  Appearance a;
  a.reflexActive = true;
  a.reflex = blorb::reflex::hop;
  a.reflexPhase = Fx::ratio(5, 12);
  const Appearance at = paint::between(a, a, 0, 0);
  EXPECT_NEAR(double(at.reflexPhase.raw) / Fx::kOne, 5.0 / 12 * 307 / 256, 1e-4);
  const Appearance half = paint::between(a, a, 0, 50);   // half a tick on: half a twelfth more
  EXPECT_NEAR(double(half.reflexPhase.raw) / Fx::kOne, 5.5 / 12 * 307 / 256, 1e-4);
  a.reflexPhase = Fx::ratio(11, 12);
  EXPECT_EQ(paint::between(a, a, 0, 99).reflexPhase, Fx::one());
}

// Where he stands is eased from the tick before: the whole way back at the
// tick, none of it a tick on. A pet who did not move carries no `from`.
TEST(Tween, WhereHeStandsIsEasedOneTickBehind) {
  Appearance was, now;
  was.at = {Fx::ratio(-1, 4), Fx::zero()};
  now.at = {Fx::ratio(1, 4), Fx::zero()};
  const Appearance start = paint::between(was, now, 0, 0), mid = paint::between(was, now, 0, 50),
                   end = paint::between(was, now, 0, 100);
  EXPECT_EQ(start.behind, 256);
  EXPECT_EQ(start.from.x, was.at.x);
  EXPECT_EQ(mid.behind, 128);
  EXPECT_EQ(end.behind, 0);
  EXPECT_EQ(paint::between(now, now, 0, 50).behind, 0);
}


// Walking, his feet run on ahead at the step he took, so a tilt moves him the
// frame it lands with no lag. Walking on, the next tick picks up where the
// guess left him; stopping, he is corrected by at most the step; turning
// round, by twice it.
TEST(Tween, WalkingHeRunsAheadAndATickCorrectsTheGuess) {
  const Appearance him = ticksOfHim(1)[0];
  const Fx step = Fx::ratio(6, 100);   // four kWalk steps, so a pixel or so shows clearly
  const Appearance t0 = walkingAt(him, Fx::zero()), t1 = walkingAt(him, step), t2 = walkingAt(him, step + step);
  const Appearance mid = paint::between(t0, t1, 1000, 1050);
  EXPECT_EQ(mid.ahead, 128);
  EXPECT_EQ(mid.behind, 0);
  const Spot at1 = spotOf(paint::between(t0, t1, 1000, 1000)), at2 = spotOf(paint::between(t1, t2, 1100, 1100));
  const double stepPx = at2.x - at1.x;
  ASSERT_GT(stepPx, 1.5);
  // Half a tick on, half a step ahead of where the tick put him: no lag.
  EXPECT_NEAR(spotOf(mid).x - at1.x, stepPx / 2, 1.0);
  // Walking on: the last frame of a tick is where the next tick starts him.
  const Spot end = spotOf(paint::between(t0, t1, 1000, 1100));
  EXPECT_NEAR(end.x, at2.x, 1.0);
  // Stopped: where he stopped at once, at most a step back from the guess.
  Appearance stopped = t1;
  stopped.pose = blorb::pose::idle;
  const Spot stop = spotOf(paint::between(t1, stopped, 1100, 1100));
  EXPECT_EQ(paint::between(t1, stopped, 1100, 1120).behind, 0);
  EXPECT_EQ(paint::between(t1, stopped, 1100, 1120).ahead, 0);
  EXPECT_LE(std::abs(end.x - stop.x), stepPx + 1.0);
  // Turned round: back a step, so the guess was two steps out.
  const Spot back = spotOf(paint::between(t1, t0, 1100, 1100));
  EXPECT_LE(std::abs(end.x - back.x), 2 * stepPx + 1.0);
  // Fleeing runs ahead too; any other way he moves is eased behind.
  EXPECT_EQ(paint::between(walkingAt(him, Fx::zero(), blorb::pose::flee), walkingAt(him, step, blorb::pose::flee), 0, 50).ahead, 128);
  EXPECT_EQ(paint::between(walkingAt(him, Fx::zero(), blorb::pose::curl), walkingAt(him, step, blorb::pose::curl), 0, 50).behind, 128);
}

// A guess past the rim is held to the circle he can stand in: never off the glass.
TEST(Tween, AGuessAheadNeverTakesHimOffTheGlass) {
  const Appearance him = ticksOfHim(1)[0];
  const Appearance rim0 = walkingAt(him, Fx::ratio(94, 100)), rim1 = walkingAt(him, Fx::ratio(99, 100));
  Appearance edge = walkingAt(him, Fx::one());
  const Spot most = spotOf(paint::between(edge, edge, 0, 0));   // as far out as he can stand
  const Spot guess = spotOf(paint::between(rim0, rim1, 0, 99));  // a step on would be 1.04
  EXPECT_LE(guess.x, most.x + 1.0);
  // ...and every pixel he lights is inside the dish.
  Appearance a = paint::between(rim0, rim1, 0, 99);
  a.lively = false;
  const auto cv = drawn(a);
  for (int y = 0; y < 240; ++y)
    for (int x = 0; x < 240; ++x) {
      const double dx = x + 0.5 - 120, dy = y + 0.5 - 120;
      if (dx * dx + dy * dy > 120.0 * 120.0) EXPECT_EQ(cv->px[y * 240 + x], 0) << x << "," << y;
    }
}

// The hang's ring never lags the hands filling it: a tick on it is already
// where the next tick will put it, and it never passes full. Emptying, it eases.
TEST(Tween, TheHangRingFillsAheadAndEmptiesBehind) {
  Appearance was, now;
  was.hangCharge = Fx::ratio(3, 10);
  now.hangCharge = Fx::ratio(4, 10);
  EXPECT_EQ(paint::between(was, now, 0, 0).hangCharge, now.hangCharge);
  EXPECT_NEAR(double(paint::between(was, now, 0, 50).hangCharge.raw) / Fx::kOne, 0.45, 1e-4);
  now.hangCharge = Fx::ratio(95, 100);
  EXPECT_EQ(paint::between(was, now, 0, 99).hangCharge, Fx::one());
  std::swap(was, now);   // let go: it drains from where it was
  EXPECT_EQ(paint::between(was, now, 0, 0).hangCharge, was.hangCharge);
}

// Nothing the screen does reaches the engine: the tween reads Appearances,
// and an Appearance with its between-tick fields zero is the tick's own frame.
TEST(Tween, ZeroBetweenFieldsAreTheTicksOwnFrame) {
  const std::vector<Appearance> t = ticksOfHim(1);
  Appearance a = t[0];
  ASSERT_EQ(a.poseSub, 0);
  ASSERT_EQ(a.behind, 0);
  ASSERT_FALSE(a.lively);
  Appearance b = a;
  b.liveMs = 123456;   // read only by the idle layer, which is off
  EXPECT_TRUE(same(*drawn(a), *drawn(b)));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
