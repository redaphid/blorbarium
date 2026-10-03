#pragma once
// v2 FINAL SKETCH. Gene kinds. Layouts are frozen once shipped (a changed
// layout is a new type byte), every byte is legal, and each kind gets the
// usual express_/describe_/rules_ trio. Type bytes are assigned after the
// depth branch (MemoryGene) and the thoughts branch (OracleGene) settle 0x14
// and 0x15 between them; both claim 0x14 today.
//
//   BLORB_GENE(0x14|0x15, oracle,  OracleGene,  Mind)   // thoughts branch, widened before it lands
//   BLORB_GENE(0x16, seer,         SeerGene,    Mind)
//   BLORB_GENE(0x17, lore,         LoreGene,    Mind)   // culture: omens, routines, knacks, rhythms
//   BLORB_GENE(0x18, talent,       TalentGene,  Mind)
//   BLORB_GENE(0x19, hunt,         HuntGene,    Body)
//   BLORB_GENE(0x23, part,         PartGene,    Look)   // diploid: a part can skip a generation
//   BLORB_GENE(0x33, pace,         PaceGene,    Life)
#include <cstdint>
#include "blorb/v2_registry.h"

namespace blorb {

// The thoughts branch's gene, kept as the ONE owner of what he says and how
// often: the 8-ball's chance, his voice (a voices.def row, hybrids included)
// and his topic weights. Widened from 7 to 12 topic bytes before it lands, so
// the dialogue topics and `play` fit without a second gene type.
struct OracleGene { uint8_t chance, voice, topics[kOracleTopicBytes]; };

// How he predicts. Nothing here is about speech.
struct SeerGene {
  uint8_t beatTicks;     // ticks between looks ahead (starter 5: every 0.5 s)
  uint8_t horizonBeats;  // mod 16, +1 (starter 12: 6 s)
  uint8_t learnRate;     // NLMS step
  uint8_t dawnForget;    // share of every expectation lost at pet dawn, proportional
  uint8_t forethought;   // weight of predicted omens when he chooses an action (begging, superstition)
  uint8_t curiosity;     // 128 = none; above seeks uncertain actions, below avoids them
  uint8_t wishfulness;   // 0 sees what is likely .. 255 sees what he wants
  uint8_t bar;           // confidence he needs to name an omen; under it a shake improvises
  uint8_t routineRate;   // day-map EMA step per pet day
};

// Culture (axis A8). One gene per inherited belief, laid at death, seeded at
// hatch, confirmed or faded by each later life. `heard` counts the
// generations that confirmed it; `gen` is the generation that learned it.
//   Omen    a = cue index, b = omen,      c = unused     "the bell brings pellets"
//   Routine a = day bin,   b = topic,     c = unused     "breakfast is at the bell hour"
//   Knack   a = game,      b = context,   c = move       "guard the downhill side"
//   Rhythm  a = tempo,     b = onset bits, c = length    the family drum rhythm
// A kind past Rhythm is carried and inert.
enum class LoreKind : uint8_t { Omen, Routine, Knack, Rhythm };
struct LoreGene { uint8_t kind, a, b, c, strength, genLo, genHi, heard; };

struct TalentGene { uint8_t tongueReach, tempo, copyFidelity; };
struct HuntGene { uint8_t reach, tongueSpeed, leadPrior, patience; };

// A visible part (warts, a hood mushroom, a tail stub, a scarf): a mark layer
// with two alleles. Bit 7 of an allele is dominance, bits 0..6 the variant
// (taken modulo the pack's count for the layer). A dominant allele shows;
// with neither dominant, `a` shows. At the clutch each egg draws one allele
// from each copy (selfing), recorded as a Segregate op in the birth diff, so
// a recessive part skips a generation and can come back (story 4).
struct PartGene { uint8_t layer, a, b, tint; };

// Life-history pace, pleiotropic: one byte moves many expressed values, so
// long life, fast learning and a big clutch cannot all be had. 128 = starter.
struct PaceGene { uint8_t pace; };

}  // namespace blorb
