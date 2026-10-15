// Two whole Dishes on one ether (docs/design/BATTLES.md, testing item 5 and
// unit D9's cases): each with its own MemStorage and FakeAir, driven by 50 Hz
// BodySamples from a scripted kid who reads only the duel view, as a kid
// reads the screen.
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/friend_egg.h"
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
        else if (t == 1 || pressForMortal) hold(t, 2, Frame{-1, true}), hold(t, 1, Frame{});
        else hold(t, 33, Frame{-1, true}), hold(t, 1, Frame{});
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
          if (!v.rematchAsked) hold(-1, 2, Frame{-1, true}), hold(-1, 6, Frame{});
          return;
        }
        if (v.phaseTicks < leaveAfter) return hold(-1, 1, Frame{});
        if (hug && !v.iHugged) return hold(-1, 34, Frame{-1, true}), hold(-1, 1, Frame{});
        if (hug && !(v.eggLaid || v.eggFizzled)) return hold(-1, 1, Frame{});
        if (leave) hold(-1, 1, Frame{-1, true}), hold(-1, 1, Frame{}), hold(-1, 1, Frame{-1, true}), hold(-1, 3, Frame{});
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
    shownPhase = v.phase;
    if (plan.empty()) kid.plan(v, dueling, plan);
    Frame f;
    if (!plan.empty()) f = plan.front(), plan.pop_front();
    for (uint8_t i = 0; i < 5; ++i) dish->sample(sampleOf(f, i), ms + 20 * i);
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
  for (uint8_t k = 0; k < a.itemCount; ++k) n += a.items[k].what != Appearance::Item::What::Marble;
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

TEST(BootRows, AsleepADoublePressIsTwoPellets) {
  Alone a;
  a.d.creature().inject(driveChem(drive::sleepiness), Fx::ratio(9, 10));
  a.d.creature().force(action::sleep);
  for (int t = 0; t < 3; ++t) a.tick();
  ASSERT_TRUE(a.d.creature().body().asleep);
  expectRow(*a.d.dish, BootContext::Asleep, "asleep");
  const uint8_t before = pellets(*a.d.dish);
  a.doublePress();
  EXPECT_FALSE(a.d.dish->duelActive()) << "a sleeping pet never duels";
  EXPECT_EQ(a.d.air.opens, 0u);
  EXPECT_EQ(pellets(*a.d.dish), before + 2) << "where a double means nothing, it is two presses";
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
    anotherBout = anotherBout || (wasFriends && v.phase != DuelPhase::Friends && v.phase != DuelPhase::Closed);
    wasFriends = wasFriends || v.phase == DuelPhase::Friends;
  };
  for (int t = 0; t < 4000; ++t) {
    watch();
    if (sawAsk && !bout.a.dish->duelActive()) break;
  }
  ASSERT_TRUE(sawAsk) << "the friend's ask reached my screen first";
  for (int t = 0; t < 100; ++t) watch();
  EXPECT_FALSE(anotherBout) << "from the friends screen the only way on is out";
  EXPECT_FALSE(bout.a.dish->duelActive()) << "I left";
  EXPECT_EQ(bout.a.dish->duels().state().friends[0].s.met, 1) << "and no second bout began";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
