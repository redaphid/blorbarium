// A feeding, filmed: FEEDING_DUMP=<dir> writes each moment as a PPM for a
// person to look at (scripts/test/feeding_film.sh makes PNGs and sheets of
// them, and films the commit before the feeding show for "before"). It reads
// only the engine (the pantry, his mouth), never feed_show.h, so the same
// film runs on a checkout from before it and the two line up frame for frame.
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "../../src/debug_verbs.h"   // here, so the dependency finder links lib/debugble
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

// The frame the badge would show now: the dish, then the rim's hint over it (main.cpp drawFrame).
void shoot(Badge& b, const std::string& dir, const std::string& name) {
  static paint::Canvas240 cv;
  const Appearance a = b.dish->appearance();
  paint::draw(a, paint::packFor(a.species), cv, b.ms);
  paint::drawBootHint(cv, paint::rimHint(a.boot));
  writePpm(dir + "/" + name + ".ppm", cv);
}

bool dropped(Badge& b) { return b.count.pellets > 0; }
bool chewing(Badge& b) { return b.creature().body().mouth != Mouthful::Nothing; }

// A knock knock, 300 ms apart, starting 50 ms from now.
void knockKnock(Badge& b) {
  b.count = {};
  b.hand.knocks = {b.ms + 50, b.ms + 350};
}

// One feeding, start to end, as <name>_<n>_<moment>: a hungry pet, a knock
// knock, the pellet tossed in, and him eating it. The owner's badges: home USB-C down.
void filmFed(const std::string& dir, const std::string& name, std::optional<SpeciesId> species, const char* stage) {
  Badge b(kHomeDown, species, stage);
  b.frameMs = 20;
  int n = 0;
  auto shot = [&](const char* moment) { shoot(b, dir, name + "_" + std::to_string(n++) + "_" + moment); };
  shot("before");
  knockKnock(b);
  b.run(50 + 20);
  shot("first_knock");
  b.run(300);
  shot("second_knock");
  b.run(200);
  shot("heard");
  runUntil(b, [&] { return dropped(b); }, 2000);
  b.run(160);
  shot("tossed");
  b.run(300);
  shot("coming_down");
  b.run(400);
  shot("landed");
  b.hand.knocks.clear();
  runUntil(b, [&] {
    if (b.ms % 1500 < 20) b.creature().force(action::eat);   // as tests/feeds/eating.txt does
    return chewing(b);
  }, 20000);
  b.run(60);
  shot("bite");
  b.run(400);
  shot("chew_yum");
  b.run(600);
  shot("hearts");
  b.run(900);
  shot("belly_risen");
}

// A knock knock that feeds nobody now, shot `afterMs` after it was decided.
void filmRefused(const std::string& dir, const std::string& name, Badge& b, uint32_t afterMs) {
  b.frameMs = 20;
  knockKnock(b);
  runUntil(b, [&] { return b.count.presses > 0; }, 2000);
  b.run(afterMs);
  shoot(b, dir, name);
}

// The pantry, as <name>_<n>_<moment>: full, four feeds draining it, a knock
// knock on it empty (the shake, and his thought), then the next pellet coming back.
void filmPantry(const std::string& dir, const std::string& name) {
  Badge b(kHomeDown);
  b.frameMs = 20;
  b.keepPantryFull = false;
  b.hungry(Fx::ratio(3, 10));   // peckish: no "I'M FULL!", and in no hurry to eat
  int n = 0;
  auto shot = [&](const std::string& moment) { shoot(b, dir, name + "_" + std::to_string(n++) + "_" + moment); };
  shot("full");
  for (int feed = 1; feed <= 4; ++feed) {
    knockKnock(b);
    runUntil(b, [&] { return dropped(b); }, 2000);
    b.run(200);
    shot("feed" + std::to_string(feed) + "_draining");
    b.run(1500);
    shot("feed" + std::to_string(feed) + "_left" + std::to_string(4 - feed));
  }
  knockKnock(b);
  runUntil(b, [&] { return b.count.presses > 0; }, 2000);
  b.run(80);
  shot("empty_knock_shake");
  b.run(400);
  shot("empty_thought_food");
  b.run(600);
  shot("empty_thought_no");
  b.run(800);
  shot("empty_thought_sad");
  b.run(2000);
  // The next pellet halfway back, then back.
  const uint32_t refill = b.creature().phenotype().habitat.refillTicks;
  b.dish->habitat().pantryTick -= refill / 2;
  b.run(200);
  shot("refilling_half");
  b.dish->habitat().pantryTick -= refill / 2;
  b.run(300);
  shot("refilled_one");
}

}  // namespace

TEST(Film, DumpWhenAsked) {
  const char* dirEnv = std::getenv("FEEDING_DUMP");
  if (!dirEnv) GTEST_SKIP() << "set FEEDING_DUMP to a directory to write the frames";
  const std::string dir = dirEnv;
  filmFed(dir, "grungo", std::nullopt, "adult");
  filmFed(dir, "puffkin", SpeciesId{2}, "adult");
  filmFed(dir, "mudge_hatchling", SpeciesId{5}, "hatchling");
  filmPantry(dir, "pantry");
  {
    Badge b(kHomeDown);
    b.hungry(Fx::zero());
    filmRefused(dir, "full_no_thanks", b, 700);
  }
  {
    Badge b(kHomeDown);
    b.creature().inject(driveChem(drive::sleepiness), Fx::one());
    b.creature().inject(chem::vision, Fx::zero());   // no foresight's glow round his shut eyes
    b.creature().force(action::sleep);
    b.run(1000);
    filmRefused(dir, "asleep_zzz", b, 800);
    EXPECT_TRUE(b.creature().body().asleep) << "the zzz film is of a sleeping pet";
  }
  {
    Badge b(kHomeDown);
    b.keepPantryFull = false;
    b.dish->habitat().pantry = 0;
    filmRefused(dir, "empty_pantry_shake", b, 300);
    b.run(900);
    shoot(b, dir, "empty_pantry");
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
