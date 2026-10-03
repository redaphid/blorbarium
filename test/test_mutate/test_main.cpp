#include <gtest/gtest.h>
#include <cstdlib>
#include <type_traits>
#include <vector>
#include "blorb/genes.h"
#include "blorb/mutate.h"

using namespace blorb;

namespace {

const Genome& parent() {
  static const Genome g = starterGenome(7);
  return g;
}

GeneClass classOf(const Genome& parent, const Genome& child, GeneUid uid) {
  std::optional<GeneView> v = child.find(uid);
  if (!v) v = parent.find(uid);
  return v && geneType(v->header.type) ? geneType(v->header.type)->cls : GeneClass::Body;
}

GeneUid uidOf(const MutationOp& op) {
  return std::visit([](const auto& o) {
    if constexpr (std::is_same_v<std::decay_t<decltype(o)>, MutHeirloom>) return o.after;
    else return o.gene;
  }, op);
}

int lookChanges(const Genome& parent, const Offspring& o, uint8_t minDelta) {
  int n = 0;
  for (const MutationOp& op : o.diff.ops) {
    if (classOf(parent, o.genome, uidOf(op)) != GeneClass::Look) continue;
    auto* p = std::get_if<MutPoint>(&op);
    n += std::holds_alternative<MutWake>(op) || (p && std::abs(int(p->to) - int(p->from)) >= minDelta);
  }
  return n;
}

int mindChanges(const Genome& parent, const Offspring& o) {
  int n = 0;
  for (const MutationOp& op : o.diff.ops)
    n += std::holds_alternative<MutHeirloom>(op) || classOf(parent, o.genome, uidOf(op)) == GeneClass::Mind;
  return n;
}

GeneUid lifeGene(const Genome& g) {
  GeneUid uid{};
  g.forEach([&](const GeneView& v) {
    if (v.header.type == GeneKindOf<ChemGene>::value && v.body[0] == chem::life.v) uid = v.header.uid;
  });
  return uid;
}

Genome withByte(const Genome& g, GeneUid uid, uint8_t offset, uint8_t value) {
  return *GenomeBuilder::from(g).setByte(uid, offset, value).build();
}

}  // namespace

TEST(Viability, TheStarterIsViable) {
  Viability v = viability(parent());
  EXPECT_TRUE(v.ok) << v.reason;
}

TEST(Viability, RejectsAPlantedLethalLife) {
  Viability v = viability(withByte(parent(), lifeGene(parent()), 1, 1));
  EXPECT_FALSE(v.ok);
  EXPECT_STREQ(v.reason, "dies in the dry run");
}

TEST(Viability, RejectsAMissingShape) {
  EXPECT_FALSE(viability(withByte(parent(), lifeGene(parent()), 2, 0)).ok) << "life never seeded";
  GenomeBuilder noFood = GenomeBuilder::from(parent());
  parent().forEach([&](const GeneView& v) {
    if (v.header.type == GeneKindOf<ReactionGene>::value && v.body[0] == chem::food.v) noFood.erase(v.header.uid);
  });
  EXPECT_FALSE(viability(*noFood.build()).ok) << "no food -> energy";
}

TEST(Heredity, TenThousandSeedsReplayRepeatDifferAndLive) {
  const MutationPolicy pol = policyOf(parent(), Fx::zero());
  for (uint64_t seed = 0; seed < 10000; ++seed) {
    Rng rng = Rng::seeded(seed), again = Rng::seeded(seed);
    Offspring o = mutate(parent(), pol, {}, rng);
    std::optional<Genome> replayed = apply(parent(), o.diff);
    ASSERT_TRUE(replayed.has_value()) << "seed " << seed;
    ASSERT_EQ(replayed->bytes(), o.genome.bytes()) << "seed " << seed;
    ASSERT_EQ(mutate(parent(), pol, {}, again).genome.bytes(), o.genome.bytes()) << "seed " << seed;
    ASSERT_GE(lookChanges(parent(), o, pol.minVisibleDelta), 1) << "seed " << seed;
    ASSERT_GE(mindChanges(parent(), o), 1) << "seed " << seed;
    Viability v = viability(o.genome);
    ASSERT_TRUE(v.ok) << "seed " << seed << ": " << v.reason;
  }
}

// Every byte rerolled on every gene: most rolls are lethal, so mutate() has
// to retry, and some seeds reach the point-only fallback. All stay viable.
TEST(Heredity, RetriesStillReturnAViableChild) {
  MutationPolicy wild = policyOf(parent(), Fx::one());
  wild.pointPerGene = Fx::one();
  int fallbacks = 0;
  for (uint64_t seed = 0; seed < 200; ++seed) {
    Rng rng = Rng::seeded(seed);
    Offspring o = mutate(parent(), wild, {}, rng);
    Viability v = viability(o.genome);
    ASSERT_TRUE(v.ok) << "seed " << seed << ": " << v.reason;
    ASSERT_GE(lookChanges(parent(), o, wild.minVisibleDelta), 1);
    ASSERT_GE(mindChanges(parent(), o), 1);
    fallbacks += o.diff.ops.size() == 2;
  }
  EXPECT_GT(fallbacks, 0) << "the wild policy never exhausted its retries";
}

TEST(Heredity, ApplyRefusesAMissingGeneOrAStaleByte) {
  MutationDiff missing{{MutDel{GeneUid{60000}}}};
  EXPECT_FALSE(apply(parent(), missing).has_value());
  GeneUid life = lifeGene(parent());
  uint8_t half = parent().find(life)->body[1];
  MutationDiff stale{{MutPoint{life, 1, uint8_t(half + 1), 9, false}}};
  EXPECT_FALSE(apply(parent(), stale).has_value());
  MutationDiff twice{{MutDel{life}, MutDel{life}}};
  EXPECT_FALSE(apply(parent(), twice).has_value()) << "the second op names a gene the first removed";
}

TEST(Heredity, HeirloomsBecomeInstinctGenesAndUpdateNextTime) {
  const MutationPolicy pol = policyOf(parent(), Fx::zero());
  std::vector<Belief> learned = {{locus::cradled, action::rest, drive::fear, -Fx::ratio(1, 2), Fx::one()},
                                 {locus::held, action::curl, drive::fear, -Fx::ratio(1, 50), Fx::ratio(1, 10)}};
  Rng rng = Rng::seeded(11);
  Offspring child = mutate(parent(), pol, learned, rng);
  int heirlooms = 0;
  child.genome.forEach([&](const GeneView& v) {
    if (!(v.header.flags & GeneFlags::Heirloom)) return;
    ++heirlooms;
    EXPECT_EQ(v.body[0], locus::cradled.v);
    EXPECT_EQ(v.body[3], action::rest.v);
    EXPECT_EQ(v.body[5], 64) << "level byte encodes effect -0.5";
  });
  EXPECT_EQ(heirlooms, 1) << "the unconfident belief stays behind";
  EXPECT_EQ(apply(parent(), child.diff)->bytes(), child.genome.bytes());

  // No random ops, so the only Mind change is the update to the inherited gene.
  MutationPolicy quiet = pol;
  quiet.pointPerGene = quiet.dupPerGene = quiet.delPerGene = quiet.wakePerDormant = quiet.sleepPerGene = Fx::zero();
  learned[0].effect = -Fx::ratio(1, 4);
  Rng rng2 = Rng::seeded(12);
  Offspring grandchild = mutate(child.genome, quiet, learned, rng2);
  int again = 0;
  grandchild.genome.forEach([&](const GeneView& v) {
    if (!(v.header.flags & GeneFlags::Heirloom)) return;
    ++again;
    EXPECT_EQ(v.body[4], drive::fear.v);
    EXPECT_EQ(v.body[5], 96) << "the inherited instinct moved to the new belief";
  });
  EXPECT_EQ(again, 1) << "updated in place, not added again";
  EXPECT_GE(mindChanges(child.genome, grandchild), 1);
}

// A uid names one gene for the whole lineage: a gene new in a child (a
// duplicate's copy, an heirloom) never takes a uid any ancestor has used.
TEST(Heredity, ANewGeneNeverReusesAnAncestorsUid) {
  constexpr uint64_t kChains = 120;
  constexpr int kGenerations = 25;
  int fresh = 0;
  for (uint64_t seed = 0; seed < kChains; ++seed) {
    Genome g = parent();
    std::vector<bool> used(65536, false);
    g.forEach([&](const GeneView& v) { used[v.header.uid.v] = true; });
    Rng rng = Rng::seeded(seed);
    for (int gen = 0; gen < kGenerations; ++gen) {
      MutationPolicy pol = policyOf(g, Fx::zero());
      pol.dupPerGene = Fx::ratio(1, 20);
      pol.delPerGene = Fx::ratio(1, 10);
      std::vector<Belief> heirlooms = {{locus::held, action::rest, drive::fear, -Fx::ratio(2, 10), Fx::one()}};
      Offspring o = mutate(g, pol, gen % 3 == 0 ? heirlooms : std::vector<Belief>{}, rng);
      for (const MutationOp& op : o.diff.ops) {
        std::optional<GeneUid> born;
        if (auto* d = std::get_if<MutDup>(&op)) born = d->copy;
        if (auto* h = std::get_if<MutHeirloom>(&op)) born = GeneUid{uint16_t(h->gene[6] | h->gene[7] << 8)};
        if (!born) continue;
        ++fresh;
        ASSERT_FALSE(used[born->v]) << "uid " << born->v << " reused, chain " << seed << " generation " << gen + 1;
      }
      g = o.genome;
      g.forEach([&](const GeneView& v) { used[v.header.uid.v] = true; });
    }
  }
  EXPECT_GT(fresh, 500) << "the test must see many new genes";
}

TEST(Policy, ReadsTheGenomeAndTheFeatBonus) {
  MutationPolicy plain = policyOf(parent(), Fx::zero()), lucky = policyOf(parent(), Fx::ratio(1, 10));
  EXPECT_EQ(plain.heirloomMax, 3);
  EXPECT_EQ(lucky.wild, plain.wild + Fx::ratio(1, 10));
  EXPECT_GT(plain.pointPerGene, plain.dupPerGene);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
