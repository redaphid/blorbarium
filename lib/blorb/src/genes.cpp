#include "blorb/genes.h"

#include <algorithm>
#include <array>
#include <cstring>
#include "blorb/creature.h"
#include "blorb/protocol.h"

namespace blorb {
namespace {

// A known kind whose len disagrees with its layout is never expressed, but
// describe_ may still be handed one, so decoding zero-fills what is missing.
template <class Body>
Body decode(const GeneView& v) {
  Body b{};
  std::memcpy(&b, v.body, std::min<size_t>(v.header.len, sizeof(Body)));
  return b;
}

// 2^(k/12) in Q16, the scale decayFromByte uses for half-lives.
constexpr uint32_t kSemitoneQ16[12] = {65536, 69433, 73562, 77936, 82570, 87480,
                                       92682, 98193, 104032, 110218, 116772, 123715};

// Rates share the half-life time scale: byte b means "fills 0 to 1 in
// 10 ticks * 2^((b-1)/12)" (1 s at byte 1, about 27 days at 255); 0 = none.
// The result is the amount per application when one happens every
// 2^period ticks, computed here so a slow rate keeps its precision.
Fx perApplication(uint8_t b, uint8_t period) {
  if (b == 0) return Fx::zero();
  uint32_t k = uint32_t(b) - 1;
  int shift = int(k / 12) - int(period);
  if (shift < -20) return Fx::one();
  uint64_t num = uint64_t(Fx::kOne) << 16;
  uint64_t den = uint64_t(10) * kSemitoneQ16[k % 12];
  if (shift >= 0) den <<= shift;
  else num <<= -shift;
  uint64_t v = (num + den / 2) / den;   // nearest, so even the slowest byte moves one LSB
  return Fx{int32_t(std::min<uint64_t>(v, uint64_t(Fx::kOne)))};
}

constexpr uint8_t kMaxPeriod = 31;

// Receptor gain: 128 = 0, steps of 1/16, so -8 .. +7.94.
Fx receptorGain(uint8_t b) { return Fx{(int32_t(b) - 128) * (Fx::kOne / 16)}; }

const char* chemName(uint8_t c) {
  if (c >= kDriveBase && c < kDriveBase + kDriveCount) return DRIVES[c - kDriveBase].name;
  for (const ChemInfo& i : CHEMICALS) if (i.id.v == c) return i.name;
  return nullptr;
}
template <class Row, size_t N>
const char* rowName(const Row (&rows)[N], uint8_t id) {
  for (const Row& r : rows) if (r.id.v == id) return r.name;
  return nullptr;
}
void named(Describe& d, const char* field, const char* name, uint8_t raw) {
  if (name) d.field(field, name);
  else d.field(field, raw);
}
void chemField(Describe& d, const char* field, uint8_t c) { named(d, field, chemName(c), c); }
void locusField(Describe& d, const char* field, uint8_t l) { named(d, field, rowName(LOCI, l), l); }
void driveField(Describe& d, const char* field, uint8_t v) { named(d, field, rowName(DRIVES, v), v); }

constexpr MutRule A = MutRule::Any, N = MutRule::Nudge, F = MutRule::Flag, X = MutRule::Fixed;

}  // namespace

// ---- chem ----------------------------------------------------------------------
const MutRule rules_chem[] = {A, N, N};
void express_chem(const GeneView& v, Phenotype& p) {
  auto g = decode<ChemGene>(v);
  p.chem.decay[g.chem] = decayFromByte(g.halfLife);
  if (g.initial != 0) p.chem.seeds.push_back(Seed{ChemId{g.chem}, Fx::unitByte(g.initial)});
}
void describe_chem(const GeneView& v, Describe& d) {
  auto g = decode<ChemGene>(v);
  chemField(d, "chem", g.chem);
  d.field("halfLife", g.halfLife);
  d.field("initial", g.initial);
}

// ---- reaction ------------------------------------------------------------------
const MutRule rules_reaction[] = {A, N, A, N, A, N, A, N, N};
void express_reaction(const GeneView& v, Phenotype& p) {
  auto g = decode<ReactionGene>(v);
  p.chem.reactions.push_back(Reaction{ChemId{g.a}, ChemId{g.b}, ChemId{g.c}, ChemId{g.d}, g.qa, g.qb, g.qc, g.qd,
                                      perApplication(g.rate, 0)});
}
void describe_reaction(const GeneView& v, Describe& d) {
  auto g = decode<ReactionGene>(v);
  chemField(d, "a", g.a); d.field("qa", g.qa);
  chemField(d, "b", g.b); d.field("qb", g.qb);
  chemField(d, "c", g.c); d.field("qc", g.qc);
  chemField(d, "d", g.d); d.field("qd", g.qd);
  d.field("rate", g.rate);
}

// ---- emitter -------------------------------------------------------------------
const MutRule rules_emitter[] = {A, A, N, N, F, N};
void express_emitter(const GeneView& v, Phenotype& p) {
  auto g = decode<EmitterGene>(v);
  uint8_t period = std::min(g.period, kMaxPeriod);
  p.chem.emitters.push_back(Emitter{LocusId{g.locus}, ChemId{g.chem}, Fx::unitByte(g.threshold),
                                    perApplication(g.gain, period), (g.flags & 1) != 0, (g.flags & 2) != 0,
                                    (g.flags & 4) != 0, period});
}
void describe_emitter(const GeneView& v, Describe& d) {
  auto g = decode<EmitterGene>(v);
  locusField(d, "locus", g.locus);
  chemField(d, "chem", g.chem);
  d.field("threshold", g.threshold);
  d.field("gain", g.gain);
  d.field("flags", g.flags);
  d.field("period", g.period);
}

// ---- receptor ------------------------------------------------------------------
const MutRule rules_receptor[] = {A, A, N, N, N, F};
void express_receptor(const GeneView& v, Phenotype& p) {
  auto g = decode<ReceptorGene>(v);
  p.chem.receptors.push_back(Receptor{ChemId{g.chem}, LocusId{g.locus}, Fx::unitByte(g.threshold),
                                      Fx::signedByte(g.nominal), receptorGain(g.gain), (g.flags & 1) != 0,
                                      (g.flags & 2) != 0});
}
void describe_receptor(const GeneView& v, Describe& d) {
  auto g = decode<ReceptorGene>(v);
  chemField(d, "chem", g.chem);
  locusField(d, "locus", g.locus);
  d.field("threshold", g.threshold);
  d.field("nominal", g.nominal);
  d.field("gain", g.gain);
  d.field("flags", g.flags);
}

// ---- stimulus ------------------------------------------------------------------
const MutRule rules_stimulus[] = {A, F, A, A, A, N, N, N};
void express_stimulus(const GeneView& v, Phenotype& p) {
  auto g = decode<StimulusGene>(v);
  Phenotype::StimResponse r{StimId{g.stim}, (g.flags & 1) != 0, (g.flags & 2) != 0, {}, {}};
  for (int i = 0; i < 3; ++i) {
    r.chem[i] = ChemId{g.chem[i]};
    r.amount[i] = Fx::signedByte(g.amount[i]);
  }
  p.stimuli.push_back(r);
}
void describe_stimulus(const GeneView& v, Describe& d) {
  auto g = decode<StimulusGene>(v);
  named(d, "stim", rowName(STIMULI, g.stim), g.stim);
  d.field("flags", g.flags);
  static const char* const chems[3] = {"chem0", "chem1", "chem2"};
  static const char* const amounts[3] = {"amount0", "amount1", "amount2"};
  for (int i = 0; i < 3; ++i) {
    chemField(d, chems[i], g.chem[i]);
    d.field(amounts[i], g.amount[i]);
  }
}

// ---- instinct ------------------------------------------------------------------
const MutRule rules_instinct[] = {A, A, A, A, A, N, N};
void express_instinct(const GeneView& v, Phenotype& p) {
  auto g = decode<InstinctGene>(v);
  Instinct in{{}, 0, ActionId{g.action}, DriveId{g.drive}, Fx::signedByte(g.level), Fx::unitByte(g.strength)};
  for (uint8_t c : g.cue) if (c != 255) in.cue[in.cueCount++] = LocusId{c};
  p.instincts.push_back(in);
}
void describe_instinct(const GeneView& v, Describe& d) {
  auto g = decode<InstinctGene>(v);
  static const char* const cues[3] = {"cue0", "cue1", "cue2"};
  for (int i = 0; i < 3; ++i) locusField(d, cues[i], g.cue[i]);
  named(d, "action", rowName(ACTIONS, g.action), g.action);
  driveField(d, "drive", g.drive);
  d.field("level", g.level);
  d.field("strength", g.strength);
}

// ---- expression ----------------------------------------------------------------
const MutRule rules_expression[] = {A, N, A, A, A, N, N, N};
void express_expression(const GeneView& v, Phenotype& p) {
  auto g = decode<ExpressionGene>(v);
  Phenotype::Face f{ExprId{g.face}, Fx::unitByte(g.weight), {}, {}};
  for (int i = 0; i < 3; ++i) {
    f.drive[i] = DriveId{g.drive[i]};
    f.amount[i] = Fx::signedByte(g.amount[i]);
  }
  p.faces.push_back(f);
}
void describe_expression(const GeneView& v, Describe& d) {
  auto g = decode<ExpressionGene>(v);
  named(d, "face", rowName(EXPRESSIONS, g.face), g.face);
  d.field("weight", g.weight);
  static const char* const drives[3] = {"drive0", "drive1", "drive2"};
  static const char* const amounts[3] = {"amount0", "amount1", "amount2"};
  for (int i = 0; i < 3; ++i) {
    driveField(d, drives[i], g.drive[i]);
    d.field(amounts[i], g.amount[i]);
  }
}

// ---- temperament ---------------------------------------------------------------
const MutRule rules_temperament[] = {N, N, N, N, N, N, N};
void express_temperament(const GeneView& v, Phenotype& p) {
  auto g = decode<TemperamentGene>(v);
  p.temperament = Temperament{Fx::unitByte(g.learnRate),   Fx::unitByte(g.forgetRate),
                              Fx::unitByte(g.explore),     Fx::unitByte(g.habituation),
                              uint16_t(g.dreamEvery * 10), uint16_t(g.dreamLen * 10),
                              decayFromByte(g.traceHalfLife)};
}
void describe_temperament(const GeneView& v, Describe& d) {
  auto g = decode<TemperamentGene>(v);
  d.field("learnRate", g.learnRate);
  d.field("forgetRate", g.forgetRate);
  d.field("explore", g.explore);
  d.field("habituation", g.habituation);
  d.field("dreamEvery", g.dreamEvery);
  d.field("dreamLen", g.dreamLen);
  d.field("traceHalfLife", g.traceHalfLife);
}

// ---- memory --------------------------------------------------------------------
// The half-life of a recent-stimulus locus, so how long the brain still sees a
// stimulus as its situation. Capped near a minute: past that, "recent" would
// mean "ever" and the feature would turn into a bias.
constexpr uint8_t kLongestMemory = 72;
const MutRule rules_memory[] = {N};
void express_memory(const GeneView& v, Phenotype& p) {
  auto g = decode<MemoryGene>(v);
  p.recentFade = g.recentHalfLife == 0 ? Phenotype::kOneTickMemory
                                       : decayFromByte(std::min(g.recentHalfLife, kLongestMemory));
}
void describe_memory(const GeneView& v, Describe& d) {
  d.field("recentHalfLife", decode<MemoryGene>(v).recentHalfLife);
}

// ---- palette -------------------------------------------------------------------
const MutRule rules_palette[] = {X, N, N, N, A, N};
void express_palette(const GeneView& v, Phenotype& p) {
  auto g = decode<PaletteGene>(v);
  p.palette.push_back(Phenotype::Paint{RegionId{uint8_t(g.region % kRegionCount)},
                                       Tint{int8_t(int(g.hue) - 128), g.sat, g.val}, ChemId{g.chemBound},
                                       Fx::unitByte(g.gain), g.chemBound != 0});
}
void describe_palette(const GeneView& v, Describe& d) {
  auto g = decode<PaletteGene>(v);
  d.field("region", REGIONS[g.region % kRegionCount].name);
  d.field("hue", g.hue);
  d.field("sat", g.sat);
  d.field("val", g.val);
  chemField(d, "chemBound", g.chemBound);
  d.field("gain", g.gain);
}

// ---- size ----------------------------------------------------------------------
const MutRule rules_size[] = {N, N, N};
void express_size(const GeneView& v, Phenotype& p) {
  auto g = decode<SizeGene>(v);
  p.size = Phenotype::Size{g.base, g.growth, g.squash};
}
void describe_size(const GeneView& v, Describe& d) {
  auto g = decode<SizeGene>(v);
  d.field("base", g.base);
  d.field("growth", g.growth);
  d.field("squash", g.squash);
}

// ---- mark ----------------------------------------------------------------------
const MutRule rules_mark[] = {X, N, N, A};
void express_mark(const GeneView& v, Phenotype& p) {
  auto g = decode<MarkGene>(v);
  p.marks.push_back(Phenotype::Mark{g.layer, g.variant, g.tint, ChemId{g.chemBound}, g.chemBound != 0});
}
void describe_mark(const GeneView& v, Describe& d) {
  auto g = decode<MarkGene>(v);
  d.field("layer", g.layer);
  d.field("variant", g.variant);
  d.field("tint", g.tint);
  chemField(d, "chemBound", g.chemBound);
}

// ---- mutation (read by policyOf, which reads the genome itself) -----------------
const MutRule rules_mutation[] = {N, N, N, N, N, N, N, N};
void express_mutation(const GeneView&, Phenotype&) {}
void describe_mutation(const GeneView& v, Describe& d) {
  auto g = decode<MutationPolicyGene>(v);
  d.field("point", g.point);
  d.field("dup", g.dup);
  d.field("del", g.del);
  d.field("wake", g.wake);
  d.field("sleep", g.sleep);
  d.field("wild", g.wild);
  d.field("heirloomMax", g.heirloomMax);
  d.field("heirloomConf", g.heirloomConf);
}

// ---- egg -----------------------------------------------------------------------
const MutRule rules_egg[] = {N, N, N};
void express_egg(const GeneView& v, Phenotype& p) {
  auto g = decode<EggGene>(v);
  p.egg = EggRules{g.incubateMinutes * kTicksPerMinute, Fx::unitByte(g.warmthBoost), g.hatchBurstDreams};
}
void describe_egg(const GeneView& v, Describe& d) {
  auto g = decode<EggGene>(v);
  d.field("incubateMinutes", g.incubateMinutes);
  d.field("warmthBoost", g.warmthBoost);
  d.field("hatchBurstDreams", g.hatchBurstDreams);
}

// ---- metabolism ----------------------------------------------------------------
const MutRule rules_metabolism[] = {N, N, N, N};
void express_metabolism(const GeneView& v, Phenotype& p) {
  auto g = decode<MetabolismGene>(v);
  p.habitat = HabitatRules{uint8_t(std::clamp<int>(g.pantrySize, 1, Habitat::kMaxPellets)),
                           std::max<uint32_t>(1, g.refillHours) * kTicksPerHour / 4,
                           std::max<uint32_t>(1, g.rotMinutes) * kTicksPerMinute, Fx::unitByte(g.biteSize)};
}
void describe_metabolism(const GeneView& v, Describe& d) {
  auto g = decode<MetabolismGene>(v);
  d.field("pantrySize", g.pantrySize);
  d.field("refillHours", g.refillHours);
  d.field("rotMinutes", g.rotMinutes);
  d.field("biteSize", g.biteSize);
}

// ---- note ----------------------------------------------------------------------
const MutRule rules_note[] = {X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X};
void express_note(const GeneView&, Phenotype&) {}
void describe_note(const GeneView& v, Describe& d) {
  auto g = decode<NoteGene>(v);
  char text[sizeof g.text + 1] = {};
  for (size_t i = 0; i < sizeof g.text && g.text[i] != 0; ++i)
    text[i] = (g.text[i] >= 0x20 && g.text[i] < 0x7F) ? char(g.text[i]) : '?';
  d.field("text", text);
}

// ---- lookup and expression -----------------------------------------------------
namespace {
constexpr uint8_t kNoKind = 0xFF;
constexpr std::array<uint8_t, 256> buildKindIndex() {
  std::array<uint8_t, 256> index{};
  for (uint8_t& i : index) i = kNoKind;
  for (size_t i = 0; i < countOf(GENE_TYPES); ++i) index[GENE_TYPES[i].type] = uint8_t(i);
  return index;
}
constexpr std::array<uint8_t, 256> kKindIndex = buildKindIndex();
static_assert(countOf(GENE_TYPES) < kNoKind, "the kind index stores row numbers in a byte");
}  // namespace

const GeneTypeInfo* geneType(uint8_t type) {
  uint8_t row = kKindIndex[type];
  return row == kNoKind ? nullptr : &GENE_TYPES[row];
}

void expressStage(const Genome& genome, Stage stage, uint32_t legacyFeats, Phenotype& p) {
  if (uint8_t(stage) >= kStageCount) return;
  uint8_t bit = uint8_t(1u << uint8_t(stage));
  if (p.expressedStages & bit) return;
  p.expressedStages |= bit;
  genome.forEach([&](const GeneView& v) {
    const GeneTypeInfo* info = geneType(v.header.type);
    if (info && v.header.len == info->bodyLen && expressedAt(v.header, stage, legacyFeats)) info->express(v, p);
  });
}

}  // namespace blorb
