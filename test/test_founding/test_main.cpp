#include <gtest/gtest.h>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include "blorb/founding.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/dish.h"
#include "mem_storage.h"
#include "paint/sprite_pack.h"
#include "../../src/debug_verbs.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint32_t kGiftSeed = 15048;
constexpr uint32_t kGiftHash = 0xd30f00d1u;
constexpr uint64_t kLineageId = 0x9f31c2d04a7bull;
constexpr const char* kV1 = "test/fixtures/keepsake_v1.bin";

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

std::string lineName(SpeciesId s) {
  std::string n = speciesInfo(s).name;
  n[0] = char(std::toupper(static_cast<unsigned char>(n[0])));
  return n;
}

// A board by its MAC: one of the batch, or one off the same reel that is not.
struct Board { uint8_t mac[6]; };
constexpr Board kMossby{{0xd4, 0x05, 0x92, 0x77, 0x1b, 0x3c}};
constexpr Board kStranger{{0xd4, 0x05, 0x92, 0x77, 0x23, 0x7c}};   // ends 23:7c, as the gift device does

// What src/main.cpp's setup() does with a board's storage before the Dish.
Dish boot(MemStorage& store, const Board& board) {
  const uint32_t seed = fnv1a(board.mac, 6);
  DishOptions o;
  std::optional<Genome> founder;
  if (needsFounding(store)) {
    const SpeciesId s = foundingSpecies(store, board.mac);
    founder = foundingGenome(s, seed, paint::bandsFor(s));
    o.founder = &*founder;
  }
  return Dish(store, seed, kLineageId, o);
}

// The gift device: its pet came from the starter, long before founders, and
// is known by its seed. A board with a pet never reaches the founding.
Dish bootGift(MemStorage& store) {
  const uint8_t mac[6] = {0xd4, 0x05, 0x92, 0x77, 0x23, 0x7c};
  DishOptions o;
  std::optional<Genome> founder;
  if (needsFounding(store)) {
    const SpeciesId s = foundingSpecies(store, mac);
    founder = foundingGenome(s, kGiftSeed, paint::bandsFor(s));
    o.founder = &*founder;
  }
  return Dish(store, kGiftSeed, kLineageId, o);
}

}  // namespace

// ---- the table ------------------------------------------------------------------------------------

TEST(Table, EachBadgeIsItsSpeciesAndTheEightAreOneOfEach) {
  const struct { uint8_t m4, m5; const char* species; } kBatch[] = {
      {0x1b, 0x3c, "mossby"}, {0x1b, 0x44, "puffkin"}, {0x1b, 0x34, "gilly"},    {0x1c, 0xb0, "mothwick"},
      {0x1b, 0x30, "mudge"},  {0x1b, 0x40, "fizz"},    {0x22, 0x58, "hushwing"}, {0x22, 0x40, "tinker"}};
  std::set<uint8_t> seen;
  for (const auto& b : kBatch) {
    const uint8_t mac[6] = {0xd4, 0x05, 0x92, 0x77, b.m4, b.m5};
    ASSERT_TRUE(tableSpecies(mac)) << b.species;
    EXPECT_STREQ(speciesInfo(*tableSpecies(mac)).name, b.species);
    EXPECT_EQ(badgeSpecies(mac), *tableSpecies(mac));
    seen.insert(tableSpecies(mac)->v);
  }
  EXPECT_EQ(seen.size(), 8u);
  EXPECT_EQ(seen.count(kGrungo.v), 0u) << "grungo is the gift device's";
}

TEST(Table, AnyOtherBoardTakesASpeciesFromItsMacsHashAcrossAllNine) {
  EXPECT_FALSE(tableSpecies(kStranger.mac));
  EXPECT_EQ(badgeSpecies(kStranger.mac).v, fnv1a(kStranger.mac, 6) % kSpeciesCount);
  EXPECT_STREQ(speciesInfo(badgeSpecies(kStranger.mac)).name, "fizz") << "tests/test_flash_batch.py expects the same";
  std::set<uint8_t> species;
  for (uint32_t n = 0; n < 4096; ++n) {
    const uint8_t mac[6] = {0x02, 0x00, 0x00, uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
    const SpeciesId s = badgeSpecies(mac);
    EXPECT_EQ(badgeSpecies(mac), s) << "the same board, the same species";
    species.insert(s.v);
  }
  EXPECT_EQ(species.size(), kSpeciesCount);
  const uint8_t nearMiss[6] = {0xd4, 0x05, 0x92, 0x77, 0x1b, 0x3d};
  EXPECT_FALSE(tableSpecies(nearMiss)) << "the whole MAC, not its tail";
}

// ---- who is founded --------------------------------------------------------------------------------

TEST(Founding, AFreshBadgeFoundsItsTableSpeciesWithNoChoice) {
  MemStorage store;
  Dish d = boot(store, kMossby);
  EXPECT_EQ(d.boot(), Boot::Fresh);
  EXPECT_EQ(d.appearance().species, species::mossby);
  EXPECT_EQ(std::string(d.lineage().currentName()), "Mossby");
  const uint32_t seed = fnv1a(kMossby.mac, 6);
  EXPECT_EQ(std::get<Egg>(d.occupant()).genome().bytes(),
            foundingGenome(species::mossby, seed, paint::bandsFor(species::mossby)).bytes());
  MemStorage again;
  EXPECT_EQ(std::get<Egg>(boot(again, kMossby).occupant()).genome().hash(),
            std::get<Egg>(d.occupant()).genome().hash())
      << "a board founds the same pet every time";
}

TEST(Founding, ThePinIsTheOwnersOverrideOverTheTable) {
  for (const SpeciesInfo& row : SPECIES) {
    MemStorage store;
    ASSERT_TRUE(writeFoundPin(store, row.id));
    EXPECT_EQ(foundingSpecies(store, kMossby.mac), row.id) << row.name;
    Dish d = boot(store, kMossby);
    EXPECT_EQ(d.boot(), Boot::Fresh) << row.name;
    EXPECT_EQ(d.appearance().species, row.id) << row.name;
    EXPECT_EQ(std::string(d.lineage().currentName()), lineName(row.id)) << row.name;
    Dish again = boot(store, kMossby);
    EXPECT_EQ(again.boot(), Boot::Resumed) << row.name;
    EXPECT_EQ(std::string(again.lineage().currentName()), lineName(row.id)) << "the Founding entry on the log";
  }
}

TEST(Founding, ABoardNotInTheTableFoundsItsHashedSpecies) {
  MemStorage store;
  Dish d = boot(store, kStranger);
  EXPECT_EQ(d.appearance().species, badgeSpecies(kStranger.mac));
  EXPECT_EQ(std::string(d.lineage().currentName()), lineName(badgeSpecies(kStranger.mac)));
}

// ---- an existing pet is never founded again ------------------------------------------------------

TEST(NeedsFounding, AnyKeepsakeOrLineageMeansAPetLivesHere) {
  for (const char* f : {"snap.a", "snap.b", "rescue/snap.a.0", "rescue/snap.b.0", "lineage.log", "lineage.tmp"}) {
    MemStorage store;
    store.files[f] = {0xFF};
    EXPECT_FALSE(needsFounding(store)) << "one stray byte of " << f;
  }
  MemStorage store;
  store.files["friend.egg"] = {1, 2, 3};
  store.files["found.pin"] = {1};
  EXPECT_TRUE(needsFounding(store)) << "no pet file, no pet";
}

TEST(NeedsFounding, TheGiftDevicesGrungoIsNeverFoundedAgain) {
  MemStorage store;
  {
    Dish gift(store, kGiftSeed, kLineageId);
    ASSERT_EQ(std::get<Egg>(gift.occupant()).genome().hash(), kGiftHash);
    ASSERT_TRUE(gift.flush());
  }
  EXPECT_FALSE(needsFounding(store));
  store.files.erase("snap.a"), store.files.erase("snap.b");
  EXPECT_FALSE(needsFounding(store)) << "the lineage alone";
  const auto before = store.files;
  EXPECT_NE(debugverbs::found(store, "gilly"), nullptr);
  EXPECT_EQ(store.files, before) << "the verb writes nothing on a pet's flash";
  ASSERT_TRUE(writeFoundPin(store, SpeciesId{4}));
  Dish again = bootGift(store);
  EXPECT_EQ(std::get<Egg>(again.occupant()).genome().hash(), kGiftHash) << "a pin is ignored once a pet lives";
  EXPECT_EQ(std::string(again.lineage().currentName()), "Grungo");
}

TEST(NeedsFounding, TheCommittedV1PetWithAPinStaysHimself) {
  MemStorage store;
  store.files["snap.a"] = readFile(kV1);
  ASSERT_FALSE(store.files["snap.a"].empty()) << kV1 << " is missing (run from the project root)";
  ASSERT_TRUE(writeFoundPin(store, SpeciesId{7}));
  Dish d = bootGift(store);
  EXPECT_EQ(d.boot(), Boot::Resumed);
  EXPECT_STREQ(d.settings().name, "Grungo_IV");
}

// ---- found.pin ---------------------------------------------------------------------------------------

TEST(Pin, TheVerbPinsTheSpeciesAFreshFoundingFounds) {
  MemStorage store;
  ASSERT_EQ(debugverbs::found(store, "gilly"), nullptr);
  SpeciesId gilly;
  ASSERT_TRUE(debugverbs::speciesByName("gilly", gilly));
  EXPECT_EQ(foundingSpecies(store, kMossby.mac), gilly);
  EXPECT_EQ(store.files["found.pin"], (std::vector<uint8_t>{'B', 'F', 'P', 'N', 1, gilly.v})) << "the pin's bytes";
  ASSERT_EQ(debugverbs::found(store, "2"), nullptr) << "by number too";
  EXPECT_EQ(foundingSpecies(store, kMossby.mac), SpeciesId{2}) << "the last pin wins";
}

TEST(Pin, OffGivesTheTableBackAndDeletesNothing) {
  MemStorage store;
  ASSERT_EQ(debugverbs::found(store, "off"), nullptr) << "nothing pinned is already off";
  ASSERT_EQ(debugverbs::found(store, "gilly"), nullptr);
  ASSERT_EQ(debugverbs::found(store, "off"), nullptr);
  EXPECT_EQ(foundingSpecies(store, kMossby.mac), species::mossby);
  EXPECT_EQ(store.files.count("found.off"), 1u);
}

TEST(Pin, ANameOrNumberWithNoRowIsRefusedAndWritesNothing) {
  for (const char* bad : {"grungus", "9", "255", "-1", ""}) {
    MemStorage store;
    EXPECT_NE(debugverbs::found(store, bad), nullptr) << "'" << bad << "'";
    EXPECT_TRUE(store.files.empty()) << "'" << bad << "'";
  }
}

TEST(Pin, AnythingButAWholePinIsNoPin) {
  const std::vector<uint8_t> good{'B', 'F', 'P', 'N', 1, 3};
  std::vector<std::vector<uint8_t>> bad{{}, {'B', 'F', 'P', 'N', 1}, {'B', 'F', 'P', 'X', 1, 3},
                                        {'B', 'F', 'P', 'N', 2, 3}, {'B', 'F', 'P', 'N', 1, 9},
                                        {'B', 'F', 'P', 'N', 1, 3, 0}};
  MemStorage store;
  store.files["found.pin"] = good;
  ASSERT_EQ(readFoundPin(store), SpeciesId{3});
  for (const std::vector<uint8_t>& b : bad) {
    store.files["found.pin"] = b;
    EXPECT_FALSE(readFoundPin(store)) << b.size() << " bytes";
    EXPECT_EQ(foundingSpecies(store, kMossby.mac), species::mossby);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
