#pragma once
// The append-only CRC'd log both lineage.log and duels.log are made of. Each
// entry: [kind u8][len u16 LE][payload][crc32 LE of kind, len and payload].
// A reader walks whole, CRC-valid frames and stops at the first that is not,
// so a torn tail hides nothing before it and is cut off by the next open.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "blorb/protocol.h"
#include "blorb/seams.h"

namespace blorb::logframes {

constexpr size_t kFrame = 1 + 2 + 4;

struct Out {
  std::vector<uint8_t> b;
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v) { u8(uint8_t(v)), u8(uint8_t(v >> 8)); }
  void u32(uint32_t v) { u16(uint16_t(v)), u16(uint16_t(v >> 16)); }
  void u64(uint64_t v) { u32(uint32_t(v)), u32(uint32_t(v >> 32)); }
  void raw(const void* p, size_t n) { b.insert(b.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n); }
  void blob(const std::vector<uint8_t>& v) { u16(uint16_t(v.size())), raw(v.data(), v.size()); }
};

// Reads past the end yield zeros and clear `ok`, so a short payload is one check.
struct In {
  const uint8_t* p;
  size_t n, at = 0;
  bool ok = true;
  uint8_t u8() {
    if (at >= n) { ok = false; return 0; }
    return p[at++];
  }
  uint16_t u16() { uint16_t lo = u8(); return uint16_t(lo | u8() << 8); }
  uint32_t u32() { uint32_t lo = u16(); return lo | uint32_t(u16()) << 16; }
  uint64_t u64() { uint64_t lo = u32(); return lo | uint64_t(u32()) << 32; }
  void raw(void* out, size_t len) {
    if (n - at < len) { ok = false; std::memset(out, 0, len); return; }
    std::memcpy(out, p + at, len);
    at += len;
  }
  std::vector<uint8_t> blob() {
    std::vector<uint8_t> v(u16());
    raw(v.data(), v.size());
    return v;
  }
};

inline std::vector<uint8_t> frame(uint8_t kind, const Out& payload) {
  Out o;
  o.u8(kind);
  o.u16(uint16_t(payload.b.size()));
  o.raw(payload.b.data(), payload.b.size());
  o.u32(crc32(o.b.data(), o.b.size()));
  return o.b;
}

// Names one log and the temp file a rewrite of it streams into.
struct LogFile { const char* log; const char* tmp; };

// Walks whole, CRC-valid frames in the first `limit` bytes of the log. Each
// frame is read from flash on its own, so RAM holds one entry and never the
// log. f(kind, payload, frame bytes, offset). Returns the valid prefix length.
template <class F>
size_t walk(Storage& s, LogFile file, size_t limit, F&& f) {
  const size_t size = std::min(limit, s.size(file.log));
  std::vector<uint8_t> frame;
  size_t at = 0;
  while (size - at >= kFrame) {
    uint8_t head[3];
    if (s.read(file.log, at, head, sizeof head) != sizeof head) break;
    const size_t len = size_t(head[1]) | size_t(head[2]) << 8;
    if (size - at - kFrame < len) break;
    frame.resize(kFrame + len);
    if (s.read(file.log, at, frame.data(), frame.size()) != frame.size()) break;
    const uint8_t* crcAt = &frame[3 + len];
    uint32_t crc = uint32_t(crcAt[0]) | uint32_t(crcAt[1]) << 8 | uint32_t(crcAt[2]) << 16 | uint32_t(crcAt[3]) << 24;
    if (crc32(frame.data(), 3 + len) != crc) break;
    f(frame[0], In{&frame[3], len}, frame, at);
    at += frame.size();
  }
  return at;
}

// Rewrites the log as `head` followed by the valid frames in its first
// `limit` bytes that keep(kind, payload, offset) accepts. They stream into the
// temp file, which is renamed over the log once whole, so a power cut leaves
// the old log as it was.
template <class Keep>
bool rewrite(Storage& s, LogFile file, size_t limit, Keep&& keep, const std::vector<uint8_t>& head = {}) {
  if (!s.writeAtomic(file.tmp, head.data(), head.size())) return false;
  bool ok = true;
  walk(s, file, limit, [&](uint8_t k, In in, const std::vector<uint8_t>& frame, size_t at) {
    if (ok && keep(k, in, at)) ok = s.append(file.tmp, frame.data(), frame.size());
  });
  return ok && s.rename(file.tmp, file.log);
}

inline bool keepAll(uint8_t, In, size_t) { return true; }

// Cuts a torn tail off: the valid prefix is all a reader would ever see.
inline size_t truncateTorn(Storage& s, LogFile file, size_t valid) {
  if (valid < s.size(file.log)) rewrite(s, file, valid, keepAll);
  return valid;
}

// All of `bytes` lands, or the log is put back as it was: a torn append left
// in place would hide every later entry behind it.
inline bool appendWhole(Storage& s, LogFile file, const std::vector<uint8_t>& bytes) {
  size_t before = s.size(file.log);
  if (s.append(file.log, bytes.data(), bytes.size())) return true;
  rewrite(s, file, before, keepAll);
  return false;
}

}  // namespace blorb::logframes
