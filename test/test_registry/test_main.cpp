#include <gtest/gtest.h>
#include <set>
#include "blorb/registry.h"

using namespace blorb;

// Written out by hand from loci.def and stimuli.def, so a change to how the
// feature vector is derived (order, filter, recent mapping) shows up here.
TEST(Features, SituationLociInTableOrderThenRecentLociOfSituationStimuli) {
  const uint8_t expected[] = {
      3, 4, 5, 6, 7, 8, 9, 11, 13, 14, 16, 17,          // motion .. cradled
      64 + 0, 64 + 2, 64 + 3, 64 + 10, 64 + 17, 64 + 19, 64 + 22, 64 + 48,   // knock .. petted
  };
  ASSERT_EQ(kFeatureCount, sizeof(expected));
  for (size_t i = 0; i < kFeatureCount; ++i) EXPECT_EQ(FEATURES[i].v, expected[i]) << "feature " << i;
}

TEST(Features, EveryFeatureIsDistinct) {
  std::set<uint8_t> seen;
  for (LocusId f : FEATURES) EXPECT_TRUE(seen.insert(f.v).second) << int(f.v);
}

TEST(RecentLoci, EachStimulusOwnsSixtyFourPlusItsId) {
  for (const StimInfo& s : STIMULI) {
    LocusId r = locus::recent(s.id);
    EXPECT_EQ(r.v, 64 + s.id.v) << s.name;
    EXPECT_GE(r.v, 64);
    EXPECT_LT(r.v, 128) << s.name;
  }
  EXPECT_EQ(locus::recent(stim::shake).v, 66);
  EXPECT_EQ(locus::recent(stim::petted).v, 112);
}

TEST(RecentLoci, NoNamedLocusSitsInTheRecentBand) {
  for (const LocusInfo& l : LOCI) EXPECT_TRUE(l.id.v < 64 || l.id.v >= 128) << l.name;
}

TEST(Loci, DirectionMatchesTheIdBand) {
  for (const LocusInfo& l : LOCI) {
    if (l.dir == LocusDir::Sense) EXPECT_LT(l.id.v, 64) << l.name;
    if (l.dir == LocusDir::Act) EXPECT_TRUE(l.id.v >= 128 && l.id.v < 192) << l.name;
  }
}

TEST(Tables, EachDerivedCountMatchesItsRows) {
  EXPECT_EQ(kDriveCount, 8u);
  EXPECT_EQ(kActionCount, 11u);
  EXPECT_EQ(kRegionCount, 7u);
  EXPECT_EQ(kReflexCount, 2u);
  EXPECT_EQ(kFeatureCount, 20u);
  EXPECT_EQ(countOf(CHEMICALS), 9u);
  EXPECT_EQ(countOf(LOCI), 34u);
  EXPECT_EQ(countOf(STIMULI), 29u);
  EXPECT_EQ(countOf(POSES), 10u);
  EXPECT_EQ(countOf(EXPRESSIONS), 10u);
  EXPECT_EQ(countOf(CARES), 5u);
  EXPECT_EQ(countOf(FEATS), 6u);
}

// The brain, the face and the pack index these tables by id, so their ids
// must be dense and in row order.
template <class T, size_t N>
void expectIdIsRowIndex(const T (&rows)[N], const char* table) {
  for (size_t i = 0; i < N; ++i) EXPECT_EQ(size_t(rows[i].id.v), i) << table << " row " << i;
}

TEST(Tables, IndexedTablesHaveDenseIds) {
  expectIdIsRowIndex(DRIVES, "drives");
  expectIdIsRowIndex(ACTIONS, "actions");
  expectIdIsRowIndex(POSES, "poses");
  expectIdIsRowIndex(EXPRESSIONS, "expressions");
  expectIdIsRowIndex(REGIONS, "regions");
  expectIdIsRowIndex(REFLEXES, "reflexes");
  expectIdIsRowIndex(CARES, "cares");
  for (size_t i = 0; i < countOf(FEATS); ++i) EXPECT_EQ(FEATS[i].bit, i);
}

TEST(Tables, NamedChemicalsStayClearOfTheDriveSlots) {
  for (const ChemInfo& c : CHEMICALS) EXPECT_GT(c.id.v, kDriveBase + kDriveCount - 1) << c.name;
  EXPECT_EQ(chem::life.v, 16);
  for (const DriveInfo& d : DRIVES) EXPECT_EQ(driveChem(d.id).v, 1 + d.id.v);
}

TEST(Tables, CrossReferencesNameRealRows) {
  for (const ReflexInfo& r : REFLEXES) {
    bool act = false;
    for (const LocusInfo& l : LOCI) act |= l.id == r.trigger && l.dir == LocusDir::Act;
    EXPECT_TRUE(act) << r.name;
  }
  EXPECT_EQ(REFLEXES[reflex::hop.v].trigger, locus::startle);
  for (const CareInfo& c : CARES) EXPECT_LT(c.drive.v, kDriveCount) << c.name;
  for (const ActionInfo& a : ACTIONS) {
    if (a.selfStim == stim::none) continue;
    bool self = false;
    for (const StimInfo& s : STIMULI) self |= s.id == a.selfStim && s.source == StimSource::Self;
    EXPECT_TRUE(self) << a.name;
  }
}

TEST(Ids, UniquenessCheckCatchesADuplicate) {
  const DriveInfo dup[] = {{DriveId{0}, "a"}, {DriveId{1}, "b"}, {DriveId{0}, "c"}};
  EXPECT_FALSE(idsUnique(dup));
  EXPECT_TRUE(idsUnique(DRIVES));
  EXPECT_TRUE(idsUnique(LOCI));
  EXPECT_TRUE(idsUnique(STIMULI));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
