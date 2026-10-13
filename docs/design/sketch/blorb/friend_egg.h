#pragma once
// SKETCH (docs/design/SPECIES.md, "Crossing"). The friend egg: after a duel,
// if BOTH kids hug their own pets (a 3 s cradle on the friends screen, each on
// their own device), the two devices swap genomes and each pet's next clutch
// gains one extra, speckled egg: a cross of the two. Graft from candidate B,
// re-expressed so it needs no new lineage op.
//
// Crossing is allele exchange at shared loci. Every starter genome is built
// in one fixed order from one recipe (starterGenomeOf), so all nine species
// share the starter's uids. crossDiff walks MY genome by uid in linkage
// blocks (the source parent switches about once every 8 genes, so neighbours
// travel together) and, where the friend's gene has the same uid, the same
// type and the same length, copies the friend's body bytes over mine. Each
// copied byte is a MutPoint (the op that exists today), so the cross is an
// ordinary MutationDiff:
//
//   apply(mine, crossDiff + mutateDiff) == child      (no friend genome needed)
//
// and lineage.log's Birth entry, kMutateVersion and the replay are unchanged.
// Genes only the friend has (its own duplications, newer gene kinds) do not
// cross: that is the price of no format change, and it is also why a cross
// cannot smuggle an unknown kind in. SpeciesGene and PowerGene sit far apart
// in the starter's order, so about half of friend eggs are hybrids (a
// grungo body with a zap): the clutch preview shows which.
#include <cstdint>
#include <optional>
#include "blorb/fixed.h"
#include "blorb/genome.h"
#include "blorb/mutate.h"
#include "blorb/seams.h"
#include "blorb/species.h"

namespace blorb {

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
// and length, never on a gene of class Life except PowerGene, never touching
// the mutation-policy gene (a friend cannot make my line mutate faster).
MutationDiff crossDiff(const Genome& mine, const Genome& friends, Rng&);

// At layClutch: if a friend egg waits, the clutch holds one more egg, built
// as mutate(apply(parent, crossDiff)), with the ordinary Look/Mind guarantee,
// viability() and the fallback. A cross that fails viability eight times
// falls back to the parent's plain egg ("the friend magic fizzled"). The
// clutch cursor starts on his own first egg and the 30-minute timeout picks
// that one, never the friend egg: a friend egg is chosen by a kid or not at
// all. Picking it records an ordinary Birth whose diff carries the cross.
//
// The keepsake change (additive, called out in BATTLES.md): the Clutch chunk
// gains a trailing byte, "egg n is the friend egg" (0 = none), and the egg's
// cross seed. A reader that predates it ignores trailing bytes, as the Body
// chunk's mouth byte already relies on (DEVIATIONS 8), so kFormatVersion stays 1.
struct FriendEggStore {
  static constexpr const char* kFile = "friend.egg";
  static std::optional<FriendEggRecord> load(Storage&, Genome* friendOut);
  static bool save(Storage&, const FriendEggRecord&, const Genome& friends);   // writeAtomic; replaces
  static bool consume(Storage&);   // after the clutch is laid with it: renamed to friend.used, never deleted
};

}  // namespace blorb
