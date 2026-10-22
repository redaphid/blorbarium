#include <gtest/gtest.h>
#include <cstring>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/duel_wire.h"

using namespace blorb;

namespace {

BattleCard card(uint8_t salt) {
  BattleCard c;
  c.species = SpeciesId{uint8_t(salt % kSpeciesCount)};
  c.power = PowerId{uint8_t(salt % kPowerCount)};
  c.knack.order = uint8_t(salt % 6);
  c.mood = Mood{uint8_t(salt + 1), uint8_t(salt + 2), uint8_t(salt + 3), uint8_t(salt + 4)};
  c.level = uint8_t(salt % 12 + 1);
  c.prowess = uint8_t(salt * 7);
  c.lineageId = 0x0123456789abcdefull ^ salt;
  for (uint8_t r = 0; r < kRegionCount; ++r) c.look.regions[r] = Tint{int8_t(salt - r), uint8_t(r * 9), uint8_t(salt)};
  c.look.scalePct = 90;
  c.look.stage = Stage::Child;
  c.look.markCount = 2;
  c.look.marks[1] = CardLook::Mark{3, 4};
  c.look.lifeSeed = 0xdeadbeef ^ salt;
  return c;
}

StatePacket state() {
  StatePacket p;
  p.roster = duelRulesHash();
  p.battle = BattleId{duelSeed(11, 22, 1, Stakes::Mortal), 1, Stakes::Mortal};
  p.nonce = 11;
  p.chosen = Stakes::Mortal;
  p.confirmed = true;
  p.ceiling = Stakes::Mortal;
  p.why[1] = Decline::Wounded;
  p.why[2] = Decline::Unfair;
  p.skull[0] = 0, p.skull[1] = 80, p.skull[2] = 200;
  p.hold = 128;
  p.card = card(5);
  // A downing on turn 15 and the winner's verdict in slot 15: the log is full.
  p.moveCount = DuelRules::kLogSlots;
  for (uint8_t i = 0; i < DuelRules::kMaxTurns; ++i) p.moves[i] = Move{Stance(i % 3), i == 7, i % 2 == 0}.pack();
  p.moves[DuelRules::kMaxTurns] = Move{Stance::Pounce, false, false, Verdict::Finish}.pack();
  p.heard = 15;
  p.emoteSeq = 9;
  p.emote = Emote::Plead;
  p.rematch = true;
  p.hugged = true;
  p.genomeBytes = 1528;
  p.genomeHash = 0xfeedface;
  return p;
}

struct Frame {
  uint8_t b[kDatagramMax]{};
  size_t n = 0;
  explicit Frame(const Packet& p) { n = encode(p, b); }
  // Recomputes the crc, so a test can plant a bad field the crc does not catch.
  void reseal() {
    const uint32_t c = crc32(b, n - 4);
    for (int i = 0; i < 4; ++i) b[n - 4 + i] = uint8_t(c >> (8 * i));
  }
  std::optional<Packet> decoded() const { return decode(b, n); }
};

template <class P>
P roundTrip(const P& p) {
  Frame f{Packet{p}};
  std::optional<Packet> back = f.decoded();
  EXPECT_TRUE(back.has_value());
  EXPECT_TRUE(back && std::holds_alternative<P>(*back));
  // What was decoded encodes to the same bytes: every field survived.
  Frame again{*back};
  EXPECT_EQ(f.n, again.n);
  EXPECT_EQ(0, std::memcmp(f.b, again.b, f.n));
  return std::get<P>(*back);
}

}  // namespace

TEST(DuelWire, EveryKindFitsOneFrameAtItsMeasuredSize) {
  EXPECT_EQ(Frame{Packet{BeaconPacket{}}}.n, kBeaconWireBytes);
  EXPECT_EQ(Frame{Packet{state()}}.n, kStateWireBytes);
  EXPECT_EQ(Frame{Packet{GenomePacket{}}}.n, kGenomeWireBytes);
  EXPECT_EQ(kBeaconWireBytes, 78u);
  EXPECT_EQ(kStateWireBytes, 111u);
  EXPECT_EQ(kGenomeWireBytes, 224u);
  EXPECT_LE(kGenomeWireBytes, kDatagramMax);
}

TEST(DuelWire, StateRoundTripsWithAVerdictAfterATurn15Downing) {
  const StatePacket p = state();
  const StatePacket q = roundTrip(p);
  EXPECT_EQ(q.battle, p.battle);
  EXPECT_EQ(q.moveCount, DuelRules::kLogSlots);
  EXPECT_EQ(Move::unpack(q.moves[DuelRules::kMaxTurns]).verdict, Verdict::Finish);
  EXPECT_EQ(q.card.lineageId, p.card.lineageId);
  EXPECT_EQ(q.why[2], Decline::Unfair);
  EXPECT_EQ(q.emote, Emote::Plead);
  EXPECT_TRUE(q.confirmed && q.rematch && q.hugged && !q.leaving);
}

TEST(DuelWire, ABeaconNeverBroadcastsTheLineage) {
  BeaconPacket p;
  p.roster = duelRulesHash();
  p.nonce = 77;
  p.wants = PeerAddr{{2, 0, 0, 0, 0, 9}};
  p.seq = 0xBEEF;
  p.decline = Decline::Sleepy;
  p.ceiling = Stakes::Scrap;
  p.card = card(3);
  const BeaconPacket q = roundTrip(p);
  EXPECT_EQ(q.card.lineageId, 0u);
  EXPECT_EQ(q.card.look.lifeSeed, p.card.look.lifeSeed);
  EXPECT_EQ(q.wants, p.wants);
  EXPECT_EQ(q.seq, 0xBEEF);
  EXPECT_EQ(q.decline, Decline::Sleepy);
}

TEST(DuelWire, AGenomeChunkRoundTrips) {
  GenomePacket p;
  p.battle = BattleId{42, 0, Stakes::Spar};
  p.have = 0x155;
  p.index = 7;
  p.len = 128;
  for (uint8_t i = 0; i < p.len; ++i) p.bytes[i] = uint8_t(i * 3);
  const GenomePacket q = roundTrip(p);
  EXPECT_EQ(q.have, p.have);
  EXPECT_EQ(0, std::memcmp(q.bytes, p.bytes, p.len));
}

TEST(DuelWire, TheBoundaryRefusesEachMalformedFrame) {
  const Frame good{Packet{state()}};
  ASSERT_TRUE(good.decoded());
  const auto refused = [&](auto plant) {
    Frame f = good;
    plant(f);
    return !f.decoded().has_value();
  };
  EXPECT_TRUE(refused([](Frame& f) { f.n -= 1; }));                       // short
  EXPECT_TRUE(refused([](Frame& f) { f.b[0] = 'x', f.reseal(); }));       // magic
  EXPECT_TRUE(refused([](Frame& f) { f.b[2] = kWireVersion + 1, f.reseal(); }));   // newer version
  EXPECT_TRUE(refused([](Frame& f) { f.b[3] = 9, f.reseal(); }));         // unknown kind
  EXPECT_TRUE(refused([](Frame& f) { f.b[20] ^= 1; }));                   // bad crc
  // Body offsets: [4] roster 2, battle 6, nonce 4, chosen, confirmed, ceiling, why 3, skull 3, hold, card...
  constexpr size_t kChosen = 4 + 2 + 6 + 4, kWhy = kChosen + 3, kCard = kWhy + 3 + 3 + 1;
  constexpr size_t kMoveCount = kCard + kCardWireBytes;
  EXPECT_TRUE(refused([](Frame& f) { f.b[kChosen] = 3, f.reseal(); }));   // a fourth tier
  EXPECT_TRUE(refused([](Frame& f) { f.b[kWhy] = 99, f.reseal(); }));     // an unknown picture
  EXPECT_TRUE(refused([](Frame& f) { f.b[kCard] = DuelRules::kCardVersion + 1, f.reseal(); }));
  EXPECT_TRUE(refused([](Frame& f) { f.b[kMoveCount] = DuelRules::kLogSlots + 1, f.reseal(); }));
  // ...and the frame it came from still decodes.
  EXPECT_FALSE(refused([](Frame&) {}));

  GenomePacket g;
  g.index = uint8_t(kMaxGenomeChunks);
  g.len = 10;
  EXPECT_FALSE(Frame{Packet{g}}.decoded());   // a chunk past the 8 KB cap
  g.index = uint8_t(kMaxGenomeChunks - 1), g.len = kGenomeChunk;
  EXPECT_FALSE(Frame{Packet{g}}.decoded());   // the last chunk overruns kMaxGenomeBytes
}

TEST(DuelWire, DescribeNamesEachFrameAndEachRefusal) {
  Frame st{Packet{state()}};
  EXPECT_STREQ(describe(st.b, st.n).s, "state t16 h15 Mortal hug");
  BeaconPacket bp;
  bp.wants = PeerAddr{{2, 0, 0, 0, 0, 1}};
  Frame b{Packet{bp}};
  EXPECT_STREQ(describe(b.b, b.n).s, "beacon claims #0");
  GenomePacket g;
  g.index = 4, g.len = 10, g.have = 0b111;
  Frame gf{Packet{g}};
  EXPECT_STREQ(describe(gf.b, gf.n).s, "genome 4 have 3");
  st.b[20] ^= 1;
  EXPECT_STREQ(describe(st.b, st.n).s, "bad crc");
  st.b[20] ^= 1;
  st.b[2] = kWireVersion + 1;
  EXPECT_STREQ(describe(st.b, st.n).s, "version");
  EXPECT_STREQ(describe(st.b, 3).s, "short");
}

// A million random buffers: decode never crashes, and nothing without a valid
// crc gets through. Half carry a real header and length, so the fuzz reaches
// past the cheap checks to the crc.
TEST(DuelWire, AMillionRandomBuffersNeverCrashDecode) {
  Rng rng = Rng::seeded(0xd8);
  uint8_t b[kDatagramMax];
  uint32_t accepted = 0;
  constexpr size_t kLens[] = {kBeaconWireBytes, kStateWireBytes, kGenomeWireBytes};
  for (uint32_t i = 0; i < 1000000; ++i) {
    const bool framed = i & 1;
    const size_t n = framed ? kLens[rng.below(3)] : rng.below(kDatagramMax + 1);
    for (size_t k = 0; k < n; ++k) b[k] = uint8_t(rng.next());
    if (framed) b[0] = kWireMagic0, b[1] = kWireMagic1, b[2] = kWireVersion, b[3] = uint8_t(1 + rng.below(3));
    if (decode(b, n)) ++accepted;
  }
  EXPECT_EQ(accepted, 0u);   // 2^-32 a frame: none of a million has a matching crc
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
