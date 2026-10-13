// The Storage seam on the 1.28's flash (DEVIATIONS.md 5). The snapshot slots
// are blobs in the labelled `pet` NVS partition; everything else is a file on
// the `petfs` LittleFS. One that will not open is formatted only when neither
// holds a blorbarium pet (blorb/provision.h); otherwise it stays shut and its
// writes fail.
#pragma once

#include <LittleFS.h>
#include <Preferences.h>
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
  blorb::Provisioned begin() {
    SlotsVolume slots(prefs_);
    FilesVolume files;
    const blorb::Provisioned p = blorb::provision(slots, files, kBlock);
    slotsUp_ = isUp(p.slots.opened);
    filesUp_ = isUp(p.files.opened);
    return p;
  }
  uint32_t writes() const { return writes_; }

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
  class SlotsVolume : public Partition {
   public:
    explicit SlotsVolume(Preferences& prefs)
        : Partition(esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "pet")),
          prefs_(prefs) {}
    bool open() override { return prefs_.begin("blorb", false, "pet"); }
    bool format() override { return nvs_flash_erase_partition("pet") == ESP_OK && open(); }
   private:
    Preferences& prefs_;
  };
  class FilesVolume : public Partition {
   public:
    FilesVolume()
        : Partition(esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "petfs")) {}
    bool open() override { return LittleFS.begin(false, kRoot, 4, "petfs"); }
    bool format() override { return LittleFS.begin(true, kRoot, 4, "petfs"); }
  };
  static bool isUp(blorb::Opened o) { return o == blorb::Opened::Ok || o == blorb::Opened::Formatted; }

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
    return whereOf(name) == Where::Slot ? prefs_.remove(name) : filesUp() && std::remove(Path(name).s) == 0;
  }
  // The one cross case is a torn slot quarantined into rescue/ on petfs.
  bool move(const char* from, const char* to) {
    std::optional<size_t> n = lengthOf(from);
    if (!n) return false;
    std::vector<uint8_t> whole(*n);
    if (read(from, 0, whole.data(), *n) != n) return false;
    return put(to, whole.data(), *n) && erase(from);
  }

  // ---- slots: NVS blobs ----
  // isKey first: getBytesLength logs an error for every missing key.
  std::optional<size_t> slotLength(const char* name) {
    if (!slotsUp_ || !prefs_.isKey(name)) return std::nullopt;
    return prefs_.getBytesLength(name);
  }
  // getBytes wants a buffer the blob's whole size, so a partial range reads through a copy.
  std::optional<size_t> readSlot(const char* name, size_t offset, uint8_t* buf, size_t cap) {
    std::optional<size_t> len = slotLength(name);
    if (!len) return std::nullopt;
    if (cap == 0 || offset >= *len) return 0;
    if (offset == 0 && cap >= *len) return prefs_.getBytes(name, buf, *len);
    std::vector<uint8_t> whole(*len);
    if (prefs_.getBytes(name, whole.data(), *len) != *len) return 0;
    const size_t n = std::min(cap, *len - offset);
    std::memcpy(buf, whole.data() + offset, n);
    return n;
  }
  bool putSlot(const char* name, const uint8_t* data, size_t len) {
    return slotsUp_ && len > 0 && prefs_.putBytes(name, data, len) == len;   // NVS replaces a key atomically
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
    if (!filesUp()) return false;
    char tmp[80];
    std::snprintf(tmp, sizeof tmp, "%s/%s.tmp", kRoot, name);
    return writeAll(tmp, "wb", data, len) && std::rename(tmp, Path(name).s) == 0;   // LittleFS replaces atomically
  }
  bool appendFile(const char* name, const uint8_t* data, size_t len) {
    return filesUp() && writeAll(Path(name).s, "ab", data, len);
  }
  bool renameFile(const char* from, const char* to) {
    if (!filesUp()) return false;
    if (const char* slash = std::strrchr(to, '/')) {
      char dir[80];
      std::snprintf(dir, sizeof dir, "%s/%.*s", kRoot, int(slash - to), to);
      mkdir(dir, 0775);   // EEXIST is the usual answer
    }
    return std::rename(Path(from).s, Path(to).s) == 0;
  }

  Preferences prefs_;
  bool slotsUp_ = false;
  bool filesUp_ = false;
  uint32_t writes_ = 0;
};

}  // namespace hw
