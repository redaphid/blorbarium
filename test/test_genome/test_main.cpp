#include <gtest/gtest.h>
#include <vector>
#include "blorb/fixed.h"
#include "blorb/genome.h"
#include "blorb/registry.h"

using namespace blorb;

namespace {

GeneHeader header(uint8_t type, uint8_t len, uint16_t uid, Stage stage = Stage::Baby, uint8_t featGate = 0,
                  uint8_t flags = GeneFlags::Mutable) {
  return GeneHeader{type, len, flags, stage, featGate, 64, GeneUid{uid}};
}

std::vector<uint8_t> threeGenes() {
  const uint8_t a[3] = {16, 217, 255};
  const uint8_t b[6] = {0, 1, 2, 3, 4, 5};
  const uint8_t c[1] = {9};
  GenomeBuilder builder;
  builder.append(header(0x01, 3, 1), a).append(header(0x03, 6, 2), b).append(header(0x55, 1, 7), c);
  return builder.build()->bytes();
}

std::vector<uint8_t> bodyOf(const GeneView& v) { return {v.body, v.body + v.header.len}; }

}  // namespace

TEST(Parse, RoundTripsBytesHeadersAndBodies) {
  std::vector<uint8_t> bytes = threeGenes();
  ASSERT_EQ(bytes.size(), 3 * kGeneHeaderLen + 3 + 6 + 1);
  std::optional<Genome> g = Genome::parse(bytes.data(), bytes.size());
  ASSERT_TRUE(g);
  EXPECT_EQ(g->bytes(), bytes);
  ASSERT_EQ(g->geneCount(), 3u);
  GeneView second = g->gene(1);
  EXPECT_EQ(second.header.type, 0x03);
  EXPECT_EQ(second.header.uid, GeneUid{2});
  EXPECT_EQ(second.header.mutWeight, 64);
  EXPECT_EQ(second.index, 1);
  EXPECT_EQ(bodyOf(second), (std::vector<uint8_t>{0, 1, 2, 3, 4, 5}));
  EXPECT_EQ(g->find(GeneUid{7})->index, 2);
  EXPECT_FALSE(g->find(GeneUid{3}));
  EXPECT_EQ(GenomeBuilder::from(*g).build()->bytes(), bytes);
  EXPECT_EQ(g->hash(), fnv1a(bytes.data(), bytes.size()));
}

TEST(Parse, EmptyGenomeIsValid) {
  std::optional<Genome> g = Genome::parse(nullptr, 0);
  ASSERT_TRUE(g);
  EXPECT_EQ(g->geneCount(), 0u);
  EXPECT_EQ(g->nextUid(), GeneUid{1});
}

TEST(Parse, RejectsALengthChainThatOverruns) {
  std::vector<uint8_t> bytes = threeGenes();
  for (size_t cut = 1; cut < bytes.size(); ++cut) {
    bool boundary = cut == 11 || cut == 25;   // after gene 0 or gene 1
    EXPECT_EQ(Genome::parse(bytes.data(), cut).has_value(), boundary) << "cut at " << cut;
  }
}

TEST(Parse, RejectsPastTheSizeCap) {
  std::vector<uint8_t> body(255, 0);
  GenomeBuilder b;
  uint16_t uid = 1;
  size_t size = 0;
  while (size + kGeneHeaderLen + 255 <= kMaxGenomeBytes) {
    b.append(header(0x60, 255, uid++), body.data());
    size += kGeneHeaderLen + 255;
  }
  uint8_t fill = uint8_t(kMaxGenomeBytes - size - kGeneHeaderLen);
  b.append(header(0x60, fill, uid++), body.data());
  ASSERT_TRUE(b.build());
  EXPECT_EQ(b.build()->bytes().size(), kMaxGenomeBytes);
  EXPECT_FALSE(b.append(header(0x60, 0, uid), nullptr).build());
}

TEST(Uid, DuplicateUidIsRejectedByParseAndBuild) {
  const uint8_t body[2] = {1, 2};
  GenomeBuilder b;
  b.append(header(0x01, 2, 5), body).append(header(0x02, 2, 5), body);
  EXPECT_FALSE(b.build());
  std::vector<uint8_t> bytes = threeGenes();
  bytes[11 + 6] = 1;   // gene 1's uid := gene 0's
  EXPECT_FALSE(Genome::parse(bytes.data(), bytes.size()));
}

TEST(Uid, NextUidIsOnePastTheLargest) {
  std::vector<uint8_t> bytes = threeGenes();
  EXPECT_EQ(Genome::parse(bytes.data(), bytes.size())->nextUid(), GeneUid{8});
}

TEST(Uid, ASurvivingGeneKeepsItsUidWhenItsIndexMoves) {
  std::optional<Genome> g = GenomeBuilder::from(*Genome::parse(threeGenes().data(), threeGenes().size()))
                                .erase(GeneUid{1})
                                .build();
  ASSERT_TRUE(g);
  EXPECT_EQ(g->find(GeneUid{7})->index, 1);
  EXPECT_EQ(bodyOf(*g->find(GeneUid{7})), std::vector<uint8_t>{9});
}

TEST(Builder, InsertAfterPlacesTheGeneNextToItsAnchor) {
  Genome g = *Genome::parse(threeGenes().data(), threeGenes().size());
  const uint8_t body[2] = {0xAA, 0xBB};
  std::optional<Genome> out = GenomeBuilder::from(g).insertAfter(GeneUid{1}, header(0x04, 2, 40), body).build();
  ASSERT_TRUE(out);
  ASSERT_EQ(out->geneCount(), 4u);
  EXPECT_EQ(out->gene(1).header.uid, GeneUid{40});
  EXPECT_EQ(bodyOf(out->gene(1)), (std::vector<uint8_t>{0xAA, 0xBB}));
  EXPECT_EQ(out->gene(2).header.uid, GeneUid{2});
  out = GenomeBuilder::from(g).insertAfter(GeneUid{7}, header(0x04, 2, 41), body).build();
  EXPECT_EQ(out->gene(3).header.uid, GeneUid{41});
}

TEST(Builder, SetByteAndSetFlagsTouchOnlyTheNamedGene) {
  Genome g = *Genome::parse(threeGenes().data(), threeGenes().size());
  Genome out = *GenomeBuilder::from(g).setByte(GeneUid{2}, 5, 200).setFlags(GeneUid{2}, GeneFlags::Dormant).build();
  std::vector<uint8_t> expected = g.bytes();
  expected[11 + 8 + 5] = 200;
  expected[11 + 2] = GeneFlags::Dormant;
  EXPECT_EQ(out.bytes(), expected);
}

TEST(Builder, OpsOnAMissingUidOrPastTheBodyChangeNothing) {
  Genome g = *Genome::parse(threeGenes().data(), threeGenes().size());
  const uint8_t body[1] = {1};
  Genome out = *GenomeBuilder::from(g)
                    .setByte(GeneUid{2}, 6, 99)
                    .setByte(GeneUid{99}, 0, 99)
                    .setFlags(GeneUid{99}, 0)
                    .erase(GeneUid{99})
                    .insertAfter(GeneUid{99}, header(0x01, 1, 50), body)
                    .build();
  EXPECT_EQ(out.bytes(), g.bytes());
}

TEST(Unknown, AnUnknownKindIsCarriedByteForByte) {
  std::vector<uint8_t> bytes = threeGenes();
  std::vector<uint8_t> unknown(bytes.begin() + 25, bytes.end());
  const uint8_t body[3] = {1, 2, 3};
  Genome g = *Genome::parse(bytes.data(), bytes.size());
  Genome out = *GenomeBuilder::from(g).append(header(0x01, 3, g.nextUid().v), body).erase(GeneUid{1}).build();
  const std::vector<uint8_t>& o = out.bytes();
  EXPECT_EQ(std::vector<uint8_t>(o.begin() + 14, o.begin() + 23), unknown);
}

TEST(FeatGate, GateNOpensOnFeatBitNMinusOne) {
  GeneHeader gated = header(0x20, 6, 9, Stage::Adult, 2);
  EXPECT_FALSE(expressedAt(gated, Stage::Adult, 0));
  EXPECT_FALSE(expressedAt(gated, Stage::Adult, feat::reached_adult));
  EXPECT_TRUE(expressedAt(gated, Stage::Adult, feat::reached_elder));
  EXPECT_FALSE(expressedAt(gated, Stage::Child, feat::reached_elder));
  EXPECT_TRUE(expressedAt(header(0x20, 6, 9, Stage::Adult, 0), Stage::Adult, 0));
  EXPECT_TRUE(expressedAt(header(0x20, 6, 9, Stage::Adult, 32), Stage::Adult, 0x80000000u));
}

TEST(FeatGate, AGatePastTheFeatBitsNeverOpens) {
  for (int gate = 33; gate <= 255; ++gate)
    EXPECT_FALSE(expressedAt(header(0x20, 6, 9, Stage::Baby, uint8_t(gate)), Stage::Baby, 0xFFFFFFFFu)) << gate;
}

TEST(FeatGate, DormantNeverExpresses) {
  GeneHeader dormant = header(0x20, 6, 9, Stage::Baby, 0, GeneFlags::Dormant | GeneFlags::Mutable);
  EXPECT_FALSE(expressedAt(dormant, Stage::Baby, 0xFFFFFFFFu));
}

// Pure noise is almost always rejected, so half the inputs are well-chained
// genes (random headers and bodies) with random damage: a truncation, a byte
// flipped, or a uid copied from another gene.
std::vector<uint8_t> fuzzInput(Rng& rng) {
  std::vector<uint8_t> out;
  if (rng.below(2) == 0) {
    out.resize(rng.below(400));
    for (uint8_t& b : out) b = uint8_t(rng.next());
    return out;
  }
  uint32_t genes = rng.below(30);
  for (uint32_t i = 0; i < genes; ++i) {
    uint8_t len = uint8_t(rng.below(rng.below(4) == 0 ? 256 : 12));
    out.push_back(uint8_t(rng.next()));
    out.push_back(len);
    for (int k = 0; k < 6; ++k) out.push_back(uint8_t(rng.next()));
    for (uint8_t k = 0; k < len; ++k) out.push_back(uint8_t(rng.next()));
  }
  switch (rng.below(4)) {
    case 0: if (!out.empty()) out.resize(rng.below(uint32_t(out.size()))); break;
    case 1: if (!out.empty()) out[rng.below(uint32_t(out.size()))] = uint8_t(rng.next()); break;
    case 2: if (out.size() > 20) { out[6] = out[out.size() - 2]; } break;
    default: break;
  }
  return out;
}

TEST(Fuzz, HundredThousandByteStringsNeverCrashAndAcceptedOnesReserialise) {
  Rng rng = Rng::seeded(2024);
  uint32_t accepted = 0;
  for (int i = 0; i < 100000; ++i) {
    std::vector<uint8_t> in = fuzzInput(rng);
    std::optional<Genome> g = Genome::parse(in.data(), in.size());
    if (!g) continue;
    ++accepted;
    ASSERT_EQ(g->bytes(), in) << "input " << i;
    std::optional<Genome> again = GenomeBuilder::from(*g).build();
    ASSERT_TRUE(again) << "input " << i;
    ASSERT_EQ(again->bytes(), in) << "input " << i;
    size_t walked = 0;
    g->forEach([&](const GeneView& v) { walked += kGeneHeaderLen + v.header.len; });
    ASSERT_EQ(walked, in.size()) << "input " << i;
  }
  EXPECT_GT(accepted, 20000u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
