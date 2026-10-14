#include <gtest/gtest.h>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "blorb/founding.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/dish.h"
#include "mem_storage.h"
#include "../../src/debug_verbs.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint32_t kSeed = 0x00C0FFEEu;
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

// A kid's hand on a chooser at 50 Hz, the device lying flat.
struct Hand {
  FoundingChooser& chooser;
  uint32_t ms = 1000;
  std::vector<SpeciesId> kept;

  void step(BodySample b) {
    b.az = 1000;
    if (std::optional<SpeciesId> s = chooser.sample(b, ms)) kept.push_back(*s);
    ms += 20;
  }
  void idle(uint32_t forMs) { for (const uint32_t end = ms + forMs; ms < end;) step({}); }
  void button(uint32_t downMs) {
    BodySample b;
    b.buttonDown = true;
    for (const uint32_t end = ms + downMs; ms < end;) step(b);
    idle(200);
  }
  void press() { button(150); }
  void hold() { button(1300); }
  void tap(uint8_t code) {
    BodySample b;
    b.tapCode = code;
    step(b);
    idle(500);
  }
};

Nest threeEggs() {
  MemStorage empty;
  return *nestFor(empty, kSeed);
}

// What src/main.cpp's setup() does with the nest, for a nest that needs no hand.
Dish boot(MemStorage& store, uint32_t seed) {
  DishOptions o;
  std::optional<Genome> founder;
  if (std::optional<Nest> nest = nestFor(store, seed))
    if (std::optional<SpeciesId> pick = FoundingChooser(*nest).sample({}, 0)) founder = starterGenomeOf(*pick, seed);
  if (founder) o.founder = &*founder;
  return Dish(store, seed, kLineageId, o);
}

}  // namespace

// ---- the nest ------------------------------------------------------------------------------------

TEST(Nest, AFreshDeviceOffersOneEggPerElementRolledFromItsSeed) {
  MemStorage store;
  ASSERT_TRUE(needsFounding(store));
  std::optional<Nest> nest = nestFor(store, kSeed);
  ASSERT_TRUE(nest);
  ASSERT_EQ(nest->count, kCycle);
  const FoundingNest rolled = foundingNest(kSeed);
  for (uint8_t e = 0; e < kCycle; ++e) {
    EXPECT_EQ(nest->eggs[e], rolled.eggs[e]);
    EXPECT_EQ(uint8_t(speciesInfo(nest->eggs[e]).element), e);
  }
  EXPECT_TRUE(store.files.empty()) << "asking writes nothing";
}

TEST(Chooser, APressLightsTheNextEggAndAHoldKeepsTheLitOne) {
  FoundingChooser c(threeEggs());
  Hand h{c};
  h.idle(500);
  EXPECT_EQ(c.lit(), 0);
  h.press();
  EXPECT_EQ(c.lit(), 1);
  h.press();
  EXPECT_EQ(c.lit(), 2);
  h.press();
  EXPECT_EQ(c.lit(), 0) << "past the last egg, the first again";
  h.press();
  ASSERT_TRUE(h.kept.empty()) << "a press only moves the glint";
  h.hold();
  ASSERT_EQ(h.kept.size(), 1u);
  EXPECT_EQ(h.kept[0], c.nest().eggs[1]);
  h.idle(FoundingChooser::kTimeoutMs * 2);
  h.hold();
  EXPECT_EQ(h.kept.size(), 1u) << "a nest is chosen once";
}

TEST(Chooser, AKnockLightsTheNextEggAndADoubleKnockKeepsTheLitOne) {
  FoundingChooser c(threeEggs());
  Hand h{c};
  h.tap(1);
  h.tap(1);
  EXPECT_EQ(c.lit(), 2);
  ASSERT_TRUE(h.kept.empty());
  h.tap(2);
  ASSERT_EQ(h.kept.size(), 1u);
  EXPECT_EQ(h.kept[0], c.nest().eggs[2]);
}

TEST(Chooser, NobodyChoosingForAMinuteKeepsTheFirstEgg) {
  FoundingChooser c(threeEggs());
  Hand h{c};
  h.idle(FoundingChooser::kTimeoutMs - 20);
  ASSERT_TRUE(h.kept.empty()) << "not before the minute";
  h.idle(40);
  ASSERT_EQ(h.kept.size(), 1u);
  EXPECT_EQ(h.kept[0], c.nest().eggs[0]);
}

TEST(Chooser, LightingAnotherEggStartsTheMinuteAgainAndTheTimeoutStillKeepsTheFirst) {
  FoundingChooser c(threeEggs());
  Hand h{c};
  h.idle(50000);
  h.press();
  h.press();
  const uint32_t lastPress = h.ms;
  h.idle(FoundingChooser::kTimeoutMs - 1000);
  ASSERT_TRUE(h.kept.empty()) << "a slow chooser is never hurried";
  EXPECT_EQ(c.lit(), 2);
  h.idle(2000);
  ASSERT_EQ(h.kept.size(), 1u);
  EXPECT_EQ(h.kept[0], c.nest().eggs[0]) << "the first egg, not the lit one";
  EXPECT_GE(h.ms - lastPress, FoundingChooser::kTimeoutMs);
}

TEST(Chooser, OneEggNeedsNoChoice) {
  FoundingChooser c(Nest{{SpeciesId{4}}, 1});
  EXPECT_EQ(c.sample({}, 0), SpeciesId{4});
  EXPECT_FALSE(c.sample({}, 20));
}

// ---- who is founded --------------------------------------------------------------------------------

TEST(Founding, ThePickIsThePetAndNamesTheLine) {
  for (const SpeciesInfo& row : SPECIES) {
    MemStorage store;
    ASSERT_TRUE(writeFoundPin(store, row.id));
    Dish d = boot(store, kSeed);
    EXPECT_EQ(d.boot(), Boot::Fresh) << row.name;
    EXPECT_EQ(d.appearance().species, row.id) << row.name;
    EXPECT_EQ(std::string(d.lineage().currentName()), lineName(row.id)) << row.name;
    Dish again = boot(store, kSeed);
    EXPECT_EQ(again.boot(), Boot::Resumed) << row.name;
    EXPECT_EQ(std::string(again.lineage().currentName()), lineName(row.id)) << "the Founding entry on the log";
  }
}

TEST(Founding, AnUnpinnedNestFoundsTheDefaultUntilAKidChooses) {
  MemStorage store;
  Dish d = boot(store, kGiftSeed);
  EXPECT_EQ(std::get<Egg>(d.occupant()).genome().hash(), kGiftHash) << "three eggs wait for a hand, so today's grungo";
}

// ---- an existing pet is never founded again ------------------------------------------------------

TEST(NeedsFounding, AnyKeepsakeOrLineageMeansAPetLivesHere) {
  for (const char* f : {"snap.a", "snap.b", "rescue/snap.a.0", "rescue/snap.b.0", "lineage.log", "lineage.tmp"}) {
    MemStorage store;
    store.files[f] = {0xFF};
    EXPECT_FALSE(needsFounding(store)) << "one stray byte of " << f;
    EXPECT_FALSE(nestFor(store, kSeed)) << f;
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
  EXPECT_FALSE(nestFor(store, kGiftSeed)) << "a pin is ignored once a pet lives";
  Dish again = boot(store, kGiftSeed);
  EXPECT_EQ(std::get<Egg>(again.occupant()).genome().hash(), kGiftHash);
  EXPECT_EQ(std::string(again.lineage().currentName()), "Grungo");
}

TEST(NeedsFounding, TheCommittedV1PetWithAPinStaysHimself) {
  MemStorage store;
  store.files["snap.a"] = readFile(kV1);
  ASSERT_FALSE(store.files["snap.a"].empty()) << kV1 << " is missing (run from the project root)";
  ASSERT_TRUE(writeFoundPin(store, SpeciesId{7}));
  EXPECT_FALSE(nestFor(store, kGiftSeed));
  Dish d = boot(store, kGiftSeed);
  EXPECT_EQ(d.boot(), Boot::Resumed);
  EXPECT_STREQ(d.settings().name, "Grungo_IV");
}

// ---- found.pin ---------------------------------------------------------------------------------------

TEST(Pin, MakesTheNestOneEggAtAFreshFounding) {
  MemStorage store;
  ASSERT_EQ(debugverbs::found(store, "gilly"), nullptr);
  SpeciesId gilly;
  ASSERT_TRUE(debugverbs::speciesByName("gilly", gilly));
  std::optional<Nest> nest = nestFor(store, kSeed);
  ASSERT_TRUE(nest);
  ASSERT_EQ(nest->count, 1);
  EXPECT_EQ(nest->eggs[0], gilly);
  EXPECT_EQ(store.files["found.pin"], (std::vector<uint8_t>{'B', 'F', 'P', 'N', 1, gilly.v})) << "the pin's bytes";
  ASSERT_EQ(debugverbs::found(store, "2"), nullptr) << "by number too";
  EXPECT_EQ(nestFor(store, kSeed)->eggs[0], SpeciesId{2}) << "the last pin wins";
}

TEST(Pin, OffGivesTheThreeEggsBackAndDeletesNothing) {
  MemStorage store;
  ASSERT_EQ(debugverbs::found(store, "off"), nullptr) << "nothing pinned is already off";
  ASSERT_EQ(debugverbs::found(store, "gilly"), nullptr);
  ASSERT_EQ(debugverbs::found(store, "off"), nullptr);
  EXPECT_EQ(nestFor(store, kSeed)->count, kCycle);
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
    EXPECT_EQ(nestFor(store, kSeed)->count, kCycle);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
