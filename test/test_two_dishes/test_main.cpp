// Two whole Dishes on one ether (docs/design/BATTLES.md, testing item 5 and
// unit D9's cases): each with its own MemStorage and FakeAir, driven by 50 Hz
// BodySamples from a scripted kid who reads only the duel view, as a kid
// reads the screen.
#include <gtest/gtest.h>
#include <deque>
#include <optional>
#include <set>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/friend_egg.h"
#include "blorb/species.h"
#include "blorb/starter_duel.h"
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
  bool down = false, tap = false, shake = false, cradle = false, lid = false;
};

BodySample sampleOf(const Frame& f, uint8_t i) {
  BodySample s;
  s.az = 1000;
  if (f.aim >= 0) s.ax = kAimTilt[f.aim].ax, s.ay = kAimTilt[f.aim].ay, s.az = kAimTilt[f.aim].az;
  if (f.cradle) s.ax = 450, s.ay = 0, s.az = 893;
  if (f.lid) s.ax = 0, s.ay = 0, s.az = -1000;
  if (f.shake) s.ax = 0, s.ay = 0, s.az = i % 2 ? 1700 : 1000;   // a jolt of 0.7 g every other sample
  s.tapCode = f.tap && i == 0 ? 1 : 0;
  s.buttonDown = f.down;
  return s;
}

enum class Mercy : uint8_t { Spare, Finish };

// How one kid plays, everything read off the view.
struct Kid {
  Stakes tier = Stakes::Spar;
  Mercy mercy = Mercy::Spare;
  bool hug = false, shakeOnce = false, wantDuel = true, leave = true;
  Rng rng = Rng::seeded(1);
  bool shook = false;

  void plan(const DuelView& v, bool dueling, std::deque<Frame>& out) {
    auto hold = [&](int8_t aim, int n, Frame f) { for (int i = 0; i < n; ++i) { f.aim = aim; out.push_back(f); } };
    if (!dueling) {
      if (!wantDuel) return;
      wantDuel = false;
      hold(-1, 1, Frame{-1, true}), hold(-1, 1, Frame{}), hold(-1, 1, Frame{-1, true}), hold(-1, 3, Frame{});
      return;
    }
    switch (v.phase) {
      case DuelPhase::Lobby:
        if (v.peerCount > 0) hold(0, 4, Frame{}), hold(0, 1, Frame{-1, false, true}), hold(0, 2, Frame{});
        return;
      case DuelPhase::Staking: {
        if (v.confirmed) return hold(-1, 1, Frame{});
        const int8_t t = int8_t(std::min(uint8_t(tier), uint8_t(v.ceiling[0])));
        hold(t, 4, Frame{});
        if (t == 0) hold(t, 1, Frame{-1, false, true});
        else if (t == 1) hold(t, 2, Frame{-1, true}), hold(t, 1, Frame{});
        else hold(t, 33, Frame{-1, true}), hold(t, 1, Frame{});
        return;
      }
      case DuelPhase::Choosing:
        if (v.locked) return hold(-1, 1, Frame{});
        if (shakeOnce && !shook && v.lastTurn >= 1) {
          shook = true;
          return hold(-1, 8, Frame{-1, false, false, true}), hold(-1, 4, Frame{});
        }
        {
          const int8_t a = int8_t(rng.below(3));
          hold(a, 4, Frame{}), hold(a, 1, Frame{-1, false, true}), hold(-1, 2, Frame{});
        }
        return;
      case DuelPhase::Mercy:
        if (v.theyAreDown && v.verdict == Verdict::None) {
          if (mercy == Mercy::Finish) hold(-1, 34, Frame{-1, true}), hold(-1, 1, Frame{});
          else hold(-1, 1, Frame{-1, false, true}), hold(-1, 5, Frame{});
        }
        return;
      case DuelPhase::Friends:
        if (hug && !v.iHugged) return hold(-1, 34, Frame{-1, true}), hold(-1, 1, Frame{});
        if (hug && !(v.eggLaid || v.eggFizzled)) return hold(-1, 1, Frame{});
        if (leave) hold(-1, 1, Frame{-1, true}), hold(-1, 1, Frame{}), hold(-1, 1, Frame{-1, true}), hold(-1, 3, Frame{});
        return;
      default: return;
    }
  }
};

// One device: its storage, radio, Dish and the kid holding it.
struct Device {
  MemStorage store;
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
    shownPhase = v.phase;
    if (plan.empty()) kid.plan(v, dueling, plan);
    Frame f;
    if (!plan.empty()) f = plan.front(), plan.pop_front();
    for (uint8_t i = 0; i < 5; ++i) dish->sample(sampleOf(f, i), ms + 20 * i);
    ms += 100;
    dish->tick(ms, link);
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

  EXPECT_FALSE(a.feltDueling.count(stim::knock.v)) << "a lock-in knock is the duel's, not the pet's";
  EXPECT_FALSE(a.feltDueling.count(stim::button.v));
  EXPECT_FALSE(a.feltDueling.count(stim::button_hold.v));
  EXPECT_TRUE(a.feltDueling.count(stim::shake.v)) << "a shake always reaches him";

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

    // The slayer's line: the feat reaches his Death entry, and opens the war paint.
    victor->creature().fall();
    for (int t = 0; t < 3; ++t) bout.step();
    EXPECT_TRUE(victor->dish->lineage().legacyFeats() & feat::slayer);
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
