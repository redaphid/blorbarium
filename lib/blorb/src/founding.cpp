#include "blorb/founding.h"

#include <cstring>
#include <vector>
#include "blorb/creature.h"
#include "blorb/genes.h"
#include "blorb/starter_duel.h"

namespace blorb {
namespace {

// found.pin: magic "BFPN", version u8, species u8. Six bytes or it is no pin.
constexpr const char* kPinFile = "found.pin";
constexpr const char* kPinAside = "found.off";
constexpr char kMagic[4] = {'B', 'F', 'P', 'N'};
constexpr uint8_t kVersion = 1;
constexpr size_t kPinLen = sizeof kMagic + 2;

// Every name the keepsake and the lineage ever leave on the flash. A rescued
// slot alone still means a pet lived here.
constexpr const char* kPetFiles[] = {"snap.a", "snap.b", "rescue/snap.a.0", "rescue/snap.b.0", "lineage.log",
                                     "lineage.tmp"};

struct Badge { uint8_t mac[6]; SpeciesId species; };
constexpr Badge kBadges[] = {
#define BLORB_BADGE(n, m0, m1, m2, m3, m4, m5, sp) {{m0, m1, m2, m3, m4, m5}, species::sp},
#include "blorb/defs/badges.def"
#undef BLORB_BADGE
};

constexpr bool sameMac(const uint8_t (&a)[6], const uint8_t (&b)[6]) {
  for (int i = 0; i < 6; ++i) if (a[i] != b[i]) return false;
  return true;
}
constexpr bool oneOfEach() {
  if (countOf(kBadges) != kSpeciesCount) return false;
  for (size_t i = 0; i < countOf(kBadges); ++i) {
    if (kBadges[i].species.v >= kSpeciesCount) return false;
    for (size_t j = i + 1; j < countOf(kBadges); ++j)
      if (kBadges[i].species == kBadges[j].species || sameMac(kBadges[i].mac, kBadges[j].mac)) return false;
  }
  return true;
}
static_assert(oneOfEach(), "badges.def: every species exactly once, no MAC twice");

// The byte range a founder rolls body byte i of gene v across, if it rolls it.
struct Roll { uint8_t lo, hi; };
std::optional<Roll> rollOf(const GeneView& v, uint8_t i, const LookBands& bands) {
  if (v.header.flags & GeneFlags::Dormant) return std::nullopt;
  if (v.header.type == GeneKindOf<PaletteGene>::value) {
    const RegionBand& b = bands[v.body[0] % kRegionCount];
    if (i == 1) return Roll{uint8_t(128 + b.hueMin), uint8_t(128 + b.hueMax)};
    if (i == 2) return Roll{b.satMin, b.satMax};
    if (i == 3) return Roll{b.valMin, b.valMax};
  } else if (v.header.type == GeneKindOf<MarkGene>::value && v.body[0] == 0 && i == 1) {
    return Roll{2, 4};   // mottling density
  }
  return std::nullopt;
}

bool isDuelGene(GeneUid uid) {
  return uid.v >= kFirstDuelGeneUid.v && uid.v < kFirstDuelGeneUid.v + kDuelGeneCount;
}

}  // namespace

std::optional<SpeciesId> tableSpecies(const uint8_t mac[6]) {
  for (const Badge& b : kBadges)
    if (std::memcmp(b.mac, mac, sizeof b.mac) == 0) return b.species;
  return std::nullopt;
}

SpeciesId badgeSpecies(const uint8_t mac[6]) { return tableSpecies(mac).value_or(kGrungo); }

bool needsFounding(Storage& s) {
  for (const char* f : kPetFiles)
    if (s.size(f) > 0) return false;
  return true;
}

SpeciesId foundingSpecies(Storage& s, const uint8_t mac[6]) {
  if (std::optional<SpeciesId> pin = readFoundPin(s)) return *pin;
  return badgeSpecies(mac);
}

std::optional<SpeciesId> readFoundPin(Storage& s) {
  uint8_t b[kPinLen + 1];
  std::optional<size_t> n = s.read(kPinFile, 0, b, sizeof b);
  if (!n || *n != kPinLen || std::memcmp(b, kMagic, sizeof kMagic) != 0 || b[4] != kVersion) return std::nullopt;
  if (b[5] >= kSpeciesCount) return std::nullopt;
  return SpeciesId{b[5]};
}

bool writeFoundPin(Storage& s, SpeciesId species) {
  const uint8_t b[kPinLen] = {uint8_t(kMagic[0]), uint8_t(kMagic[1]), uint8_t(kMagic[2]), uint8_t(kMagic[3]),
                              kVersion, species.v};
  return s.writeAtomic(kPinFile, b, sizeof b);
}

bool clearFoundPin(Storage& s) {
  uint8_t probe;
  return !s.read(kPinFile, 0, &probe, 0) || s.rename(kPinFile, kPinAside);
}

Genome foundingGenome(SpeciesId species, uint32_t speciesSeed, const LookBands& bands) {
  const Genome starter = starterGenomeOf(species, speciesSeed);
  Rng rng = Rng::seeded(uint64_t(speciesSeed) << 32 | 0x666f756eu);
  GenomeBuilder b = GenomeBuilder::from(starter);
  starter.forEach([&](const GeneView& v) {
    for (uint8_t i = 0; i < v.header.len; ++i)
      if (std::optional<Roll> r = rollOf(v, i, bands))
        b.setByte(v.header.uid, i, r->hi <= r->lo ? r->lo : uint8_t(r->lo + rng.below(uint32_t(r->hi - r->lo) + 1u)));
  });
  return *b.build();
}

bool predatesSpecies(const Genome& g) {
  bool any = false;
  g.forEach([&](const GeneView& v) { any = any || v.header.type == GeneKindOf<SpeciesGene>::value; });
  return !any;
}

bool adoptSpecies(Creature& c, SpeciesId species, uint32_t speciesSeed, const LookBands& bands) {
  if (species == kGrungo || !predatesSpecies(c.genome())) return false;
  const Genome founder = foundingGenome(species, speciesSeed, bands);
  const Genome grungo = starterGenome(speciesSeed);

  // The founder's genes past the recipe, in his order (the SpeciesGene first,
  // the PowerGene last). A line that already feels duels (graftDuelGenes)
  // keeps its own duel genes rather than feeling every duel twice.
  const bool feels = feelsDuels(c.genome());
  GenomeBuilder grown = GenomeBuilder::from(c.genome());
  founder.forEach([&](const GeneView& v) {
    if (v.header.uid.v < kSpeciesGeneUid.v || c.genome().find(v.header.uid)) return;
    if (feels && isDuelGene(v.header.uid)) return;
    grown.append(v.header, v.body);
  });
  std::optional<Genome> g = grown.build();
  if (!g || !c.extendGenome(std::move(*g))) return false;

  // The recipe bytes the species owns: the founder's rolled look, and every
  // byte where the species' starter differs from grungo's. Collected first,
  // because each edit replaces the genome the views point into.
  struct Edit { GeneUid uid; uint8_t at, value; };
  std::vector<Edit> edits;
  founder.forEach([&](const GeneView& f) {
    if (f.header.uid.v >= kSpeciesGeneUid.v) return;
    const std::optional<GeneView> mine = c.genome().find(f.header.uid);
    const std::optional<GeneView> base = grungo.find(f.header.uid);
    if (!mine || !base || mine->header.type != f.header.type || mine->header.len != f.header.len ||
        base->header.len != f.header.len)
      return;
    for (uint8_t i = 0; i < f.header.len; ++i)
      if ((rollOf(f, i, bands) || base->body[i] != f.body[i]) && mine->body[i] != f.body[i])
        edits.push_back(Edit{f.header.uid, i, f.body[i]});
  });
  for (const Edit& e : edits) c.editGene(e.uid, e.at, e.value);
  return true;
}

SpeciesId speciesOf(const Genome& g) {
  Phenotype p;
  expressStage(g, Stage::Baby, 0, p);
  return p.species.body;
}

SpeciesId advertisedSpecies(const Genome* pet, const uint8_t mac[6]) {
  if (!pet) return badgeSpecies(mac);
  if (predatesSpecies(*pet))
    if (std::optional<SpeciesId> s = tableSpecies(mac)) return *s;
  return speciesOf(*pet);
}

}  // namespace blorb
