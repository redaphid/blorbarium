#pragma once
// SKETCH (docs/design/SPECIES.md, "Crossing"). The friend egg: after a duel,
// if BOTH kids hug their own pets (a 3 s cradle on the friends screen, each on
// their own device), the two devices swap genomes and each pet's next clutch
// gains one extra, speckled egg: a cross of the two. Graft from candidate B,
// re-expressed so it needs no new lineage op.
//
// Crossing is allele exchange at shared loci, over an allowlist. Every
// recipe gene sits at a fixed uid on every device (species.h), so the nine
// species and both builds share them. crossDiff walks MY genome by uid in
// linkage blocks (the source parent switches about once every 8 genes) and,
// where the friend's gene has the same uid, type and length AND its kind is
// in kCrossableKinds, copies the friend's body bytes over mine. Each copied
// byte is a MutPoint (apply() checks only the offset and the `from` byte), so
// the cross is an ordinary MutationDiff:
//
//   apply(mine, crossDiff + mutateDiff) == child      (no friend genome needed)
//
// and lineage.log's Birth entry, kMutateVersion and the replay are unchanged.
// Only looks, temperament, faces, knack, species and power cross. Chemistry
// (chem, reaction, emitter, receptor), stimulus, instinct and Life genes
// never do: the friend's bytes came over an unencrypted radio, and viability()
// only feeds a candidate for 48 pet-hours, so it would not catch a crossed
// `knock -> injury`. Genes only the friend has never cross either.
// SpeciesGene and PowerGene sit at uids far apart (kSpeciesGeneUid,
// kPowerGeneUid) with the knack between, so about half of friend eggs are
// hybrids (an estimate from the block rate; D7 measures it).
#include <cstdint>
#include <optional>
#include "blorb/fixed.h"
#include "blorb/genome.h"
#include "blorb/mutate.h"
#include "blorb/seams.h"
#include "blorb/species.h"

namespace blorb {

// Gene kinds a cross may copy (gene_kinds.def rows): palette, size, mark,
// expression, temperament, knack, species, power. A v2 part joins when it lands.
inline constexpr uint8_t kCrossableKinds[] = {0x20, 0x21, 0x22, 0x12, 0x13, 0x1A, 0x24, 0x25};
constexpr bool crossKindsAreSafe() {
  for (uint8_t k : kCrossableKinds)
    if (k <= 0x04 || k == 0x10 || k == 0x11 || (k >= 0x30 && k <= 0x3F) || k == 0x7F) return false;
  return true;
}
static_assert(crossKindsAreSafe(), "a cross never copies chemistry, stimulus, instinct or Life genes");

// One pending friend egg, held on petfs as `friend.egg`: this record, then
// the friend's genome bytes. A new file, so no save format changes; an older
// firmware never opens it. One at a time: a newer hug replaces the waiting
// egg ("a new friend egg is waiting"), so a kid who duels everyone gets one
// friend egg in each clutch, from their latest hug.
struct FriendEggRecord {
  uint64_t friendLineage = 0;
  SpeciesId friendSpecies{};
  uint32_t battleSeed = 0;      // the cross's seed: replayable from the duel book
  uint32_t friendGenomeHash = 0;
  uint16_t friendGenomeBytes = 0;
};

// The receiving boundary. Parses the friend's bytes as a Genome
// (Genome::parse: structural damage refused), checks the hash the State
// packet announced, and refuses a genome with no gene sharing a uid with
// mine (a different engine's creature). nullopt: no egg, and the friends
// screen shows the egg fizzling into sparkles.
std::optional<Genome> acceptFriendGenome(const uint8_t* bytes, size_t len, uint32_t announcedHash,
                                         const Genome& mine);

// Pure. Only MutPoint ops, only at loci both genomes share with equal type
// and length, only for kCrossableKinds (never the mutation-policy gene, so a
// friend cannot make my line mutate faster).
MutationDiff crossDiff(const Genome& mine, const Genome& friends, Rng&);

// At layClutch: if a friend egg waits, it joins the clutch WITHIN kMaxClutch
// (3, ids.h): a clutch of 1 or 2 grows by one; a full clutch of 3 gives its
// last slot to the friend egg. The saved clutch count stays 1..3, so neither
// the keepsake nor lineage.log's three seeds change shape.
//
// The egg is checked twice, because mutate()'s last fallback hands back its
// input unchecked (mutate.cpp): crossed = apply(parent, crossDiff) must pass
// viability(), then child = mutate(crossed) must pass it too; either failing
// gives the parent's plain egg in that slot ("the friend magic fizzled"). The
// clutch cursor starts on his own first egg and the 30-minute timeout picks
// that one, never the friend egg: a friend egg is chosen by a kid or not at
// all. Picking it records an ordinary Birth whose diff carries the cross.
//
// The keepsake change (additive, called out in BATTLES.md): the clutch record
// inside Chunk::Occupant (Kind::Clutch) gains 5 trailing bytes, the friend
// egg's slot (0 = none, else 1..3) and its cross seed (u32). The decoder stops
// after the heirlooms and never checks for leftovers (keepsake.cpp), so an
// older reader ignores them and kFormatVersion stays 1. A committed fixture
// proves it.
struct FriendEggStore {
  static constexpr const char* kFile = "friend.egg";
  static std::optional<FriendEggRecord> load(Storage&, Genome* friendOut);
  static bool save(Storage&, const FriendEggRecord&, const Genome& friends);   // writeAtomic; replaces
  // At the Birth (any egg picked), not when the clutch is laid: the clutch keeps
  // seeds and rebuilds its eggs, so the friend's genome must outlive a reboot
  // during the 30-minute pick. Renamed to friend.used, never deleted.
  static bool consume(Storage&);
};

}  // namespace blorb
