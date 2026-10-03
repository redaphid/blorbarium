#include <gtest/gtest.h>
#include <pthread.h>
#include <cstdio>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "links.h"
#include "mem_storage.h"
#include "scripted_owner.h"
#include "time_sources.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint32_t kWall0 = 1790000000u;
constexpr uint32_t kDaySeconds = 24 * 3600;
constexpr uint64_t kLineage = 0x9f31c2d04a7bull;

Genome withGene(uint8_t type, uint8_t offset, uint8_t value, uint8_t firstByte = 255) {
  Genome g = starterGenome(7);
  GenomeBuilder b = GenomeBuilder::from(g);
  g.forEach([&](const GeneView& v) {
    if (v.header.type == type && (firstByte == 255 || v.body[0] == firstByte)) b.setByte(v.header.uid, offset, value);
  });
  return *b.build();
}

// A one-minute egg, so a creature is a minute away.
Genome quickEgg() { return withGene(GeneKindOf<EggGene>::value, 0, 1); }
// Life halving every 1.5 pet hours instead of 3 days: a whole life in about 5 pet hours.
Genome shortLived() { return withGene(GeneKindOf<ChemGene>::value, 1, 150, chem::life.v); }

// One dish on one store that can be unplugged and plugged back in.
struct Box {
  MemStorage store;
  FakeRtc rtc;
  Genome founder;
  std::optional<Dish> dish;
  NullLink link;
  uint32_t ms = 0;
  uint32_t seed = 7;

  explicit Box(Genome g = quickEgg()) : founder(std::move(g)) { rtc.poweredMs = &ms; }
  Dish& boot() {
    dish.emplace(store, seed, kLineage, DishOptions{&rtc, &founder});
    ms = 0;
    dish->tick(ms, link);
    return *dish;
  }
  void run(uint32_t forMs) { runFor(*dish, link, ms, forMs); }
  void hatch() {
    while (!std::holds_alternative<Creature>(dish->occupant())) run(1000);
  }
  // Pull the plug for `seconds` of wall time; the next boot's RTC reads the time it is now.
  void unplugFor(uint32_t seconds) { rtc.atBoot = *dish->wallNow() + seconds; }
  const Creature& creature() const { return std::get<Creature>(dish->occupant()); }
};

int count(const Lineage& l, size_t kind) {
  int n = 0;
  l.forEach([&](const LineageEntry& e) { n += e.index() == kind; });
  return n;
}
constexpr size_t kDeath = 3;

std::optional<Snapshot> newest(MemStorage& store) {
  MemStorage copy = store;
  Snapshot s;
  SlotState state = Keepsake(copy).load(s);
  if (state != SlotState::Resumed && state != SlotState::FellBack) return std::nullopt;
  return s;
}

// A day in the dish: a pellet, a shake, a cradle and a knock every 90 minutes.
BodySample routine(uint32_t ms) {
  uint32_t t = ms % (90 * 60000);
  if (t < 200) return pressed();
  if (t >= 30000 && t < 30480) return jolt((t / 80) % 2 == 0);
  if (t >= 60000 && t < 64000) return cradled();
  BodySample s = still();
  if (t == 70000) s.tapCode = 1;
  return s;
}

uint32_t dayOfRoutine(Dish& dish) {
  NullLink link;
  for (uint32_t ms = 0; ms < 24 * 3600 * 1000u; ms += kSampleMs) {
    dish.sample(routine(ms), ms);
    dish.tick(ms, link);
  }
  return dish.hash();
}

}  // namespace

// ---- pacing and saving ------------------------------------------------------------------

TEST(Pacing, AtMostTenTicksPerCallAndTicksFollowTime) {
  MemStorage store;
  NullLink link;
  Dish dish(store, 7, kLineage);
  dish.tick(60000, link);
  EXPECT_EQ(dish.tickCount(), 10u) << "a minute behind still runs only ten";
  dish.tick(60000, link);
  EXPECT_EQ(dish.tickCount(), 20u);
  Dish paced(store, 7, kLineage);
  uint32_t ms = 0;
  runFor(paced, link, ms, 10000);
  EXPECT_EQ(paced.tickCount(), 99u) << "50 Hz calls up to 9,980 ms: a tick at every whole 100 ms after 0";
}

TEST(Saving, EveryFiveMinutesAndAtOnceOnAnEvent) {
  Box box(starterGenome(7));
  box.boot();
  ASSERT_EQ(newest(box.store)->seq, 1u) << "a fresh dish saves at once";
  box.run(SavePolicy::kIntervalTicks * kTickMs - 1000);
  EXPECT_EQ(newest(box.store)->seq, 1u) << "nothing happened, so no save yet";
  box.run(2000);
  EXPECT_EQ(newest(box.store)->seq, 2u) << "the five-minute save";
  box.hatch();
  std::optional<Snapshot> s = newest(box.store);
  ASSERT_TRUE(std::holds_alternative<Creature>(s->occupant));
  EXPECT_LT(std::get<Creature>(s->occupant).stats().ageTicks, 10u) << "the hatch saved within the same tick() call";
}

TEST(Saving, ADisconnectSaves) {
  Box box;
  box.boot();
  ScriptedLink phone;
  uint32_t ms = 0;
  runFor(*box.dish, phone, ms, 2000);
  uint32_t seq = newest(box.store)->seq;
  box.dish->tick(ms, box.link);
  EXPECT_EQ(newest(box.store)->seq, seq + 1);
}

// ---- boot paths ---------------------------------------------------------------------------

TEST(Boot, FreshThenResumedToTheSameState) {
  Box box;
  EXPECT_EQ(box.boot().boot(), Boot::Fresh);
  EXPECT_TRUE(std::holds_alternative<Egg>(box.dish->occupant()));
  box.hatch();
  box.run(30000);
  box.dish->flush();
  uint32_t h = box.dish->hash();
  EXPECT_EQ(box.boot().boot(), Boot::Resumed);
  EXPECT_EQ(box.dish->hash(), h);
}

TEST(Boot, ATornNewestSlotFallsBack) {
  Box box;
  box.boot();
  box.run(5000);
  uint32_t h = box.dish->hash();
  box.dish->flush();   // seq 2, snap.b
  box.run(5000);
  box.dish->flush();   // seq 3, snap.a
  box.store.files["snap.a"].resize(40);
  EXPECT_EQ(box.boot().boot(), Boot::FellBack);
  EXPECT_EQ(box.dish->hash(), h);
}

TEST(Boot, BothTornBootsAnEggOfTheLatestGenomeFromTheLineage) {
  Box box;
  box.boot();
  box.hatch();
  Creature& c = std::get<Creature>(box.dish->occupant());
  c.inject(chem::life, Fx::ratio(5, 100));
  box.run(500);
  ASSERT_TRUE(box.dish->pick(0));
  const Egg& egg = std::get<Egg>(box.dish->occupant());
  std::vector<uint8_t> genome = egg.genome().bytes();
  box.store.files["snap.a"][20] ^= 0x5A;
  box.store.files["snap.b"][20] ^= 0x5A;
  EXPECT_EQ(box.boot().boot(), Boot::FromLineage);
  const Egg* back = std::get_if<Egg>(&box.dish->occupant());
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->generation(), 1);
  EXPECT_EQ(back->genome().bytes(), genome);
  EXPECT_EQ(box.store.files.count("rescue/snap.a.0") + box.store.files.count("rescue/snap.b.0"), 2u);
}

TEST(Boot, ANewerFormatRunsReadOnlyAndWritesNothing) {
  Box box;
  box.boot();
  box.run(3000);
  box.dish->flush();
  for (const char* slot : {"snap.a", "snap.b"}) box.store.files[slot][4] = uint8_t(kFormatVersion + 1);
  auto before = box.store.files;
  EXPECT_EQ(box.boot().boot(), Boot::ReadOnlyNewer);
  box.run(SavePolicy::kIntervalTicks * kTickMs + 70000);
  box.dish->flush();
  EXPECT_TRUE(std::holds_alternative<Creature>(box.dish->occupant())) << "the throwaway egg hatched and lives";
  EXPECT_EQ(box.store.files, before);
}

// ---- founding a life --------------------------------------------------------------------

// Items draw behind him, so a marble left where he hatches peeks out between his feet.
TEST(Founding, TheMarbleStartsOnASeededSpotClearOfTheHatchling) {
  std::vector<DishPos> spots;
  for (uint32_t seed : {7u, 8u, 9u}) {
    Box box;
    box.seed = seed;
    box.boot();
    box.hatch();
    DishPos him = box.creature().body().at, m = box.dish->habitat().marble.at;
    Fx dx = m.x - him.x, dy = m.y - him.y;
    EXPECT_GE(dx * dx + dy * dy, Fx::ratio(2, 10)) << "seed " << seed << ": the marble starts at least 0.45 from him";
    EXPECT_LE(m.x * m.x + m.y * m.y, Fx::ratio(1, 2)) << "seed " << seed << ": and well inside the rim";
    spots.push_back(m);
  }
  EXPECT_TRUE(spots[0].x != spots[1].x || spots[0].y != spots[1].y) << "the spot comes from the seed";
  EXPECT_TRUE(spots[1].x != spots[2].x || spots[1].y != spots[2].y);
}

// ---- the replay check -----------------------------------------------------------------------

// The same genome, seed and script give the same Dish hash after 24 pet hours.
// The device prints its own from HASH after the same feed (unit 20).
TEST(Replay, TwentyFourHoursOfTheSameRoutineGiveTheCommittedHash) {
  constexpr uint32_t kCommitted = 0x79bce0d7u;
  MemStorage a, b;
  Dish first(a, 7, kLineage), second(b, 7, kLineage);
  uint32_t h = dayOfRoutine(first);
  EXPECT_EQ(dayOfRoutine(second), h);
  EXPECT_EQ(h, kCommitted) << std::hex << h;
  EXPECT_TRUE(std::holds_alternative<Creature>(first.occupant()));
  EXPECT_GE(std::get<Creature>(first.occupant()).stats().fed, 1u);
}

// Hatch, every stage, death, the clutch pick and the next hatch, with a
// caretaker who only has the toy and a phone that never connects.
TEST(Lifecycle, body_only_full_life) {
  MemStorage store;
  NullLink link;
  ScriptedOwner owner;
  Genome founder = shortLived();
  Dish dish(store, 7, 1, DishOptions{nullptr, &founder});
  uint8_t stages = 0, clutchSize = 0;
  bool died = false, picked = false, nextHatch = false;
  DeathCause cause = DeathCause::Unknown;
  for (uint32_t ms = 0; ms < 16 * 3600 * 1000u && !nextHatch; ms += kSampleMs) {
    dish.sample(owner.next(ms, dish), ms);
    dish.tick(ms, link);
    const Occupant& o = dish.occupant();
    if (const auto* c = std::get_if<Creature>(&o)) {
      if (c->generation() == 0) stages |= uint8_t(1u << uint8_t(c->stage()));
      nextHatch = c->generation() == 1;
    } else if (const auto* k = std::get_if<Clutch>(&o)) {
      died = true;
      cause = k->cause;
      clutchSize = k->count;
    } else {
      picked = picked || std::get<Egg>(o).generation() == 1;
    }
  }
  EXPECT_EQ(stages, 0b1111) << "Baby, Child, Adult and Elder";
  ASSERT_TRUE(died);
  EXPECT_EQ(cause, DeathCause::OldAge);
  EXPECT_EQ(clutchSize, 3) << "reached adult and elder: two more eggs";
  EXPECT_TRUE(picked);
  EXPECT_TRUE(nextHatch);
  EXPECT_GE(owner.counts().presses, 1u);
  EXPECT_EQ(owner.counts().cursorMoves, 1u);
  EXPECT_EQ(dish.lineage().currentGeneration(), 1);
  EXPECT_NE(dish.lineage().legacyFeats() & feat::reached_elder, 0u);
  int births = 0;
  dish.lineage().forEach([&](const LineageEntry& e) {
    if (auto* d = std::get_if<Death>(&e)) {
      EXPECT_GE(d->stats.fed, 1u) << "the button fed him";
    } else if (auto* b = std::get_if<Birth>(&e)) {
      ++births;
      EXPECT_EQ(b->chosen, 1) << "the owner moved the cursor once, then held";
      EXPECT_EQ(b->clutchSize, 3);
    }
  });
  EXPECT_EQ(births, 1);
}

// ---- the loop task's stack and the engine's heap ------------------------------------------

namespace budget {

// Heap the engine holds, counted only on the thread under measure. Storage
// and the phone link stand in for flash and radio, so what they allocate is
// not the engine's and is not counted.
thread_local bool counting = false;
thread_local bool offBudget = false;
std::atomic<size_t> live{0}, peak{0};

struct OffBudget {
  bool was = offBudget;
  OffBudget() { offBudget = true; }
  ~OffBudget() { offBudget = was; }
};

}  // namespace budget

void* operator new(size_t n) {
  size_t* p = static_cast<size_t*>(std::malloc(n + 2 * sizeof(size_t)));
  if (!p) throw std::bad_alloc();
  p[0] = n;
  p[1] = budget::counting && !budget::offBudget;
  if (p[1]) {
    const size_t now = budget::live += n;
    size_t was = budget::peak;
    while (now > was && !budget::peak.compare_exchange_weak(was, now)) {}
  }
  return p + 2;
}
void operator delete(void* q) noexcept {
  if (!q) return;
  size_t* p = static_cast<size_t*>(q) - 2;
  if (p[1]) budget::live -= p[0];
  std::free(p);
}
void* operator new[](size_t n) { return operator new(n); }
void operator delete[](void* q) noexcept { operator delete(q); }
void operator delete(void* q, size_t) noexcept { operator delete(q); }
void operator delete[](void* q, size_t) noexcept { operator delete(q); }

namespace {

struct FlashStorage : MemStorage {
  std::optional<size_t> read(const char* n, size_t at, uint8_t* b, size_t cap) override {
    budget::OffBudget off;
    return MemStorage::read(n, at, b, cap);
  }
  bool writeAtomic(const char* n, const uint8_t* d, size_t len) override {
    budget::OffBudget off;
    return MemStorage::writeAtomic(n, d, len);
  }
  bool append(const char* n, const uint8_t* d, size_t len) override {
    budget::OffBudget off;
    return MemStorage::append(n, d, len);
  }
  bool rename(const char* from, const char* to) override {
    budget::OffBudget off;
    return MemStorage::rename(from, to);
  }
};

struct RadioLink : ScriptedLink {
  std::optional<std::string_view> readLine() override {
    budget::OffBudget off;
    return ScriptedLink::readLine();
  }
  void writeLine(std::string_view line) override {
    budget::OffBudget off;
    ScriptedLink::writeLine(line);
  }
};

// How deep `body` reaches into a thread stack painted with a known byte, past
// what an empty thread uses (glibc keeps its thread block at the stack's top).
size_t stackReach(void* (*body)(void*)) {
  constexpr size_t kSize = 256 * 1024;
  alignas(4096) static uint8_t mem[kSize];
  auto reach = [&](void* (*f)(void*)) {
    std::memset(mem, 0xA5, kSize);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, mem, kSize);
    pthread_t t;
    pthread_create(&t, &attr, f, nullptr);
    pthread_join(t, nullptr);
    pthread_attr_destroy(&attr);
    size_t untouched = 0;
    while (untouched < kSize && mem[untouched] == 0xA5) ++untouched;
    return kSize - untouched;
  };
  return reach(body) - reach([](void*) -> void* { return nullptr; });
}

// What setup() and loop() ask of the engine: a founding boot, a whole life to
// the next hatch with every save and the clutch's dry runs, then a boot that
// decodes that creature and answers the phone's reads. The Dish is static, as
// main.cpp's is.
void* lifeAndReboot(void*) {
  static std::optional<Dish> dish;
  FlashStorage store;
  NullLink quiet;
  ScriptedOwner owner;
  RadioLink phone;
  const Genome founder = shortLived();
  phone.inbound = {"#1 SNAPSHOT", "#2 HASH", "#3 LINEAGE", "#4 ANCESTOR 1", "#5 DIFF 1", "#6 PORTRAIT 0",
                   "#7 STATE", "#8 GENOME", "#9 BRAIN", "#10 CHEM", "#11 SCHEMA", "#12 CLUTCH"};
  budget::live = 0;
  budget::peak = 0;
  budget::counting = true;
  dish.emplace(store, 7, 1, DishOptions{nullptr, &founder});
  for (uint32_t ms = 0; ms < 16 * 3600 * 1000u; ms += kSampleMs) {
    dish->sample(owner.next(ms, *dish), ms);
    dish->tick(ms, quiet);
    const Creature* c = std::get_if<Creature>(&dish->occupant());
    if (c && c->generation() == 1) break;
  }
  dish.emplace(store, 7, 1, DishOptions{nullptr, &founder});
  dish->tick(1000, phone);
  dish.reset();
  budget::counting = false;
  return nullptr;
}

}  // namespace

// The firmware runs these calls on the Arduino loop task, whose stack is
// 8,192 B, in 327,680 B of internal RAM with no PSRAM (DESIGN.md section 8).
// x86-64 frames stand in for the ESP32-S3's here (tools/stack_check.sh
// measures those one function at a time), so the stack bar keeps a quarter
// spare. The heap bar is the engine's line in DESIGN.md section 8. Unit 20
// reads both on the board.
TEST(Budget, ALifeAndARebootFitTheLoopTaskAndTheEngineHeapLine) {
#if defined(__SANITIZE_ADDRESS__)
  GTEST_SKIP() << "ASan's redzones inflate every frame";
#endif
  size_t stack = stackReach(lifeAndReboot);
  std::printf("engine stack high-water: %zu B, engine heap peak: %zu B\n", stack, size_t(budget::peak));
  EXPECT_LT(stack, 6144u);
  EXPECT_LT(size_t(budget::peak), 20u * 1024);
  EXPECT_EQ(size_t(budget::live), 0u) << "the engine frees what it holds";
}

// ---- wall time and the unpowered catch-up (DEVIATIONS.md 3) ---------------------------------

TEST(CatchUp, TenMinutesUnpluggedMovesTheDrivesALittleAndNothingElse) {
  Box box;
  box.rtc.atBoot = kWall0;
  box.boot();
  box.hatch();
  box.run(60000);
  box.dish->flush();
  const Creature before = box.creature();
  const uint32_t tick = box.dish->tickCount();
  const int deaths = count(box.dish->lineage(), kDeath);
  box.unplugFor(600);

  box.boot();
  EXPECT_EQ(box.dish->lastCatchUp().ticks, 6000u);
  EXPECT_EQ(box.dish->lastCatchUp().clampedTicks, 0u);
  EXPECT_EQ(box.dish->tickCount(), tick + 6000);
  const Creature& after = box.creature();
  EXPECT_FALSE(after.dead());
  EXPECT_EQ(after.stage(), before.stage());
  EXPECT_EQ(after.generation(), before.generation());
  EXPECT_EQ(after.stats().ageTicks, before.stats().ageTicks + 6000);
  EXPECT_GT(after.chemistry().drive(drive::boredom), before.chemistry().drive(drive::boredom) + Fx::ratio(1, 100));
  for (const DriveInfo& d : DRIVES) {
    Fx moved = after.chemistry().drive(d.id) - before.chemistry().drive(d.id);
    EXPECT_LT(moved, Fx::ratio(2, 10)) << d.name;
    EXPECT_GT(moved, -Fx::ratio(2, 10)) << d.name;
  }
  EXPECT_EQ(count(box.dish->lineage(), kDeath), deaths);
  EXPECT_TRUE(box.dish->timeKnown());
}

TEST(CatchUp, DaysOfNeglectKillHimAndLeaveAClutchWithOneDeath) {
  Box box;
  box.rtc.atBoot = kWall0;
  box.boot();
  box.hatch();
  box.run(60000);
  box.dish->flush();
  box.unplugFor(6 * kDaySeconds);

  box.boot();
  const Clutch* k = std::get_if<Clutch>(&box.dish->occupant());
  ASSERT_NE(k, nullptr) << "he died while unplugged";
  EXPECT_EQ(k->cause, DeathCause::Starved);
  EXPECT_EQ(k->sinceDeath, Clutch::kVigilTicks) << "the vigil ran out; the choice waits for the owner";
  EXPECT_EQ(count(box.dish->lineage(), kDeath), 1);
  std::optional<Snapshot> saved = newest(box.store);
  ASSERT_TRUE(saved && std::holds_alternative<Clutch>(saved->occupant)) << "saved at once";

  box.boot();
  EXPECT_EQ(box.dish->lastCatchUp().ticks, 0u);
  EXPECT_EQ(count(box.dish->lineage(), kDeath), 1) << "a reboot records no second death";
}

TEST(CatchUp, TheSameWallTimeIsNeverAppliedTwice) {
  Box box;
  box.rtc.atBoot = kWall0;
  box.boot();
  box.hatch();
  box.run(5000);
  box.dish->flush();
  box.unplugFor(3 * 3600);
  box.boot();
  ASSERT_EQ(box.dish->lastCatchUp().ticks, 3u * 3600 * 10);
  const uint32_t tick = box.dish->tickCount(), h = box.dish->hash();

  box.dish->checkWall();
  EXPECT_EQ(box.dish->tickCount(), tick) << "read again at once";
  EXPECT_EQ(box.dish->hash(), h);

  box.boot();
  EXPECT_EQ(box.dish->lastCatchUp().ticks, 0u) << "rebooted from the saved snapshot";
  EXPECT_EQ(box.dish->tickCount(), tick);
  EXPECT_EQ(box.dish->hash(), h);
}

TEST(CatchUp, WithNoSourceNothingIsCaughtUpUntilThePhoneGivesTheTime) {
  Box box;
  box.rtc.atBoot = kWall0;
  box.boot();
  box.hatch();
  box.dish->flush();
  const uint32_t anchorWall = *box.dish->wallNow();
  box.rtc.atBoot.reset();   // the battery went flat while unplugged

  box.boot();
  EXPECT_FALSE(box.dish->timeKnown());
  box.run(60000);
  EXPECT_FALSE(box.dish->timeKnown());
  EXPECT_EQ(box.dish->lastCatchUp().ticks, 0u) << "he carries on as if no time passed";

  ScriptedLink phone;
  phone.inbound.push_back("#7 TIME " + std::to_string(anchorWall + 60 + 2 * 3600));
  box.dish->tick(box.ms - kSampleMs, phone);
  ASSERT_FALSE(phone.sent.empty());
  EXPECT_EQ(phone.sent[0], "#7 OK caught_up=72000") << "two hours, after the powered minute";
  EXPECT_TRUE(box.dish->timeKnown());
}

TEST(CatchUp, AnEggIncubatesThroughTheGapAndHatches) {
  Box box(starterGenome(7));
  box.rtc.atBoot = kWall0;
  box.boot();
  ASSERT_TRUE(std::holds_alternative<Egg>(box.dish->occupant()));
  box.unplugFor(2 * 3600);
  box.boot();
  ASSERT_TRUE(std::holds_alternative<Creature>(box.dish->occupant()));
  EXPECT_GE(box.creature().stats().ageTicks, uint32_t(85 * kTicksPerMinute)) << "hatched at 30 minutes, then lived";
}

TEST(CatchUp, AnAbsenceLongerThanTheCapIsClampedAndRecorded) {
  Box box;
  box.rtc.atBoot = kWall0;
  box.boot();
  box.hatch();
  box.dish->flush();
  box.unplugFor(60 * kDaySeconds);
  box.boot();
  EXPECT_EQ(box.dish->lastCatchUp().ticks, 30u * kDaySeconds * 10);
  EXPECT_EQ(box.dish->lastCatchUp().clampedTicks, 30u * kDaySeconds * 10);
  EXPECT_TRUE(std::holds_alternative<Clutch>(box.dish->occupant()));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
