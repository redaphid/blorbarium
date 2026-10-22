// The first hours of a fresh pet on a board with no phone, no battery and no
// wall time (the owner on a plane, a kid in a car): founded from a species
// starter, driven at 50 Hz through the Dish exactly as main.cpp does, with
// main.cpp's founding hour and an RTC that never knows the time. A seated owner glances
// at him every few minutes and tilts, picks up, taps or presses BOOT for a
// pellet when the rim hint says so; or nobody touches him at all. Every run
// logs what the screen shows (lines, faces, sleep, bites, hops) and sums it
// into the numbers docs/notes/first-hours.md tracks.
//
// The floors below are what that note settled on. The sweep that measured
// them runs with FIRST_HOURS_SWEEP=1 (all nine species, FIRST_HOURS_SEEDS
// seeds, both owners), and FIRST_HOURS_OUT=<dir> writes each run's timeline.
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/registry.h"
#include "blorb/species.h"
#include "blorb/founding.h"
#include "founding_040.h"
#include "links.h"
#include "mem_storage.h"
#include "time_sources.h"
#include "traces.h"
#include "paint/sprite_pack.h"

using namespace blorb;
using namespace blorbtest;

namespace {

constexpr uint32_t kMinuteMs = 60000;
constexpr uint32_t kTicksPerSec = 1000 / kTickMs;

// Tucked: face down on the tray table the whole time (lidded: bedtime).
enum class Owner : uint8_t { Nobody, Seated, Tucked };
const char* ownerName(Owner o) { return o == Owner::Nobody ? "nobody" : o == Owner::Seated ? "seated" : "tucked"; }

// A seated passenger: glances every 3 to 10 minutes and does one gentle thing.
// Never a shake, a drop or a face-down tuck.
class Passenger {
 public:
  explicit Passenger(uint64_t seed) : rng_(Rng::seeded(seed ^ 0x9A55E6E7ull)) {}
  struct Counts { uint32_t glances = 0, presses = 0, holds = 0, tilts = 0, taps = 0; };

  BodySample next(uint32_t ms, const Dish& dish, const Appearance& a) {
    if (ms >= until_) choose(ms, dish, a);
    BodySample s = still();
    switch (g_) {
      case G::Press: s.buttonDown = true; break;
      case G::Hold: s = cradled(); break;
      case G::Tilt: s.ax = tiltX_, s.ay = tiltY_, s.az = 975; break;
      case G::Tap:
        if (!tapped_) s.tapCode = 1, tapped_ = true;
        break;
      case G::Rest: break;
    }
    return s;
  }
  const Counts& counts() const { return n_; }

 private:
  enum class G : uint8_t { Rest, Press, Hold, Tilt, Tap };
  void act(G g, uint32_t ms, uint32_t forMs) { g_ = g, until_ = ms + forMs, tapped_ = false; }
  uint32_t between(uint32_t lo, uint32_t hi) { return lo + rng_.below(hi - lo + 1); }

  void choose(uint32_t ms, const Dish& dish, const Appearance& a) {
    if (g_ != G::Rest) return act(G::Rest, ms, 1500);   // every gesture ends in a release
    if (taps_ > 0) {
      --taps_;
      ++n_.taps;
      return act(G::Tap, ms, 900);
    }
    if (!glanced_) {
      glanced_ = true;
      return act(G::Rest, ms, between(3, 10) * kMinuteMs);
    }
    glanced_ = false;
    ++n_.glances;
    if (std::holds_alternative<Egg>(dish.occupant())) {
      if (rng_.below(2) == 0) return hold(ms, between(20, 60));
      return act(G::Rest, ms, 1000);
    }
    const Creature* c = std::get_if<Creature>(&dish.occupant());
    if (!c) return act(G::Rest, ms, 1000);
    const bool pelletDown = dish.habitat().nearestPellet(DishPos{}).has_value();
    if (a.hasHint && a.hint == care::feed && !pelletDown && dish.habitat().pantry > 0) return press(ms);
    if (c->body().asleep && rng_.below(10) < 7) return act(G::Rest, ms, 1000);   // let him sleep
    if (a.hasHint && (a.hint == care::cuddle || a.hint == care::comfort)) return hold(ms, between(8, 20));
    if (a.hasHint && a.hint == care::play) return tap(ms);
    switch (rng_.below(10)) {
      case 0: case 1: case 2: case 3: return tilt(ms);
      case 4: case 5: case 6: return hold(ms, between(5, 15));
      case 7: case 8: return tap(ms);
      default: return press(ms);   // a kid presses the only button there is
    }
  }
  void press(uint32_t ms) { ++n_.presses, act(G::Press, ms, 200); }
  void hold(uint32_t ms, uint32_t s) { ++n_.holds, act(G::Hold, ms, s * 1000); }
  void tap(uint32_t ms) {
    taps_ = uint8_t(between(1, 3));
    act(G::Rest, ms, 10);
  }
  void tilt(uint32_t ms) {
    ++n_.tilts;
    const int16_t lean = int16_t(150 + rng_.below(120));   // 9 to 16 degrees: a good lean, under "held"
    tiltX_ = rng_.below(2) ? lean : int16_t(-lean);
    tiltY_ = int16_t(int(rng_.below(201)) - 100);
    act(G::Tilt, ms, between(4, 10) * 1000);
  }

  Rng rng_;
  G g_ = G::Rest;
  uint32_t until_ = 0;
  bool glanced_ = false, tapped_ = false;
  uint8_t taps_ = 0;
  int16_t tiltX_ = 0, tiltY_ = 0;
  Counts n_;
};

struct Report {
  std::string species;
  uint32_t seed = 0;
  Owner owner = Owner::Nobody;
  double hours = 0;
  double hatchMin = -1;
  uint32_t thoughts = 0, distinctLines = 0, distinctRows = 0, topLineCount = 0;
  double longestThoughtGapMin = 0;     // awake, hatched: the longest stretch between two lines
  double longestAsleepGapMin = 0;      // asleep: the longest stretch with no line, from falling asleep
  uint32_t visible = 0;                // lines, face changes, hops, sleep edges, bites
  double longestQuietMin = 0;          // the longest stretch with none of those
  double asleepH = 0, longestSleepMin = 0, firstSleepMin = -1;
  uint32_t naps = 0, faceChanges = 0, distinctFaces = 0, reflexes = 0, bites = 0, rottenBites = 0;
  uint32_t pellets = 0;
  double hungerMax = 0, starvingMin = 0, injuryMax = 0, sickMin = 0;
  double boredomMean = 0, lonelyMean = 0, touchMean = 0, sleepyMean = 0, hungerMean = 0;
  bool died = false;
  std::string cause;
  std::string topLine;
  std::map<std::string, uint32_t> actionTicks;
  Passenger::Counts care;
  std::vector<std::string> timeline;
  // Per hour of powered time: minutes asleep, lines said awake and asleep.
  double asleepMinByHour[48]{};
  uint32_t awakeLinesByHour[48]{}, asleepLinesByHour[48]{};
};

// A badge flashed with 0.4.0 on 2026-10-15: founded from foundingGenome040
// with the pet clock at 00:00, lived `adoptAfterH` on 0.4.0, then updated
// over the air and adopted to its table species (Dish::adoptSpecies). On
// 0.7.0 the species founder still had the Gift recipe's sleep, energy, face
// and egg bytes (giftSleep); from 0.7.1 adoption writes FirstHours'.
struct FieldPet { double adoptAfterH = 1.0; bool giftSleep = false, retune = false; };

// The recipe bytes FirstHours retunes, as the species' Gift recipe has them.
void keepGiftRecipe(Dish& dish, SpeciesId species, uint32_t seed) {
  GenomeBuilder gift, first;
  authorStarter(gift, speciesInfo(species).starter, seed, kNeglectCanKill, Recipe::Gift);
  authorStarter(first, speciesInfo(species).starter, seed, kNeglectCanKill, Recipe::FirstHours);
  const Genome g = *gift.build(), f = *first.build();
  struct Edit { GeneUid uid; uint8_t at, value; };
  std::vector<Edit> edits;
  g.forEach([&](const GeneView& v) {
    const std::optional<GeneView> w = f.find(v.header.uid);
    for (uint8_t i = 0; w && i < v.header.len; ++i)
      if (w->body[i] != v.body[i]) edits.push_back({v.header.uid, i, v.body[i]});
  });
  for (const Edit& e : edits) dish.editGene(e.uid, e.at, e.value);
}

double pm(Fx v) { return double(v.raw) / double(Fx::kOne); }

Report live(SpeciesId species, uint32_t seed, Owner owner, double hours, const FieldPet* field = nullptr) {
  Report r;
  r.species = speciesInfo(species).name;
  r.seed = seed;
  r.owner = owner;
  r.hours = hours;
  MemStorage store;
  NullLink link;
  FakeRtc rtc;   // a flat battery: never any wall time
  uint32_t ms = 0;
  rtc.poweredMs = &ms;
  // FIRST_HOURS_GIFT=1: the Gift recipe (starterGenome), what an unpicked nest still founds.
  const Genome founder = field ? foundingGenome040(seed)
                         : std::getenv("FIRST_HOURS_GIFT") ? starterGenome(seed) : starterGenomeOf(species, seed);
  DishOptions opt;
  opt.rtc = &rtc;
  opt.founder = &founder;
  const char* hour = std::getenv("FIRST_HOURS_FOUNDING_HOUR");   // 0: the clock before first-hours.md
  opt.foundingHour = field ? 0 : hour ? uint8_t(std::atoi(hour)) : kUnaskedFoundingHour;
  bool adopted = !field;
  Dish dish(store, seed, seed, opt);
  Passenger who(seed);

  auto at = [&] { return double(ms) / kMinuteMs; };
  auto log = [&](const std::string& what) {
    char b[24];
    std::snprintf(b, sizeof b, "%7.2f ", at());
    r.timeline.push_back(b + what);
  };

  std::map<std::string, uint32_t> lines;
  std::set<uint8_t> rows, faces;
  double lastVisible = 0, lastThought = -1, sleepStart = -1, lastAsleepLine = 0;
  bool wasAsleep = false, wasThinking = false, wasReflex = false, hatched = false;
  std::string lastLine;
  ExprId lastFace{0xFF};
  ActionId lastAction{0xFF};
  uint16_t lastFed = 0;
  uint32_t lastTick = 0, samples = 0;
  double sums[5]{};
  Appearance a = dish.appearance();
  auto visible = [&] {
    r.visible++;
    r.longestQuietMin = std::max(r.longestQuietMin, at() - lastVisible);
    lastVisible = at();
  };

  const uint32_t endMs = uint32_t(hours * 60 * kMinuteMs);
  for (; ms < endMs; ms += kSampleMs) {
    const BodySample s = owner == Owner::Seated ? who.next(ms, dish, a) : owner == Owner::Tucked ? faceDown() : still();
    dish.sample(s, ms);
    dish.tick(ms, link);
    if (!adopted && ms >= uint32_t(field->adoptAfterH * 60 * kMinuteMs) &&
        dish.adoptSpecies(species, paint::bandsFor(species)) != Dish::Adoption::NotYet) {
      adopted = true;
      if (field->giftSleep) keepGiftRecipe(dish, species, seed);
      log("ADOPTED");
      if (field->retune && dish.retuneFirstHours(species) == Dish::Adoption::Adopted) log("RETUNED");
    }
    if (dish.tickCount() == lastTick) continue;
    lastTick = dish.tickCount();
    a = dish.appearance();
    if (const Clutch* k = std::get_if<Clutch>(&dish.occupant())) {
      if (!r.died) {
        r.died = true;
        const char* causes[] = {"old age", "starved", "injured", "poisoned", "unknown"};
        r.cause = causes[int(k->cause)];
        log("DIED " + r.cause);
      }
      continue;
    }
    const Creature* c = std::get_if<Creature>(&dish.occupant());
    if (!c) continue;
    if (!hatched) {
      hatched = true;
      r.hatchMin = at();
      lastVisible = lastThought = at();
      log("HATCH");
      visible();
    }
    const Chemistry& ch = c->chemistry();
    const double hunger = pm(ch.drive(drive::hunger)), injury = pm(ch.chem[chem::injury.v]);
    const double toxin = pm(ch.chem[chem::toxin.v]);
    r.hungerMax = std::max(r.hungerMax, hunger);
    r.injuryMax = std::max(r.injuryMax, injury);
    if (hunger >= 0.8) r.starvingMin += 1.0 / (60 * kTicksPerSec);
    if (injury >= 0.1 || toxin >= 0.1) r.sickMin += 1.0 / (60 * kTicksPerSec);
    sums[0] += pm(ch.drive(drive::boredom));
    sums[1] += pm(ch.drive(drive::loneliness));
    sums[2] += pm(ch.drive(drive::need_touch));
    sums[3] += pm(ch.drive(drive::sleepiness));
    sums[4] += hunger;
    ++samples;
    r.actionTicks[ACTIONS[c->action().v].name]++;

    if (a.thinking && (!wasThinking || lastLine != a.line)) {
      r.thoughts++;
      (a.asleep ? r.asleepLinesByHour : r.awakeLinesByHour)[std::min<uint32_t>(47, ms / (60 * kMinuteMs))]++;
      lines[a.line]++;
      rows.insert(a.thought.v);
      if (!a.asleep) r.longestThoughtGapMin = std::max(r.longestThoughtGapMin, at() - lastThought);
      else r.longestAsleepGapMin = std::max(r.longestAsleepGapMin, at() - lastAsleepLine);
      lastAsleepLine = at();
      lastThought = at();
      log(std::string(a.prophecy ? "SAY* " : "SAY  ") + a.line);
      visible();
    }
    if (a.asleep) lastThought = at();   // a sleeping pet's silence is not a gap in his talk
    wasThinking = a.thinking;
    lastLine = a.thinking ? a.line : "";
    // As paint::faceFor draws it: asleep is always the asleep face.
    const ExprId face = a.asleep ? expr::asleep : a.expression;
    if (face != lastFace && (a.asleep || a.intensity >= Fx::ratio(3, 10))) {
      if (lastFace.v != 0xFF) r.faceChanges++;
      faces.insert(face.v);
      log(std::string("FACE ") + EXPRESSIONS[face.v].name);
      lastFace = face;
      visible();
    }
    if (c->action() != lastAction) {
      lastAction = c->action();
      log(std::string("ACT  ") + ACTIONS[lastAction.v].name);
    }
    if (a.reflexActive && !wasReflex) {
      r.reflexes++;
      log(std::string("HOP  ") + REFLEXES[a.reflex.v].name);
      visible();
    }
    wasReflex = a.reflexActive;
    if (a.asleep != wasAsleep) {
      char why[64];
      std::snprintf(why, sizeof why, " sleepy=%.2f gate=%.2f", pm(ch.drive(drive::sleepiness)),
                    pm(ch.locus[locus::sleep_gate.v]));
      if (a.asleep) {
        sleepStart = lastAsleepLine = at();
        r.naps++;
        if (r.firstSleepMin < 0) r.firstSleepMin = at() - r.hatchMin;
        log(std::string("SLEEP") + why);
      } else {
        r.longestSleepMin = std::max(r.longestSleepMin, at() - sleepStart);
        log(std::string("WAKE ") + why);
      }
      wasAsleep = a.asleep;
      visible();
    }
    if (a.asleep) r.asleepH += 1.0 / (3600 * kTicksPerSec);
    if (a.asleep) r.asleepMinByHour[std::min<uint32_t>(47, ms / (60 * kMinuteMs))] += 1.0 / (60 * kTicksPerSec);
    if (c->stats().fed != lastFed) {
      lastFed = c->stats().fed;
      r.bites++;
      const bool bad = c->body().mouth == Mouthful::RottenPellet;
      r.rottenBites += bad;
      log(bad ? "BITE rotten" : "BITE");
      visible();
    }
    if (dish.tickCount() % (10 * 60 * kTicksPerSec) == 0) {
      char b[160];
      std::snprintf(b, sizeof b, "NEED hunger=%.2f sleepy=%.2f bored=%.2f lonely=%.2f touch=%.2f injury=%.2f pantry=%u",
                    hunger, pm(ch.drive(drive::sleepiness)), pm(ch.drive(drive::boredom)),
                    pm(ch.drive(drive::loneliness)), pm(ch.drive(drive::need_touch)), injury,
                    unsigned(dish.habitat().pantry));
      log(b);
    }
  }
  if (wasAsleep) r.longestSleepMin = std::max(r.longestSleepMin, at() - sleepStart);
  if (hatched) r.longestQuietMin = std::max(r.longestQuietMin, at() - lastVisible);
  else r.longestQuietMin = at();
  if (samples) {
    r.boredomMean = sums[0] / samples, r.lonelyMean = sums[1] / samples, r.touchMean = sums[2] / samples;
    r.sleepyMean = sums[3] / samples, r.hungerMean = sums[4] / samples;
  }
  if (const Creature* c = std::get_if<Creature>(&dish.occupant())) r.pellets = c->stats().fed;
  r.distinctLines = uint32_t(lines.size());
  r.distinctRows = uint32_t(rows.size());
  r.distinctFaces = uint32_t(faces.size());
  for (const auto& [line, n] : lines)
    if (n > r.topLineCount) r.topLineCount = n, r.topLine = line;
  r.care = who.counts();
  return r;
}

double awakeHours(const Report& r) {
  const double alive = r.hours - std::max(0.0, r.hatchMin) / 60;
  return std::max(0.01, alive - r.asleepH);
}

std::string row(const Report& r) {
  char b[512];
  const double alive = std::max(0.01, r.hours - std::max(0.0, r.hatchMin) / 60);
  std::snprintf(b, sizeof b,
                "%-9s %6u %-6s hatch=%5.1fm vis/h=%5.1f quiet=%5.1fm say/h=%4.1f(awake %4.1f) lines=%2u rows=%2u "
                "top=%2u gap=%5.1fm asleep=%4.2fh naps=%2u longnap=%5.1fm first=%5.1fm faces=%u/%u hops=%u "
                "bites=%u/%u hunger=%.2f/%.2f starve=%5.1fm inj=%.2f sick=%5.1fm bored=%.2f lonely=%.2f %s",
                r.species.c_str(), unsigned(r.seed), ownerName(r.owner), r.hatchMin, r.visible / alive,
                r.longestQuietMin, r.thoughts / alive, r.thoughts / awakeHours(r), r.distinctLines,
                r.distinctRows, r.topLineCount, r.longestThoughtGapMin, r.asleepH, r.naps, r.longestSleepMin,
                r.firstSleepMin, r.distinctFaces, r.faceChanges, r.reflexes, r.bites, r.rottenBites, r.hungerMean, r.hungerMax,
                r.starvingMin, r.injuryMax, r.sickMin, r.boredomMean, r.lonelyMean,
                r.died ? ("DIED " + r.cause).c_str() : "");
  return b;
}

void write(const Report& r) {
  const char* dir = std::getenv("FIRST_HOURS_OUT");
  if (!dir) return;
  const std::string path = std::string(dir) + "/" + r.species + "-" + std::to_string(r.seed) + "-" +
                           ownerName(r.owner) + ".txt";
  if (FILE* f = std::fopen(path.c_str(), "w")) {
    std::fprintf(f, "%s\n", row(r).c_str());
    for (const auto& [name, n] : r.actionTicks) std::fprintf(f, "# action %s %.1f%%\n", name.c_str(), 100.0 * n / (r.hours * 36000));
    for (const std::string& l : r.timeline) std::fprintf(f, "%s\n", l.c_str());
    std::fclose(f);
  }
}

double envHours() {
  const char* h = std::getenv("FIRST_HOURS_HOURS");
  return h ? std::atof(h) : 6.0;
}

}  // namespace

// The measurement itself: every species, a few seeds, both owners. Off by
// default (a few minutes of CPU); the floors below are the suite's guard.
TEST(Sweep, EverySpeciesSeedAndOwner) {
  if (!std::getenv("FIRST_HOURS_SWEEP")) GTEST_SKIP() << "FIRST_HOURS_SWEEP=1 runs it";
  const char* n = std::getenv("FIRST_HOURS_SEEDS");
  const uint32_t seeds = n ? uint32_t(std::atoi(n)) : 4;
  const double hours = envHours();
  for (size_t sp = 0; sp < kSpeciesCount; ++sp)
    for (uint32_t s = 0; s < seeds; ++s)
      for (Owner o : {Owner::Nobody, Owner::Seated}) {
        const Report r = live(SPECIES[sp].id, 0x5EED0000u + s * 7919u, o, hours);
        std::printf("%s\n", row(r).c_str());
        std::fflush(stdout);
        write(r);
      }
}

// A board that will never hear the time founds into his morning, and a
// replug (no battery) resumes the same pet clock rather than starting another.
TEST(FirstHours, ANoPhoneBoardFoundsIntoMorningAndAReplugKeepsIt) {
  MemStorage store;
  NullLink link;
  uint32_t ms = 0;
  const Genome founder = starterGenomeOf(SpeciesId{1}, 7);
  DishOptions opt;
  opt.founder = &founder;
  opt.foundingHour = kUnaskedFoundingHour;
  uint32_t offset = 0;
  {
    Dish dish(store, 7, 7, opt);
    ASSERT_EQ(dish.boot(), Boot::Fresh);
    EXPECT_FALSE(dish.clock().night()) << "founded at pet " << int(kUnaskedFoundingHour) << ":00";
    runFor(dish, link, ms, 20 * kMinuteMs);
    ASSERT_TRUE(dish.flush());
    offset = uint32_t(dish.clock().phaseOffsetTicks);
  }
  Dish again(store, 7, 7, opt);
  EXPECT_EQ(again.boot(), Boot::Resumed);
  EXPECT_EQ(uint32_t(again.clock().phaseOffsetTicks), offset) << "the founding hour is saved, never re-applied";
  EXPECT_TRUE(std::holds_alternative<Creature>(again.occupant())) << "a 15-minute egg has hatched by 20 minutes";
  DishOptions plain;
  plain.founder = &founder;
  MemStorage other;
  EXPECT_EQ(Dish(other, 7, 7, plain).clock().phaseOffsetTicks, 0) << "tests and the sim keep 00:00";
}

// The floors docs/notes/first-hours.md measured, for a day species and a
// night-sleeper of each voice length, alone and with a seated owner. Each run
// is six pet hours from a fresh founding with no phone and no wall time.
TEST(FirstHours, AFreshPetPlaysThroughHisFirstSixHours) {
  for (SpeciesId sp : {SpeciesId{0}, SpeciesId{1}, SpeciesId{5}, SpeciesId{7}})
    for (Owner o : {Owner::Nobody, Owner::Seated}) {
      const Report r = live(sp, 0x5EED0000u, o, 6.0);
      const std::string at = row(r);
      const double alive = r.hours - r.hatchMin / 60;
      EXPECT_FALSE(r.died) << at;
      EXPECT_LE(r.hatchMin, 16) << at;
      EXPECT_GE(r.thoughts / alive, 20) << at;
      EXPECT_LE(r.longestThoughtGapMin, 6) << at;
      EXPECT_GE(r.distinctLines, 40u) << at;
      EXPECT_LE(r.topLineCount * 8, r.thoughts) << "no line is an eighth of what he says: " << at;
      EXPECT_GE(r.faceChanges, 3u) << at;
      EXPECT_LT(r.injuryMax, 0.1) << at;
      // With no clock he has no night of his own (2026-10-21, sleep-idle.md):
      // looked at every few minutes he is awake all through it, and left
      // alone on the tray table it goes dim for him after 45 minutes and he
      // goes to bed. (The floors were naps >= 1, asleep <= 2.5 h and no sleep
      // over 60 minutes for both, from the pet night his clock used to keep.)
      if (o == Owner::Seated) {
        EXPECT_LE(r.asleepH, 0.5) << "awake while he is being looked at: " << at;
        EXPECT_LE(r.longestSleepMin, 30) << at;
        EXPECT_GE(r.bites, 2u) << at;
        EXPECT_EQ(r.starvingMin, 0) << at;
      } else {
        EXPECT_GE(r.naps, 1u) << "left alone, he goes to bed: " << at;
        EXPECT_GE(r.firstSleepMin, 45) << "not before it is dim: " << at;
        EXPECT_GE(r.hungerMax, 0.5) << "hungry by the time anyone looks again: " << at;
      }
    }
}

// The badges in the field (FIELD_PETS_SWEEP=1): each table species, 0.4.0's
// founding and an adoption after FIELD_PETS_ADOPT_H (1), FIELD_PETS_HOURS (12)
// of powered time, both owners, as adopted on 0.7.0 and on 0.7.1. Per hour:
// minutes asleep, lines awake, lines asleep.
TEST(Sweep, FieldPetsHourByHour) {
  if (!std::getenv("FIELD_PETS_SWEEP")) GTEST_SKIP() << "FIELD_PETS_SWEEP=1 runs it";
  const char* h = std::getenv("FIELD_PETS_HOURS");
  const char* adopt = std::getenv("FIELD_PETS_ADOPT_H");
  const double hours = h ? std::atof(h) : 12.0;
  for (size_t sp = 1; sp < kSpeciesCount; ++sp)
    for (int v : {0, 1, 2})
      for (Owner o : {Owner::Nobody, Owner::Seated}) {
        FieldPet f;
        f.giftSleep = v < 2;
        f.retune = v == 1;
        if (adopt) f.adoptAfterH = std::atof(adopt);
        Report r = live(SPECIES[sp].id, 0x5EED0000u, o, hours, &f);
        r.species += v == 0 ? "@070" : v == 1 ? "@070+retune" : "@071";
        std::printf("%s\n", row(r).c_str());
        std::printf("  hour   :");
        for (int i = 0; i < int(hours); ++i) std::printf(" %4d", i);
        std::printf("\n  asleep :");
        for (int i = 0; i < int(hours); ++i) std::printf(" %4.0f", r.asleepMinByHour[i]);
        std::printf("\n  awake  :");
        for (int i = 0; i < int(hours); ++i) std::printf(" %4u", r.awakeLinesByHour[i]);
        std::printf("\n  dozing :");
        for (int i = 0; i < int(hours); ++i) std::printf(" %4u", r.asleepLinesByHour[i]);
        std::printf("\n");
        std::fflush(stdout);
        write(r);
      }
}

// The badges 0.4.0 founded slept through most of their first night (pet
// 00:00 at founding, no phone to say otherwise), and the owner saw no text
// until a phone woke him. With no clock there is no such night any more
// (sleep-idle.md); a pet tucked in face down sleeps instead. Asleep, he
// murmurs: every few minutes, in his voice.
TEST(FieldPets, ASleepingPetStillTalks) {
  FieldPet f;
  f.giftSleep = true;   // adopted on 0.7.0, the longest sleeper
  for (SpeciesId sp : {SpeciesId{1}, SpeciesId{5}, SpeciesId{7}}) {
    const Report r = live(sp, 0x5EED0000u, Owner::Tucked, 8.0, &f);
    const std::string at = row(r);
    ASSERT_GE(r.asleepH, 4.0) << "the night this guards: " << at;
    double asleepLines = 0;
    for (uint32_t n : r.asleepLinesByHour) asleepLines += n;
    EXPECT_GE(asleepLines / r.asleepH, 12) << at;
    EXPECT_LE(r.longestAsleepGapMin, 15) << at;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
