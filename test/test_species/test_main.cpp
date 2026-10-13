#include <gtest/gtest.h>
#include <algorithm>
#include <optional>
#include <set>
#include "blorb/creature.h"
#include "blorb/genes.h"
#include "blorb/keepsake.h"
#include "blorb/mutate.h"
#include "blorb/species.h"
#include "blorb/stakes.h"
#include "phenotype_print.h"

using namespace blorb;

namespace {

// starterGenome(15048) is byte for byte the genome of the gift device's
// grungo (HANDOFF.md): the device's seed is fnv1a of its MAC, and this seed
// rolls the same four Look nudges. The two prints were taken on the engine
// before species genes existed (battles-d2 0a64811).
constexpr uint32_t kGiftSeed = 15048;
constexpr uint32_t kGiftHash = 0xd30f00d1u;
constexpr uint32_t kGiftLifePrint = 0x69bc9280u, kGiftLifePrintAllFeats = 0xd06bd969u;

Phenotype lifeOf(const Genome& g, uint32_t feats = 0) {
  Phenotype p{};
  for (uint8_t s = 0; s < kStageCount; ++s) expressStage(g, Stage(s), feats, p);
  return p;
}

std::optional<GeneView> firstOfKind(const Genome& g, uint8_t type) {
  std::optional<GeneView> found;
  g.forEach([&](const GeneView& v) { if (!found && v.header.type == type) found = v; });
  return found;
}

size_t countOfKind(const Genome& g, uint8_t type) {
  size_t n = 0;
  g.forEach([&](const GeneView& v) { n += v.header.type == type; });
  return n;
}

Genome without(const Genome& g, uint16_t fromUid) {
  GenomeBuilder b;
  g.forEach([&](const GeneView& v) { if (v.header.uid.v < fromUid) b.append(v.header, v.body); });
  return *b.build();
}

template <class Body>
Genome plus(const Genome& g, const Body& body) {
  return *GenomeBuilder::from(g).append(body, GeneFlags::Mutable).build();
}

constexpr uint8_t kSpecies = GeneKindOf<SpeciesGene>::value, kPower = GeneKindOf<PowerGene>::value,
                  kKnack = GeneKindOf<KnackGene>::value;

}  // namespace

// ---- grungo, and every pet alive today ------------------------------------------------

TEST(Gift, TheGiftGenomeHashesAndExpressesExactlyAsBefore) {
  Genome gift = starterGenome(kGiftSeed);
  ASSERT_EQ(gift.hash(), kGiftHash);
  EXPECT_EQ(lifePrint(gift, 0), kGiftLifePrint);
  EXPECT_EQ(lifePrint(gift, 0xFFFFFFFFu), kGiftLifePrintAllFeats);
  Phenotype p = lifeOf(gift);
  EXPECT_EQ(p.species.body, kGrungo);
  EXPECT_EQ(p.species.power, power::foresee);
  EXPECT_FALSE(p.species.hybrid());
  EXPECT_EQ(kKnackOrders[p.species.knackOrder][uint8_t(Stance::Trick)], 2) << "grungo loves a trick";
  EXPECT_EQ(p.oracle.voice, voice::mystic);
}

TEST(Gift, NoSpeciesGeneIsAPureBredGrungoWhoForesees) {
  Phenotype empty = lifeOf(Genome{});
  EXPECT_EQ(empty.species.body, kGrungo);
  EXPECT_EQ(empty.species.power, power::foresee);
  for (uint32_t seed : {0u, 7u, 99u, kGiftSeed}) {
    Genome g = starterGenome(seed);
    EXPECT_EQ(countOfKind(g, kSpecies) + countOfKind(g, kPower) + countOfKind(g, kKnack), 0u);
    EXPECT_EQ(lifeOf(g).species.body, kGrungo);
  }
}

TEST(Gift, GrungosStarterIsTheOldStarterPlusOnlyTheSpeciesGenes) {
  for (uint32_t seed : {0u, 7u, kGiftSeed}) {
    Genome of = starterGenomeOf(kGrungo, seed);
    EXPECT_EQ(without(of, kSpeciesGeneUid.v).bytes(), starterGenome(seed).bytes()) << "seed " << seed;
  }
}

// ---- the nine ----------------------------------------------------------------------------

TEST(Starters, NineAreViableAndEachExpressesItsOwnSpeciesPowerAndKnack) {
  std::set<uint32_t> hashes;
  for (const SpeciesInfo& row : SPECIES) {
    Genome g = starterGenomeOf(row.id, 7);
    hashes.insert(g.hash());
    Viability v = viability(g);
    EXPECT_TRUE(v.ok) << row.name << ": " << (v.reason ? v.reason : "");
    Phenotype p = lifeOf(g);
    EXPECT_EQ(p.species.body, row.id) << row.name;
    EXPECT_EQ(p.species.power, row.power) << row.name;
    EXPECT_FALSE(p.species.hybrid()) << row.name;
    EXPECT_EQ(kKnackOrders[p.species.knackOrder][uint8_t(row.style)], 2) << row.name;
    EXPECT_EQ(p.oracle.voice, row.starter.voice) << row.name;
    EXPECT_LE(g.bytes().size(), kMaxGenomeBytes);
  }
  EXPECT_EQ(hashes.size(), kSpeciesCount);
}

TEST(Starters, NoSpeciesButGrungoExpressesGrungo) {
  for (const SpeciesInfo& row : SPECIES) {
    if (row.id == kGrungo) continue;
    EXPECT_NE(lifeOf(starterGenomeOf(row.id, 7)).species.body, kGrungo) << row.name;
  }
}

TEST(Starters, SpeciesGoesFirstAndPowerLastSoACrossCanSplitThem) {
  for (const SpeciesInfo& row : SPECIES) {
    Genome g = starterGenomeOf(row.id, 3);
    EXPECT_EQ(g.gene(0).header.uid, kSpeciesGeneUid) << row.name;
    EXPECT_EQ(g.gene(uint16_t(g.geneCount() - 1)).header.uid, kPowerGeneUid) << row.name;
  }
}

TEST(Starters, TheRowsBytesReachTheRecipe) {
  auto receptorGain = [](const Genome& g, LocusId locus) {
    for (const Receptor& r : lifeOf(g).chem.receptors) if (r.locus == locus) return r.gain.raw;
    return int32_t(-1);
  };
  Genome mudge = starterGenomeOf(SpeciesId{5}, 7), mossby = starterGenomeOf(SpeciesId{1}, 7),
         grungo = starterGenomeOf(kGrungo, 7);
  EXPECT_GT(receptorGain(mudge, locus::startle), receptorGain(grungo, locus::startle)) << "mudge is brash";
  EXPECT_LT(receptorGain(mossby, locus::startle), receptorGain(grungo, locus::startle)) << "mossby is timid";
  auto emitterGain = [](const Genome& g, LocusId locus, ChemId c) {
    for (const Emitter& e : lifeOf(g).chem.emitters) if (e.locus == locus && e.chem == c) return e.gain.raw;
    return int32_t(-1);
  };
  const ChemId hunger = driveChem(drive::hunger);
  EXPECT_GT(emitterGain(mudge, locus::always, hunger), emitterGain(grungo, locus::always, hunger)) << "mudge eats more";
  EXPECT_LT(emitterGain(mossby, locus::always, hunger), emitterGain(grungo, locus::always, hunger)) << "mossby less";
  EXPECT_GT(emitterGain(mudge, locus::light, chem::melatonin), emitterGain(grungo, locus::light, chem::melatonin))
      << "a night sleeper grows sleepy sooner after dark";
}

TEST(Starters, AGenomeWithoutAKnackRanksItsBodysStyle) {
  for (const SpeciesInfo& row : SPECIES) {
    Genome full = starterGenomeOf(row.id, 7);
    GenomeBuilder b = GenomeBuilder::from(full);
    b.erase(kKnackGeneUid);
    EXPECT_EQ(lifeOf(*b.build()).species.knackOrder, lifeOf(full).species.knackOrder) << row.name;
  }
}

// ---- expression: the first of each kind wins ---------------------------------------------

TEST(Express, FirstSpeciesWinsAndAMissingPowerIsTheBodysOwn) {
  Genome g = plus(plus(starterGenome(7), SpeciesGene{6, 0}), SpeciesGene{3, 0});
  Phenotype p = lifeOf(g);
  EXPECT_EQ(p.species.body, SpeciesId{6});
  EXPECT_EQ(p.species.power, power::zap) << "fizz with no power gene zaps";
  EXPECT_FALSE(p.species.hybrid());
}

TEST(Express, ABodyWithAnotherPowerIsAHybrid) {
  Phenotype p = lifeOf(plus(plus(starterGenome(7), PowerGene{power::zap.v, 0}), SpeciesGene{0, 0}));
  EXPECT_EQ(p.species.body, kGrungo);
  EXPECT_EQ(p.species.power, power::zap);
  EXPECT_TRUE(p.species.hybrid()) << "a grungo who zaps";
}

TEST(Express, AnUnnamedByteIsGrungoAndADormantGeneIsNothing) {
  EXPECT_EQ(lifeOf(plus(starterGenome(7), SpeciesGene{200, 0})).species.body, kGrungo);
  EXPECT_EQ(lifeOf(plus(starterGenome(7), PowerGene{200, 0})).species.power, power::foresee);
  Genome sleepy = *GenomeBuilder::from(starterGenome(7)).append(SpeciesGene{6, 0}, GeneFlags::Dormant).build();
  EXPECT_EQ(lifeOf(sleepy).species.body, kGrungo);
}

TEST(Express, WarPaintWaitsForTheSlayerFeat) {
  Genome g = starterGenomeOf(SpeciesId{6}, 7);
  const size_t plain = lifeOf(g, 0xFFFFFFFFu & ~feat::slayer).marks.size();
  EXPECT_EQ(lifeOf(g, 0xFFFFFFFFu).marks.size(), plain + 1);
  EXPECT_EQ(lifeOf(g, feat::slayer).marks.size(), lifeOf(g, 0).marks.size() + 1);
}

// ---- mutation never moves a species or a power ----------------------------------------------

TEST(Mutation, TenThousandMutationsNeverMoveASpeciesOrPowerByte) {
  Rng rng = Rng::seeded(2026);
  Genome line[kSpeciesCount] = {starterGenomeOf(SpeciesId{0}, 1), starterGenomeOf(SpeciesId{1}, 1),
                                starterGenomeOf(SpeciesId{2}, 1), starterGenomeOf(SpeciesId{3}, 1),
                                starterGenomeOf(SpeciesId{4}, 1), starterGenomeOf(SpeciesId{5}, 1),
                                starterGenomeOf(SpeciesId{6}, 1), starterGenomeOf(SpeciesId{7}, 1),
                                starterGenomeOf(SpeciesId{8}, 1)};
  // Every other round a harsh policy, so dup, del, sleep and wild rerolls are all tried on every gene.
  const MutationPolicy harsh{Fx::ratio(1, 2), Fx::ratio(1, 4), Fx::ratio(1, 4), Fx::one(), Fx::ratio(1, 4),
                             Fx::one(), 0, Fx::one(), 8, 0};
  size_t changed = 0;
  for (int i = 0; i < 10000; ++i) {
    const size_t s = size_t(i) % kSpeciesCount;
    const Genome& parent = line[s];
    Offspring child = mutate(parent, i % 2 ? harsh : policyOf(parent, Fx::one()), {}, rng);
    changed += child.genome.bytes() != parent.bytes();
    for (GeneUid uid : {kSpeciesGeneUid, kPowerGeneUid}) {
      std::optional<GeneView> was = parent.find(uid), now = child.genome.find(uid);
      ASSERT_TRUE(now) << "round " << i << " lost uid " << uid.v;
      ASSERT_EQ(now->header.flags, was->header.flags) << "round " << i;
      ASSERT_TRUE(std::equal(was->body, was->body + was->header.len, now->body)) << "round " << i;
    }
    ASSERT_EQ(countOfKind(child.genome, kSpecies), 1u) << "round " << i;
    ASSERT_EQ(countOfKind(child.genome, kPower), 1u) << "round " << i;
    const Phenotype p = lifeOf(child.genome);
    ASSERT_EQ(p.species.body, SPECIES[s].id) << "round " << i;
    ASSERT_EQ(p.species.power, SPECIES[s].power) << "round " << i;
    line[s] = child.genome;
  }
  EXPECT_GT(changed, 9000u) << "the mutations were real";
}

// ---- fixed uids: the cross matches genes by uid ------------------------------------------------

TEST(Uids, StartersWithAndWithoutTheStarvationBlockShareEveryRecipeUid) {
  for (const SpeciesInfo& row : SPECIES) {
    GenomeBuilder with, lean;
    authorStarter(with, row.starter, 7, true);
    authorStarter(lean, row.starter, 7, false);
    Genome a = *with.build(), b = *lean.build();
    ASSERT_EQ(a.geneCount(), b.geneCount() + 4) << row.name;
    b.forEach([&](const GeneView& v) {
      std::optional<GeneView> same = a.find(v.header.uid);
      ASSERT_TRUE(same) << row.name << " uid " << v.header.uid.v;
      EXPECT_EQ(same->header.type, v.header.type) << row.name << " uid " << v.header.uid.v;
      EXPECT_TRUE(std::equal(v.body, v.body + v.header.len, same->body)) << row.name << " uid " << v.header.uid.v;
    });
    EXPECT_LT(a.nextUid().v, kSpeciesGeneUid.v) << "the recipe stays clear of the species uids";
  }
}

TEST(Uids, EveryStarterHasTheSameRecipeUidsAndKinds) {
  Genome grungo = starterGenomeOf(kGrungo, 7);
  for (const SpeciesInfo& row : SPECIES) {
    Genome g = starterGenomeOf(row.id, 7);
    ASSERT_EQ(g.geneCount(), grungo.geneCount()) << row.name;
    for (uint16_t i = 0; i < g.geneCount(); ++i) {
      EXPECT_EQ(g.gene(i).header.uid, grungo.gene(i).header.uid) << row.name << " gene " << i;
      EXPECT_EQ(g.gene(i).header.type, grungo.gene(i).header.type) << row.name << " gene " << i;
    }
  }
}

// ---- the die-locus and swoon guards, on every expression path --------------------------------

namespace {

ReceptorGene toDie(ChemId reads) { return ReceptorGene{reads.v, locus::die.v, 0, 255, 128, 1}; }

// Each would kill at once, or tie a swoon to something other than sleep, if it expressed.
Genome planted() {
  GenomeBuilder b = GenomeBuilder::from(starterGenome(7));
  for (ChemId c : {chem::daze, chem::pluck, driveChem(drive::pain), chem::adrenaline, chem::mettle, chem::dread})
    b.append(toDie(c), GeneFlags::Mutable);
  b.append(ReceptorGene{driveChem(drive::pain).v, locus::cause.v, 0, 255, 128, 1}, GeneFlags::Mutable);
  b.append(ReceptorGene{chem::daze.v, locus::startle.v, 0, 255, 128, 1}, GeneFlags::Mutable);
  b.append(ReactionGene{chem::pluck.v, 1, 0, 0, chem::injury.v, 1, 0, 0, 1}, GeneFlags::Mutable);
  b.append(ReactionGene{chem::energy.v, 1, chem::daze.v, 1, chem::toxin.v, 1, 0, 0, 1}, GeneFlags::Mutable);
  return *b.build();
}

Creature hatch(const Genome& g) { return Creature(Egg(Offspring{g, {}}, 0, 0), 0, 0); }

}  // namespace

TEST(Guard, PlantedDieAndSwoonGenesNeverExpress) {
  EXPECT_EQ(lifePrint(planted(), 0), lifePrint(starterGenome(7), 0));
  for (const Receptor& r : lifeOf(planted()).chem.receptors)
    EXPECT_TRUE(receptorExpresses(r.chem, r.locus)) << "chem " << int(r.chem.v) << " -> locus " << int(r.locus.v);
}

TEST(Guard, ASwoonMayStillHoldHimAsleepAndInjuryStillKills) {
  Genome g = plus(starterGenome(7), ReceptorGene{chem::daze.v, locus::sleep_gate.v, 0, 255, 128, 1});
  EXPECT_EQ(lifeOf(g).chem.receptors.size(), lifeOf(starterGenome(7)).chem.receptors.size() + 1);
  size_t dieReaders = 0;
  for (const Receptor& r : lifeOf(starterGenome(7)).chem.receptors) dieReaders += r.locus == locus::die;
  EXPECT_EQ(dieReaders, 3u) << "life, starvation and injury";
}

TEST(Guard, AHatchedCreatureIgnoresThem) {
  EXPECT_EQ(phenotypePrint(hatch(planted()).phenotype()), phenotypePrint(hatch(starterGenome(7)).phenotype()));
}

TEST(Guard, AKeepsakeLoadIgnoresThem) {
  auto reload = [](const Genome& g) {
    Snapshot s{0, PetClock{}, hatch(g), Habitat{}, Settings{}, Rng::seeded(1), std::nullopt, {}};
    std::vector<uint8_t> bytes = Keepsake::encode(s);
    Snapshot back{};
    EXPECT_TRUE(Keepsake::decode(bytes.data(), bytes.size(), back));
    return phenotypePrint(std::get<Creature>(back.occupant).phenotype());
  };
  EXPECT_EQ(reload(planted()), reload(starterGenome(7)));
}

TEST(Guard, ViabilityIgnoresThemSoTheyNeverKillAnEmbryo) {
  EXPECT_TRUE(viability(planted()).ok);
  EXPECT_FALSE(viability(plus(starterGenome(7), toDie(chem::life))).ok) << "a vital reader still kills";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
