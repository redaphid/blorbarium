// Boot::Sealed (issue #2): a keepsake on the flash that would not open. The
// board shows the pet asleep in amber and touches nothing it holds.
#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "blorb/dish.h"
#include "blorb/keepsake.h"
#include "grungo_pack.h"
#include "paint/sprite_pack.h"
#include "links.h"
#include "mem_storage.h"

using namespace blorb;

namespace {

constexpr uint64_t kLineage = 0x5C9409500600ull;

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}

// MemStorage that counts every call, so "touches nothing" is a number.
struct Counting : blorbtest::MemStorage {
  int reads = 0, writes = 0;
  std::optional<size_t> read(const char* n, size_t o, uint8_t* b, size_t c) override {
    ++reads;
    return MemStorage::read(n, o, b, c);
  }
  bool writeAtomic(const char* n, const uint8_t* d, size_t l) override {
    ++writes;
    return MemStorage::writeAtomic(n, d, l);
  }
  bool append(const char* n, const uint8_t* d, size_t l) override {
    ++writes;
    return MemStorage::append(n, d, l);
  }
  bool rename(const char* f, const char* t) override {
    ++writes;
    return MemStorage::rename(f, t);
  }
  size_t size(const char* n) override {
    ++reads;
    return MemStorage::size(n);
  }
};

// The keepsake as the flash holds it: a good slot, a torn one that a normal
// load would quarantine (a rename, so a write), and a lineage.
Counting keepsake() {
  Counting s;
  s.files["snap.a"] = readFile("test/fixtures/keepsake_v1.bin");
  EXPECT_FALSE(s.files["snap.a"].empty()) << "run from the project root";
  s.files["snap.b"] = std::vector<uint8_t>(s.files["snap.a"].begin(), s.files["snap.a"].begin() + 40);
  s.files["lineage.log"] = std::vector<uint8_t>(300, 0x5A);
  return s;
}

DishOptions sealed() {
  DishOptions o;
  o.sealed = true;
  return o;
}

std::string phaseLine(const blorbtest::ScriptedLink& link) {
  for (const std::string& l : link.sent)
    if (l.rfind("#1 + phase=", 0) == 0) return l;
  return "";
}

// Two hours of ticks, a phone visit and every kind of mutating line.
void liveThrough(Dish& dish) {
  blorbtest::ScriptedLink phone;
  for (const char* l : {"#1 HELLO", "#2 TIME 1790000000", "#3 TWIST stimulus shake", "#4 TWIST prophecy_treat",
                        "#5 TWIST gene_edit 9 1 7", "#6 SUB state", "#7 SNAPSHOT", "#8 TIME 1790090000",
                        "#9 TWIST teach HELLO", "#a TWIST unteach"})
    phone.inbound.emplace_back(l);
  for (uint32_t ms = 0; ms <= 2 * 3600 * 1000; ms += 1000) dish.tick(ms, phone);
}

}  // namespace

TEST(Sealed, ReadsAndWritesNothingThroughHoursOfLifeAndEveryMutatingLine) {
  Counting store = keepsake();
  const auto before = store.files;
  Dish dish(store, 7, kLineage, sealed());
  EXPECT_EQ(dish.boot(), Boot::Sealed);
  liveThrough(dish);
  EXPECT_EQ(store.writes, 0) << "nothing is written to either partition";
  EXPECT_EQ(store.reads, 0) << "nothing is read either: a load can quarantine";
  EXPECT_EQ(store.files, before);
}

// The guard above is not vacuous: the same store, unsealed, is written at once.
TEST(Sealed, TheSameKeepsakeUnsealedIsWritten) {
  Counting store = keepsake();
  Dish dish(store, 7, kLineage);
  liveThrough(dish);
  EXPECT_GT(store.writes, 0);
}

TEST(Sealed, ShowsThePetAsleepAndHoldsStill) {
  Counting store = keepsake();
  Dish dish(store, 7, kLineage, sealed());
  const Appearance first = dish.appearance();
  EXPECT_TRUE(first.sealed);
  EXPECT_TRUE(first.asleep);
  EXPECT_EQ(first.kind, Appearance::Kind::Creature) << "the pet, not an egg that replaced him";
  liveThrough(dish);
  const Appearance later = dish.appearance();
  EXPECT_TRUE(later.sealed);
  EXPECT_EQ(later.lifeSeed, first.lifeSeed);
  ASSERT_TRUE(std::holds_alternative<Egg>(dish.occupant())) << "no throwaway life ran";
  EXPECT_EQ(std::get<Egg>(dish.occupant()).progress(), Fx::zero());
}

TEST(Sealed, StateTellsTheSite) {
  Counting store = keepsake();
  Dish dish(store, 7, kLineage, sealed());
  blorbtest::ScriptedLink phone;
  phone.inbound.emplace_back("#1 STATE");
  dish.tick(0, phone);
  EXPECT_NE(phaseLine(phone).find(" sealed=1"), std::string::npos) << phaseLine(phone);

  blorbtest::MemStorage fresh;
  Dish open(fresh, 7, kLineage);
  blorbtest::ScriptedLink other;
  other.inbound.emplace_back("#1 STATE");
  open.tick(0, other);
  ASSERT_FALSE(phaseLine(other).empty());
  EXPECT_EQ(phaseLine(other).find("sealed"), std::string::npos) << "an ordinary board says nothing of it";
  EXPECT_FALSE(open.appearance().sealed);
}

// A rolled-back image meeting a newer firmware's save: the same harm, the same screen.
Counting newerKeepsake() {
  Counting s = keepsake();
  s.files["snap.b"] = s.files["snap.a"];
  for (const char* slot : {"snap.a", "snap.b"}) s.files[slot][4] = uint8_t(kFormatVersion + 1);
  return s;
}

TEST(Newer, ANewerSaveIsSealedWritesNothingAndHoldsStill) {
  Counting store = newerKeepsake();
  const auto before = store.files;
  Dish dish(store, 7, kLineage);
  EXPECT_EQ(dish.boot(), Boot::ReadOnlyNewer);
  EXPECT_TRUE(dish.sealed());
  liveThrough(dish);
  EXPECT_EQ(store.writes, 0);
  EXPECT_EQ(store.files, before);
  EXPECT_TRUE(dish.appearance().sealed);
  ASSERT_TRUE(std::holds_alternative<Egg>(dish.occupant())) << "no throwaway life ran";
  EXPECT_EQ(std::get<Egg>(dish.occupant()).progress(), Fx::zero());
}

TEST(Newer, StateTellsTheSiteWhichSeal) {
  Counting store = newerKeepsake();
  Dish dish(store, 7, kLineage);
  blorbtest::ScriptedLink phone;
  phone.inbound.emplace_back("#1 STATE");
  dish.tick(0, phone);
  EXPECT_NE(phaseLine(phone).find(" sealed=newer"), std::string::npos) << phaseLine(phone);
}

// So the sealed golden stands for both: the two boots draw the same pixels.
TEST(Newer, DrawsTheSameFrameAsASealedBoard) {
  Counting newer = newerKeepsake(), stuck = keepsake();
  Dish a(newer, 7, kLineage), b(stuck, 7, kLineage, sealed());
  auto ca = std::make_unique<paint::Canvas240>(), cb = std::make_unique<paint::Canvas240>();
  paint::draw(a.appearance(), grungoPack(), *ca, 3000);
  paint::draw(b.appearance(), grungoPack(), *cb, 3000);
  EXPECT_EQ(std::memcmp(ca->px, cb->px, sizeof ca->px), 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
