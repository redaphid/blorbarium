// Shaking hatches the egg, no phone needed (owner, 2026-10-27: "it stays in
// egg form forever until a phone claims it. Nobody is doing that. Just make
// it so shaking it hatches the pets." Then: "Have people need to shake it for
// a while, and have the egg show cracks etc while being shook.").
//
// The firmware's eggs wait on a phone's ADOPT for their clock
// (DishOptions::hatchNeedsPhone), so these run with it set, as the badge
// does: time alone never hatches them; a few seconds of hard shaking does,
// cracking it on the way; a pick-up, a knock or a bump never starts it.
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <variant>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/founding.h"
#include "blorb/friend_egg.h"
#include "blorb/species.h"
#include "debugble/jump.h"
#include "founding_040.h"
#include "links.h"
#include "mem_storage.h"
#include "ota/held_storage.h"
#include "paint/sprite_pack.h"
#include "scripted_owner.h"
#include "time_sources.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint64_t kLineage = 0x5ea4c0ffee11ull;

// A badge as the firmware runs it: the founder's own 15-minute egg, waiting
// on a phone for its clock.
struct Badge {
  MemStorage store;
  FakeRtc rtc;
  Genome founder = starterGenome(7);
  std::optional<Dish> dish;
  NullLink link;
  uint32_t ms = 0;

  Badge() { rtc.poweredMs = &ms; }
  // On the badge's own flash, or `on` (an update's trial holds it).
  Dish& boot(Storage* on = nullptr) {
    DishOptions o{&rtc, &founder};
    o.hatchNeedsPhone = true;
    o.foundingHour = kUnaskedFoundingHour;
    dish.emplace(on ? *on : store, 7, kLineage, o);
    ms = 0;
    dish->tick(ms, link);
    rest(2000);   // lying still a moment: the detectors learn which way is down
    return *dish;
  }
  void at(const BodySample& s) {
    ms += kSampleMs;
    dish->sample(s, ms);
    dish->tick(ms, link);
  }
  void rest(uint32_t forMs) {
    for (uint32_t end = ms + forMs; ms < end;) at(still());
  }
  // A hard shake in a hand: swung both ways along x, a swing every 60 ms.
  void shake(uint32_t forMs) {
    for (uint32_t n = forMs / kSampleMs; n; --n, ++swing) {
      BodySample s;
      s.ax = int16_t(swing / 3 % 2 ? -1500 : 1500), s.az = 1000;
      at(s);
    }
  }
  uint32_t swing = 0;
  bool egg() const { return std::holds_alternative<Egg>(dish->occupant()); }
  const Egg& theEgg() const { return std::get<Egg>(dish->occupant()); }
  Creature& creature() { return std::get<Creature>(dish->occupant()); }
  // Shakes in bursts of `burstMs`, a rest between, until it hatches: the shaking it took, ms.
  uint32_t shakeUntilHatched(uint32_t burstMs, uint32_t restMs = 800) {
    uint32_t shaken = 0;
    while (egg() && shaken < 60000) shake(burstMs), shaken += burstMs, rest(restMs);
    return shaken;
  }
  bool clutch() const { return std::holds_alternative<Clutch>(dish->occupant()); }
  // Ticks with no samples between, as `DEBUG warp` (a fixture's shortcut through a life, never a kid's).
  void warp(uint32_t ticks) {
    for (uint32_t t = 0; t < ticks; ++t) dish->tick(ms += kTickMs, link);
  }
  // Grown up by the debug link's STAGE, the way the clutch tests reach an elder.
  void growTo(Stage s) {
    static Badge* self;
    self = this;
    ASSERT_EQ(debugble::growTo(*dish, s, [](uint32_t t) { self->warp(t); }), debugble::Jump::Done);
    rest(2000);
  }
  // He dies (as debugble::die: of his injuries) and the vigil runs out, lying still.
  void dieAndKeepVigil() {
    creature().fall();
    rest(200);
    ASSERT_TRUE(clutch()) << "he died";
    rest(Clutch::kVigilTicks * kTickMs + 1000);
  }
  // Tilted toward egg `i` as a duel aims (dish_duel.h kAimTilt), BOOT down or not.
  void tilted(uint8_t i, bool down, uint32_t forMs) {
    for (uint32_t end = ms + forMs; ms < end;) {
      BodySample s;
      s.ax = kAimTilt[i].ax, s.ay = kAimTilt[i].ay, s.az = kAimTilt[i].az;
      s.buttonDown = down;
      at(s);
    }
  }
  // Nobody shook it, for `forMs`: it is the same egg, no further on.
  void waitsWithoutAShake(uint32_t forMs = 20 * 60 * 1000) {
    ASSERT_TRUE(egg());
    ASSERT_TRUE(dish->hatchWaits());
    const Fx was = theEgg().progress();
    rest(forMs);
    ASSERT_TRUE(egg()) << "time alone never hatches it";
    EXPECT_EQ(theEgg().progress(), was);
  }
  // Shaken until it hatches, with no phone: a few seconds, and he is out.
  void shakeItOpen() {
    const uint32_t took = shakeUntilHatched(1000);
    ASSERT_FALSE(egg()) << "shaking hatched it";
    EXPECT_LE(took, 8000u);
    EXPECT_FALSE(dish->adopted()) << "no phone ever adopted it";
  }
};

}  // namespace

// ---- the egg's own count --------------------------------------------------------------------

TEST(Egg, kShakeTicksOfShakingHatchIt) {
  Egg e(Offspring{starterGenome(7), {}}, 0, 0);
  for (int i = 0; i < Egg::kShakeTicks - 1; ++i) ASSERT_FALSE(e.shaken(true, true)) << i;
  EXPECT_TRUE(e.shaken(true, true));
  EXPECT_EQ(e.crackStage(), Egg::kCrackStages);
}

TEST(Egg, ACrackHalfMadeClosesGentlyButTheStagesReachedStay) {
  Egg e(Offspring{starterGenome(7), {}}, 0, 0);
  for (int i = 0; i < 4; ++i) e.shaken(true, true);
  EXPECT_EQ(e.crackStage(), 0);
  const Fx made = e.progress();
  e.shaken(false, true);
  EXPECT_LT(e.progress(), made) << "settles";
  EXPECT_GT(e.progress(), made - Fx::ratio(1, 20)) << "gently";
  for (int i = 0; i < 1000; ++i) e.shaken(false, true);
  EXPECT_EQ(e.progress(), Fx::zero()) << "a few shakes short of a crack heal";

  for (int i = 0; i < Egg::kShakeTicks / 2 + 3; ++i) e.shaken(true, true);
  EXPECT_EQ(e.crackStage(), 2);
  for (int i = 0; i < 1000; ++i) e.shaken(false, true);
  EXPECT_EQ(e.crackStage(), 2) << "the cracks a kid made stay";
  EXPECT_EQ(e.progress(), Fx::ratio(1, 2));
}

TEST(Egg, IncubatingByTimeItNeverSettles) {
  Egg e(Offspring{starterGenome(7), {}}, 0, 0);
  for (int i = 0; i < 4; ++i) e.shaken(true, false);
  const Fx made = e.progress();
  for (int i = 0; i < 100; ++i) e.shaken(false, false);
  EXPECT_EQ(e.progress(), made);
}

// ---- on the badge ---------------------------------------------------------------------------

TEST(ShakeHatch, TimeAloneNeverHatchesItAndAPhoneIsNotNeeded) {
  Badge b;
  b.boot();
  ASSERT_TRUE(b.egg());
  ASSERT_TRUE(b.dish->hatchWaits());
  b.rest(20 * 60 * 1000);   // past its 15 minutes
  ASSERT_TRUE(b.egg()) << "the clock still waits on a phone";
  EXPECT_EQ(b.theEgg().progress(), Fx::zero());
  const uint32_t took = b.shakeUntilHatched(1000);
  EXPECT_FALSE(b.egg()) << "shaking hatched it";
  EXPECT_FALSE(b.dish->adopted()) << "with no phone";
  // A few seconds of real shaking, not one shake: never under 3 s, never over 8.
  EXPECT_GE(took, 3000u);
  EXPECT_LE(took, 8000u);
}

TEST(ShakeHatch, OneLongShakeHatchesItInAboutFourSeconds) {
  Badge b;
  b.boot();
  uint32_t shaken = 0;
  while (b.egg() && shaken < 20000) b.shake(100), shaken += 100;
  EXPECT_FALSE(b.egg());
  EXPECT_GE(shaken, 4000u);
  EXPECT_LE(shaken, 5500u);
}

// The egg cracks as it is shaken: the screen sees its progress climb and the
// shake in the hand, and once the shaking stops it settles back no further
// than the last crack made.
TEST(ShakeHatch, TheEggCracksAsItIsShakenAndKeepsItsCracks) {
  Badge b;
  b.boot();
  EXPECT_EQ(b.dish->appearance().eggProgress, Fx::zero());
  b.shake(800);
  Appearance a = b.dish->appearance();
  EXPECT_GT(a.eggProgress, Fx::zero()) << "a crack begins";
  EXPECT_GT(a.eggShaken, Fx::ratio(1, 2)) << "and it jiggles with the hand";
  b.shake(1600);
  const uint8_t stage = b.theEgg().crackStage();
  EXPECT_GE(stage, 2);
  b.rest(60 * 1000);
  EXPECT_EQ(b.theEgg().crackStage(), stage) << "kept";
  EXPECT_EQ(b.dish->appearance().eggShaken, Fx::zero()) << "still now";
  // ...and kept over a replug.
  b.dish->flush();
  b.boot();
  ASSERT_TRUE(b.egg());
  EXPECT_EQ(b.theEgg().crackStage(), stage);
}

// What a hand does to a badge all day: none of it starts the egg cracking.
TEST(ShakeHatch, PickUpsKnocksAndBumpsNeverCrackIt) {
  Badge b;
  b.boot();
  for (int round = 0; round < 20; ++round) {
    // Five short jolts, quiet between: a Shake to the detector, as a pick-up
    // from face down was on the recordings, but no hard shake.
    for (int i = 0; i < 50; ++i) {
      BodySample s;
      s.az = 1000;
      if (i % 4 == 0) s.ax = int16_t(i / 4 % 2 ? -1300 : 1300);
      b.at(s);
    }
    b.rest(700);
    // Bumps in a bag: a jolt up and back down now and then.
    for (int i = 0; i < 6; ++i) b.at(jolt(true)), b.at(jolt(false)), b.rest(300);
    // A knock knock: a sharp tap twice.
    for (int k = 0; k < 2; ++k) {
      BodySample s = still();
      s.az = 2400;
      b.at(s);
      b.rest(200);
    }
    b.rest(1000);
  }
  ASSERT_TRUE(b.egg());
  EXPECT_EQ(b.theEgg().progress(), Fx::zero());
}

// A badge stuck as an egg before this firmware: its egg saved, never
// adopted. The new image boots it as it was, and a shake hatches it.
TEST(ShakeHatch, AnEggSavedWaitingHatchesOnAShakeAfterTheUpdate) {
  Badge b;
  b.boot();
  b.rest(5 * 60 * 1000);
  b.dish->flush();
  EXPECT_EQ(b.boot().boot(), Boot::Resumed);
  ASSERT_TRUE(b.egg());
  b.shakeUntilHatched(1500);
  EXPECT_FALSE(b.egg());
}

// Hatched by a shake, he is a pet like any other: the shell flies off him,
// he gets hungry, a press feeds him, he ages, and he says things.
TEST(ShakeHatch, AShakenHatchlingLivesAsAnyOther) {
  Badge b;
  b.boot();
  b.shakeUntilHatched(1500);
  ASSERT_FALSE(b.egg());
  Appearance a = b.dish->appearance();
  EXPECT_EQ(a.kind, Appearance::Kind::Creature);
  EXPECT_EQ(a.stage, Stage::Baby);
  EXPECT_LT(a.hatchedMs, Appearance::kHatchBurstMs) << "the shell flies off him";
  b.rest(Appearance::kHatchBurstMs);
  EXPECT_EQ(b.dish->appearance().hatchedMs, UINT16_MAX);

  const uint32_t born = b.creature().stats().ageTicks;
  Fx least = Fx::one();
  bool said = false;
  for (int minute = 0; minute < 4 * 60; ++minute) {
    b.rest(60 * 1000);
    said = said || b.dish->appearance().thinking;
    least = std::min(least, b.creature().chemistry().drive(drive::hunger));
  }
  Creature& c = b.creature();
  EXPECT_GT(c.stats().ageTicks, born + 4 * 3600 * 1000 / kTickMs - 100) << "he ages";
  const Fx hungry = c.chemistry().drive(drive::hunger);
  EXPECT_GT(hungry, least) << "he gets hungry";
  EXPECT_TRUE(said) << "he says things";

  const uint16_t fed = c.stats().fed;
  b.at(pressed());   // a press drops a cookie
  b.rest(60 * 1000);
  EXPECT_GT(b.creature().stats().fed, fed) << "a press feeds him";
  EXPECT_LT(b.creature().chemistry().drive(drive::hunger), hungry) << "and he eats it";
}

// The phone stays an option: ADOPT still lets the clock hatch it alone.
TEST(ShakeHatch, AnAdoptedEggStillHatchesOnItsClock) {
  Badge b;
  b.boot();
  ASSERT_TRUE(b.dish->adopt());
  EXPECT_FALSE(b.dish->hatchWaits());
  b.rest(16 * 60 * 1000);
  EXPECT_FALSE(b.egg());
}

// ---- every way a badge comes to hold an egg ---------------------------------------------------
//
// Owner, 2026-10-28: "when the pets are eggs, they don't need to be 'claimed'
// by the phone to hatch." Each test reaches an egg its own way, on the
// badge's options (hatchNeedsPhone), shows time alone leaves it as it is, and
// shakes it open: no ADOPT, no FREEHATCH, no debug link at the egg. (A few
// reach a death through the debug link's STAGE, as the clutch tests do: a
// shortcut through a life, never through the egg.)

// A whole life with only the toy: the founder's egg, every stage, an old-age
// death, the vigil, the kid tilting to an egg and holding BOOT to keep it,
// and that egg. Both eggs wait on a shake; both hatch to one.
TEST(EveryEgg, AWholeLifeWithNoPhoneHatchesBothEggsByShaking) {
  Badge b;
  // test_dish's short life: life halving every 1.5 pet hours, so he is old in about 5.
  {
    GenomeBuilder g = GenomeBuilder::from(b.founder);
    b.founder.forEach([&](const GeneView& v) {
      if (v.header.type == GeneKindOf<ChemGene>::value && v.body[0] == chem::life.v) g.setByte(v.header.uid, 1, 150);
    });
    b.founder = *g.build();
  }
  b.boot();
  ScriptedOwner owner;
  uint32_t eggAt = b.ms;
  int hatchedByShaking = 0, eggs = 1;
  bool died = false, wasEgg = true;
  uint8_t clutchSize = 0;
  for (const uint32_t end = b.ms + 16 * 3600 * 1000u; b.ms < end && hatchedByShaking < 2;) {
    const bool egg = b.egg();
    if (egg && !wasEgg) eggAt = b.ms, ++eggs;
    // Hatched since the last look: in the kid's shaking (or the moment after
    // it, while the swing still turns back), since the owner's hands assert below.
    if (!egg && wasEgg) ++hatchedByShaking;
    wasEgg = egg;
    if (hatchedByShaking == 2) break;
    if (const auto* k = std::get_if<Clutch>(&b.dish->occupant())) died = true, clutchSize = k->count;
    // An egg: the owner's own hands for 20 minutes (a cradle warms it), then a kid shakes it.
    if (!egg || b.ms - eggAt < 20 * 60 * 1000u) {
      b.at(owner.next(b.ms + kSampleMs, *b.dish));
      ASSERT_FALSE(egg && !b.egg()) << "an egg hatched with nobody shaking it";
      continue;
    }
    ASSERT_TRUE(b.dish->hatchWaits());
    b.shake(1000);
    b.rest(500);
  }
  EXPECT_EQ(hatchedByShaking, 2);
  EXPECT_EQ(eggs, 2);
  EXPECT_TRUE(died);
  EXPECT_EQ(clutchSize, 3) << "an elder's clutch: the picker, not a lone egg";
  EXPECT_EQ(owner.counts().picks, 1u) << "the kid chose the egg";
  ASSERT_FALSE(b.egg());
  EXPECT_EQ(b.creature().generation(), 1);
  EXPECT_FALSE(b.dish->adopted());
}

// A pet who dies a child lays one egg, no choice: it waits for a shake.
TEST(EveryEgg, AChildsLoneEggHatchesOnAShake) {
  Badge b;
  b.boot();
  b.shakeItOpen();
  b.growTo(Stage::Child);
  b.dieAndKeepVigil();
  ASSERT_TRUE(b.egg()) << "one egg, laid as the vigil ends";
  EXPECT_EQ(b.theEgg().generation(), 1);
  b.waitsWithoutAShake();
  b.shakeItOpen();
  EXPECT_EQ(b.creature().generation(), 1);
}

// The kid never chooses: the picker's timeout keeps the cursor's egg, which waits for a shake.
TEST(EveryEgg, AClutchNobodyChoosesLaysAnEggThatAShakeHatches) {
  Badge b;
  b.boot();
  b.shakeItOpen();
  b.growTo(Stage::Elder);
  b.dieAndKeepVigil();
  ASSERT_TRUE(b.clutch());
  EXPECT_EQ(std::get<Clutch>(b.dish->occupant()).count, 3);
  b.rest(Clutch::kPickTimeoutTicks * kTickMs + 1000);
  ASSERT_TRUE(b.egg()) << "the timeout chose";
  b.waitsWithoutAShake();
  b.shakeItOpen();
  EXPECT_EQ(b.creature().generation(), 1);
}

// A hug's egg (friend_egg.h) is another lineage's cross: the kid tilts to it,
// holds BOOT to keep it, and a shake hatches it.
TEST(EveryEgg, AFriendsEggTheKidPicksHatchesOnAShake) {
  Badge b;
  b.boot();
  b.shakeItOpen();
  b.growTo(Stage::Elder);
  const Genome theirs = starterGenome(99);
  ASSERT_TRUE(FriendEggStore::save(b.store, FriendEgg{FriendEggRecord{0x5eed5eedull, SpeciesId{0}, 4242, theirs.hash()}, theirs}));
  b.dieAndKeepVigil();
  ASSERT_TRUE(b.clutch());
  const Clutch& k = std::get<Clutch>(b.dish->occupant());
  ASSERT_NE(k.friendSlot, 0) << "the hug's egg joined the clutch";
  const uint8_t slot = uint8_t(k.friendSlot - 1);
  b.tilted(slot, false, 1000);
  b.tilted(slot, true, 1500);
  b.tilted(slot, false, 500);
  ASSERT_TRUE(b.egg()) << "the kid kept an egg";
  EXPECT_FALSE(FriendEggStore::load(b.store)) << "the friend's egg, consumed";
  int chosen = -1;
  b.dish->lineage().forEach([&](const LineageEntry& e) {
    if (const auto* birth = std::get_if<Birth>(&e)) chosen = birth->chosen;
  });
  EXPECT_EQ(chosen, slot);
  b.waitsWithoutAShake();
  b.shakeItOpen();
  EXPECT_EQ(b.creature().generation(), 1);
}

// The keepsake lost (both slots unreadable, or gone): the line goes on from
// the lineage log as an egg of the newest generation, and a shake hatches it.
TEST(EveryEgg, AnEggRebuiltFromTheLineageLogHatchesOnAShake) {
  Badge b;
  b.boot();
  b.shakeItOpen();
  b.growTo(Stage::Child);
  b.dieAndKeepVigil();
  b.shakeItOpen();
  b.dish->flush();
  b.store.files.erase("snap.a");
  b.store.files.erase("snap.b");
  EXPECT_EQ(b.boot().boot(), Boot::FromLineage);
  ASSERT_TRUE(b.egg());
  EXPECT_EQ(b.theEgg().generation(), 1);
  b.waitsWithoutAShake();
  b.shakeItOpen();
  EXPECT_EQ(b.creature().generation(), 1);
}

// Every species a fresh board founds (main.cpp: foundingGenome over its pack's
// bands): its egg waits, and a shake hatches it as that species.
TEST(EveryEgg, EverySpeciesFoundersEggHatchesOnAShake) {
  for (const SpeciesInfo& r : SPECIES) {
    SCOPED_TRACE(r.name);
    Badge b;
    b.founder = foundingGenome(r.id, 0xb10bu + r.id.v, paint::bandsFor(r.id));
    b.boot();
    EXPECT_EQ(b.dish->boot(), Boot::Fresh);
    b.waitsWithoutAShake(2 * 60 * 1000);
    b.shakeItOpen();
    EXPECT_EQ(b.dish->appearance().species, r.id);
  }
}

// One of the eight badges flashed with 0.4.0, still an egg: its founder from
// before species (founding_040.h), a 30-minute egg, saved. Today's image
// boots it as it was, and a shake hatches it.
TEST(EveryEgg, A040FoundersSavedEggHatchesOnAShakeAfterTheUpdate) {
  Badge b;
  b.founder = foundingGenome040(7);
  b.boot();
  b.rest(60 * 1000);
  b.dish->flush();
  b.founder = foundingGenome(species::puffkin, 7, paint::bandsFor(species::puffkin));   // today's founder: unused
  EXPECT_EQ(b.boot().boot(), Boot::Resumed);
  ASSERT_TRUE(b.egg());
  EXPECT_TRUE(predatesSpecies(b.theEgg().genome())) << "the 0.4.0 egg, not a new one";
  b.waitsWithoutAShake(40 * 60 * 1000);
  b.shakeItOpen();
}

// An update's first boot is a trial: the flash is held (ota::HeldStorage), so
// the old image could still read the pet as it left him. The egg hatches on a
// shake all the same; once the trial keeps the image, he is saved.
TEST(EveryEgg, AnEggOnAnUpdatesTrialHatchesOnAShake) {
  Badge b;
  b.boot();
  b.rest(60 * 1000);
  b.dish->flush();
  ota::HeldStorage held(b.store);
  held.hold(true);
  const auto files = b.store.files;
  EXPECT_EQ(b.boot(&held).boot(), Boot::Resumed);
  b.waitsWithoutAShake(2 * 60 * 1000);
  b.shakeItOpen();
  EXPECT_EQ(b.store.files, files) << "a held board writes nothing";
  held.hold(false);   // the trial keeps the image
  EXPECT_TRUE(b.dish->flush());
  b.boot();
  EXPECT_FALSE(b.egg()) << "saved hatched";
}

// Unplugged for days (the RTC kept time), or left lying until the badge naps:
// the egg is lived coarsely as an egg that waits, and a shake still hatches it.
TEST(EveryEgg, DaysAwayAndANapLeaveItWaitingForAShake) {
  Badge b;
  b.rtc.atBoot = 1790000000u;
  b.boot();
  b.rest(60 * 1000);
  b.dish->flush();
  const uint64_t before = b.dish->clock().petTicks;
  b.rtc.atBoot = 1790000000u + 3 * 24 * 3600u;
  b.boot();
  EXPECT_GT(b.dish->clock().petTicks, before + 6 * kTicksPerHour) << "the absence lived (capped)";
  b.waitsWithoutAShake(60 * 1000);
  b.ms += 10 * 60 * 1000;   // a nap: ten minutes with no loop at all
  b.dish->tick(b.ms, b.link);
  b.waitsWithoutAShake(60 * 1000);
  b.shakeItOpen();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
