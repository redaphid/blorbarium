// What the Dish's appearance() costs, a call, on a pet as the badge lives him:
// the `look=` of `DEBUG fps`, which the frame memo shows whole now that only
// the frames that changed are counted.
//   g++ -std=c++17 -O2 -Ilib/blorb/include -Ilib/blorb/src -Ilib/paint/include -Ipets -Ipets/grungo -Itest/support \
//       scripts/test/look_bench.cpp outputs/paint_bench/common/blorb_*.o -o outputs/look_bench && outputs/look_bench
#include <chrono>
#include <cstdio>
#include <variant>

#include "blorb/dish.h"
#include "links.h"
#include "mem_storage.h"
#include "traces.h"

int main() {
  using namespace blorb;
  using Clock = std::chrono::steady_clock;
  blorbtest::MemStorage store;
  blorbtest::NullLink link;
  Dish dish(store, 7, 1, DishOptions{});
  uint32_t ms = 0;
  for (int s = 0; s < 6 * 3600 && !std::holds_alternative<Creature>(dish.occupant()); ++s) blorbtest::runFor(dish, link, ms, 1000);
  double total = 0;
  int n = 0;
  for (int i = 0; i < 2000; ++i) {
    blorbtest::runFor(dish, link, ms, 100);
    const auto t0 = Clock::now();
    const Appearance a = dish.appearance();
    total += std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
    n += a.kind == Appearance::Kind::Creature;
  }
  std::printf("appearance() us a call: %.2f (creature frames %d)\n", total / 2000, n);
  return 0;
}
