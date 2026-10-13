#include "blorb/provision.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace blorb {
namespace {

constexpr size_t kChunk = 4096;
constexpr size_t kNvsEntry = 32;
constexpr char kSnapshotMagic[4] = {'B', 'L', 'R', 'B'};   // keepsake.cpp's kMagic

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// Reads the whole partition a chunk at a time; `seen` returns true to stop early.
template <class F>
std::optional<Holds> scan(Volume& v, F&& seen) {
  std::unique_ptr<uint8_t[]> chunk(new uint8_t[kChunk]);
  bool blank = true;
  for (size_t at = 0; at < v.size(); at += kChunk) {
    const size_t n = std::min(kChunk, v.size() - at);
    if (!v.readRaw(at, chunk.get(), n)) return Holds::Unreadable;
    for (size_t i = 0; i < n && blank; ++i) blank = chunk[i] == 0xFF;
    if (seen(chunk.get(), n)) return Holds::Pet;
  }
  return blank ? Holds::Blank : Holds::Foreign;
}

bool mayHoldPet(Holds h) { return h == Holds::Pet || h == Holds::Unreadable; }

Opened settle(Volume& v, bool up, bool petAnywhere) {
  if (up) return Opened::Ok;
  if (petAnywhere) return Opened::LeftAlone;
  return v.format() ? Opened::Formatted : Opened::FormatFailed;
}

}  // namespace

Holds slotsHold(Volume& v) {
  return *scan(v, [](const uint8_t* c, size_t n) {
    for (size_t i = 0; i + sizeof kSnapshotMagic <= n; i += kNvsEntry)
      if (std::memcmp(c + i, kSnapshotMagic, sizeof kSnapshotMagic) == 0) return true;
    return false;
  });
}

Holds filesHold(Volume& v, uint32_t blockSize) {
  for (uint32_t block = 0; block < 2; ++block) {
    uint8_t head[32];
    if (!v.readRaw(size_t(block) * blockSize, head, sizeof head)) return Holds::Unreadable;
    if (std::memcmp(head + 8, "littlefs", 8) == 0 && le32(head + 24) == blockSize &&
        le32(head + 28) == v.size() / blockSize)
      return Holds::Pet;
  }
  return *scan(v, [](const uint8_t*, size_t) { return false; });
}

Provisioned provision(Volume& slots, Volume& files, uint32_t fsBlockSize) {
  Provisioned p;
  const bool slotsUp = slots.open(), filesUp = files.open();
  if (slotsUp && filesUp) return p;
  p.slots.held = slotsHold(slots);
  p.files.held = filesHold(files, fsBlockSize);
  const bool petAnywhere = mayHoldPet(*p.slots.held) || mayHoldPet(*p.files.held);
  p.slots.opened = settle(slots, slotsUp, petAnywhere);
  p.files.opened = settle(files, filesUp, petAnywhere);
  return p;
}

const char* describe(const VolumeReport& r) {
  switch (r.opened) {
    case Opened::Ok: return "ok";
    case Opened::Formatted:
      return r.held == Holds::Blank ? "formatted: it was blank"
                                    : "formatted: it held another firmware's bytes and never a pet";
    case Opened::LeftAlone:
      return r.held == Holds::Pet          ? "LEFT ALONE: it holds a damaged blorbarium pet; nothing saves"
             : r.held == Holds::Unreadable ? "LEFT ALONE: it would not read; nothing saves"
                                           : "LEFT ALONE: the other partition holds, or may hold, a blorbarium pet; nothing saves";
    case Opened::FormatFailed: return "FAILED: it would not format; nothing saves";
  }
  return "?";
}

}  // namespace blorb
