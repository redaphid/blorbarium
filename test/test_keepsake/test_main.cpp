#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include "blorb/keepsake.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "mem_storage.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr Fx kHalf = Fx::ratio(1, 2);

Settings settingsOf(const char* name) {
  Settings s{};
  std::strncpy(s.name, name, sizeof s.name - 1);
  s.lineageId = 0x9f31c2d04a7bull;
  s.speciesSeed = 7;
  s.brightness = 200;
  return s;
}

// A creature some minutes into a life: a pellet dropped and eaten or not, a
// shake (so a reflex, adrenaline and learning), and self-stimuli pending.
Snapshot creatureSnapshot() {
  Creature c(Egg(Offspring{starterGenome(7), {}}, 3, 0), feat::reached_adult, 0);
  Habitat h;
  h.pantry = c.phenotype().habitat.pantrySize;
  Behaviours b;
  Rng rng = Rng::seeded(11);
  PetClock clock;
  for (uint32_t t = 0; t < 1500; ++t) {
    SenseOut s;
    clock.advance(false);
    if (t == 40) h.dropPellet(c.phenotype().habitat, rng, t, s);
    if (t == 700) s.fire(stim::shake);
    h.step(c.phenotype().habitat, kHalf, kHalf, c.body().at, t, s);
    c.tick(s, h, b, t);
  }
  return Snapshot{0, clock, std::move(c), h, settingsOf("Grungo_IV"), rng, WallAnchor{1790000000u, 1200}, {}};
}

Snapshot eggSnapshot() {
  Clutch k;
  k.parent = starterGenome(7);
  k.generation = 2;
  k.seeds[0] = 5;
  Egg e(k.child(0), 2, 77);
  SenseOut warm;
  warm.set(locus::held, Fx::one());
  for (int i = 0; i < 30; ++i) e.tick(i % 3 ? SenseOut{} : warm);
  return Snapshot{0, PetClock{}, std::move(e), Habitat{}, settingsOf("Grungo_II"), Rng::seeded(2), std::nullopt, {}};
}

Snapshot clutchSnapshot() {
  Snapshot s = creatureSnapshot();
  Creature& c = std::get<Creature>(s.occupant);
  c.inject(chem::life, Fx::ratio(5, 100));
  Habitat h;
  Behaviours b;
  c.tick(SenseOut{}, h, b, 1500);
  Clutch k = c.layClutch(3, Fx::ratio(4, 100), 1501);
  k.sinceDeath = 1234;
  k.cursor = 2;
  s.occupant = std::move(k);
  return s;
}

std::vector<uint8_t> roundTrip(const Snapshot& s) {
  std::vector<uint8_t> blob = Keepsake::encode(s);
  Snapshot back;
  if (!Keepsake::decode(blob.data(), blob.size(), back)) {
    ADD_FAILURE() << "does not decode";
    return {};
  }
  EXPECT_EQ(back.hash(), s.hash());
  return Keepsake::encode(back);
}

void setFormat(std::vector<uint8_t>& blob, uint16_t format) {
  blob[4] = uint8_t(format);
  blob[5] = uint8_t(format >> 8);
}

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

constexpr const char* kFixture = "test/fixtures/keepsake_v1.bin";

}  // namespace

TEST(Codec, EveryOccupantRoundTripsByteIdentical) {
  for (const Snapshot& s : {creatureSnapshot(), eggSnapshot(), clutchSnapshot()}) {
    std::vector<uint8_t> blob = Keepsake::encode(s);
    EXPECT_EQ(roundTrip(s), blob);
  }
}

TEST(Codec, TheCreatureComesBackAsItWas) {
  Snapshot s = creatureSnapshot();
  std::vector<uint8_t> blob = Keepsake::encode(s);
  Snapshot back;
  ASSERT_TRUE(Keepsake::decode(blob.data(), blob.size(), back));
  const Creature &a = std::get<Creature>(s.occupant), &b = std::get<Creature>(back.occupant);
  EXPECT_EQ(b.hash(), a.hash());
  EXPECT_EQ(b.stats().shaken, 1u);
  EXPECT_EQ(b.stats().actionTicks[action::rest.v], a.stats().actionTicks[action::rest.v]);
  EXPECT_EQ(b.phenotype().chem.receptors.size(), a.phenotype().chem.receptors.size()) << "re-expressed";
  EXPECT_EQ(b.phenotype().instincts.size(), a.phenotype().instincts.size());
  EXPECT_EQ(b.brain().strongestBeliefs(5).size(), a.brain().strongestBeliefs(5).size());

  // The decoded one goes on living exactly as the original would.
  Snapshot live = creatureSnapshot();
  Creature& c1 = std::get<Creature>(live.occupant);
  Creature& c2 = std::get<Creature>(back.occupant);
  Behaviours b1, b2;
  for (uint32_t t = 1500; t < 2500; ++t) {
    SenseOut s1, s2;
    if (t == 1800) s1.fire(stim::knock), s2.fire(stim::knock);
    live.habitat.step(c1.phenotype().habitat, kHalf, kHalf, c1.body().at, t, s1);
    back.habitat.step(c2.phenotype().habitat, kHalf, kHalf, c2.body().at, t, s2);
    c1.tick(s1, live.habitat, b1, t);
    c2.tick(s2, back.habitat, b2, t);
  }
  EXPECT_EQ(c2.hash(), c1.hash());
}

TEST(Codec, TheWallAnchorRoundTripsAndUnknownStaysUnknown) {
  Snapshot known = creatureSnapshot();
  std::vector<uint8_t> blob = Keepsake::encode(known);
  Snapshot back;
  ASSERT_TRUE(Keepsake::decode(blob.data(), blob.size(), back) && back.wall);
  EXPECT_EQ(back.wall->wallSeconds, 1790000000u);
  EXPECT_EQ(back.wall->tick, 1200u);

  Snapshot unknown = eggSnapshot();
  blob = Keepsake::encode(unknown);
  ASSERT_TRUE(Keepsake::decode(blob.data(), blob.size(), back));
  EXPECT_FALSE(back.wall.has_value()) << "decoding over a known anchor clears it";
}

TEST(Codec, AnyFlippedByteIsRejectedNeverMisread) {
  std::vector<uint8_t> blob = Keepsake::encode(creatureSnapshot());
  Snapshot scratch;
  for (size_t at = 0; at < blob.size(); at += 7) {
    std::vector<uint8_t> bad = blob;
    bad[at] ^= uint8_t(1 + at % 255);
    EXPECT_FALSE(Keepsake::decode(bad.data(), bad.size(), scratch)) << "byte " << at;
  }
  for (size_t len = 0; len < blob.size(); len += 13)
    EXPECT_FALSE(Keepsake::decode(blob.data(), len, scratch)) << "truncated to " << len;
}

TEST(Slots, SavingTwiceLeavesTwoValidSlotsAndLoadTakesTheNewest) {
  MemStorage store;
  Keepsake k(store);
  Snapshot l;
  ASSERT_EQ(k.load(l), SlotState::Empty);
  Snapshot first = eggSnapshot(), second = creatureSnapshot();
  ASSERT_TRUE(k.save(first));
  ASSERT_TRUE(k.save(second));
  EXPECT_TRUE(Keepsake::decode(store.files["snap.a"].data(), store.files["snap.a"].size(), l));
  EXPECT_TRUE(Keepsake::decode(store.files["snap.b"].data(), store.files["snap.b"].size(), l));
  Keepsake again(store);
  ASSERT_EQ(again.load(l), SlotState::Resumed);
  EXPECT_EQ(l.seq, 2u);
  EXPECT_EQ(l.hash(), second.hash());
}

TEST(Slots, ATornSlotBLoadsAAndIsQuarantined) {
  MemStorage store;
  Keepsake k(store);
  Snapshot a = eggSnapshot();
  ASSERT_TRUE(k.save(a));
  ASSERT_TRUE(k.save(creatureSnapshot()));
  std::vector<uint8_t> torn = store.files["snap.b"];
  torn.resize(torn.size() / 2);
  store.files["snap.b"] = torn;

  Keepsake again(store);
  Snapshot l;
  ASSERT_EQ(again.load(l), SlotState::FellBack);
  EXPECT_EQ(l.seq, 1u);
  EXPECT_EQ(l.hash(), a.hash());
  EXPECT_EQ(store.files.count("snap.b"), 0u);
  EXPECT_EQ(store.files["rescue/snap.b.0"], torn) << "kept for rescue, never deleted";

  ASSERT_TRUE(again.save(clutchSnapshot()));
  EXPECT_EQ(store.files.count("snap.b"), 1u) << "the next save fills the free slot, not the good one";
  ASSERT_EQ(Keepsake(store).load(l), SlotState::Resumed);
  EXPECT_EQ(l.seq, 2u);
}

// A newer slot whose CRC holds but whose Stats chunk is empty: it fails only
// once its creature is half built in the target. Load falls back to the
// older slot, decoded whole, not the debris.
TEST(Slots, ANewerSlotThatFailsMidCreatureFallsBackToTheOlderWhole) {
  MemStorage store;
  Keepsake k(store);
  Snapshot older = eggSnapshot();
  ASSERT_TRUE(k.save(older));
  ASSERT_TRUE(k.save(creatureSnapshot()));
  std::vector<uint8_t>& b = store.files["snap.b"];
  for (size_t at = 18; at + 4 <= b.size();) {
    const size_t n = size_t(b[at + 2] | b[at + 3] << 8);
    if (b[at] == uint8_t(Chunk::Stats)) {
      b.erase(b.begin() + std::ptrdiff_t(at + 4), b.begin() + std::ptrdiff_t(at + 4 + n));
      b[at + 2] = b[at + 3] = 0;
      break;
    }
    at += 4 + n;
  }
  auto le32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + size_t(i)] = uint8_t(v >> (8 * i)); };
  le32(10, uint32_t(b.size() - 18));
  le32(14, 0);
  le32(14, crc32(b.data(), b.size()));

  Snapshot l;
  ASSERT_EQ(Keepsake(store).load(l), SlotState::FellBack);
  EXPECT_EQ(l.seq, 1u);
  EXPECT_TRUE(std::holds_alternative<Egg>(l.occupant));
  EXPECT_EQ(l.hash(), older.hash());
}

TEST(Slots, BothTornIsCorruptAndBothAreQuarantined) {
  MemStorage store;
  Keepsake k(store);
  ASSERT_TRUE(k.save(eggSnapshot()));
  ASSERT_TRUE(k.save(eggSnapshot()));
  store.files["snap.a"][30] ^= 0xFF;
  store.files["snap.b"].resize(10);
  store.files["rescue/snap.a.0"] = {1, 2, 3};   // an earlier rescue is not overwritten
  Snapshot l;
  EXPECT_EQ(Keepsake(store).load(l), SlotState::Corrupt);
  EXPECT_EQ(store.files["rescue/snap.a.0"], (std::vector<uint8_t>{1, 2, 3}));
  EXPECT_EQ(store.files.count("rescue/snap.a.1"), 1u);
  EXPECT_EQ(store.files.count("rescue/snap.b.0"), 1u);
}

TEST(Slots, ANewerFormatIsReadOnlyAndUntouched) {
  MemStorage store;
  Keepsake k(store);
  ASSERT_TRUE(k.save(eggSnapshot()));
  ASSERT_TRUE(k.save(eggSnapshot()));
  setFormat(store.files["snap.b"], kFormatVersion + 1);
  auto before = store.files;
  Snapshot l;
  EXPECT_EQ(Keepsake(store).load(l), SlotState::NewerFormat);
  EXPECT_EQ(store.files, before);
}

TEST(Slots, AnUnknownChunkSurvivesASave) {
  Snapshot s = creatureSnapshot();
  s.unknown.push_back(RawChunk{200, 3, {9, 8, 7, 6}});
  std::vector<uint8_t> blob = Keepsake::encode(s);
  Snapshot back;
  ASSERT_TRUE(Keepsake::decode(blob.data(), blob.size(), back));
  ASSERT_EQ(back.unknown.size(), 1u);

  MemStorage store;
  Keepsake k(store);
  ASSERT_TRUE(k.save(back));
  Snapshot l;
  ASSERT_EQ(Keepsake(store).load(l), SlotState::Resumed);
  ASSERT_EQ(l.unknown.size(), 1u);
  EXPECT_EQ(l.unknown[0].tag, 200);
  EXPECT_EQ(l.unknown[0].ver, 3);
  EXPECT_EQ(l.unknown[0].bytes, (std::vector<uint8_t>{9, 8, 7, 6}));
}

// The committed fixture is a format-1 keepsake. It must load in every later
// firmware, through Keepsake::migrate once the format moves on.
TEST(Fixture, TheCommittedV1KeepsakeLoads) {
  if (std::getenv("BLORB_WRITE_FIXTURE")) {
    std::vector<uint8_t> blob = Keepsake::encode(creatureSnapshot());
    std::ofstream(kFixture, std::ios::binary).write(reinterpret_cast<const char*>(blob.data()), std::streamsize(blob.size()));
  }
  std::vector<uint8_t> blob = readFile(kFixture);
  ASSERT_FALSE(blob.empty()) << kFixture << " is missing (run from the project root)";
  ASSERT_EQ(blob[4] | blob[5] << 8, 1);
  Snapshot s;
  ASSERT_TRUE(Keepsake::decode(blob.data(), blob.size(), s));
  const Creature& c = std::get<Creature>(s.occupant);
  EXPECT_EQ(c.generation(), 3);
  EXPECT_EQ(c.stats().shaken, 1u);
  EXPECT_STREQ(s.settings.name, "Grungo_IV");
  ASSERT_TRUE(s.wall);
  EXPECT_EQ(s.wall->wallSeconds, 1790000000u);
  EXPECT_EQ(Keepsake::encode(s), blob) << "format 1 still writes what it read";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
