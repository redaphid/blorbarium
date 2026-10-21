// How long lib/paint takes to draw a frame, and a hash of every frame it drew,
// so a faster renderer can be proven to draw the same bytes. Host only: the
// board's own numbers come from `DEBUG fps` (src/main.cpp). Built and run by
// scripts/test/paint_bench.sh, which can build the same bench against another
// commit's lib/paint to compare hashes.
//
// The frames are scripts/test/paint_shots.h's: every species, egg to adult,
// with the boot hint and the idle bar over each, as the loop draws them; then
// every duel moment for a few pairs. Only the drawing is timed, not the hash.
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "paint/phases.h"
#include "paint/sprite_pack.h"
#include "paint_shots.h"

namespace {

uint64_t fnv(const paint::Canvas240& cv, uint64_t h) {
  const auto* b = reinterpret_cast<const uint8_t*>(cv.px);
  for (size_t i = 0; i < sizeof cv.px; ++i) h = (h ^ b[i]) * 0x100000001B3ull;
  return h;
}

using Clock = std::chrono::steady_clock;
double us(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::micro>(b - a).count(); }
uint32_t nowNs() { return uint32_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()); }

}  // namespace

int main(int argc, char** argv) {
  const int reps = argc > 1 ? std::atoi(argv[1]) : 3;
  static paint::Canvas240 cv;
  const std::vector<blorb::Appearance> shots = paintbench::dishShots();
  const std::vector<paint::DuelScene> duels = paintbench::duelShots();

  uint64_t hash = 0;
  double dishBest = 1e18, duelBest = 1e18;
  for (int r = 0; r < reps; ++r) {
    hash = 0xCBF29CE484222325ull;
    double dishUs = 0, duelUs = 0;
    uint32_t now = 0;
    for (const blorb::Appearance& a : shots) {
      const Clock::time_point t0 = Clock::now();
      paint::draw(a, paint::packFor(a.species), cv, now);
      paint::drawBootHint(cv, paint::rimHint(a.boot));
      paint::drawIdleBar(cv, uint16_t(now / 7 % 1001));
      dishUs += us(t0, Clock::now());
      hash = fnv(cv, hash);
      now += 40;
    }
    for (const paint::DuelScene& sc : duels) {
      const Clock::time_point t0 = Clock::now();
      paint::drawDuel(sc, paint::packFor(sc.me.look.species), paint::packFor(sc.them.look.species), cv, now);
      duelUs += us(t0, Clock::now());
      hash = fnv(cv, hash);
    }
    dishBest = dishUs < dishBest ? dishUs : dishBest;
    duelBest = duelUs < duelBest ? duelUs : duelBest;
  }
  // One more pass of the dish with the phase clock on: where a frame's time goes.
  paint::phaseClock.now = nowNs;
  uint32_t now = 0;
  for (const blorb::Appearance& a : shots) {
    paint::draw(a, paint::packFor(a.species), cv, now);
    paint::drawBootHint(cv, paint::rimHint(a.boot));
    now += 40;
  }
  paint::phaseClock.now = nullptr;
  const char* names[] = {"bg", "fig", "rim", "text", "rings", "mask", "hint"};
  std::printf("phases us/frame:");
  for (int i = 0; i < int(paint::Phase::Count); ++i)
    std::printf(" %s=%.1f", names[i], paint::phaseClock.spent[i] / 1000.0 / double(shots.size()));
  std::printf("\n");
  std::printf("dish_frames=%zu us_per_frame=%.1f duel_frames=%zu us_per_frame=%.1f hash=%016llx\n", shots.size(),
              dishBest / double(shots.size()), duels.size(), duelBest / double(duels.size()), (unsigned long long)hash);
  return 0;
}
