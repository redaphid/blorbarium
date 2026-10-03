#include <gtest/gtest.h>
#include "blorb/fixed.h"

using blorb::Fx;

TEST(Fx, AddSaturatesInsteadOfWrapping) {
  Fx big{INT32_MAX - 1};
  EXPECT_EQ((big + Fx::one()).raw, INT32_MAX);
  EXPECT_EQ((-big - Fx::one() - Fx::one()).raw, INT32_MIN);
}

TEST(Fx, UnitByteSpansZeroToOne) {
  EXPECT_EQ(Fx::unitByte(0), Fx::zero());
  EXPECT_EQ(Fx::unitByte(255), Fx::one());
  EXPECT_EQ(Fx::signedByte(128), Fx::zero());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
