#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "blorb/ble_id.h"
#include "blorb/ble_name.h"

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

// ---- the name (blorb/ble_name.h) ----------------------------------------------------------

namespace blename = blorb::blename;

std::string named(const char* pet, const char* species = "mossby") {
  char out[blename::kMax + 1];
  return blename::make(pet, species, out);
}

// A pet nobody renamed advertises exactly what every badge since 0.7.0 did.
TEST(BleName, ThePetsNameLowercase) {
  EXPECT_EQ(named("Mossby"), "mossby");
  EXPECT_EQ(named("Hushwing", "hushwing"), "hushwing");
  EXPECT_EQ(named("Grungo_V"), "grungo_v");
  EXPECT_EQ(named("Zorp-2"), "zorp-2");
}

TEST(BleName, ThePetsNameEvenWhenHisSpeciesSaysOtherwise) {
  EXPECT_EQ(named("Grungo", "puffkin"), "grungo") << "a 0.4.0 pet before his board adopts him";
  EXPECT_EQ(named("Blorbo", "fizz"), "blorbo");
}

// Only a board that could not read its pet (sealed) has no name: its species.
TEST(BleName, NoReadableNameIsTheSpecies) {
  EXPECT_EQ(named(nullptr, "tinker"), "tinker");
  EXPECT_EQ(named("", "tinker"), "tinker");
  EXPECT_EQ(named("!!  ", "tinker"), "tinker") << "nothing a rename allows";
}

TEST(BleName, NeverBlorbXxxx) {
  for (const char* pet : {"Mossby", "Grungo", "", "a"})
    EXPECT_NE(named(pet, "gilly").rfind("blorb-", 0), 0u) << pet;
}

// A rename takes up to 15 characters; all of them fit the scan response, and
// Improv's UUID stays beside any name of up to 11.
TEST(BleName, TheLongestNameFitsTheScanResponse) {
  EXPECT_EQ(named("Grungo_the_16th"), "grungo_the_16th");
  EXPECT_EQ(named("Grungo_the_sixteenth"), "grungo_the_sixt") << "cut at 15, never past the buffer";
  EXPECT_LE(blename::nameBytes(blename::kMax), blename::kScanMax);
  EXPECT_TRUE(blename::uuidFits(std::strlen("mothwick")));
  EXPECT_TRUE(blename::uuidFits(11));
  EXPECT_FALSE(blename::uuidFits(12));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
