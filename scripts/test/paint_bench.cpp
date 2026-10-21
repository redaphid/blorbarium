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
#include <cstring>
#include <thread>

#include "paint/parallel.h"
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
// The second core, as the board lends it (paint/parallel.h): "threads" runs
// half 0 on a thread of its own, "reverse" runs the halves here, 1 then 0. Either
// must draw the same bytes as none (both here, 0 then 1).
void onThread(paint::Half half, void* job) {
  std::thread other(half, job, 0);
  half(job, 1);
  other.join();
}
void reversed(paint::Half half, void* job) { half(job, 1), half(job, 0); }

uint32_t nowNs() { return uint32_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()); }

}  // namespace

int main(int argc, char** argv) {
  const int reps = argc > 1 ? std::atoi(argv[1]) : 3;
  if (argc > 2 && !std::strcmp(argv[2], "threads")) paint::parallel.run = onThread;
  if (argc > 2 && !std::strcmp(argv[2], "reverse")) paint::parallel.run = reversed;
  static paint::Canvas240 cv;
  const std::vector<blorb::Appearance> shots = paintbench::dishShots();
  const std::vector<paint::DuelScene> duels = paintbench::duelShots();

  uint64_t hash = 0, dishHash = 0;   // dishHash: the home frames alone, for a change that only moves the duel's
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
    dishHash = hash;
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
  // And the figure by species and stage: a heavy pet's frame, not the average's, is what the badge shows.
  double figBy[blorb::kSpeciesCount][5] = {}, figMax[blorb::kSpeciesCount][5] = {};
  int figN[blorb::kSpeciesCount][5] = {};
  for (const blorb::Appearance& a : shots) {
    const uint32_t fig0 = paint::phaseClock.spent[uint8_t(paint::Phase::Figure)];
    paint::draw(a, paint::packFor(a.species), cv, now);
    paint::drawBootHint(cv, paint::rimHint(a.boot));
    const double us = (paint::phaseClock.spent[uint8_t(paint::Phase::Figure)] - fig0) / 1000.0;
    const int sp = a.species.v % blorb::kSpeciesCount, st = a.kind == blorb::Appearance::Kind::Egg ? 0 : 1 + int(a.stage) % 4;
    figBy[sp][st] += us, ++figN[sp][st], figMax[sp][st] = us > figMax[sp][st] ? us : figMax[sp][st];
    now += 40;
  }
  paint::phaseClock.now = nullptr;
  std::printf("fig us by species (mean/max): egg, then each stage\n");
  for (int sp = 0; sp < blorb::kSpeciesCount; ++sp) {
    std::printf("  %-10s", blorb::SPECIES[sp].name);
    for (int st = 0; st < 5; ++st)
      if (figN[sp][st]) std::printf(" %6.1f/%-6.1f", figBy[sp][st] / figN[sp][st], figMax[sp][st]);
      else std::printf(" %13s", "-");
    std::printf("\n");
  }
  const char* names[] = {"bg", "fig", "rim", "text", "rings", "mask", "hint"};
  std::printf("phases us/frame:");
  for (int i = 0; i < int(paint::Phase::Count); ++i)
    std::printf(" %s=%.1f", names[i], paint::phaseClock.spent[i] / 1000.0 / double(shots.size()));
  std::printf("\n");
  std::printf("dish_frames=%zu us_per_frame=%.1f duel_frames=%zu us_per_frame=%.1f hash=%016llx\n", shots.size(),
              dishBest / double(shots.size()), duels.size(), duelBest / double(duels.size()), (unsigned long long)hash);
  std::printf("dish_hash=%016llx\n", (unsigned long long)dishHash);
  return 0;
}
