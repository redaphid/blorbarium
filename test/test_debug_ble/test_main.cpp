// The debug link (lib/debugble, docs/debug-ble.md): its grammar, its gates
// (Harm needs this connection's unlock; Write and Harm wait out a trial, an
// update and a sealed dish), every jump, and that a debug session leaves a
// pet the next boot resumes.
#include <gtest/gtest.h>
#include <cstring>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "debugble/console.h"
#include "debugble/jump.h"
#include "links.h"
#include "mem_storage.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;
using debugble::Console;
using debugble::TimeScale;
using debugble::Unlock;

namespace {

// ota::HeldStorage's rule (lib/ota/include/ota/held_storage.h): reads go
// through, and while held no write lands. Here so this suite links no OTA.
struct Held : Storage {
  Storage& inner;
  bool held = false;
  explicit Held(Storage& s) : inner(s) {}
  void hold(bool h) { held = h; }
  std::optional<size_t> read(const char* n, size_t at, uint8_t* b, size_t cap) override { return inner.read(n, at, b, cap); }
  size_t size(const char* n) override { return inner.size(n); }
  bool writable() override { return !held && inner.writable(); }
  bool writeAtomic(const char* n, const uint8_t* d, size_t len) override { return !held && inner.writeAtomic(n, d, len); }
  bool append(const char* n, const uint8_t* d, size_t len) override { return !held && inner.append(n, d, len); }
  bool rename(const char* f, const char* t) override { return !held && inner.rename(f, t); }
};

constexpr uint64_t kLineage = 0x9f31c2d04a7bull;
constexpr uint32_t kSeed = 7;
constexpr uint32_t kWall0 = 1790000000u;

// A board around one Dish: its storage (held while `trial`), a system clock
// the Dish reads first, and the firmware's warp, all in memory. As on the
// board, `ms` is real time (millis()) and the Dish runs `ahead` of it by
// whatever the warps lived (main.cpp's warpMs).
struct Rig : debugble::Board {
  MemStorage disk;
  Held held{disk};
  uint32_t ms = 0, ahead = 0;
  std::optional<uint32_t> clockAt;
  uint32_t clockSetMs = 0;
  struct Clock : TimeSource {
    Rig* rig;
    explicit Clock(Rig* r) : rig(r) {}
    std::optional<uint32_t> unixSeconds() override {
      if (!rig->clockAt) return std::nullopt;
      return *rig->clockAt + (rig->ms - rig->clockSetMs) / 1000;
    }
  } clock{this};
  Genome founder = starterGenomeOf(kGrungo, kSeed);
  std::optional<Dish> dish;
  std::optional<Console> console;
  NullLink quiet;
  ScriptedLink out;
  bool trial = false, ota = false, sealed = false;
  std::optional<SpeciesId> looked;
  std::optional<std::optional<SpeciesId>> wiped;

  bool onTrial() override { return trial; }
  bool otaActive() override { return ota; }
  void warp(uint32_t ticks) override {
    for (uint32_t done = 0; done < ticks; done += 10) {   // main.cpp's warp: ten ticks a call
      ahead += 10 * kTickMs;
      dish->tick(ms + ahead, quiet);
    }
  }
  void look(std::optional<SpeciesId> s) override { looked = s; }
  std::optional<SpeciesId> looks() override { return looked; }
  void setClock(std::optional<uint32_t> t) override {
    clockAt = t;
    clockSetMs = ms;
  }
  bool wipe(std::optional<SpeciesId> found) override {
    wiped = found;
    return true;
  }

  // A boot after a power cut, as the firmware's: the system clock is unset,
  // millis() starts again, and everything in RAM is new.
  Dish& boot() {
    held.hold(trial);
    ms = ahead = 0;
    clockAt.reset();
    looked.reset();
    DishOptions o{&clock, &founder};
    o.sealed = sealed;
    dish.emplace(held, kSeed, kLineage, o);
    console.emplace(*this);
    dish->tick(ms, quiet);
    return *dish;
  }
  void run(uint32_t forMs) {
    for (uint32_t end = ms + forMs; ms < end; ms += kSampleMs) {
      dish->sample(still(), ms + ahead);
      dish->tick(ms + ahead, quiet);
    }
  }
  // The final reply line to `line` from `connection`.
  std::string send(const std::string& line, uint32_t connection = 1) {
    out.sent.clear();
    console->handle(line, connection, ms, *dish, out);
    return out.sent.empty() ? "" : out.sent.back();
  }
  std::vector<std::string> sendAll(const std::string& line, uint32_t connection = 1) {
    send(line, connection);
    return out.sent;
  }
  // UNLOCK, and a press on the dish once the 1.5 s have passed.
  void unlock(uint32_t connection = 1) {
    ASSERT_EQ(send("#1 UNLOCK", connection), "#1 OK unlock=asking ms=30000");
    ms += Unlock::kMinAskMs;
    ASSERT_TRUE(console->press(ms));
    console->tick(ms, connection, out);
  }
  Creature& creature() { return std::get<Creature>(dish->occupant()); }
};

bool isEgg(const Dish& d) { return std::holds_alternative<Egg>(d.occupant()); }

// ---- the grammar ----------------------------------------------------------------------

TEST(Grammar, RepliesCarryTheRequestIdAndRefuseWhatIsNotALine) {
  Rig r;
  r.boot();
  EXPECT_EQ(r.send("#2b STATUS").substr(0, 22), "#2b OK unlock=locked s");
  EXPECT_EQ(r.send("STATUS").substr(0, 5), "#0 OK");
  EXPECT_EQ(r.send("#xyz STATUS"), "#0 ERR 400 BAD_ID");
  EXPECT_EQ(r.send("#1234567 STATUS"), "#0 ERR 400 BAD_ID");
  EXPECT_EQ(r.send("#5 FEED"), "#5 ERR 404 UNKNOWN_VERB");
  EXPECT_EQ(r.send("#5"), "#5 ERR 400 NO_VERB");
  EXPECT_EQ(r.send("#6 " + std::string(200, 'A')), "#6 ERR 413 TOO_LONG");
  EXPECT_EQ(r.send("#7 STATUS now"), "#7 ERR 400 BAD_ARGS");
}

TEST(Grammar, HelpListsEveryVerbWithItsReach) {
  Rig r;
  r.boot();
  const std::vector<std::string> lines = r.sendAll("#1 HELP");
  ASSERT_EQ(lines.size(), std::size(debugble::VERBS) + 1);
  EXPECT_EQ(lines.back(), "#1 OK " + std::to_string(std::size(debugble::VERBS)));
  EXPECT_EQ(lines[0], "#1 + HELP look ");
  bool wipe = false, think = false;
  for (const std::string& l : lines) {
    wipe = wipe || l == "#1 + WIPE harm [species]";
    think = think || l == "#1 + THINK show <thought>";
  }
  EXPECT_TRUE(wipe);
  EXPECT_TRUE(think);
}

TEST(Grammar, FreeHatchLetsAnEggRunWithoutAPhone) {
  Rig r;
  DishOptions o{&r.clock, &r.founder};
  o.hatchNeedsPhone = true;
  r.dish.emplace(r.held, kSeed, kLineage, o);
  r.console.emplace(r);
  EXPECT_TRUE(r.dish->hatchWaits());
  EXPECT_EQ(r.send("#1 FREEHATCH on"), "#1 OK freehatch=on");
  EXPECT_FALSE(r.dish->hatchWaits());
  EXPECT_EQ(r.send("#2 FREEHATCH off"), "#2 OK freehatch=off");
  EXPECT_TRUE(r.dish->hatchWaits());
  EXPECT_EQ(r.send("#3 FREEHATCH maybe"), "#3 ERR 400 BAD_ARGS");
  r.dish->phoneTime().set(kWall0);   // a phone merely connecting does not lift it
  r.dish->checkWall();
  EXPECT_TRUE(r.dish->hatchWaits());
  EXPECT_TRUE(r.dish->adopt());   // the page's Adopt
  EXPECT_FALSE(r.dish->hatchWaits());
  Dish again(r.held, kSeed, kLineage, o);   // a replug remembers
  EXPECT_FALSE(again.hatchWaits());
}

TEST(Grammar, StatusSaysWhereHeIs) {
  Rig r;
  r.boot();
  EXPECT_EQ(r.send("#1 STATUS"),
            "#1 OK unlock=locked scale=1 trial=0 ota=0 sealed=0 phase=egg stage=- age=0 species=grungo look=own "
            "time=unknown dropped=0 fight=off last=none");
}

// ---- the unlock -----------------------------------------------------------------------

// Every harm verb, each well formed, so only the gate can refuse it.
const char* const kHarm[] = {"HATCH", "STAGE adult", "WARP 5", "CHEM hunger 900", "DIE", "SPECIES gilly", "WIPE"};

TEST(Unlock, EveryHarmVerbIsRefusedLockedAndChangesNothing) {
  Rig r;
  r.boot();
  const uint32_t hash = r.dish->hash();
  for (const char* v : kHarm) EXPECT_EQ(r.send(std::string("#9 ") + v), "#9 ERR 423 LOCKED") << v;
  EXPECT_TRUE(isEgg(*r.dish));
  EXPECT_EQ(r.dish->hash(), hash);
  EXPECT_FALSE(r.wiped);
}

TEST(Unlock, APressAfterOneAndAHalfSecondsOpensHarmToThisConnection) {
  Rig r;
  r.boot();
  EXPECT_EQ(r.send("#1 UNLOCK"), "#1 OK unlock=asking ms=30000");
  r.ms += 1000;
  EXPECT_TRUE(r.console->press(r.ms));   // too soon: the question takes it, and it is no yes
  r.console->tick(r.ms, 1, r.out);
  EXPECT_EQ(r.send("#2 HATCH"), "#2 ERR 423 LOCKED");
  EXPECT_EQ(r.send("#3 UNLOCK"), "#3 OK unlock=asking ms=29000");   // asking again restarts nothing
  r.ms += 600;
  EXPECT_TRUE(r.console->press(r.ms));
  r.out.sent.clear();
  r.console->tick(r.ms, 1, r.out);
  EXPECT_EQ(r.out.sent, std::vector<std::string>{"! DEBUG unlock=open"});
  EXPECT_FALSE(r.console->press(r.ms + 100));   // the question is gone: the pet feels the next press
  EXPECT_EQ(r.send("#4 UNLOCK"), "#4 OK unlock=open");
  EXPECT_EQ(r.send("#5 HATCH").substr(0, 22), "#5 OK phase=creature s");
}

TEST(Unlock, TheQuestionLapsesAfterThirtySeconds) {
  Rig r;
  r.boot();
  r.send("#1 UNLOCK");
  r.ms += Unlock::kAskMs;
  r.out.sent.clear();
  r.console->tick(r.ms, 1, r.out);
  EXPECT_EQ(r.out.sent, std::vector<std::string>{"! DEBUG unlock=lapsed"});
  EXPECT_FALSE(r.console->press(r.ms + 1));
  EXPECT_EQ(r.send("#2 HATCH"), "#2 ERR 423 LOCKED");
}

TEST(Unlock, IsForTheConnectionThatAskedAndEndsWithIt) {
  Rig r;
  r.boot();
  r.unlock(1);
  EXPECT_EQ(r.send("#1 HATCH", 2), "#1 ERR 423 LOCKED");   // another phone, while the first is up
  EXPECT_EQ(r.send("#2 STATUS", 1).substr(0, 18), "#2 OK unlock=open ");
  r.console->tick(r.ms, std::nullopt, r.out);   // the owner's phone drops
  EXPECT_EQ(r.send("#3 HATCH", 1), "#3 ERR 423 LOCKED");
  EXPECT_TRUE(isEgg(*r.dish));
}

TEST(Unlock, LockShutsItAgain) {
  Rig r;
  r.boot();
  r.unlock();
  EXPECT_EQ(r.send("#1 LOCK"), "#1 OK unlock=locked");
  EXPECT_EQ(r.send("#2 HATCH"), "#2 ERR 423 LOCKED");
}

// ---- trial, update, sealed ------------------------------------------------------------

TEST(Gates, OnTrialEveryWriteIsRefusedAndTheFlashStaysAsItWas) {
  Rig r;
  r.boot();
  r.dish->flush();
  r.unlock();
  r.trial = true;   // as an image that went on trial: its store refuses every write
  r.held.hold(true);
  const auto before = r.disk.files;
  for (const char* v : kHarm) EXPECT_EQ(r.send(std::string("#9 ") + v), "#9 ERR 409 TRIAL") << v;
  for (const char* v : {"FIRE knock", "WALL 1790000000", "WALL clear", "SCALE 10"})
    EXPECT_EQ(r.send(std::string("#9 ") + v), "#9 ERR 409 TRIAL") << v;
  EXPECT_EQ(r.send("#a UNLOCK"), "#a ERR 409 TRIAL");
  // Show and Look still answer: they write nothing.
  EXPECT_EQ(r.send("#d LOOK gilly"), "#d OK look=gilly");
  EXPECT_EQ(r.send("#e STATUS").substr(0, 5), "#e OK");
  r.run(60000);
  EXPECT_TRUE(isEgg(*r.dish));
  EXPECT_EQ(r.disk.files, before);
  EXPECT_FALSE(r.wiped);
}

TEST(Gates, DuringAnUpdateEveryWriteIsRefusedAndTheQuestionAndScaleEnd) {
  Rig r;
  r.boot();
  r.unlock();
  EXPECT_EQ(r.send("#1 SCALE 10"), "#1 OK scale=10");
  r.ota = true;
  for (const char* v : kHarm) EXPECT_EQ(r.send(std::string("#9 ") + v), "#9 ERR 409 OTA_ACTIVE") << v;
  EXPECT_EQ(r.send("#2 FIRE knock"), "#2 ERR 409 OTA_ACTIVE");
  r.console->tick(r.ms, 1, r.out);
  EXPECT_EQ(r.console->scale().factor(), 1);
  EXPECT_EQ(r.send("#3 LOCK"), "#3 OK unlock=locked");
  EXPECT_EQ(r.send("#4 UNLOCK"), "#4 ERR 409 OTA_ACTIVE");   // the update's question owns the button
  r.ota = false;
  r.send("#5 UNLOCK");
  r.ota = true;
  r.console->tick(r.ms, 1, r.out);
  EXPECT_FALSE(r.console->unlock().asking());
  EXPECT_TRUE(isEgg(*r.dish));
}

TEST(Gates, ASealedDishTakesNoWrite) {
  Rig r;
  r.sealed = true;
  r.boot();
  ASSERT_TRUE(r.dish->sealed());
  EXPECT_EQ(r.send("#1 UNLOCK"), "#1 ERR 409 SEALED");
  for (const char* v : kHarm) EXPECT_EQ(r.send(std::string("#9 ") + v), "#9 ERR 409 SEALED") << v;
  EXPECT_EQ(r.send("#2 FIRE knock"), "#2 ERR 409 SEALED");
  EXPECT_TRUE(r.disk.files.empty());
}

// ---- the jumps ------------------------------------------------------------------------

TEST(Jumps, HatchThenEachStageInTurn) {
  Rig r;
  r.boot();
  r.unlock();
  EXPECT_EQ(r.send("#1 HATCH").substr(0, 34), "#1 OK phase=creature stage=baby ag");
  EXPECT_EQ(r.send("#2 HATCH"), "#2 ERR 409 NOT_EGG");
  for (const char* s : {"child", "adult", "elder"}) {
    EXPECT_EQ(r.send(std::string("#3 STAGE ") + s).substr(0, 27 + std::strlen(s)),
              std::string("#3 OK phase=creature stage=") + s);
    EXPECT_LT(r.creature().chemistry().drive(drive::hunger), Fx::ratio(1, 20));   // settled, then 20 ticks of life
  }
  EXPECT_EQ(r.send("#4 STAGE adult"), "#4 ERR 409 PAST_STAGE");
  EXPECT_EQ(r.send("#5 STAGE ancient"), "#5 ERR 400 BAD_ARGS");
}

// A pet who dies on the way to the stage asked for is said to have died.
TEST(Jumps, AStageHeDoesNotLiveToSeeSaysHeDied) {
  Rig r;
  r.boot();
  r.unlock();
  r.send("#1 HATCH");
  EXPECT_EQ(r.send("#2 CHEM toxin 1000 injury 1000"), "#2 OK 2");
  EXPECT_EQ(r.send("#3 STAGE elder"), "#3 ERR 409 DIED");
  EXPECT_FALSE(std::holds_alternative<Creature>(r.dish->occupant())) << "the jump stopped at his death";
}

TEST(Jumps, StageFromAnEggHatchesHimFirst) {
  Rig r;
  r.boot();
  r.unlock();
  EXPECT_EQ(r.send("#1 STAGE adult").substr(0, 33), "#1 OK phase=creature stage=adult ");
}

TEST(Jumps, WarpLivesThatManyMinutes) {
  Rig r;
  r.boot();
  r.unlock();
  r.send("#1 HATCH");
  const uint32_t age = r.creature().stats().ageTicks;
  EXPECT_EQ(r.send("#2 WARP 10").substr(0, 5), "#2 OK");
  EXPECT_EQ(r.creature().stats().ageTicks, age + 10 * kTicksPerMinute);
  EXPECT_EQ(r.send("#3 WARP 0"), "#3 ERR 416 OUT_OF_RANGE");
  EXPECT_EQ(r.send("#4 WARP 121"), "#4 ERR 416 OUT_OF_RANGE");
}

TEST(Jumps, DieShowsTheClutchOrStopsAtTheRemains) {
  for (bool eggs : {true, false}) {
    Rig r;
    r.boot();
    r.unlock();
    r.send("#1 STAGE adult");
    const std::string reply = r.send(eggs ? "#2 DIE" : "#2 DIE remains");
    const Clutch* k = std::get_if<Clutch>(&r.dish->occupant());
    ASSERT_NE(k, nullptr) << reply;
    EXPECT_EQ(k->cause, DeathCause::Injured);
    EXPECT_EQ(k->sinceDeath >= Clutch::kVigilTicks, eggs);
    if (eggs) {
      EXPECT_EQ(k->previewed, k->count);
    }
    EXPECT_EQ(reply.substr(0, 18), "#2 OK phase=clutch");
    EXPECT_EQ(r.send("#3 DIE"), "#3 ERR 409 NO_CREATURE");
    bool died = false;
    r.dish->lineage().forEach([&](const LineageEntry& e) { died = died || std::holds_alternative<Death>(e); });
    EXPECT_TRUE(died);
  }
}

TEST(Jumps, ChemSetsEveryPairOrNone) {
  Rig r;
  r.boot();
  r.unlock();
  r.send("#1 HATCH");
  EXPECT_EQ(r.send("#2 CHEM hunger 900 fear 0 adrenaline 500"), "#2 OK 3");
  const Chemistry& c = r.creature().chemistry();
  EXPECT_EQ(c.drive(drive::hunger), Fx::ratio(900, 1000));
  EXPECT_EQ(c.drive(drive::fear), Fx::zero());
  EXPECT_EQ(c.chem[chem::adrenaline.v], Fx::ratio(500, 1000));
  const uint32_t hash = r.dish->hash();
  EXPECT_EQ(r.send("#3 CHEM hunger 100 nonsense 5"), "#3 ERR 416 OUT_OF_RANGE");
  EXPECT_EQ(r.send("#4 CHEM hunger 1001"), "#4 ERR 400 BAD_ARGS");
  EXPECT_EQ(r.send("#5 CHEM hunger"), "#5 ERR 400 BAD_ARGS");
  EXPECT_EQ(r.send("#6 CHEM"), "#6 ERR 400 BAD_ARGS");
  EXPECT_EQ(r.dish->hash(), hash);
}

TEST(Jumps, SpeciesChangesHisBodyAndPowerInPlaceAndTheLineageSaysSo) {
  Rig r;
  r.boot();
  r.unlock();
  EXPECT_EQ(r.send("#1 SPECIES gilly"), "#1 ERR 409 NO_CREATURE");
  r.send("#2 STAGE child");
  const uint32_t age = r.creature().stats().ageTicks;
  EXPECT_EQ(r.send("#3 SPECIES gilly"), "#3 OK species=gilly");
  const SpeciesTraits& t = r.creature().phenotype().species;
  EXPECT_EQ(t.body, SpeciesId{3});
  EXPECT_EQ(t.power, speciesInfo(SpeciesId{3}).power);
  EXPECT_EQ(r.creature().stats().ageTicks, age);
  EXPECT_EQ(r.creature().stage(), Stage::Child);
  EXPECT_EQ(r.dish->appearance().species, SpeciesId{3});
  EXPECT_EQ(r.send("#4 SPECIES dragon"), "#4 ERR 416 OUT_OF_RANGE");
  EXPECT_EQ(r.send("#5 SPECIES 9"), "#5 OK species=octopus");
  EXPECT_EQ(r.dish->appearance().species, SpeciesId{9});
  r.boot();   // the genome is the save's: he comes back an octopus
  EXPECT_EQ(r.dish->boot(), Boot::Resumed);
  EXPECT_EQ(r.dish->appearance().species, SpeciesId{9});
}

TEST(Jumps, SpeciesNeedsASpeciesGene) {
  Rig r;
  r.founder = starterGenome(kSeed);   // a line founded before species
  r.boot();
  r.unlock();
  r.send("#1 HATCH");
  EXPECT_EQ(r.send("#2 SPECIES gilly"), "#2 ERR 404 NO_SPECIES_GENE");
}

TEST(Jumps, WipeAsksTheBoardWithTheFoundingSpecies) {
  Rig r;
  r.boot();
  r.unlock();
  EXPECT_EQ(r.send("#1 WIPE dragon"), "#1 ERR 416 OUT_OF_RANGE");
  EXPECT_FALSE(r.wiped);
  EXPECT_EQ(r.send("#2 WIPE mossby"), "#2 OK wiping found=mossby");
  ASSERT_TRUE(r.wiped);
  EXPECT_EQ(*r.wiped, std::optional<SpeciesId>(SpeciesId{1}));
  EXPECT_EQ(r.send("#3 WIPE"), "#3 OK wiping found=nest");
  ASSERT_TRUE(r.wiped);
  EXPECT_EQ(*r.wiped, std::nullopt);
}

// ---- what needs no press --------------------------------------------------------------

TEST(Open, ThoughtsAndFortunesNeedNoPressAndSaveNothing) {
  Rig r;
  r.boot();
  r.unlock();
  r.send("#1 HATCH");
  r.send("#1 LOCK");
  const uint32_t hash = r.dish->hash();
  EXPECT_EQ(r.send("#2 THINK crunch"), "#2 OK think=crunch prophecy=0");
  EXPECT_EQ(r.dish->appearance().thinking, true);
  EXPECT_EQ(r.send("#3 THINK p_feast"), "#3 OK think=p_feast prophecy=1");
  EXPECT_EQ(r.dish->appearance().prophecy, true);
  EXPECT_EQ(r.send("#4 FORTUNE p_snack"), "#4 OK fortune=p_snack");
  EXPECT_EQ(r.send("#5 FORTUNE crunch"), "#5 ERR 416 OUT_OF_RANGE");
  EXPECT_EQ(r.send("#6 FORTUNE").substr(0, 16), "#6 OK fortune=p_");
  EXPECT_EQ(r.send("#7 THINK 3"), "#7 OK think=crunch prophecy=0");
  EXPECT_EQ(r.send("#8 THINK nothing"), "#8 ERR 416 OUT_OF_RANGE");
  EXPECT_EQ(r.dish->hash(), hash);
}

TEST(Open, LookDrawsAnotherSpeciesAndTheSaveNeverSeesIt) {
  Rig r;
  r.boot();
  const uint32_t hash = r.dish->hash();
  EXPECT_EQ(r.send("#1 LOOK tinker"), "#1 OK look=tinker");
  EXPECT_EQ(r.looked, std::optional<SpeciesId>(SpeciesId{8}));
  EXPECT_EQ(r.send("#2 STATUS").find("look=tinker") != std::string::npos, true);
  EXPECT_EQ(r.send("#3 LOOK off"), "#3 OK look=own");
  EXPECT_EQ(r.looked, std::nullopt);
  EXPECT_EQ(r.send("#4 LOOK dragon"), "#4 ERR 416 OUT_OF_RANGE");
  EXPECT_EQ(r.dish->hash(), hash);
}

TEST(Open, FireTakesAnyStimulusEvenTheBodysOwn) {
  Rig r;
  r.boot();
  r.unlock();
  r.send("#1 HATCH");
  r.send("#1 LOCK");
  const unsigned pantry = r.dish->habitat().pantry;
  EXPECT_EQ(r.send("#2 FIRE button"), "#2 OK fire=button");   // TWIST stimulus refuses this one: BODY_ONLY
  r.run(200);
  EXPECT_EQ(unsigned(r.dish->habitat().pantry), pantry - 1);   // a pellet left the pantry for the dish
  EXPECT_EQ(r.send("#3 FIRE 0"), "#3 OK fire=knock");
  EXPECT_EQ(r.send("#4 FIRE sneeze"), "#4 ERR 416 OUT_OF_RANGE");
}

TEST(Open, WallSetsTheTimeWithoutLivingAGapAndClearForgetsIt) {
  Rig r;
  r.boot();
  r.unlock();
  r.send("#1 HATCH");
  r.dish->phoneTime().set(kWall0);
  r.dish->checkWall();
  ASSERT_TRUE(r.dish->timeKnown());
  const uint32_t age = r.creature().stats().ageTicks;
  // Three days on. TIME would live twelve hours of them; WALL lives none.
  EXPECT_EQ(r.send("#2 WALL 1790259200 60"), "#2 OK time=known wall=1790259200 tz=60 night=0");
  EXPECT_EQ(r.creature().stats().ageTicks, age);
  EXPECT_EQ(r.dish->lastCatchUp().ticks, 0u);
  EXPECT_EQ(r.dish->wallNow(), std::optional<uint32_t>(1790259200));
  EXPECT_EQ(r.clockAt, std::optional<uint32_t>(1790259200));
  EXPECT_EQ(r.send("#3 WALL clear"), "#3 OK time=unknown");
  r.run(1000);
  EXPECT_FALSE(r.dish->timeKnown());
  EXPECT_FALSE(r.dish->live().wall);
  EXPECT_EQ(r.send("#4 WALL 1000"), "#4 ERR 416 OUT_OF_RANGE");
  EXPECT_EQ(r.send("#5 WALL soon"), "#5 ERR 400 BAD_ARGS");
  EXPECT_EQ(r.send("#6 WALL 1790259200 900"), "#6 ERR 400 BAD_ARGS");
}

// ---- the time scale -------------------------------------------------------------------

// The firmware's loop at the given scale: real time, plus the scale's owed
// ticks run as warps, budgeted per loop as main.cpp does.
void live(Rig& r, uint32_t forMs, uint32_t connection = 1) {
  for (uint32_t end = r.ms + forMs; r.ms < end;) {
    r.ms += 40;
    r.dish->sample(still(), r.ms + r.ahead);
    r.dish->tick(r.ms + r.ahead, r.quiet);
    const uint32_t owed = r.console->scale().owed(r.ms);
    r.warp(owed / 10 * 10);
    r.console->scale().ran(owed / 10 * 10);
    r.console->tick(r.ms, connection, r.out);
  }
}

TEST(Scale, SixtyTimesLivesSixtyTimesTheTicksAndNoClockJumps) {
  Rig r;
  r.boot();
  r.unlock();
  r.send("#1 HATCH");
  r.send("#1 LOCK");
  EXPECT_EQ(r.send("#2 SCALE 60"), "#2 OK scale=60");
  const uint32_t age = r.creature().stats().ageTicks, tick = r.dish->tickCount(), clock = r.ms + r.ahead;
  live(r, 10000);
  const uint32_t lived = r.creature().stats().ageTicks - age;
  // Every tick he lived is one the Dish counted; none came from a jump.
  EXPECT_EQ(lived, r.dish->tickCount() - tick);
  EXPECT_GE(lived, 59u * 100);
  EXPECT_LE(lived, 60u * 100 + 30);   // each warp runs whole tens
  EXPECT_GE(r.ms + r.ahead - clock, 59u * 10000);   // the loop's clock ran forward the same way, and only forward
  EXPECT_EQ(r.send("#3 SCALE 61"), "#3 ERR 416 OUT_OF_RANGE");
  EXPECT_EQ(r.send("#4 SCALE 0"), "#4 ERR 416 OUT_OF_RANGE");
  EXPECT_EQ(r.send("#5 SCALE 1"), "#5 OK scale=1");
  const uint32_t before = r.creature().stats().ageTicks;
  live(r, 10000);
  EXPECT_EQ(r.creature().stats().ageTicks - before, 100u);
}

TEST(Scale, EndsWithTheConnectionThatSetIt) {
  Rig r;
  r.boot();
  r.send("#1 SCALE 10", 1);
  r.console->tick(r.ms, 1, r.out);
  EXPECT_EQ(r.console->scale().factor(), 10);
  r.console->tick(r.ms, 2, r.out);   // another phone now holds the dish
  EXPECT_EQ(r.console->scale().factor(), 1);
  r.send("#2 SCALE 10", 2);
  r.console->tick(r.ms, std::nullopt, r.out);
  EXPECT_EQ(r.console->scale().factor(), 1);
}

TEST(Scale, ASlowLoopDropsWhatItOwesPastAMinute) {
  TimeScale s;
  s.set(60, 0, 1);
  EXPECT_EQ(s.owed(1000), 590u);
  EXPECT_EQ(s.owed(2000), TimeScale::kMaxOwedTicks);
  EXPECT_EQ(s.dropped(), 580u);
  s.ran(600);
  EXPECT_EQ(s.owed(2050), 29u);   // 50 ms at 59 extra, the remainder carried
}

// ---- a debug session, then a reboot --------------------------------------------------

TEST(Reboot, EveryJumpLeavesAPetTheNextBootResumes) {
  Rig r;
  r.boot();
  r.unlock();
  r.send("#1 WALL 1790000000");
  ASSERT_EQ(r.send("#2 STAGE adult").substr(0, 5), "#2 OK");
  ASSERT_EQ(r.send("#3 CHEM hunger 700 pain 200"), "#3 OK 2");
  ASSERT_EQ(r.send("#4 SPECIES fizz"), "#4 OK species=fizz");
  r.send("#5 FIRE petted");
  r.send("#6 THINK crunch");
  r.send("#7 LOOK hushwing");
  r.send("#8 SCALE 60");
  live(r, 5000);
  ASSERT_EQ(r.send("#9 WARP 3").substr(0, 5), "#9 OK");
  r.dish->flush();
  const uint32_t hash = r.dish->hash(), age = r.creature().stats().ageTicks;

  r.boot();   // the power comes back: nothing of the debug session but the pet itself
  ASSERT_EQ(r.dish->boot(), Boot::Resumed);
  EXPECT_EQ(r.dish->hash(), hash);
  EXPECT_EQ(r.creature().stage(), Stage::Adult);
  EXPECT_EQ(r.creature().stats().ageTicks, age);
  EXPECT_EQ(r.dish->appearance().species, SpeciesId{6});
  EXPECT_EQ(r.console->scale().factor(), 1);
  EXPECT_EQ(r.send("#a STATUS").substr(0, 21), "#a OK unlock=locked s");
  r.run(60000);   // and he lives on from there
  EXPECT_TRUE(std::holds_alternative<Creature>(r.dish->occupant()));

  r.unlock();
  ASSERT_EQ(r.send("#b DIE").substr(0, 18), "#b OK phase=clutch");
  const uint32_t clutchHash = r.dish->hash();
  r.boot();
  ASSERT_EQ(r.dish->boot(), Boot::Resumed);
  EXPECT_EQ(r.dish->hash(), clutchHash);
  ASSERT_TRUE(std::holds_alternative<Clutch>(r.dish->occupant()));
  EXPECT_TRUE(r.dish->pick(0));   // the clutch still hatches a child of the line
  r.run(1000);
  EXPECT_EQ(std::get<Egg>(r.dish->occupant()).generation(), 1);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
