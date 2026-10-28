#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
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

// HeldIn: BOOT held from the downing's reveal on and never let go.
// Regrip: the same, let go once in Mercy, then held again.
enum class Mercy : uint8_t { Spare, Finish, Wait, HeldIn, Regrip };

// How one scripted kid plays. Everything it does it reads off the view, as a
// kid reads the screen.
struct Policy {
  Stakes tier = Stakes::Scrap;
  uint16_t confirmAfter = 4;         // ticks into Staking before confirming
  uint16_t pickPermille = 700;       // turns the kid picks; the rest run out and the pet picks
  uint16_t queuePermille = 300;      // of picked turns with the meter full: a special (half of them heavy)
  Mercy mercy = Mercy::Spare;
  bool hug = false, rematch = false;
  bool lidWhenBehind = false;        // a Scrap or Mortal: put the lid down (and drop it) while losing
  bool aimOnly = false;              // Staking: tilts to the tier and never knocks or holds
};

// The tilt that points at a tier (tierAimed's inverse).
int8_t aimOfTier(Stakes t) {
  for (int8_t a = 0; a < 4; ++a)
    if (tierAimed(a) == int8_t(t)) return a;
  return -1;
}

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
  uint8_t boutsEnded = 0, stayFor = 0;   // fight mode: bouts with anyone after the first, then the kid leaves

  std::vector<Meeting> meetings;
  std::vector<DuelView> lastViews;   // the view on the tick each Meeting arrived
  uint32_t woundTurns = 0, phases = 0, specials = 0, closedAt = 0;
  bool fought = false;   // this bout reached Staking: its end is a bout's end
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
    if ((policy.mercy == Mercy::HeldIn || policy.mercy == Mercy::Regrip) && v.theyAreDown && !v.iAmDown &&
        (v.phase == DuelPhase::Revealing || v.phase == DuelPhase::Mercy)) {
      in.bootDown = !(policy.mercy == Mercy::Regrip && v.phase == DuelPhase::Mercy && v.phaseTicks == 5);
      return in;
    }
    switch (v.phase) {
      case DuelPhase::Lobby:
        // Fight mode pairs by itself; a kid tilting the badge about changes nothing.
        in.aim = int8_t(tick % 4);
        break;
      case DuelPhase::Staking: {
        // Picked as a turn is: tilt toward the tier and knock; Mortal is held
        // 3 s. Before the kid acts the badge lies level. aimOnly: he tilts to
        // the tier and never knocks or holds, so the ring runs out on it.
        const Stakes want = Stakes(std::min(uint8_t(policy.tier), uint8_t(v.ceiling[0])));
        if (!v.confirmed && v.phaseTicks >= policy.confirmAfter) {
          const uint16_t k = uint16_t(v.phaseTicks - policy.confirmAfter);
          in.aim = aimOfTier(want);
          if (policy.aimOnly) break;
          if (want == Stakes::Mortal) in.bootDown = k < 32;
          else in.knock = k == 2;
        }
        break;
      }
      case DuelPhase::Choosing:
        if (policy.lidWhenBehind && v.mine.hearts < v.theirs.hearts) {
          in.lid = in.dropped = true;
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
    if (uint8_t(s.view(tick).phase) >= uint8_t(DuelPhase::Staking)) fought = true;
    if (fx.radioClosed && !closedAt) closedAt = tick, ended = s.view(tick).ended;
    // Fight mode searches again after a bout. A rematch both asked for is
    // the next bout; otherwise this kid presses twice and leaves fight mode.
    if (fx.backToLobby && fought) {
      fought = false;
      closedAt = tick, ended = s.view(tick).ended;
      ++boutsEnded;
      if (fx.rematch && policy.rematch && bouts < maxBouts) {
        ++bouts;
        closedAt = 0;
      } else if (boutsEnded <= stayFor) {
        closedAt = 0;
      } else {
        s.close(air, ended);
      }
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
template <class Stop>
uint32_t runAll(Ether& ether, const std::vector<Device*>& devices, uint32_t from, uint32_t limit, Stop stop) {
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

// A lid, a drop or a face-down badge never ends a fight (a car, a pocket):
// the kid who would once have fled plays on to the rules' own end.
TEST(DuelSession, ALidOrADropNeverEndsAFight) {
  uint32_t tried = 0;
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Ether ether(seed, kStorm);
    Device a(ether, 1, kGrungo, seed), b(ether, 2, SpeciesId{4}, seed + 99);
    a.policy.tier = b.policy.tier = Stakes::Scrap;
    a.policy.lidWhenBehind = true;
    ASSERT_TRUE(a.open(0) && b.open(0));
    runOut(ether, a, b);
    ASSERT_EQ(a.meetings.size(), 1u) << "seed " << seed;
    ASSERT_EQ(b.meetings.size(), 1u) << "seed " << seed;
    expectSameBattle(a.meetings[0], b.meetings[0]);
    EXPECT_FALSE(a.meetings[0].fled || a.meetings[0].fizzled) << "seed " << seed;
    EXPECT_NE(a.ended, EndReason::LidDown);
    EXPECT_NE(a.ended, EndReason::Dropped);
    tried += a.meetings[0].ending != Ending::Draw;
  }
  EXPECT_GT(tried, 5u);
}

// No RSSI gate: a friend across the room, or further, pairs.
TEST(DuelSession, AFriendAcrossTheRoomStillPairs) {
  Ether ether(6, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  b.air.heardAt = -95;
  ASSERT_TRUE(a.open(0) && b.open(0));
  runOut(ether, a, b);
  ASSERT_EQ(a.meetings.size(), 1u);
  ASSERT_EQ(b.meetings.size(), 1u);
  expectSameBattle(a.meetings[0], b.meetings[0]);
}

// Fight mode is a mode: the old lobby's minute is long gone. A friend who
// enters it minutes later still pairs, with no tap and no tilt.
TEST(DuelSession, FightModeWaitsForAFriendWhoComesMinutesLater) {
  Ether ether(9, kStorm);
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  ASSERT_TRUE(a.open(0));
  run(ether, {&a, &b}, 1, 2400, [](uint32_t) { return false; });
  ASSERT_TRUE(a.s.searching());
  EXPECT_LT(a.s.view(2400).fightTicksLeft, 700);
  ASSERT_TRUE(b.open(2400));
  runOut(ether, a, b, 2400);
  ASSERT_EQ(a.meetings.size(), 1u);
  ASSERT_EQ(b.meetings.size(), 1u);
  expectSameBattle(a.meetings[0], b.meetings[0]);
}

// Out of fight mode by a double press, or after five minutes with no fight.
// A pet resting after a fight holds that clock; a tilt or a knock never leaves.
TEST(DuelSession, FightModeEndsOnADoublePressOrAfterFiveMinutes) {
  Ether ether(10, {});
  Device a(ether, 1, kGrungo, 1);
  ASSERT_TRUE(a.open(0));
  for (uint32_t t = 1; t < 200; ++t) {
    DuelInput in;
    in.aim = int8_t(t % 4), in.knock = t % 7 == 0, in.lid = t % 11 == 0, in.dropped = t % 13 == 0;
    a.s.tick(a.air, in, t);
  }
  EXPECT_TRUE(a.s.searching());
  DuelInput twice;
  twice.doublePress = true;
  const DuelEffects fx = a.s.tick(a.air, twice, 200);
  EXPECT_TRUE(fx.radioClosed);
  EXPECT_FALSE(a.s.active());
  EXPECT_EQ(a.s.view(200).ended, EndReason::Left);

  ASSERT_TRUE(a.open(1000));
  uint32_t t = 1001;
  for (; a.s.active() && t < 1000 + 4000; ++t) a.s.tick(a.air, DuelInput{}, t);
  EXPECT_EQ(t - 1, 1000u + T::kFightModeTicks);
  EXPECT_EQ(a.s.view(t).ended, EndReason::LobbyTimeout);

  ASSERT_TRUE(a.open(10000));
  a.s.setDecline(Decline::Resting);
  for (t = 10001; t < 11000; ++t) a.s.tick(a.air, DuelInput{}, t);
  a.s.setDecline(Decline::None);
  for (; a.s.active() && t < 20000; ++t) a.s.tick(a.air, DuelInput{}, t);
  EXPECT_GE(t - 1, 10999u + T::kFightModeTicks) << "the rest held the clock";
}

// The stakes are picked as a stance is: tilt to the tier and knock. The duel
// runs at the lower pick; Mortal needs the hold. A kid who tilts and lets the
// ring run out gets the tier he tilted to (an unheld Mortal as Scrap), and a
// kid who never tilts gets a Spar.
TEST(DuelSession, TheTiltPicksTheStakes) {
  for (const auto& [ta, tb, want] : {std::tuple{Stakes::Spar, Stakes::Mortal, Stakes::Spar},
                                     std::tuple{Stakes::Scrap, Stakes::Scrap, Stakes::Scrap},
                                     std::tuple{Stakes::Scrap, Stakes::Mortal, Stakes::Scrap},
                                     std::tuple{Stakes::Mortal, Stakes::Mortal, Stakes::Mortal}}) {
    Ether ether(11, {});
    Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
    a.policy.tier = ta, b.policy.tier = tb;
    ASSERT_TRUE(a.open(0) && b.open(0));
    runOut(ether, a, b);
    ASSERT_EQ(a.meetings.size(), 1u);
    EXPECT_EQ(a.meetings[0].battle.stakes, want);
  }
  for (const auto& [tier, aimOnly, want] : {std::tuple{Stakes::Scrap, true, Stakes::Scrap},
                                            std::tuple{Stakes::Mortal, true, Stakes::Scrap},
                                            std::tuple{Stakes::Mortal, false, Stakes::Spar}}) {
    Ether ether(12, {});
    Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
    a.policy.tier = b.policy.tier = tier;
    a.policy.aimOnly = b.policy.aimOnly = aimOnly;
    if (!aimOnly) a.policy.confirmAfter = b.policy.confirmAfter = 9999;   // never tilts at all
    ASSERT_TRUE(a.open(0) && b.open(0));
    uint16_t ring = 0;
    run(ether, {&a, &b}, 1, 2000, [&](uint32_t t) {
      const DuelView v = a.s.view(t);
      if (v.phase == DuelPhase::Staking && !v.confirmed) ring = std::max(ring, v.chooseTicksLeft);
      return v.phase == DuelPhase::Choosing;
    });
    EXPECT_EQ(a.s.view(2000).agreed, want) << "tier " << int(tier) << " aimOnly " << aimOnly;
    EXPECT_GT(ring, T::kStakeAutoTicks - 5) << "the rim ring runs while the kid picks";
  }
}

// A knock or a press under the skull agrees to nothing, and a tier the pair
// may not enter is never picked however the badge is tilted.
TEST(DuelSession, OnlyTheHoldAgreesToMortalAndABarredTierIsNeverPicked) {
  Ether ether(13, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  b.setup.ceiling.top = Stakes::Scrap;
  ASSERT_TRUE(a.open(0) && b.open(0));
  a.policy.confirmAfter = b.policy.confirmAfter = 9999;
  uint32_t t = run(ether, {&a, &b}, 1, 400, [&](uint32_t t) {
    return a.s.view(t).phase == DuelPhase::Staking && b.s.view(t).phase == DuelPhase::Staking;
  });
  ASSERT_EQ(a.s.view(t).phase, DuelPhase::Staking);
  ++t;
  // From here these inputs are the kids' hands.
  const auto tickBoth = [&](const DuelInput& ia, const DuelInput& ib) {
    ether.advance(t), a.s.tick(a.air, ia, t), b.s.tick(b.air, ib, t), ++t;
  };
  DuelInput skull;
  skull.aim = aimOfTier(Stakes::Mortal);
  tickBoth(skull, skull);
  EXPECT_EQ(a.s.view(t).chosen, Stakes::Mortal);
  EXPECT_EQ(b.s.view(t).chosen, Stakes::Spar) << "Mortal is above B's ceiling: not picked";
  DuelInput knock = skull, press = skull;
  knock.knock = true, press.press = true;
  tickBoth(knock, knock);
  tickBoth(press, press);
  EXPECT_FALSE(a.s.view(t).confirmed) << "no knock or press agrees to Mortal";
  EXPECT_FALSE(b.s.view(t).confirmed) << "a knock at a struck-through tier locks nothing";
  DuelInput scrap;
  scrap.aim = aimOfTier(Stakes::Scrap), scrap.knock = true;
  tickBoth(DuelInput{}, scrap);
  EXPECT_TRUE(b.s.view(t).confirmed);
  EXPECT_EQ(b.s.view(t).chosen, Stakes::Scrap);
  DuelInput held = skull;
  held.bootDown = true;
  for (uint16_t i = 0; i < kFinishHoldTicks; ++i) tickBoth(held, DuelInput{});
  EXPECT_TRUE(a.s.view(t).confirmed) << "the 3 s hold under the skull agrees";
  EXPECT_EQ(a.s.view(t).chosen, Stakes::Mortal);
}

// One direction of the link lost. Lost from before a pair locks: neither
// badge ever shows the fight. Lost mid-handshake (packets already in flight
// can still complete it on one side, as no handshake can rule out a cut after
// its last message): at most one badge shows it, for no longer than the
// friend's greeting takes to give up, and both are searching again after.
// Once the air clears they fight, and agree.
TEST(DuelSession, AOneWayLinkShowsAFightOnOneBadgeBrieflyAtMost) {
  uint32_t oneSided = 0;
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    for (const bool midway : {false, true}) {
      for (const bool aDeaf : {false, true}) {
        Ether ether(seed, kStorm);
        Device a(ether, 1, kGrungo, seed), b(ether, 2, SpeciesId{4}, seed + 7);
        Device& cut = aDeaf ? b : a;
        a.stayFor = b.stayFor = 1;   // a bout the cut ended on one side is not the kid's last
        if (!midway) cut.air.muted = 1000;
        ASSERT_TRUE(a.open(0) && b.open(0));
        uint32_t from = 1;
        if (midway) {
          from = run(ether, {&a, &b}, 1, 400, [&](uint32_t t) {
            return a.s.view(t).phase == DuelPhase::Greeting || b.s.view(t).phase == DuelPhase::Greeting;
          }) + 1;
          cut.air.muted = 1000;
        }
        uint32_t shown[2] = {0, 0};
        run(ether, {&a, &b}, from, from + 400, [&](uint32_t t) {
          for (int i = 0; i < 2; ++i)
            shown[i] += uint8_t((i ? b : a).s.view(t).phase) >= uint8_t(DuelPhase::Staking);
          return false;
        });
        const std::string what = "seed " + std::to_string(seed) + (midway ? " midway" : " before") + (aDeaf ? " b->a" : " a->b");
        if (!midway) EXPECT_EQ(shown[0] + shown[1], 0u) << what;
        EXPECT_TRUE(shown[0] == 0 || shown[1] == 0) << what;
        EXPECT_LE(shown[0] + shown[1], uint32_t(T::kGreetTicks) + 20) << what;
        oneSided += shown[0] + shown[1] > 0;
        EXPECT_TRUE(a.s.searching() && b.s.searching()) << what;
        cut.air.muted = 0;
        runOut(ether, a, b, from + 400);
        ASSERT_FALSE(a.meetings.empty()) << what;
        const Meeting& last = a.meetings.back();
        const Meeting* theirs = nullptr;
        for (const Meeting& m : b.meetings)
          if (m.battle == last.battle) theirs = &m;
        ASSERT_NE(theirs, nullptr) << what;
        expectSameBattle(last, *theirs);
        EXPECT_FALSE(last.fizzled) << what;
      }
    }
  }
  EXPECT_LT(oneSided, 20u);   // the three steps make even a cut mid-handshake rarely show anything
}

// A crowd in fight mode on a stormy link (drops, doubles, late packets): each
// badge is in one fight at a time, never two; a fight that shows on a badge
// shows on its friend too, within a few packets; both sides of every fight
// agree on every byte; and an odd badge out keeps searching and pairs once a
// friend is free. Each badge fights twice, then its kid leaves fight mode.
void crowd(uint8_t n, uint32_t seeds) {
  uint32_t fights = 0;
  for (uint32_t seed = 1; seed <= seeds; ++seed) {
    Ether ether(seed * 31 + n, kStorm);
    std::vector<std::unique_ptr<Device>> owned;
    std::vector<Device*> all;
    for (uint8_t i = 0; i < n; ++i) {
      owned.push_back(std::make_unique<Device>(ether, uint8_t(i + 1), SpeciesId{uint8_t(i % 9)}, seed * 16 + i));
      owned.back()->stayFor = 1;
      owned.back()->policy.tier = Stakes(i % 2);
      all.push_back(owned.back().get());
    }
    for (Device* d : all) ASSERT_TRUE(d->open(0));
    struct Start { uint32_t tick; uint64_t friendId; };
    std::vector<std::vector<Start>> starts(n);
    std::vector<bool> showing(n, false);
    const auto indexOf = [&](uint64_t lineage) {
      for (uint8_t i = 0; i < n; ++i)
        if (all[i]->setup.card.lineageId == lineage) return int(i);
      return -1;
    };
    runAll(ether, all, 1, 40000, [&](uint32_t t) {
      std::vector<uint8_t> claimedBy(n, 0);
      for (uint8_t i = 0; i < n; ++i) {
        const DuelView v = all[i]->s.view(t);
        // In a fight: the stakes to the fall. The friends screen after it is a
        // farewell each badge leaves on its own clock, so a friend may still
        // show it a moment after the other has paired again (seen once the
        // choose timer shrank each turn, #24).
        const bool shows = uint8_t(v.phase) >= uint8_t(DuelPhase::Staking) && v.phase != DuelPhase::Friends;
        if (shows) {
          const int f = indexOf(v.them.lineageId);
          EXPECT_GE(f, 0);
          EXPECT_NE(f, int(i));
          // The friends screen is after the fight: on a stormy link one side
          // may linger there a few packets after the other has left and paired
          // again, which is no second fight.
          if (f >= 0 && v.phase != DuelPhase::Friends) ++claimedBy[f];
          if (!showing[i]) starts[i].push_back({t, v.them.lineageId});
        }
        showing[i] = shows;
      }
      for (uint8_t i = 0; i < n; ++i)
        if (claimedBy[i] > 1) {
          std::string who;
          for (uint8_t j = 0; j < n; ++j) {
            const DuelView v = all[j]->s.view(t);
            who += " badge " + std::to_string(j) + " phase " + std::to_string(int(v.phase)) + " v " +
                   std::to_string(indexOf(v.them.lineageId)) + " ticks " + std::to_string(v.phaseTicks) + ";";
          }
          ADD_FAILURE() << "seed " << seed << ": badge " << int(i) << " in two fights, tick " << t << ":" << who;
        }
      for (Device* d : all)
        if (d->s.active()) return false;
      return true;
    });
    for (uint8_t i = 0; i < n; ++i) {
      EXPECT_FALSE(all[i]->s.active()) << "seed " << seed << " badge " << int(i);
      for (const Start& s : starts[i]) {
        const int f = indexOf(s.friendId);
        ASSERT_GE(f, 0);
        bool mirrored = false;
        for (const Start& o : starts[f])
          mirrored = mirrored || (o.friendId == all[i]->setup.card.lineageId && o.tick + 60 >= s.tick && s.tick + 60 >= o.tick);
        EXPECT_TRUE(mirrored) << "seed " << seed << ": a fight showed on badge " << i << " alone at " << s.tick;
      }
      for (const Meeting& m : all[i]->meetings) {
        const int f = indexOf(m.friendCard.lineageId);
        ASSERT_GE(f, 0);
        const Meeting* theirs = nullptr;
        for (const Meeting& o : all[f]->meetings)
          if (o.battle == m.battle) theirs = &o;
        ASSERT_NE(theirs, nullptr) << "seed " << seed << ": badge " << i << " met one its friend never did";
        expectSameBattle(m, *theirs);
        ++fights;
      }
      EXPECT_GE(all[i]->meetings.size(), n % 2 && n > 1 ? 0u : 1u) << "seed " << seed << " badge " << int(i);
    }
  }
  EXPECT_GT(fights, seeds * (n / 2) * 2);   // each side's record, at least one bout per pair
}

TEST(FightMode, ThreeBadgesPairOneAtATimeAndAgree) { crowd(3, 30); }
TEST(FightMode, FourBadgesPairOneAtATimeAndAgree) { crowd(4, 20); }
TEST(FightMode, NineBadgesPairOneAtATimeAndAgree) { crowd(9, 8); }

// A badge everyone hears but who hears nobody (across a room, in a case, a
// body between): his beacons say he wants nobody, forever. He has the lowest
// MAC, so he is everyone's first pick. The two badges who can hear each other
// must still find each other, on a clear link and in a storm.
TEST(FightMode, ABadgeWhoHearsNobodyNeverBlocksTwoWhoDo) {
  uint32_t slowest = 0;
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    for (const bool storm : {false, true}) {
      Ether ether(seed * 7 + 3, storm ? kStorm : Weather{});
      Device x(ether, 1, SpeciesId{2}, seed), a(ether, 2, kGrungo, seed + 1), b(ether, 3, SpeciesId{4}, seed + 2);
      x.air.deaf = true;
      ASSERT_TRUE(x.open(0) && a.open(0) && b.open(0));
      const std::string what = "seed " + std::to_string(seed) + (storm ? " storm" : " clear");
      const auto fighting = [](Device& d, uint32_t t) { return uint8_t(d.s.view(t).phase) >= uint8_t(DuelPhase::Staking); };
      const uint32_t paired = run(ether, {&x, &a, &b}, 1, 1200, [&](uint32_t t) { return fighting(a, t) && fighting(b, t); });
      ASSERT_LT(paired, 1200u) << what << ": A and B never paired with the deaf badge in the room";
      // Patience (4 s) on the deaf badge, then the pair: well inside 15 s even in a storm.
      EXPECT_LT(paired, 150u) << what;
      slowest = std::max(slowest, paired);
      EXPECT_EQ(a.s.view(paired).them.lineageId, b.setup.card.lineageId) << what;
      EXPECT_EQ(b.s.view(paired).them.lineageId, a.setup.card.lineageId) << what;
      run(ether, {&x, &a, &b}, paired + 1, paired + 6000, [&](uint32_t) { return !a.meetings.empty() && !b.meetings.empty(); });
      ASSERT_FALSE(a.meetings.empty() || b.meetings.empty()) << what;
      expectSameBattle(a.meetings.front(), b.meetings.front());
      EXPECT_TRUE(x.meetings.empty()) << what;
    }
  }
  std::printf("one-way badge in the room: slowest pairing %u ticks\n", unsigned(slowest));
}

// A phantom: his beacons arrive and he claims the lowest badge, but his
// battle frames never do, so every greeting with him falls through. He is
// set aside after one, and the two real badges pair.
TEST(FightMode, APhantomWhoseGreetingsNeverArriveIsSetAside) {
  uint32_t slowest = 0;
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    Ether ether(seed * 5 + 2, seed % 2 ? kStorm : Weather{});
    Device a(ether, 1, kGrungo, seed), x(ether, 2, SpeciesId{2}, seed + 1), b(ether, 3, SpeciesId{4}, seed + 2);
    x.air.unicastLost = true;
    ASSERT_TRUE(x.open(0) && a.open(0) && b.open(0));
    const auto fighting = [](Device& d, uint32_t t) { return uint8_t(d.s.view(t).phase) >= uint8_t(DuelPhase::Staking); };
    const uint32_t paired = run(ether, {&x, &a, &b}, 1, 1200, [&](uint32_t t) { return fighting(a, t) && fighting(b, t); });
    ASSERT_LT(paired, 1200u) << "seed " << seed << ": the phantom kept A from ever pairing with B";
    EXPECT_LT(paired, 400u) << "seed " << seed;   // a storm may drop the real greeting too
    slowest = std::max(slowest, paired);
    EXPECT_EQ(a.s.view(paired).them.lineageId, b.setup.card.lineageId) << "seed " << seed;
  }
  std::printf("phantom in the room: slowest pairing %u ticks\n", unsigned(slowest));
}

// Four badges and one who hears nobody (the lowest address): the four still
// pair off, each fight agreed on both sides.
TEST(FightMode, FourBadgesAndADeafOneStillPairOff) {
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Ether ether(seed * 11 + 4, kStorm);
    std::vector<std::unique_ptr<Device>> owned;
    std::vector<Device*> all;
    for (uint8_t i = 0; i < 5; ++i) {
      owned.push_back(std::make_unique<Device>(ether, uint8_t(i + 1), SpeciesId{uint8_t(i % 9)}, seed * 16 + i));
      all.push_back(owned.back().get());
    }
    all[0]->air.deaf = true;
    for (Device* d : all) ASSERT_TRUE(d->open(0));
    runAll(ether, all, 1, 12000, [&](uint32_t) {
      for (uint8_t i = 1; i < 5; ++i)
        if (all[i]->meetings.empty()) return false;
      return true;
    });
    for (uint8_t i = 1; i < 5; ++i) {
      ASSERT_FALSE(all[i]->meetings.empty()) << "seed " << seed << " badge " << int(i) << " never fought";
      const Meeting& m = all[i]->meetings.front();
      const Meeting* theirs = nullptr;
      for (Device* d : all)
        for (const Meeting& o : d->meetings)
          if (d != all[i] && o.battle == m.battle) theirs = &o;
      ASSERT_NE(theirs, nullptr) << "seed " << seed;
      expectSameBattle(m, *theirs);
    }
    EXPECT_TRUE(all[0]->meetings.empty());
  }
}

// Six badges who hear nobody, all ABOVE the two who can hear each other, on a lossy link.
TEST(FightMode, SixDeafBadgesAboveTwoWhoHearEachOtherNeverBlockThem) {
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    Ether ether(seed * 3 + 9, Weather{360, 0, 0, 0});
    Device a(ether, 1, kGrungo, seed), b(ether, 2, SpeciesId{4}, seed + 2);
    std::vector<std::unique_ptr<Device>> ghosts;
    std::vector<Device*> all{&a, &b};
    for (uint8_t i = 0; i < 6; ++i) {
      ghosts.push_back(std::make_unique<Device>(ether, uint8_t(10 + i), SpeciesId{2}, seed * 9 + i));
      ghosts.back()->air.deaf = true;
      all.push_back(ghosts.back().get());
    }
    for (Device* d : all) ASSERT_TRUE(d->open(0));
    const auto fighting = [](Device& d, uint32_t t) { return uint8_t(d.s.view(t).phase) >= uint8_t(DuelPhase::Staking); };
    const uint32_t paired = runAll(ether, all, 1, 1200, [&](uint32_t t) { return fighting(a, t) && fighting(b, t); });
    EXPECT_LT(paired, 150u) << "seed " << seed;
  }
}

// A messy room: six badges who hear each other, two who hear nobody and a
// phantom, in a storm, addresses shuffled. Every one of the six fights within
// a few minutes, and both sides of every fight agree on every byte.
TEST(FightMode, AMessyRoomStillPairsEveryoneWhoCanHear) {
  for (uint32_t seed = 1; seed <= 25; ++seed) {
    Ether ether(seed * 17 + 5, kStorm);
    Rng shuffle = Rng::seeded(seed);
    uint8_t ids[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    for (uint8_t i = 8; i > 0; --i) std::swap(ids[i], ids[shuffle.below(i + 1)]);
    std::vector<std::unique_ptr<Device>> owned;
    std::vector<Device*> all;
    for (uint8_t i = 0; i < 9; ++i) {
      owned.push_back(std::make_unique<Device>(ether, ids[i], SpeciesId{uint8_t(i % 9)}, seed * 16 + i));
      owned.back()->stayFor = 0;
      all.push_back(owned.back().get());
    }
    all[6]->air.deaf = all[7]->air.deaf = true;
    all[8]->air.unicastLost = true;
    for (Device* d : all) ASSERT_TRUE(d->open(0));
    const uint32_t t = runAll(ether, all, 1, 6000, [&](uint32_t) {
      for (uint8_t i = 0; i < 6; ++i)
        if (all[i]->meetings.empty()) return false;
      return true;
    });
    for (uint8_t i = 0; i < 6; ++i) {
      ASSERT_FALSE(all[i]->meetings.empty()) << "seed " << seed << ": badge " << int(ids[i]) << " never fought (tick " << t << ")";
      for (const Meeting& m : all[i]->meetings) {
        const Meeting* theirs = nullptr;
        for (Device* d : all)
          for (const Meeting& o : d->meetings)
            if (d != all[i] && o.battle == m.battle) theirs = &o;
        ASSERT_NE(theirs, nullptr) << "seed " << seed << ": a fight only one side met";
        expectSameBattle(m, *theirs);
      }
    }
    for (uint8_t i = 6; i < 9; ++i) EXPECT_TRUE(all[i]->meetings.empty()) << "seed " << seed;
  }
}

// Two badges alone never wait on a set-aside: a pair on a lossy link that
// misses each other's first claims still pairs as soon as a claim lands.
TEST(FightMode, TwoBadgesAloneNeverWaitOnPatience) {
  uint32_t slowest = 0;
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    Ether ether(seed * 13 + 1, Weather{500, 100, 200, 8});   // half the frames lost
    Device a(ether, 1, kGrungo, seed), b(ether, 2, SpeciesId{4}, seed + 9);
    ASSERT_TRUE(a.open(0) && b.open(0));
    const uint32_t paired = run(ether, {&a, &b}, 1, 1200, [&](uint32_t t) {
      return uint8_t(a.s.view(t).phase) >= uint8_t(DuelPhase::Staking) && uint8_t(b.s.view(t).phase) >= uint8_t(DuelPhase::Staking);
    });
    EXPECT_LT(paired, 150u) << "seed " << seed;
    slowest = std::max(slowest, paired);
  }
  std::printf("two badges at 50%% loss: slowest pairing %u ticks\n", unsigned(slowest));
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

// ---- mixed firmware: a badge on other duel rules (#58) -----------------------------------

// What every badge before #58 puts in the roster field: plain rosterHash(), as
// test_species_registry pins it. Those badges resolve() by the old rules.
constexpr uint16_t kOldRules = 5381;

// Makes `d` a badge on the old rules, at the wire: it sends kOldRules where
// this firmware sends duelRulesHash(), and hears the two the other way round,
// so it sees itself exactly as an old badge does (its own kind match, ours
// do not).
void speakOldRules(Device& d) {
  d.air.translate = [](Datagram& f) {
    std::optional<Packet> p = decode(f.bytes, f.len);
    if (!p) return;
    const auto swap = [](uint16_t& h) { h = h == duelRulesHash() ? kOldRules : h == kOldRules ? duelRulesHash() : h; };
    if (BeaconPacket* b = std::get_if<BeaconPacket>(&*p)) swap(b->roster);
    else if (StatePacket* s = std::get_if<StatePacket>(&*p)) swap(s->roster);
    else return;
    f.len = uint8_t(encode(*p, f.bytes));
  };
}

TEST(MixedRules, TheNewRulesSpeakADifferentHashFromTheOld) {
  EXPECT_NE(duelRulesHash(), kOldRules) << "an old badge would resolve our battles by its own rules";
}

// The two badges see each other, greyed out ("they speak another frog"), and
// neither ever claims, greets or fights the other, through the whole of fight
// mode, until the radio gives up on its own.
TEST(MixedRules, AnOldBadgeAndANewOneNeverFight) {
  for (const bool storm : {false, true}) {
    Ether ether(storm ? 11 : 10, storm ? kStorm : Weather{});
    Device a(ether, 1, kGrungo, 1), old(ether, 2, SpeciesId{4}, 2);
    speakOldRules(old);
    ASSERT_TRUE(a.open(0) && old.open(0));
    run(ether, {&a, &old}, 1, 100, [](uint32_t) { return false; });
    for (Device* d : {&a, &old}) {
      const DuelView v = d->s.view(100);
      ASSERT_EQ(v.peerCount, 1);
      EXPECT_FALSE(v.peers[0].compatible) << "greyed out on both rims";
      EXPECT_FALSE(v.peers[0].chosen) << "never claimed";
    }
    const uint32_t end = runOut(ether, a, old, 100, T::kFightModeTicks + 400);
    EXPECT_LT(end, T::kFightModeTicks + 400u) << "both radios give up by themselves";
    for (Device* d : {&a, &old}) {
      EXPECT_EQ(d->phases & ~((1u << uint8_t(DuelPhase::Lobby)) | (1u << uint8_t(DuelPhase::Closed))), 0u)
          << "never past the lobby";
      EXPECT_TRUE(d->meetings.empty());
    }
  }
}

// In a room of both, each badge pairs with one on its own rules, never across,
// and each pair agrees on every byte.
TEST(MixedRules, ARoomOfOldAndNewBadgesPairsEachWithItsOwnKind) {
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Ether ether(seed * 3 + 1, seed % 2 ? kStorm : Weather{});
    Device a(ether, 1, kGrungo, seed), x(ether, 2, SpeciesId{2}, seed + 1), b(ether, 3, SpeciesId{4}, seed + 2),
        y(ether, 4, SpeciesId{6}, seed + 3);
    speakOldRules(x), speakOldRules(y);
    ASSERT_TRUE(a.open(0) && x.open(0) && b.open(0) && y.open(0));
    run(ether, {&a, &x, &b, &y}, 1, 20000, [&](uint32_t) {
      return !a.meetings.empty() && !b.meetings.empty() && !x.meetings.empty() && !y.meetings.empty();
    });
    const std::string what = "seed " + std::to_string(seed);
    ASSERT_FALSE(a.meetings.empty() || b.meetings.empty() || x.meetings.empty() || y.meetings.empty()) << what;
    EXPECT_EQ(a.meetings.front().friendCard.lineageId, b.setup.card.lineageId) << what;
    EXPECT_EQ(x.meetings.front().friendCard.lineageId, y.setup.card.lineageId) << what;
    expectSameBattle(a.meetings.front(), b.meetings.front());
    expectSameBattle(x.meetings.front(), y.meetings.front());
  }
}

// A State from a badge on the old rules that names a battle in progress (a
// replayed or crossed frame) is ignored: the pair fights on and agrees.
TEST(MixedRules, AnOldRulesStateNamingOurBattleIsIgnored) {
  Ether ether(5, {});
  Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
  ASSERT_TRUE(a.open(0) && b.open(0));
  bool injected = false;
  runOut(ether, a, b);
  ASSERT_EQ(a.meetings.size(), 1u);
  const Meeting clean = a.meetings[0];
  Ether again(5, {});
  Device a2(again, 1, kGrungo, 1), b2(again, 2, SpeciesId{4}, 2);
  ASSERT_TRUE(a2.open(0) && b2.open(0));
  run(again, {&a2, &b2}, 1, 30000, [&](uint32_t t) {
    const DuelView v = b2.s.view(t);
    if (!injected && v.phase == DuelPhase::Choosing && v.hasLast) {
      StatePacket st;
      st.roster = kOldRules;
      st.battle = clean.battle;
      st.card = a2.setup.card;
      st.moveCount = 3;   // a log that disagrees with what b2 holds: a Conflict, were it heard
      for (uint8_t i = 0; i < 3; ++i) st.moves[i] = Move{Stance(i % 3), false, false}.pack();
      Datagram d;
      d.from = addr(1), d.rssi = -40;
      d.len = uint8_t(encode(Packet{st}, d.bytes));
      b2.air.inject(d);
      injected = true;
    }
    return !a2.s.active() && !b2.s.active();
  });
  ASSERT_TRUE(injected);
  ASSERT_EQ(a2.meetings.size(), 1u);
  ASSERT_EQ(b2.meetings.size(), 1u);
  expectSameBattle(a2.meetings[0], b2.meetings[0]);
  EXPECT_EQ(b2.meetings[0].outcomeHash, clean.outcomeHash) << "the old frame changed nothing";
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

// Both kids ask for a rematch on a lossy air (#54): one hears the other's ask
// first and goes back to the lobby pinned to him while he still waits on the
// friends screen, beaconing nothing. Its claim, beaconed at once, tells him it
// asked; before, a lost State left him to wait out the friends screen alone.
TEST(DuelSession, BothAskedRematchesOnEveryAir) {
  const Weather airs[] = {Weather{}, kStorm, Weather{500, 0, 0, 0}, Weather{200, 200, 400, 12}};
  int runs = 0, both = 0;
  for (const Weather& w : airs)
    for (uint64_t seed = 1; seed <= 12; ++seed) {
      Ether ether(seed * 13 + 7, w);
      Device a(ether, 1, kGrungo, 1), b(ether, 2, SpeciesId{4}, 2);
      a.policy.rematch = b.policy.rematch = true;
      a.maxBouts = b.maxBouts = 2;
      ASSERT_TRUE(a.open(0) && b.open(0));
      run(ether, {&a, &b}, 1, 60000, [&](uint32_t) { return a.bouts == 2 && b.bouts == 2 && a.meetings.size() >= 2 && b.meetings.size() >= 2; });
      if (a.meetings.empty() || b.meetings.empty()) continue;   // the first bout never ended here: nothing to rematch
      ++runs;
      const bool ok = a.bouts == 2 && b.bouts == 2;
      both += ok;
      EXPECT_TRUE(ok) << "air drop=" << w.drop << " seed " << seed << ": bouts " << int(a.bouts) << "/" << int(b.bouts);
    }
  std::printf("[ rematch ] both rematched in %d of %d runs\n", both, runs);
  EXPECT_GE(runs, 40);
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
      late.roster = duelRulesHash();
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
  EXPECT_TRUE(injected) << "bouts " << int(a.bouts) << "/" << int(b.bouts) << ", ended " << int(a.ended) << "/"
                        << int(b.ended) << ", meetings " << a.meetings.size() << "/" << b.meetings.size();
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

// A BOOT hold that began before the skull showed (a heavy special held
// through the reveal) never finishes: the kid must let go and hold again.
TEST(DuelSession, AHoldCarriedIntoMercyIsNoFinish) {
  uint32_t heldIn = 0, regripped = 0;
  for (uint32_t seed = 1; seed <= 60; ++seed) {
    for (Mercy mercy : {Mercy::HeldIn, Mercy::Regrip}) {
      Ether ether(seed, {});
      Device a(ether, 1, kGrungo, seed), b(ether, 2, SpeciesId{4}, seed + 3);
      a.policy.tier = b.policy.tier = Stakes::Mortal;
      a.policy.mercy = b.policy.mercy = mercy;
      ASSERT_TRUE(a.open(0) && b.open(0));
      runOut(ether, a, b);
      if (!a.downed && !b.downed) continue;
      Device& winner = a.downed ? b : a;
      Device& loser = a.downed ? a : b;
      ASSERT_EQ(winner.meetings.size(), 1u);
      if (mercy == Mercy::HeldIn) {
        ++heldIn;
        EXPECT_FALSE(loser.fell) << "seed " << seed;
        EXPECT_EQ(winner.meetings[0].verdict, Verdict::Spare) << "seed " << seed;
      } else {
        ++regripped;
        EXPECT_TRUE(loser.fell) << "seed " << seed;
        EXPECT_TRUE(winner.meetings[0].iSlew) << "seed " << seed;
      }
    }
  }
  EXPECT_GT(heldIn, 5u);
  EXPECT_GT(regripped, 5u);
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
