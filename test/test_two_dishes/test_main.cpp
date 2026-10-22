// Two whole Dishes on one ether (docs/design/BATTLES.md, testing item 5 and
// unit D9's cases): each with its own MemStorage and FakeAir, driven by 50 Hz
// BodySamples from a scripted kid who reads only the duel view, as a kid
// reads the screen.
#include <algorithm>
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/food.h"
#include "blorb/friend_egg.h"
#include "blorb/genes.h"
#include "blorb/species.h"
#include "blorb/starter_duel.h"
#include "paint/duel.h"
#include "paint/sprite_pack.h"
#include "fake_air.h"
#include "links.h"
#include "mem_storage.h"
#include "../../src/debug_verbs.h"

using namespace blorb;
using blorbtest::Ether;
using blorbtest::FakeAir;
using blorbtest::MemStorage;
using blorbtest::NullLink;
using blorbtest::Weather;

namespace {

PeerAddr addr(uint8_t n) { return PeerAddr{{2, 0, 0, 0, 0, n}}; }

// One 100 ms tick of a hand: five samples at 50 Hz.
struct Frame {
  int8_t aim = -1;
  bool down = false, tap = false;
  int8_t shake = 0;   // a hand's swing this tick: 1 up, -1 down
  bool cradle = false, lid = false;
  // The button's twins (controls.h), for a kid whose case covers BOOT.
  bool side = false, knock2 = false, hang = false;
};

BodySample sampleOf(const Frame& f, uint8_t i) {
  BodySample s;
  s.az = 1000;
  if (f.aim >= 0) s.ax = kAimTilt[f.aim].ax, s.ay = kAimTilt[f.aim].ay, s.az = kAimTilt[f.aim].az;
  if (f.cradle) s.ax = 450, s.ay = 0, s.az = 893;
  if (f.lid) s.ax = 0, s.ay = 0, s.az = -1000;
  if (f.shake) s.ax = 0, s.ay = 0, s.az = i == 2 ? int16_t(1000 + 1000 * f.shake) : 1000;   // 1 g off gravity
  s.tapCode = f.tap && i == 0 ? 1 : 0;
  s.buttonDown = f.down;
  // On edge in the screen's frame (main.cpp has turned the sample); only the level says how.
  if (f.side) s.ax = 0, s.ay = -1000, s.az = 0, s.sideDown = true;
  if (f.hang) s.ax = 0, s.ay = -1000, s.az = 0, s.invertedDown = true;
  if (f.knock2 && i == 0) s.tapCode = 2;
  return s;
}

enum class Mercy : uint8_t { Spare, Finish };

// How one kid plays, everything read off the view.
struct Kid {
  Stakes tier = Stakes::Spar;
  Mercy mercy = Mercy::Spare;
  bool hug = false, shakeOnce = false, wantDuel = true, leave = true;
  bool special = false;          // a quick special whenever the meter holds it
  bool pressForMortal = false;   // tries to agree to Mortal with a press (it must not count)
  bool spareByPress = false;     // spares with BOOT, not a knock
  bool askRematch = false;       // the friends screen: BOOT once for another bout, then waits
  uint16_t leaveAfter = 0;       // the friends screen: ticks to look before leaving
  Rng rng = Rng::seeded(1);
  bool shook = false;
  bool fought = false;   // a fight showed: back in fight mode, a kid who leaves presses twice

  // No BOOT at all: every press is a knock knock, every hold a turn onto a
  // side held as long, every double press a hang (controls.h).
  bool noButton = false;
  bool everPressed = false;   // a noButton kid's BOOT went down: the harness is wrong

  void plan(const DuelView& v, bool dueling, std::deque<Frame>& out) {
    auto hold = [&](int8_t aim, int n, Frame f) {
      if (noButton && f.down) {
        f.down = false;
        if (n <= 2) {   // a press: the knock knock, then a quiet beat (the tap engine's own gap)
          Frame k = f;
          k.knock2 = true, k.aim = aim;
          out.push_back(k);
          f.aim = aim;
          for (int i = 1; i < std::max(n, 5); ++i) out.push_back(f);
          return;
        }
        f.side = true;
      }
      for (int i = 0; i < n; ++i) { f.aim = aim; out.push_back(f); }
    };
    auto dbl = [&]() {
      if (noButton) {
        Frame h;
        h.hang = true;
        for (int i = 0; i < 6; ++i) out.push_back(Frame{});   // the right way up first: a hang is a turn
        for (int i = 0; i < 12; ++i) out.push_back(h);
        for (int i = 0; i < 4; ++i) out.push_back(Frame{});
        return;
      }
      hold(-1, 1, Frame{-1, true}), hold(-1, 1, Frame{}), hold(-1, 1, Frame{-1, true}), hold(-1, 3, Frame{});
    };
    if (!dueling) {
      if (!wantDuel) return;
      wantDuel = false;
      dbl();
      return;
    }
    switch (v.phase) {
      case DuelPhase::Lobby:
        // Fight mode pairs by itself: the kid just waits, tilting the badge
        // about, and after a fight leaves it with a double press.
        if (fought && leave) {
          fought = false;
          return dbl();
        }
        return hold(int8_t(rng.below(4)), 1, Frame{});
      case DuelPhase::Staking: {
        // BOOT only, whatever the tilt: a press per step up from Spar, then a
        // 3 s hold locks it (a kid pressing for Mortal and never holding gets Scrap).
        if (v.confirmed) return hold(-1, 1, Frame{});
        const int8_t t = int8_t(std::min(uint8_t(tier), uint8_t(v.ceiling[0])));
        const int8_t lean = int8_t(rng.below(3));
        hold(lean, 4, Frame{});
        for (int8_t i = 0; i < t; ++i) hold(lean, 1, Frame{-1, true}), hold(lean, 2, Frame{});
        if (!pressForMortal) hold(lean, 33, Frame{-1, true}), hold(lean, 1, Frame{});
        else hold(lean, 90, Frame{});
        return;
      }
      case DuelPhase::Choosing:
        if (v.locked) return hold(-1, 1, Frame{});
        if (shakeOnce && !shook && v.lastTurn >= 1) {
          shook = true;
          for (int8_t k = 0; k < 8; ++k) hold(-1, 1, Frame{-1, false, false, int8_t(k % 2 ? -1 : 1)});
          return hold(-1, 4, Frame{});
        }
        {
          const int8_t a = int8_t(rng.below(3));
          hold(a, 4, Frame{});
          if (special && v.specialReady) hold(a, 2, Frame{-1, true}), hold(a, 1, Frame{});
          else hold(a, 1, Frame{-1, false, true});
          hold(-1, 2, Frame{});
        }
        return;
      case DuelPhase::Mercy:
        if (v.theyAreDown && v.verdict == Verdict::None) {
          if (mercy == Mercy::Finish) hold(-1, 34, Frame{-1, true}), hold(-1, 1, Frame{});
          else if (spareByPress) hold(-1, 2, Frame{-1, true}), hold(-1, 5, Frame{});
          else hold(-1, 1, Frame{-1, false, true}), hold(-1, 5, Frame{});
        }
        return;
      case DuelPhase::Friends:
        if (askRematch) {
          // A second press inside the double's window would be a double, which leaves.
          if (!v.rematchAsked) hold(-1, 2, Frame{-1, true}), hold(-1, 10, Frame{});
          return;
        }
        if (v.phaseTicks < leaveAfter) return hold(-1, 1, Frame{});
        if (hug && !v.iHugged) return hold(-1, 34, Frame{-1, true}), hold(-1, 1, Frame{});
        if (hug && !(v.eggLaid || v.eggFizzled)) return hold(-1, 1, Frame{});
        if (leave) dbl();
        return;
      default: return;
    }
  }
};

// MemStorage that, while taping, keeps every state its files pass through:
// each is what a power cut right after that write would leave.
struct CutStorage : MemStorage {
  bool taping = false;
  std::vector<std::map<std::string, std::vector<uint8_t>>> tape;
  bool writeAtomic(const char* n, const uint8_t* d, size_t l) override { return keep(MemStorage::writeAtomic(n, d, l)); }
  bool append(const char* n, const uint8_t* d, size_t l) override { return keep(MemStorage::append(n, d, l)); }
  bool rename(const char* f, const char* t) override { return keep(MemStorage::rename(f, t)); }

 private:
  bool keep(bool ok) {
    if (taping) tape.push_back(files);
    return ok;
  }
};

// One device: its storage, radio, Dish and the kid holding it.
struct Device {
  CutStorage store;
  FakeAir air;
  std::optional<Dish> dish;
  NullLink link;
  Kid kid;
  std::deque<Frame> plan;
  uint32_t ms = 0;
  std::set<uint8_t> felt;          // every stimulus his body took, all run
  std::set<uint8_t> feltDueling;   // the ones he took while a duel was open
  std::vector<Meeting> met;
  DuelPhase shownPhase = DuelPhase::Closed;
  std::set<uint8_t> contexts;      // every BootContext a duel showed, each checked against its hint
  bool hopped = false, prophesied = false, judged = false;

  Device(Ether& e, uint8_t n) : air(e, addr(n)) {}

  void boot(const Genome& founder, uint32_t seed, bool radio = true) {
    DishOptions o;
    o.founder = &founder;
    o.air = radio ? &air : nullptr;
    dish.emplace(store, seed, 0xD15400ull + seed, o);
  }

  // Grows him to an adult at pet noon, rested and fed, with no duel in the radio's way.
  void growUp() {
    static Device* self;
    self = this;
    const char* why = debugverbs::growTo(*dish, "adult", [](uint32_t ticks) {
      for (uint32_t t = 0; t < ticks; ++t) self->dish->tick(self->ms += 100, self->link);
    });
    ASSERT_EQ(why, nullptr) << why;
    PetClock& c = dish->clock();
    c.phaseOffsetTicks = int32_t(PetClock::kDayTicks / 2) - int32_t(c.petTicks % PetClock::kDayTicks);
    Creature& me = creature();
    for (const auto& d : DRIVES) me.inject(driveChem(d.id), Fx::zero());
    for (int i = 0; i < 20 && me.body().asleep; ++i) dish->tick(ms += 100, link);
  }

  Creature& creature() { return std::get<Creature>(dish->occupant()); }
  bool alive() const { return std::holds_alternative<Creature>(dish->occupant()); }

  void step() {
    const bool dueling = dish->duelActive();
    const DuelView v = dish->duelView();
    if (v.phase != shownPhase) plan.clear();   // a new screen: the kid looks again
    if (uint8_t(v.phase) >= uint8_t(DuelPhase::Staking)) kid.fought = true;
    shownPhase = v.phase;
    if (plan.empty()) kid.plan(v, dueling, plan);
    Frame f;
    if (!plan.empty()) f = plan.front(), plan.pop_front();
    for (uint8_t i = 0; i < 5; ++i) {
      const BodySample smp = sampleOf(f, i);
      if (kid.noButton && smp.buttonDown) kid.everPressed = true;
      dish->sample(smp, ms + 20 * i);
    }
    ms += 100;
    dish->tick(ms, link);
    if (dish->duelActive()) {
      const DuelView w = dish->duelView();
      const Appearance look = dish->appearance();
      prophesied = prophesied || w.prophesied;
      judged = judged || w.prophecyJudged;
      hopped = hopped || (look.reflexActive && look.reflex == reflex::hop);
      const BootContext ctx = dish->bootContext();
      contexts.insert(uint8_t(ctx));
      EXPECT_EQ(dish->appearance().boot.glyph, bootMeaning(ctx).hint) << "context " << int(ctx);
    }
    if (const Creature* c = std::get_if<Creature>(&dish->occupant()))
      for (uint8_t s = 0; s < 64; ++s)
        if (c->chemistry().locus[kRecentBase + s] >= Fx::ratio(1, 2)) {
          felt.insert(s);
          if (dueling || dish->duelActive()) feltDueling.insert(s);
        }
  }
};

Fx level(const Creature& c, ChemId id) { return c.chemistry().chem[id.v]; }

void seasonBook(MemStorage& s, uint64_t stranger) {
  DuelBook book = DuelBook::open(s);
  Meeting m;
  m.friendCard.lineageId = stranger;
  m.battle.seed = uint32_t(stranger);
  m.ending = Ending::AWon;
  m.manualTurns = 40;
  book.record(m, 0, 1, 0);
}

// Two devices and a still twin of the first, on one ether, on one clock.
struct Bout {
  Ether ether;
  Device a, b, twin;
  Genome ga, gb;
  uint32_t tick = 0;

  // seasoned: each book has 40 picked turns against a stranger, so Mortal's
  // fairness gate (30 picked turns each) opens and nothing else about the pair moves.
  Bout(uint64_t seed, SpeciesId sa, SpeciesId sb, bool seasoned = false, Weather w = {})
      : ether(seed, w), a(ether, 1), b(ether, 2), twin(ether, 9),
        ga(starterGenomeOf(sa, 11)), gb(starterGenomeOf(sb, 22)) {
    if (seasoned) seasonBook(a.store, 0xAAAA), seasonBook(b.store, 0xBBBB);
    a.boot(ga, 11), b.boot(gb, 22), twin.boot(ga, 11, false);
    a.growUp(), b.growUp(), twin.growUp();
    a.kid.rng = Rng::seeded(seed * 3 + 1), b.kid.rng = Rng::seeded(seed * 5 + 2);
    twin.kid.wantDuel = true;   // the same double press, with no radio: the same pellet, then still
  }

  void step() {
    ether.advance(++tick);
    a.step(), b.step(), twin.step();
  }
  // Runs until both radios are off, or the budget runs out. True if both closed.
  bool runDuel(uint32_t maxTicks = 6000) {
    bool opened = false;
    for (uint32_t t = 0; t < maxTicks; ++t) {
      step();
      opened = opened || (a.dish->duelActive() && b.dish->duelActive());
      if (opened && !a.dish->duelActive() && !b.dish->duelActive()) return true;
    }
    return false;
  }
};

uint32_t deaths(const Lineage& l) {
  uint32_t n = 0;
  l.forEach([&](const LineageEntry& e) { n += std::holds_alternative<Death>(e); });
  return n;
}
uint32_t checkpoints(const Lineage& l) {
  uint32_t n = 0;
  l.forEach([&](const LineageEntry& e) { n += std::holds_alternative<Checkpoint>(e); });
  return n;
}

}  // namespace

// ---- the aim and the double press, through the real detectors ------------------------

TEST(Gestures, EachAimTiltLandsOnItsAim) {
  for (const AimTilt& t : kAimTilt) {
    Detectors d;
    SenseOut out;
    Fx tx = Fx::ratio(1, 2), ty = Fx::ratio(1, 2);
    for (uint32_t ms = 0; ms < 1000; ms += 20) {
      BodySample s;
      s.ax = t.ax, s.ay = t.ay, s.az = t.az;
      out.clear();
      d.sample(s, ms, out);
      for (uint8_t i = 0; i < out.lociCount; ++i) {
        if (out.loci[i].locus == locus::tilt_x) tx = out.loci[i].value;
        if (out.loci[i].locus == locus::tilt_y) ty = out.loci[i].value;
      }
    }
    EXPECT_EQ(DuelInput::from(SenseOut{}, 0, tx, ty, false).aim, t.aim) << "kAimTilt row " << int(t.aim);
  }
}

TEST(Gestures, TwoQuickPressesAreADoubleAndASlowSecondIsAPress) {
  auto run = [](uint32_t gapMs) {
    ButtonDetector d;
    std::vector<uint8_t> fired;
    const uint32_t downs[2] = {0, 150 + gapMs};
    for (uint32_t ms = 0; ms < 2000; ms += 20) {
      BodySample s;
      for (uint32_t at : downs) s.buttonDown = s.buttonDown || (ms >= at && ms < at + 150);
      SenseOut out;
      d.sample(s, ms, out);
      for (uint8_t i = 0; i < out.stimCount; ++i) fired.push_back(out.stimuli[i].v);
    }
    return fired;
  };
  EXPECT_EQ(run(160), (std::vector<uint8_t>{stim::button.v, stim::button_double.v}));
  EXPECT_EQ(run(kDoublePressMs + 60), (std::vector<uint8_t>{stim::button.v, stim::button.v}));
}

// The first press of a double feeds him the moment it comes up, not once
// the window has closed; the second opens fight mode.
TEST(Gestures, TheFirstPressOfADoubleFeedsAtOnce) {
  ButtonDetector d;
  std::vector<std::pair<uint32_t, uint8_t>> fired;
  for (uint32_t ms = 0; ms < 2000; ms += 20) {
    BodySample s;
    s.buttonDown = ms < 150 || (ms >= 450 && ms < 600);
    SenseOut out;
    d.sample(s, ms, out);
    for (uint8_t i = 0; i < out.stimCount; ++i) fired.push_back({ms, out.stimuli[i].v});
  }
  ASSERT_EQ(fired.size(), 2u);
  EXPECT_EQ(fired[0], (std::pair<uint32_t, uint8_t>{180, stim::button.v}));   // dated 160, settled a sample later
  EXPECT_EQ(fired[1].second, stim::button_double.v);
}

// The first press lights BOOT's hint until the double's window closes, and a
// double opens fight mode, which says it is looking and how long it has, and
// finds the other badge in fight mode by itself: no tap, no tilt.
// A kid's natural double tap on the real sampling path (50 Hz, each sample
// stamped): two presses of 80 to 200 ms, 100 to 350 ms apart, at every phase
// of the sampling clock, is one press and one double, nothing else.
TEST(Gestures, AKidsNaturalDoubleTapAlwaysRegisters) {
  int combos = 0;
  for (uint32_t pressMs = 80; pressMs <= 200; pressMs += 20)
    for (uint32_t gapMs = 100; gapMs <= 350; gapMs += 25)
      for (uint32_t phase = 0; phase < 20; phase += 3) {
        ButtonDetector d;
        std::vector<uint8_t> fired;
        const uint32_t second = 1000 + pressMs + gapMs;
        for (uint32_t ms = phase; ms < 3000; ms += 20) {
          BodySample s;
          s.buttonDown = (ms >= 1000 && ms < 1000 + pressMs) || (ms >= second && ms < second + pressMs);
          SenseOut out;
          d.sample(s, ms, out);
          for (uint8_t i = 0; i < out.stimCount; ++i) fired.push_back(out.stimuli[i].v);
        }
        EXPECT_EQ(fired, (std::vector<uint8_t>{stim::button.v, stim::button_double.v}))
            << pressMs << " ms presses, " << gapMs << " ms apart, phase " << phase;
        ++combos;
      }
  EXPECT_GT(combos, 400);
}

// GPIO0 is read raw: a bounce one sample long inside a press is still one
// press, not a double that opens fight mode by itself.
TEST(Gestures, AContactBounceNeverSplitsAPress) {
  ButtonDetector d;
  std::vector<uint8_t> fired;
  for (uint32_t ms = 0; ms < 2000; ms += 20) {
    BodySample s;
    s.buttonDown = ms >= 100 && ms < 260 && ms != 160;
    SenseOut out;
    d.sample(s, ms, out);
    for (uint8_t i = 0; i < out.stimCount; ++i) fired.push_back(out.stimuli[i].v);
  }
  EXPECT_EQ(fired, (std::vector<uint8_t>{stim::button.v}));
}

namespace {
// Two 140 ms presses 200 ms apart, sampled at 50 Hz, then the Dish ticks.
void doublePress(Device& d) {
  for (int p = 0; p < 2; ++p) {
    for (uint32_t t = 0; t < 140; t += 20) {
      BodySample s;
      s.az = 1000, s.buttonDown = true;
      d.dish->sample(s, d.ms += 20);
    }
    for (uint32_t t = 0; t < (p ? 100u : 200u); t += 20) {
      BodySample s;
      s.az = 1000;
      d.dish->sample(s, d.ms += 20);
    }
  }
  d.dish->tick(d.ms, d.link);
}
void idle(Device& d, int ticks) {
  for (int i = 0; i < ticks; ++i) d.dish->tick(d.ms += 100, d.link);
}
// Held upside down, sampled at 50 Hz: upright first (the detector's rearm),
// then the pose steady past HangDetector's 1 s dwell, then the Dish ticks.
void hang(Device& d) {
  for (uint32_t t = 0; t < 700; t += 20) {
    BodySample s;
    s.az = 1000;
    d.dish->sample(s, d.ms += 20);
  }
  for (uint32_t t = 0; t < 1300; t += 20) {
    BodySample s;
    s.ay = -1000, s.invertedDown = true;   // on edge, upside down (the screen's frame)
    d.dish->sample(s, d.ms += 20);
  }
  d.dish->tick(d.ms, d.link);
}
}  // namespace

// Every double press says what it did: fight mode, or why not, in words.
TEST(FightAsk, AnEggSaysSoAndAFreshBoardNeverFightsSilently) {
  Ether ether(5, {});
  Device a(ether, 1);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  a.boot(ga, 1);
  ASSERT_TRUE(std::holds_alternative<Egg>(a.dish->occupant()));
  doublePress(a);
  EXPECT_FALSE(a.dish->duelActive());
  const Appearance look = a.dish->appearance();
  EXPECT_EQ(look.noFight, NoFight::Egg);
  idle(a, 60);
  EXPECT_EQ(a.dish->appearance().noFight, NoFight::None) << "the reason shows for a few seconds";
}

TEST(FightAsk, AnAsleepPetWakesForAFightUnlessTooSleepy) {
  Ether ether(6, {});
  Device a(ether, 1);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  a.boot(ga, 1);
  a.growUp();
  auto sleep = [&](Fx sleepiness) {
    a.creature().inject(driveChem(drive::sleepiness), Fx::ratio(9, 10));
    a.creature().force(action::sleep);
    for (int i = 0; i < 50 && !a.creature().body().asleep; ++i) idle(a, 1);
    a.creature().inject(driveChem(drive::sleepiness), sleepiness);
  };
  sleep(Fx::ratio(95, 100));
  ASSERT_TRUE(a.creature().body().asleep);
  doublePress(a);
  EXPECT_FALSE(a.dish->duelActive());
  EXPECT_EQ(a.dish->appearance().noFight, NoFight::TooSleepy);
  EXPECT_TRUE(a.creature().body().asleep) << "too sleepy: left to sleep";

  idle(a, 60);
  sleep(Fx::ratio(3, 10));
  ASSERT_TRUE(a.creature().body().asleep);
  doublePress(a);
  EXPECT_TRUE(a.dish->duelActive()) << "fight mode opens at once";
  idle(a, 3);
  EXPECT_FALSE(a.creature().body().asleep) << "woken for it";
  EXPECT_EQ(a.dish->duelView().myDecline, Decline::None);
  EXPECT_TRUE(a.dish->appearance().lobby);
}

TEST(FightAsk, AHungryPetEntersFightModeReadyToFight) {
  Ether ether(7, {});
  Device a(ether, 1);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  a.boot(ga, 1);
  a.growUp();
  a.creature().inject(driveChem(drive::hunger), Fx::ratio(97, 100));
  doublePress(a);
  ASSERT_TRUE(a.dish->duelActive());
  const Appearance look = a.dish->appearance();
  EXPECT_TRUE(look.lobby);
  // 0.7.9: hunger is no refusal — on the buttonless case he cannot be fed,
  // so a hunger refusal would lock him out of fights for good.
  EXPECT_EQ(look.lobbyDecline, Decline::None);
}

// The field failure 0.7.9 fixes: two unfed badges in their cases searched
// forever, each beaconing a hunger refusal the other could never claim.
TEST(FightAsk, TwoHungryPetsPair) {
  Ether ether(17, {});
  Device a(ether, 1), b(ether, 2);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  const Genome gb = starterGenomeOf(kGrungo, 23);
  a.boot(ga, 1);
  b.boot(gb, 2);
  a.growUp();
  b.growUp();
  a.creature().inject(driveChem(drive::hunger), Fx::ratio(97, 100));
  b.creature().inject(driveChem(drive::hunger), Fx::ratio(97, 100));
  uint32_t t = 1;
  for (; t <= 200 && (a.dish->duelView().phase == DuelPhase::Lobby ||
                      b.dish->duelView().phase == DuelPhase::Lobby);
       ++t) {
    ether.advance(t);
    a.dish->tick(a.ms += 100, a.link);
    b.dish->tick(b.ms += 100, b.link);
  }
  EXPECT_NE(a.dish->duelView().phase, DuelPhase::Lobby) << "a hungry pet is claimable";
  EXPECT_NE(b.dish->duelView().phase, DuelPhase::Lobby) << "and can claim";
}

// Right after an update the radio is the trial's until it keeps: the kid is
// told, and fight mode opens by itself once the radio is free.
TEST(FightAsk, DuringAnUpdatesTrialFightModeWaitsAndOpensByItself) {
  Ether ether(8, {});
  Device a(ether, 1);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  a.boot(ga, 1);
  a.growUp();
  a.air.refuseOpen = true;   // RadioGate: an image on trial, or an update session
  doublePress(a);
  EXPECT_FALSE(a.dish->duelActive());
  EXPECT_EQ(a.dish->appearance().noFight, NoFight::Updating);
  idle(a, 200);
  EXPECT_EQ(a.dish->appearance().noFight, NoFight::Updating) << "said for as long as it waits";
  a.air.refuseOpen = false;   // the trial kept
  idle(a, 15);
  EXPECT_TRUE(a.dish->duelActive()) << "fight mode opened by itself";
  EXPECT_EQ(a.dish->appearance().noFight, NoFight::None);
}

namespace {
std::vector<std::string> diags(Device& d) {
  std::vector<std::string> out;
  for (char l[DiagLines::kLine]; d.dish->takeDiag(l);) out.emplace_back(l);
  return out;
}
bool has(const std::vector<std::string>& lines, const std::string& part) {
  for (const std::string& l : lines)
    if (l.find(part) != std::string::npos) return true;
  return false;
}
std::string joined(const std::vector<std::string>& lines) {
  std::string s;
  for (const std::string& l : lines) s += l + " | ";
  return s;
}
}  // namespace

// Every BOOT press and every ask for a fight is a line for the debug link:
// how long, how far apart, where BOOT was, what stood in the way, what came of it.
TEST(FightAsk, EveryPressAndEveryAskIsLogged) {
  Ether ether(10, {});
  Device egg(ether, 1);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  egg.boot(ga, 1);
  doublePress(egg);
  std::vector<std::string> l = diags(egg);
  EXPECT_TRUE(has(l, "boot press ms=140 gap=none")) << joined(l);
  EXPECT_TRUE(has(l, "boot double ms=140 gap=200")) << joined(l);
  EXPECT_TRUE(has(l, "fight double=1 hang=0 ctx=egg gate=egg result=refused")) << joined(l);
  EXPECT_STREQ(egg.dish->fightState(), "off");
  EXPECT_STREQ(egg.dish->lastFightGate(), "egg");

  Device a(ether, 2);
  a.boot(ga, 2);
  a.growUp();
  diags(a);
  a.creature().inject(driveChem(drive::hunger), Fx::ratio(97, 100));
  doublePress(a);
  l = diags(a);
  EXPECT_TRUE(has(l, "fight double=1 hang=0 ctx=awake gate=ok")) << joined(l) << " (hungry no longer refuses, 0.7.9)";
  EXPECT_TRUE(has(l, "result=entered")) << joined(l);
  EXPECT_STREQ(a.dish->fightState(), "searching");

  Device b(ether, 3);
  b.boot(ga, 3);
  b.growUp();
  b.air.refuseOpen = true;
  diags(b);
  doublePress(b);
  idle(b, 30);
  l = diags(b);
  EXPECT_TRUE(has(l, "fight double=1 hang=0 ctx=awake gate=radio_closed result=waiting")) << joined(l);
  EXPECT_EQ(std::count_if(l.begin(), l.end(), [](const std::string& s) { return s.find("waiting") != std::string::npos; }), 1)
      << "a retry each second logs once: " << joined(l);
  EXPECT_STREQ(b.dish->fightState(), "waiting");
  b.air.refuseOpen = false;
  idle(b, 15);
  l = diags(b);
  EXPECT_TRUE(has(l, "fight double=0 hang=0 ctx=awake gate=ok result=entered")) << joined(l);
  EXPECT_STREQ(b.dish->fightState(), "searching");
  EXPECT_STREQ(b.dish->lastFightGate(), "ok");
}

TEST(FightAsk, ASleepingPetsAskSaysWokenOrTooSleepy) {
  Ether ether(11, {});
  Device a(ether, 1);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  a.boot(ga, 1);
  a.growUp();
  auto sleep = [&](Fx sleepiness) {
    a.creature().inject(driveChem(drive::sleepiness), Fx::ratio(9, 10));
    a.creature().force(action::sleep);
    for (int i = 0; i < 50 && !a.creature().body().asleep; ++i) idle(a, 1);
    a.creature().inject(driveChem(drive::sleepiness), sleepiness);
  };
  sleep(Fx::ratio(95, 100));
  diags(a);
  doublePress(a);
  std::vector<std::string> l = diags(a);
  EXPECT_TRUE(has(l, "fight double=1 hang=0 ctx=asleep gate=asleep:sleepy:0.9")) << joined(l);
  EXPECT_TRUE(has(l, "result=refused")) << joined(l);
  idle(a, 60);
  sleep(Fx::ratio(3, 10));
  diags(a);
  doublePress(a);
  l = diags(a);
  EXPECT_TRUE(has(l, "fight double=1 hang=0 ctx=asleep gate=woken result=entered")) << joined(l);
}

TEST(FightAsk, ASecondDoublePressTakesBackAWaitingAsk) {
  Ether ether(9, {});
  Device a(ether, 1);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  a.boot(ga, 1);
  a.growUp();
  a.air.refuseOpen = true;
  doublePress(a);
  idle(a, 10);
  doublePress(a);
  a.air.refuseOpen = false;
  idle(a, 30);
  EXPECT_FALSE(a.dish->duelActive());
  EXPECT_EQ(a.dish->appearance().noFight, NoFight::None);
}

// The double press's no-button twin: a case that covers BOOT cannot stop a
// kid asking for a fight, and the ask is the same ask in every way that
// matters (the same consent, the same refusals, the same log, with hang=1).
TEST(FightAsk, AHangOpensFightModeAndAHangAgainLeavesIt) {
  Ether ether(5, {});
  Device a(ether, 1);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  a.boot(ga, 1);
  a.growUp();
  hang(a);
  EXPECT_TRUE(a.dish->duelActive());
  EXPECT_STREQ(a.dish->fightState(), "searching");
  EXPECT_TRUE(a.dish->appearance().lobby) << "looking for a fight: the red ring's state";
  std::vector<std::string> l = diags(a);
  EXPECT_TRUE(has(l, "fight double=0 hang=1 ctx=awake gate=ok result=entered")) << joined(l);
  hang(a);
  EXPECT_FALSE(a.dish->duelActive());
  EXPECT_STREQ(a.dish->fightState(), "off");
}

TEST(FightAsk, AnEggIsToldNoForAHangToo) {
  Ether ether(5, {});
  Device a(ether, 1);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  a.boot(ga, 1);
  ASSERT_TRUE(std::holds_alternative<Egg>(a.dish->occupant()));
  hang(a);
  EXPECT_FALSE(a.dish->duelActive());
  EXPECT_EQ(a.dish->appearance().noFight, NoFight::Egg);
  std::vector<std::string> l = diags(a);
  EXPECT_TRUE(has(l, "fight double=0 hang=1 ctx=egg gate=egg result=refused")) << joined(l);
}

// A hang the phone fired is not the body's ask: the debug link's FIRE marks
// it remote, and no remote stimulus opens a duel (twists refuse hang too).
TEST(FightAsk, APhoneFiredHangOpensNothing) {
  Ether ether(5, {});
  Device a(ether, 1);
  const Genome ga = starterGenomeOf(kGrungo, 11);
  a.boot(ga, 1);
  a.growUp();
  a.dish->fire(stim::hang);
  idle(a, 5);
  EXPECT_FALSE(a.dish->duelActive());
  EXPECT_STREQ(a.dish->fightState(), "off");
}

TEST(Gestures, TheFirstPressGlowsAndFightModeFindsAFriendByItself) {
  Ether ether(3, {});
  Device a(ether, 1), b(ether, 2);
  const Genome ga = starterGenomeOf(kGrungo, 11), gb = starterGenomeOf(SpeciesId{4}, 22);
  a.boot(ga, 1), b.boot(gb, 2);
  a.growUp(), b.growUp();
  ASSERT_EQ(a.dish->bootContext(), BootContext::Awake);
  auto press = [](Device& d, uint32_t downMs, uint32_t upMs) {
    for (uint32_t t = 0; t < downMs; t += 20) {
      BodySample s;
      s.az = 1000, s.buttonDown = true;
      d.dish->sample(s, d.ms += 20);
    }
    for (uint32_t t = 0; t < upMs; t += 20) {
      BodySample s;
      s.az = 1000;
      d.dish->sample(s, d.ms += 20);
    }
    d.dish->tick(d.ms, d.link);
  };
  press(a, 140, 100);
  EXPECT_GT(a.dish->appearance().boot.doubleFill, 0) << "the first press waits, visibly, for its second";
  EXPECT_FALSE(a.dish->duelActive());
  press(a, 0, kDoublePressMs + 100);
  EXPECT_EQ(a.dish->appearance().boot.doubleFill, 0) << "the window closed: the glow is gone";
  EXPECT_FALSE(a.dish->appearance().lobby);

  for (Device* d : {&a, &b}) press(*d, 140, 200), press(*d, 140, 100);
  ASSERT_TRUE(a.dish->duelActive() && b.dish->duelActive());
  Appearance look = a.dish->appearance();
  EXPECT_TRUE(look.lobby);
  EXPECT_GT(look.lobbyTicksLeft, DuelTimings::kFightModeTicks - 20);
  bool connecting = false;
  uint32_t t = 1;
  for (; t <= 100 && a.dish->duelView().phase != DuelPhase::Staking; ++t) {
    ether.advance(t);
    for (Device* d : {&a, &b}) d->dish->tick(d->ms += 100, d->link);
    look = a.dish->appearance();
    connecting = connecting || look.lobbyConnecting;
    if (a.dish->duelView().phase == DuelPhase::Lobby) EXPECT_TRUE(look.lobby && !look.lobbyConnecting);
  }
  EXPECT_TRUE(connecting) << "a locked pair says so before the fight shows";
  EXPECT_EQ(a.dish->duelView().phase, DuelPhase::Staking) << "paired within " << t << " ticks";
  EXPECT_FALSE(a.dish->appearance().lobby) << "the fight shows";
}

// ---- testing item 5 ------------------------------------------------------------------

TEST(TwoDishes, ASparRunsWholeAndChangesNothingAWoundWould) {
  Bout bout(7, kGrungo, SpeciesId{6});   // grungo against fizz
  bout.a.kid.shakeOnce = true;
  ASSERT_TRUE(bout.runDuel());
  Device& a = bout.a;
  Device& b = bout.b;

  ASSERT_EQ(a.dish->duels().friendCount(), 1) << "both books hold the meeting";
  ASSERT_EQ(b.dish->duels().friendCount(), 1);
  EXPECT_EQ(a.dish->duels().state().friends[0].s.lineageId, 0xD15400ull + 22);
  EXPECT_EQ(b.dish->duels().state().friends[0].s.lineageId, 0xD15400ull + 11);
  EXPECT_TRUE(a.felt.count(stim::duel_befriended.v)) << "both sides, always";
  EXPECT_TRUE(b.felt.count(stim::duel_befriended.v));

  EXPECT_EQ(a.dish->duels().state().friends[0].s.met, 1) << "leaving by a double press asked no rematch";
  EXPECT_TRUE(a.contexts.count(uint8_t(BootContext::DuelOutcome))) << "the friends screen";
  EXPECT_FALSE(a.feltDueling.count(stim::knock.v)) << "a lock-in knock is the duel's, not the pet's";
  EXPECT_FALSE(a.feltDueling.count(stim::button.v));
  EXPECT_FALSE(a.feltDueling.count(stim::button_hold.v));
  EXPECT_TRUE(a.feltDueling.count(stim::shake.v)) << "a shake always reaches him";
  EXPECT_TRUE(a.hopped) << "and hops him";
  EXPECT_TRUE(a.prophesied) << "he foretells the friend's stance";
  EXPECT_TRUE(a.judged) << "and the reveal judges it";

  // A Spar leaves no wound: injury, toxin and life as his still twin's.
  const Creature& me = a.creature();
  const Creature& twin = bout.twin.creature();
  EXPECT_EQ(level(me, chem::injury), level(twin, chem::injury));
  EXPECT_EQ(level(me, chem::toxin), level(twin, chem::toxin));
  EXPECT_EQ(level(me, chem::life), level(twin, chem::life));
  EXPECT_EQ(me.scars().count, 0);
}

TEST(TwoDishes, TheLoserOfASparSwoonsNapsAndWakes) {
  for (uint64_t seed = 1; seed < 12; ++seed) {
    Bout bout(seed, kGrungo, SpeciesId{6});
    ASSERT_TRUE(bout.runDuel());
    for (Device* d : {&bout.a, &bout.b}) {
      if (!d->felt.count(stim::duel_fainted.v)) continue;
      EXPECT_GT(level(d->creature(), chem::daze), Fx::ratio(1, 10)) << "the swoon floods daze";
      EXPECT_TRUE(d->creature().body().asleep) << "and he naps";
      for (int t = 0; t < 6000 && d->creature().body().asleep; ++t) bout.step();
      EXPECT_FALSE(d->creature().body().asleep) << "and wakes within ten minutes";
      return;
    }
  }
  FAIL() << "no seed under 12 ended a Spar in a faint";
}

TEST(TwoDishes, AScrapWoundsByExactlyTheHeartsHeLostAndNothingElse) {
  Bout bout(3, kGrungo, SpeciesId{6});
  bout.a.kid.tier = bout.b.kid.tier = Stakes::Scrap;
  std::optional<uint8_t> last;
  uint8_t lost = 0;
  bool scrap = false;
  for (int t = 0; t < 6000; ++t) {
    bout.step();
    if (!bout.a.dish->duelActive()) {
      if (scrap) break;
      continue;
    }
    const DuelView v = bout.a.dish->duelView();
    if (v.phase < DuelPhase::Choosing || v.phase == DuelPhase::Friends) continue;
    scrap = scrap || v.agreed == Stakes::Scrap;
    if (last && *last > v.mine.hearts) lost = uint8_t(lost + *last - v.mine.hearts);
    last = v.mine.hearts;
  }
  ASSERT_TRUE(scrap) << "both kids picked Scrap";
  ASSERT_GT(lost, 0) << "seed 3 must cost grungo a heart";
  const Creature& me = bout.a.creature();
  const Creature& twin = bout.twin.creature();
  // woundFor's amounts land whole; from then on injury is his own chemistry,
  // healing a little and drawing a little life, as any injury does.
  const Fx woundFor = rule(Stakes::Scrap).injuryPerHeart * Fx::ratio(lost, 1);
  const Fx added = level(me, chem::injury) - level(twin, chem::injury);
  EXPECT_LE(added, woundFor) << "never more than the tier's wound per heart lost";
  EXPECT_GE(added, woundFor * Fx::ratio(98, 100)) << "healing over the duel takes under 2 percent back";
  EXPECT_EQ(level(me, chem::toxin), level(twin, chem::toxin));
  EXPECT_LE(level(me, chem::life), level(twin, chem::life));
  EXPECT_GE(level(me, chem::life), level(twin, chem::life) - Fx::ratio(1, 100)) << "only what the injury draws";
  EXPECT_GE(me.scars().count, 1) << "the first turn that cost him hearts scars";
}

// ---- the acceptance: a Finish ---------------------------------------------------------

TEST(TwoDishes, AFinishKillsOnceAndCrownsTheVictor) {
  for (uint64_t seed = 1; seed < 30; ++seed) {
    Bout bout(seed, kGrungo, SpeciesId{6}, true);
    bout.a.kid.tier = bout.b.kid.tier = Stakes::Mortal;
    bout.a.kid.mercy = bout.b.kid.mercy = Mercy::Finish;
    bout.runDuel();
    Device* fallen = !bout.a.alive() ? &bout.a : !bout.b.alive() ? &bout.b : nullptr;
    if (!fallen) continue;   // nobody was downed by turn 15: another seed
    Device* victor = fallen == &bout.a ? &bout.b : &bout.a;

    EXPECT_EQ(deaths(fallen->dish->lineage()), 1u) << "one Death entry";
    const Clutch* k = std::get_if<Clutch>(&fallen->dish->occupant());
    ASSERT_NE(k, nullptr) << "a clutch on the fallen side";
    EXPECT_EQ(k->cause, DeathCause::Injured) << "he died of his wounds";
    EXPECT_EQ(fallen->dish->appearance().kind, Appearance::Kind::Remains);
    for (int t = 0; t < 50; ++t) bout.step();
    EXPECT_EQ(deaths(fallen->dish->lineage()), 1u) << "and only one";

    const DuelBook& book = victor->dish->duels();
    ASSERT_EQ(book.state().trophies.size(), 1u) << "a trophy";
    EXPECT_EQ(book.state().trophies[0].fallen, fallen == &bout.a ? kGrungo : SpeciesId{6});
    EXPECT_TRUE(book.featsThisLife(0) & feat::slayer) << "the slayer feat";
    EXPECT_TRUE(victor->felt.count(stim::duel_slew.v)) << "and duel_slew";
    EXPECT_EQ(deaths(victor->dish->lineage()), 0u);
    EXPECT_EQ(victor->dish->appearance().trophyCount, 1) << "on the rim at home";

    // A reboot right after the fall: he loads dead and dies once, not twice.
    {
      MemStorage cut;
      cut.files = fallen->store.files;
      const uint32_t s = fallen == &bout.a ? 11 : 22;
      Dish back(cut, s, 0xD15400ull + s);
      NullLink link;
      for (uint32_t ms = 100; ms <= 500; ms += 100) back.tick(ms, link);
      EXPECT_EQ(deaths(back.lineage()), 1u);
    }

    // Each line's next generation: the fallen's hatches seeded against the
    // victor's element (the vendetta); the slayer's wears the war paint.
    const auto nextGeneration = [&](Device& d) {
      Clutch& k = std::get<Clutch>(d.dish->occupant());
      k.sinceDeath = Clutch::kVigilTicks;
      d.dish->pick(0);
      for (int t = 0; t < 200000 && !d.alive(); t += 10) d.dish->tick(d.ms += 1000, d.link);
      return d.alive();
    };
    victor->creature().fall();
    for (int t = 0; t < 3; ++t) bout.step();
    EXPECT_TRUE(victor->dish->lineage().legacyFeats() & feat::slayer) << "the feat reaches his Death entry";
    ASSERT_TRUE(nextGeneration(*victor));
    bool warPaint = false;
    for (const auto& m : victor->creature().phenotype().marks) warPaint = warPaint || m.layer == 2;
    EXPECT_TRUE(warPaint) << "the slayer's line wears the war paint from the next generation";

    ASSERT_TRUE(nextGeneration(*fallen));
    const uint16_t gen = fallen->creature().generation();
    ASSERT_TRUE(fallen->dish->duels().vendetta(gen).has_value()) << "his line remembers";
    const DuelSkill seeded = fallen->dish->duels().skill(gen);
    bool nudged = false;
    for (const auto& ctx : seeded.win)
      for (Q15 q : ctx) nudged = nudged || q.v != 0;
    EXPECT_TRUE(nudged) << "and the newborn's duel Skill is seeded against the victor";
    return;
  }
  FAIL() << "no seed under 30 downed a pet";
}

// ---- the friend egg -------------------------------------------------------------------

TEST(TwoDishes, BothHugsLayAFriendEggOnEachSide) {
  Bout bout(5, kGrungo, SpeciesId{6});
  bout.a.kid.hug = bout.b.kid.hug = true;
  ASSERT_TRUE(bout.runDuel());
  std::optional<FriendEgg> ea = FriendEggStore::load(bout.a.store), eb = FriendEggStore::load(bout.b.store);
  ASSERT_TRUE(ea && eb) << "a friend egg on both";
  EXPECT_EQ(ea->record.friendLineage, 0xD15400ull + 22);
  EXPECT_EQ(eb->record.friendLineage, 0xD15400ull + 11);
  EXPECT_EQ(ea->genome.hash(), bout.b.creature().genome().hash());
  EXPECT_EQ(ea->record.battleSeed, eb->record.battleSeed) << "the battle's seed is the cross's";
}

// ---- no button at all: the twins (controls.h) ------------------------------------------

// Two kids whose cases cover BOOT play a whole Mortal duel: a hang opens fight
// mode, knock knocks step the stakes to Mortal and a 3 s turn onto a side
// agrees to it, knocks lock the aimed stances, knock knocks throw specials,
// the winner turns his badge onto its side for the 3 s the skull takes and
// Finishes, and a hang leaves. BOOT never goes down.
TEST(NoButton, AWholeMortalDuelFinishedWithGesturesAlone) {
  for (uint64_t seed = 1; seed < 30; ++seed) {
    Bout bout(seed, kGrungo, SpeciesId{6}, true);
    for (Device* d : {&bout.a, &bout.b}) {
      d->kid.noButton = true;
      d->kid.tier = Stakes::Mortal;
      d->kid.mercy = Mercy::Finish;
      d->kid.special = true;
    }
    bout.runDuel();
    EXPECT_FALSE(bout.a.kid.everPressed || bout.b.kid.everPressed) << "BOOT went down";
    Device* fallen = !bout.a.alive() ? &bout.a : !bout.b.alive() ? &bout.b : nullptr;
    if (!fallen) continue;   // nobody was downed: another seed
    Device* victor = fallen == &bout.a ? &bout.b : &bout.a;
    EXPECT_EQ(deaths(fallen->dish->lineage()), 1u) << "seed " << seed;
    EXPECT_TRUE(victor->dish->duels().featsThisLife(0) & feat::slayer) << "seed " << seed;
    EXPECT_FALSE(bout.a.dish->duelActive() || bout.b.dish->duelActive()) << "a hang left fight mode";
    return;
  }
  FAIL() << "no seed under 30 finished a pet with gestures alone";
}

// The highlight follows the hand, not the tick: two samples into a lean
// the duel shows its aim (Dish::duelView, AimDetector), though no tick has
// run since, and the next tick locks that same aim.
TEST(NoButton, TheDuelShowsTheAimTwoSamplesIntoALean) {
  Bout bout(3, kGrungo, SpeciesId{6});
  for (Device* d : {&bout.a, &bout.b}) d->kid.noButton = true;
  int t = 0;
  while (t++ < 3000 && !(bout.a.dish->duelView().phase == DuelPhase::Choosing && !bout.a.dish->duelView().locked))
    bout.step();
  ASSERT_EQ(bout.a.dish->duelView().phase, DuelPhase::Choosing);
  Device& d = bout.a;
  for (int8_t want : {int8_t(0), int8_t(1), int8_t(2)}) {
    BodySample level;
    level.az = 1000;
    for (int tick = 0; tick < 5; ++tick) {   // level and let go: no side hold under way (its aim is from before the turn)
      for (int i = 0; i < 5; ++i) d.dish->sample(level, d.ms + 20 * i);
      d.ms += 100;
      d.dish->tick(d.ms, d.link);
    }
    EXPECT_EQ(d.dish->duelView().aim, -1);
    const AimTilt& a = kAimTilt[want];
    BodySample lean;
    lean.ax = a.ax, lean.ay = a.ay, lean.az = a.az;
    d.dish->sample(lean, d.ms + 20);
    d.dish->sample(lean, d.ms + 40);
    EXPECT_EQ(d.dish->duelView().aim, want) << "two samples in, before any tick";
    for (int i = 3; i < 5; ++i) d.dish->sample(lean, d.ms + 20 * i);
    d.ms += 100;
    d.dish->tick(d.ms, d.link);
    EXPECT_EQ(d.dish->duelView().aim, want) << "and the tick reads the same";
  }
}

// Turning him onto his side tips the tilt, so while he is held there the
// duel's aim is the one from before the turn: a heavy special lands where
// the kid aimed, not wherever the turn left the tilt.
TEST(NoButton, ASideHoldKeepsTheAimFromBeforeTheTurn) {
  Bout bout(3, kGrungo, SpeciesId{6});
  for (Device* d : {&bout.a, &bout.b}) d->kid.noButton = true;
  int t = 0;
  while (t++ < 3000 && !(bout.a.dish->duelView().phase == DuelPhase::Choosing && !bout.a.dish->duelView().locked))
    bout.step();
  ASSERT_EQ(bout.a.dish->duelView().phase, DuelPhase::Choosing);
  bout.step();   // the kid sees the new screen (and plans for it): the plan below replaces his
  ASSERT_EQ(bout.a.dish->duelView().phase, DuelPhase::Choosing);
  int checked = 0;
  for (int8_t want : {int8_t(0), int8_t(1)}) {
    Frame aim, side;
    aim.aim = want, side.side = true;
    bout.a.plan.clear();
    for (int i = 0; i < 20; ++i) bout.a.plan.push_back(aim);
    for (int i = 0; i < 8; ++i) bout.a.plan.push_back(side);
    bout.a.kid.wantDuel = false;
    for (int i = 0; i < 20; ++i) bout.step();
    if (bout.a.dish->duelView().phase != DuelPhase::Choosing) break;
    EXPECT_EQ(bout.a.dish->duelView().aim, want) << "aimed before the turn";
    for (int i = 0; i < 6; ++i) {
      bout.step();
      if (bout.a.dish->duelView().phase != DuelPhase::Choosing) break;
      EXPECT_EQ(bout.a.dish->duelView().aim, want) << "on his side, tick " << i;
      ++checked;
    }
  }
  EXPECT_GE(checked, 6) << "the aim was looked at on his side";
}

// Spared, with no button: a knock spares as it always did, and nobody dies.
TEST(NoButton, AMortalDuelSparedByAKnock) {
  for (uint64_t seed = 1; seed < 30; ++seed) {
    Bout bout(seed, kGrungo, SpeciesId{6}, true);
    for (Device* d : {&bout.a, &bout.b}) d->kid.noButton = true, d->kid.tier = Stakes::Mortal;
    ASSERT_TRUE(bout.runDuel()) << "seed " << seed;
    EXPECT_TRUE(bout.a.alive() && bout.b.alive()) << "seed " << seed;
    EXPECT_FALSE(bout.a.kid.everPressed || bout.b.kid.everPressed);
  }
}

// The hug, as a 3 s turn onto a side on the friends screen, lays the friend egg.
TEST(NoButton, BothSideHoldHugsLayAFriendEgg) {
  Bout bout(5, kGrungo, SpeciesId{6});
  for (Device* d : {&bout.a, &bout.b}) d->kid.noButton = true, d->kid.hug = true;
  ASSERT_TRUE(bout.runDuel());
  EXPECT_TRUE(FriendEggStore::load(bout.a.store) && FriendEggStore::load(bout.b.store)) << "a friend egg on both";
  EXPECT_FALSE(bout.a.kid.everPressed || bout.b.kid.everPressed);
}

// ---- the graft, consent, and what is written at once ----------------------------------

namespace {
// One device alone in the room, founded on the gift's starter: no duel genes.
struct Alone {
  Ether ether{1, Weather{}};
  Device d{ether, 1};
  Genome g = starterGenome(33);
  Alone() { d.boot(g, 33), d.growUp(); }
  void tick(Frame f = {}) {
    ether.advance(d.ms / 100);
    d.plan.clear();
    d.plan.push_back(f);
    d.kid.wantDuel = false;
    d.step();
  }
  void doublePress() { tick({-1, true}), tick(), tick({-1, true}), tick(), tick(), tick(); }
};
}  // namespace

TEST(Graft, APreDuelLineGetsTheDuelGenesOnceAsOneCheckpoint) {
  Alone a;
  const size_t genes = a.d.creature().genome().geneCount();
  const uint32_t marks = checkpoints(a.d.dish->lineage());
  a.doublePress();
  ASSERT_TRUE(a.d.dish->duelActive()) << "he is ready: the lobby opens";
  EXPECT_EQ(a.d.creature().genome().geneCount(), genes + kDuelGeneCount);
  EXPECT_EQ(checkpoints(a.d.dish->lineage()), marks + 1) << "recorded as one Checkpoint";
  const uint32_t grown = a.d.creature().genome().hash();
  {
    MemStorage cut;
    cut.files = a.d.store.files;
    Dish back(cut, 33, 0xD15400ull + 33);
    EXPECT_EQ(std::get<Creature>(back.occupant()).genome().hash(), grown) << "saved at once";
  }
  a.doublePress();
  EXPECT_FALSE(a.d.dish->duelActive()) << "a double press closes the lobby";
  for (int t = 0; t < 1300; ++t) a.tick();   // past the rest after a duel
  a.doublePress();
  ASSERT_TRUE(a.d.dish->duelActive());
  EXPECT_EQ(a.d.creature().genome().hash(), grown) << "idempotent";
  EXPECT_EQ(checkpoints(a.d.dish->lineage()), marks + 1);
}

TEST(Consent, ThePhoneCanNeitherOpenADuelNorLockAMove) {
  {
    Alone a;
    for (int i = 0; i < 3; ++i) a.d.dish->fire(stim::button_double), a.tick();
    EXPECT_FALSE(a.d.dish->duelActive());
    EXPECT_EQ(a.d.air.opens, 0u) << "a closed device is deaf and dumb";
    EXPECT_EQ(a.d.air.framesSent, 0u);
  }
  Bout bout(7, kGrungo, SpeciesId{6});
  bout.a.kid.wantDuel = false;   // nobody presses BOOT on a
  for (int t = 0; t < 300; ++t) bout.step();
  EXPECT_EQ(bout.a.air.opens, 0u) << "b's beacons never reach a closed device";
  bout.a.kid.wantDuel = true;
  for (int t = 0; t < 600 && bout.a.dish->duelView().phase != DuelPhase::Choosing; ++t) bout.step();
  ASSERT_EQ(bout.a.dish->duelView().phase, DuelPhase::Choosing);
  for (int t = 0; t < 20; ++t) {
    bout.a.plan.assign(1, Frame{0});   // the kid aims and does nothing else
    bout.a.dish->fire(stim::knock), bout.a.dish->fire(stim::button);
    bout.step();
    ASSERT_FALSE(bout.a.dish->duelView().locked) << "a knock or a press from the phone locks nothing";
  }
}

TEST(AtOnce, ACablePulledMidScrapKeepsEveryWoundSoFar) {
  Bout bout(3, kGrungo, SpeciesId{6});
  bout.a.kid.tier = bout.b.kid.tier = Stakes::Scrap;
  for (int t = 0; t < 4000; ++t) {
    bout.step();
    if (!bout.a.dish->duelActive()) continue;
    const DuelView v = bout.a.dish->duelView();
    if (v.agreed != Stakes::Scrap || v.phase < DuelPhase::Choosing || v.mine.hearts == v.mine.maxHearts) continue;
    const Fx live = level(bout.a.creature(), chem::injury);
    MemStorage cut;
    cut.files = bout.a.store.files;   // the cable comes out now
    Dish back(cut, 11, 0xD15400ull + 11);
    const Creature& rebooted = std::get<Creature>(back.occupant());
    EXPECT_GT(level(rebooted, chem::injury), level(bout.twin.creature(), chem::injury)) << "the wound was saved";
    EXPECT_LE(level(rebooted, chem::injury), live + Fx::ratio(1, 1000));
    EXPECT_EQ(rebooted.scars().count, bout.a.creature().scars().count);
    return;
  }
  FAIL() << "grungo never lost a heart";
}

TEST(AtOnce, ADownedPetWhoseCableComesOutBeforeTheVerdictLivesAndFled) {
  for (uint64_t seed = 1; seed < 30; ++seed) {
    Bout bout(seed, kGrungo, SpeciesId{6}, true);
    bout.a.kid.tier = bout.b.kid.tier = Stakes::Mortal;
    bout.a.kid.mercy = bout.b.kid.mercy = Mercy::Finish;
    for (int t = 0; t < 4000; ++t) {
      bout.step();
      for (Device* d : {&bout.a, &bout.b}) {
        if (!d->dish->duelActive() || !d->dish->duelView().iAmDown) continue;
        MemStorage cut;
        cut.files = d->store.files;
        const uint32_t mine = d == &bout.a ? 11 : 22, theirs = d == &bout.a ? 22 : 11;
        Dish back(cut, mine, 0xD15400ull + mine);
        EXPECT_TRUE(std::holds_alternative<Creature>(back.occupant())) << "he lives";
        const std::optional<FriendSummary> f = back.duels().friendOf(0xD15400ull + theirs);
        ASSERT_TRUE(f.has_value());
        EXPECT_EQ(f->lost, 1) << "fled: a loss to the victor's line";
        return;
      }
      if (t > 50 && !bout.a.dish->duelActive() && !bout.b.dish->duelActive()) break;
    }
  }
  FAIL() << "no seed under 30 downed a pet";
}

// ---- #57: a win's spoils, food taken from the loser's pantry for the winner's ------

namespace {

GeneUid metabolismOf(const Genome& g) {
  GeneUid uid{};
  g.forEach([&](const GeneView& v) {
    if (v.header.type == GeneKindOf<MetabolismGene>::value) uid = v.header.uid;
  });
  return uid;
}

// Both pantries at `a` and `b`, after a hand has been on each badge (so no
// homecoming lands later), with the refill slowed to hours: what moves in
// the duel is only the spoils.
void setPantries(Bout& bout, uint8_t a, uint8_t b) {
  for (Device* d : {&bout.a, &bout.b}) {
    BodySample held;
    held.ax = 450, held.az = 893;
    for (int t = 0; t < 10; ++t) {
      for (uint8_t i = 0; i < 5; ++i) d->dish->sample(held, d->ms + 20 * i);
      d->dish->tick(d->ms += 100, d->link);
    }
    Creature& c = d->creature();
    ASSERT_TRUE(d->dish->editGene(metabolismOf(c.genome()), 1, 250));
    d->dish->habitat().pantry = d == &bout.a ? a : b;
    d->dish->habitat().pantryTick = d->dish->tickCount();
  }
}

// Who won, as both books say it; nullopt for a draw. Both must agree.
std::optional<bool> aWon(Bout& bout) {
  const std::optional<Meeting>& ma = bout.a.dish->lastMeeting();
  const std::optional<Meeting>& mb = bout.b.dish->lastMeeting();
  EXPECT_TRUE(ma && mb);
  if (!ma || !mb) return std::nullopt;
  EXPECT_EQ(ma->outcomeHash, mb->outcomeHash) << "one battle, one outcome";
  if (ma->ending != Ending::AWon && ma->ending != Ending::BWon) return std::nullopt;
  return (ma->ending == Ending::AWon) == (ma->me == Side::A);
}

// One Spar from pantries `pa` and `pb`, then a while after it. Returns how
// many bouts were decided; checks the food on both badges.
int spoilsBout(uint64_t seed, Weather w, uint8_t pa, uint8_t pb) {
  Bout bout(seed, kGrungo, SpeciesId{6}, false, w);
  setPantries(bout, pa, pb);
  if (::testing::Test::HasFatalFailure()) return 0;
  EXPECT_TRUE(bout.runDuel(9000)) << "seed " << seed;
  const std::optional<bool> won = aWon(bout);
  bool shownA = false, shownB = false;
  for (int t = 0; t < 600; ++t) {   // a minute after: shown once the dish is back, and never again
    bout.step();
    shownA = shownA || bout.a.dish->appearance().feed.spoils != 0;
    shownB = shownB || bout.b.dish->appearance().feed.spoils != 0;
  }
  const int na = bout.a.dish->habitat().pantry, nb = bout.b.dish->habitat().pantry;
  const std::string at = "seed " + std::to_string(seed) + " a=" + std::to_string(na) + " b=" + std::to_string(nb);
  EXPECT_EQ(na + nb, pa + pb) << "food moves, it is never made or lost: " << at;
  if (!won) {
    EXPECT_EQ(na, pa) << "a draw moves nothing: " << at;
    return 0;
  }
  const int winnerHad = *won ? pa : pb, loserHad = *won ? pb : pa;
  const int moved = std::min<int>({food::kSpoils, loserHad, 8 - winnerHad});
  EXPECT_EQ(*won ? na : nb, winnerHad + moved) << "the winner gains: " << at;
  EXPECT_EQ(*won ? nb : na, loserHad - moved) << "the loser loses the same: " << at;
  if (moved) {
    EXPECT_TRUE(shownA && shownB) << "both screens show the pellet go: " << at;
  }
  return 1;
}

}  // namespace

TEST(Spoils, AWinTakesAPelletFromTheLoserForTheWinnerOnBothBadgesOnce) {
  int decided = 0;
  for (uint64_t seed = 1; seed <= 6; ++seed) decided += spoilsBout(seed, Weather{}, 3, 3);
  EXPECT_GE(decided, 3) << "enough Spars ended in a win to see the spoils";
}

// Under BATTLES.md's storm (30% lost, 10% doubled, 20% late) both badges still
// move the same food, once.
TEST(Spoils, UnderAStormBothBadgesStillAgree) {
  constexpr Weather kStorm{300, 100, 200, 8};
  int decided = 0;
  for (uint64_t seed = 1; seed <= 6; ++seed) decided += spoilsBout(seed, kStorm, 2, 5);
  EXPECT_GE(decided, 2);
}

// Nothing to take, or no room to put it: nothing moves, on either badge.
TEST(Spoils, AnEmptyLoserOrAFullWinnerMovesNothing) {
  spoilsBout(2, Weather{}, 0, 0);
  spoilsBout(2, Weather{}, 8, 8);
}

// A badge from before the spoils sends its card's fourth mark slot as zeros:
// pantry 0 and room 0, so spoils with it are none, on both sides.
TEST(Spoils, TheCardCarriesThemWhereAnOldBadgeSendsZeros) {
  BattleCard c;
  c.pantry = 5, c.room = 3;
  c.look.markCount = 2;
  c.look.marks[0] = {0, 4}, c.look.marks[1] = {1, 2};
  uint8_t buf[kDatagramMax];
  BeaconPacket beacon;
  beacon.card = c;
  const size_t n = encode(Packet{beacon}, buf);
  ASSERT_EQ(n, kBeaconWireBytes) << "the frame is the same size";
  const std::optional<Packet> back = decode(buf, n);
  ASSERT_TRUE(back);
  const BattleCard& got = std::get<BeaconPacket>(*back).card;
  EXPECT_EQ(got.pantry, 5);
  EXPECT_EQ(got.room, 3);
  EXPECT_EQ(got.look.marks[3].layer, 0) << "the slot reads as no mark";
  EXPECT_EQ(got.look.marks[0].variant, 4);
  c.look.markCount = 4;   // a card with four marks has no slot for them: none
  c.look.marks[3] = {2, 1};
  beacon.card = c;
  const std::optional<Packet> full = decode(buf, encode(Packet{beacon}, buf));
  ASSERT_TRUE(full);
  EXPECT_EQ(std::get<BeaconPacket>(*full).card.pantry, 0);
  EXPECT_EQ(std::get<BeaconPacket>(*full).card.look.marks[3].layer, 2);
}

// The friend egg's life, cut after every write from the death to the hatch:
// whatever a power cut leaves, the friend's egg is used once or lost, never twice.
TEST(FriendEgg, APowerCutAtAnyStepNeverHatchesItTwice) {
  Bout bout(5, kGrungo, SpeciesId{6});
  bout.a.kid.hug = bout.b.kid.hug = true;
  ASSERT_TRUE(bout.runDuel());
  ASSERT_TRUE(FriendEggStore::load(bout.a.store));
  Device& a = bout.a;
  a.store.taping = true;
  a.creature().fall();
  for (int t = 0; t < 3; ++t) a.step();
  Clutch& k = std::get<Clutch>(a.dish->occupant());
  ASSERT_NE(k.friendSlot, 0) << "the friend egg joined the clutch";
  k.sinceDeath = Clutch::kVigilTicks;
  ASSERT_TRUE(a.dish->pick(uint8_t(k.friendSlot - 1)));
  for (int t = 0; t < 3; ++t) a.step();
  a.dish->flush();
  a.store.taping = false;
  ASSERT_GE(a.store.tape.size(), 4u);

  for (size_t i = 0; i < a.store.tape.size(); ++i) {
    MemStorage cut;
    cut.files = a.store.tape[i];
    Dish back(cut, 11, 0xD15400ull + 11);
    if (Clutch* again = std::get_if<Clutch>(&back.occupant())) {
      again->sinceDeath = Clutch::kVigilTicks;
      back.pick(again->friendSlot ? uint8_t(again->friendSlot - 1) : 0);
    }
    uint32_t used = 0;
    for (const auto& f : cut.files) used += f.first.rfind(FriendEggStore::kUsed, 0) == 0;
    EXPECT_LE(used, 1u) << "cut after write " << i;
    if (std::holds_alternative<Egg>(back.occupant())) {
      EXPECT_EQ(cut.files.count(FriendEggStore::kFile), 0u) << "a born egg leaves no friend egg waiting, cut " << i;
    }
  }
}

// ---- three losses want a cuddle -------------------------------------------------------

TEST(Readiness, ThreeLossesInARowWantACuddleAndACuddleIsEnough) {
  Alone a;
  {
    DuelBook book = DuelBook::open(a.d.store);
    for (uint32_t i = 0; i < 3; ++i) {
      Meeting m;
      m.friendCard.lineageId = 0xF00;
      m.battle.seed = 100 + i;
      m.me = Side::A;
      m.ending = Ending::BWon;
      book.record(m, 0, 10 + i, 0);
    }
  }
  a.d.dish.reset();
  a.d.boot(a.g, 33);
  a.d.ms = 0;
  a.doublePress();
  EXPECT_FALSE(a.d.dish->duelView().phase == DuelPhase::Lobby && a.d.dish->appearance().declined == Decline::None);
  EXPECT_EQ(a.d.dish->appearance().declined, Decline::WantsACuddle) << "a hug, as a picture";
  if (a.d.dish->duelActive()) a.doublePress();   // he beaconed his picture; close the lobby
  for (int t = 0; t < 40; ++t) a.tick(Frame{-1, false, false, false, true});
  for (int t = 0; t < 5; ++t) a.tick();
  EXPECT_EQ(a.d.dish->duels().lossesInARow(), 0) << "the cuddle is in the book";
  a.doublePress();
  EXPECT_TRUE(a.d.dish->duelActive());
  EXPECT_EQ(a.d.dish->appearance().declined, Decline::None);
}

// ---- BOOT, everywhere: one row of kBoot at a time --------------------------------------

namespace {
void expectRow(const Dish& d, BootContext ctx, const char* where) {
  EXPECT_EQ(d.bootContext(), ctx) << where;
  const Appearance a = d.appearance();
  EXPECT_EQ(a.boot.glyph, bootMeaning(ctx).hint) << where;
  EXPECT_EQ(a.boot.hasDouble, bootMeaning(ctx).doublePress != BootAct::Nothing) << where;
}
uint8_t pellets(const Dish& d) {
  uint8_t n = 0;
  const Appearance a = d.appearance();
  for (uint8_t k = 0; k < a.itemCount; ++k) n += a.items[k].what == Appearance::Item::What::Pellet || a.items[k].what == Appearance::Item::What::RottenPellet;
  return n;
}
}  // namespace

TEST(BootRows, AwakeAPressFeedsAHoldTucksInADoubleOpensTheLobby) {
  Alone a;
  expectRow(*a.d.dish, BootContext::Awake, "awake");
  EXPECT_TRUE(a.d.dish->appearance().boot.hasHold) << "the moon ring: a hold tucks him in";
  const uint8_t before = pellets(*a.d.dish);
  a.tick({-1, true}), a.tick(), a.tick(), a.tick(), a.tick(), a.tick();
  EXPECT_EQ(pellets(*a.d.dish), before + 1) << "a press drops a pellet";
  a.d.felt.clear();
  for (int t = 0; t < 6; ++t) a.tick({-1, true});
  EXPECT_GT(a.d.dish->appearance().boot.holdFill, 0) << "while held, the ring fills";
  for (int t = 0; t < 8; ++t) a.tick({-1, true});
  a.tick();
  EXPECT_TRUE(a.d.felt.count(stim::button_hold.v)) << "a hold is the tuck-in";
  a.doublePress();
  EXPECT_TRUE(a.d.dish->duelActive()) << "a double press opens the lobby";
}

// The same rows with no button (controls.h): a knock knock feeds, a turn onto
// a side held tucks him in (the ring filling as it would for BOOT), a hang
// opens the lobby, and a knock knock in the lobby still feeds him.
TEST(BootRows, NoButtonAKnockKnockFeedsASideHoldTucksInAHangOpensTheLobby) {
  Alone a;
  Frame kk, side, hang;
  kk.knock2 = true, side.side = true, hang.hang = true;
  a.tick(), a.tick();
  const uint8_t before = pellets(*a.d.dish);
  a.tick(kk), a.tick(), a.tick();
  EXPECT_EQ(pellets(*a.d.dish), before + 1) << "a knock knock drops a pellet";
  EXPECT_TRUE(has(diags(a.d), "gesture knock_knock=press"));
  a.d.felt.clear();
  for (int t = 0; t < 6; ++t) a.tick(side);
  EXPECT_GT(a.d.dish->appearance().boot.holdFill, 0) << "while held on his side, the ring fills";
  for (int t = 0; t < 16; ++t) a.tick(side);
  a.tick();
  EXPECT_TRUE(a.d.felt.count(stim::button_hold.v)) << "a side hold is the tuck-in";
  EXPECT_TRUE(has(diags(a.d), "gesture side=hold ms=2000"));
  EXPECT_EQ(pellets(*a.d.dish), before + 1) << "and drops nothing";
  for (int t = 0; t < 4; ++t) a.tick();
  for (int t = 0; t < 12; ++t) a.tick(hang);
  a.tick();
  EXPECT_TRUE(a.d.dish->duelActive()) << "a hang opens the lobby";
}

// Face down is bedtime, always: wide awake and rested, laid face down for the
// lid's 30 s, he is asleep, whatever his genes make of the lid.
TEST(BootRows, FaceDownAlwaysPutsHimToSleep) {
  Alone a;
  for (const auto& d : DRIVES) a.d.creature().inject(driveChem(d.id), Fx::zero());
  a.tick(), a.tick();
  ASSERT_FALSE(a.d.creature().body().asleep);
  Frame lid;
  lid.lid = true;
  for (int t = 0; t < 330; ++t) a.tick(lid);
  bool slept = false;
  for (int t = 0; t < 5; ++t) a.tick(lid), slept = slept || a.d.creature().body().asleep;
  EXPECT_TRUE(a.d.felt.count(stim::lid_down.v)) << "the lid came down";
  EXPECT_TRUE(slept) << "33 s face down";
}

// The clutch with no button: knock knocks step through the eggs, a side hold keeps one.
TEST(BootRows, NoButtonTheClutchIsKnockedThroughAndASideHoldKeeps) {
  Alone a;
  Frame kk, side;
  kk.knock2 = true, side.side = true;
  a.d.creature().fall();
  a.tick(), a.tick();
  Clutch& k = std::get<Clutch>(a.d.dish->occupant());
  ASSERT_EQ(k.count, 2);
  k.sinceDeath = Clutch::kVigilTicks;
  a.tick();
  a.tick(kk), a.tick(), a.tick(), a.tick(), a.tick(), a.tick();
  EXPECT_EQ(std::get<Clutch>(a.d.dish->occupant()).cursor, 1) << "a knock knock: the next egg";
  ASSERT_TRUE(std::holds_alternative<Clutch>(a.d.dish->occupant())) << "and it keeps nothing";
  for (int t = 0; t < 22; ++t) a.tick(side);
  a.tick();
  ASSERT_TRUE(std::holds_alternative<Egg>(a.d.dish->occupant())) << "a side hold keeps the egg";
}

// Asleep, a double press asks for a fight (he is woken for it); too sleepy,
// he is left to sleep, the first press still fed him, and the screen says why.
TEST(BootRows, AsleepAndTooSleepyADoublePressIsAPelletAndAReason) {
  Alone a;
  a.d.creature().inject(driveChem(drive::sleepiness), Fx::ratio(9, 10));
  a.d.creature().force(action::sleep);
  for (int t = 0; t < 3; ++t) a.tick();
  ASSERT_TRUE(a.d.creature().body().asleep);
  expectRow(*a.d.dish, BootContext::Asleep, "asleep");
  const uint8_t before = pellets(*a.d.dish);
  a.doublePress();
  EXPECT_FALSE(a.d.dish->duelActive()) << "too sleepy to be woken for a fight";
  EXPECT_EQ(a.d.air.opens, 0u);
  EXPECT_EQ(pellets(*a.d.dish), before + 1) << "the first press fed him; the second asked for a fight";
  EXPECT_EQ(a.d.dish->appearance().noFight, NoFight::TooSleepy);
}

TEST(BootRows, AnEggIsPeekedAndRemainsGetAFlowerAndTheClutchIsTappedThrough) {
  Alone a;
  a.d.creature().fall();
  a.tick(), a.tick();
  expectRow(*a.d.dish, BootContext::Vigil, "vigil");
  a.tick({-1, true}), a.tick();
  EXPECT_EQ(a.d.dish->appearance().echo, BootAct::LayFlower);
  Clutch& k = std::get<Clutch>(a.d.dish->occupant());
  ASSERT_EQ(k.count, 2) << "an adult's clutch";
  k.sinceDeath = Clutch::kVigilTicks;
  a.tick();
  expectRow(*a.d.dish, BootContext::ClutchPick, "clutch");
  a.tick({-1, true}), a.tick(), a.tick(), a.tick(), a.tick(), a.tick();
  EXPECT_EQ(std::get<Clutch>(a.d.dish->occupant()).cursor, 1) << "a press: the next egg";
  a.doublePress();
  EXPECT_EQ(std::get<Clutch>(a.d.dish->occupant()).cursor, 1) << "a quick double is two taps, round a clutch of two";
  for (int t = 0; t < 14; ++t) a.tick({-1, true});
  a.tick();
  ASSERT_TRUE(std::holds_alternative<Egg>(a.d.dish->occupant())) << "a hold keeps the egg";
  expectRow(*a.d.dish, BootContext::Egg, "egg");
  a.tick({-1, true}), a.tick();
  EXPECT_EQ(a.d.dish->appearance().echo, BootAct::PeekEgg);
}

TEST(BootRows, ASealedDishShowsTheLockAndAPressMovesNothing) {
  MemStorage store;
  DishOptions o;
  o.sealed = true;
  Dish d(store, 1, 2, o);
  NullLink link;
  const uint32_t hash = d.hash();
  BodySample down;
  down.buttonDown = true;
  for (uint32_t ms = 0; ms < 3000; ms += 20) d.sample(ms < 200 ? down : BodySample{}, ms), d.tick(ms, link);
  expectRow(d, BootContext::Sealed, "sealed");
  EXPECT_EQ(d.hash(), hash);
  EXPECT_TRUE(store.files.empty()) << "nothing written";
}

// The duel's rows, as a Mortal duel visits them: each context the screen is
// in shows that row's hint, and the ones a duel must pass through all appear.
TEST(BootRows, EveryDuelRowShowsItsOwnHint) {
  for (uint64_t seed = 1; seed < 30; ++seed) {
    Bout bout(seed, kGrungo, SpeciesId{6}, true);
    bout.a.kid.tier = bout.b.kid.tier = Stakes::Mortal;
    bout.a.kid.mercy = bout.b.kid.mercy = Mercy::Finish;
    bout.a.kid.special = bout.b.kid.special = true;
    std::set<uint8_t> seen;
    for (int t = 0; t < 6000; ++t) {
      bout.step();
      for (Device* d : {&bout.a, &bout.b}) {
        if (!d->dish->duelActive()) continue;
        const BootContext ctx = d->dish->bootContext();
        seen.insert(uint8_t(ctx));
        ASSERT_EQ(d->dish->appearance().boot.glyph, bootMeaning(ctx).hint) << "context " << int(ctx);
      }
      if (t > 50 && !bout.a.dish->duelActive() && !bout.b.dish->duelActive()) break;
    }
    if (!seen.count(uint8_t(BootContext::Fallen)) || !seen.count(uint8_t(BootContext::WindingUp))) continue;
    // A fall ends the duel on its last scene; the friends screen is the Spar test's.
    for (BootContext c : {BootContext::Lobby, BootContext::Staking, BootContext::DuelTurn, BootContext::WindingUp,
                          BootContext::MercyWinner, BootContext::MercyLoser, BootContext::Fallen})
      EXPECT_TRUE(seen.count(uint8_t(c))) << "never in context " << int(c);
    return;
  }
  FAIL() << "no seed under 30 queued a special and finished a pet";
}

TEST(BootRows, APressNeverFinishesAPetNorAgreesToAMortalDuel) {
  {
    Bout bout(1, kGrungo, SpeciesId{6}, true);
    bout.a.kid.tier = bout.b.kid.tier = Stakes::Mortal;
    bout.a.kid.pressForMortal = true;
    for (int t = 0; t < 400; ++t) {
      bout.step();
      ASSERT_FALSE(bout.a.dish->duelView().confirmed && bout.a.dish->duelView().chosen == Stakes::Mortal)
          << "a press under the skull confirms nothing";
    }
  }
  for (uint64_t seed = 1; seed < 30; ++seed) {
    Bout bout(seed, kGrungo, SpeciesId{6}, true);
    bout.a.kid.tier = bout.b.kid.tier = Stakes::Mortal;
    bout.a.kid.mercy = bout.b.kid.mercy = Mercy::Spare;   // a knock
    bout.a.kid.spareByPress = bout.b.kid.spareByPress = true;
    bout.runDuel();
    if (!bout.a.alive() || !bout.b.alive()) FAIL() << "a press finished a pet, seed " << seed;
    const DuelBook& ab = bout.a.dish->duels();
    const DuelBook& bb = bout.b.dish->duels();
    const auto spared = [](const DuelBook& b) {
      uint8_t n = 0;
      b.forEachFriend([&](const FriendSummary& f) { n += f.owesUs + f.weOwe; });
      return n;
    };
    if (spared(ab) + spared(bb) == 0) continue;   // nobody was downed: another seed
    EXPECT_EQ(deaths(bout.a.dish->lineage()) + deaths(bout.b.dish->lineage()), 0u);
    return;
  }
  FAIL() << "no seed under 30 downed a pet";
}

// ---- the duel scene, from a live view ---------------------------------------------------

namespace {
// D9_FRAMES=<dir> writes the first frame of each phase there, to look at.
void dump(const paint::Canvas240& cv, const char* name) {
  const char* dir = std::getenv("D9_FRAMES");
  if (!dir) return;
  const std::string path = std::string(dir) + "/" + name + ".ppm";
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  std::fprintf(f, "P6 240 240 255\n");
  for (uint16_t p : cv.px) {
    const uint8_t rgb[3] = {uint8_t((p >> 11) << 3), uint8_t(((p >> 5) & 63) << 2), uint8_t((p & 31) << 3)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
}
}  // namespace

TEST(Scene, EveryPhaseOfALiveDuelDrawsItsAct) {
  static paint::Canvas240 cv;
  static paint::DuelScene scene;
  for (uint64_t seed = 1; seed < 30; ++seed) {
    Bout bout(seed, kGrungo, SpeciesId{6}, true);
    bout.a.kid.tier = bout.b.kid.tier = Stakes::Mortal;
    bout.a.kid.mercy = bout.b.kid.mercy = Mercy::Finish;
    bout.a.kid.special = bout.b.kid.special = true;
    std::set<uint8_t> drawn;
    for (int t = 0; t < 6000; ++t) {
      bout.step();
      for (Device* d : {&bout.a, &bout.b}) {
        if (!d->dish->duelActive()) continue;
        const DuelView v = d->dish->duelView();
        if (v.phase == DuelPhase::Lobby) continue;   // the dish stays up until a friend is matched
        const Appearance me = d->dish->appearance();
        scene = paint::sceneOf(v, me);
        paint::drawDuel(scene, paint::packFor(me.species), paint::packFor(v.them.species), cv, d->ms);
        uint32_t lit = 0;
        for (uint16_t p : cv.px) lit += p != 0;
        ASSERT_GT(lit, 20000u) << "phase " << int(v.phase) << " drew a disc";
        if (drawn.insert(uint8_t(v.phase) * 2 + (d == &bout.a)).second) {
          char name[32];
          std::snprintf(name, sizeof name, "%s_phase%d", d == &bout.a ? "a" : "b", int(v.phase));
          dump(cv, name);
        }
      }
      if (t > 50 && !bout.a.dish->duelActive() && !bout.b.dish->duelActive()) break;
    }
    if (!drawn.count(uint8_t(DuelPhase::Fallen) * 2) && !drawn.count(uint8_t(DuelPhase::Fallen) * 2 + 1)) continue;
    for (DuelPhase p : {DuelPhase::Greeting, DuelPhase::Staking, DuelPhase::Choosing, DuelPhase::Revealing, DuelPhase::Mercy})
      EXPECT_TRUE(drawn.count(uint8_t(p) * 2) || drawn.count(uint8_t(p) * 2 + 1)) << "never drew phase " << int(p);
    return;
  }
  FAIL() << "no seed under 30 finished a pet";
}

TEST(BootRows, LeavingTheFriendsScreenNeverAsksARematch) {
  Bout bout(7, kGrungo, SpeciesId{6});
  bout.b.kid.askRematch = true;   // the friend wants another bout
  bout.a.kid.leaveAfter = 30;     // my kid sees that, and leaves with a double press
  bool sawAsk = false, wasFriends = false, anotherBout = false;
  auto watch = [&] {
    bout.step();
    const DuelView v = bout.a.dish->duelView();
    sawAsk = sawAsk || (v.phase == DuelPhase::Friends && v.theyAskedRematch);
    // A press that asked on the way out reopens at once, since the friend asked too.
    anotherBout = anotherBout || (wasFriends && uint8_t(v.phase) >= uint8_t(DuelPhase::Greeting) && v.phase != DuelPhase::Friends);
    wasFriends = wasFriends || v.phase == DuelPhase::Friends;
  };
  for (int t = 0; t < 4000; ++t) {
    watch();
    if (sawAsk && !bout.a.dish->duelActive()) break;
  }
  ASSERT_TRUE(sawAsk) << "the friend's ask reached my screen first";
  for (int t = 0; t < 100; ++t) watch();
  EXPECT_FALSE(anotherBout) << "from the friends screen the only way on is out: fight mode, then out of it";
  EXPECT_FALSE(bout.a.dish->duelActive()) << "I left";
  EXPECT_EQ(bout.a.dish->duels().state().friends[0].s.met, 1) << "and no second bout began";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
