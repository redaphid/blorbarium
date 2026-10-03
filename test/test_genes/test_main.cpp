#include <gtest/gtest.h>
#include <set>
#include <string>
#include <vector>
#include "blorb/creature.h"
#include "blorb/genes.h"
#include "blorb/protocol.h"
#include "blorb/seams.h"

using namespace blorb;

namespace {

struct CaptureLink : Link {
  std::vector<std::string> lines;
  bool connected() override { return true; }
  std::optional<std::string_view> readLine() override { return std::nullopt; }
  void writeLine(std::string_view s) override { lines.emplace_back(s); }
};

Genome oneGene(uint8_t type, const std::vector<uint8_t>& body, Stage stage = Stage::Baby, uint8_t flags = 0,
               uint8_t featGate = 0) {
  GenomeBuilder b;
  b.append(GeneHeader{type, uint8_t(body.size()), flags, stage, featGate, 64, GeneUid{1}}, body.data());
  return *b.build();
}

Phenotype expressed(const Genome& g, Stage stage = Stage::Baby, uint32_t feats = 0) {
  Phenotype p{};
  expressStage(g, stage, feats, p);
  return p;
}

// Everything expression can write, flattened, so two phenotypes compare whole.
std::vector<int64_t> fingerprint(const Phenotype& p) {
  std::vector<int64_t> f;
  for (const Decay& d : p.chem.decay) f.insert(f.end(), {d.keep, d.shift});
  for (const Reaction& r : p.chem.reactions) f.insert(f.end(), {r.a.v, r.b.v, r.c.v, r.d.v, r.qa, r.qd, r.rate.raw});
  for (const Emitter& e : p.chem.emitters) f.insert(f.end(), {e.locus.v, e.chem.v, e.gain.raw, e.threshold.raw});
  for (const Receptor& r : p.chem.receptors) f.insert(f.end(), {r.chem.v, r.locus.v, r.gain.raw, r.nominal.raw});
  for (const Seed& s : p.chem.seeds) f.insert(f.end(), {s.chem.v, s.level.raw});
  for (const auto& s : p.stimuli) f.insert(f.end(), {s.stim.v, s.amount[0].raw, s.whenAsleep});
  for (const Instinct& i : p.instincts) f.insert(f.end(), {i.action.v, i.cueCount, i.level.raw});
  for (const auto& x : p.faces) f.insert(f.end(), {x.face.v, x.weight.raw});
  for (const auto& x : p.palette) f.insert(f.end(), {x.region.v, x.tint.hue, x.tint.val});
  for (const auto& x : p.marks) f.insert(f.end(), {x.layer, x.variant});
  f.insert(f.end(), {p.size.base, p.temperament.learnRate.raw, p.recentFade.keep, p.egg.incubateTicks,
                     p.habitat.refillTicks, p.expressedStages, int64_t(p.chem.reactions.size()),
                     int64_t(p.instincts.size())});
  return f;
}

}  // namespace

TEST(GeneType, LookupAgreesWithTheTableForAllTypeBytes) {
  for (int t = 0; t < 256; ++t) {
    const GeneTypeInfo* want = nullptr;
    for (const GeneTypeInfo& info : GENE_TYPES) if (info.type == t) want = &info;
    EXPECT_EQ(geneType(uint8_t(t)), want) << "type " << t;
  }
  EXPECT_EQ(geneType(0x00), nullptr);
  EXPECT_EQ(geneType(0xFF), nullptr);
  EXPECT_EQ(geneType(0x20)->bodyLen, sizeof(PaletteGene));
}

TEST(Builder, AppendBodyTagsTheKindAndTakesTheNextFreeUid) {
  const uint8_t raw[1] = {0};
  GenomeBuilder b;
  b.append(SizeGene{130, 1, 2}, GeneFlags::Mutable);
  b.append(GeneHeader{0x55, 1, 0, Stage::Baby, 0, 64, GeneUid{10}}, raw);
  b.append(NoteGene{}, GeneFlags::OwnerEditable, Stage::Adult, 3, 9);
  Genome g = *b.build();
  ASSERT_EQ(g.geneCount(), 3u);
  EXPECT_EQ(g.gene(0).header.uid, GeneUid{1});
  EXPECT_EQ(g.gene(0).header.type, 0x21);
  EXPECT_EQ(g.gene(0).header.len, 3);
  EXPECT_EQ(g.gene(0).body[0], 130);
  GeneHeader note = g.gene(2).header;
  EXPECT_EQ(note.uid, GeneUid{11});
  EXPECT_EQ(note.type, 0x7F);
  EXPECT_EQ(note.len, 24);
  EXPECT_EQ(note.stage, Stage::Adult);
  EXPECT_EQ(note.featGate, 3);
  EXPECT_EQ(note.mutWeight, 9);
}

TEST(Decode, CrossSliceContracts) {
  Phenotype t = expressed(oneGene(0x13, {255, 0, 128, 51, 6, 3, 20}));
  EXPECT_EQ(t.temperament.learnRate, Fx::one());
  EXPECT_EQ(t.temperament.forgetRate, Fx::zero());
  EXPECT_EQ(t.temperament.habituation, Fx::unitByte(51));
  EXPECT_EQ(t.temperament.dreamEveryTicks, 60);
  EXPECT_EQ(t.temperament.dreamLenTicks, 30);
  EXPECT_EQ(t.temperament.trace.keep, decayFromByte(20).keep);

  Phenotype i = expressed(oneGene(0x11, {255, 14, 3, 2, 0, 40, 255}));
  ASSERT_EQ(i.instincts.size(), 1u);
  EXPECT_EQ(i.instincts[0].cueCount, 2);
  EXPECT_EQ(i.instincts[0].cue[0], locus::food_near);
  EXPECT_EQ(i.instincts[0].cue[1], locus::motion);
  EXPECT_EQ(i.instincts[0].action, action::eat);
  EXPECT_EQ(i.instincts[0].drive, drive::hunger);
  EXPECT_LT(i.instincts[0].level, Fx::zero());
  EXPECT_EQ(i.instincts[0].strength, Fx::one());

  Phenotype s = expressed(oneGene(0x10, {2, 3, 20, 0, 7, 205, 128, 0}));
  ASSERT_EQ(s.stimuli.size(), 1u);
  EXPECT_EQ(s.stimuli[0].stim, stim::shake);
  EXPECT_TRUE(s.stimuli[0].whenAsleep);
  EXPECT_TRUE(s.stimuli[0].wakes);
  EXPECT_EQ(s.stimuli[0].chem[0], chem::adrenaline);
  EXPECT_EQ(s.stimuli[0].amount[0], Fx::signedByte(205));
  EXPECT_EQ(s.stimuli[0].amount[2], -Fx::one());

  Phenotype pal = expressed(oneGene(0x20, {9, 0, 77, 200, 0, 255}));
  ASSERT_EQ(pal.palette.size(), 1u);
  EXPECT_EQ(pal.palette[0].region, region::cloak);   // 9 mod 7
  EXPECT_EQ(pal.palette[0].tint.hue, -128);
  EXPECT_EQ(pal.palette[0].tint.sat, 77);
  EXPECT_EQ(pal.palette[0].tint.val, 200);
  EXPECT_FALSE(pal.palette[0].hasBound);

  Phenotype m = expressed(oneGene(0x32, {0, 0, 0, 255}));
  EXPECT_EQ(m.habitat.pantrySize, 1);
  EXPECT_EQ(m.habitat.refillTicks, kTicksPerHour / 4);
  EXPECT_EQ(m.habitat.rotTicks, kTicksPerMinute);
  EXPECT_EQ(m.habitat.biteSize, Fx::one());
  EXPECT_EQ(expressed(oneGene(0x32, {200, 8, 90, 0})).habitat.pantrySize, Habitat::kMaxPellets);
  EXPECT_EQ(expressed(oneGene(0x32, {200, 8, 90, 0})).habitat.refillTicks, 2 * kTicksPerHour);

  Phenotype e = expressed(oneGene(0x31, {30, 255, 12}));
  EXPECT_EQ(e.egg.incubateTicks, 30 * kTicksPerMinute);
  EXPECT_EQ(e.egg.warmthBoost, Fx::one());
  EXPECT_EQ(e.egg.hatchBurstDreams, 12);

  Phenotype c = expressed(oneGene(0x01, {16, 217, 255}));
  EXPECT_EQ(c.chem.decay[16].keep, decayFromByte(217).keep);
  ASSERT_EQ(c.chem.seeds.size(), 1u);
  EXPECT_EQ(c.chem.seeds[0].level, Fx::one());
  EXPECT_TRUE(expressed(oneGene(0x01, {16, 217, 0})).chem.seeds.empty());
}

// Rates share the half-life scale: byte 1 fills in one second (10 ticks),
// every 12 bytes doubles the time, and the period scales one application.
TEST(Decode, RatesFollowTheHalfLifeScale) {
  auto emitterGain = [](uint8_t gain, uint8_t period) {
    return expressed(oneGene(0x03, {0, 1, 0, gain, 0, period})).chem.emitters.at(0).gain;
  };
  EXPECT_EQ(emitterGain(0, 0), Fx::zero());
  EXPECT_NEAR(emitterGain(1, 0).raw, Fx::ratio(1, 10).raw, 1);
  EXPECT_NEAR(emitterGain(13, 0).raw, Fx::ratio(1, 20).raw, 1);
  EXPECT_NEAR(emitterGain(13, 1).raw, Fx::ratio(1, 10).raw, 1);
  EXPECT_NEAR(emitterGain(181, 4).raw, emitterGain(181, 0).raw * 16, 16);
  EXPECT_GT(emitterGain(255, 0).raw, 0);   // about 27 days still moves
  EXPECT_EQ(emitterGain(1, 200), Fx::one());
  EXPECT_EQ(expressed(oneGene(0x03, {0, 1, 0, 1, 0, 200})).chem.emitters[0].period, 31);
  Fx rate = expressed(oneGene(0x02, {19, 1, 0, 0, 18, 1, 0, 0, 25})).chem.reactions.at(0).rate;
  EXPECT_NEAR(rate.raw, Fx::ratio(1, 40).raw, 1);
  Receptor r = expressed(oneGene(0x04, {20, 133, 64, 128, 144, 3})).chem.receptors.at(0);
  EXPECT_EQ(r.gain, Fx::one());
  EXPECT_EQ(r.nominal, Fx::zero());
  EXPECT_TRUE(r.digital && r.invert);
  EXPECT_EQ(expressed(oneGene(0x04, {20, 133, 64, 128, 0, 0})).chem.receptors[0].gain, Fx{-8 * Fx::kOne});
}

// Three base patterns, each field swept through all 256 values. Run under
// UBSan by the integrator's sanitizer build as well.
TEST(Decode, EveryByteOfEveryBodyFieldExpressesAndDescribes) {
  CaptureLink link;
  Reply reply(link, 0);
  Describe describe{reply};
  size_t genomes = 0;
  for (const GeneTypeInfo& info : GENE_TYPES) {
    for (uint8_t base : {uint8_t(0), uint8_t(128), uint8_t(255)}) {
      for (uint8_t at = 0; at < info.bodyLen; ++at) {
        for (int value = 0; value < 256; ++value) {
          std::vector<uint8_t> body(info.bodyLen, base);
          body[at] = uint8_t(value);
          Genome g = oneGene(info.type, body);
          Phenotype p = expressed(g);
          info.describe(g.gene(0), describe);
          ++genomes;
          for (const Emitter& e : p.chem.emitters) {
            EXPECT_LE(e.period, 31);
            EXPECT_TRUE(e.gain >= Fx::zero() && e.gain <= Fx::one());
          }
          for (const Reaction& r : p.chem.reactions) EXPECT_TRUE(r.rate >= Fx::zero() && r.rate.raw <= Fx::ratio(1, 10).raw + 1);
          for (const auto& x : p.palette) EXPECT_LT(x.region.v, kRegionCount);
          for (const Instinct& in : p.instincts) EXPECT_LE(in.cueCount, 3);
          EXPECT_LE(p.habitat.pantrySize, Habitat::kMaxPellets);
        }
      }
    }
  }
  EXPECT_GT(genomes, 60000u);
  for (const std::string& line : link.lines) {
    ASSERT_LE(line.size(), 200u);
    ASSERT_EQ(line.rfind("#0 + ", 0), 0u) << line;
  }
}

TEST(Decode, AKnownKindWithTheWrongLengthOrAnUnknownKindExpressesNothing) {
  Phenotype p = expressed(oneGene(0x21, {200, 1}));
  EXPECT_EQ(p.size.base, 0);
  Phenotype u = expressed(oneGene(0x55, {1, 2, 3}));
  EXPECT_EQ(fingerprint(u), fingerprint(expressed(Genome{})));
}

TEST(Describe, NamesIdsAndFramesEachField) {
  CaptureLink link;
  Reply reply(link, 0x1b);
  Describe describe{reply};
  Genome g = oneGene(0x20, {2, 159, 128, 128, 24, 255});
  geneType(0x20)->describe(g.gene(0), describe);
  ASSERT_EQ(link.lines.size(), 6u);
  EXPECT_EQ(link.lines[0], "#1b + region=cloak");
  EXPECT_EQ(link.lines[1], "#1b + hue=159");
  EXPECT_EQ(link.lines[4], "#1b + chemBound=vision");
  NoteGene note{};
  note.text[0] = 'h';
  note.text[1] = 1;
  note.text[2] = 'i';
  GenomeBuilder b;
  b.append(note, 0);
  geneType(0x7F)->describe(b.build()->gene(0), describe);
  EXPECT_EQ(link.lines.back(), "#1b + text=h?i");
}

TEST(ExpressStage, TwiceIsANoOp) {
  Genome starter = starterGenome(7);
  Phenotype p{};
  expressStage(starter, Stage::Baby, 0, p);
  std::vector<int64_t> once = fingerprint(p);
  expressStage(starter, Stage::Baby, 0, p);
  EXPECT_EQ(fingerprint(p), once);
  expressStage(starter, Stage::Child, 0, p);
  std::vector<int64_t> child = fingerprint(p);
  EXPECT_NE(child, once);
  expressStage(starter, Stage::Child, 0, p);
  expressStage(starter, Stage::Baby, 0, p);
  EXPECT_EQ(fingerprint(p), child);
}

TEST(ExpressStage, FeatGatedGenesWaitForTheFeatAndDormantGenesNeverExpress) {
  Genome starter = starterGenome(7);
  auto marksAtAdult = [&](uint32_t feats) { return expressed(starter, Stage::Adult, feats).marks.size(); };
  auto instinctsAtAdult = [&](uint32_t feats) { return expressed(starter, Stage::Adult, feats).instincts.size(); };
  EXPECT_EQ(marksAtAdult(0), 0u);
  EXPECT_EQ(marksAtAdult(feat::reached_adult), 0u);
  EXPECT_EQ(marksAtAdult(feat::reached_elder), 1u);
  EXPECT_EQ(instinctsAtAdult(feat::dreamer), instinctsAtAdult(0) + 1);

  Phenotype all = expressed(starter, Stage::Baby, 0xFFFFFFFFu);
  size_t dormantPalettes = 0, shakeGenes = 0, expressedShakes = 0;
  starter.forEach([&](const GeneView& v) {
    if (v.header.type == 0x20 && (v.header.flags & GeneFlags::Dormant)) ++dormantPalettes;
    if (v.header.type == 0x10 && v.body[0] == stim::shake.v) ++shakeGenes;
  });
  for (const auto& s : all.stimuli) expressedShakes += s.stim == stim::shake;
  EXPECT_EQ(dormantPalettes, 2u);
  EXPECT_EQ(all.palette.size(), 5u);
  EXPECT_EQ(shakeGenes, 2u);
  EXPECT_EQ(expressedShakes, 1u);
}

TEST(Starter, CarriesEveryGeneKindAndExpressesEachAtSomeStage) {
  Genome starter = starterGenome(7);
  std::set<uint8_t> kinds;
  starter.forEach([&](const GeneView& v) {
    ASSERT_NE(geneType(v.header.type), nullptr);
    ASSERT_EQ(v.header.len, geneType(v.header.type)->bodyLen);
    kinds.insert(v.header.type);
  });
  for (const GeneTypeInfo& info : GENE_TYPES) EXPECT_EQ(kinds.count(info.type), 1u) << info.name;
  EXPECT_LE(starter.bytes().size(), kMaxGenomeBytes);
  Phenotype p{};
  for (uint8_t s = 0; s < kStageCount; ++s) expressStage(starter, Stage(s), 0, p);
  EXPECT_EQ(p.expressedStages, 0x0F);
  EXPECT_EQ(p.egg.incubateTicks, 30 * kTicksPerMinute);
  EXPECT_EQ(p.faces.size(), 10u);
  EXPECT_EQ(p.size.base, 136);
}

TEST(Starter, SpeciesSeedNudgesOnlyLookBytes) {
  Genome base = starterGenome(0);
  EXPECT_EQ(starterGenome(0).bytes(), base.bytes());
  bool anyDiffers = false;
  for (uint32_t seed = 1; seed < 64; ++seed) {
    Genome other = starterGenome(seed);
    ASSERT_EQ(other.geneCount(), base.geneCount());
    for (uint16_t i = 0; i < base.geneCount(); ++i) {
      GeneView a = base.gene(i), b = other.gene(i);
      ASSERT_EQ(a.header.uid, b.header.uid);
      ASSERT_EQ(a.header.type, b.header.type);
      if (std::equal(a.body, a.body + a.header.len, b.body)) continue;
      anyDiffers = true;
      EXPECT_EQ(geneType(a.header.type)->cls, GeneClass::Look) << "seed " << seed << " gene " << i;
      for (uint8_t k = 0; k < a.header.len; ++k) EXPECT_LE(std::abs(a.body[k] - b.body[k]), 20);
    }
  }
  EXPECT_TRUE(anyDiffers);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
