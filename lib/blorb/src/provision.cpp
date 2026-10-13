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

// The second half of boot's decision, once both partitions are judged. A
// sealed board writes nothing, so nothing is formatted or opened for writing.
void act(Volume& v, bool up, bool sealed, bool hold, VolumeReport& r) {
  if (r.opened == Opened::LeftAlone) return;
  if (up) r.opened = sealed || hold || v.writable() ? Opened::Ok : Opened::ReadOnly;
  else if (sealed) r.opened = Opened::LeftAlone;
  else if (hold) r.opened = Opened::HeldForTrial;
  else r.opened = v.format() ? Opened::Formatted : Opened::FormatFailed;
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

Provisioned provision(Volume& slots, Volume& files, uint32_t fsBlockSize, bool hold) {
  Provisioned p;
  const bool slotsUp = slots.open(), filesUp = files.open();
  if (!slotsUp) {
    p.slots.held = slotsHold(slots);
    if (mayHoldPet(*p.slots.held)) p.slots.opened = Opened::LeftAlone;
  }
  if (!filesUp) {
    p.files.held = filesHold(files, fsBlockSize);
    // Junk in petfs beside a pet is a half-damaged keepsake whose lineage may be in it.
    const bool besidePet =
        p.files.held == Holds::Foreign && mayHoldPet(p.slots.held ? *p.slots.held : slotsHold(slots));
    if (mayHoldPet(*p.files.held) || besidePet) p.files.opened = Opened::LeftAlone;
  }
  const bool sealed = leftAlone(p);
  act(slots, slotsUp, sealed, hold, p.slots);
  act(files, filesUp, sealed, hold, p.files);
  return p;
}

const char* describe(const VolumeReport& r) {
  switch (r.opened) {
    case Opened::Ok: return "ok";
    case Opened::Formatted:
      return r.held == Holds::Blank ? "formatted: it was blank"
                                    : "formatted: it held another firmware's bytes and never a pet";
    case Opened::LeftAlone:
      return r.held == Holds::Pet       ? "LEFT ALONE: it holds a damaged blorbarium pet; nothing saves"
             : r.held == Holds::Foreign ? "LEFT ALONE: unknown bytes beside a blorbarium pet; nothing saves"
             : r.held == Holds::Blank   ? "LEFT ALONE: blank, beside a blorbarium pet; nothing saves"
                                        : "LEFT ALONE: it would not read; nothing saves";
    case Opened::FormatFailed: return "FAILED: it would not format; nothing saves";
    case Opened::ReadOnly: return "FAILED: it opened but not for writing; nothing saves";
    case Opened::HeldForTrial: return "HELD: it would be formatted, but this image is on trial; its rollback decides";
  }
  return "?";
}

}  // namespace blorb
