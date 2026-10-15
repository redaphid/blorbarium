// The Storage seam on the 1.28's flash (DEVIATIONS.md 5). The snapshot slots
// are blobs in the labelled `pet` NVS partition; everything else is a file on
// the `petfs` LittleFS. One that will not open is formatted only when it holds
// no blorbarium pet of its own (blorb/provision.h); otherwise it stays shut and
// its writes fail.
#pragma once

#include <LittleFS.h>
#include <nvs.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

#include "blorb/provision.h"
#include "blorb/seams.h"

namespace hw {

enum class Where { Slot, File };

inline Where whereOf(const char* name) { return std::strncmp(name, "snap.", 5) == 0 ? Where::Slot : Where::File; }

class NvsFsStorage : public blorb::Storage {
 public:
  // `hold`: this image is on trial (docs/ota.md): open read-only, format
  // nothing, write nothing until releaseHold().
  blorb::Provisioned begin(bool hold = false) {
    SlotsVolume slots(nvs_);
    FilesVolume files;
    const blorb::Provisioned p = blorb::provision(slots, files, kBlock, hold);
    slotsUp_ = isUp(p.slots.opened);
    filesUp_ = isUp(p.files.opened);
    // A board with a keepsake left alone is sealed: whatever opened is read, never written.
    slotsWrite_ = !hold && slotsUp_ && p.slots.opened != blorb::Opened::ReadOnly && !blorb::leftAlone(p);
    filesWrite_ = !hold && filesUp_ && p.files.opened != blorb::Opened::ReadOnly && !blorb::leftAlone(p);
    held_ = hold && !blorb::leftAlone(p);
    return p;
  }
  // The trial kept this image: open for writing what begin() opened read-only.
  void releaseHold() {
    if (!held_) return;
    held_ = false;
    SlotsVolume slots(nvs_);
    FilesVolume files;
    slotsWrite_ = slotsUp_ && slots.writable();
    filesWrite_ = filesUp_ && files.writable();
  }
  // The debug link's WIPE (docs/debug-ble.md): `pet` erased whole and
  // `petfs`'s superblocks erased, then provisioned again by the rule boot
  // uses, which finds them blank and foreign beside no pet and formats both,
  // as on a board fresh from the factory. The default `nvs`, where the OTA
  // trial keeps its records, is not touched. The caller restarts at once:
  // the Dish in RAM still holds the old pet and must never save it here.
  blorb::Provisioned wipe() {
    nvs_close(nvs_);
    LittleFS.end();
    nvs_flash_erase_partition("pet");   // deinits it first (nvs_api.cpp)
    if (const esp_partition_t* fs =
            esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "petfs"))
      esp_partition_erase_range(fs, 0, 2 * kBlock);
    return begin(false);
  }
  uint32_t writes() const { return writes_; }
  bool writable() override { return slotsWrite_ || filesWrite_; }

  std::optional<size_t> read(const char* name, size_t offset, uint8_t* buf, size_t cap) override {
    return whereOf(name) == Where::Slot ? readSlot(name, offset, buf, cap) : readFile(name, offset, buf, cap);
  }
  bool writeAtomic(const char* name, const uint8_t* data, size_t len) override { return counted(put(name, data, len)); }
  bool append(const char* name, const uint8_t* data, size_t len) override {
    return counted(whereOf(name) == Where::File && appendFile(name, data, len));
  }
  bool rename(const char* from, const char* to) override {
    const bool files = whereOf(from) == Where::File && whereOf(to) == Where::File;
    return counted(files ? renameFile(from, to) : move(from, to));
  }
  size_t size(const char* name) override { return lengthOf(name).value_or(0); }

 private:
  static constexpr const char* kRoot = "/petfs";
  static constexpr uint32_t kBlock = 4096;   // esp_littlefs's block, which its superblock records

  // The partitions as provision() sees them: raw reads, and open or format.
  class Partition : public blorb::Volume {
   public:
    explicit Partition(const esp_partition_t* p) : p_(p) {}
    size_t size() override { return p_ ? p_->size : 0; }
    bool readRaw(size_t at, uint8_t* buf, size_t n) override {
      return p_ && esp_partition_read(p_, at, buf, n) == ESP_OK;
    }
   private:
    const esp_partition_t* p_;
  };
  // IDF's own NVS calls, not Arduino's Preferences: Preferences logs every
  // failure through a table of error names that ends at INVALID_LENGTH, and a
  // later code (NEW_VERSION_FOUND, NO_FREE_PAGES, a flash fault) is printed
  // through NULL. On the test board that was a panic every 0.6 s (issue #1).
  class SlotsVolume : public Partition {
   public:
    explicit SlotsVolume(nvs_handle_t& handle)
        : Partition(esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "pet")),
          handle_(handle) {}
    // Read-only: a read-write open creates a missing namespace, and that is a
    // write. A missing "blorb" answers NOT_FOUND, so this partition will not
    // open and provision judges its raw bytes: a pet whose namespace entry was
    // lost still shows by its BLRB and is sealed, not re-linked by accident.
    bool open() override { return init() && openAs(NVS_READONLY); }
    bool writable() override {
      nvs_close(handle_);
      return openAs(NVS_READWRITE);
    }
    // Safe after a half-open NVS (init succeeded, nvs_open did not): IDF's
    // nvs_flash_erase_partition deinits an initialised partition before it
    // erases (nvs_api.cpp).
    bool format() override { return nvs_flash_erase_partition("pet") == ESP_OK && init() && openAs(NVS_READWRITE); }
   private:
    static bool init() { return nvs_flash_init_partition("pet") == ESP_OK; }
    bool openAs(nvs_open_mode_t mode) { return nvs_open_from_partition("pet", "blorb", mode, &handle_) == ESP_OK; }
    nvs_handle_t& handle_;
  };
  class FilesVolume : public Partition {
   public:
    FilesVolume()
        : Partition(esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "petfs")) {}
    bool open() override { return LittleFS.begin(false, kRoot, 4, "petfs"); }   // never formats
    bool writable() override { return true; }
    bool format() override { return LittleFS.begin(true, kRoot, 4, "petfs"); }
  };
  static bool isUp(blorb::Opened o) {
    return o == blorb::Opened::Ok || o == blorb::Opened::Formatted || o == blorb::Opened::ReadOnly;
  }

  struct Path {
    char s[80];
    explicit Path(const char* name) { std::snprintf(s, sizeof s, "%s/%s", kRoot, name); }
  };

  bool counted(bool ok) {
    if (ok) ++writes_;
    return ok;
  }
  bool filesUp() const { return filesUp_; }

  // ---- routing by name ----
  std::optional<size_t> lengthOf(const char* name) {
    return whereOf(name) == Where::Slot ? slotLength(name) : fileLength(name);
  }
  bool put(const char* name, const uint8_t* data, size_t len) {
    return whereOf(name) == Where::Slot ? putSlot(name, data, len) : putFile(name, data, len);
  }
  bool erase(const char* name) {
    return whereOf(name) == Where::Slot ? eraseSlot(name) : filesWrite_ && std::remove(Path(name).s) == 0;
  }
  // The one cross case is a torn slot quarantined into rescue/ on petfs.
  bool move(const char* from, const char* to) {
    std::optional<size_t> n = lengthOf(from);
    if (!n) return false;
    std::vector<uint8_t> whole(*n);
    if (read(from, 0, whole.data(), *n) != n) return false;
    return put(to, whole.data(), *n) && erase(from);
  }

  // ---- slots: NVS blobs (the SlotsVolume comment says why not Preferences) ----
  std::optional<size_t> slotLength(const char* name) {
    size_t len = 0;
    if (!slotsUp_ || nvs_get_blob(nvs_, name, nullptr, &len) != ESP_OK) return std::nullopt;
    return len;
  }
  bool getSlot(const char* name, uint8_t* buf, size_t len) {
    size_t n = len;
    return nvs_get_blob(nvs_, name, buf, &n) == ESP_OK && n == len;
  }
  // nvs_get_blob wants a buffer the blob's whole size, so a partial range reads through a copy.
  std::optional<size_t> readSlot(const char* name, size_t offset, uint8_t* buf, size_t cap) {
    std::optional<size_t> len = slotLength(name);
    if (!len) return std::nullopt;
    if (cap == 0 || offset >= *len) return 0;
    if (offset == 0 && cap >= *len) return getSlot(name, buf, *len) ? *len : 0;
    std::vector<uint8_t> whole(*len);
    if (!getSlot(name, whole.data(), *len)) return 0;
    const size_t n = std::min(cap, *len - offset);
    std::memcpy(buf, whole.data() + offset, n);
    return n;
  }
  bool putSlot(const char* name, const uint8_t* data, size_t len) {   // NVS replaces a key atomically
    return slotsWrite_ && len > 0 && nvs_set_blob(nvs_, name, data, len) == ESP_OK && nvs_commit(nvs_) == ESP_OK;
  }
  bool eraseSlot(const char* name) {
    return slotsWrite_ && nvs_erase_key(nvs_, name) == ESP_OK && nvs_commit(nvs_) == ESP_OK;
  }

  // ---- files: POSIX stdio over the VFS (Arduino's File logs every missing-file probe) ----
  std::optional<size_t> fileLength(const char* name) {
    struct stat st;
    if (!filesUp() || stat(Path(name).s, &st) != 0) return std::nullopt;
    return size_t(st.st_size);
  }
  std::optional<size_t> readFile(const char* name, size_t offset, uint8_t* buf, size_t cap) {
    if (!filesUp()) return std::nullopt;
    FILE* f = std::fopen(Path(name).s, "rb");
    if (!f) return std::nullopt;
    size_t n = 0;
    if (cap > 0 && std::fseek(f, long(offset), SEEK_SET) == 0) n = std::fread(buf, 1, cap, f);
    std::fclose(f);
    return n;
  }
  static bool writeAll(const char* path, const char* mode, const uint8_t* data, size_t len) {
    FILE* f = std::fopen(path, mode);
    if (!f) return false;
    const bool whole = len == 0 || std::fwrite(data, 1, len, f) == len;
    return std::fclose(f) == 0 && whole;
  }
  bool putFile(const char* name, const uint8_t* data, size_t len) {
    if (!filesWrite_) return false;
    char tmp[80];
    std::snprintf(tmp, sizeof tmp, "%s/%s.tmp", kRoot, name);
    return writeAll(tmp, "wb", data, len) && std::rename(tmp, Path(name).s) == 0;   // LittleFS replaces atomically
  }
  bool appendFile(const char* name, const uint8_t* data, size_t len) {
    return filesWrite_ && writeAll(Path(name).s, "ab", data, len);
  }
  bool renameFile(const char* from, const char* to) {
    if (!filesWrite_) return false;
    if (const char* slash = std::strrchr(to, '/')) {
      char dir[80];
      std::snprintf(dir, sizeof dir, "%s/%.*s", kRoot, int(slash - to), to);
      mkdir(dir, 0775);   // EEXIST is the usual answer
    }
    return std::rename(Path(from).s, Path(to).s) == 0;
  }

  nvs_handle_t nvs_ = 0;
  bool slotsUp_ = false, slotsWrite_ = false;
  bool held_ = false;
  bool filesUp_ = false, filesWrite_ = false;
  uint32_t writes_ = 0;
};

}  // namespace hw
