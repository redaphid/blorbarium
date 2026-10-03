#pragma once
// Storage in memory, with fault injection: tearAt(k) makes the next write
// stop after k bytes, as a power cut would. A torn append keeps those k bytes;
// a torn writeAtomic leaves the old file whole (temp + rename). largestRead
// is the most one read() ever asked for: what a reader must hold in RAM.
#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "blorb/seams.h"

namespace blorbtest {

struct MemStorage : blorb::Storage {
  std::map<std::string, std::vector<uint8_t>> files;
  std::optional<size_t> tear;
  size_t largestRead = 0;

  void tearAt(size_t bytes) { tear = bytes; }

  std::optional<size_t> read(const char* name, size_t offset, uint8_t* buf, size_t cap) override {
    largestRead = std::max(largestRead, cap);
    auto it = files.find(name);
    if (it == files.end()) return std::nullopt;
    if (offset >= it->second.size()) return 0;
    size_t n = std::min(cap, it->second.size() - offset);
    if (n) std::memcpy(buf, it->second.data() + offset, n);
    return n;
  }
  bool writeAtomic(const char* name, const uint8_t* data, size_t len) override {
    if (takeTear()) return false;
    files[name].assign(data, data + len);
    return true;
  }
  bool append(const char* name, const uint8_t* data, size_t len) override {
    std::vector<uint8_t>& f = files[name];
    if (tear && *tear < len) {
      f.insert(f.end(), data, data + *tear);
      tear.reset();
      return false;
    }
    tear.reset();
    f.insert(f.end(), data, data + len);
    return true;
  }
  bool rename(const char* from, const char* to) override {
    auto it = files.find(from);
    if (it == files.end()) return false;
    files[to] = std::move(it->second);
    files.erase(it);
    return true;
  }
  size_t size(const char* name) override {
    auto it = files.find(name);
    return it == files.end() ? 0 : it->second.size();
  }

 private:
  bool takeTear() {
    bool torn = tear.has_value();
    tear.reset();
    return torn;
  }
};

}  // namespace blorbtest
