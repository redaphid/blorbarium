#include <gtest/gtest.h>
#include "blorb/squatters.h"

using blorb::BleAddr;
using blorb::Squatters;
using Act = Squatters::Act;

namespace {
const BleAddr kDaemon{{0x35, 0x1b, 0x77, 0x92, 0x05, 0xd4}, 0};
const BleAddr kPhone{{1, 2, 3, 4, 5, 6}, 1};
}  // namespace

TEST(SpeaksBlorb, KnownVerbsAndOtaWithOrWithoutAnId) {
  for (const char* yes : {"#1 HELLO", "HELLO", "#1a TIME 1760000000 0", "#2 OTA STATUS", "OTA STATUS", "#ffffff STATE"})
    EXPECT_TRUE(blorb::speaksBlorb(yes)) << yes;
  for (const char* no : {"o 3f9a0c", "?", "#3 FLY", "#zz HELLO", "#1234567 HELLO", "", "hello", "#1HELLO"})
    EXPECT_FALSE(blorb::speaksBlorb(no)) << no;
}

TEST(Squatters, ASilentCentralIsDroppedAfterTenSeconds) {
  Squatters s;
  EXPECT_EQ(s.connected(kDaemon, 1000), Act::None);
  EXPECT_EQ(s.poll(1000 + Squatters::kSpeakWithinMs - 1, false), Act::None);
  EXPECT_EQ(s.poll(1000 + Squatters::kSpeakWithinMs, false), Act::Evict);
  EXPECT_EQ(s.poll(1000 + Squatters::kSpeakWithinMs + 250, false), Act::None) << "once";
}

TEST(Squatters, OneThatSpokeTheProtocolStays) {
  Squatters s;
  s.connected(kPhone, 0);
  s.spoke();
  EXPECT_EQ(s.poll(10 * 60000, false), Act::None);
}

TEST(Squatters, AnUpdateInProgressIsNeverDroppedThisWay) {
  Squatters s;
  s.connected(kPhone, 0);
  EXPECT_EQ(s.poll(Squatters::kSpeakWithinMs * 5, true), Act::None);
}

TEST(Squatters, AnEvictedAddressIsRefusedForAMinuteAndSaidOnce) {
  Squatters s;
  s.connected(kDaemon, 0);
  const uint32_t out = Squatters::kSpeakWithinMs;
  ASSERT_EQ(s.poll(out, false), Act::Evict);
  EXPECT_EQ(s.connected(kDaemon, out + 900), Act::Refuse);
  EXPECT_EQ(s.connected(kDaemon, out + 1800), Act::RefuseQuietly);
  EXPECT_EQ(s.poll(out + 30000, false), Act::None) << "a refused connection is not timed";
  EXPECT_EQ(s.connected(kPhone, out + 2000), Act::None) << "only that address";
  EXPECT_EQ(s.connected(kDaemon, out + Squatters::kDenyMs), Act::None) << "once its refusal is up it may try again";
}

TEST(Squatters, ARefusalWindowIsKeptPerAddress) {
  Squatters s;
  const BleAddr a[5] = {{{1}, 0}, {{2}, 0}, {{3}, 0}, {{4}, 0}, {{5}, 0}};
  const uint32_t step = Squatters::kDenyMs / 4;
  for (uint32_t i = 0; i < 4; ++i) {
    s.connected(a[i], i * step);
    ASSERT_EQ(s.poll(i * step + Squatters::kSpeakWithinMs, false), Act::Evict);
  }
  // A fifth takes the slot that lapses soonest: the first one's.
  s.connected(a[4], 4 * step);
  const uint32_t out = 4 * step + Squatters::kSpeakWithinMs;
  ASSERT_EQ(s.poll(out, false), Act::Evict);
  EXPECT_EQ(s.connected(a[4], out + 1), Act::Refuse);
  EXPECT_EQ(s.connected(a[3], out + 1), Act::Refuse);
  EXPECT_EQ(s.connected(a[0], out + 1), Act::None) << "its refusal was up anyway";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
