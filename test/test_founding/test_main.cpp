#include <gtest/gtest.h>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
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
constexpr Board kStranger{{0xd4, 0x05, 0x92, 0x77, 0x23, 0x7d}};   // one past board 237c's

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

}  // namespace

// ---- the table ------------------------------------------------------------------------------------

TEST(Table, NineAssignedDevicesKeepTheirSpeciesOnceEach) {
  const struct { uint8_t m4, m5; const char* species; } kDevices[] = {
      {0x23, 0x7c, "grungo"}, {0x1b, 0x3c, "mossby"}, {0x1b, 0x44, "puffkin"},
      {0x1b, 0x34, "gilly"},  {0x1c, 0xb0, "mothwick"}, {0x1b, 0x30, "mudge"},
      {0x1b, 0x40, "fizz"},   {0x22, 0x58, "hushwing"}, {0x22, 0x40, "tinker"}};
  std::set<uint8_t> species;
  std::set<std::vector<uint8_t>> macs;
  for (const auto& d : kDevices) {
    const uint8_t mac[6] = {0xd4, 0x05, 0x92, 0x77, d.m4, d.m5};
    ASSERT_TRUE(tableSpecies(mac)) << d.species;
    EXPECT_STREQ(speciesInfo(*tableSpecies(mac)).name, d.species);
    EXPECT_EQ(badgeSpecies(mac), *tableSpecies(mac));
    species.insert(tableSpecies(mac)->v);
    macs.insert(std::vector<uint8_t>(mac, mac + 6));
  }
  EXPECT_EQ(species.size(), 9u) << "the nine assigned devices each have a stable species";
  EXPECT_EQ(macs.size(), 9u) << "no MAC twice";
}

// Grungo is not the catch-all: a board off the table takes a species of the
// regular cast from a hash of its MAC, the same one every time.
TEST(Table, AnyOtherBoardTakesARegularSpeciesFromItsMac) {
  EXPECT_FALSE(tableSpecies(kStranger.mac));
  EXPECT_EQ(badgeSpecies(kStranger.mac), castSpecies(fnv1a(kStranger.mac, 6)));
  std::map<uint8_t, int> seen;
  for (uint32_t n = 0; n < 4096; ++n) {
    const uint8_t mac[6] = {0x02, 0x00, 0x00, uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
    const SpeciesId s = badgeSpecies(mac);
    EXPECT_EQ(s, castSpecies(fnv1a(mac, 6)));
    EXPECT_EQ(badgeSpecies(mac), s) << "deterministic";
    EXPECT_TRUE(inRegularCast(s)) << "never the octopus";
    ++seen[s.v];
  }
  size_t cast = 0;
  for (const SpeciesInfo& row : SPECIES) cast += inRegularCast(row.id);
  EXPECT_EQ(cast, 9u);
  EXPECT_EQ(seen.size(), cast) << "every regular species turns up";
  for (const auto& [id, n] : seen) EXPECT_GT(n, 4096 / int(cast) / 2) << speciesInfo(SpeciesId{id}).name;
  const uint8_t nearMiss[6] = {0xd4, 0x05, 0x92, 0x77, 0x1b, 0x3d};
  EXPECT_FALSE(tableSpecies(nearMiss)) << "the whole MAC, not its tail";
}

TEST(Table, TheRegularCastIsEveryRowButTheOctopusInRowOrder) {
  std::vector<uint8_t> cast;
  for (uint32_t n = 0; n < 9; ++n) cast.push_back(castSpecies(n).v);
  EXPECT_EQ(cast, (std::vector<uint8_t>{0, 1, 2, 3, 4, 5, 6, 7, 8}));
  EXPECT_EQ(castSpecies(9), castSpecies(0)) << "modulo the cast";
  EXPECT_FALSE(inRegularCast(species::octopus));
  for (uint32_t n = 0; n < 100000; n += 7) EXPECT_NE(castSpecies(n), species::octopus);
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

TEST(Founding, ABoardNotInTheTableFoundsTheSpeciesItsMacGives) {
  const SpeciesId s = badgeSpecies(kStranger.mac);
  MemStorage store;
  Dish d = boot(store, kStranger);
  EXPECT_EQ(d.boot(), Boot::Fresh);
  EXPECT_EQ(d.appearance().species, s);
  EXPECT_EQ(std::string(d.lineage().currentName()), lineName(s));
  EXPECT_EQ(std::get<Egg>(d.occupant()).genome().bytes(),
            foundingGenome(s, fnv1a(kStranger.mac, 6), paint::bandsFor(s)).bytes());
}

// The octopus is the one-off variant: a board founds one only when found.pin
// names it (or a table row would). No MAC, in the table or not, gets one.
TEST(Founding, TheOctopusComesOnlyByName) {
  for (uint32_t n = 0; n < 4096; ++n) {
    const uint8_t mac[6] = {0xd4, 0x05, 0x92, uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
    MemStorage store;
    EXPECT_NE(foundingSpecies(store, mac), species::octopus);
  }
  MemStorage store;
  ASSERT_EQ(debugverbs::found(store, "octopus"), nullptr);
  EXPECT_EQ(foundingSpecies(store, kStranger.mac), species::octopus);
  Dish d = boot(store, kStranger);
  EXPECT_EQ(d.appearance().species, species::octopus);
}

// Puck, the owner's own cat (cyber-puck's), is a one-off like the octopus:
// no MAC founds him by chance, and found.pin founds him by name.
TEST(Founding, PuckComesOnlyByName) {
  EXPECT_FALSE(inRegularCast(species::puck));
  for (uint32_t n = 0; n < 4096; ++n) {
    const uint8_t mac[6] = {0xd4, 0x05, 0x92, uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
    MemStorage store;
    EXPECT_NE(foundingSpecies(store, mac), species::puck);
  }
  for (uint32_t n = 0; n < 100000; n += 7) EXPECT_NE(castSpecies(n), species::puck);
  MemStorage store;
  ASSERT_EQ(debugverbs::found(store, "puck"), nullptr);
  EXPECT_EQ(foundingSpecies(store, kMossby.mac), species::puck) << "the pin outranks the table";
  Dish d = boot(store, kMossby);
  EXPECT_EQ(d.boot(), Boot::Fresh);
  EXPECT_EQ(d.appearance().species, species::puck);
  EXPECT_EQ(std::string(d.lineage().currentName()), "Puck");
}

// Board 237c is an ordinary table row: it founds its grungo as any badge
// founds its species.
TEST(Founding, Board237cFoundsItsTableGrungoLikeAnyBadge) {
  const Board k237c{{0xd4, 0x05, 0x92, 0x77, 0x23, 0x7c}};
  ASSERT_EQ(tableSpecies(k237c.mac), std::optional<SpeciesId>{kGrungo});
  MemStorage store;
  Dish d = boot(store, k237c);
  EXPECT_EQ(d.boot(), Boot::Fresh);
  EXPECT_EQ(d.appearance().species, kGrungo);
  EXPECT_EQ(std::get<Egg>(d.occupant()).genome().bytes(),
            foundingGenome(kGrungo, fnv1a(k237c.mac, 6), paint::bandsFor(kGrungo)).bytes());
  EXPECT_FALSE(predatesSpecies(std::get<Egg>(d.occupant()).genome())) << "a founder carries grungo's SpeciesGene";
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

TEST(NeedsFounding, APetThatLivesIsNeverFoundedAgain) {
  constexpr uint32_t kSeed = 15048;
  const uint32_t starterHash = starterGenome(kSeed).hash();
  MemStorage store;
  {
    Dish pet(store, kSeed, kLineageId);
    ASSERT_EQ(std::get<Egg>(pet.occupant()).genome().hash(), starterHash);
    ASSERT_TRUE(pet.flush());
  }
  EXPECT_FALSE(needsFounding(store));
  store.files.erase("snap.a"), store.files.erase("snap.b");
  EXPECT_FALSE(needsFounding(store)) << "the lineage alone";
  const auto before = store.files;
  EXPECT_NE(debugverbs::found(store, "gilly"), nullptr);
  EXPECT_EQ(store.files, before) << "the verb writes nothing on a pet's flash";
  ASSERT_TRUE(writeFoundPin(store, SpeciesId{4}));
  Dish again = boot(store, kMossby);
  EXPECT_EQ(std::get<Egg>(again.occupant()).genome().hash(), starterHash) << "a pin is ignored once a pet lives";
  EXPECT_EQ(std::string(again.lineage().currentName()), "Grungo");
}

TEST(NeedsFounding, TheCommittedV1PetWithAPinStaysHimself) {
  MemStorage store;
  store.files["snap.a"] = readFile(kV1);
  ASSERT_FALSE(store.files["snap.a"].empty()) << kV1 << " is missing (run from the project root)";
  ASSERT_TRUE(writeFoundPin(store, SpeciesId{7}));
  Dish d = boot(store, kMossby);
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
  for (const char* bad : {"grungus", "11", "255", "-1", ""}) {
    MemStorage store;
    EXPECT_NE(debugverbs::found(store, bad), nullptr) << "'" << bad << "'";
    EXPECT_TRUE(store.files.empty()) << "'" << bad << "'";
  }
}

TEST(Pin, AnythingButAWholePinIsNoPin) {
  const std::vector<uint8_t> good{'B', 'F', 'P', 'N', 1, 3};
  std::vector<std::vector<uint8_t>> bad{{}, {'B', 'F', 'P', 'N', 1}, {'B', 'F', 'P', 'X', 1, 3},
                                        {'B', 'F', 'P', 'N', 2, 3}, {'B', 'F', 'P', 'N', 1, 11},
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
