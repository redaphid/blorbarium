#pragma once
// Gene bodies and the gene type registry (expanded from defs/gene_kinds.def).
//
// A body is a struct of bytes. Its decoder (in express_<name>) turns bytes
// into engine values, so every byte is legal. express_<name> adds the gene's
// effect to a Phenotype. describe_<name> renders it for the phone and for
// lineage diffs. rules_<name> says, per body byte, how mutate() may move it.
#include <cstdint>
#include "blorb/fixed.h"
#include "blorb/genome.h"
#include "blorb/registry.h"

namespace blorb {

struct Phenotype;   // creature.h
struct Describe;    // protocol.h: a text sink with field(name, value)

enum class MutRule : uint8_t { Any, Nudge, Flag, Fixed };   // ids jump anywhere; rates only nudge
enum class GeneClass : uint8_t { Body, Mind, Look, Life };   // mutate() guarantees a Look and a Mind change per egg

// A tint relative to the art's authored colour, so shading ramps survive.
// The pack clamps it to the region's band.
struct Tint { int8_t hue = 0; uint8_t sat = 128, val = 128; };   // hue in 1/256 turn; 128 = x1

// ---- bodies (every field uint8_t) ----------------------------------------------
struct ChemGene        { uint8_t chem, halfLife, initial; };                         // decay + starting level
struct ReactionGene    { uint8_t a, qa, b, qb, c, qc, d, qd, rate; };                // qa*A + qb*B -> qc*C + qd*D
struct EmitterGene     { uint8_t locus, chem, threshold, gain, flags, period; };      // flags: digital|invert|clear; every 2^period ticks
struct ReceptorGene    { uint8_t chem, locus, threshold, nominal, gain, flags; };     // flags: digital|invert
struct StimulusGene    { uint8_t stim, flags, chem[3], amount[3]; };                  // flags: whenAsleep|wakes; amount 128 = 0
struct InstinctGene    { uint8_t cue[3], action, drive, level, strength; };          // cue 255 = unused
struct ExpressionGene  { uint8_t face, weight, drive[3], amount[3]; };               // drive mix -> face
struct TemperamentGene { uint8_t learnRate, forgetRate, explore, habituation, dreamEvery, dreamLen, traceHalfLife; };
struct PaletteGene     { uint8_t region, hue, sat, val, chemBound, gain; };          // region mod kRegionCount
struct SizeGene        { uint8_t base, growth, squash; };
struct MarkGene        { uint8_t layer, variant, tint, chemBound; };                 // overlay variant mod the pack's count
struct MutationPolicyGene { uint8_t point, dup, del, wake, sleep, wild, heirloomMax, heirloomConf; };
struct EggGene         { uint8_t incubateMinutes, warmthBoost, hatchBurstDreams; };
struct MetabolismGene  { uint8_t pantrySize, refillHours, rotMinutes, biteSize; };
struct NoteGene        { uint8_t text[24]; };                                         // family motto; owner-editable

// ---- registry ------------------------------------------------------------------------
struct GeneTypeInfo {
  uint8_t type;
  const char* name;
  uint8_t bodyLen;
  const MutRule* rules;                                 // bodyLen entries
  void (*express)(const GeneView&, Phenotype&);         // once, when the gene switches on
  void (*describe)(const GeneView&, Describe&);
  GeneClass cls;
};

#define BLORB_GENE(type, name, Body, cls)                           \
  static_assert(sizeof(Body) <= 255, "gene body exceeds len byte"); \
  void express_##name(const GeneView&, Phenotype&);                 \
  void describe_##name(const GeneView&, Describe&);                 \
  extern const MutRule rules_##name[sizeof(Body)];
#include "blorb/defs/gene_kinds.def"
#undef BLORB_GENE

inline constexpr GeneTypeInfo GENE_TYPES[] = {
#define BLORB_GENE(type, name, Body, cls) \
  {type, #name, uint8_t(sizeof(Body)), rules_##name, &express_##name, &describe_##name, GeneClass::cls},
#include "blorb/defs/gene_kinds.def"
#undef BLORB_GENE
};

// Body -> type byte, from the same rows, so append<Body> cannot mislabel a gene.
template <class Body> struct GeneKindOf;
#define BLORB_GENE(type, name, Body, cls) \
  template <> struct GeneKindOf<Body> { static constexpr uint8_t value = type; };
#include "blorb/defs/gene_kinds.def"
#undef BLORB_GENE

// Appends a known kind with the next free uid (largest so far + 1).
template <class Body>
GenomeBuilder& GenomeBuilder::append(const Body& body, uint8_t flags, Stage stage, uint8_t featGate,
                                     uint8_t mutWeight) {
  uint16_t top = 0;
  for (size_t at = 0; at + kGeneHeaderLen <= bytes_.size(); at += kGeneHeaderLen + bytes_[at + 1]) {
    uint16_t uid = uint16_t(bytes_[at + 6] | (bytes_[at + 7] << 8));
    top = uid > top ? uid : top;
  }
  GeneHeader h{GeneKindOf<Body>::value, uint8_t(sizeof(Body)), flags, stage, featGate, mutWeight,
               GeneUid{uint16_t(top + 1)}};
  return append(h, reinterpret_cast<const uint8_t*>(&body));
}

// O(1) by type byte. nullptr for an unknown type: carried and skipped.
const GeneTypeInfo* geneType(uint8_t type);

// Express every gene that switches on at `stage`. Pure over the genome;
// writes only the phenotype. Idempotent: the phenotype records which stages
// it has expressed, so a crash-and-replay never doubles a reaction.
void expressStage(const Genome&, Stage, uint32_t legacyFeats, Phenotype&);

}  // namespace blorb
