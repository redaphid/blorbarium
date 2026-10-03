#pragma once
// Keepsake: the atomic, versioned snapshot. Owns the on-disk format end to
// end (encode, decode, migrate). Nothing else knows the layout.
//
// Layout: header {magic "BLRB", formatVersion u16, seq u32, len u32, crc32 u32}
// then TLV chunks {tag u8, ver u8, len u16, bytes}. Two slots, snap.a and
// snap.b, written alternately through Storage::writeAtomic; load takes the
// highest seq whose CRC verifies.
//
// Rules that keep a keepsake safe:
//  * A chunk tag this firmware does not know is carried verbatim and re-emitted.
//  * A snapshot from a newer format is never overwritten: the Dish boots
//    ReadOnlyNewer and saves nothing.
//  * Unreadable slots are renamed into rescue/, never deleted or formatted.
//  * Derived state is not saved: phenotype (re-expressed), detector state,
//    clutch previews, the face crossfade.
#include <cstdint>
#include <optional>
#include <vector>
#include "blorb/creature.h"
#include "blorb/habitat.h"
#include "blorb/senses.h"

namespace blorb {

class Storage;   // seams.h

constexpr uint16_t kFormatVersion = 1;

enum class Chunk : uint8_t {
  Clock = 1,       // PetClock
  Occupant = 2,    // Egg | Creature | Clutch discriminator and its fields
  Genome = 3,      // the occupant's genome (Clutch: the parent's)
  Chemistry = 4,   // chem[256], locus[256] as Fx
  Brain = 5,       // axes (feature, action, drive ids), then W as Q15, habit, episodes, instinct queue
  Body = 6,
  Stats = 7,
  Habitat = 8,
  Rng = 9,
  Settings = 10,
};

struct Settings { char name[16]; uint64_t lineageId; uint32_t speciesSeed; uint8_t brightness; };
struct RawChunk { uint8_t tag, ver; std::vector<uint8_t> bytes; };

struct Snapshot {
  uint32_t seq;
  PetClock clock;
  Occupant occupant;
  Habitat habitat;
  Settings settings;
  std::vector<RawChunk> unknown;   // newer firmware's chunks, carried
};

enum class SlotState : uint8_t { Resumed, FellBack, Empty, Corrupt, NewerFormat };
struct Loaded { SlotState state; std::optional<Snapshot> snapshot; };

class Keepsake {
 public:
  explicit Keepsake(Storage&);
  Loaded load();                       // Corrupt slots are quarantined before returning
  bool save(const Snapshot&);          // the other slot, seq + 1; saving twice leaves two valid slots

  // Pure codec, for tests and for BACKUP / RESTORE.
  static std::vector<uint8_t> encode(const Snapshot&);
  static std::optional<Snapshot> decode(const uint8_t*, size_t);
  // One pure step per version, each with a committed fixture test/fixtures/keepsake_v<n>.bin.
  static bool migrate(std::vector<uint8_t>& blob, uint16_t fromVersion);

 private:
  Storage& store_;
  uint32_t lastSeq_ = 0;
  bool nextIsA_ = true;
};

// When to save. Every 5 minutes if anything changed, and at once on a stage
// change, death, clutch pick, hatch, rename, edit, TIME and disconnect. There
// is no shutdown save: phone power vanishes without warning.
struct SavePolicy {
  static constexpr uint32_t kIntervalTicks = 5 * kTicksPerMinute;
  static bool due(uint32_t tick, uint32_t lastSaveTick, bool eventDirty);
};

}  // namespace blorb
