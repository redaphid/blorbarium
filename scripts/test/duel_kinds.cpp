// What each prop kind costs to draw in the paint bench's duel frames
// (paint_shots.h's duelShots): every frame drawn from its staging with and
// without that kind, best of a few runs. Host only, a ranking, not the board's
// number; built and run by scripts/test/duel_kinds.sh.
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "paint/phases.h"
#include "paint_shots.h"

namespace {

using Clock = std::chrono::steady_clock;
paint::Canvas240 cv;
constexpr int kAll = -1, kNone = -2;

// us a frame for the duel frames drawn with every prop but `drop` (kAll: every prop, kNone: no props).
double run(const std::vector<paint::DuelScene>& d, const std::vector<paint::DuelStaging>& st, int drop, int reps) {
  double best = 1e18;
  for (int r = 0; r < reps; ++r) {
    const Clock::time_point t0 = Clock::now();
    for (size_t i = 0; i < d.size(); ++i) {
      paint::DuelStaging s = st[i];
      int n = 0;
      for (int k = 0; k < s.propCount; ++k)
        if (drop != kNone && int(s.props[k].kind) != drop) s.props[n++] = s.props[k];
      s.propCount = uint8_t(n);
      paint::drawStaged(d[i], s, paint::packFor(d[i].me.look.species), paint::packFor(d[i].them.look.species), cv);
    }
    const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count() / double(d.size());
    best = us < best ? us : best;
  }
  return best;
}

}  // namespace

int main(int argc, char** argv) {
  const int reps = argc > 1 ? std::atoi(argv[1]) : 15;
  const std::vector<paint::DuelScene> d = paintbench::duelShots();
  std::vector<paint::DuelStaging> st;
  for (const paint::DuelScene& s : d)
    st.push_back(paint::stageDuel(s, paint::packFor(s.me.look.species), paint::packFor(s.them.look.species)));
  const double all = run(d, st, kAll, reps), none = run(d, st, kNone, reps);
  std::printf("duel_frames=%zu us_per_frame=%.1f without_props=%.1f props=%.1f\n", d.size(), all, none, all - none);
  static const char* const kNames[] = {"Heart", "Pip",    "Stance", "Power", "Tier",   "Element", "Trophy",
                                       "Debt",  "Plaster", "Burst", "Aura",  "Countdown", "Arrow", "Tick",
                                       "Dots",  "Strike", "Timer",  "Stars", "Glow",   "Button",  "Plea",
                                       "Reticle", "Lock"};
  for (int k = 0; k < int(sizeof kNames / sizeof kNames[0]); ++k) {
    int n = 0;
    for (const paint::DuelStaging& s : st)
      for (int i = 0; i < s.propCount; ++i) n += int(s.props[i].kind) == k;
    if (n) std::printf("  %-10s n=%3d %7.1f us/frame\n", kNames[k], n, all - run(d, st, k, reps));
  }
  return 0;
}
