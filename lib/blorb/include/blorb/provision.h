#pragma once
// Provision: what boot does with a pet partition that will not open.
//
// A board keeps its pet in two partitions: the snapshot slots (NVS `pet`) and
// the files (LittleFS `petfs`, where lineage.log lives). A partition that will
// not open and holds a blorbarium mark is a damaged keepsake: it is left alone
// for a person to rescue, never formatted by boot. Without a mark:
//  * `pet` is formatted, judged by its own bytes alone: blank, or another
//    firmware's (a new board still carries its factory demo).
//  * `petfs` is formatted when it is blank, or when it is foreign and `pet`
//    holds no pet. Junk in petfs beside a pet is left alone: on the test board,
//    64 KB over both partitions' heads left the pet resuming from NVS while
//    petfs had lost its superblock, and formatting it threw his lineage away.
// Judging the pair wholly together was tried first and was wrong both ways: a
// factory `pet` beside a `petfs` an older build had formatted stayed unsavable
// forever, and a real pet's blank `petfs` was never formatted.
//
// The marks are bytes every save already carries, so a board saved before
// this rule existed is recognised with no new marker:
//  * slots: a snapshot's magic "BLRB" at the start of a 32-byte NVS entry,
//    which is where NVS starts every blob's data;
//  * files: a LittleFS superblock of petfs's own geometry, "littlefs" at
//    offset 8 of block 0 or 1 with the block size and count after it. Another
//    firmware's LittleFS of a different size is foreign.
#include <cstddef>
#include <cstdint>
#include <optional>

namespace blorb {

// One partition as boot sees it. Firmware: src/hw/storage_nvs_fs.h; tests: memory.
class Volume {
 public:
  virtual ~Volume() = default;
  virtual bool open() = 0;     // mount it as it stands; never formats
  virtual bool format() = 0;   // erase it and mount it empty
  virtual size_t size() = 0;
  virtual bool readRaw(size_t at, uint8_t* buf, size_t n) = 0;
};

enum class Holds : uint8_t { Blank, Foreign, Pet, Unreadable };
enum class Opened : uint8_t { Ok, Formatted, LeftAlone, FormatFailed };

// `held` is read only when that partition would not open.
struct VolumeReport { Opened opened = Opened::Ok; std::optional<Holds> held; };
struct Provisioned { VolumeReport slots, files; };

Holds slotsHold(Volume&);
Holds filesHold(Volume&, uint32_t blockSize);
Provisioned provision(Volume& slots, Volume& files, uint32_t fsBlockSize);

// One line for the boot log: what the partition held and what boot did about it.
const char* describe(const VolumeReport&);

}  // namespace blorb
