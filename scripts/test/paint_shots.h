#pragma once
// The frames the paint bench draws (scripts/test/paint_bench.cpp, and the
// board build of it, paint_bench_s3.cpp, through paint_shots_dump.cpp): a pet
// of each species hatched and lived for a while, every 100 ms tick's
// Appearance as the loop draws it, and now and then a thought crossing, a hang
// filling, fight mode's lobby or the sealed amber. Then every duel moment for
// a few pairs.
#include <cstdio>
#include <variant>
#include <vector>

#include "blorb/dish.h"
#include "blorb/registry.h"
#include "duel_scenes.h"
#include "links.h"
#include "mem_storage.h"
#include "paint/duel.h"
#include "traces.h"

namespace paintbench {

inline std::vector<blorb::Appearance> dishShots() {
  using namespace blorb;
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
  return shots;
}

inline std::vector<paint::DuelScene> duelShots() {
  using blorb::SpeciesId;
  std::vector<paint::DuelScene> duels;
  for (const blorbtest::DuelMoment& m : blorbtest::kDuelMoments)
    for (uint8_t sp = 0; sp + 1 < blorb::kSpeciesCount; sp += 3)
      duels.push_back(blorbtest::duelScene(m.name, SpeciesId{sp}, SpeciesId{uint8_t(sp + 1)}));
  return duels;
}

}  // namespace paintbench
