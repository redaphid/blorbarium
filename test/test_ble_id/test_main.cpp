#include <gtest/gtest.h>

#include <vector>

#include "blorb/ble_id.h"

namespace bleid = blorb::bleid;

// The manufacturer data the page's chooser filter matches (web/public/ota.js
// chooserFilters): company 0xFFFF little-endian, then the MAC in print order.
TEST(BleId, PayloadIsCompanyThenTheMacAsPrinted) {
  const uint8_t mac[6] = {0xd4, 0x05, 0x92, 0x77, 0x1b, 0x30};   // mudge in defs/badges.def
  const auto p = bleid::payload(mac);
  EXPECT_EQ(std::vector<uint8_t>(p.begin(), p.end()),
            (std::vector<uint8_t>{0xff, 0xff, 0xd4, 0x05, 0x92, 0x77, 0x1b, 0x30}));
}

// Flags (3) + the UART service's 128-bit UUID (18) + the id (2 + 8): exactly 31.
TEST(BleId, FillsTheAdvertisementExactly) {
  EXPECT_EQ(bleid::kRecord, 10u);
  EXPECT_EQ(bleid::kAdvUsed, 31u);
  EXPECT_EQ(bleid::kAdvUsed, bleid::kAdvMax);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
