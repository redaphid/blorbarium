#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/duel_session.h"
#include "blorb/species.h"
#include "duel_access.h"
#include "fake_air.h"

using namespace blorb;
using blorbtest::Ether;
using blorbtest::FakeAir;
using blorbtest::Weather;
using T = DuelTimings;
using Special = TurnReport::Special;

namespace {

PeerAddr addr(uint8_t n) { return PeerAddr{{2, 0, 0, 0, 0, n}}; }
constexpr Weather kStorm{300, 100, 200, 8};   // BATTLES.md testing item 4: 30% lost, 10% doubled, 20% up to 0.8 s late

enum class Mercy : uint8_t { Spare, Finish, Wait };

// How one scripted kid plays. Everything it does it reads off the view, as a
// kid reads the screen.
struct Policy {
  Stakes tier = Stakes::Scrap;
  uint16_t confirmAfter = 4;         // ticks into Staking before confirming
  uint16_t pickPermille = 700;       // turns the kid picks; the rest run out and the pet picks
  uint16_t queuePermille = 300;      // of picked turns with the meter full: a special (half of them heavy)
  Mercy mercy = Mercy::Spare;
  bool hug = false, rematch = false;
  std::optional<uint32_t> clinkAt;   // lobby: knock at this tick and aim at nobody
  bool lidWhenBehind = false;        // a Scrap or Mortal: put the lid down while losing
};

Policy randomPolicy(Rng& r) {
  Policy p;
  p.tier = Stakes(r.below(3));
  p.confirmAfter = uint16_t(2 + r.below(30));
  p.pickPermille = uint16_t(r.below(1001));
  p.queuePermille = uint16_t(r.below(600));
  p.mercy = Mercy(r.below(3));
  p.hug = r.below(4) == 0;
  return p;
}

struct Device {
  Genome genome;
  FakeAir air;
  DuelSession s;
  DuelSetup setup;
  Policy policy;
  Rng rng;
  uint8_t bouts = 1, maxBouts = 1;

  std::vector<Meeting> meetings;
  std::vector<DuelView> lastViews;   // the view on the tick each Meeting arrived
  uint32_t woundTurns = 0, phases = 0, specials = 0, closedAt = 0;
  bool downed = false, fell = false, sawEggFizzle = false;
  std::optional<bool> mercyLocked;
  std::optional<FriendEgg> egg;
  EndReason ended = EndReason::None;
  // This turn's plan: when to act, at what, how.
  int16_t planAt = -1;
  int8_t planAim = 0;
  uint8_t planHow = 0;   // 0 knock, 1 quick, 2 heavy

  Device(Ether& e, uint8_t n, SpeciesId species, uint32_t seed)
      : genome(starterGenomeOf(species, seed)), air(e, addr(n)), rng(Rng::seeded(seed * 31 + n)) {
    setup.card.species = species;
    setup.card.power = speciesInfo(species).power;
    setup.card.knack.order = uint8_t(n % 6);
    setup.card.lineageId = 0x51ed000000ull + seed * 8 + n;
    setup.genome = &genome;
    setup.ceiling.top = Stakes::Mortal;
    setup.skull[1] = 60, setup.skull[2] = 140;
    setup.manualTurns = 100;
    setup.nonce = seed * 2654435761u + n;
  }
  bool open(uint32_t tick) { return s.open(DuelTestAccess::consent(), air, setup, tick); }

  DuelInput kid(const DuelView& v, uint32_t tick) {
    DuelInput in;
    switch (v.phase) {
      case DuelPhase::Lobby:
        if (policy.clinkAt) {
          in.knock = tick == *policy.clinkAt;
        } else if (v.peerCount > 0) {
          in.aim = 0;
          in.knock = tick % 5 == 0;
        }
        break;
      case DuelPhase::Staking: {
        const Stakes tier = Stakes(std::min(uint8_t(policy.tier), uint8_t(v.ceiling[0])));
        in.aim = int8_t(tier);
        if (v.phaseTicks >= policy.confirmAfter) {
          if (tier == Stakes::Mortal) in.bootDown = true;
          else if (tier == Stakes::Scrap) in.press = v.phaseTicks % 4 == 0;
          else in.knock = v.phaseTicks % 4 == 0;
        }
        break;
      }
      case DuelPhase::Choosing:
        if (policy.lidWhenBehind && v.mine.hearts < v.theirs.hearts) {
          in.lid = true;
          break;
        }
        if (v.phaseTicks == 0 || planAt < 0) {
          planAt = rng.below(1000) < policy.pickPermille ? int16_t(1 + rng.below(40)) : int16_t(9999);
          planAim = int8_t(rng.below(3));
          planHow = rng.below(1000) < policy.queuePermille ? uint8_t(1 + rng.below(2)) : 0;
        }
        if (v.phaseTicks == planAt) {
          in.aim = planAim;
          if (planHow == 0 || !v.specialReady) in.knock = true;
          else if (planHow == 1) in.press = true;
          else in.hold = in.bootDown = true;
          planAt = -1;
        }
        if (rng.below(200) == 0) in.doubleKnock = true;
        if (rng.below(100) == 0) in.shake = true;
        break;
      case DuelPhase::Mercy:
        if (v.iAmDown) {
          in.press = v.phaseTicks == 5;
        } else if (policy.mercy == Mercy::Finish) {
          in.bootDown = true;
        } else if (policy.mercy == Mercy::Spare) {
          in.knock = v.phaseTicks == 10;
        }
        break;
      case DuelPhase::Friends:
        if (policy.hug) in.bootDown = true;
        if (policy.rematch && bouts < maxBouts) in.press = v.phaseTicks == 3;
        break;
      default:
        break;
    }
    return in;
  }

  void step(uint32_t tick) {
    const DuelView v = s.view(tick);
    phases |= 1u << uint8_t(v.phase);
    if (v.hasLast)
      for (const Special sp : v.last.special) specials |= 1u << uint8_t(sp);
    // Only the winner's device writes the verdict, in the slot right after
    // the downing: while the skull waits the winner's log stops there, and
    // the downed side's log (which a special may have run ahead) never grows.
    if (v.phase == DuelPhase::Mercy && v.verdict == Verdict::None) {
      if (!v.iAmDown) EXPECT_FALSE(v.locked);
      if (v.iAmDown && !mercyLocked) mercyLocked = v.locked;
      if (v.iAmDown) EXPECT_EQ(v.locked, *mercyLocked);
    }
    if (v.eggFizzled) sawEggFizzle = true;
    DuelEffects fx = s.tick(air, kid(v, tick), tick);
    woundTurns += fx.woundCount;
    downed |= fx.downed;
    fell |= fx.fall;
    if (fx.met) meetings.push_back(*fx.met), lastViews.push_back(v);
    if (fx.egg) egg = std::move(fx.egg);
    if (fx.radioClosed && !closedAt) closedAt = tick, ended = s.view(tick).ended;
    if (fx.rematch && policy.rematch && bouts < maxBouts) {
      setup.pin = *fx.rematch;
      setup.nonce = setup.nonce * 747796405u + 1;
      ++bouts;
      closedAt = 0;
      EXPECT_TRUE(open(tick));
    }
  }
};

template <class Stop>
uint32_t run(Ether& ether, std::initializer_list<Device*> devices, uint32_t from, uint32_t limit, Stop stop) {
  uint32_t t = from;
  for (; t < limit; ++t) {
    ether.advance(t);
    for (Device* d : devices) d->step(t);
    if (stop(t)) break;
  }
  return t;
}
uint32_t runOut(Ether& ether, Device& a, Device& b, uint32_t from = 1, uint32_t limit = 30000) {
  return run(ether, {&a, &b}, from, limit, [&](uint32_t) { return !a.s.active() && !b.s.active(); });
}

void expectSameBattle(const Meeting& a, const Meeting& b) {
  EXPECT_EQ(a.battle, b.battle);
  EXPECT_EQ(a.outcomeHash, b.outcomeHash);
  EXPECT_EQ(a.ending, b.ending);
  EXPECT_EQ(a.turns, b.turns);
  EXPECT_EQ(a.verdict, b.verdict);
  EXPECT_NE(a.me, b.me);
  EXPECT_EQ(a.friendCard.lineageId == b.friendCard.lineageId, false);
}

}  // namespace

// BATTLES.md testing item 4: over a thousand seeds of bad weather, both
// devices finish with the same battle, byte for byte, whatever each kid did.
TEST(DuelSession, AThousandSeedsOfBadWeatherAgreeOnEveryByte) {
  uint32_t phases = 0, specials = 0, spared = 0, finished = 0, timedOut = 0, mortal = 0, eggs = 0;
  for (uint32_t seed = 1; seed <= 1000; ++seed) {
    Ether ether(seed, kStorm);
    Rng r = Rng::seeded(seed);
    Device a(ether, 1, SpeciesId{uint8_t(r.below(kSpeciesCount))}, seed);
    Device b(ether, 2, SpeciesId{uint8_t(r.below(kSpeciesCount))}, seed + 5000);
    a.policy = randomPolicy(r), b.policy = randomPolicy(r);
    if (seed % 3 == 0) a.policy.tier = b.policy.tier = Stakes::Mortal;
    ASSERT_TRUE(a.open(0) && b.open(0));
    runOut(ether, a, b);
    ASSERT_EQ(a.meetings.size(), 1u) << "seed " << seed;
    ASSERT_EQ(b.meetings.size(), 1u) << "seed " << seed;
    const Meeting &ma = a.meetings[0], &mb = b.meetings[0];
    expectSameBattle(ma, mb);
    EXPECT_FALSE(ma.fizzled || mb.fizzled || ma.fled || mb.fled) << "seed " << seed;
    EXPECT_EQ(ma.battle.stakes, agreedStakes(a.policy.tier, b.policy.tier)) << "seed " << seed;
    EXPECT_EQ(ma.friendCard.lineageId, b.setup.card.lineageId);
    if (ma.battle.stakes == Stakes::Spar) EXPECT_EQ(a.woundTurns + b.woundTurns, 0u);
    if (ma.verdict == Verdict::Finish) {
      ++finished;
      EXPECT_NE(a.fell, b.fell) << "seed " << seed;
      EXPECT_EQ(ma.iSlew, !a.fell) << "seed " << seed;   // the ack arrived: the victor took the slaying
      EXPECT_EQ(mb.iSlew, !b.fell) << "seed " << seed;
    }
    if (ma.verdict == Verdict::Spare) {
      ++spared;
      const Mercy w = a.downed ? b.policy.mercy : a.policy.mercy;
      timedOut += w == Mercy::Wait;
      if (w == Mercy::Finish) ADD_FAILURE() << "a held Finish spared, seed " << seed;
    }
    mortal += ma.battle.stakes == Stakes::Mortal;
    eggs += a.egg.has_value() && b.egg.has_value();
    if (a.egg) EXPECT_EQ(a.egg->genome.hash(), b.genome.hash());
    phases |= a.phases | b.phases;
    specials |= a.specials | b.specials;
  }
  for (DuelPhase p : {DuelPhase::Lobby, DuelPhase::Greeting, DuelPhase::Staking, DuelPhase::Choosing, DuelPhase::Waiting,
                      DuelPhase::Revealing, DuelPhase::Fainting, DuelPhase::Mercy, DuelPhase::Fallen, DuelPhase::Friends})
    EXPECT_TRUE(phases >> uint8_t(p) & 1) << "phase " << int(p) << " never shown";
  for (Special s : {Special::Committed, Special::Landed, Special::Riposted, Special::Whiffed, Special::Staggered})
    EXPECT_TRUE(specials >> uint8_t(s) & 1) << "special fate " << int(s) << " never seen";
  EXPECT_GT(mortal, 100u);
  EXPECT_GT(spared, 0u);
  EXPECT_GT(finished, 0u);
  EXPECT_GT(timedOut, 0u);   // the winner's own clock spared
  EXPECT_GT(eggs, 0u);
}

TEST(DuelSession, ARefusedOpenLeavesTheSessionClosed) {
  Ether ether(1, {});
  Device a(ether, 1, kGrungo, 1);
  a.air.refuseOpen = true;   // an OTA session, or an image on trial
  EXPECT_FALSE(a.open(0));
  EXPECT_FALSE(a.s.active());
  EXPECT_FALSE(a.air.isOpen());
  EXPECT_EQ(a.s.view(0).phase, DuelPhase::Closed);
}

// A device whose owner never asked hears nothing and says nothing, while two
// others duel beside it.
TEST(DuelSession, ClosedDeviceIsDeaf) {
  Ether ether(3, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2), c(ether, 3, SpeciesId{6}, 3);
  ASSERT_TRUE(a.open(0) && b.open(0));
  run(ether, {&a, &b, &c}, 1, 30000, [&](uint32_t) { return !a.s.active() && !b.s.active(); });
  EXPECT_EQ(a.meetings.size(), 1u);
  EXPECT_EQ(c.air.framesSent, 0u);
  EXPECT_FALSE(c.air.receive().has_value());
  EXPECT_EQ(c.phases, 1u << uint8_t(DuelPhase::Closed));
  EXPECT_TRUE(c.meetings.empty());
}

TEST(DuelSession, SilenceFizzlesBothWithinTwentySeconds) {
  Ether ether(4, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  a.policy.tier = b.policy.tier = Stakes::Spar;
  a.policy.pickPermille = b.policy.pickPermille = 0;
  ASSERT_TRUE(a.open(0) && b.open(0));
  const uint32_t cut = run(ether, {&a, &b}, 1, 5000, [&](uint32_t t) { return a.s.view(t).hasLast; });
  ether.weather.drop = 1000;
  runOut(ether, a, b, cut + 1);
  for (Device* d : {&a, &b}) {
    EXPECT_LE(d->closedAt, cut + T::kSilentTicks + 2);
    EXPECT_EQ(d->ended, EndReason::WanderedOff);
    ASSERT_EQ(d->meetings.size(), 1u);
    EXPECT_TRUE(d->meetings[0].fizzled);
    EXPECT_EQ(d->meetings[0].ending, Ending::Draw);
  }
}

TEST(DuelSession, ALidOnOneSideEndsBoth) {
  Ether ether(5, kStorm);
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  a.policy.tier = b.policy.tier = Stakes::Spar;
  ASSERT_TRUE(a.open(0) && b.open(0));
  const uint32_t lid = run(ether, {&a, &b}, 1, 5000, [&](uint32_t t) { return a.s.view(t).hasLast; });
  DuelInput down;
  down.lid = true;
  a.s.tick(a.air, down, lid + 1);
  EXPECT_FALSE(a.s.active());
  runOut(ether, a, b, lid + 2);
  EXPECT_LE(b.closedAt, lid + 10);   // the goodbye burst, not the silence timer
  EXPECT_EQ(b.ended, EndReason::Left);
  ASSERT_EQ(b.meetings.size(), 1u);
  EXPECT_TRUE(b.meetings[0].fizzled);   // a Spar: nobody flees one
}

// Fleeing a Scrap while behind is a forfeit that keeps every wound.
TEST(DuelSession, FleeingAScrapWhileBehindForfeitsAndKeepsTheWounds) {
  bool seen = false;
  for (uint32_t seed = 1; seed <= 40 && !seen; ++seed) {
    Ether ether(seed, kStorm);
    Device a(ether, 1, kGrungo, seed), b(ether, 2, SpeciesId{4}, seed + 99);
    a.policy.tier = b.policy.tier = Stakes::Scrap;
    a.policy.lidWhenBehind = true;
    ASSERT_TRUE(a.open(0) && b.open(0));
    runOut(ether, a, b);
    if (a.ended != EndReason::Fled) continue;
    seen = true;
    ASSERT_EQ(a.meetings.size(), 1u);
    ASSERT_EQ(b.meetings.size(), 1u);
    EXPECT_TRUE(a.meetings[0].fled);
    EXPECT_FALSE(a.meetings[0].fizzled);
    EXPECT_EQ(a.meetings[0].ending, b.meetings[0].ending);
    EXPECT_EQ(b.meetings[0].ending, b.meetings[0].me == Side::A ? Ending::AWon : Ending::BWon);
    EXPECT_FALSE(b.meetings[0].fled || b.meetings[0].fizzled);
    EXPECT_GT(a.woundTurns, 0u);   // applied turn by turn, before the lid; nothing undoes them
  }
  EXPECT_TRUE(seen);
}

TEST(DuelSession, AFriendFarAwayIsNeverMatched) {
  Ether ether(6, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  b.air.heardAt = T::kNearRssi - 8;   // a heard B across the room
  ASSERT_TRUE(a.open(0) && b.open(0));
  runOut(ether, a, b);
  EXPECT_EQ(a.phases & (1u << uint8_t(DuelPhase::Staking)), 0u);
  EXPECT_EQ(b.phases & (1u << uint8_t(DuelPhase::Staking)), 0u);
  EXPECT_TRUE(a.meetings.empty() && b.meetings.empty());
  EXPECT_EQ(a.ended, EndReason::LobbyTimeout);
}

TEST(DuelSession, ADecliningFriendShowsWhyAndIsNeverMatched) {
  Ether ether(7, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  b.setup.decline = Decline::Sleepy;
  ASSERT_TRUE(a.open(0) && b.open(0));
  run(ether, {&a, &b}, 1, 100, [](uint32_t) { return false; });
  const DuelView v = a.s.view(100);
  ASSERT_EQ(v.peerCount, 1);
  EXPECT_EQ(v.peers[0].decline, Decline::Sleepy);   // his zzz on my rim
  runOut(ether, a, b, 100);
  EXPECT_EQ(a.phases & (1u << uint8_t(DuelPhase::Greeting)), 0u);
  EXPECT_TRUE(a.meetings.empty() && b.meetings.empty());
}

// Two kids knock their devices together; a third knocks a second later.
// Only the coincident pair matches.
TEST(DuelSession, OnlyTheCoincidentPairOfThreeMatchesOnAClink) {
  Ether ether(8, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2), c(ether, 3, SpeciesId{6}, 3);
  a.policy.clinkAt = b.policy.clinkAt = 20u;
  c.policy.clinkAt = 30u;
  ASSERT_TRUE(a.open(0) && b.open(0) && c.open(0));
  run(ether, {&a, &b, &c}, 1, 80, [](uint32_t) { return false; });
  EXPECT_TRUE(a.phases >> uint8_t(DuelPhase::Staking) & 1);
  EXPECT_TRUE(b.phases >> uint8_t(DuelPhase::Staking) & 1);
  EXPECT_EQ(c.phases & (1u << uint8_t(DuelPhase::Greeting)), 0u);
  EXPECT_EQ(a.s.view(80).them.lineageId, b.setup.card.lineageId);
}

TEST(DuelSession, ThreeDevicesKnockingAtOnceMatchNobody) {
  Ether ether(8, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2), c(ether, 3, SpeciesId{6}, 3);
  a.policy.clinkAt = b.policy.clinkAt = c.policy.clinkAt = 20u;
  ASSERT_TRUE(a.open(0) && b.open(0) && c.open(0));
  run(ether, {&a, &b, &c}, 1, 80, [](uint32_t) { return false; });
  for (Device* d : {&a, &b, &c}) EXPECT_EQ(d->phases & (1u << uint8_t(DuelPhase::Greeting)), 0u);
}

TEST(PairCeiling,MortalNeedsAFairFightPickedTurnsAndAnEvenRecord) {
  Ceiling all;
  all.top = Stakes::Mortal;
  BattleCard me, them;
  EXPECT_EQ(pairCeiling(all, me, them, 30, {}, 0).top, Stakes::Mortal);

  me.prowess = 20, them.prowess = 250;   // two assist hearts
  Ceiling c = pairCeiling(all, me, them, 100, {}, 0);
  EXPECT_EQ(c.top, Stakes::Scrap);
  EXPECT_EQ(c.why[2], Decline::Unfair);
  EXPECT_EQ(pairCeiling(all, them, me, 100, {}, 0).top, Stakes::Scrap);   // and from the stronger side too

  me.prowess = them.prowess = 128;
  EXPECT_EQ(pairCeiling(all, me, them, 29, {}, 0).top, Stakes::Scrap);    // under 30 picked turns
  EXPECT_EQ(pairCeiling(all, me, them, 100, {4, 0, 4}, 0).top, Stakes::Spar);   // a lopsided record
  EXPECT_EQ(pairCeiling(all, me, them, 100, {4, 1, 3}, 0).top, Stakes::Mortal); // one in four is enough
  c = pairCeiling(all, me, them, 100, {}, 1);                             // today's for-keeps duel was fought
  EXPECT_EQ(c.top, Stakes::Spar);
  EXPECT_EQ(c.why[1], Decline::Unfair);
  EXPECT_EQ(c.why[2], Decline::Unfair);

  Ceiling young;
  young.top = Stakes::Spar;
  young.why[1] = young.why[2] = Decline::TooYoung;
  EXPECT_EQ(pairCeiling(young, me, them, 100, {}, 0).why[2], Decline::TooYoung);   // the pet's own reason stands
}

// Staking runs at the lower tier, and both screens show both pets' skulls.
TEST(DuelSession, StakingAgreesOnTheLowerTierAndShowsBothSkulls) {
  Ether ether(9, kStorm);
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  a.policy.tier = Stakes::Mortal, b.policy.tier = Stakes::Scrap;
  b.setup.skull[2] = 222;
  ASSERT_TRUE(a.open(0) && b.open(0));
  run(ether, {&a, &b}, 1, 3000, [&](uint32_t t) { return a.s.view(t).phase == DuelPhase::Choosing; });
  const DuelView v = a.s.view(3000);
  EXPECT_EQ(v.agreed, Stakes::Scrap);
  EXPECT_EQ(v.skull[0][2], 140);
  EXPECT_EQ(v.skull[1][2], 222);
  EXPECT_EQ(v.theyChose, Stakes::Scrap);
}

TEST(DuelSession, MortalIsRefusedAtTwoAssistHearts) {
  Ether ether(10, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  a.setup.card.prowess = 20, b.setup.card.prowess = 250;
  a.policy.tier = b.policy.tier = Stakes::Mortal;
  ASSERT_TRUE(a.open(0) && b.open(0));
  run(ether, {&a, &b}, 1, 3000, [&](uint32_t t) { return a.s.view(t).phase == DuelPhase::Choosing; });
  for (Device* d : {&a, &b}) {
    const DuelView v = d->s.view(3000);
    EXPECT_EQ(v.ceiling[0], Stakes::Scrap);
    EXPECT_EQ(v.why[0][2], Decline::Unfair);
    EXPECT_EQ(v.agreed, Stakes::Scrap);
  }
}

// B confirms first and every State it sends while waiting is lost. A still
// learns B's confirm, from the first State B sends under the agreed id.
TEST(DuelSession, ALostConfirmDoesNotStrandStaking) {
  Ether ether(11, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  a.policy.tier = b.policy.tier = Stakes::Scrap;
  a.policy.confirmAfter = 40;
  ASSERT_TRUE(a.open(0) && b.open(0));
  bool muted = false;
  run(ether, {&a, &b}, 1, 2000, [&](uint32_t t) {
    const DuelView vb = b.s.view(t);
    b.air.muted = vb.phase == DuelPhase::Staking && vb.confirmed ? 1000 : 0;
    muted = muted || b.air.muted;
    return a.s.view(t).phase == DuelPhase::Choosing && vb.phase == DuelPhase::Choosing;
  });
  EXPECT_TRUE(muted);
  EXPECT_EQ(a.s.view(2000).agreed, Stakes::Scrap);
  EXPECT_EQ(b.s.view(2000).agreed, Stakes::Scrap);
}

// Both kids ask for a rematch: the session closes, reopens pinned to the same
// friend, and runs Staking again in full, under a new battle id.
TEST(DuelSession, ARematchRerunsStakingAndALatePacketFromTheLastBoutIsIgnored) {
  Ether ether(12, kStorm);
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  a.policy.rematch = b.policy.rematch = true;
  a.maxBouts = b.maxBouts = 2;
  ASSERT_TRUE(a.open(0) && b.open(0));
  StatePacket late;
  bool injected = false;
  run(ether, {&a, &b}, 1, 60000, [&](uint32_t t) {
    if (a.bouts == 2 && b.s.view(t).phase == DuelPhase::Choosing && !injected) {
      // A State from bout 1 that disagrees with everything in bout 2.
      late.roster = rosterHash();
      late.battle = a.meetings[0].battle;
      late.card = a.setup.card;
      late.moveCount = 6;
      for (uint8_t i = 0; i < 6; ++i) late.moves[i] = Move{Stance(i % 3), false, true}.pack();
      Datagram d;
      d.from = addr(1), d.rssi = -40;
      d.len = uint8_t(encode(Packet{late}, d.bytes));
      b.air.inject(d);
      injected = true;
    }
    return a.bouts == 2 && !a.s.active() && !b.s.active();
  });
  EXPECT_TRUE(injected);
  ASSERT_EQ(a.meetings.size(), 2u);
  ASSERT_EQ(b.meetings.size(), 2u);
  expectSameBattle(a.meetings[1], b.meetings[1]);
  EXPECT_FALSE(b.meetings[1].fizzled);
  EXPECT_EQ(a.meetings[1].battle.rematch, 1);
  EXPECT_NE(a.meetings[1].battle.seed, a.meetings[0].battle.seed);
}

// The verdict is the winner's alone, and its own 15 s spare when nobody acts.
// A Finish is the victor's to take only once the fallen side has heard it.
TEST(DuelSession, TheWinnersSilenceSparesAndAFinishNeedsTheAck) {
  uint32_t waited = 0, acked = 0;
  for (uint32_t seed = 1; seed <= 60; ++seed) {
    for (Mercy mercy : {Mercy::Wait, Mercy::Finish}) {
      Ether ether(seed, kStorm);
      Device a(ether, 1, kGrungo, seed), b(ether, 2, SpeciesId{4}, seed + 3);
      a.policy.tier = b.policy.tier = Stakes::Mortal;
      a.policy.mercy = b.policy.mercy = mercy;
      ASSERT_TRUE(a.open(0) && b.open(0));
      runOut(ether, a, b);
      if (!a.downed && !b.downed) continue;
      ASSERT_EQ(a.meetings.size(), 1u);
      ASSERT_EQ(b.meetings.size(), 1u);
      expectSameBattle(a.meetings[0], b.meetings[0]);
      Device& winner = a.downed ? b : a;
      Device& loser = a.downed ? a : b;
      if (mercy == Mercy::Wait) {
        ++waited;
        EXPECT_EQ(winner.meetings[0].verdict, Verdict::Spare);
        EXPECT_TRUE(winner.meetings[0].iSpared && loser.meetings[0].wasSpared);
        EXPECT_FALSE(loser.fell);
      } else {
        ++acked;
        EXPECT_TRUE(loser.fell);
        EXPECT_TRUE(loser.meetings[0].iFell);
        EXPECT_TRUE(winner.meetings[0].iSlew);
      }
    }
  }
  EXPECT_GT(waited, 5u);
  EXPECT_GT(acked, 5u);
}

TEST(DuelSession, AFinishTheFallenSideNeverHeardSlaysNobody) {
  bool seen = false;
  for (uint32_t seed = 1; seed <= 60 && !seen; ++seed) {
    Ether ether(seed, {});
    Device a(ether, 1, kGrungo, seed), b(ether, 2, SpeciesId{4}, seed + 3);
    a.policy.tier = b.policy.tier = Stakes::Mortal;
    a.policy.mercy = b.policy.mercy = Mercy::Finish;
    ASSERT_TRUE(a.open(0) && b.open(0));
    run(ether, {&a, &b}, 1, 30000, [&](uint32_t t) {
      // From the moment the skull waits, nothing the victor sends arrives.
      for (Device* d : {&a, &b})
        if (d->s.view(t).phase == DuelPhase::Mercy && !d->s.view(t).iAmDown) d->air.muted = 1000;
      return !a.s.active() && !b.s.active();
    });
    if (!a.downed && !b.downed) continue;
    seen = true;
    Device& winner = a.downed ? b : a;
    Device& loser = a.downed ? a : b;
    ASSERT_EQ(winner.meetings.size(), 1u);
    ASSERT_EQ(loser.meetings.size(), 1u);
    EXPECT_FALSE(winner.meetings[0].iSlew);   // "they left before the verdict": no kill
    EXPECT_FALSE(loser.fell);                 // he never heard it, so he lives
    EXPECT_FALSE(loser.meetings[0].fled);     // the victor went quiet: nobody fled
    EXPECT_EQ(loser.meetings[0].verdict, Verdict::None);
  }
  EXPECT_TRUE(seen);
}

TEST(DuelSession, TheGenomeSwapCompletesAtThirtyPercentLoss) {
  Ether ether(13, kStorm);
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  a.policy.hug = b.policy.hug = true;
  a.policy.tier = b.policy.tier = Stakes::Spar;
  ASSERT_TRUE(a.open(0) && b.open(0));
  runOut(ether, a, b);
  ASSERT_TRUE(a.egg && b.egg);
  EXPECT_EQ(a.egg->genome.hash(), b.genome.hash());
  EXPECT_EQ(b.egg->genome.hash(), a.genome.hash());
  EXPECT_EQ(a.egg->record.friendLineage, b.setup.card.lineageId);
  EXPECT_EQ(a.egg->record.friendSpecies.v, b.setup.card.species.v);
  EXPECT_EQ(a.egg->record.battleSeed, a.meetings[0].battle.seed);
  EXPECT_EQ(a.egg->record.battleSeed, b.egg->record.battleSeed);
}

TEST(DuelSession, TheGenomeSwapFizzlesCleanlyWhenCutOff) {
  Ether ether(14, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  a.policy.hug = b.policy.hug = true;
  a.policy.tier = b.policy.tier = Stakes::Spar;
  ASSERT_TRUE(a.open(0) && b.open(0));
  const uint32_t cut = run(ether, {&a, &b}, 1, 30000, [&](uint32_t t) {
    const DuelView va = a.s.view(t), vb = b.s.view(t);
    return va.iHugged && va.theyHugged && vb.iHugged && vb.theyHugged;
  });
  ether.weather.drop = 1000;
  runOut(ether, a, b, cut + 1);
  EXPECT_FALSE(a.egg || b.egg);
  EXPECT_TRUE(a.sawEggFizzle && b.sawEggFizzle);
  EXPECT_LE(a.closedAt, cut + T::kGenomeTicks + T::kRevealTicks + 2);
  EXPECT_LE(b.closedAt, cut + T::kGenomeTicks + T::kRevealTicks + 2);
}

TEST(DuelInput, APhonesStimuliAreNeverGestures) {
  SenseOut raw;
  raw.fire(stim::knock);
  raw.fire(stim::button);
  const Fx level = Fx::ratio(1, 2);
  EXPECT_TRUE(DuelInput::from(raw, 0, level, level, false).knock);
  const DuelInput remote = DuelInput::from(raw, duelStimBit(stim::knock) | duelStimBit(stim::button), level, level, false);
  EXPECT_FALSE(remote.knock || remote.press);
  EXPECT_EQ(DuelInput::from(raw, 0, Fx::ratio(9, 10), level, false).aim, 0);   // tilted left: Pounce
  EXPECT_EQ(DuelInput::from(raw, 0, Fx::ratio(1, 10), level, false).aim, 1);   // right: Guard
  EXPECT_EQ(DuelInput::from(raw, 0, level, Fx::ratio(1, 10), false).aim, 2);   // toward you: Trick
  EXPECT_EQ(DuelInput::from(raw, 0, level, level, false).aim, -1);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
