#pragma once
// Heredity. A dead creature's genome, plus what its brain learned, becomes an
// egg's genome and a diff the phone can show.
//
// Everything is a pure function of (genome, policy, heirlooms, rng), so a
// clutch stores seeds, not genomes, and a recorded diff replays to the
// identical child. Ops address genes by uid, so a diff names "palette #12"
// across generations and apply() refuses an op whose gene is missing.
//
// Guarantees, each a host test:
//  * no mutation is lethal: the child passes viability(), or it is the parent
//    unchanged with an empty diff (a phone EDIT can leave a non-viable parent);
//  * apart from that unchanged fallback, at least one Look-class change of at
//    least minVisibleDelta and at least one Mind-class change per egg, so every
//    egg looks and acts different;
//  * apply(parent, diff) == child, byte for byte;
//  * the same (parent, policy, heirlooms, seed) always gives the same child.
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>
#include "blorb/brain.h"
#include "blorb/fixed.h"
#include "blorb/genome.h"

namespace blorb {

struct MutPoint    { GeneUid gene; uint8_t offset; uint8_t from; uint8_t to; bool wild; };
struct MutDup      { GeneUid gene; GeneUid copy; };            // copy inserted right after gene
struct MutDel      { GeneUid gene; };
struct MutWake     { GeneUid gene; };                          // Dormant cleared: a latent trait appears
struct MutSleep    { GeneUid gene; };                          // Dormant set: carried, quiet
struct MutHeirloom { GeneUid after; std::vector<uint8_t> gene; };  // header + body, Heirloom flag set
using MutationOp = std::variant<MutPoint, MutDup, MutDel, MutWake, MutSleep, MutHeirloom>;

struct MutationDiff { std::vector<MutationOp> ops; };   // applied in order, parent -> child

// Rates come from the genome's MutationPolicyGene, so a lineage can drift
// toward wildness; the wild bonus comes from the lineage's feats.
struct MutationPolicy {
  Fx pointPerGene, dupPerGene, delPerGene, wakePerDormant, sleepPerGene;
  Fx wild;                    // chance a point mutation rerolls its field's whole range
  uint8_t heirloomMax;        // learned beliefs that may become instinct genes
  Fx heirloomMinConfidence;
  uint8_t minVisibleDelta;    // the forced Look change moves its byte at least this far
  uint8_t lookSlot = 0;       // which hue step the forced Look change takes; a clutch gives each egg its own
};
MutationPolicy policyOf(const Genome&, Fx wildBonus);

struct Offspring { Genome genome; MutationDiff diff; };

// Retries a non-viable roll from the same rng stream (still deterministic) up
// to 8 times, then falls back to the forced Look and Mind changes alone, and
// to the parent itself if even those fail viability().
Offspring mutate(const Genome& parent, const MutationPolicy&, const std::vector<Belief>& heirlooms, Rng&);

std::optional<Genome> apply(const Genome& parent, const MutationDiff&);

// Static shape checks (a life chemical is seeded, a food -> energy reaction
// exists, a Look gene is expressed, under the size cap), then a chemistry-only
// dry run: 48 pet-hours at a five-minute stride (576 coarse steps) with a
// pellet's worth of food every refill interval and no other input. Rejects a genome whose die locus
// fires or whose drives all pin at 1. This is the embryo trial's intent at
// about 5 KB of RAM and no brain: a lethal mutation never hatches.
struct Viability { bool ok; const char* reason; };
Viability viability(const Genome&);

}  // namespace blorb
