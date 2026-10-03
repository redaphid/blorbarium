#pragma once
// Branded ids and the engine's few hard facts.
//
// Every id is a byte (gene uids are two) so a mutation can write any value
// into a gene and still name something: an id no registry row names is legal
// and inert. Ids are distinct types, so a chemical can never be passed where a
// locus is expected.
#include <cstdint>

namespace blorb {

template <class Tag, class Rep = uint8_t>
struct Id {
  Rep v{};
  friend constexpr bool operator==(Id a, Id b) { return a.v == b.v; }
  friend constexpr bool operator!=(Id a, Id b) { return a.v != b.v; }
};

using ChemId   = Id<struct ChemTag>;
using LocusId  = Id<struct LocusTag>;
using StimId   = Id<struct StimTag>;
using DriveId  = Id<struct DriveTag>;
using ActionId = Id<struct ActionTag>;
using PoseId   = Id<struct PoseTag>;
using ExprId   = Id<struct ExprTag>;
using RegionId = Id<struct RegionTag>;
using ReflexId = Id<struct ReflexTag>;
using CareId   = Id<struct CareTag>;
using ThoughtId = Id<struct ThoughtTag>;
using TopicId   = Id<struct TopicTag>;
using VoiceId   = Id<struct VoiceTag>;
using GeneUid  = Id<struct GeneUidTag, uint16_t>;   // a gene's identity across generations; never reused in a lineage

// Life stages. Each maps to stage art: Baby and Child draw the hatchling,
// Adult the adult, Elder the old frog. The egg and the remains are not stages
// (they are Occupant alternatives, creature.h).
enum class Stage : uint8_t { Baby = 0, Child = 1, Adult = 2, Elder = 3 };
constexpr uint8_t kStageCount = 4;

// The engine's only hard-coded chemistry fact: drive n is chemical 1 + n.
constexpr uint8_t kDriveBase = 1;
constexpr ChemId driveChem(DriveId d) { return ChemId{uint8_t(kDriveBase + d.v)}; }

constexpr uint16_t kChemSlots = 256;
constexpr uint16_t kLocusSlots = 256;
constexpr uint8_t kRecentBase = 64;   // recent-stimulus locus for stim s is 64 + s (registry.h)

// Cadence. Every genetic rate is in ticks, never milliseconds.
constexpr uint32_t kTickMs = 100;        // 10 Hz body tick
constexpr uint32_t kSampleMs = 20;       // 50 Hz IMU sampling; detectors need it (shake jolts are brief)
constexpr uint32_t kBrainEvery = 2;      // brain at 5 Hz
constexpr uint32_t kTicksPerMinute = 60000 / kTickMs;
constexpr uint32_t kTicksPerHour = 60 * kTicksPerMinute;

constexpr uint8_t kMaxClutch = 3;

}  // namespace blorb
