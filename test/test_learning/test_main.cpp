#include <gtest/gtest.h>

#include <cstdio>

#include "blorb/dish.h"   // named here so PlatformIO's dependency finder links lib/blorb and lib/paint
#include "paint/sprite_pack.h"
#include "e2e/check.h"

using namespace e2e;

// The contract the comparisons rest on: the arms differ only by Treatment rows.
TEST(Harness, ArmsDifferOnlyByTheStimulus) {
  std::vector<Row> rows = {daily(Who::Both, 0, 0, 11 * kH, press(), 30 * kM), once(Who::Both, 12 * kH, shake())};
  EXPECT_EQ(run(rows, 14 * kH, Arm::Control, 3).finalHash, run(rows, 14 * kH, Arm::Treatment, 3).finalHash);
  rows.push_back(once(Who::Treatment, 13 * kH, shake()));
  EXPECT_NE(run(rows, 14 * kH, Arm::Control, 3).finalHash, run(rows, 14 * kH, Arm::Treatment, 3).finalHash);
  EXPECT_EQ(run(rows, 14 * kH, Arm::Treatment, 3).finalHash, run(rows, 14 * kH, Arm::Treatment, 3).finalHash);
}

// What a plainly cared-for grungo does, hour by hour: the backdrop every
// scenario's probe is read against. Prints; asserts only that he lives.
TEST(Harness, BaselineDay) {
  std::vector<Row> rows;
  for (uint64_t at : {11 * kH, 13 * kH + 30 * kM, 16 * kH, 18 * kH + 30 * kM, 21 * kH}) rows.push_back(daily(Who::Both, 0, 4, at, press()));
  for (int d = 1; d < 5; ++d) rows.push_back(once(Who::Both, d * kD + 11 * kH, shake()));
  for (int d = 1; d < 5; ++d) rows.push_back(once(Who::Both, d * kD + 11 * kH - kS, watch(40 * kS)));
  RunLog log = run(rows, 5 * kD, Arm::Control, 1);
  ASSERT_EQ(log.hatches.size(), 1u) << "fed only while he is awake, he lives five days";
  EXPECT_TRUE(log.deaths.empty());
  std::printf("hour  sleep  rest wand  eat slep fore call curl foll flee chas hopc   hunger bored need_t\n");
  for (int h = 0; h < 24; ++h) {
    uint64_t all = 0, asleep = 0, acts[kActionCount]{};
    int64_t drv[kDriveCount]{};
    for (int d = 1; d < 5; ++d) {
      all += log.creatureTicks[d][h];
      asleep += log.asleepTicks[d][h];
      for (size_t a = 0; a < kActionCount; ++a) acts[a] += log.actionTicks[d][h][a];
      for (size_t k = 0; k < kDriveCount; ++k) drv[k] += log.driveSum[d][h][k];
    }
    std::printf("%4d  %5.2f ", h, double(asleep) / all);
    for (size_t a = 0; a < kActionCount; ++a) std::printf(" %4.2f", double(acts[a]) / all);
    std::printf("   %6.0f %5.0f %6.0f\n", double(drv[0]) / all, double(drv[2]) / all, double(drv[7]) / all);
  }
  for (const Window& w : log.windows) {
    std::printf("shake at day %llu:", (unsigned long long)(w.at / kD));
    for (size_t i = 0; i < w.ticks.size(); i += 10)
      std::printf(" %s%s", detail::actionName(w.ticks[i].action), w.ticks[i].hop ? "^" : "");
    std::printf("  fear peak");
    int16_t peak = 0;
    for (const Tick& t : w.ticks) peak = std::max(peak, t.drive[4]);
    std::printf(" %d\n", peak);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
