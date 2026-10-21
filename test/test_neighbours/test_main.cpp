// The neighbours prototype (src/hw/neighbours.h, docs/design/NEIGHBOURS.md):
// the advert's codec, proximity from RSSI, the scan schedule and the rarity
// ledger. Nothing here touches the engine; main.cpp does not include it yet.
#include <gtest/gtest.h>

#include "../../src/hw/neighbours.h"

namespace {

using namespace neighbours;

Addr addr(uint8_t last) { return Addr{{0xd4, 0x05, 0x92, 0x77, 0x1b, last}}; }
const Addr kMe = addr(0x3d), kMossby = addr(0x45), kGilly = addr(0x35), kFizz = addr(0x41);

Advert ad(uint8_t species, uint8_t epoch, uint8_t flags = flag::hatched, uint16_t thanks = 0) {
  Advert a;
  a.species = species, a.epoch = epoch, a.flags = flags, a.thanks = thanks;
  return a;
}

// ------------------------------------------------------------------ advert

TEST(Advert, RoundTripsEveryField) {
  const Advert a = ad(7, 0xA5, flag::hatched | flag::asleep, 0xBEEF);
  uint8_t out[kAdvertBytes];
  ASSERT_EQ(encode(a, out), kAdvertBytes);
  const auto b = decode(out, sizeof out);
  ASSERT_TRUE(b);
  EXPECT_EQ(b->version, kVersion);
  EXPECT_EQ(b->species, 7);
  EXPECT_EQ(b->flags, flag::hatched | flag::asleep);
  EXPECT_EQ(b->epoch, 0xA5);
  EXPECT_EQ(b->thanks, 0xBEEF);
}

TEST(Advert, IsTheBytesTheDesignSays) {
  uint8_t out[kAdvertBytes];
  encode(ad(3, 9, flag::hatched, 0x1234), out);
  const uint8_t want[kAdvertBytes] = {0xFF, 0xFF, 0xB1, 3, flag::hatched, 9, 0x34, 0x12};
  EXPECT_EQ(0, std::memcmp(out, want, sizeof want));
}

TEST(Advert, FitsBesideTheFlagsAndTheServiceUuid) {
  // 31 = flags (3) + the NUS 128-bit UUID (18) + this AD's length and type (2) + its data.
  EXPECT_EQ(3 + 18 + 2 + kAdvertBytes, 31u);
}

TEST(Advert, RefusesWhatIsNotOurs) {
  uint8_t good[kAdvertBytes];
  encode(ad(1, 1), good);
  EXPECT_FALSE(decode(nullptr, 8));
  EXPECT_FALSE(decode(good, 7));
  uint8_t other[kAdvertBytes];
  std::memcpy(other, good, sizeof other);
  other[0] = 0x4C, other[1] = 0x00;   // Apple's id: an iBeacon
  EXPECT_FALSE(decode(other, sizeof other));
  std::memcpy(other, good, sizeof other);
  other[2] = 0xA1;   // someone else's 0xFFFF bytes
  EXPECT_FALSE(decode(other, sizeof other));
  std::memcpy(other, good, sizeof other);
  other[2] = 0xB0;   // version 0
  EXPECT_FALSE(decode(other, sizeof other));
}

TEST(Advert, ANewerVersionIsReadForWhatThisOneKnows) {
  // A later firmware may set reserved flag bits and append bytes; this one
  // still reads the species, epoch and thanks, and ignores the rest.
  uint8_t longer[12];
  encode(ad(5, 2, flag::hatched | 0x80, 0x0102), reinterpret_cast<uint8_t(&)[kAdvertBytes]>(longer));
  longer[2] = 0xB3;   // version 3
  longer[8] = 0xEE, longer[9] = 0xEE, longer[10] = 0xEE, longer[11] = 0xEE;
  const auto a = decode(longer, sizeof longer);
  ASSERT_TRUE(a);
  EXPECT_EQ(a->version, 3);
  EXPECT_EQ(a->species, 5);
  EXPECT_EQ(a->epoch, 2);
  EXPECT_EQ(a->thanks, 0x0102);
  EXPECT_TRUE(a->has(flag::hatched));
  EXPECT_EQ(a->flags & ~flag::known, 0x80);   // kept, unread
}

TEST(Advert, ThanksIsNeverNoneAndNamesGiverAndEpoch) {
  for (int e = 0; e < 256; ++e) EXPECT_NE(thanksTag(kMossby, uint8_t(e)), 0);
  EXPECT_NE(thanksTag(kMossby, 1), thanksTag(kMossby, 2));
  EXPECT_NE(thanksTag(kMossby, 1), thanksTag(kGilly, 1));
  EXPECT_EQ(thanksTag(kMossby, 1), thanksTag(kMossby, 1));
}

// --------------------------------------------------------------- proximity

// `rounds` scan rounds in which `who` is heard at `rssi`, 5 s apart.
void rounds(Proximity& p, const Addr& who, int8_t rssi, int n, uint32_t& ms, uint8_t species = 1) {
  for (int i = 0; i < n; ++i, ms += 5000) {
    p.see(who, rssi, ad(species, 0), species);
    p.endRound(ms);
  }
}
void quiet(Proximity& p, int n, uint32_t& ms) {
  for (int i = 0; i < n; ++i, ms += 5000) p.endRound(ms);
}

TEST(Proximity, AFriendArrivesAfterTwoRoundsNear) {
  Proximity p;
  uint32_t ms = 0;
  rounds(p, kMossby, -60, 1, ms);
  EXPECT_FALSE(p.next());   // one round: maybe a walk-past
  rounds(p, kMossby, -60, 1, ms);
  const auto e = p.next();
  ASSERT_TRUE(e);
  EXPECT_EQ(e->kind, Event::Kind::Arrived);
  EXPECT_EQ(e->who, kMossby);
  EXPECT_EQ(p.atLeast(Band::Near), 1);
  EXPECT_FALSE(p.next());
}

TEST(Proximity, FarAwayIsHeardButNeverArrives) {
  Proximity p;
  uint32_t ms = 0;
  rounds(p, kMossby, -85, 10, ms);
  EXPECT_FALSE(p.next());
  EXPECT_EQ(p.count(), 1);
  EXPECT_EQ(p.atLeast(Band::Near), 0);
}

TEST(Proximity, SideBySideBecomesTogetherAndCountsItsTime) {
  Proximity p;
  uint32_t ms = 0;
  rounds(p, kGilly, -40, 3, ms, 3);
  ASSERT_EQ(p.next()->kind, Event::Kind::Arrived);
  const auto t = p.next();
  ASSERT_TRUE(t);
  EXPECT_EQ(t->kind, Event::Kind::Together);
  EXPECT_EQ(t->species, 3);
  const uint32_t since = ms - 5000;
  rounds(p, kGilly, -40, 12, ms, 3);
  EXPECT_EQ(p.togetherMs(kGilly, ms), ms - since);
  EXPECT_GE(p.togetherMs(kGilly, ms), 60000u);
}

TEST(Proximity, OneLoudFadeDoesNotMoveABand) {
  // The median of three: a single hand over the antenna (-30 dB) changes nothing.
  Proximity p;
  uint32_t ms = 0;
  rounds(p, kMossby, -45, 4, ms);
  while (p.next()) {}
  rounds(p, kMossby, -88, 1, ms);
  rounds(p, kMossby, -45, 1, ms);
  EXPECT_FALSE(p.next());
  EXPECT_EQ(p.find(kMossby)->band, Band::Together);
}

TEST(Proximity, HysteresisKeepsABorderlineFriend) {
  Proximity p;
  uint32_t ms = 0;
  rounds(p, kMossby, -68, 3, ms);
  ASSERT_EQ(p.next()->kind, Event::Kind::Arrived);
  rounds(p, kMossby, -74, 6, ms);   // below near, inside the 6 dB
  EXPECT_FALSE(p.next());
  rounds(p, kMossby, -80, 3, ms);   // past it
  const auto e = p.next();
  ASSERT_TRUE(e);
  EXPECT_EQ(e->kind, Event::Kind::Left);
}

TEST(Proximity, DriftingApartThenGone) {
  Proximity p;
  uint32_t ms = 0;
  rounds(p, kFizz, -40, 3, ms, 6);
  while (p.next()) {}
  rounds(p, kFizz, -62, 3, ms, 6);
  auto e = p.next();
  ASSERT_TRUE(e);
  EXPECT_EQ(e->kind, Event::Kind::Apart);
  quiet(p, 2, ms);
  EXPECT_FALSE(p.next());   // two missed rounds: still here
  quiet(p, 1, ms);
  e = p.next();
  ASSERT_TRUE(e);
  EXPECT_EQ(e->kind, Event::Kind::Left);
  EXPECT_EQ(p.count(), 0);
}

TEST(Proximity, AnOldFirmwareBadgeIsKnownByItsGuess) {
  Proximity p;
  uint32_t ms = 0;
  for (int i = 0; i < 2; ++i, ms += 5000) {
    p.see(kMossby, -55, std::nullopt, 1);
    p.endRound(ms);
  }
  const auto e = p.next();
  ASSERT_TRUE(e);
  EXPECT_EQ(e->species, 1);
  EXPECT_FALSE(p.find(kMossby)->advert);
}

TEST(Proximity, StrangersFarAwayNeverPushOutAFriendNear) {
  Proximity p;
  uint32_t ms = 0;
  for (uint8_t i = 0; i < Proximity::kMaxPeers; ++i) p.see(addr(uint8_t(0x80 + i)), -50, std::nullopt, 0);
  p.endRound(ms), ms += 5000;
  for (uint8_t i = 0; i < Proximity::kMaxPeers; ++i) p.see(addr(uint8_t(0x80 + i)), -50, std::nullopt, 0);
  p.endRound(ms), ms += 5000;
  ASSERT_EQ(p.atLeast(Band::Near), Proximity::kMaxPeers);
  p.see(kMossby, -90, std::nullopt, 1);   // full of friends near: the stranger waits
  EXPECT_FALSE(p.find(kMossby));
}

TEST(Proximity, SameSightingsSameEvents) {
  auto run = [] {
    Proximity p;
    uint32_t ms = 0, sum = 0;
    for (int r = 0; r < 40; ++r, ms += 5000) {
      p.see(kMossby, int8_t(-40 - (r * 7) % 50), ad(1, 0), 1);
      if (r % 3) p.see(kGilly, int8_t(-45 - (r * 11) % 40), ad(3, 0), 3);
      p.endRound(ms);
      while (auto e = p.next()) sum = sum * 31 + uint32_t(e->kind) * 7 + e->who.b[5];
    }
    return sum;
  };
  EXPECT_EQ(run(), run());
}

// ---------------------------------------------------------------- schedule

TEST(Schedule, ListensLittleAndNeverDuringADuelOrOff) {
  EXPECT_EQ(scanEveryMs(Power::Awake, false, true), 0u);   // the air is the duel's, an update, a trial
  EXPECT_EQ(scanEveryMs(Power::Off, false, false), 0u);
  EXPECT_LT(scanEveryMs(Power::Awake, true, false), scanEveryMs(Power::Awake, false, false));
  EXPECT_LE(listeningPer100k(Power::Awake, false), 1000u);   // 1% awake
  EXPECT_LE(listeningPer100k(Power::Low, false), 200u);      // 0.2% with the screen dark
  EXPECT_GE(kWindowMs, 150u + 10u);   // a window hears every advertiser at its slowest interval
}

// ------------------------------------------------------------------ ledger

TEST(Ledger, WavesAtAFirstMeetingAndAfterTimeApart) {
  Ledger l;
  EXPECT_TRUE(l.notice(kMossby, 1, 100));
  EXPECT_TRUE(l.firstMeeting(kMossby));
  EXPECT_FALSE(l.notice(kMossby, 1, 100 + 5 * kTicksPerMinute));   // a fade and back: the same visit
  EXPECT_FALSE(l.notice(kMossby, 1, 100 + 14 * kTicksPerMinute));  // the gap counts from the last sight
  EXPECT_TRUE(l.notice(kMossby, 1, 100 + 30 * kTicksPerMinute));
  EXPECT_EQ(l.find(kMossby)->meetings, 2);
  EXPECT_FALSE(l.firstMeeting(kMossby));
}

TEST(Ledger, CommentsAreRare) {
  Ledger l;
  l.notice(kMossby, 1, 1);
  l.notice(kGilly, 3, 1);
  EXPECT_TRUE(l.mayComment(kMossby, 10));
  l.commented(kMossby, 10);
  EXPECT_FALSE(l.mayComment(kGilly, 10 + kTicksPerMinute));        // anyone: 5 minutes
  EXPECT_TRUE(l.mayComment(kGilly, 10 + 5 * kTicksPerMinute));
  EXPECT_FALSE(l.mayComment(kMossby, 10 + 20 * kTicksPerMinute));  // the pair: 30 minutes
  EXPECT_TRUE(l.mayComment(kMossby, 10 + 30 * kTicksPerMinute));
  EXPECT_FALSE(l.mayComment(kFizz, 10 + 60 * kTicksPerMinute));    // never met
}

// The first epoch for which the roll says yes for (giver -> receiver).
uint8_t luckyEpoch(const Addr& giver, const Addr& receiver, uint8_t from = 0) {
  for (int e = from; e < 256; ++e)
    if (Ledger::mayGive(giver, uint8_t(e), receiver)) return uint8_t(e);
  ADD_FAILURE() << "no lucky epoch";
  return 0;
}

TEST(Ledger, AGiftNeedsAMinuteTogether) {
  Ledger l;
  l.notice(kMossby, 1, 1);
  const uint8_t e = luckyEpoch(kMossby, kMe);
  EXPECT_FALSE(l.receive(kMe, kMossby, ad(1, e), 59999, 100));
  EXPECT_TRUE(l.receive(kMe, kMossby, ad(1, e), 60000, 100));
}

TEST(Ledger, BothBadgesAgreeWithNoMessage) {
  // The receiver decides from the giver's advert; the giver from its own
  // address and epoch. The same roll, so the giver's wind-up and the
  // receiver's gift match, and the receiver's thanks closes it.
  for (uint8_t e = 0; e < 64; ++e) {
    Ledger receiver;
    receiver.notice(kMossby, 1, 1);
    const bool giverThinks = Ledger::mayGive(kMossby, e, kMe);
    const auto got = receiver.receive(kMe, kMossby, ad(1, e), 60000, 100);
    EXPECT_EQ(giverThinks, bool(got)) << int(e);
    if (!got) continue;
    Ledger giver;
    giver.notice(kMe, 2, 1);
    EXPECT_FALSE(giver.thanked(kMossby, e, kMe, ad(2, 0)));   // no thanks yet
    EXPECT_TRUE(giver.thanked(kMossby, e, kMe, ad(2, 0, flag::hatched, got->thanks)));
    EXPECT_FALSE(giver.thanked(kMossby, e, kMe, ad(2, 0, flag::hatched, got->thanks)));   // shown once
  }
}

TEST(Ledger, OneGiftPerGiverEpochEvenReplayed) {
  Ledger l;
  l.notice(kMossby, 1, 1);
  const uint8_t e = luckyEpoch(kMossby, kMe);
  ASSERT_TRUE(l.receive(kMe, kMossby, ad(1, e), 60000, 100));
  const Ledger saved = l;   // what the friends file holds after the gift
  EXPECT_FALSE(l.receive(kMe, kMossby, ad(1, e), 600000, 100 + 2 * kEpochTicks));   // the same advert, much later
  Ledger rebooted = saved;
  EXPECT_FALSE(rebooted.receive(kMe, kMossby, ad(1, e), 60000, 200));   // a reboot farms nothing
}

TEST(Ledger, FromOneFriendAtMostOneAPetDay) {
  Ledger l;
  l.notice(kMossby, 1, 1);
  const uint8_t e1 = luckyEpoch(kMossby, kMe);
  const uint8_t e2 = luckyEpoch(kMossby, kMe, uint8_t(e1 + 1));
  ASSERT_TRUE(l.receive(kMe, kMossby, ad(1, e1), 60000, 1000));
  // The giver's clock says a new epoch (a fast-forwarded or reset badge), mine does not.
  EXPECT_FALSE(l.receive(kMe, kMossby, ad(1, e2), 60000, 1000 + kEpochTicks / 2));
  EXPECT_TRUE(l.receive(kMe, kMossby, ad(1, e2), 60000, 1000 + kEpochTicks));
}

TEST(Ledger, AtMostThreeGiftsAPetDayFromEveryone) {
  Ledger l;
  Addr givers[5];
  for (uint8_t i = 0; i < 5; ++i) givers[i] = addr(uint8_t(0x60 + i)), l.notice(givers[i], i, 1);
  int got = 0;
  for (uint8_t i = 0; i < 5; ++i)
    got += bool(l.receive(kMe, givers[i], ad(i, luckyEpoch(givers[i], kMe)), 60000, 100 + i));
  EXPECT_EQ(got, 3);
  EXPECT_EQ(l.giftsInEpoch(105), 3);
  EXPECT_EQ(l.giftsInEpoch(100 + kEpochTicks + 5), 0);
}

TEST(Ledger, EggsAndDuelistsGiveNothing) {
  Ledger l;
  l.notice(kMossby, 1, 1);
  const uint8_t e = luckyEpoch(kMossby, kMe);
  EXPECT_FALSE(l.receive(kMe, kMossby, ad(1, e, 0), 60000, 100));
  EXPECT_FALSE(l.receive(kMe, kMossby, ad(1, e, flag::hatched | flag::dueling), 60000, 100));
  EXPECT_TRUE(l.receive(kMe, kMossby, ad(1, e), 60000, 100));
}

TEST(Ledger, AboutHalfOfVisitsBringAGiftAndOneInEightIsRare) {
  int yes = 0, rare = 0;
  for (int e = 0; e < 256; ++e)
    for (uint8_t g = 0; g < 8; ++g) {
      const uint32_t roll = giftRoll(addr(uint8_t(0x30 + g)), kMe, uint8_t(e));
      if ((roll & 0xFF) < Rules{}.chanceIn256) ++yes, rare += giftKind(roll) == 2;
    }
  EXPECT_NEAR(yes / 2048.0, 0.5, 0.05);
  EXPECT_NEAR(double(rare) / yes, 0.125, 0.04);
}

TEST(Ledger, AStrangerCrowdForgetsTheLeastRecentlyMet) {
  Ledger l;
  for (uint8_t i = 0; i < Ledger::kMaxFriends + 4; ++i) l.notice(addr(uint8_t(0x90 + i)), 0, 10u * (i + 1));
  EXPECT_EQ(l.count(), Ledger::kMaxFriends);
  EXPECT_FALSE(l.find(addr(0x90)));
  EXPECT_TRUE(l.find(addr(uint8_t(0x90 + Ledger::kMaxFriends + 3))));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
