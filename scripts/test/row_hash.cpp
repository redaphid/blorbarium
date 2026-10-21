// The panel's row hash (src/hw/board_lcd128.h, panelrows::of) against the
// changes it must never miss: a row that changed only in the high bits of its
// words (red, and red at an odd x especially), one word or two. A miss is a
// stale stripe on the panel for up to 64 frames.
//   g++ -O2 -std=c++17 scripts/test/row_hash.cpp -o outputs/row_hash && outputs/row_hash [trials]
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>

constexpr int CANVAS_W = 240;

uint32_t of(const uint16_t* p) {   // as panelrows::of
  uint32_t h = 2166136261u;
  for (int x = 0; x < CANVAS_W; x += 2) {
    h = (h ^ (uint32_t(p[x]) | uint32_t(p[x + 1]) << 16)) * 16777619u;
    h ^= h >> 15;
  }
  return h;
}

int main(int argc, char** argv) {
  const long trials = argc > 1 ? std::atol(argv[1]) : 2000000;
  std::mt19937 rng(7);
  uint16_t row[CANVAS_W], was[CANVAS_W];
  long misses[3] = {0, 0, 0};
  for (long t = 0; t < trials; ++t) {
    for (int x = 0; x < CANVAS_W; ++x) row[x] = was[x] = uint16_t(rng());
    const uint32_t before = of(row);
    const int kind = int(t % 3);
    const int a = int(rng() % CANVAS_W), b = int(rng() % CANVAS_W);
    if (kind == 0) row[a | 1] ^= uint16_t(1u << (11 + rng() % 5));    // one red bit at an odd x
    else if (kind == 1) row[a] ^= 0x8000, row[b] ^= 0x8000;           // two red MSBs
    else row[a] ^= uint16_t((rng() | 1u) << 8);                      // any high byte change
    bool changed = false;
    for (int x = 0; x < CANVAS_W; ++x) changed |= row[x] != was[x];
    if (changed && of(row) == before) ++misses[kind];
  }
  std::printf("trials=%ld misses: odd-x red bit %ld, two red MSBs %ld, high byte %ld\n", trials, misses[0], misses[1],
              misses[2]);
  return misses[0] + misses[1] + misses[2] ? 1 : 0;
}
