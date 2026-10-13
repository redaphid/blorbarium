// Boot's decision about pet partitions that will not open (issue #1). The
// flash fixtures are the real formats, made by tools/storage_fixtures.sh.
#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "blorb/dish.h"
#include "blorb/keepsake.h"
#include "blorb/provision.h"
#include "mem_storage.h"

using namespace blorb;

namespace {

constexpr size_t kPetSize = 0x40000;      // tools/partitions_16mb.csv
constexpr size_t kPetfsSize = 0x5A0000;
constexpr uint32_t kBlock = 4096;
constexpr uint64_t kLineage = 0x5C9409500600ull;

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}

std::vector<uint8_t> fixture(const char* name, size_t size) {
  std::vector<uint8_t> bytes = readFile(std::string("test/fixtures/") + name);
  EXPECT_FALSE(bytes.empty()) << name << " is missing (run from the project root)";
  bytes.resize(size, 0xFF);
  return bytes;
}

std::vector<uint8_t> blank(size_t size) { return std::vector<uint8_t>(size, 0xFF); }

// What a factory demo or an older firmware might have left: not 0xFF, not ours.
std::vector<uint8_t> foreign(size_t size) {
  std::vector<uint8_t> bytes(size);
  uint32_t x = 0xE9;
  for (uint8_t& b : bytes) b = uint8_t((x = x * 1664525u + 1013904223u) >> 24);
  return bytes;
}

struct MemVolume : Volume {
  std::vector<uint8_t> bytes;
  bool mounts = false;   // whether these bytes mount as they stand
  bool readable = true;
  bool up = false;
  int formats = 0;

  MemVolume(std::vector<uint8_t> b, bool m) : bytes(std::move(b)), mounts(m) {}
  bool open() override { return up = mounts; }
  bool format() override {
    ++formats;
    std::fill(bytes.begin(), bytes.end(), 0xFF);
    return up = mounts = true;
  }
  size_t size() override { return bytes.size(); }
  bool readRaw(size_t at, uint8_t* buf, size_t n) override {
    if (!readable || at + n > bytes.size()) return false;
    std::memcpy(buf, bytes.data() + at, n);
    return true;
  }
};

// NvsFsStorage's routing over two volumes: every call to one that is not up fails.
struct Board : Storage {
  MemVolume pet, petfs;
  blorbtest::MemStorage kept;   // what the volumes hold while up

  Board(MemVolume p, MemVolume f) : pet(std::move(p)), petfs(std::move(f)) {}
  Provisioned power() { return provision(pet, petfs, kBlock); }
  MemVolume& on(const char* name) { return std::strncmp(name, "snap.", 5) == 0 ? pet : petfs; }

  std::optional<size_t> read(const char* name, size_t offset, uint8_t* buf, size_t cap) override {
    return on(name).up ? kept.read(name, offset, buf, cap) : std::nullopt;
  }
  bool writeAtomic(const char* name, const uint8_t* data, size_t len) override {
    return on(name).up && kept.writeAtomic(name, data, len);
  }
  bool append(const char* name, const uint8_t* data, size_t len) override {
    return on(name).up && kept.append(name, data, len);
  }
  bool rename(const char* from, const char* to) override {
    return on(from).up && on(to).up && kept.rename(from, to);
  }
  size_t size(const char* name) override { return on(name).up ? kept.size(name) : 0; }
};

Boot bootDish(Board& b) {
  Dish dish(b, 7, kLineage);
  return dish.boot();
}

// A new board's first boot founds a pet, saves it, and the next boot finds it.
void expectFreshThenResumed(Board& b) {
  ASSERT_EQ(bootDish(b), Boot::Fresh);
  EXPECT_TRUE(b.kept.files.count("snap.a")) << "the founding egg saved";
  EXPECT_TRUE(b.kept.files.count("lineage.log")) << "the lineage was founded";
  const int petFormats = b.pet.formats, petfsFormats = b.petfs.formats;
  Provisioned again = b.power();
  EXPECT_EQ(again.slots.opened, Opened::Ok);
  EXPECT_EQ(again.files.opened, Opened::Ok);
  EXPECT_EQ(bootDish(b), Boot::Resumed);
  EXPECT_EQ(b.pet.formats, petFormats) << "the second boot formats nothing";
  EXPECT_EQ(b.petfs.formats, petfsFormats);
}

}  // namespace

TEST(Foreign, InBothRegionsBootsFreshFormatsSavesAndResumes) {
  Board b({foreign(kPetSize), false}, {foreign(kPetfsSize), false});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::Formatted);
  EXPECT_EQ(p.slots.held, Holds::Foreign);
  EXPECT_EQ(p.files.opened, Opened::Formatted);
  EXPECT_EQ(p.files.held, Holds::Foreign);
  EXPECT_STREQ(describe(p.files), "formatted: it held another firmware's bytes and never a pet");
  expectFreshThenResumed(b);
}

TEST(Foreign, InPetAloneFormatsPetAndLeavesPetfs) {
  Board b({foreign(kPetSize), false}, {blank(kPetfsSize), true});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::Formatted);
  EXPECT_EQ(p.files.opened, Opened::Ok);
  EXPECT_EQ(b.petfs.formats, 0);
  expectFreshThenResumed(b);
}

// NVS may well open over foreign bytes (it counts pages it cannot read as free)
// while LittleFS will not mount them: then only petfs is formatted.
TEST(Foreign, InPetfsAloneFormatsPetfsAndLeavesPet) {
  const std::vector<uint8_t> petBytes = foreign(kPetSize);
  Board b({petBytes, true}, {foreign(kPetfsSize), false});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::Ok);
  EXPECT_EQ(p.slots.held, Holds::Foreign);
  EXPECT_EQ(p.files.opened, Opened::Formatted);
  EXPECT_EQ(b.pet.bytes, petBytes) << "a partition that opened is never formatted";
  expectFreshThenResumed(b);
}

TEST(Foreign, AnotherFirmwaresNvsAndLittleFsAreForeign) {
  MemVolume nvs(fixture("nvs_foreign.bin", kPetSize), false);
  EXPECT_EQ(slotsHold(nvs), Holds::Foreign);
  std::vector<uint8_t> fs = fixture("petfs_head.bin", kPetfsSize);
  fs[28] = fs[kBlock + 28] = 0x00;   // block_count 0x5A0 -> 0x500: someone else's partition size
  MemVolume other(fs, false);
  EXPECT_EQ(filesHold(other, kBlock), Holds::Foreign);
}

TEST(Blank, BothRegionsBootFreshAsToday) {
  Board b({blank(kPetSize), false}, {blank(kPetfsSize), false});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.held, Holds::Blank);
  EXPECT_EQ(p.files.held, Holds::Blank);
  EXPECT_EQ(p.files.opened, Opened::Formatted);
  EXPECT_STREQ(describe(p.files), "formatted: it was blank");
  expectFreshThenResumed(b);
}

// The gift device's save, in the format it is in today: NVS entries written by
// ESP-IDF's own generator around keepsake_v1.bin, and mklittlefs's superblock.
TEST(Keepsake, TheCurrentFormatIsRecognisedWithNoNewMarker) {
  MemVolume pet(fixture("nvs_pet_v1.bin", kPetSize), true);
  MemVolume petfs(fixture("petfs_head.bin", kPetfsSize), true);
  EXPECT_EQ(slotsHold(pet), Holds::Pet);
  EXPECT_EQ(filesHold(petfs, kBlock), Holds::Pet);
}

TEST(Keepsake, AValidSaveResumesUntouched) {
  const std::vector<uint8_t> petBytes = fixture("nvs_pet_v1.bin", kPetSize);
  const std::vector<uint8_t> fsBytes = fixture("petfs_head.bin", kPetfsSize);
  Board b({petBytes, true}, {fsBytes, true});
  const std::vector<uint8_t> blob = readFile("test/fixtures/keepsake_v1.bin");
  b.kept.files["snap.a"] = blob;
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::Ok);
  EXPECT_EQ(p.files.opened, Opened::Ok);
  EXPECT_FALSE(p.slots.held) << "both opened, so nothing was scanned";
  Dish dish(b, 7, kLineage);
  EXPECT_EQ(dish.boot(), Boot::Resumed);
  Snapshot saved;
  ASSERT_TRUE(Keepsake::decode(blob.data(), blob.size(), saved));
  EXPECT_EQ(std::get<Creature>(dish.occupant()).genome().hash(), std::get<Creature>(saved.occupant).genome().hash());
  EXPECT_EQ(b.pet.bytes, petBytes);
  EXPECT_EQ(b.petfs.bytes, fsBytes);
}

TEST(Keepsake, ADamagedPetPartitionIsLeftAloneAndSaysSo) {
  std::vector<uint8_t> petBytes = fixture("nvs_pet_v1.bin", kPetSize);
  for (size_t page = 0; page < 0x6000; page += kBlock) petBytes[page + 8] ^= 0x5A;   // every page header's CRC fails
  Board b({petBytes, false}, {fixture("petfs_head.bin", kPetfsSize), true});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.held, Holds::Pet);
  EXPECT_EQ(p.slots.opened, Opened::LeftAlone);
  EXPECT_STREQ(describe(p.slots), "LEFT ALONE: it holds a damaged blorbarium pet; nothing saves");
  EXPECT_EQ(b.pet.formats, 0);
  EXPECT_EQ(b.pet.bytes, petBytes) << "not one byte of a keepsake is written";
  bootDish(b);
  EXPECT_FALSE(b.kept.files.count("snap.a")) << "nothing saves over it";
}

TEST(Keepsake, ADamagedPetfsIsLeftAlone) {
  std::vector<uint8_t> fs = fixture("petfs_head.bin", kPetfsSize);
  Board b({blank(kPetSize), true}, {fs, false});
  Provisioned p = b.power();
  EXPECT_EQ(p.files.held, Holds::Pet);
  EXPECT_EQ(p.files.opened, Opened::LeftAlone);
  EXPECT_EQ(b.petfs.formats, 0);
  EXPECT_EQ(b.petfs.bytes, fs);
}

// The pet is the pair: a petfs that holds only junk on a board whose slots
// hold a pet is a keepsake's damaged half, not a new board.
TEST(Keepsake, APetInEitherPartitionKeepsTheOtherFromFormatting) {
  std::vector<uint8_t> junk = foreign(kPetfsSize);
  Board b({fixture("nvs_pet_v1.bin", kPetSize), true}, {junk, false});
  Provisioned p = b.power();
  EXPECT_EQ(p.files.held, Holds::Foreign);
  EXPECT_EQ(p.files.opened, Opened::LeftAlone);
  EXPECT_STREQ(describe(p.files), "LEFT ALONE: the other partition holds, or may hold, a blorbarium pet; nothing saves");
  EXPECT_EQ(b.petfs.bytes, junk);
}

TEST(Keepsake, AnUnreadablePartitionIsLeftAlone) {
  MemVolume pet(foreign(kPetSize), false);
  pet.readable = false;
  MemVolume petfs(foreign(kPetfsSize), false);
  Provisioned p = provision(pet, petfs, kBlock);
  EXPECT_EQ(p.slots.held, Holds::Unreadable);
  EXPECT_EQ(p.slots.opened, Opened::LeftAlone);
  EXPECT_EQ(p.files.opened, Opened::LeftAlone);
  EXPECT_EQ(pet.formats + petfs.formats, 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
