#include <gtest/gtest.h>
#include <cstring>
#include "blorb/fixed.h"
#include "blorb/ids.h"

using namespace blorb;

TEST(Fx, AddSaturatesInsteadOfWrapping) {
  Fx big{INT32_MAX - 1};
  EXPECT_EQ((big + Fx::one()).raw, INT32_MAX);
  EXPECT_EQ((-big - Fx::one() - Fx::one()).raw, INT32_MIN);
  EXPECT_EQ((Fx{INT32_MAX} * Fx{INT32_MAX}).raw, INT32_MAX);
}

TEST(Fx, UnitByteSpansZeroToOne) {
  EXPECT_EQ(Fx::unitByte(0), Fx::zero());
  EXPECT_EQ(Fx::unitByte(255), Fx::one());
  EXPECT_EQ(Fx::signedByte(128), Fx::zero());
  EXPECT_EQ(Fx::signedByte(0), -Fx::one());
}

TEST(Fx, SlowestDriveRateIsStillRepresentable) {
  Fx perTick = Fx::ratio(1, 6 * int32_t(kTicksPerHour));
  EXPECT_GT(perTick.raw, 50);
}

TEST(Q15, RoundTripKeepsWeightsWithinOneStep) {
  for (int32_t raw = -Fx::kOne; raw <= Fx::kOne; raw += 7919) {
    Fx back = fromQ15(toQ15(Fx{raw}));
    EXPECT_LE(std::abs(back.raw - raw), 512) << raw;
  }
  EXPECT_EQ(toQ15(Fx{5 * Fx::kOne}).v, 32767);
  EXPECT_EQ(toQ15(Fx{-5 * Fx::kOne}).v, -32768);
}

Fx stepFor(Fx c, Decay d, uint32_t ticks) {
  for (uint32_t t = 1; t <= ticks; ++t) c = applyDecay(c, d, t);
  return c;
}

TEST(Decay, ZeroByteNeverDecays) {
  Decay d = decayFromByte(0);
  EXPECT_EQ(stepFor(Fx::one(), d, 100000), Fx::one());
  EXPECT_EQ(applyDecayTicks(Fx::one(), d, 1u << 30), Fx::one());
}

// Half-life of byte b is 10 ticks * 2^((b-1)/12): byte 1 is one second.
uint32_t expectedHalfLife(uint8_t b) {
  double h = 10.0;
  for (int i = 1; i < b; ++i) h *= 1.0594630943592953;
  return uint32_t(h + 0.5);
}

TEST(Decay, HalfLifeByteHalvesInItsTickCount) {
  for (uint8_t b : {1, 2, 13, 25, 60, 100, 140, 160}) {
    uint32_t h = expectedHalfLife(b);
    Fx after = stepFor(Fx::one(), decayFromByte(b), h);
    EXPECT_NEAR(double(after.raw) / Fx::kOne, 0.5, 0.02) << "byte " << int(b) << " half-life " << h;
  }
}

TEST(Decay, LongHalfLivesNeverRoundToZeroDecay) {
  Decay d = decayFromByte(220);   // about 3.5 pet-days
  Fx after = stepFor(Fx::one(), d, kTicksPerHour);
  EXPECT_LT(after, Fx::one());
  EXPECT_GT(after, Fx::ratio(98, 100));
}

// Each stepped multiply truncates, which drifts from the closed form by at
// most 1 / (1 - keep) raw: under 190 raw (1.1e-5) because keep <= 1 - 1/128.
TEST(Decay, ClosedFormMatchesStepping) {
  for (uint8_t b : {1, 7, 40, 90, 130}) {
    Decay d = decayFromByte(b);
    for (uint32_t n : {0u, 1u, 64u, 1000u, 33333u}) {
      Fx stepped = stepFor(Fx::ratio(3, 4), d, n);
      Fx closed = applyDecayTicks(Fx::ratio(3, 4), d, n);
      EXPECT_NEAR(stepped.raw, closed.raw, 256 + stepped.raw / 4096) << "byte " << int(b) << " n " << n;
    }
  }
}

TEST(Rng, XoshiroMatchesReferenceSequence) {
  Rng r{{1, 2, 3, 4}};
  const uint32_t expected[] = {11520, 0, 5927040, 70819200, 2031721883, 1637235492};
  for (uint32_t e : expected) EXPECT_EQ(r.next(), e);
}

TEST(Rng, SeededMatchesSplitmixReference) {
  Rng r = Rng::seeded(42);
  const uint32_t expected[] = {360185622, 1627704702, 880513817, 1441732288, 3615975362u, 4028933122u};
  for (uint32_t e : expected) EXPECT_EQ(r.next(), e);
}

TEST(Rng, BelowAndUnitStayInRange) {
  Rng r = Rng::seeded(7);
  for (int i = 0; i < 10000; ++i) {
    EXPECT_LT(r.below(6), 6u);
    Fx u = r.unit();
    EXPECT_GE(u, Fx::zero());
    EXPECT_LT(u, Fx::one());
  }
  EXPECT_FALSE(Rng::seeded(1).chance(Fx::zero()));
}

TEST(Hash, Crc32MatchesTheStandardCheckValue) {
  EXPECT_EQ(crc32("123456789", 9), 0xCBF43926u);
}

TEST(Hash, Fnv1aMatchesTheStandardVector) {
  EXPECT_EQ(fnv1a("a", 1), 0xE40C292Cu);
  EXPECT_EQ(fnv1a("", 0), 0x811C9DC5u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
