#pragma once
// The registries. Every extensible noun is one row in a defs/*.def file; this
// header expands each table once into typed constants (stim::shake), a
// constexpr table the engine iterates and SCHEMA streams to the phone, and a
// derived count. There is no second list to keep in sync: brain dimensions,
// the feature vector, recent-stimulus loci and the save remap all derive from
// the rows. Ids are explicit and stable, so a new row never reinterprets an
// old save or genome.
//
// Three more tables expand where their behaviour lives: gene kinds (genes.h),
// detectors (senses.h) and phone verbs (protocol.h).
#include <array>
#include <cstddef>
#include <cstdint>
#include "blorb/ids.h"

namespace blorb {

enum class LocusDir : uint8_t { Sense, Act, Free };
enum class StimSource : uint8_t { Body, World, Self, Phone };

// ---- typed constants ---------------------------------------------------------
namespace chem {
#define BLORB_CHEM(id, name, rgb, show) inline constexpr ChemId name{id};
#include "blorb/defs/chemicals.def"
#undef BLORB_CHEM
}  // namespace chem
namespace drive {
#define BLORB_DRIVE(id, name) inline constexpr DriveId name{id};
#include "blorb/defs/drives.def"
#undef BLORB_DRIVE
}  // namespace drive
namespace stim {
inline constexpr StimId none{255};
#define BLORB_STIM(id, name, src, sit) inline constexpr StimId name{id};
#include "blorb/defs/stimuli.def"
#undef BLORB_STIM
}  // namespace stim
namespace locus {
#define BLORB_LOCUS(id, name, dir, sit) inline constexpr LocusId name{id};
#include "blorb/defs/loci.def"
#undef BLORB_LOCUS
constexpr LocusId recent(StimId s) { return LocusId{uint8_t(kRecentBase + s.v)}; }
}  // namespace locus
namespace action {
#define BLORB_ACTION(id, name, Type, minTicks, self) inline constexpr ActionId name{id};
#include "blorb/defs/actions.def"
#undef BLORB_ACTION
}  // namespace action
namespace pose {
#define BLORB_POSE(id, name) inline constexpr PoseId name{id};
#include "blorb/defs/poses.def"
#undef BLORB_POSE
}  // namespace pose
namespace expr {
#define BLORB_EXPR(id, name) inline constexpr ExprId name{id};
#include "blorb/defs/expressions.def"
#undef BLORB_EXPR
}  // namespace expr
namespace region {
#define BLORB_REGION(id, name) inline constexpr RegionId name{id};
#include "blorb/defs/regions.def"
#undef BLORB_REGION
}  // namespace region
namespace reflex {
#define BLORB_REFLEX(id, name, loc, ticks, face, p) inline constexpr ReflexId name{id};
#include "blorb/defs/reflexes.def"
#undef BLORB_REFLEX
}  // namespace reflex
namespace care {
#define BLORB_CARE(id, name, d, s) inline constexpr CareId name{id};
#include "blorb/defs/care.def"
#undef BLORB_CARE
}  // namespace care
namespace feat {
#define BLORB_FEAT(bit, name) inline constexpr uint32_t name = uint32_t(1) << (bit);
#include "blorb/defs/feats.def"
#undef BLORB_FEAT
}  // namespace feat

// ---- rows ----------------------------------------------------------------------
struct ChemInfo   { ChemId id; const char* name; uint16_t rgb565; bool show; };
struct DriveInfo  { DriveId id; const char* name; };
enum class Situation : uint8_t { None, Cue, Context };   // loci.def's situation column
struct LocusInfo  { LocusId id; const char* name; LocusDir dir; Situation situation; };
struct StimInfo   { StimId id; const char* name; StimSource source; bool situation; };
struct ActionInfo { ActionId id; const char* name; uint16_t minTicks; StimId selfStim; };
struct PoseInfo   { PoseId id; const char* name; };
struct ExprInfo   { ExprId id; const char* name; };
struct RegionInfo { RegionId id; const char* name; };
struct ReflexInfo { ReflexId id; const char* name; LocusId trigger; uint16_t ticks; ExprId face; PoseId pose; };
struct CareInfo   { CareId id; const char* name; DriveId drive; StimId gesture; };
struct FeatInfo   { uint8_t bit; const char* name; };

inline constexpr ChemInfo CHEMICALS[] = {
#define BLORB_CHEM(id, name, rgb, show) {ChemId{id}, #name, rgb, show != 0},
#include "blorb/defs/chemicals.def"
#undef BLORB_CHEM
};
inline constexpr DriveInfo DRIVES[] = {
#define BLORB_DRIVE(id, name) {DriveId{id}, #name},
#include "blorb/defs/drives.def"
#undef BLORB_DRIVE
};
inline constexpr LocusInfo LOCI[] = {
#define BLORB_LOCUS(id, name, dir, sit) {LocusId{id}, #name, LocusDir::dir, Situation(sit)},
#include "blorb/defs/loci.def"
#undef BLORB_LOCUS
};
inline constexpr StimInfo STIMULI[] = {
#define BLORB_STIM(id, name, src, sit) {StimId{id}, #name, StimSource::src, sit != 0},
#include "blorb/defs/stimuli.def"
#undef BLORB_STIM
};
inline constexpr ActionInfo ACTIONS[] = {
#define BLORB_ACTION(id, name, Type, minTicks, self) {ActionId{id}, #name, minTicks, stim::self},
#include "blorb/defs/actions.def"
#undef BLORB_ACTION
};
inline constexpr PoseInfo POSES[] = {
#define BLORB_POSE(id, name) {PoseId{id}, #name},
#include "blorb/defs/poses.def"
#undef BLORB_POSE
};
inline constexpr ExprInfo EXPRESSIONS[] = {
#define BLORB_EXPR(id, name) {ExprId{id}, #name},
#include "blorb/defs/expressions.def"
#undef BLORB_EXPR
};
inline constexpr RegionInfo REGIONS[] = {
#define BLORB_REGION(id, name) {RegionId{id}, #name},
#include "blorb/defs/regions.def"
#undef BLORB_REGION
};
inline constexpr ReflexInfo REFLEXES[] = {
#define BLORB_REFLEX(id, name, loc, ticks, face, p) {ReflexId{id}, #name, locus::loc, ticks, expr::face, pose::p},
#include "blorb/defs/reflexes.def"
#undef BLORB_REFLEX
};
inline constexpr CareInfo CARES[] = {
#define BLORB_CARE(id, name, d, s) {CareId{id}, #name, drive::d, stim::s},
#include "blorb/defs/care.def"
#undef BLORB_CARE
};
inline constexpr FeatInfo FEATS[] = {
#define BLORB_FEAT(bit, name) {bit, #name},
#include "blorb/defs/feats.def"
#undef BLORB_FEAT
};

// ---- derived dimensions --------------------------------------------------------
template <class T, size_t N> constexpr size_t countOf(const T (&)[N]) { return N; }
inline constexpr size_t kDriveCount = countOf(DRIVES);
inline constexpr size_t kActionCount = countOf(ACTIONS);
inline constexpr size_t kRegionCount = countOf(REGIONS);
inline constexpr size_t kReflexCount = countOf(REFLEXES);

// The brain's feature vector: every situation locus, then the recent locus of
// every situation stimulus. Order is table order; saves key weights by locus id.
constexpr size_t countFeatures() {
  size_t n = 0;
  for (const LocusInfo& l : LOCI) n += l.situation != Situation::None ? 1 : 0;
  for (const StimInfo& s : STIMULI) n += s.situation ? 1 : 0;
  return n;
}
inline constexpr size_t kFeatureCount = countFeatures();
constexpr std::array<LocusId, kFeatureCount> buildFeatures() {
  std::array<LocusId, kFeatureCount> out{};
  size_t i = 0;
  for (const LocusInfo& l : LOCI) if (l.situation != Situation::None) out[i++] = l.id;
  for (const StimInfo& s : STIMULI) if (s.situation) out[i++] = locus::recent(s.id);
  return out;
}
inline constexpr std::array<LocusId, kFeatureCount> FEATURES = buildFeatures();

// ---- compile-time self-check ----------------------------------------------------
template <class T, size_t N>
constexpr bool idsUnique(const T (&rows)[N]) {
  for (size_t i = 0; i < N; ++i)
    for (size_t j = i + 1; j < N; ++j)
      if (rows[i].id == rows[j].id) return false;
  return true;
}
constexpr bool situationsKnown() {
  for (const LocusInfo& l : LOCI) if (uint8_t(l.situation) > uint8_t(Situation::Context)) return false;
  return true;
}
constexpr bool stimIdsFitRecentRange() {
  for (const StimInfo& s : STIMULI) if (s.id.v >= 64) return false;
  return true;
}
static_assert(idsUnique(CHEMICALS) && idsUnique(DRIVES) && idsUnique(LOCI) && idsUnique(STIMULI) &&
              idsUnique(ACTIONS) && idsUnique(POSES) && idsUnique(EXPRESSIONS) && idsUnique(REGIONS) &&
              idsUnique(REFLEXES) && idsUnique(CARES), "a registry has a duplicate id");
static_assert(situationsKnown(), "a locus situation is 0, 1 (cue) or 2 (context)");
static_assert(stimIdsFitRecentRange(), "stimulus ids must be < 64 so their recent loci fit 64..127");
static_assert(kDriveCount <= 15, "drive chemicals are 1..15; 16 is life");
static_assert(countOf(FEATS) <= 32, "feats are a 32-bit set");

}  // namespace blorb
