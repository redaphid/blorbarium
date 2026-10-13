// The nine starters (SPECIES.md, "What differs per species"): grungo's recipe
// with the row's bytes written in, plus the genes that say which kind he is.
//
// Gene order is linkage: a friend egg's crossover takes runs of genes from one
// parent, so the SpeciesGene goes first and the PowerGene last, as far apart
// as the genome allows, and about half of friend eggs wear one parent's body
// with the other's power. Uids are fixed (species.h), never "largest + 1", so
// they match on every device whatever the build.
#include "blorb/genes.h"
#include "blorb/registry.h"

namespace blorb {
namespace {

constexpr uint8_t kFixed = 0;   // never point-mutated, duplicated, deleted or slept (mutate.cpp)

constexpr uint8_t gateOf(uint32_t feat) {
  uint8_t bit = 0;
  while (bit < 32 && !((feat >> bit) & 1u)) ++bit;
  return uint8_t(bit + 1);
}

// War paint: a line that has finished a friend's pet wears it from the next
// generation on. It waits on the lineage's slayer feat rather than the
// Dormant flag, because a mutation may wake a dormant gene and a mark nobody
// earned would be a lie.
constexpr GeneUid kWarPaintUid{0x0F42};
constexpr uint8_t kWarPaintLayer = 2;   // mark layers 0 and 1 are grungo's mottling and stalks

}  // namespace

Genome starterGenomeOf(SpeciesId species, uint32_t speciesSeed) {
  const SpeciesInfo& row = speciesInfo(species);
  GenomeBuilder b;
  appendAt(b, kSpeciesGeneUid, SpeciesGene{row.id.v, 0}, kFixed, Stage::Baby, 0, 0);
  authorStarter(b, row.starter, speciesSeed);
  appendAt(b, kKnackGeneUid, knackFor(row.style), GeneFlags::Mutable);
  appendAt(b, kWarPaintUid, MarkGene{kWarPaintLayer, 0, 128, 0}, GeneFlags::Mutable, Stage::Baby, gateOf(feat::slayer));
  appendAt(b, kPowerGeneUid, PowerGene{row.power.v, 0}, kFixed, Stage::Baby, 0, 0);
  return *b.build();
}

}  // namespace blorb
