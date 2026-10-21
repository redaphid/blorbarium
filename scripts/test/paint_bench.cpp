// How long lib/paint takes to draw a frame, and a hash of every frame it drew,
// so a faster renderer can be proven to draw the same bytes. Host only: the
// board's own numbers come from `DEBUG fps` (src/main.cpp). Built and run by
// scripts/test/paint_bench.sh, which can build the same bench against another
// commit's lib/paint to compare hashes.
//
// A pet of each species is hatched and lived for a while; every 100 ms tick's
// Appearance is drawn, as the loop draws it, with the boot hint and the idle
// bar over it, and now and then a thought crossing, a hang filling, fight
// mode's lobby or the sealed amber. Then every duel moment for a few pairs.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <variant>
#include <vector>

#include "blorb/dish.h"
#include "blorb/registry.h"
#include "duel_scenes.h"
#include "links.h"
#include "mem_storage.h"
#include "paint/duel.h"
#include "paint/sprite_pack.h"
#include "traces.h"

using namespace blorb;

namespace {

uint64_t fnv(const paint::Canvas240& cv, uint64_t h) {
  const auto* b = reinterpret_cast<const uint8_t*>(cv.px);
  for (size_t i = 0; i < sizeof cv.px; ++i) h = (h ^ b[i]) * 0x100000001B3ull;
  return h;
}

struct Timed { double us = 0; size_t frames = 0; };

}  // namespace

int main(int argc, char** argv) {
  const int reps = argc > 1 ? std::atoi(argv[1]) : 3;
  static paint::Canvas240 cv;
  std::vector<Appearance> shots;
  for (uint8_t sp = 0; sp < kSpeciesCount; ++sp) {
    blorbtest::MemStorage store;
    blorbtest::NullLink link;
    Dish dish(store, 7 + sp, 1, DishOptions{});
    uint32_t ms = 0;
    for (int s = 0; s < 6 * 3600 && !std::holds_alternative<Creature>(dish.occupant()); ++s) {
      blorbtest::runFor(dish, link, ms, 1000);
      if (s % 60 == 0) shots.push_back(dish.appearance());   // the egg, too
    }
    for (int i = 0; i < 400; ++i) {
      blorbtest::runFor(dish, link, ms, 100);
      Appearance a = dish.appearance();
      a.species = SpeciesId{sp};
      const int k = i % 100;
      if (k < 20) {   // a thought crossing, as the marquee draws it
        a.thinking = true;
        std::snprintf(a.line, sizeof a.line, "I am thinking about pellets");
        a.thoughtPhase = Fx{int32_t(int64_t(k) * Fx::kOne / 20)};
      } else if (k < 40) {   // a hang filling up, to full
        a.hangCharge = Fx{int32_t(int64_t(k - 20) * Fx::kOne / 19)};
      } else if (k < 50) {   // fight mode, searching
        a.lobby = true;
        a.lobbyTicksLeft = uint16_t(3000 - i);
      } else if (k < 53) {
        a.sealed = true;
      }
      shots.push_back(a);
    }
  }
  std::vector<paint::DuelScene> duels;
  for (const blorbtest::DuelMoment& m : blorbtest::kDuelMoments)
    for (uint8_t sp = 0; sp + 1 < kSpeciesCount; sp += 3)
      duels.push_back(blorbtest::duelScene(m.name, SpeciesId{sp}, SpeciesId{uint8_t(sp + 1)}));

  uint64_t hash = 0;
  Timed dish{1e18, 0}, duel{1e18, 0};
  for (int r = 0; r < reps; ++r) {
    hash = 0xCBF29CE484222325ull;
    auto t0 = std::chrono::steady_clock::now();
    uint32_t now = 0;
    for (const Appearance& a : shots) {
      paint::draw(a, paint::packFor(a.species), cv, now);
      paint::drawBootHint(cv, paint::rimHint(a.boot));
      paint::drawIdleBar(cv, uint16_t(now / 7 % 1001));
      hash = fnv(cv, hash);
      now += 40;
    }
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    if (us < dish.us) dish = {us, shots.size()};
    t0 = std::chrono::steady_clock::now();
    for (const paint::DuelScene& sc : duels) {
      paint::drawDuel(sc, paint::packFor(sc.me.look.species), paint::packFor(sc.them.look.species), cv, now);
      hash = fnv(cv, hash);
    }
    us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    if (us < duel.us) duel = {us, duels.size()};
  }
  std::printf("dish_frames=%zu us_per_frame=%.1f duel_frames=%zu us_per_frame=%.1f hash=%016llx\n", dish.frames,
              dish.us / double(dish.frames), duel.frames, duel.us / double(duel.frames), (unsigned long long)hash);
  return 0;
}
