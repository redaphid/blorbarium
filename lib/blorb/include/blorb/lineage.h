#pragma once
// Lineage: the meta-progression. Each life is a run; this is the record the
// runs leave behind, the family tree the phone draws, and the keepsake's last
// line of defence (an unreadable snapshot restarts as an egg of the latest
// genome rebuilt from here).
//
// Storage: an append-only log of CRC'd entries in lineage.log. Genomes are
// diffs from the parent with a full checkpoint at the founding and every
// kCheckpointEvery generations, so any ancestor is at most kCheckpointEvery
// replays away. Death summaries are kept forever; diffs older than
// kKeepDetailGenerations are compacted away when the log passes its cap.
#include <cstdint>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>
#include "blorb/creature.h"
#include "blorb/mutate.h"

namespace blorb {

struct Founding   { uint64_t lineageId; uint32_t at; Genome genome; char species[16]; };
// The chosen egg's diff, plus the seeds of the eggs not chosen ("roads not
// taken"): the phone can preview a sibling while mutateVersion still matches.
struct Birth      { uint16_t generation; uint32_t at; MutationDiff diff; uint8_t chosen, clutchSize;
                    uint64_t seeds[kMaxClutch]; uint8_t mutateVersion; };
struct Checkpoint { uint16_t generation; Genome genome; };
struct Death      { uint16_t generation; uint32_t at; DeathCause cause; LifeStats stats; uint32_t feats; char name[16]; };
struct Rename     { uint16_t generation; char name[16]; };
using LineageEntry = std::variant<Founding, Birth, Checkpoint, Death, Rename>;

constexpr uint16_t kCheckpointEvery = 8;
constexpr uint16_t kKeepDetailGenerations = 32;
constexpr uint32_t kLineageByteCap = 192 * 1024;
constexpr uint8_t kMutateVersion = 1;   // bump when mutate()'s seed-to-child mapping changes

class Storage;     // seams.h
struct Describe;   // protocol.h

// RAM holds only the current generation, name and feats. Entries are read
// from lineage.log on demand (phone requests and boot recovery only; a scan
// of the 192 KB cap is tens of milliseconds), so a long lineage costs flash,
// not RAM.
class Lineage {
 public:
  // Scan once, truncating a torn tail. An empty log founds a lineage from `starter`.
  static Lineage open(Storage&, const Genome& starter, uint64_t lineageId, uint32_t at);

  // Idempotent by (entry kind, generation): a repeat after a crash is a no-op.
  void recordBirth(const Egg&, const Clutch&, uint8_t chosen, uint32_t at);
  void recordDeath(const Creature&, const char* name, uint32_t at);
  void rename(const char* name);

  uint16_t currentGeneration() const { return generation_; }
  uint32_t legacyFeats() const { return legacyFeats_; }   // union of every Death's feats
  const char* currentName() const { return name_; }

  // Streaming reads for the phone: f(const LineageEntry&) per entry, oldest first.
  template <class F> void forEach(F&& f) const;
  std::optional<Genome> genomeOf(uint16_t generation) const;        // nullopt once compacted
  std::optional<MutationDiff> diffOf(uint16_t generation) const;
  void compactIfNeeded();                                           // idempotent

 private:
  void visit(void (*f)(const LineageEntry&, void*), void* ctx) const;   // forEach without the template
  Storage* store_ = nullptr;
  uint64_t id_ = 0;
  uint16_t generation_ = 0;
  uint32_t legacyFeats_ = 0;
  char name_[16] = {};
};

template <class F> void Lineage::forEach(F&& f) const {
  using Fn = std::remove_reference_t<F>;
  visit([](const LineageEntry& e, void* ctx) { (*static_cast<Fn*>(ctx))(e); },
        const_cast<void*>(static_cast<const void*>(&f)));
}

// ---- feats and unlocks ---------------------------------------------------------
#define BLORB_FEAT(bit, name) bool feat_##name(const LifeStats&, uint16_t generation);
#include "blorb/defs/feats.def"
#undef BLORB_FEAT

uint32_t featsOf(const LifeStats&, uint16_t generation);   // OR of the predicates
struct Unlocks { uint8_t clutchSize; Fx wildBonus; };
// clutchSize = 1 + reached_adult + reached_elder (so 1..3); wildBonus = 2% per feat held.
// A new unlock is a change here, and every old save gains it.
Unlocks unlocksFor(uint32_t legacyFeats);

// "gen 4 -> 5: palette #12 (cloak) hue +31, teal -> blue; instinct #41 added:
// cradled + curl lowers fear (learned by gen 4); stimulus #9 woke up".
void describeDiff(const Genome& parent, const MutationDiff&, Describe&);

}  // namespace blorb
