// A genome's trait sheet, decoded by the engine itself: the raw facts the AI
// lines pipeline (#37, web/src/traits.js) turns into words. The Worker decodes
// genome bytes in JavaScript; this tool is the reference it is tested against
// (web/test/traits.test.mjs reads the fixtures this writes).
//
// Build and run from the checkout (one g++, under the build lock):
//
//   flock /tmp/blorb-pio.lock g++ -std=c++17 -O1 -Ilib/blorb/include -o .jobs/trait_sheet
//       tools/trait_sheet.cpp lib/blorb/src/*.cpp                       (one command)
//   .jobs/trait_sheet baselines  > web/src/baselines.json   every species' starter, per stage
//   .jobs/trait_sheet fixtures   > web/test/fixtures/genomes.json   the eval's 20 pets
//   .jobs/trait_sheet hex <genome hex> [stage 0-3] [feats]
//
// Raw means bytes and engine units, never words: the words are traits.js's
// job, relative to the species' own starter (the baselines), so "bolder than
// most mossbys" means what it says.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "blorb/creature.h"
#include "blorb/genes.h"
#include "blorb/mutate.h"
#include "blorb/registry.h"
#include "blorb/thoughts.h"
#include "blorb/species.h"

using namespace blorb;

namespace {

// ---- a tiny JSON writer ----------------------------------------------------------
struct Json {
  std::string s;
  bool first = true;
  void sep() { if (!first) s += ","; first = false; }
  void key(const char* k) { sep(); s += "\""; s += k; s += "\":"; first = true; }
  void open(char c) { sep(); s += c; first = true; }
  void close(char c) { s += c; first = false; }
  void str(const char* v) {
    sep();
    s += "\"";
    for (const char* p = v; *p; ++p) {
      if (*p == '"' || *p == '\\') s += '\\';
      s += *p;
    }
    s += "\"";
  }
  void num(long v) { sep(); s += std::to_string(v); }
  void null() { sep(); s += "null"; }
  void kstr(const char* k, const char* v) { key(k); str(v); }
  void knum(const char* k, long v) { key(k); num(v); }
};

template <class Row, size_t N>
const char* rowName(const Row (&rows)[N], uint8_t id) {
  for (const Row& r : rows) if (r.id.v == id) return r.name;
  return nullptr;
}
const char* chemName(uint8_t c) {
  if (c >= kDriveBase && c < kDriveBase + kDriveCount) return DRIVES[c - kDriveBase].name;
  return rowName(CHEMICALS, c);
}
std::string locusName(uint8_t l) {
  if (const char* n = rowName(LOCI, l)) return n;
  if (l >= 64 && l < 128) {
    if (const char* s = rowName(STIMULI, uint8_t(l - 64))) return std::string("recent_") + s;
  }
  return "locus_" + std::to_string(l);
}
const char* stageName(uint8_t s) {
  static const char* const names[] = {"baby", "child", "adult", "elder"};
  return s < 4 ? names[s] : "baby";
}
const char* stanceName(uint8_t s) {
  static const char* const names[] = {"pounce", "guard", "trick"};
  return s < 3 ? names[s] : "pounce";
}
const char* elementName(Element e) {
  static const char* const names[] = {"splash", "sprout", "spark"};
  return names[uint8_t(e)];
}
// Fx to permille, truncated toward zero, as traits.js does.
long permille(Fx v) { return long((int64_t(v.raw) * 1000) / Fx::kOne); }

bool known(const GeneView& v) {
  const GeneTypeInfo* info = geneType(v.header.type);
  return info && v.header.len == info->bodyLen;
}
bool expressedBy(const GeneView& v, uint8_t stage, uint32_t feats) {
  if (!known(v)) return false;
  for (uint8_t s = 0; s <= stage; ++s)
    if (expressedAt(v.header, Stage(s), feats)) return true;
  return false;
}
template <class Body> bool is(const GeneView& v) { return v.header.type == GeneKindOf<Body>::value; }
template <class Body> Body body(const GeneView& v) {
  Body b{};
  std::memcpy(&b, v.body, sizeof b);
  return b;
}

// Genes in the order the engine expresses them: stage by stage, genome order within.
template <class F> void inExpressionOrder(const Genome& g, uint8_t stage, uint32_t feats, F&& f) {
  for (uint8_t s = 0; s <= stage; ++s)
    g.forEach([&](const GeneView& v) { if (known(v) && expressedAt(v.header, Stage(s), feats)) f(v); });
}

void rawSheet(Json& j, const Genome& g, uint8_t stage, uint32_t feats) {
  Phenotype p;
  for (uint8_t s = 0; s <= stage; ++s) expressStage(g, Stage(s), feats, p);
  const SpeciesInfo& sp = speciesInfo(p.species.body);
  j.open('{');
  j.kstr("species", sp.name);
  j.kstr("power", powerInfo(p.species.power).name);
  // The knack's order ranks the stances 2 (best) to 0.
  uint8_t loved = 0;
  for (uint8_t s = 0; s < kCycle; ++s) if (kKnackOrders[p.species.knackOrder][s] == 2) loved = s;
  j.kstr("lovedStance", stanceName(loved));
  j.kstr("element", elementName(sp.element));
  j.kstr("style", stanceName(uint8_t(sp.style)));
  j.kstr("voice", rowName(VOICES, p.oracle.voice.v));
  j.kstr("stage", stageName(stage));

  // Boldness: the receptors from adrenaline to the startle locus, gain in 1/16 steps.
  long startle = 0;
  for (const Receptor& r : p.chem.receptors)
    if (r.chem == chem::adrenaline && r.locus == locus::startle) startle += long(r.gain.raw / (Fx::kOne / 16));
  j.knum("startleGain", startle);

  // Tonic emitters as bytes (fill time on the half-life scale: smaller fills faster).
  std::vector<long> hunger, melatonin, sleepiness;
  long hungerHalfLife = -1;
  inExpressionOrder(g, stage, feats, [&](const GeneView& v) {
    if (is<EmitterGene>(v)) {
      auto e = body<EmitterGene>(v);
      if (e.chem == drive::hunger.v + kDriveBase && e.locus == locus::always.v) hunger.push_back(e.gain);
      if (e.chem == chem::melatonin.v && e.locus == locus::light.v) melatonin.push_back(e.gain);
      if (e.chem == drive::sleepiness.v + kDriveBase) sleepiness.push_back(e.gain);
    }
    if (is<ChemGene>(v)) {
      auto c = body<ChemGene>(v);
      if (c.chem == drive::hunger.v + kDriveBase) hungerHalfLife = c.halfLife;
    }
  });
  auto list = [&](const char* k, const std::vector<long>& xs) {
    j.key(k);
    j.open('[');
    for (long x : xs) j.num(x);
    j.close(']');
  };
  list("hungerFill", hunger);
  j.knum("hungerHalfLife", hungerHalfLife);
  list("melatoninFill", melatonin);
  list("sleepinessFill", sleepiness);

  const Temperament& t = p.temperament;
  j.key("temperament");
  j.open('{');
  j.knum("learnRate", permille(t.learnRate));
  j.knum("forgetRate", permille(t.forgetRate));
  j.knum("explore", permille(t.explore));
  j.knum("habituation", permille(t.habituation));
  j.knum("dreamEvery", t.dreamEveryTicks);
  j.knum("dreamLen", t.dreamLenTicks);
  j.close('}');

  // What each stimulus does to him: the sum over his stimulus genes, per chemical.
  std::map<std::string, std::map<std::string, long>> stim;
  for (const Phenotype::StimResponse& r : p.stimuli) {
    const char* sn = rowName(STIMULI, r.stim.v);
    std::string sname = sn ? sn : "stim_" + std::to_string(r.stim.v);
    for (int i = 0; i < 3; ++i) {
      if (r.chem[i].v == 0) continue;
      const char* cn = chemName(r.chem[i].v);
      stim[sname][cn ? cn : "chem_" + std::to_string(r.chem[i].v)] += permille(r.amount[i]);
    }
  }
  j.key("stimuli");
  j.open('{');
  for (auto& [sname, chems] : stim) {
    j.key(sname.c_str());
    j.open('{');
    for (auto& [cname, v] : chems) j.knum(cname.c_str(), v);
    j.close('}');
  }
  j.close('}');

  j.key("palette");
  j.open('[');
  for (const Phenotype::Paint& c : p.palette) {
    j.open('{');
    j.kstr("region", REGIONS[c.region.v].name);
    j.knum("hue", c.tint.hue);
    j.knum("sat", c.tint.sat);
    j.knum("val", c.tint.val);
    j.key("bound");
    if (c.hasBound) j.str(chemName(c.bound.v) ? chemName(c.bound.v) : "?");
    else j.null();
    j.close('}');
  }
  j.close(']');
  j.key("marks");
  j.open('[');
  for (const Phenotype::Mark& m : p.marks) {
    j.open('{');
    j.knum("layer", m.layer);
    j.knum("variant", m.variant);
    j.knum("tint", m.tint);
    j.close('}');
  }
  j.close(']');
  j.key("size");
  j.open('{');
  j.knum("base", p.size.base);
  j.knum("growth", p.size.growth);
  j.knum("squash", p.size.squash);
  j.close('}');

  // Life genes read straight from their bodies (the last expressed wins, as expression does).
  MetabolismGene meta{};
  EggGene egg{};
  MutationPolicyGene mut{};
  std::string motto;
  long dormant = 0, genes = 0;
  inExpressionOrder(g, stage, feats, [&](const GeneView& v) {
    if (is<MetabolismGene>(v)) meta = body<MetabolismGene>(v);
    if (is<EggGene>(v)) egg = body<EggGene>(v);
  });
  g.forEach([&](const GeneView& v) {
    ++genes;
    if (known(v) && (v.header.flags & GeneFlags::Dormant)) ++dormant;
    if (!known(v)) return;
    if (is<MutationPolicyGene>(v)) mut = body<MutationPolicyGene>(v);
    if (is<NoteGene>(v) && motto.empty()) {
      auto n = body<NoteGene>(v);
      for (size_t i = 0; i < sizeof n.text && n.text[i]; ++i)
        motto += (n.text[i] >= 0x20 && n.text[i] < 0x7F) ? char(n.text[i]) : '?';
    }
  });
  j.key("metabolism");
  j.open('{');
  j.knum("pantrySize", meta.pantrySize);
  j.knum("refillHours", meta.refillHours);
  j.knum("rotMinutes", meta.rotMinutes);
  j.knum("biteSize", meta.biteSize);
  j.close('}');
  j.knum("incubateMinutes", egg.incubateMinutes);
  j.knum("wild", mut.wild);
  j.kstr("motto", motto.c_str());
  j.knum("dormant", dormant);
  j.knum("genes", genes);

  // Heirlooms: instincts an ancestor learned, which he was born with.
  j.key("heirlooms");
  j.open('[');
  inExpressionOrder(g, stage, feats, [&](const GeneView& v) {
    if (!is<InstinctGene>(v) || !(v.header.flags & GeneFlags::Heirloom)) return;
    auto in = body<InstinctGene>(v);
    j.open('{');
    j.kstr("cue", locusName(in.cue[0]).c_str());
    const char* a = rowName(ACTIONS, in.action);
    j.kstr("action", a ? a : "?");
    const char* d = rowName(DRIVES, in.drive);
    j.kstr("drive", d ? d : "?");
    j.knum("level", long(in.level) - 128);
    j.close('}');
  });
  j.close(']');
  j.close('}');
}

std::string hexOf(const std::vector<uint8_t>& b) {
  static const char* const digits = "0123456789abcdef";
  std::string s;
  for (uint8_t x : b) { s += digits[x >> 4]; s += digits[x & 15]; }
  return s;
}

std::vector<uint8_t> unhex(const char* h) {
  std::vector<uint8_t> out;
  for (size_t i = 0; h[i] && h[i + 1]; i += 2) out.push_back(uint8_t(std::strtoul(std::string(h + i, 2).c_str(), nullptr, 16)));
  return out;
}

// The eval's pets: two per species. The first is a near-starter (one
// generation of mutation); the second has drifted for many generations, some
// carry ancestors' lessons, and two are hybrids wearing another species' power.
struct FixtureSpec { uint8_t species; uint32_t seed; uint16_t gens; uint8_t stage; bool heirlooms; int hybridPower; };

Genome breed(const FixtureSpec& f) {
  Genome g = starterGenomeOf(SpeciesId{f.species}, f.seed);
  Rng rng = Rng::seeded(f.seed * 2654435761u + f.gens);
  std::vector<Belief> lessons;
  if (f.heirlooms) {
    lessons = {Belief{locus::cradled, action::rest, drive::fear, Fx::ratio(-3, 4), Fx::one()},
               Belief{locus::recent(stim::shake), action::curl, drive::fear, Fx::ratio(-1, 2), Fx::one()},
               Belief{locus::owner_near, action::call, drive::loneliness, Fx::ratio(-2, 3), Fx::one()}};
  }
  for (uint16_t i = 0; i < f.gens; ++i) {
    const bool last = i + 1 == f.gens;
    Offspring o = mutate(g, policyOf(g, Fx::zero()), last ? lessons : std::vector<Belief>{}, rng);
    g = o.genome;
  }
  if (f.hybridPower >= 0) g = *GenomeBuilder::from(g).setByte(kPowerGeneUid, 0, uint8_t(f.hybridPower)).build();
  return g;
}

int baselines() {
  Json j;
  j.open('{');
  for (const SpeciesInfo& sp : SPECIES) {
    j.key(sp.name);
    j.open('{');
    const Genome g = starterGenomeOf(sp.id, 1);
    for (uint8_t s = 0; s < kStageCount; ++s) {
      j.key(stageName(s));
      rawSheet(j, g, s, 0);
    }
    j.close('}');
  }
  j.close('}');
  std::printf("%s\n", j.s.c_str());
  return 0;
}

int fixtures() {
  std::vector<FixtureSpec> specs;
  for (uint8_t sp = 0; sp < kSpeciesCount; ++sp) {
    specs.push_back({sp, 1000u + sp, 1, uint8_t(Stage::Adult), false, -1});
    const uint16_t gens = uint16_t(6 + (sp % 3) * 5);   // 6, 11 or 16 generations of drift
    const uint8_t stage = uint8_t(sp % 4 == 3 ? Stage::Elder : (sp % 4 == 1 ? Stage::Child : Stage::Adult));
    const int hybrid = sp == 3 ? int(power::zap.v) : (sp == 8 ? int(power::foresee.v) : -1);
    specs.push_back({sp, 2000u + sp, gens, stage, sp % 2 == 0, hybrid});
  }
  Json j;
  j.open('[');
  for (const FixtureSpec& f : specs) {
    const Genome g = breed(f);
    j.open('{');
    char id[32];
    std::snprintf(id, sizeof id, "%s-%c", SPECIES[f.species].name, f.gens == 1 ? 'a' : 'b');
    j.kstr("id", id);
    j.knum("generation", f.gens + 1);
    j.knum("feats", 0);
    j.kstr("genome", hexOf(g.bytes()).c_str());
    j.key("raw");
    rawSheet(j, g, f.stage, 0);
    j.close('}');
  }
  j.close(']');
  std::printf("%s\n", j.s.c_str());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && !std::strcmp(argv[1], "baselines")) return baselines();
  if (argc >= 2 && !std::strcmp(argv[1], "fixtures")) return fixtures();
  if (argc >= 3 && !std::strcmp(argv[1], "hex")) {
    std::vector<uint8_t> b = unhex(argv[2]);
    auto g = Genome::parse(b.data(), b.size());
    if (!g) { std::fprintf(stderr, "not a genome\n"); return 1; }
    Json j;
    rawSheet(j, *g, uint8_t(argc > 3 ? std::atoi(argv[3]) : 2), uint32_t(argc > 4 ? std::strtoul(argv[4], nullptr, 0) : 0));
    std::printf("%s\n", j.s.c_str());
    return 0;
  }
  std::fprintf(stderr, "usage: trait_sheet baselines | fixtures | hex <genome hex> [stage] [feats]\n");
  return 2;
}
