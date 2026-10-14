#pragma once
// The friend egg (docs/design/SPECIES.md, "Crossing"). After a duel, if both
// kids hug their own pets, the two devices swap genomes and each pet's next
// clutch gains a speckled egg: a cross of the two.
//
// Crossing is allele exchange at shared loci, over an allowlist. Every recipe
// gene sits at a fixed uid on every device (species.h). crossDiff walks MY
// genome in order, in linkage blocks (the source parent switches about once
// every 8 genes), and where the friend's gene has the same uid, type and
// length and its kind is crossable, copies the friend's body bytes over mine.
// Each copied byte is a MutPoint, so the cross is an ordinary MutationDiff:
//
//   apply(mine, crossDiff + mutateDiff) == child      (no friend genome needed)
//
// and lineage.log's Birth entry, kMutateVersion and the replay are unchanged.
// Chemistry, stimulus, instinct and Life genes never cross: the friend's bytes
// came over an unencrypted radio, and viability() only feeds a candidate for
// 48 pet-hours, so it would not catch a crossed `knock -> injury`.
#include <cstdint>
#include <optional>
#include "blorb/creature.h"
#include "blorb/genes.h"
#include "blorb/genome.h"
#include "blorb/mutate.h"
#include "blorb/species.h"

namespace blorb {

class Storage;   // seams.h

// Gene kinds a cross may copy: looks, faces, temperament, knack, species and
// power. Never the mutation-policy gene, so a friend cannot make my line
// mutate faster.
inline constexpr uint8_t kCrossableKinds[] = {
    GeneKindOf<PaletteGene>::value,    GeneKindOf<SizeGene>::value,  GeneKindOf<MarkGene>::value,
    GeneKindOf<ExpressionGene>::value, GeneKindOf<TemperamentGene>::value, GeneKindOf<KnackGene>::value,
    GeneKindOf<SpeciesGene>::value,    GeneKindOf<PowerGene>::value};
inline constexpr uint8_t kNeverCrossed[] = {
    GeneKindOf<ChemGene>::value,     GeneKindOf<ReactionGene>::value,       GeneKindOf<EmitterGene>::value,
    GeneKindOf<ReceptorGene>::value, GeneKindOf<StimulusGene>::value,       GeneKindOf<InstinctGene>::value,
    GeneKindOf<MutationPolicyGene>::value, GeneKindOf<EggGene>::value, GeneKindOf<MetabolismGene>::value,
    GeneKindOf<NoteGene>::value};
constexpr bool crossKindsAreSafe() {
  for (uint8_t k : kCrossableKinds)
    for (uint8_t n : kNeverCrossed)
      if (k == n) return false;
  return true;
}
static_assert(crossKindsAreSafe(), "a cross never copies chemistry, stimulus, instinct or Life genes");
constexpr bool crossable(uint8_t kind) {
  for (uint8_t k : kCrossableKinds) if (k == kind) return true;
  return false;
}

// The waiting egg's facts. battleSeed is the cross's seed, the one the clutch
// keeps as friendSeed, so a friend.egg that is not this clutch's is never used.
struct FriendEggRecord {
  uint64_t friendLineage = 0;
  SpeciesId friendSpecies{};
  uint32_t battleSeed = 0;
  uint32_t friendGenomeHash = 0;
};
struct FriendEgg { FriendEggRecord record; Genome genome; };

// The receiving boundary: structural damage refused by Genome::parse, the hash
// the State packet announced, and at least one uid shared with mine (a genome
// from another engine shares none). nullopt: the egg fizzles into sparkles.
std::optional<Genome> acceptFriendGenome(const uint8_t* bytes, size_t len, uint32_t announcedHash,
                                         const Genome& mine);

// Pure. Only MutPoint ops, only where the bytes differ, only at loci both
// genomes share with equal type and length, only for kCrossableKinds.
MutationDiff crossDiff(const Genome& mine, const Genome& friends, Rng&);

// At layClutch, when a friend egg waits: it joins WITHIN kMaxClutch. A clutch
// of 1 or 2 grows by one; a full clutch gives its last slot. The saved count
// stays 1..3, so neither the keepsake nor lineage's three seeds change shape.
void joinClutch(Clutch&, uint32_t crossSeed);

// Egg i of the clutch. The friend slot, with this clutch's friend egg, is the
// cross: crossed = apply(parent, crossDiff) must pass viability(), then
// child = mutate(crossed) must too, because mutate()'s last fallback hands its
// input back unchecked. Anything else (a fizzled check, no friend.egg, one
// from another hug) is the plain egg Clutch::child(i) gives.
Offspring childOf(const Clutch&, uint8_t i, const FriendEgg*);

// friend.egg on petfs, a new file an older firmware never opens. One at a
// time: a newer hug replaces it. Consumed at the Birth (any egg picked), not
// when the clutch is laid, so a reboot during the 30-minute pick keeps the
// friend's genome. Renamed to friend.used, never deleted.
struct FriendEggStore {
  static constexpr const char* kFile = "friend.egg";
  static constexpr const char* kUsed = "friend.used";
  static std::optional<FriendEgg> load(Storage&);
  static bool save(Storage&, const FriendEgg&);
  static bool consume(Storage&);
};

}  // namespace blorb
