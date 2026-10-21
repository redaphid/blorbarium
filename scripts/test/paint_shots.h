#pragma once
// The frames the paint bench draws (scripts/test/paint_bench.cpp, and the
// board build of it, paint_bench_s3.cpp, through paint_shots_dump.cpp): a pet
// of each species hatched and lived for a while, every 100 ms tick's
// Appearance as the loop draws it, and now and then a thought crossing, a hang
// filling, fight mode's lobby or the sealed amber; each grown, as an adult and
// an elder; then the heavy frames (heavyShots). Then every duel moment for a
// few pairs.
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

// What the lived frames lack, from each species' own frame: adult, elder and
// baby; at 55, 100 and 130%; with 0, 2 and 4 scars; mottled not at all, some
// and wholly; standing, hopping, away from home and crossfading faces. 9,720
// frames (from the optimisation review's harness): every scarred pixel and
// the costliest pets in the bench's hash.
inline void heavyShots(const blorb::Appearance (&from)[blorb::kSpeciesCount], std::vector<blorb::Appearance>& shots) {
  using namespace blorb;
  const Stage stages[] = {Stage::Adult, Stage::Elder, Stage::Baby};
  for (uint8_t sp = 0; sp < kSpeciesCount; ++sp)
    for (Stage st : stages)
      for (int scale : {55, 100, 130})
        for (int scars = 0; scars <= 4; scars += 2)
          for (int mottle : {0, 90, 255}) {
            Appearance a = from[sp];
            a.kind = Appearance::Kind::Creature;
            a.species = SpeciesId{sp};
            a.stage = st;
            a.scalePct = uint8_t(scale);
            a.markCount = 1;
            a.marks[0] = {0, uint8_t(mottle), Tint{int8_t(sp * 7), 128, 128}};
            a.scars.count = uint8_t(scars);
            for (int i = 0; i < scars; ++i) {
              Scar& s = a.scars.mark[i];
              s.kind = Scar::Kind((i + sp) % 5);
              s.x = uint8_t(37 * i + sp * 11);
              s.y = uint8_t(61 * i + 13);
              s.fade = uint8_t(96 + 40 * i);
            }
            for (int t = 0; t < 12; ++t) {
              Appearance b = a;
              b.poseTick = uint16_t(t * 7 + sp);
              if (t % 4 == 1) {   // a hop
                b.reflexActive = true;
                b.reflex = reflex::hop;
                b.reflexPhase = Fx::ratio(t, 12);
                b.reflexStrength = Fx::ratio(3, 4);
              }
              if (t % 4 == 2) b.at.x = Fx::ratio(t - 6, 10), b.at.y = Fx::ratio(3 - t % 5, 9);   // away from home
              if (t % 4 == 3) b.exprTicks = 1, b.previous = expr::alarmed;                        // a face crossfading
              shots.push_back(b);
            }
          }
}

inline std::vector<blorb::Appearance> dishShots() {
  using namespace blorb;
  std::vector<Appearance> shots;
  Appearance lived[kSpeciesCount];
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
    // The same pet grown: 120 of those ticks again as an adult at full size,
    // 40 as an elder. Living him there takes days of ticks, and the frames
    // only need his stage's art at his size (a heavy pet is a big one).
    const size_t baby = shots.size() - 400;
    lived[sp] = shots[baby + 200];
    for (int i = 0; i < 160; ++i) {
      Appearance a = shots[baby + 240 + size_t(i)];
      a.stage = i < 120 ? Stage::Adult : Stage::Elder;
      a.scalePct = 100;
      shots.push_back(a);
    }
  }
  heavyShots(lived, shots);
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
