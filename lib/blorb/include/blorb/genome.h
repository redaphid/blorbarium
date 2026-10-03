#pragma once
// The genome: a byte string of typed, self-delimiting, fixed-length genes.
// It is the only description of the creature.
//
// Invariants
//  * Every body byte may take any 8-bit value: decoders map bytes onto ranges,
//    ids are bytes, and an id no registry names is inert.
//  * A genome is immutable for one life. It changes only at egg-laying
//    (mutate.h) or by an owner EDIT of an OwnerEditable gene (protocol.h).
//  * Every gene carries a uid that survives across generations, so the
//    lineage names "palette #12" across the whole family tree and a mutation
//    op that names a missing uid fails loudly instead of hitting the wrong gene.
//  * A reader keeps genes whose type it does not know (len is in the header).
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include "blorb/ids.h"

namespace blorb {

// [type u8][len u8][flags u8][stage u8][featGate u8][mutWeight u8][uid u16 LE][body ...]
constexpr size_t kGeneHeaderLen = 8;
constexpr size_t kMaxGenomeBytes = 8 * 1024;   // mutate() refuses duplications past it

struct GeneFlags {
  static constexpr uint8_t Mutable = 1 << 0;        // body bytes may point-mutate
  static constexpr uint8_t Dupable = 1 << 1;
  static constexpr uint8_t Delable = 1 << 2;
  static constexpr uint8_t Dormant = 1 << 3;        // carried and inherited, never expressed; may wake
  static constexpr uint8_t OwnerEditable = 1 << 4;  // the phone's EDIT may rewrite the body
  static constexpr uint8_t Heirloom = 1 << 5;       // distilled from a parent's learning ("learned by gen 4")
};

struct GeneHeader {
  uint8_t type;        // row in GENE_TYPES; unknown types are carried, not expressed
  uint8_t len;         // body bytes
  uint8_t flags;       // GeneFlags
  Stage stage;         // the life stage at which it is expressed
  uint8_t featGate;    // 0 = always; n = expressed only once the lineage has earned feat bit n-1
  uint8_t mutWeight;   // 0 = never point-mutates, 255 = hot spot
  GeneUid uid;
};

// A borrowed view of one gene. Never outlives its genome.
struct GeneView {
  GeneHeader header;
  const uint8_t* body;   // header.len bytes
  uint16_t index;        // position now; changes across generations, uid does not
};

class Genome {
 public:
  // Boundary parse (flash, phone, lineage replay). Rejects only structural
  // damage: a length chain that overruns, a duplicate uid, the size cap.
  static std::optional<Genome> parse(const uint8_t* bytes, size_t len);

  const std::vector<uint8_t>& bytes() const { return bytes_; }
  size_t geneCount() const { return offsets_.size(); }
  GeneView gene(uint16_t index) const;
  std::optional<GeneView> find(GeneUid) const;
  GeneUid nextUid() const;                      // max uid + 1
  template <class F> void forEach(F&& f) const {  // f(const GeneView&), genome order
    for (uint16_t i = 0; i < geneCount(); ++i) f(gene(i));
  }
  uint32_t hash() const;                        // fnv1a over bytes; lineage records carry it

 private:
  friend class GenomeBuilder;
  std::vector<uint8_t> bytes_;
  std::vector<uint16_t> offsets_;               // built at parse; gene i starts at offsets_[i]
};

// The only writer. The starter genome, mutate(), apply() and the EDIT verb all
// go through it, so the length chain and uid uniqueness hold by construction.
class GenomeBuilder {
 public:
  static GenomeBuilder from(const Genome&);
  GenomeBuilder& append(GeneHeader, const uint8_t* body);
  template <class Body> GenomeBuilder& append(const Body& body, uint8_t flags, Stage stage = Stage::Baby,
                                               uint8_t featGate = 0, uint8_t mutWeight = 64);
  GenomeBuilder& insertAfter(GeneUid after, GeneHeader, const uint8_t* body);  // linkage by adjacency
  GenomeBuilder& erase(GeneUid);
  GenomeBuilder& setByte(GeneUid, uint8_t bodyOffset, uint8_t value);
  GenomeBuilder& setFlags(GeneUid, uint8_t flags);
  std::optional<Genome> build() const;          // nullopt only past kMaxGenomeBytes
 private:
  std::vector<uint8_t> bytes_;
};

// Expression filter: a pure function of the header and the lineage's situation.
// A gate past bit 31 names no feat, so it never opens.
constexpr bool expressedAt(const GeneHeader& h, Stage stage, uint32_t legacyFeats) {
  return !(h.flags & GeneFlags::Dormant) && h.stage == stage &&
         (h.featGate == 0 || (h.featGate <= 32 && ((legacyFeats >> (h.featGate - 1)) & 1u) != 0));
}

// The starter genome every new lineage begins from: grungo's temperament
// (skittish, curious, nocturnal, calm), authored with GenomeBuilder in
// src/starter_genome.cpp. It deliberately carries dormant genes (a bluer
// cloak, a "loves being shaken" stimulus gene, a brighter seer glow) and
// feat-gated ones, so awakenings and unlocks surprise from generation 1.
Genome starterGenome(uint32_t speciesSeed);

}  // namespace blorb
