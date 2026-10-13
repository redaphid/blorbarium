#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include "blorb/friend_egg.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/keepsake.h"
#include "mem_storage.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint32_t kGiftSeed = 15048;
constexpr const char* kV1 = "test/fixtures/keepsake_v1.bin";
constexpr const char* kScarsFixture = "test/fixtures/keepsake_v1_scars.bin";
constexpr const char* kFriendFixture = "test/fixtures/keepsake_v1_friend_egg.bin";
constexpr uint32_t kFixtureCrossSeed = 0xF00DF00Du;
// The Body chunk's fields before the bite: place, facing, pose, sleep and reflex.
constexpr size_t kBodyFixedLen = 27;
constexpr size_t kFriendTailLen = 5;

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}
void writeFile(const std::string& path, const std::vector<uint8_t>& b) {
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

Phenotype lifeOf(const Genome& g) {
  Phenotype p{};
  for (uint8_t s = 0; s < kStageCount; ++s) expressStage(g, Stage(s), 0, p);
  return p;
}

GeneUid firstOfKind(const Genome& g, uint8_t kind) {
  GeneUid uid{};
  bool found = false;
  g.forEach([&](const GeneView& v) {
    if (!found && v.header.type == kind) uid = v.header.uid, found = true;
  });
  EXPECT_TRUE(found) << "no gene of kind " << int(kind);
  return uid;
}

Clutch clutchOf(const Genome& parent, uint8_t count, uint64_t seed) {
  Clutch k;
  k.parent = parent;
  k.generation = 4;
  Rng r = Rng::seeded(seed);
  for (uint64_t& s : k.seeds) s = uint64_t(r.next()) << 32 | r.next();
  k.count = count;
  return k;
}

FriendEgg friendEggOf(const Genome& g, uint32_t seed) {
  return FriendEgg{FriendEggRecord{0x5eed5eedull, SpeciesId{0}, seed, g.hash()}, g};
}

// ---- the keepsake's chunks, walked by hand so a test can cut a tail off --------------

struct Tlv { uint8_t tag; size_t at, n; };   // at: the first body byte
std::vector<Tlv> chunksOf(const std::vector<uint8_t>& blob) {
  std::vector<Tlv> out;
  for (size_t at = 18; at + 4 <= blob.size();) {
    size_t n = size_t(blob[at + 2]) | size_t(blob[at + 3]) << 8;
    out.push_back(Tlv{blob[at], at + 4, n});
    at += 4 + n;
  }
  return out;
}
const Tlv* findChunk(const std::vector<Tlv>& cs, Chunk tag) {
  for (const Tlv& c : cs) if (c.tag == uint8_t(tag)) return &c;
  return nullptr;
}
void reseal(std::vector<uint8_t>& blob) {
  uint32_t payload = uint32_t(blob.size() - 18);
  for (int i = 0; i < 4; ++i) blob[10 + i] = uint8_t(payload >> (8 * i)), blob[14 + i] = 0;
  uint32_t crc = crc32(blob.data(), blob.size());
  for (int i = 0; i < 4; ++i) blob[14 + i] = uint8_t(crc >> (8 * i));
}
// The blob with chunk `tag`'s body cut to `keep` bytes, as a format without the tail wrote it.
std::vector<uint8_t> cutTail(const std::vector<uint8_t>& blob, Chunk tag, size_t keep) {
  std::vector<Tlv> cs = chunksOf(blob);
  const Tlv* c = findChunk(cs, tag);
  if (!c || keep > c->n) return {};
  std::vector<uint8_t> out(blob.begin(), blob.begin() + long(c->at + keep));
  out.insert(out.end(), blob.begin() + long(c->at + c->n), blob.end());
  out[c->at - 2] = uint8_t(keep), out[c->at - 1] = uint8_t(keep >> 8);
  reseal(out);
  return out;
}

Snapshot decoded(const std::vector<uint8_t>& blob) {
  Snapshot s;
  EXPECT_TRUE(Keepsake::decode(blob.data(), blob.size(), s));
  return s;
}

// The committed v1 pet, scarred: the scars fixture.
Snapshot scarredV1() {
  Snapshot s = decoded(readFile(kV1));
  Creature& c = std::get<Creature>(s.occupant);
  c.addScar(Scar{Scar::Kind(0), 40, 200, 255});
  c.addScar(Scar{Scar::Kind(4), 230, 90, 180});
  return s;
}
// The committed v1 pet, dead with a clutch of two that a friend egg made three.
Snapshot friendEggV1() {
  Snapshot s = decoded(readFile(kV1));
  Creature& c = std::get<Creature>(s.occupant);
  Clutch k = c.layClutch(2, Fx::ratio(4, 100), 1501);
  k.sinceDeath = 1234;
  joinClutch(k, kFixtureCrossSeed);
  s.occupant = std::move(k);
  return s;
}

}  // namespace

// ---- the cross ----------------------------------------------------------------------------

// The Birth entry carries only the diff: lineage replays a friend egg with no
// friend genome. Every one of the 81 pairs, through the whole egg.
TEST(Cross, EveryPairsFriendEggReplaysFromItsDiffAlone) {
  for (const SpeciesInfo& a : SPECIES)
    for (const SpeciesInfo& b : SPECIES)
      for (uint32_t seed : {1u, 2u}) {
        Clutch k = clutchOf(starterGenomeOf(a.id, seed), 2, seed);
        FriendEgg f = friendEggOf(starterGenomeOf(b.id, seed + 7), seed * 2654435761u);
        joinClutch(k, f.record.battleSeed);
        Offspring o = childOf(k, uint8_t(k.friendSlot - 1), &f);
        std::optional<Genome> replay = apply(k.parent, o.diff);
        ASSERT_TRUE(replay) << a.name << " x " << b.name;
        EXPECT_EQ(replay->bytes(), o.genome.bytes()) << a.name << " x " << b.name;
        EXPECT_TRUE(viability(o.genome).ok) << a.name << " x " << b.name;
      }
}

// The cross alone, ten thousand times: 81 pairs at 124 seeds each.
TEST(Cross, TenThousandCrossesReplayAndAreDeterministic) {
  Genome line[kSpeciesCount] = {starterGenomeOf(SpeciesId{0}, 1), starterGenomeOf(SpeciesId{1}, 1),
                                starterGenomeOf(SpeciesId{2}, 1), starterGenomeOf(SpeciesId{3}, 1),
                                starterGenomeOf(SpeciesId{4}, 1), starterGenomeOf(SpeciesId{5}, 1),
                                starterGenomeOf(SpeciesId{6}, 1), starterGenomeOf(SpeciesId{7}, 1),
                                starterGenomeOf(SpeciesId{8}, 1)};
  int crosses = 0;
  for (uint8_t a = 0; a < kSpeciesCount; ++a)
    for (uint8_t b = 0; b < kSpeciesCount; ++b)
      for (uint32_t seed = 0; seed < 124; ++seed, ++crosses) {
        Rng r1 = Rng::seeded(seed), r2 = Rng::seeded(seed);
        MutationDiff d = crossDiff(line[a], line[b], r1);
        ASSERT_EQ(crossDiff(line[a], line[b], r2).ops.size(), d.ops.size());
        std::optional<Genome> g = apply(line[a], d);
        ASSERT_TRUE(g) << int(a) << " x " << int(b) << " seed " << seed;
        for (const MutationOp& op : d.ops) {
          const MutPoint* p = std::get_if<MutPoint>(&op);
          ASSERT_TRUE(p) << "a cross is MutPoint ops only";
          EXPECT_EQ(g->find(p->gene)->body[p->offset], line[b].find(p->gene)->body[p->offset]);
        }
      }
  EXPECT_GE(crosses, 10000);
}

// The friend's bytes came over the radio. A planted knock -> injury, and every
// other gene a cross must not touch scrambled, cross nothing at all.
TEST(Cross, APlantedKnockToInjuryCrossesNothingButAllowlistedGenes) {
  const Genome mine = starterGenomeOf(SpeciesId{3}, 5);
  GenomeBuilder hostile = GenomeBuilder::from(starterGenomeOf(SpeciesId{6}, 5));
  starterGenomeOf(SpeciesId{6}, 5).forEach([&](const GeneView& v) {
    if (crossable(v.header.type)) return;
    for (uint8_t i = 0; i < v.header.len; ++i) hostile.setByte(v.header.uid, i, uint8_t(~v.body[i]));
  });
  const GeneUid knock = firstOfKind(mine, GeneKindOf<StimulusGene>::value);
  hostile.setByte(knock, 0, stim::knock.v).setByte(knock, 2, chem::injury.v).setByte(knock, 5, 255);
  std::optional<Genome> friends = hostile.build();
  ASSERT_TRUE(friends);
  int copied = 0;
  for (uint32_t seed = 0; seed < 500; ++seed) {
    Rng r = Rng::seeded(seed);
    MutationDiff d = crossDiff(mine, *friends, r);
    std::optional<Genome> crossed = apply(mine, d);
    ASSERT_TRUE(crossed);
    copied += int(d.ops.size());
    mine.forEach([&](const GeneView& v) {
      if (crossable(v.header.type)) return;
      EXPECT_EQ(0, std::memcmp(crossed->find(v.header.uid)->body, v.body, v.header.len))
          << "seed " << seed << " crossed uid " << v.header.uid.v << " of kind " << int(v.header.type);
    });
  }
  EXPECT_GT(copied, 0) << "the allowlisted genes still cross";
}

// Genes only the friend has, or with another kind or length at my uid, never cross.
TEST(Cross, OnlyLociBothShareWithTheSameKindAndLength) {
  const Genome mine = starterGenomeOf(SpeciesId{2}, 9);
  const GeneUid palette = firstOfKind(mine, GeneKindOf<PaletteGene>::value);
  const GeneUid size = firstOfKind(mine, GeneKindOf<SizeGene>::value);
  GenomeBuilder b;
  starterGenomeOf(SpeciesId{7}, 9).forEach([&](const GeneView& v) {
    GeneHeader h = v.header;
    if (h.uid == palette) h.type = GeneKindOf<MarkGene>::value, h.len = sizeof(MarkGene);
    if (h.uid == size) return;
    b.append(h, v.body);
  });
  const uint8_t extra[sizeof(PaletteGene)] = {1, 2, 3, 4, 5, 6};
  b.append(GeneHeader{GeneKindOf<PaletteGene>::value, sizeof(PaletteGene), GeneFlags::Mutable, Stage::Baby, 0, 64,
                      GeneUid{0x7ABC}},
           extra);
  std::optional<Genome> friends = b.build();
  ASSERT_TRUE(friends);
  for (uint32_t seed = 0; seed < 300; ++seed) {
    Rng r = Rng::seeded(seed);
    for (const MutationOp& op : crossDiff(mine, *friends, r).ops) {
      const MutPoint& p = std::get<MutPoint>(op);
      EXPECT_NE(p.gene, palette) << "another kind at my uid";
      EXPECT_NE(p.gene, size) << "a gene the friend lacks";
      EXPECT_NE(p.gene, GeneUid{0x7ABC}) << "a gene only the friend has";
    }
  }
}

// Species and power sit far apart, so about half of crosses between two
// species give one parent's body with the other's power. Measured.
TEST(Cross, AHybridComesOfThirtyFiveToSixtyFivePercentOfCrosses) {
  int hybrids = 0, crosses = 0;
  for (const SpeciesInfo& a : SPECIES)
    for (const SpeciesInfo& b : SPECIES) {
      if (a.id == b.id) continue;
      const Genome mine = starterGenomeOf(a.id, 3), friends = starterGenomeOf(b.id, 3);
      for (uint32_t seed = 0; seed < 40; ++seed, ++crosses) {
        Rng r = Rng::seeded(seed * 977u + a.id.v * 31u + b.id.v);
        std::optional<Genome> g = apply(mine, crossDiff(mine, friends, r));
        ASSERT_TRUE(g);
        hybrids += lifeOf(*g).species.hybrid();
      }
    }
  const int permille = hybrids * 1000 / crosses;
  RecordProperty("hybrid_permille", permille);
  std::printf("hybrids: %d of %d crosses (%d permille)\n", hybrids, crosses, permille);
  EXPECT_GE(permille, 350);
  EXPECT_LE(permille, 650);
}

// A parent an owner EDIT left non-viable: the cross is refused before mutate,
// and the mutated cross would be refused after it (mutate hands a non-viable
// input back unchanged). Either way the slot holds the parent's plain egg.
TEST(Cross, AFizzledCrossIsThePlainEggInThatSlot) {
  const Genome viable = starterGenomeOf(SpeciesId{1}, 4);
  std::optional<Genome> sick =
      GenomeBuilder::from(viable).erase(firstOfKind(viable, GeneKindOf<MetabolismGene>::value)).build();
  ASSERT_TRUE(sick);
  ASSERT_FALSE(viability(*sick).ok);
  Clutch k = clutchOf(*sick, 3, 77);
  FriendEgg f = friendEggOf(starterGenomeOf(SpeciesId{8}, 4), 99);
  joinClutch(k, 99);
  Rng r = Rng::seeded(99);
  ASSERT_FALSE(crossDiff(k.parent, f.genome, r).ops.empty()) << "a cross that would have changed him";
  Offspring friendSlot = childOf(k, 2, &f), plain = k.child(2);
  EXPECT_EQ(friendSlot.genome.bytes(), plain.genome.bytes());
  EXPECT_EQ(friendSlot.diff.ops.size(), plain.diff.ops.size());
}

TEST(Cross, AnotherSlotOrAnotherHugsEggIsThePlainEgg) {
  Clutch k = clutchOf(starterGenomeOf(SpeciesId{4}, 2), 2, 5);
  FriendEgg f = friendEggOf(starterGenomeOf(SpeciesId{0}, 2), 1234);
  joinClutch(k, 1234);
  auto same = [](const Offspring& a, const Offspring& b) { return a.genome.bytes() == b.genome.bytes(); };
  EXPECT_FALSE(same(childOf(k, 2, &f), k.child(2))) << "the friend egg is a cross";
  EXPECT_TRUE(same(childOf(k, 0, &f), k.child(0)));
  EXPECT_TRUE(same(childOf(k, 1, &f), k.child(1)));
  EXPECT_TRUE(same(childOf(k, 2, nullptr), k.child(2))) << "no friend.egg";
  FriendEgg other = f;
  other.record.battleSeed = 4321;
  EXPECT_TRUE(same(childOf(k, 2, &other), k.child(2))) << "a friend.egg from another hug";
  Clutch none = clutchOf(starterGenomeOf(SpeciesId{4}, 2), 3, 5);
  EXPECT_TRUE(same(childOf(none, 2, &f), none.child(2))) << "a clutch with no friend egg";
}

// ---- the clutch ---------------------------------------------------------------------------

TEST(Clutch, AFriendEggJoinsWithinThreeAndAFullClutchGivesUpItsLastSlot) {
  for (uint8_t count : {1, 2, 3}) {
    Clutch k = clutchOf(starterGenome(7), count, 1);
    joinClutch(k, 42);
    EXPECT_EQ(k.count, std::min<int>(count + 1, kMaxClutch));
    EXPECT_EQ(k.friendSlot, k.count) << "the last egg";
    EXPECT_EQ(k.friendSeed, 42u);
    joinClutch(k, 43);
    EXPECT_EQ(k.count, std::min<int>(count + 1, kMaxClutch)) << "a second join does not grow it again";
    EXPECT_EQ(k.friendSeed, 43u);
  }
}

namespace {
std::optional<uint8_t> runClutch(Clutch& k, uint8_t knocks, bool hold) {
  SenseOut none;
  for (uint32_t t = 0; t < Clutch::kVigilTicks; ++t) if (auto i = k.tick(none)) return i;
  for (uint8_t i = 0; i < knocks; ++i) {
    SenseOut s;
    s.fire(stim::knock);
    if (auto picked = k.tick(s)) return picked;
  }
  if (hold) {
    SenseOut s;
    s.fire(stim::button_hold);
    return k.tick(s);
  }
  for (uint32_t t = 0; t <= Clutch::kPickTimeoutTicks; ++t) if (auto i = k.tick(none)) return i;
  return std::nullopt;
}
}  // namespace

TEST(Clutch, TheTimeoutNeverPicksTheFriendEggButAKidMay) {
  auto withFriend = [] {
    Clutch k = clutchOf(starterGenome(7), 2, 1);
    joinClutch(k, 42);
    return k;
  };
  Clutch a = withFriend(), b = withFriend(), c = withFriend(), plain = clutchOf(starterGenome(7), 3, 1);
  EXPECT_EQ(runClutch(a, 2, false), uint8_t(0)) << "the cursor left on the friend egg";
  EXPECT_EQ(runClutch(b, 1, false), uint8_t(1)) << "his own second egg";
  EXPECT_EQ(runClutch(c, 2, true), uint8_t(2)) << "a kid's hold picks the friend egg";
  EXPECT_EQ(runClutch(plain, 2, false), uint8_t(2)) << "with no friend egg the timeout takes the cursor";
}

// The clutch keeps seeds and rebuilds its eggs, so the friend's genome lives
// in friend.egg until the Birth. A reboot mid-pick is a keepsake decode and a
// friend.egg load, and gives the same friend egg.
TEST(Clutch, ARebootDuringThePickKeepsTheFriendsGenome) {
  MemStorage store;
  Snapshot s = friendEggV1();
  const Clutch& k = std::get<Clutch>(s.occupant);
  FriendEgg f = friendEggOf(starterGenomeOf(SpeciesId{6}, 3), kFixtureCrossSeed);
  ASSERT_TRUE(FriendEggStore::save(store, f));
  const Offspring before = childOf(k, uint8_t(k.friendSlot - 1), &f);

  ASSERT_TRUE(Keepsake(store).save(s));
  Snapshot back;
  ASSERT_EQ(Keepsake(store).load(back), SlotState::Resumed);
  const Clutch& rebooted = std::get<Clutch>(back.occupant);
  std::optional<FriendEgg> loaded = FriendEggStore::load(store);
  ASSERT_TRUE(loaded);
  const Offspring after = childOf(rebooted, uint8_t(rebooted.friendSlot - 1), &*loaded);
  EXPECT_EQ(after.genome.bytes(), before.genome.bytes());
  ASSERT_EQ(after.diff.ops.size(), before.diff.ops.size());
  EXPECT_NE(after.genome.bytes(), rebooted.child(uint8_t(rebooted.friendSlot - 1)).genome.bytes());

  const std::vector<uint8_t> saved = store.files[FriendEggStore::kFile];
  ASSERT_TRUE(FriendEggStore::consume(store));
  EXPECT_FALSE(FriendEggStore::load(store)) << "consumed at the Birth";
  EXPECT_EQ(store.files[FriendEggStore::kUsed], saved) << "renamed, never deleted";
}

// ---- friend.egg and the receiving boundary ------------------------------------------------

TEST(Store, FriendEggRoundTripsAndANewerHugReplacesIt) {
  MemStorage store;
  FriendEgg a = friendEggOf(starterGenomeOf(SpeciesId{2}, 1), 7), b = friendEggOf(starterGenomeOf(SpeciesId{5}, 1), 8);
  a.record.friendSpecies = SpeciesId{2};
  ASSERT_TRUE(FriendEggStore::save(store, a));
  std::optional<FriendEgg> l = FriendEggStore::load(store);
  ASSERT_TRUE(l);
  EXPECT_EQ(l->genome.bytes(), a.genome.bytes());
  EXPECT_EQ(l->record.friendLineage, a.record.friendLineage);
  EXPECT_EQ(l->record.friendSpecies, SpeciesId{2});
  EXPECT_EQ(l->record.battleSeed, 7u);
  EXPECT_EQ(l->record.friendGenomeHash, a.genome.hash());
  ASSERT_TRUE(FriendEggStore::save(store, b));
  EXPECT_EQ(FriendEggStore::load(store)->record.battleSeed, 8u);
  MemStorage empty;
  EXPECT_FALSE(FriendEggStore::load(empty)) << "no file";
}

TEST(Store, AnyDamageIsNoFriendEggNeverAMisreadOne) {
  MemStorage store;
  ASSERT_TRUE(FriendEggStore::save(store, friendEggOf(starterGenomeOf(SpeciesId{1}, 1), 7)));
  const std::vector<uint8_t> good = store.files[FriendEggStore::kFile];
  for (size_t i = 0; i < good.size(); ++i) {
    store.files[FriendEggStore::kFile] = good;
    store.files[FriendEggStore::kFile][i] ^= 0x10;
    EXPECT_FALSE(FriendEggStore::load(store)) << "flipped byte " << i;
  }
  for (size_t n : {size_t(0), size_t(10), good.size() - 1}) {
    store.files[FriendEggStore::kFile].assign(good.begin(), good.begin() + long(n));
    EXPECT_FALSE(FriendEggStore::load(store)) << "cut to " << n;
  }
}

TEST(Accept, RefusesDamageAWrongHashAndAStranger) {
  const Genome mine = starterGenomeOf(SpeciesId{0}, 1), friends = starterGenomeOf(SpeciesId{4}, 1);
  const std::vector<uint8_t>& b = friends.bytes();
  ASSERT_TRUE(acceptFriendGenome(b.data(), b.size(), friends.hash(), mine));
  EXPECT_FALSE(acceptFriendGenome(b.data(), b.size(), friends.hash() ^ 1, mine)) << "not the announced genome";
  EXPECT_FALSE(acceptFriendGenome(b.data(), b.size() - 3, friends.hash(), mine)) << "cut short";
  GenomeBuilder stranger;
  const uint8_t body[sizeof(PaletteGene)] = {};
  stranger.append(GeneHeader{GeneKindOf<PaletteGene>::value, sizeof(PaletteGene), 0, Stage::Baby, 0, 0, GeneUid{0x7F00}},
                  body);
  Genome s = *stranger.build();
  EXPECT_FALSE(acceptFriendGenome(s.bytes().data(), s.bytes().size(), s.hash(), mine)) << "shares no uid";
}

// ---- the keepsake's trailing bytes ----------------------------------------------------------

// Nothing new is written for a pet with no scars and no friend egg: the
// committed fixture and the gift genome's egg, creature and clutch.
TEST(Keepsake, AnOrdinaryPetWritesNoTrailingBytes) {
  const std::vector<uint8_t> v1 = readFile(kV1);
  ASSERT_FALSE(v1.empty()) << kV1 << " is missing (run from the project root)";
  EXPECT_EQ(Keepsake::encode(decoded(v1)), v1) << "the v1 fixture re-encodes to its own bytes";
  EXPECT_EQ(findChunk(chunksOf(v1), Chunk::Body)->n, kBodyFixedLen) << "the fixture holds no bite";

  Creature c(Egg(Offspring{starterGenome(kGiftSeed), {}}, 1, 0), 0, 0);
  Habitat h;
  Behaviours bh;
  for (uint32_t t = 0; t < 300; ++t) c.tick(SenseOut{}, h, bh, t);
  Snapshot s = decoded(v1);
  s.occupant = c;
  std::vector<uint8_t> creature = Keepsake::encode(s);
  EXPECT_EQ(findChunk(chunksOf(creature), Chunk::Body)->n, kBodyFixedLen);
  Clutch k = c.layClutch(3, Fx::zero(), 300);
  const size_t heirlooms = k.heirlooms.size();
  s.occupant = k;
  std::vector<uint8_t> clutch = Keepsake::encode(s);
  // kind, generation, wild bonus, three seeds, count, cursor, since death, cause, reached, heirlooms.
  EXPECT_EQ(findChunk(chunksOf(clutch), Chunk::Occupant)->n, 1 + 2 + 4 + 24 + 1 + 1 + 4 + 1 + 1 + 1 + heirlooms * 11);
  s.occupant = Egg(k.child(0), k.generation, 300);
  std::vector<uint8_t> egg = Keepsake::encode(s);
  for (const std::vector<uint8_t>* blob : {&creature, &clutch, &egg})
    EXPECT_EQ(Keepsake::encode(decoded(*blob)), *blob);
  if (const char* dir = std::getenv("BLORB_D7_PLAIN_DIR")) {   // tools/old_reader.sh hands these to 0.4.0
    writeFile(std::string(dir) + "/gift_creature.bin", creature);
    writeFile(std::string(dir) + "/gift_clutch.bin", clutch);
    writeFile(std::string(dir) + "/gift_egg.bin", egg);
  }
}

TEST(Keepsake, ScarsRoundTripWithAndWithoutABite) {
  for (uint8_t n = 1; n <= kMaxScars; ++n) {
    Snapshot s = decoded(readFile(kV1));
    Creature& c = std::get<Creature>(s.occupant);
    for (uint8_t i = 0; i < n; ++i) c.addScar(Scar{Scar::Kind(i % 5), uint8_t(i * 50), uint8_t(255 - i), uint8_t(96 + i)});
    std::vector<uint8_t> blob = Keepsake::encode(s);
    EXPECT_EQ(findChunk(chunksOf(blob), Chunk::Body)->n, kBodyFixedLen + 2 + 4u * n);
    Snapshot back = decoded(blob);
    const Scars& got = std::get<Creature>(back.occupant).scars();
    ASSERT_EQ(got.count, n);
    for (uint8_t i = 0; i < n; ++i) {
      EXPECT_EQ(got.mark[i].kind, c.scars().mark[i].kind);
      EXPECT_EQ(got.mark[i].x, c.scars().mark[i].x);
      EXPECT_EQ(got.mark[i].y, c.scars().mark[i].y);
      EXPECT_EQ(got.mark[i].fade, c.scars().mark[i].fade);
    }
    EXPECT_EQ(back.hash(), s.hash());
    EXPECT_EQ(Keepsake::encode(back), blob);
  }
}

// A kind this firmware has no art for (a newer one's) reads as a claw; scars
// past four are read and not kept; the pet loads either way.
TEST(Keepsake, ANewerFirmwaresScarsLoadAsScarsThisOneCanDraw) {
  Snapshot s = scarredV1();
  std::vector<uint8_t> blob = Keepsake::encode(s);
  std::vector<Tlv> cs = chunksOf(blob);
  const Tlv body = *findChunk(cs, Chunk::Body);
  blob[body.at + kBodyFixedLen + 2 + 4] = 200;   // the second scar's kind
  reseal(blob);
  Snapshot back = decoded(blob);
  EXPECT_EQ(std::get<Creature>(back.occupant).scars().mark[1].kind, Scar::Kind{});

  std::vector<uint8_t> six(blob.begin(), blob.begin() + long(body.at + body.n));
  six[body.at + kBodyFixedLen + 1] = 6;
  for (int i = 0; i < 4; ++i) six.insert(six.end(), {uint8_t(i), 1, 2, 3});
  six.insert(six.end(), blob.begin() + long(body.at + body.n), blob.end());
  six[body.at - 2] = uint8_t(body.n + 16);
  reseal(six);
  Snapshot many = decoded(six);
  EXPECT_EQ(std::get<Creature>(many.occupant).scars().count, kMaxScars);
}

TEST(Keepsake, AFriendEggRoundTripsAndABadSlotIsNoFriendEgg) {
  Snapshot s = friendEggV1();
  std::vector<uint8_t> blob = Keepsake::encode(s);
  Snapshot back = decoded(blob);
  const Clutch& k = std::get<Clutch>(back.occupant);
  EXPECT_EQ(k.count, 3);
  EXPECT_EQ(k.friendSlot, 3);
  EXPECT_EQ(k.friendSeed, kFixtureCrossSeed);
  EXPECT_EQ(back.hash(), s.hash());
  EXPECT_EQ(Keepsake::encode(back), blob);

  const Tlv occ = *findChunk(chunksOf(blob), Chunk::Occupant);
  for (uint8_t slot : {0, 1, 4, 255}) {
    std::vector<uint8_t> bad = blob;
    bad[occ.at + occ.n - kFriendTailLen] = slot;
    reseal(bad);
    Snapshot b = decoded(bad);
    EXPECT_EQ(std::get<Clutch>(b.occupant).friendSlot, 0) << "slot " << int(slot);
    EXPECT_EQ(std::get<Clutch>(b.occupant).count, 3) << "the clutch is still whole";
  }
  std::vector<uint8_t> shortTail = cutTail(blob, Chunk::Occupant, occ.n - 2);
  EXPECT_EQ(std::get<Clutch>(decoded(shortTail).occupant).friendSlot, 0) << "a torn tail is no friend egg";
}

TEST(Keepsake, TheSnapshotHashCoversTheFriendEgg) {
  Snapshot s = friendEggV1(), plain = friendEggV1(), reseeded = friendEggV1();
  std::get<Clutch>(plain.occupant).friendSlot = 0;
  std::get<Clutch>(reseeded.occupant).friendSeed ^= 1;
  EXPECT_NE(plain.hash(), s.hash());
  EXPECT_NE(reseeded.hash(), s.hash());
}

// ---- the committed fixtures -------------------------------------------------------------------

// The v1 pet with two scars and nothing in his mouth. Cut the bite and the
// scars off and it is the committed v1 fixture, byte for byte: everything an
// older reader reads is unchanged. tools/old_reader.sh proves 0.4.0 loads it.
TEST(Fixture, TheScarsFixtureIsTheV1PetPlusTrailingBytes) {
  if (std::getenv("BLORB_WRITE_FIXTURE")) writeFile(kScarsFixture, Keepsake::encode(scarredV1()));
  const std::vector<uint8_t> blob = readFile(kScarsFixture), v1 = readFile(kV1);
  ASSERT_FALSE(blob.empty()) << kScarsFixture << " is missing (run from the project root)";
  ASSERT_EQ(blob[4] | blob[5] << 8, 1) << "kFormatVersion stays 1";
  EXPECT_EQ(blob, Keepsake::encode(scarredV1())) << "the fixture is what this firmware writes";
  EXPECT_EQ(cutTail(blob, Chunk::Body, kBodyFixedLen), v1);
  Snapshot s = decoded(blob);
  const Creature& c = std::get<Creature>(s.occupant);
  EXPECT_EQ(c.body().mouth, Mouthful::Nothing);
  ASSERT_EQ(c.scars().count, 2);
  EXPECT_EQ(c.scars().mark[1].kind, Scar::Kind(4));
  EXPECT_EQ(c.scars().mark[1].fade, 180);
  EXPECT_EQ(Keepsake::encode(s), blob);
}

// The v1 pet's clutch of two, grown to three by a friend egg. Cut the five
// bytes and it is that same clutch of three as an older firmware saves it.
TEST(Fixture, TheFriendEggFixtureIsAClutchPlusFiveTrailingBytes) {
  if (std::getenv("BLORB_WRITE_FIXTURE")) writeFile(kFriendFixture, Keepsake::encode(friendEggV1()));
  const std::vector<uint8_t> blob = readFile(kFriendFixture);
  ASSERT_FALSE(blob.empty()) << kFriendFixture << " is missing (run from the project root)";
  ASSERT_EQ(blob[4] | blob[5] << 8, 1) << "kFormatVersion stays 1";
  EXPECT_EQ(blob, Keepsake::encode(friendEggV1()));
  Snapshot plain = friendEggV1();
  std::get<Clutch>(plain.occupant).friendSlot = 0;
  std::get<Clutch>(plain.occupant).friendSeed = 0;
  const Tlv occ = *findChunk(chunksOf(blob), Chunk::Occupant);
  EXPECT_EQ(cutTail(blob, Chunk::Occupant, occ.n - kFriendTailLen), Keepsake::encode(plain));
  Snapshot s = decoded(blob);
  const Clutch& k = std::get<Clutch>(s.occupant);
  EXPECT_EQ(k.friendSlot, 3);
  EXPECT_EQ(k.friendSeed, kFixtureCrossSeed);
  EXPECT_EQ(k.generation, 4);
  EXPECT_EQ(Keepsake::encode(s), blob);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
