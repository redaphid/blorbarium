// Boot's decision about pet partitions that will not open (issue #1). The
// flash fixtures are the real formats, made by tools/storage_fixtures.sh.
//
// A MemVolume's `mounts` is what the real stack does with those bytes, opened
// read-only. `pet` opens only when IDF accepts the NVS and the "blorb"
// namespace is in it, so blank or random bytes do not, nor does a pet whose
// namespace entry was overwritten; IDF refuses a newer format or a partition
// with no free page outright. LittleFS mounts only its own superblock.
// Opening for writing is counted apart (rwOpens): on NVS it is itself a
// write, creating a missing namespace, so a sealed board must never do it.
#include <gtest/gtest.h>

#include <algorithm>
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
  bool mounts = false;   // whether these bytes open as they stand
  bool readable = true;
  bool rwRefused = false;   // opens, but not for writing
  bool up = false, rw = false;
  int formats = 0, rwOpens = 0;

  MemVolume(std::vector<uint8_t> b, bool m) : bytes(std::move(b)), mounts(m) {}
  bool open() override {
    rw = false;
    return up = mounts;
  }
  bool writable() override {
    if (!up || rwRefused) return false;
    ++rwOpens;
    return rw = true;
  }
  bool format() override {
    ++formats;
    std::fill(bytes.begin(), bytes.end(), 0xFF);
    return up = mounts = rw = true;
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
    return on(name).rw && kept.writeAtomic(name, data, len);
  }
  bool append(const char* name, const uint8_t* data, size_t len) override {
    return on(name).rw && kept.append(name, data, len);
  }
  bool rename(const char* from, const char* to) override {
    return on(from).rw && on(to).rw && kept.rename(from, to);
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

std::vector<uint8_t> currentBlob() { return readFile("test/fixtures/keepsake_v1.bin"); }

}  // namespace

// ---- a new board: nothing of ours, so whatever will not open is formatted ----

TEST(NewBoard, AFactoryNvsInANewerFormatAndJunkPetfsAreFormattedAndThePetResumes) {
  Board b({fixture("nvs_foreign_newer.bin", kPetSize), false}, {foreign(kPetfsSize), false});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::Formatted);
  EXPECT_EQ(p.slots.held, Holds::Foreign);
  EXPECT_EQ(p.files.opened, Opened::Formatted);
  EXPECT_EQ(p.files.held, Holds::Foreign);
  EXPECT_STREQ(describe(p.files), "formatted: it held another firmware's bytes and never a pet");
  expectFreshThenResumed(b);
}

TEST(NewBoard, AFactoryNvsWithNoFreePageIsFormatted) {
  Board b({fixture("nvs_foreign_full.bin", kPetSize), false}, {blank(kPetfsSize), false});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::Formatted);
  EXPECT_EQ(p.slots.held, Holds::Foreign);
  EXPECT_EQ(p.files.held, Holds::Blank);
  expectFreshThenResumed(b);
}

// IDF accepts random bytes, but no "blorb" namespace is in them.
TEST(NewBoard, RandomBytesInBothAreFormatted) {
  Board b({foreign(kPetSize), false}, {foreign(kPetfsSize), false});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.held, Holds::Foreign);
  EXPECT_EQ(p.slots.opened, Opened::Formatted);
  EXPECT_EQ(p.files.opened, Opened::Formatted);
  expectFreshThenResumed(b);
}

TEST(NewBoard, ABlankBoardBootsFreshAsToday) {
  Board b({blank(kPetSize), false}, {blank(kPetfsSize), false});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.held, Holds::Blank);
  EXPECT_EQ(p.slots.opened, Opened::Formatted);
  EXPECT_EQ(p.files.held, Holds::Blank);
  EXPECT_EQ(p.files.opened, Opened::Formatted);
  EXPECT_STREQ(describe(p.files), "formatted: it was blank");
  expectFreshThenResumed(b);
}

TEST(NewBoard, AnotherFirmwaresNvsAndLittleFsAreForeign) {
  MemVolume nvs(fixture("nvs_foreign.bin", kPetSize), false);
  EXPECT_EQ(slotsHold(nvs), Holds::Foreign);
  std::vector<uint8_t> count = fixture("petfs_head.bin", kPetfsSize);
  count[28] = count[kBlock + 28] = 0x00;   // block_count 0x5A0 -> 0x500: someone else's partition size
  MemVolume otherCount(count, false);
  EXPECT_EQ(filesHold(otherCount, kBlock), Holds::Foreign);
  std::vector<uint8_t> size = fixture("petfs_head.bin", kPetfsSize);
  size[25] = size[kBlock + 25] = 0x02;   // block_size 4096 -> 512, count unchanged
  MemVolume otherSize(size, false);
  EXPECT_EQ(filesHold(otherSize, kBlock), Holds::Foreign);
}

// ---- one partition ours, the other not: each is judged by its own bytes ----

// Found in review: a board that ran an older build once has a petfs that build
// blank-formatted, beside the factory's NVS. Its pet must still save.
TEST(Mixed, AFactoryNvsBesideAPetfsAnOlderBuildFormattedIsFormatted) {
  Board b({fixture("nvs_foreign_newer.bin", kPetSize), false}, {fixture("petfs_head.bin", kPetfsSize), true});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.held, Holds::Foreign);
  EXPECT_EQ(p.slots.opened, Opened::Formatted);
  EXPECT_EQ(p.files.opened, Opened::Ok);
  expectFreshThenResumed(b);
}

// Found in review: a real pet beside a blank petfs that will not mount. The
// older code formatted that petfs, and so must this, or no lineage is kept.
TEST(Mixed, ARealPetBesideABlankPetfsKeepsThePetAndGetsItsLineage) {
  const std::vector<uint8_t> petBytes = fixture("nvs_pet_v1.bin", kPetSize);
  Board b({petBytes, true}, {blank(kPetfsSize), false});
  b.kept.files["snap.a"] = currentBlob();
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::Ok);
  EXPECT_EQ(p.files.held, Holds::Blank);
  EXPECT_EQ(p.files.opened, Opened::Formatted);
  EXPECT_EQ(bootDish(b), Boot::Resumed);
  EXPECT_TRUE(b.kept.files.count("lineage.log")) << "the lineage saves";
  EXPECT_EQ(b.pet.bytes, petBytes);
}

// The test board (hw, 2026-10-13, round 2): 64 KB of random bytes over the
// start of both partitions of a saved pet. IDF opened `pet` over the junk and
// its later pages still held a snapshot; `petfs` had lost its superblock, and
// formatting it threw a living pet's lineage away. Junk beside a pet is kept.
TEST(Mixed, JunkOverASavedBoardsHeadsLeavesPetfsBesideItsPet) {
  std::vector<uint8_t> petBytes = foreign(0x10000);
  const std::vector<uint8_t> saved = fixture("nvs_pet_v1.bin", kPetSize - 0x10000);
  petBytes.insert(petBytes.end(), saved.begin(), saved.end());
  std::vector<uint8_t> fs = fixture("petfs_head.bin", kPetfsSize);
  const std::vector<uint8_t> junk = foreign(0x10000);
  std::copy(junk.begin(), junk.end(), fs.begin());
  Board b({petBytes, true}, {fs, false});
  b.kept.files["snap.a"] = foreign(64);
  b.kept.files["snap.b"] = currentBlob();
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::Ok);
  EXPECT_EQ(p.files.held, Holds::Foreign);
  EXPECT_EQ(p.files.opened, Opened::LeftAlone);
  EXPECT_STREQ(describe(p.files), "LEFT ALONE: unknown bytes beside a blorbarium pet; nothing saves");
  EXPECT_EQ(b.petfs.formats, 0);
  EXPECT_EQ(b.petfs.bytes, fs) << "the lineage that may be in the junk is kept";
  EXPECT_EQ(b.pet.bytes, petBytes);
}

// The same caution when `pet` cannot be read: it may hold the pet.
TEST(Mixed, JunkPetfsBesideAnUnreadablePetIsLeftAlone) {
  Board b({foreign(kPetSize), false}, {foreign(kPetfsSize), false});
  b.pet.readable = false;
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.held, Holds::Unreadable);
  EXPECT_EQ(p.files.held, Holds::Foreign);
  EXPECT_EQ(p.files.opened, Opened::LeftAlone);
  EXPECT_EQ(b.petfs.formats, 0);
}

// ---- a keepsake: recognised by bytes every save already has ----

// A board's save, in the format it is in today: NVS entries written by
// ESP-IDF's own generator around keepsake_v1.bin, and mklittlefs's superblock.
TEST(Keepsake, TheCurrentFormatIsRecognisedWithNoNewMarker) {
  MemVolume pet(fixture("nvs_pet_v1.bin", kPetSize), true);
  MemVolume petfs(fixture("petfs_head.bin", kPetfsSize), true);
  EXPECT_EQ(slotsHold(pet), Holds::Pet);
  EXPECT_EQ(filesHold(petfs, kBlock), Holds::Pet);
}

TEST(Keepsake, TheSecondSlotAloneIsAPet) {
  MemVolume pet(fixture("nvs_pet_snapb.bin", kPetSize), false);
  EXPECT_EQ(slotsHold(pet), Holds::Pet);
}

// NVS entries are 32 bytes; a scan on a coarser stride misses a blob that
// starts on an odd entry, as snap.b's does in this fixture.
TEST(Keepsake, AMagicOnAnOddNvsEntryIsSeen) {
  std::vector<uint8_t> bytes = fixture("nvs_pet_v1.bin", kPetSize);
  ASSERT_EQ(std::memcmp(bytes.data() + 0x80, "BLRB", 4), 0);
  ASSERT_EQ(std::memcmp(bytes.data() + 0x26A0, "BLRB", 4), 0);
  static_assert(0x26A0 % 64 == 32, "snap.b's data starts on an odd entry");
  bytes[0x80] = 0x00;   // snap.a's magic gone: only snap.b's is left
  MemVolume pet(bytes, false);
  EXPECT_EQ(slotsHold(pet), Holds::Pet);
}

// LittleFS writes its superblock pair alternately: either block may be the good one.
TEST(Keepsake, ASuperblockInBlockOneAloneIsAPet) {
  std::vector<uint8_t> fs = fixture("petfs_head.bin", kPetfsSize);
  std::fill(fs.begin(), fs.begin() + 32, 0x00);
  MemVolume petfs(fs, false);
  EXPECT_EQ(filesHold(petfs, kBlock), Holds::Pet);
}

TEST(Keepsake, AValidSaveResumesUntouched) {
  const std::vector<uint8_t> petBytes = fixture("nvs_pet_v1.bin", kPetSize);
  const std::vector<uint8_t> fsBytes = fixture("petfs_head.bin", kPetfsSize);
  Board b({petBytes, true}, {fsBytes, true});
  const std::vector<uint8_t> blob = currentBlob();
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

// A pet's NVS that IDF will not open (written by a newer IDF, then downgraded).
TEST(Keepsake, APetNvsThatWillNotOpenIsLeftAloneAndSaysSo) {
  const std::vector<uint8_t> petBytes = fixture("nvs_pet_newer.bin", kPetSize);
  Board b({petBytes, false}, {fixture("petfs_head.bin", kPetfsSize), true});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.held, Holds::Pet);
  EXPECT_EQ(p.slots.opened, Opened::LeftAlone);
  EXPECT_STREQ(describe(p.slots), "LEFT ALONE: it holds a damaged blorbarium pet; nothing saves");
  EXPECT_EQ(b.pet.formats, 0);
  EXPECT_EQ(b.pet.bytes, petBytes) << "not one byte of a keepsake is written";
  EXPECT_EQ(p.files.opened, Opened::Ok);
  EXPECT_EQ(b.petfs.rwOpens, 0) << "the board is sealed: petfs is read, never written";
  bootDish(b);
  EXPECT_FALSE(b.kept.files.count("snap.a")) << "nothing saves over it";
  EXPECT_FALSE(b.kept.files.count("lineage.log")) << "nor beside it";
}

// Even a blank `pet` is not formatted on a sealed board: nothing would be written to it.
TEST(Keepsake, ADamagedPetfsIsLeftAloneAndSealsTheBoard) {
  const std::vector<uint8_t> fs = fixture("petfs_head.bin", kPetfsSize);
  Board b({blank(kPetSize), false}, {fs, false});
  Provisioned p = b.power();
  EXPECT_EQ(p.files.held, Holds::Pet);
  EXPECT_EQ(p.files.opened, Opened::LeftAlone);
  EXPECT_EQ(p.slots.held, Holds::Blank);
  EXPECT_EQ(p.slots.opened, Opened::LeftAlone);
  EXPECT_STREQ(describe(p.slots), "LEFT ALONE: blank, beside a blorbarium pet; nothing saves");
  EXPECT_EQ(b.pet.formats + b.petfs.formats, 0);
  EXPECT_EQ(b.petfs.bytes, fs);
}

TEST(Keepsake, AnUnreadablePetIsLeftAlone) {
  Board b({foreign(kPetSize), false}, {fixture("petfs_head.bin", kPetfsSize), true});
  b.pet.readable = false;
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.held, Holds::Unreadable);
  EXPECT_EQ(p.slots.opened, Opened::LeftAlone);
  EXPECT_STREQ(describe(p.slots), "LEFT ALONE: it would not read; nothing saves");
  EXPECT_EQ(p.files.opened, Opened::Ok);
  EXPECT_EQ(b.pet.formats, 0);
  EXPECT_EQ(b.petfs.rwOpens, 0);
}

TEST(Keepsake, AnUnreadablePetfsIsLeftAlone) {
  Board b({fixture("nvs_pet_v1.bin", kPetSize), true}, {foreign(kPetfsSize), false});
  b.petfs.readable = false;
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::Ok);
  EXPECT_EQ(p.files.held, Holds::Unreadable);
  EXPECT_EQ(p.files.opened, Opened::LeftAlone);
  EXPECT_EQ(b.petfs.formats, 0);
}

// ---- writing waits for the decision ----

// The test board (hw, c1aa312): the read-write open that came before the
// decision wrote a fresh "blorb" namespace entry into a pet it then sealed.
// Here the namespace survived, so `pet` opens; petfs is junk beside it.
TEST(Writing, ASealedBoardNeverOpensForWriting) {
  const std::vector<uint8_t> petBytes = fixture("nvs_pet_v1.bin", kPetSize);
  Board b({petBytes, true}, {foreign(kPetfsSize), false});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::Ok);
  EXPECT_EQ(p.files.opened, Opened::LeftAlone);
  EXPECT_EQ(b.pet.rwOpens, 0) << "opening NVS for writing can write";
  EXPECT_EQ(b.pet.bytes, petBytes);
}

// The same 64 KB, where the junk took the namespace entry: `pet` will not open.
TEST(Writing, APetWhoseNamespaceWasLostIsSealedNotRelinked) {
  std::vector<uint8_t> petBytes = foreign(0x10000);
  const std::vector<uint8_t> saved = fixture("nvs_pet_v1.bin", kPetSize - 0x10000);
  petBytes.insert(petBytes.end(), saved.begin(), saved.end());
  Board b({petBytes, false}, {fixture("petfs_head.bin", kPetfsSize), true});
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.held, Holds::Pet);
  EXPECT_EQ(p.slots.opened, Opened::LeftAlone);
  EXPECT_EQ(b.pet.rwOpens + b.pet.formats + b.petfs.rwOpens, 0);
  EXPECT_EQ(b.pet.bytes, petBytes);
}

TEST(Writing, AnOpenBoardIsOpenedForWritingOnceDecided) {
  Board b({fixture("nvs_pet_v1.bin", kPetSize), true}, {fixture("petfs_head.bin", kPetfsSize), true});
  b.power();
  EXPECT_TRUE(b.pet.rw);
  EXPECT_TRUE(b.petfs.rw);
}

TEST(Writing, OneThatWillNotOpenForWritingSaysSo) {
  Board b({fixture("nvs_pet_v1.bin", kPetSize), true}, {fixture("petfs_head.bin", kPetfsSize), true});
  b.pet.rwRefused = true;
  Provisioned p = b.power();
  EXPECT_EQ(p.slots.opened, Opened::ReadOnly);
  EXPECT_STREQ(describe(p.slots), "FAILED: it opened but not for writing; nothing saves");
  EXPECT_EQ(p.files.opened, Opened::Ok);
}

// ---- an image on trial (docs/ota.md "The trial"): the flash is still the old image's ----

TEST(OnTrial, AnImageOnTrialFormatsNothingAndRunsSealed) {
  Board b({foreign(kPetSize), false}, {fixture("petfs_head.bin", kPetfsSize), true});   // `pet` would be formatted
  const std::vector<uint8_t> petBytes = b.pet.bytes;
  const Provisioned p = provision(b.pet, b.petfs, kBlock, true);
  EXPECT_EQ(p.slots.opened, Opened::HeldForTrial);
  EXPECT_TRUE(leftAlone(p)) << "it runs sealed, is not Resumed, and its trial rolls it back";
  EXPECT_EQ(b.pet.formats + b.petfs.formats + b.pet.rwOpens + b.petfs.rwOpens, 0);
  EXPECT_EQ(b.pet.bytes, petBytes);
}

TEST(OnTrial, ABlankBoardOnTrialFormatsNothing) {
  Board b({blank(kPetSize), false}, {blank(kPetfsSize), false});
  const Provisioned p = provision(b.pet, b.petfs, kBlock, true);
  EXPECT_EQ(p.slots.opened, Opened::HeldForTrial);
  EXPECT_EQ(p.files.opened, Opened::HeldForTrial);
  EXPECT_EQ(b.pet.formats + b.petfs.formats, 0);
}

TEST(OnTrial, AnImageOnTrialOpensNothingForWriting) {
  Board b({fixture("nvs_pet_v1.bin", kPetSize), true}, {fixture("petfs_head.bin", kPetfsSize), true});
  const Provisioned p = provision(b.pet, b.petfs, kBlock, true);
  EXPECT_EQ(p.slots.opened, Opened::Ok);
  EXPECT_EQ(p.files.opened, Opened::Ok);
  EXPECT_FALSE(leftAlone(p)) << "it reads the pet it was handed";
  EXPECT_EQ(b.pet.rwOpens + b.petfs.rwOpens, 0) << "a read-write NVS open can write";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
