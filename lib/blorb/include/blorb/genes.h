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
#include "blorb/species.h"

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
// pantrySize and refill are read through food.h's scales (x2 slots, 40 s a unit).
struct MetabolismGene  { uint8_t pantrySize, refill, rotMinutes, biteSize; };
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

// Appends a known kind at a uid the caller fixes. Crossing matches genes by
// uid, so a starter gene must sit at the same uid on every device whatever
// the build; GenomeBuilder::append's "largest + 1" would shift with it.
template <class Body>
GenomeBuilder& appendAt(GenomeBuilder& b, GeneUid uid, const Body& body, uint8_t flags, Stage stage = Stage::Baby,
                        uint8_t featGate = 0, uint8_t mutWeight = 64) {
  GeneHeader h{GeneKindOf<Body>::value, uint8_t(sizeof(Body)), flags, stage, featGate, mutWeight, uid};
  return b.append(h, reinterpret_cast<const uint8_t*>(&body));
}

// A knack ranks the three stances 2 (best) to 0, as one of six orders over
// (Pounce, Guard, Trick). Ties go to the earlier stance, so every three bytes
// rank to exactly one order.
inline constexpr uint8_t kKnackOrders[6][kCycle] = {{2, 1, 0}, {2, 0, 1}, {1, 2, 0}, {0, 2, 1}, {1, 0, 2}, {0, 1, 2}};
constexpr uint8_t knackOrderOf(const KnackGene& k) {
  const uint8_t v[kCycle] = {k.pounce, k.guard, k.trick};
  uint8_t rank[kCycle] = {};
  for (uint8_t s = 0; s < kCycle; ++s)
    for (uint8_t t = 0; t < kCycle; ++t)
      if (t != s && (v[t] < v[s] || (v[t] == v[s] && t > s))) ++rank[s];
  for (uint8_t i = 0; i < 6; ++i)
    if (kKnackOrders[i][0] == rank[0] && kKnackOrders[i][1] == rank[1] && kKnackOrders[i][2] == rank[2]) return i;
  return 0;
}
// A species' own knack: its style first, then the stance its style beats.
// The starter carries it as a gene, and a genome without one ranks the same.
constexpr KnackGene knackFor(Stance style) {
  uint8_t v[kCycle] = {};
  v[uint8_t(style)] = 192;
  v[(uint8_t(style) + 2) % kCycle] = 128;
  v[(uint8_t(style) + 1) % kCycle] = 64;
  return KnackGene{v[0], v[1], v[2]};
}
static_assert(kKnackOrders[knackOrderOf(knackFor(Stance::Pounce))][0] == 2 &&
                  kKnackOrders[knackOrderOf(knackFor(Stance::Guard))][1] == 2 &&
                  kKnackOrders[knackOrderOf(knackFor(Stance::Trick))][2] == 2,
              "a species' knack ranks its style first");
static_assert(knackOrderOf(KnackGene{9, 9, 9}) == 0, "a three-way tie ranks in stance order");

// Grungo's recipe with one species row's bytes written in (src/starter_genome.cpp).
// Line n of the recipe owns uid n whether or not this build writes it, so a
// block left out (the starvation genes, with neglectCanKill false) leaves its
// uids empty rather than shifting the rest. With grungo's row and Recipe::Gift
// it writes exactly the genome starterGenome() always has.
inline constexpr bool kNeglectCanKill = true;   // decided by the user: sustained neglect can end the life
// Which numbers the recipe writes. Gift is the recipe the gift device's grungo
// was founded from, kept byte for byte by starterGenome(). FirstHours is what a
// fresh founding gets (starterGenomeOf): a 15-minute egg, a hatchling peckish
// within his first hours, a happy face when content, and sleepiness as pressure
// that brings day naps (docs/notes/first-hours.md). Same genes, same uids.
enum class Recipe : uint8_t { Gift, FirstHours };
void authorStarter(GenomeBuilder&, const SpeciesStarter&, uint32_t speciesSeed, bool neglectCanKill = kNeglectCanKill,
                   Recipe = Recipe::FirstHours);

// O(1) by type byte. nullptr for an unknown type: carried and skipped.
const GeneTypeInfo* geneType(uint8_t type);

// Express every gene that switches on at `stage`. Pure over the genome;
// writes only the phenotype. Idempotent: the phenotype records which stages
// it has expressed, so a crash-and-replay never doubles a reaction.
void expressStage(const Genome&, Stage, uint32_t legacyFeats, Phenotype&);

}  // namespace blorb
