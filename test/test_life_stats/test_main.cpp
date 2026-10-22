// Life stats (#38, blorb/life_tally.h, docs/life-stats.md): the counters
// count on the real event paths (the Dish driven through its detectors, the
// whole body path for knocks, two dishes for duels in test_two_dishes), the
// file round-trips and refuses what is torn or foreign, an egg carries its
// parent's and grandparent's lives, and nothing in the engine ever reads a
// counter back: the replay hash and the keepsake are the same with or
// without them.
#include <gtest/gtest.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "../../src/debug_verbs.h"   // here, so the dependency finder links lib/debugble
#include "feeding_badge.h"
#include "links.h"
#include "mem_storage.h"
#include "scripted_owner.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint64_t kLineage = 0x5eed1e55ull;

Genome withGene(uint8_t type, uint8_t offset, uint8_t value, uint8_t firstByte = 255) {
  Genome g = starterGenome(7);
  GenomeBuilder b = GenomeBuilder::from(g);
  g.forEach([&](const GeneView& v) {
    if (v.header.type == type && (firstByte == 255 || v.body[0] == firstByte)) b.setByte(v.header.uid, offset, value);
  });
  return *b.build();
}
Genome quickEgg() { return withGene(GeneKindOf<EggGene>::value, 0, 1); }
// A whole life in about 5 pet hours (test_dish's shortLived).
Genome shortLived() { return withGene(GeneKindOf<ChemGene>::value, 1, 150, chem::life.v); }

// Counts each file's writes, and can refuse one file's.
struct CountingStorage : MemStorage {
  std::map<std::string, int> writes;
  std::string refuse;
  bool writeAtomic(const char* name, const uint8_t* data, size_t len) override {
    if (refuse == name) return false;
    ++writes[name];
    return MemStorage::writeAtomic(name, data, len);
  }
};

// One dish on one store, rebooted at will; a hatched creature at pet noon.
struct Box {
  CountingStorage store;
  Genome founder;
  std::optional<Dish> dish;
  NullLink link;
  uint32_t ms = 0;

  explicit Box(Genome g = quickEgg()) : founder(std::move(g)) {}
  Dish& boot() {
    dish.emplace(store, 7, kLineage, DishOptions{nullptr, &founder});
    ms = 0;
    dish->tick(ms, link);
    return *dish;
  }
  void run(uint32_t forMs) { runFor(*dish, link, ms, forMs); }
  void feel(BodySample (*body)(), uint32_t forMs) {
    for (uint32_t end = ms + forMs; ms < end; ms += kSampleMs) {
      dish->sample(body(), ms);
      dish->tick(ms, link);
    }
  }
  void hatch() {
    for (int s = 0; !std::holds_alternative<Creature>(dish->occupant()); ++s) {
      if (s == 24 * 3600) return ADD_FAILURE() << "no hatch within a pet day";
      run(1000);
    }
  }
  Creature& creature() { return std::get<Creature>(dish->occupant()); }
  // Awake at pet noon, every drive settled.
  void noon() {
    PetClock& c = dish->clock();
    c.phaseOffsetTicks = int32_t(PetClock::kDayTicks / 2) - int32_t(c.petTicks % PetClock::kDayTicks);
    for (const auto& d : DRIVES) creature().inject(driveChem(d.id), Fx::zero());
    for (int i = 0; i < 50 && creature().body().asleep; ++i) run(100);
  }
  // Five jolts each way in under a second: a shake (traces.h).
  void shake() {
    for (uint32_t end = ms + 480; ms < end; ms += kSampleMs) {
      dish->sample(jolt((ms / 80) % 2 == 0), ms);
      dish->tick(ms, link);
    }
    run(3000);   // the trance, then the answer
  }
  uint32_t stat(uint8_t slot) const { return dish->tally().life[slot]; }
};

LifeTally sample() {
  LifeTally t;
  t.lineageId = 0x1234567890abull;
  t.generation = 3;
  t.spells = LifeTally::kStarving | LifeTally::kAsleep;
  for (uint8_t i = 0; i < kLifeStatCount; ++i) t.life.v[i] = 1000u * i + 7;
  AncestorLife parent{2, {}}, grand{1, {}};
  parent.life.v[stat::knock_knocks] = 99;
  parent.life.v[stat::death] = 1 + uint32_t(DeathCause::Starved);
  grand.life.v[stat::won] = 5;
  t.ancestors = {parent, grand};
  return t;
}

bool same(const LifeTally& a, const LifeTally& b) {
  if (a.lineageId != b.lineageId || a.generation != b.generation || a.spells != b.spells || !(a.life == b.life) ||
      a.ancestors.size() != b.ancestors.size())
    return false;
  for (size_t i = 0; i < a.ancestors.size(); ++i)
    if (a.ancestors[i].generation != b.ancestors[i].generation || !(a.ancestors[i].life == b.ancestors[i].life))
      return false;
  return true;
}

// A file with a life and a few days of analytics.
Stats sampleFile() {
  Stats s;
  s.seq = 41;
  s.life = sample();
  for (uint32_t d = 0; d < 3 * Analytics::kBucketSeconds; ++d) {
    if (d % 3600 == 0) s.analytics.add(anx::presses), s.analytics.hist(hist::frame_ms, d % 250);
    s.analytics.second(d >= Analytics::kBucketSeconds ? std::optional<uint32_t>(1790000000u + d) : std::nullopt);
  }
  return s;
}

bool sameBuckets(const Analytics& a, const Analytics& b) {
  std::vector<AnalyticsBucket> x, y;
  a.forEach([&](const AnalyticsBucket& k, bool) { x.push_back(k); });
  b.forEach([&](const AnalyticsBucket& k, bool) { y.push_back(k); });
  return x == y && a.poweredSeconds() == b.poweredSeconds();
}

// Every count and bin in every bucket, summed: what a merge must keep.
uint64_t total(const Analytics& a) {
  uint64_t n = 0;
  a.forEach([&](const AnalyticsBucket& b, bool) {
    for (uint32_t c : b.count) n += c;
    for (const auto& h : b.bins)
      for (uint32_t v : h) n += v;
  });
  return n;
}

}  // namespace

// ---- the file ---------------------------------------------------------------------------

TEST(File, RoundTripsWithItsAncestorsAndAnalytics) {
  Stats s = sampleFile();
  ASSERT_EQ(s.analytics.closedCount(), 3u);
  const std::vector<uint8_t> b = s.encode();
  EXPECT_LT(b.size(), 2048u) << "a few days fit well inside one 4 KB block";
  std::optional<Stats::Loaded> back = Stats::decode(b.data(), b.size());
  ASSERT_TRUE(back && back->life && back->analytics);
  EXPECT_EQ(back->seq, 41u);
  EXPECT_TRUE(same(*back->life, s.life));
  EXPECT_TRUE(sameBuckets(*back->analytics, s.analytics));

  MemStorage store;
  ASSERT_TRUE(s.save(store));
  EXPECT_EQ(s.seq, 42u) << "a write that landed counts";
  std::optional<Stats::Loaded> loaded = Stats::load(store);
  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded->seq, 42u);
  EXPECT_TRUE(same(*loaded->life, s.life));
}

TEST(File, ATornForeignOrNewerFileIsNotRead) {
  const std::vector<uint8_t> good = sampleFile().encode();
  for (size_t i = 0; i < good.size(); ++i) {
    std::vector<uint8_t> b = good;
    b[i] ^= 0x10;
    EXPECT_FALSE(Stats::decode(b.data(), b.size())) << "a flipped bit at " << i;
  }
  for (size_t n = 0; n < good.size(); ++n) EXPECT_FALSE(Stats::decode(good.data(), n)) << "cut at " << n;

  // A version this firmware does not know, with its own CRC right: not ours to read.
  std::vector<uint8_t> newer(good.begin(), good.end() - 4);
  newer[4] = Stats::kVersion + 1;
  const uint32_t crc = crc32(newer.data(), newer.size());
  for (int i = 0; i < 4; ++i) newer.push_back(uint8_t(crc >> (8 * i)));
  EXPECT_FALSE(Stats::decode(newer.data(), newer.size()));

  MemStorage store;
  EXPECT_FALSE(Stats::load(store)) << "no file";
  store.files[Stats::kFile] = {'B', 'L', 'T', 'L', 1, 0, 0, 0, 0, 0};
  EXPECT_FALSE(Stats::load(store)) << "another file's bytes";
}

// A section this firmware does not know is written back as it was.
TEST(File, AnUnknownSectionIsCarried) {
  Stats s = sampleFile();
  s.unknown.push_back(Stats::Section{9, {1, 2, 3}});
  const std::vector<uint8_t> b = s.encode();
  std::optional<Stats::Loaded> l = Stats::decode(b.data(), b.size());
  ASSERT_TRUE(l);
  ASSERT_EQ(l->unknown.size(), 1u);
  Stats again;
  again.resume(std::move(l), s.life.lineageId, s.life.generation);
  again.seq = s.seq;
  EXPECT_EQ(again.encode(), b);
}

// Slots are only ever added at the end: an older file reads its missing
// slots as 0, and a newer one's extra slots are carried back out.
TEST(File, FewerSlotsReadAsZeroAndMoreAreCarried) {
  auto withSlots = [](uint8_t slots, uint32_t fill) {
    std::vector<uint8_t> b = {0};                                   // spells
    for (int i = 0; i < 8; ++i) b.push_back(i == 0 ? 0x42 : 0);   // lineage
    b.push_back(1), b.push_back(0);                                 // generation 1
    b.push_back(slots);
    for (uint8_t s = 0; s < slots; ++s)
      for (int i = 0; i < 4; ++i) b.push_back(uint8_t((fill + s) >> (8 * i)));
    b.push_back(0);   // no ancestors
    return b;
  };
  const std::vector<uint8_t> older = withSlots(10, 100);
  std::optional<LifeTally> a = LifeTally::decode(older.data(), older.size());
  ASSERT_TRUE(a);
  EXPECT_EQ(a->life[stat::presses], 100u);
  EXPECT_EQ(a->life[9], 109u);
  for (uint8_t s = 10; s < kLifeStatCount; ++s) EXPECT_EQ(a->life[s], 0u) << LIFE_STATS[s].name;
  EXPECT_EQ(a->generation, 1);

  const std::vector<uint8_t> newer = withSlots(uint8_t(kLifeStatCount + 3), 500);
  std::optional<LifeTally> b = LifeTally::decode(newer.data(), newer.size());
  ASSERT_TRUE(b);
  ASSERT_EQ(b->life.extra.size(), 3u);
  EXPECT_EQ(b->life.extra[2], 500u + kLifeStatCount + 2);
  EXPECT_EQ(b->encode(), newer) << "a rollback writes a newer firmware's slots back as they were";

  AnalyticsBucket k;
  k.first = k.last = 3;
  k.count[anx::frames] = 1000;
  k.extra = {7, 8};
  std::vector<uint8_t> kb = Analytics::encodeBucket(k);
  AnalyticsBucket back;
  ASSERT_TRUE(Analytics::decodeBucket(kb.data(), kb.size(), back));
  EXPECT_TRUE(back == k) << "a bucket keeps a newer firmware's slots too";
}

// ---- analytics: day buckets, the acknowledgement, the roll-up ------------------------------

TEST(Analytics, TheServersAckIsReadOrNothingIsDropped) {
  EXPECT_EQ(statsAckOf("{\"ok\":true,\"ack\":17}"), std::optional<uint32_t>(17));
  EXPECT_EQ(statsAckOf("{\"ack\": 3}"), std::optional<uint32_t>(3));
  EXPECT_FALSE(statsAckOf("{\"ok\":true}"));
  EXPECT_FALSE(statsAckOf("{\"ack\":\"x\"}"));
  EXPECT_FALSE(statsAckOf("{\"ack\":99999999999}"));
}

TEST(Analytics, ADayOfPoweredTimeIsABucketStampedWithItsWallTime) {
  Analytics a;
  for (uint32_t s = 0; s < Analytics::kBucketSeconds - 1; ++s) a.second(std::nullopt);
  EXPECT_EQ(a.closedCount(), 0u);
  a.add(anx::hangs);
  a.second(1790000000u);
  ASSERT_EQ(a.closedCount(), 1u);
  std::vector<AnalyticsBucket> b;
  a.forEach([&](const AnalyticsBucket& k, bool) { b.push_back(k); });
  EXPECT_EQ(b[0].first, 1u);
  EXPECT_EQ(b[0].span, Analytics::kBucketSeconds);
  EXPECT_EQ(b[0].poweredStart, 0u);
  EXPECT_EQ(b[0].wallFirst, 1790000000u) << "the wall time it saw";
  EXPECT_EQ(b[0].count[anx::hangs], 1u);
  EXPECT_EQ(b[1].first, 2u) << "the open one";
  EXPECT_EQ(b[1].poweredStart, Analytics::kBucketSeconds);
}

TEST(Analytics, OnlyAnAcknowledgementDropsAndNeverTheOpenBucket) {
  Stats s = sampleFile();
  ASSERT_EQ(s.analytics.closedCount(), 3u);
  EXPECT_FALSE(s.analytics.ack(0));
  EXPECT_TRUE(s.analytics.ack(2));
  EXPECT_EQ(s.analytics.closedCount(), 1u) << "buckets 1 and 2 went, 3 stays";
  EXPECT_TRUE(s.analytics.ack(99));
  EXPECT_EQ(s.analytics.closedCount(), 0u);
  EXPECT_EQ(s.analytics.open().first, 4u) << "the open bucket is never dropped: it is sent again, grown";
}

TEST(Analytics, WeeksOfflineRollUpAndLoseNothing) {
  Analytics a;
  uint64_t added = 0;
  for (uint32_t day = 0; day < 60; ++day) {
    for (uint32_t s = 0; s < Analytics::kBucketSeconds; ++s) {
      if (s % 7200 == 0) a.add(anx::frames, day + 1), a.hist(hist::battle_turns, day), added += day + 2;
      a.second(std::nullopt);
    }
  }
  EXPECT_EQ(a.closedCount() + 1, size_t(Analytics::kMaxBuckets)) << "a fixed budget";
  EXPECT_EQ(total(a), added) << "merged, never dropped";
  std::vector<AnalyticsBucket> b;
  a.forEach([&](const AnalyticsBucket& k, bool) { b.push_back(k); });
  EXPECT_EQ(b.front().first, 1u);
  EXPECT_EQ(b.front().last, 46u) << "the oldest holds the run it merged";
  EXPECT_EQ(b.front().span, 46u * Analytics::kBucketSeconds);
  for (size_t i = 1; i < b.size(); ++i) EXPECT_EQ(b[i].first, b[i - 1].last + 1) << "no gap, no overlap";
  EXPECT_LT(Stats{}.encode().size() + a.encode().size(), 4096u) << "the whole budget fits one block";
}

// ---- counting on the real paths ---------------------------------------------------------

TEST(Count, AKnockKnockFeedsHimAndCountsAsAPressAKnockKnockAndAPellet) {
  feedingtest::Badge b(feedingtest::kHomeUp);
  const LifeTally before = b.dish->tally();
  ASSERT_EQ(b.knock({0, 300}).pellets, 1);
  const LifeCounters& c = b.dish->tally().life;
  EXPECT_EQ(c[stat::presses] - before.life[stat::presses], 1u);
  EXPECT_EQ(c[stat::knock_knocks] - before.life[stat::knock_knocks], 1u);
  EXPECT_EQ(c[stat::pellets] - before.life[stat::pellets], 1u);
  EXPECT_EQ(c[stat::refused_full], before.life[stat::refused_full]) << "he was hungry";
}

TEST(Count, EachRefusalByWhy) {
  feedingtest::Badge b(feedingtest::kHomeUp);
  b.keepPantryFull = false;
  b.hungry(Fx::zero());
  ASSERT_EQ(b.knock({0, 300}).pellets, 1);
  EXPECT_EQ(b.dish->tally().life[stat::refused_full], 1u) << "full: the pellet waits";
  b.dish->habitat().pantry = 0;
  ASSERT_EQ(b.knock({0, 300}).pellets, 0);
  EXPECT_EQ(b.dish->tally().life[stat::refused_empty], 1u) << "the pantry was empty";
}

TEST(Count, AHangASideHoldAndTimeHeld) {
  feedingtest::Badge b(feedingtest::kHomeUp);
  const LifeTally before = b.dish->tally();
  EXPECT_GT(before.life[stat::held], 0u) << "the badge's harness holds him in a hand";
  b.hand.pose = feedingtest::Pose::UpsideDown;
  b.run(2500);
  b.hand.pose = feedingtest::Pose::Held;
  b.run(3000);
  EXPECT_EQ(b.dish->tally().life[stat::hangs] - before.life[stat::hangs], 1u);
  EXPECT_EQ(b.dish->tally().life[stat::fights_opened] - before.life[stat::fights_opened], 1u)
      << "the hang opened fight mode";
}

TEST(Count, AShakeTellsAProphecy) {
  Box box;
  box.boot();
  box.hatch();
  box.run(Thinker::kWarmUpTicks * kTickMs + 1000);
  box.noon();
  const uint32_t shakes = box.stat(stat::shakes), told = box.stat(stat::prophecies);
  box.shake();
  EXPECT_EQ(box.stat(stat::shakes), shakes + 1);
  EXPECT_EQ(box.stat(stat::prophecies), told + 1) << "the trance landed its answer";
}

TEST(Count, PhoneFiredStimuliAreNotTheKidsHands) {
  Box box;
  box.boot();
  box.hatch();
  box.noon();
  const LifeTally before = box.dish->tally();
  box.dish->fire(stim::button);
  box.dish->fire(stim::shake);
  box.run(500);
  EXPECT_EQ(box.stat(stat::presses), before.life[stat::presses]);
  EXPECT_EQ(box.stat(stat::shakes), before.life[stat::shakes]);
}

TEST(Count, AStarvingSpellIsCountedOnceAndTimed) {
  Box box;
  box.boot();
  box.hatch();
  box.noon();
  auto hunger = [&](int tenths) {
    box.creature().inject(driveChem(drive::hunger), Fx::ratio(tenths, 10));
    box.run(1000);
  };
  hunger(9);
  EXPECT_EQ(box.stat(stat::starving_spells), 1u);
  EXPECT_GT(box.stat(stat::starving), 0u);
  hunger(7);   // under the line, not yet fed back down: the same spell
  hunger(9);
  EXPECT_EQ(box.stat(stat::starving_spells), 1u);
  hunger(2);
  const uint32_t timed = box.stat(stat::starving);
  hunger(2);
  EXPECT_EQ(box.stat(stat::starving), timed) << "over";
  hunger(9);
  EXPECT_EQ(box.stat(stat::starving_spells), 2u);
}

TEST(Count, ANapAndItsLength) {
  Box box;
  box.boot();
  box.hatch();
  box.noon();
  const uint32_t naps = box.stat(stat::naps);
  box.feel(faceDown, 60000);   // face down is bedtime, always
  EXPECT_TRUE(box.creature().body().asleep);
  EXPECT_EQ(box.stat(stat::naps), naps + 1);
  EXPECT_GT(box.stat(stat::asleep), 0u);
  EXPECT_EQ(box.stat(stat::face_downs), 1u);
}

// ---- persistence, and how often it is written --------------------------------------------

TEST(Saving, ItSurvivesARebootAndRidesTheKeepsakesSaves) {
  Box box;
  box.boot();
  box.hatch();
  box.noon();
  box.feel(cradled, 5000);
  ASSERT_GT(box.stat(stat::held), 0u);
  ASSERT_TRUE(box.dish->flush());
  const LifeTally before = box.dish->tally();
  box.boot();
  EXPECT_TRUE(same(box.dish->tally(), before)) << "a flush wrote all of it";

  // Two hours: the keepsake saves every five minutes; the stats at most every 30.
  box.store.writes.clear();
  box.run(2 * 3600 * 1000);
  EXPECT_GE(box.store.writes["snap.a"] + box.store.writes["snap.b"], 24);
  EXPECT_LE(box.store.writes[Stats::kFile], 4) << "every 30 minutes, with the keepsake";
  EXPECT_GE(box.store.writes[Stats::kFile], 3);
}

TEST(Saving, ARefusedWriteIsTriedAgainAndLosesNothingCounted) {
  Box box;
  box.boot();
  box.hatch();
  box.noon();
  box.store.refuse = Stats::kFile;
  box.feel(faceDown, 60000);
  EXPECT_TRUE(box.dish->flush()) << "the keepsake's save does not wait on the stats";
  box.store.refuse.clear();
  box.run(100);
  ASSERT_TRUE(box.dish->flush());
  const uint32_t naps = box.stat(stat::naps);
  box.boot();
  EXPECT_EQ(box.stat(stat::naps), naps);
}

// ---- the lineage: an egg carries its parent's and grandparent's lives ---------------------

TEST(Lineage, TheEggCarriesItsParentsAndGrandparentsLives) {
  MemStorage store;
  NullLink link;
  ScriptedOwner owner;
  const Genome founder = shortLived();
  std::optional<Dish> dish;
  dish.emplace(store, 7, kLineage, DishOptions{nullptr, &founder});
  std::optional<LifeTally> gen0;
  uint32_t ms = 0;
  for (; ms < 40 * 3600 * 1000u; ms += kSampleMs) {
    dish->sample(owner.next(ms, *dish), ms);
    dish->tick(ms, link);
    const Occupant& o = dish->occupant();
    if (!gen0 && std::holds_alternative<Clutch>(o)) gen0 = dish->tally();
    if (const Creature* c = std::get_if<Creature>(&o); c && c->generation() == 2) break;
  }
  ASSERT_TRUE(gen0) << "the founder died";
  EXPECT_EQ(gen0->generation, 0);
  EXPECT_EQ(gen0->life[stat::death], 1u + uint32_t(DeathCause::OldAge));
  EXPECT_GT(gen0->life[stat::age], 0u);
  EXPECT_GT(gen0->life[stat::elder_at], gen0->life[stat::adult_at]);
  EXPECT_GT(gen0->life[stat::adult_at], gen0->life[stat::child_at]);
  EXPECT_GT(gen0->life[stat::presses], 0u) << "the owner fed him";
  EXPECT_GT(gen0->life[stat::bites], 0u);
  EXPECT_GT(gen0->life[stat::naps], 0u);
  EXPECT_TRUE(gen0->ancestors.empty()) << "a founder has none";

  const Creature* c = std::get_if<Creature>(&dish->occupant());
  ASSERT_TRUE(c && c->generation() == 2) << "two lives in 40 pet hours";
  const LifeTally& t = dish->tally();
  EXPECT_EQ(t.generation, 2);
  ASSERT_EQ(t.ancestors.size(), 2u);
  EXPECT_EQ(t.ancestors[0].generation, 1);
  EXPECT_EQ(t.ancestors[1].generation, 0);
  EXPECT_EQ(t.ancestors[1].life[stat::death], gen0->life[stat::death]);
  EXPECT_EQ(t.ancestors[1].life[stat::bites], gen0->life[stat::bites]);
  EXPECT_NE(t.ancestors[0].life[stat::death], 0u) << "the parent died too";
  EXPECT_EQ(t.life[stat::death], 0u) << "he lives";

  // A reboot keeps all three lives.
  ASSERT_TRUE(dish->flush());
  const LifeTally before = t;
  dish.emplace(store, 7, kLineage, DishOptions{nullptr, &founder});
  EXPECT_TRUE(same(dish->tally(), before));
}

// An egg was laid but its record never landed (the power went): the boot
// rolls the stored life over, as the lay would have.
TEST(Lineage, AnEggWhoseRecordNeverLandedIsRolledOverAtBoot) {
  LifeTally parent = sample();
  parent.generation = 4;
  const LifeTally child = LifeTally::resume(parent, parent.lineageId, 5);
  EXPECT_EQ(child.generation, 5);
  ASSERT_EQ(child.ancestors.size(), 2u);
  EXPECT_TRUE(child.ancestors[0].life == parent.life);
  EXPECT_EQ(child.ancestors[1].generation, parent.ancestors[0].generation);
  const LifeTally other = LifeTally::resume(parent, parent.lineageId + 1, 5);
  EXPECT_TRUE(other.ancestors.empty()) << "another line's record is not his";
  EXPECT_EQ(other.life[stat::presses], 0u);
}

// ---- nothing in the engine reads them -----------------------------------------------------

// The same day of hands on two dishes, one of which starts from a stats
// record full of numbers and refuses to write it: the replay hash and every
// keepsake byte are the same.
TEST(Determinism, TheStatsAreNeverAnInput) {
  Box plain, seeded;
  Stats junk = sampleFile();
  junk.life.lineageId = kLineage;
  junk.life.generation = 0;
  ASSERT_TRUE(junk.save(seeded.store));
  seeded.store.refuse = Stats::kFile;
  for (Box* b : {&plain, &seeded}) b->boot();
  auto day = [](Box& b) {
    ScriptedOwner owner;
    for (uint32_t end = b.ms + 6 * 3600 * 1000u; b.ms < end; b.ms += kSampleMs) {
      b.dish->sample(owner.next(b.ms, *b.dish), b.ms);
      b.dish->tick(b.ms, b.link);
    }
  };
  day(plain);
  day(seeded);
  EXPECT_NE(plain.dish->tally().life[stat::presses], seeded.dish->tally().life[stat::presses]);
  EXPECT_EQ(plain.dish->hash(), seeded.dish->hash());
  EXPECT_EQ(Keepsake::encode(plain.dish->live()), Keepsake::encode(seeded.dish->live()));
}

// ---- what the phone reads ---------------------------------------------------------------

struct Phone {
  Box box;
  ScriptedLink link;
  std::vector<std::string> send(const std::string& line) {
    link.sent.clear();
    box.dish->protocol().handle(line, *box.dish, link);
    return link.sent;
  }
};

TEST(Protocol, StatsAsLinesAndAsJson) {
  Phone p;
  p.box.boot();
  p.box.hatch();
  p.box.noon();
  p.box.feel(faceDown, 60000);
  const std::vector<std::string> lines = p.send("#7 STATS");
  ASSERT_GE(lines.size(), 3u);
  EXPECT_EQ(lines.front().rfind("#7 + life lineage=00005eed1e55 gen=0 seq=", 0), 0u) << lines.front();
  EXPECT_EQ(lines.back(), "#7 OK lives=1");
  std::string all;
  for (const std::string& l : lines) {
    EXPECT_LE(l.size(), 200u);
    all += l + "\n";
  }
  EXPECT_NE(all.find(" naps="), std::string::npos) << all;
  EXPECT_NE(all.find(" face_downs=1"), std::string::npos);
  EXPECT_NE(all.find(" death=none"), std::string::npos);
  for (const StatInfo& s : LIFE_STATS) EXPECT_NE(all.find(std::string(" ") + s.name), std::string::npos) << s.name;

  const std::vector<std::string> json = p.send("#8 STATS json");
  ASSERT_GE(json.size(), 2u);
  EXPECT_EQ(json.back().rfind("#8 OK ", 0), 0u);
  const std::string doc = lifeStatsJson(*p.box.dish);
  EXPECT_EQ(doc.front(), '{');
  EXPECT_EQ(doc.back(), '}');
  EXPECT_EQ(json.back(), "#8 OK " + std::to_string(doc.size()) + " " + [&] {
    char h[9];
    std::snprintf(h, sizeof h, "%08x", unsigned(crc32(doc.data(), doc.size())));
    return std::string(h);
  }());
  for (const char* part : {"\"schema\":\"blorb.life-stats\"", "\"lineage\":\"00005eed1e55\"", "\"gen\":0",
                           "\"phase\":\"creature\"", "\"stage\":\"baby\"", "\"face_downs\":1", "\"death\":null",
                           "\"asleep_s\":", "\"species\":\"grungo\""})
    EXPECT_NE(doc.find(part), std::string::npos) << part << " in " << doc;
  EXPECT_EQ(doc.find("\"parent\""), std::string::npos) << "a founder has no parent";
  EXPECT_EQ(doc.find("\"mac\""), std::string::npos) << "the phone knows which badge it reads";

  const std::string up = lifeStatsJson(*p.box.dish, StatsIdentity{"d4:05:92:77:1b:44", true});
  EXPECT_NE(up.find("\"mac\":\"d4:05:92:77:1b:44\""), std::string::npos);
  EXPECT_NE(up.find("\"genome\":\""), std::string::npos);

  EXPECT_EQ(p.send("#9 STATS xml"), (std::vector<std::string>{"#9 ERR 400 BAD_ARGS"}));
  EXPECT_NE(doc.find("\"analytics\":{\"powered_s\":"), std::string::npos);
  EXPECT_NE(doc.find("\"open\":true"), std::string::npos);
  EXPECT_EQ(p.send("#a STATS ack 0"), (std::vector<std::string>{"#a OK dropped=0 kept=0"}));
  EXPECT_EQ(p.send("#b STATS ack"), (std::vector<std::string>{"#b ERR 400 BAD_ARGS"}));
}

// Analytics outlive a reboot and leave only when the server says it has them.
TEST(Protocol, AnalyticsWaitForTheirAcknowledgementAcrossReboots) {
  Phone p;
  p.box.boot();
  p.box.run(Analytics::kBucketSeconds * 1000u + 31 * 60000);   // past the next stats save
  ASSERT_EQ(p.box.dish->stats().analytics.closedCount(), 1u) << "a day of powered time";
  const uint32_t powered = p.box.dish->stats().analytics.poweredSeconds();
  p.box.boot();   // no flush: a crash. The last save was at most 30 minutes ago.
  EXPECT_EQ(p.box.dish->stats().analytics.closedCount(), 1u);
  EXPECT_GE(p.box.dish->stats().analytics.poweredSeconds() + 30 * 60, powered);
  EXPECT_EQ(p.box.dish->stats().analytics.open().count[anx::boots], 1u) << "this boot, in the open bucket";
  EXPECT_EQ(p.send("#c STATS ack 1"), (std::vector<std::string>{"#c OK dropped=1 kept=0"}));
  p.box.boot();
  EXPECT_EQ(p.box.dish->stats().analytics.closedCount(), 0u) << "the ack was written at once";
}

TEST(Protocol, ANameIsEscapedInTheJson) {
  Phone p;
  p.box.boot();
  p.box.dish->rename("Bo\"b\\");
  const std::string doc = lifeStatsJson(*p.box.dish);
  EXPECT_NE(doc.find("\"name\":\"Bo\\\"b\\\\\""), std::string::npos) << doc;
  EXPECT_NE(doc.find("\"phase\":\"egg\""), std::string::npos);
}

// ---- what it costs (printed for the report; the bounds are loose, for a slow host) --------

TEST(Cost, TheHotPathIsAnIncrementAndTheWriteIsSmall) {
  using clk = std::chrono::steady_clock;
  feedingtest::Badge b(feedingtest::kHomeUp);
  const Creature& c = b.creature();
  SenseOut out;
  out.fire(stim::knock);
  Stats s;
  constexpr int kN = 200000;
  auto t0 = clk::now();
  for (int i = 0; i < kN; ++i) {   // what one tick asks of the stats, at most
    s.gestures(out, false, int8_t(i & 1) - 1);
    s.lived(c, 1);
    s.analytics.add(anx::frames);
    s.analytics.hist(hist::frame_ms, uint32_t(i % 300));
    if (i % 10 == 0) s.analytics.second(std::nullopt);
  }
  const double perTickNs = std::chrono::duration<double, std::nano>(clk::now() - t0).count() / kN;

  const uint32_t ticks0 = b.dish->tickCount();
  t0 = clk::now();
  b.run(60000);   // a minute held, sampled and ticked as the badge does
  const double dishTickNs =
      std::chrono::duration<double, std::nano>(clk::now() - t0).count() / std::max<uint32_t>(1, b.dish->tickCount() - ticks0);

  Stats full = sampleFile();
  for (uint32_t d = 0; d < 20 * Analytics::kBucketSeconds; ++d) {
    if (d % 60 == 0) full.analytics.add(anx::frames, 3000), full.analytics.hist(hist::frame_ms, d % 250);
    full.analytics.second(std::nullopt);
  }
  MemStorage store;
  t0 = clk::now();
  for (int i = 0; i < 100; ++i) full.save(store);
  const double saveUs = std::chrono::duration<double, std::micro>(clk::now() - t0).count() / 100;
  const size_t bytes = store.files[Stats::kFile].size();
  std::printf("[cost] stats per tick %.0f ns; a whole Dish tick with its samples %.0f ns; "
              "a full file (16 buckets) %zu bytes, encoded and written in %.1f us\n",
              perTickNs, dishTickNs, bytes, saveUs);
  EXPECT_LT(perTickNs, 2000.0);
  EXPECT_LT(bytes, 4096u) << "one LittleFS block";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
