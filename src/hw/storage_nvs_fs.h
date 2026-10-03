// The Storage seam on the 1.28's flash (DEVIATIONS.md 5). The snapshot slots
// are blobs in the labelled `pet` NVS partition; everything else is a file on
// the `petfs` LittleFS. Neither is ever formatted over data: a partition that
// will not open stays shut and its writes fail.
#pragma once

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_partition.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include "blorb/seams.h"

namespace hw {

enum class Where { Slot, File };

inline Where whereOf(const char* name) { return std::strncmp(name, "snap.", 5) == 0 ? Where::Slot : Where::File; }

struct StorageState {
  enum class Files { Ok, FormattedBlank, Failed };
  bool slots = false;
  Files files = Files::Failed;
  const char* why = "not begun";   // when files == Failed
};

class NvsFsStorage : public blorb::Storage {
 public:
  StorageState begin() {
    state_.slots = prefs_.begin("blorb", false, "pet");
    state_.files = mountFiles(state_.why);
    return state_;
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
  static constexpr size_t kChunk = 4096;

  struct Path {
    char s[80];
    explicit Path(const char* name) { std::snprintf(s, sizeof s, "%s/%s", kRoot, name); }
  };

  bool counted(bool ok) {
    if (ok) ++writes_;
    return ok;
  }
  bool filesUp() const { return state_.files != StorageState::Files::Failed; }

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
    if (!state_.slots || !prefs_.isKey(name)) return std::nullopt;
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
    return state_.slots && len > 0 && prefs_.putBytes(name, data, len) == len;   // NVS replaces a key atomically
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

  // The keepsake rule: only a partition that was never written is formatted.
  StorageState::Files mountFiles(const char*& why) {
    if (LittleFS.begin(false, kRoot, 4, "petfs")) return StorageState::Files::Ok;
    const esp_partition_t* p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "petfs");
    if (!p) {
      why = "no petfs partition";
      return StorageState::Files::Failed;
    }
    std::unique_ptr<uint8_t[]> chunk(new uint8_t[kChunk]);
    for (size_t at = 0; at < p->size; at += kChunk) {
      if (esp_partition_read(p, at, chunk.get(), kChunk) != ESP_OK) {
        why = "mount failed and the partition would not read; left alone";
        return StorageState::Files::Failed;
      }
      for (size_t i = 0; i < kChunk; ++i)
        if (chunk[i] != 0xFF) {
          why = "mount failed on a written partition; left alone";
          return StorageState::Files::Failed;
        }
    }
    if (LittleFS.begin(true, kRoot, 4, "petfs")) return StorageState::Files::FormattedBlank;
    why = "blank partition would not format";
    return StorageState::Files::Failed;
  }

  Preferences prefs_;
  StorageState state_;
  uint32_t writes_ = 0;
};

}  // namespace hw
