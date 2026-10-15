#pragma once
// Species as data (docs/design/SPECIES.md): a species.def row says
// what a kind of blorb is, and two genes say which kind THIS pet is and which
// power it carries. The genome stays the only description of the creature
// (genome.h), so a species is heritable, travels in an egg, and crosses in a
// friend egg (friend_egg.h).
//
// Nothing in the engine branches on a species id. A species changes:
//   art      paint::packFor(species) picks the sprite pack (every device holds all nine)
//   starter  starterGenomeOf(species, seed): grungo's recipe with the row's bytes
//   duel     its element, its favourite stance, and (by default) its power
//   voice    the oracle voice its starter genome carries
#include <cstddef>
#include <cstdint>
#include "blorb/fixed.h"
#include "blorb/genome.h"
#include "blorb/ids.h"
#include "blorb/registry.h"

namespace blorb {

using SpeciesId = Id<struct SpeciesTag>;   // any byte is legal: an unnamed one reads as row 0
using PowerId = Id<struct PowerTag>;

// The two rock-paper-scissors cycles, one rule for both: a beats b when b is
// the one after a, going round backwards. Pounce > Trick > Guard > Pounce, and
// Splash > Spark > Sprout > Splash.
enum class Element : uint8_t { Splash = 0, Sprout = 1, Spark = 2 };
enum class Stance : uint8_t { Pounce = 0, Guard = 1, Trick = 2 };
inline constexpr uint8_t kCycle = 3;
template <class E> constexpr bool beats(E a, E b) { return uint8_t(b) == (uint8_t(a) + 2) % kCycle; }
static_assert(beats(Stance::Pounce, Stance::Trick) && beats(Stance::Trick, Stance::Guard) &&
                  beats(Stance::Guard, Stance::Pounce),
              "pounce beats trick beats guard beats pounce");
static_assert(beats(Element::Splash, Element::Spark) && beats(Element::Spark, Element::Sprout) &&
                  beats(Element::Sprout, Element::Splash),
              "splash douses spark, spark singes sprout, sprout drinks splash");
constexpr Stance counterOf(Stance s) { return Stance((uint8_t(s) + 1) % kCycle); }   // the stance that beats s
static_assert(beats(counterOf(Stance::Trick), Stance::Trick), "counterOf names the winner");

// The phases of one turn a power handler is called in (duel.h has the order).
// OnDamage only adds hits and Block only takes them away, so two handlers in
// one phase never read what the other just wrote (zap against shell).
enum class PowerPhase : uint8_t { BeforeClash, OnDamage, Block, AfterDamage };

// The glyph the duel scene draws when a power fires; the art may restyle it.
enum class PowerGlyph : uint8_t { eye, shell, spore, leaf, lamp, wave, spark, feather, gear };

namespace power {
#define BLORB_POWER(id, name, cost, glyph) inline constexpr PowerId name{id};
#include "blorb/defs/powers.def"
#undef BLORB_POWER
}  // namespace power

struct PowerInfo { PowerId id; const char* name; uint8_t cost; PowerGlyph glyph; };
inline constexpr PowerInfo POWERS[] = {
#define BLORB_POWER(id, name, cost, glyph) {PowerId{id}, #name, cost, PowerGlyph::glyph},
#include "blorb/defs/powers.def"
#undef BLORB_POWER
};

// The species-specific bytes of a starter genome. Everything else is
// grungo's recipe (src/starter_genome.cpp), so nine starters are one function
// and nine rows, not nine authored genomes. Colours are not here: each
// species' pack is drawn in its own authored palette, and palette genes tint
// relative to it exactly as they tint grungo today.
struct SpeciesStarter {
  uint8_t bold;           // adrenaline -> startle receptor gain
  bool nocturnal;         // flips the sleep instinct's day cue
  uint16_t appetite;      // hunger tonic, permille of grungo's
  VoiceId voice;
};

struct SpeciesInfo {
  SpeciesId id;
  const char* name;       // roster.json's slug; the phone shows the roster's display name
  Element element;
  Stance style;           // the stance it loves: its lean, and its "in the zone" sparkle
  PowerId power;          // the power a pure-bred pet of this species carries
  SpeciesStarter starter;
};

inline constexpr SpeciesInfo SPECIES[] = {
#define BLORB_SPECIES(id, name, el, st, pw, bold, noct, app, vc) \
  {SpeciesId{id}, #name, Element::el, Stance::st, power::pw, {bold, noct != 0, app, voice::vc}},
#include "blorb/defs/species.def"
#undef BLORB_SPECIES
};
namespace species {
#define BLORB_SPECIES(id, name, el, st, pw, bold, noct, app, vc) inline constexpr SpeciesId name{id};
#include "blorb/defs/species.def"
#undef BLORB_SPECIES
}  // namespace species
inline constexpr size_t kSpeciesCount = countOf(SPECIES);
inline constexpr size_t kPowerCount = countOf(POWERS);
inline constexpr SpeciesId kGrungo{0};
inline constexpr uint8_t kMeterMax = 6;   // the spark meter's pips

constexpr const SpeciesInfo& speciesInfo(SpeciesId s) { return s.v < kSpeciesCount ? SPECIES[s.v] : SPECIES[0]; }
constexpr const PowerInfo& powerInfo(PowerId p) { return p.v < kPowerCount ? POWERS[p.v] : POWERS[0]; }

// ---- the wheel's balance, checked at compile time -------------------------------
constexpr bool rowsAreTheirIndex() {
  for (size_t i = 0; i < kSpeciesCount; ++i) if (SPECIES[i].id.v != i) return false;
  for (size_t i = 0; i < kPowerCount; ++i) if (POWERS[i].id.v != i) return false;
  return true;
}
constexpr bool wheelIsLatinSquare() {
  if (kSpeciesCount != kCycle * kCycle) return false;
  for (uint8_t e = 0; e < kCycle; ++e)
    for (uint8_t s = 0; s < kCycle; ++s) {
      int n = 0;
      for (const SpeciesInfo& r : SPECIES) n += (uint8_t(r.element) == e && uint8_t(r.style) == s) ? 1 : 0;
      if (n != 1) return false;
    }
  return true;
}
constexpr bool powersAreUnique() {
  for (size_t i = 0; i < kSpeciesCount; ++i)
    for (size_t j = i + 1; j < kSpeciesCount; ++j)
      if (SPECIES[i].power == SPECIES[j].power) return false;
  return true;
}
constexpr bool powersAreAffordable() {
  for (const PowerInfo& p : POWERS)
    if (p.cost == 0 || p.cost > kMeterMax) return false;
  return true;
}
static_assert(rowsAreTheirIndex(), "species.def / powers.def ids must be 0..n-1 in order");
static_assert(wheelIsLatinSquare(), "species.def: every (element, style) pair exactly once, nine rows");
static_assert(powersAreUnique(), "species.def: a shared power");
static_assert(powersAreAffordable(), "powers.def: a cost the meter cannot hold");
static_assert(SPECIES[0].power == power::foresee, "slot 0 is grungo, and grungo foresees");

// Two firmwares duel only if they agree on these tables: the beacon carries
// this hash, and a mismatch greys the friend out ("they speak another frog").
constexpr uint32_t fnvStep(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }
constexpr uint32_t fnvStr(uint32_t h, const char* s) { while (*s) h = fnvStep(h, uint8_t(*s++)); return h; }
constexpr uint16_t rosterHash() {
  uint32_t h = 2166136261u;
  for (const SpeciesInfo& r : SPECIES) {
    h = fnvStep(h, r.id.v); h = fnvStep(h, uint8_t(r.element)); h = fnvStep(h, uint8_t(r.style)); h = fnvStep(h, r.power.v);
  }
  for (const PowerInfo& p : POWERS) { h = fnvStr(h, p.name); h = fnvStep(h, p.cost); }
  return uint16_t(h ^ (h >> 16));
}

// ---- genes ------------------------------------------------------------------------
// New gene kinds (rows for defs/gene_kinds.def; type bytes clear of v2's
// 0x14..0x19, 0x23 and 0x33):
//   BLORB_GENE(0x24, species, SpeciesGene, Look)
//   BLORB_GENE(0x25, power,   PowerGene,   Life)
//   BLORB_GENE(0x1A, knack,   KnackGene,   Mind)
//
// SpeciesGene and PowerGene are separate genes at separate uids, far apart in
// the starter's order, so a friend egg's crossover (friend_egg.h) can take
// the body from one parent and the power from the other: a grungo who zaps.
// Both bytes are MutRule::Fixed and neither gene is Dupable or Delable, so
// point mutation never changes a species or a power. Only a cross does, and
// the lineage diff says so.
//
// Expression: the first expressed SpeciesGene is the body (none: grungo);
// the first expressed PowerGene is the power (none: the body's row's power).
// So every genome alive today reads as a pure-bred grungo with foresee.
struct SpeciesGene { uint8_t species, reserved; };
struct PowerGene { uint8_t power, reserved; };

// KnackGene: which stance this line loves, nudged by mutation and ranked at
// expression into a permutation of {2, 1, 0}. It shapes STYLE, never strength:
// the pet leans toward its best stance, picks it more on autopilot, and
// sparkles when it wins with it. A bred line of champions is a line with a
// recognisable habit, not a stronger one (any winner reward snowballed in the
// probe). No KnackGene: the rank comes from the species' style.
struct KnackGene { uint8_t pounce, guard, trick; };

// The duel's view of a phenotype (Phenotype gains one of these; creature.h).
struct SpeciesTraits {
  SpeciesId body = kGrungo;
  PowerId power = power::foresee;
  uint8_t knackOrder = 0;   // 0..5, Knack::kOrders
  constexpr bool hybrid() const { return speciesInfo(body).power != power; }
};

// ---- starters ---------------------------------------------------------------------
// Grungo's recipe with the row's bytes, plus a SpeciesGene, a PowerGene and a
// KnackGene ranked toward the row's style, plus the duel feeling genes
// (stakes.h names the stimuli). starterGenome(seed) stays grungo's base
// recipe, and starterGenomeOf(kGrungo, seed) differs from it only by the
// appended genes. A pet already alive keeps its genome.
//
// Crossing needs every recipe gene at the same uid on every device, whatever
// the build. So each recipe line owns a fixed uid: a block the build leaves
// out (kNeglectCanKill's four starvation genes, were it ever turned off) skips its
// uids rather than shifting the rest, and the appended genes take the fixed
// uids below, past any recipe uid. Unit D3's test crosses genomes built
// with and without that block.
inline constexpr GeneUid kSpeciesGeneUid{0x0F00}, kPowerGeneUid{0x0F40}, kKnackGeneUid{0x0F41};
Genome starterGenomeOf(SpeciesId, uint32_t speciesSeed);

}  // namespace blorb
