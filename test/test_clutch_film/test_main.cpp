// A death, filmed to the next hatch: the vigil, the clutch, choosing an egg
// and the egg it becomes. CLUTCH_DUMP=<dir> writes each moment as a PPM for a
// person to look at (scripts/test/clutch_film.sh makes PNGs and strips of
// them, and films the commit before the egg picker for "before").
// CLUTCH_GESTURES=old drives the clutch as it was before the picker (a knock
// knock for the next egg, a side hold to keep it), so the same film runs on
// that commit; the default is the picker's (tilt toward an egg, knock).
// It reads only the engine (the occupant, the clutch's count and cursor), so
// it builds on either.
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "../../src/debug_verbs.h"   // here, so the dependency finder links lib/debugble
#include "debugble/jump.h"
#include "feeding_badge.h"
#include "paint/duel.h"
#include "paint/sprite_pack.h"

using namespace blorb;
using namespace feedingtest;

namespace {

void writePpm(const std::string& path, const paint::Canvas240& cv) {
  FILE* f = std::fopen(path.c_str(), "wb");
  ASSERT_NE(f, nullptr) << path;
  std::fprintf(f, "P6\n240 240\n255\n");
  for (uint16_t p : cv.px) {
    const int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    const uint8_t rgb[3] = {uint8_t(r << 3 | r >> 2), uint8_t(g << 2 | g >> 4), uint8_t(b << 3 | b >> 2)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
}

struct Film {
  Badge& b;
  std::string dir, name;
  int n = 0;
  // The frame the badge would show now: the dish, then the rim's hint over it (main.cpp drawFrame).
  void shot(const char* moment) {
    static paint::Canvas240 cv;
    const Appearance a = b.dish->appearance();
    paint::draw(a, paint::packFor(a.species), cv, b.ms);
    paint::drawBootHint(cv, paint::rimHint(a.boot));
    char num[8];
    std::snprintf(num, sizeof num, "%02d", n++);
    writePpm(dir + "/" + name + "_" + num + "_" + moment + ".ppm", cv);
  }
};

// Ticks of the Dish with no frames between, as DEBUG warp; then the hand a moment, so the shell settles.
void warp(Badge& b, uint32_t ticks) {
  for (uint32_t t = 0; t < ticks; ++t) b.dish->tick(b.ms += 100, b.link);
  b.nextFrame = b.ms;
  b.run(1000);
}

const Clutch* clutch(Badge& b) { return std::get_if<Clutch>(&b.dish->occupant()); }
bool anEgg(Badge& b) { return std::holds_alternative<Egg>(b.dish->occupant()); }

void knocks(Badge& b, std::initializer_list<uint32_t> at) {
  for (uint32_t a : at) b.hand.knocks.push_back(b.ms + 50 + a);
}

// An elder's death and his clutch of three, held in a hand all through.
void filmClutch(const std::string& dir, const std::string& name, std::optional<SpeciesId> species, bool oldGestures) {
  Badge b(kHomeDown, species, "elder");
  b.frameMs = 20;
  b.hand.turnMs = 250;   // a hand tips it, never jumps it
  Film f{b, dir, name};
  f.shot("alive");
  b.creature().fall();   // as debugble::die: he dies of his injuries
  b.run(300);
  ASSERT_NE(clutch(b), nullptr) << "he died";
  f.shot("died");
  const uint32_t vigil = Clutch::kVigilTicks;
  warp(b, vigil / 2);
  f.shot("vigil_half");
  warp(b, vigil / 2 - vigil / 20);
  f.shot("vigil_end");
  runUntil(b, [&] { return clutch(b) && clutch(b)->sinceDeath >= vigil && clutch(b)->previewed == clutch(b)->count; },
           vigil / 10 * 100 + 2000);
  ASSERT_NE(clutch(b), nullptr);
  EXPECT_EQ(clutch(b)->count, 3) << "an elder lays three";
  f.shot("eggs");
  b.run(500);
  f.shot("eggs_later");
  if (oldGestures) {
    // A knock knock moves to the next egg; turned onto a side and held keeps it.
    knocks(b, {0, 300});
    b.run(50 + 60);
    f.shot("first_knock");
    b.run(300 + 60);
    f.shot("second_knock");
    runUntil(b, [&] { return clutch(b)->cursor == 1; }, 2000);
    b.hand.knocks.clear();
    f.shot("next_egg");
    b.run(1000);
    b.hand.pose = Pose::SideLeft;
    b.run(1000);
    f.shot("turned");
    b.run(1000);
    f.shot("holding");
    runUntil(b, [&] { return anEgg(b); }, 5000);
  } else {
    // Tilt toward an egg (as a duel aims), then knock once to keep it.
    for (int i = 0; i < 4; ++i) {
      b.run(250);
      f.shot("hint");
    }
    b.hand.pose = Pose::TiltLeft;
    b.run(60);
    f.shot("tilt_left_60ms");
    b.run(100);
    f.shot("tilt_left_160ms");
    b.run(300);
    f.shot("tilt_left_460ms");
    b.hand.pose = Pose::TiltRight;
    b.run(100);
    f.shot("tilt_right_100ms");
    b.run(100);
    f.shot("tilt_right_200ms");
    b.run(400);
    f.shot("tilt_right_600ms");
    b.hand.pose = Pose::TiltNear;
    b.run(300);
    f.shot("tilt_near");
    // Near to left, a frame every 40 ms: the slide, drawn every frame.
    b.hand.pose = Pose::TiltLeft;
    for (int i = 0; i < 14; ++i) {
      b.run(40);
      f.shot("slide");
    }
    b.run(240);
    f.shot("aimed_left");
    knocks(b, {0});
    b.run(50 + 40);
    f.shot("knock_40ms");
    b.run(260);
    f.shot("knock_300ms");
    runUntil(b, [&] { return anEgg(b); }, 3000);
    b.hand.knocks.clear();
  }
  ASSERT_TRUE(anEgg(b)) << "an egg was kept";
  f.shot("kept");
  for (uint32_t ms : {150u, 300u, 300u, 400u, 600u, 1000u}) {
    b.run(ms);
    f.shot("chosen");
  }
  b.hand.pose = Pose::Held;
  b.run(3000);
  f.shot("egg");
  // Its incubation, then the hatch.
  for (int i = 0; i < 4000 && anEgg(b); ++i) {
    const Fx p = std::get<Egg>(b.dish->occupant()).progress();
    if (p >= Fx::ratio(7, 10)) break;
    warp(b, 50);
  }
  if (anEgg(b)) f.shot("cracking");
  for (int i = 0; i < 4000 && anEgg(b); ++i) warp(b, 20);
  b.run(100);
  f.shot("hatched");
  b.run(1500);
  f.shot("hatchling");
}

// A child's death: a clutch of one, no choice, laid as the vigil ends.
void filmOne(const std::string& dir, const std::string& name) {
  Badge b(kHomeDown, SpeciesId{5}, "child");
  b.frameMs = 20;
  Film f{b, dir, name};
  b.creature().fall();
  b.run(300);
  ASSERT_NE(clutch(b), nullptr) << "he died";
  EXPECT_EQ(clutch(b)->count, 1) << "a child lays one";
  f.shot("died");
  warp(b, Clutch::kVigilTicks - Clutch::kVigilTicks / 20 - 20);
  f.shot("vigil_end");
  runUntil(b, [&] { return anEgg(b); }, Clutch::kVigilTicks / 10 * 100 + 2000);
  ASSERT_TRUE(anEgg(b));
  for (uint32_t ms : {20u, 300u, 500u, 600u, 1200u}) {
    b.run(ms);
    f.shot("laid");
  }
}

}  // namespace

TEST(Film, DumpWhenAsked) {
  const char* dirEnv = std::getenv("CLUTCH_DUMP");
  if (!dirEnv) GTEST_SKIP() << "set CLUTCH_DUMP to a directory to write the frames";
  const char* g = std::getenv("CLUTCH_GESTURES");
  const bool old = g && !std::strcmp(g, "old");
  filmClutch(dirEnv, "grungo", std::nullopt, old);
  filmClutch(dirEnv, "puffkin", SpeciesId{2}, old);
  filmOne(dirEnv, "mudge");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
