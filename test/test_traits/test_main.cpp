// What his genes make him (traits.h, #60): each trait reads the gene its name
// says, measured against the starter recipe, so the activity chooser can lean
// on it and a kid can see it.
#include <gtest/gtest.h>
#include <optional>
#include "blorb/creature.h"
#include "blorb/founding.h"
#include "blorb/genes.h"
#include "blorb/species.h"
#include "blorb/traits.h"
#include "paint/sprite_pack.h"   // named here so PlatformIO's dependency finder links lib/paint

using namespace blorb;

namespace {

Traits babyTraits(const Genome& g) {
  Phenotype p{};
  expressStage(g, Stage::Baby, 0, p);
  return p.traits;
}

std::optional<GeneView> find(const Genome& g, uint8_t type, bool (*m)(const GeneView&)) {
  std::optional<GeneView> out;
  g.forEach([&](const GeneView& v) {
    if (!out && v.header.type == type && v.header.stage == Stage::Baby && !(v.header.flags & GeneFlags::Dormant) && m(v))
      out = v;
  });
  return out;
}

Genome set(const Genome& g, const GeneView& v, uint8_t at, uint8_t value) {
  return *GenomeBuilder::from(g).setByte(v.header.uid, at, value).build();
}

bool hungerTonic(const GeneView& v) { return v.body[0] == locus::always.v && v.body[1] == driveChem(drive::hunger).v; }
bool boredomTonic(const GeneView& v) { return v.body[0] == locus::always.v && v.body[1] == driveChem(drive::boredom).v; }

}  // namespace

TEST(Traits, GrungosRecipeIsTheNorm) {
  const Traits t = babyTraits(starterGenomeOf(kGrungo, 7));
  EXPECT_EQ(t.appetite, 0);
  EXPECT_EQ(t.sleepiness, 0);
  EXPECT_EQ(t.nightOwl, 0);
  EXPECT_EQ(t.playfulness, 0);
  EXPECT_EQ(t.affection, 0);
  EXPECT_EQ(t.nerves, 0);
  EXPECT_EQ(t.boldness, 0);
  EXPECT_EQ(t.curiosity, 0);
  EXPECT_EQ(t.cheer, 0);
}

// species.def's rows, as the traits read them: mudge (bold 230, appetite 1300,
// sleeps by night) against mossby (bold 70, appetite 800).
TEST(Traits, TheSpeciesRowsShow) {
  const Traits mudge = babyTraits(starterGenomeOf(species::mudge, 7));
  const Traits mossby = babyTraits(starterGenomeOf(species::mossby, 7));
  EXPECT_GT(mudge.boldness, 50);
  EXPECT_LT(mossby.boldness, -20);
  EXPECT_GT(mudge.appetite, 30);
  EXPECT_LT(mossby.appetite, -20);
  EXPECT_LT(mudge.nightOwl, -100) << "a night sleeper grows sleepy within a quarter hour of dark";
  EXPECT_EQ(babyTraits(starterGenomeOf(species::mothwick, 7)).nightOwl, 0) << "mothwick sleeps by day, as grungo";
}

TEST(Traits, ARateGeneTwiceAsFastIsPlusOneHundred) {
  const Genome g = starterGenomeOf(kGrungo, 7);
  const GeneView v = *find(g, GeneKindOf<EmitterGene>::value, hungerTonic);
  EXPECT_EQ(babyTraits(set(g, v, 3, uint8_t(v.body[3] - 12))).appetite, 100);
  EXPECT_EQ(babyTraits(set(g, v, 3, uint8_t(v.body[3] + 24))).appetite, -200);
  EXPECT_EQ(babyTraits(set(g, v, 3, 0)).appetite, -Traits::kMax) << "no tonic at all";
}

TEST(Traits, ADuplicatedTonicDoublesIt) {
  const Genome g = starterGenomeOf(kGrungo, 7);
  const GeneView v = *find(g, GeneKindOf<EmitterGene>::value, boredomTonic);
  GenomeBuilder b = GenomeBuilder::from(g);
  GeneHeader h = v.header;
  h.uid = g.nextUid();
  b.insertAfter(v.header.uid, h, v.body);
  EXPECT_EQ(babyTraits(*b.build()).playfulness, 100);
}

TEST(Traits, DormantAndLaterStageGenesCountOnlyWhenExpressed) {
  // The elder's temperament (explore 110) replaces the baby's (170) only once he is an elder.
  const Genome g = starterGenomeOf(kGrungo, 7);
  Phenotype p{};
  for (Stage s : {Stage::Baby, Stage::Child, Stage::Adult}) expressStage(g, s, 0, p);
  EXPECT_EQ(p.traits.curiosity, 0);
  expressStage(g, Stage::Elder, 0, p);
  EXPECT_LT(p.traits.curiosity, -50);
}

// Every founder of every species, and every egg of its first clutches, reads
// inside the bounds.
TEST(Traits, AlwaysInBounds) {
  for (const SpeciesInfo& s : SPECIES) {
    const Traits t = babyTraits(foundingGenome(s.id, 99, paint::bandsFor(s.id)));
    for (int16_t v : {t.appetite, t.sleepiness, t.nightOwl, t.playfulness, t.affection, t.nerves, t.boldness, t.curiosity,
                      t.cheer}) {
      EXPECT_GE(v, -Traits::kMax) << s.name;
      EXPECT_LE(v, Traits::kMax) << s.name;
    }
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
