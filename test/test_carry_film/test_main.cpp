// Carried about, filmed: CARRY_DUMP=<dir> writes each reaction as PPM frames
// for a person to look at (scripts/test/carry_film.sh makes strips of them in
// outputs/carry-react/). Every film is a motion from src/hw/carry_ride.h
// played through the whole body path (the 250 Hz reads, the shell, the Dish)
// and drawn as the badge draws a frame (paint::between, then paint::draw),
// for a pet whose temperament takes that reaction. Without CARRY_DUMP it
// only checks that each film found its reaction.
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "../../src/debug_verbs.h"   // here, so the dependency finder links lib/debugble
#include "carry_badge.h"
#include "paint/duel.h"
#include "paint/sprite_pack.h"
#include "paint/tween.h"

using namespace blorb;
using namespace carrytest;
using hw::ride::Kind;

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

// The frame the badge shows now (main.cpp onScreen and drawFrame): between the tick and the clock.
void shoot(Badge& b, const std::string& path) {
  static paint::Canvas240 cv;
  const Appearance now = b.dish->appearance();
  const Appearance a = paint::between(now, now, b.dish->lastTickMs(), b.ms);
  paint::draw(a, paint::packFor(a.species), cv, b.ms);
  paint::drawBootHint(cv, paint::rimHint(a.boot));
  writePpm(path, cv);
}

struct Film { const char* name; Kind ride; Carry want; };
constexpr Film kFilms[] = {
    {"brace_turn", Kind::Turn, Carry::None},   {"cling_bumpy", Kind::Bumpy, Carry::Cling},
    {"dizzy_spin", Kind::Spin, Carry::Dizzy},  {"dizzy_shake", Kind::Shake, Carry::Dizzy},
    {"jelly_jolt", Kind::Jolt, Carry::Jelly},  {"whee_swing", Kind::Swing, Carry::Whee},
    {"glance_walk", Kind::Walk, Carry::Glance}, {"settle_after_walk", Kind::Settle, Carry::Settle},
};

// The film `f` for species `sp`: true when its reaction played (the brace's
// film always plays). Frames every `everyMs` while it plays, and two before.
bool film(const Film& f, SpeciesId sp, const char* dir, uint32_t everyMs) {
  Badge b(feedingtest::kHomeDown, sp);
  keepAwake(b);
  ride(b, Kind::Still, 1500);
  int n = 0;
  bool played = false;
  uint32_t lastShot = 0;
  const std::string base = dir ? std::string(dir) + "/" + f.name + "_" + speciesInfo(sp).name + "_" : "";
  auto each = [&] {
    const Carry now = b.dish->carry().playing(b.ms);
    const bool show = f.want == Carry::None ? b.dish->appearance().carry.braceX != 0 : now == f.want;
    if (!show) return;
    played = true;
    if (dir && b.ms - lastShot >= everyMs && n < 40) {
      char num[8];
      std::snprintf(num, sizeof num, "%02d", n++);
      shoot(b, base + num + ".ppm");
      lastShot = b.ms;
    }
  };
  ride(b, f.ride, 0, each);
  ride(b, Kind::Still, 4000, each);
  return played;
}

}  // namespace

TEST(Film, EachReactionPlaysForSomeSpecies) {
  const char* dir = std::getenv("CARRY_DUMP");
  for (const Film& f : kFilms) {
    int shot = 0;
    std::string who;
    for (size_t sp = 0; sp < kSpeciesCount && shot < (dir ? 3 : 1); ++sp) {
      if (!film(f, SpeciesId{uint8_t(sp)}, nullptr, 0)) continue;
      ++shot;
      who += std::string(" ") + speciesInfo(SpeciesId{uint8_t(sp)}).name;
      if (dir) film(f, SpeciesId{uint8_t(sp)}, dir, f.want == Carry::Jelly ? 60 : 120);
    }
    std::printf("[ film ] %-18s%s\n", f.name, who.c_str());
    EXPECT_GT(shot, 0) << f.name;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
