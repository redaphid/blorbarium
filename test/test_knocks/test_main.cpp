// KnockSense against real hands (test/fixtures/hands_2026-10-19.txt) and against
// scripted ones. The fixture is the recording that showed the tap engine
// dropping a pellet for every pick-up; its expectations are in its '=' lines.
#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "blorb/knocks.h"
#include "../../src/hw/orient.h"

using namespace blorb;

namespace {

struct Segment {
  std::string label;
  int knockKnocks = 0, singles = -1;   // -1: any
  bool atLeast = false;                // "5+": at least that many
  std::vector<std::pair<uint32_t, BodySample>> samples;
};

std::vector<Segment> loadHands() {
  std::ifstream f("test/fixtures/hands_2026-10-19.txt");
  std::vector<Segment> out;
  for (std::string line; std::getline(f, line);) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    if (line[0] == '=') {
      Segment s;
      std::string eq, singles, kk;
      in >> eq >> s.label >> kk >> singles;
      s.atLeast = !kk.empty() && kk.back() == '+';
      s.knockKnocks = std::stoi(kk);
      s.singles = singles == "-" ? -1 : std::stoi(singles);
      out.push_back(s);
      continue;
    }
    uint32_t ms;
    BodySample b;
    in >> ms >> b.ax >> b.ay >> b.az;
    if (in >> b.peakAx >> b.peakAy >> b.peakAz) b.hasPeak = true;
    out.back().samples.push_back({ms, b});
  }
  return out;
}

struct Count { int singles = 0, knockKnocks = 0; std::string at; };

Count play(const Segment& s) {
  KnockSense k;
  Count c;
  const uint32_t t0 = s.samples.front().first;
  auto note = [&](uint8_t code, uint32_t ms) {
    if (!code) return;
    (code == 2 ? c.knockKnocks : c.singles)++;
    char b[32];
    std::snprintf(b, sizeof b, " %s@%.2f", code == 2 ? "kk" : "k", (ms - t0) / 1000.0);
    c.at += b;
  };
  uint32_t last = t0;
  for (const auto& [ms, b] : s.samples) note(k.sample(b, ms), ms), last = ms;
  // Still for a second after the segment: a burst at its very end is decided.
  BodySample still = s.samples.back().second;
  for (uint32_t ms = last + 20; ms <= last + 1000; ms += 20) note(k.sample(still, ms), ms);
  return c;
}

// A scripted badge held still, with knocks: a jolt through the face that rings once.
std::vector<std::pair<uint32_t, BodySample>> held(std::initializer_list<uint32_t> knocksAt, uint32_t untilMs) {
  std::vector<std::pair<uint32_t, BodySample>> v;
  for (uint32_t ms = 0; ms < untilMs; ms += 20) {
    BodySample b;
    b.ay = 990, b.az = -150;
    for (uint32_t at : knocksAt) {
      if (ms == at) b.az = 1800;
      if (ms == at + 20) b.az = -1700;
    }
    v.push_back({ms, b});
  }
  return v;
}

Count script(std::initializer_list<uint32_t> knocksAt, uint32_t untilMs = 4000) {
  Segment s;
  s.samples = held(knocksAt, untilMs);
  return play(s);
}

}  // namespace

TEST(Hands, EverySegmentOfTheRecordingCountsWhatTheHandsDid) {
  const std::vector<Segment> segs = loadHands();
  ASSERT_GE(segs.size(), 15u) << "test/fixtures/hands_2026-10-19.txt";
  for (const Segment& s : segs) {
    const Count c = play(s);
    std::printf("[ hands ] %-22s knock knocks %d (want %d), singles %d:%s\n", s.label.c_str(), c.knockKnocks,
                s.knockKnocks, c.singles, c.at.c_str());
    if (s.atLeast) EXPECT_GE(c.knockKnocks, s.knockKnocks) << s.label << c.at;
    else EXPECT_EQ(c.knockKnocks, s.knockKnocks) << s.label << c.at;
    if (s.singles >= 0) EXPECT_EQ(c.singles, s.singles) << s.label << c.at;
  }
}

TEST(Hands, SingleKnocksAreSinglesNeverAKnockKnock) {
  for (const Segment& s : loadHands())
    if (s.label.rfind("3_single", 0) == 0) {
      const Count c = play(s);
      EXPECT_EQ(c.knockKnocks, 0);
      EXPECT_GE(c.singles, 4) << c.at;
    }
}

// Replayed as the shell does (main.cpp feel()): KnockSense first, its busy()
// riding the sample as `knocking`, then every detector. A knock's bounce in
// the hand swings both ways like a tiny shake; on the board every knock knock
// also told the future until `knocking` held the shake off.
int shakes(const Segment& s) {
  KnockSense k;
  orient::Up up{1};   // ORIENT_R0 on the 1.28
  Detectors d;
  SenseOut out;
  int n = 0;
  for (const auto& [ms, b] : s.samples) {
    const uint8_t rot = up.sample(b, ms);
    BodySample x = up.toScreen(b);   // the screen's frame, as the Dish sees it
    x.tapCode = k.sample(b, ms);
    x.knocking = k.busy();
    x.sideDown = rot == 1 || rot == 3, x.invertedDown = rot == 0;
    out.clear();
    d.sample(x, ms, out);
    for (uint8_t i = 0; i < out.stimCount; ++i) n += out.stimuli[i] == stim::shake;
  }
  return n;
}

TEST(Hands, AKnockKnockIsNoShakeAndAShakeStillIs) {
  for (const Segment& s : loadHands()) {
    if (s.label.rfind("L1_", 0) == 0 || s.label.rfind("L2_", 0) == 0 || s.label.rfind("2b_", 0) == 0) {
      EXPECT_EQ(shakes(s), 0) << s.label;
    }
    if (s.label == "9_shake") {
      EXPECT_GE(shakes(s), 1) << s.label;
    }
  }
}

TEST(Scripted, TwoKnocksAreAKnockKnockReportedOnceItIsOver) {
  KnockSense k;
  int code = 0;
  uint32_t when = 0;
  for (const auto& [ms, b] : held({1000, 1300}, 3000))
    if (uint8_t c = k.sample(b, ms)) code = c, when = ms;
  EXPECT_EQ(code, 2);
  EXPECT_GT(when, 1300 + KnockSense::kChainMs);
  EXPECT_LE(when, 1340 + KnockSense::kChainMs + 20) << "the last knock rings to 1340";
}

TEST(Scripted, OneKnockIsASingleAndTwoToFourAreAKnockKnock) {
  EXPECT_EQ(script({1000}).singles, 1);
  EXPECT_EQ(script({1000}).knockKnocks, 0);
  EXPECT_EQ(script({1000, 1240, 1480}).knockKnocks, 1) << "kids knock thrice";
  EXPECT_EQ(script({1000, 1240, 1480, 1720}).knockKnocks, 1) << "and four times";
  const Count five = script({1000, 1240, 1480, 1720, 1960});
  EXPECT_EQ(five.knockKnocks + five.singles, 0) << "five is hammering:" << five.at;
}

// A quick second knock, 0.12 s after the first once its bounce has died
// down, is a knock knock (a kid on the board did it); a hard knock's own
// rebound at 0.08 s is not.
TEST(Scripted, AQuickSecondKnockCounts) {
  EXPECT_EQ(script({1000, 1120}).knockKnocks, 1);
}

TEST(Scripted, KnocksTooFarApartAreTwoSingles) {
  const Count c = script({1000, 2000});
  EXPECT_EQ(c.knockKnocks, 0);
  EXPECT_EQ(c.singles, 2);
}

TEST(Scripted, AKnockOnABadgeThatWasMovingCountsForNothing) {
  Segment s;
  s.samples = held({1000, 1300}, 3000);
  for (auto& [ms, b] : s.samples)   // waved about until just before the knocks
    if (ms >= 500 && ms < 990) b.ax = int16_t(ms % 40 ? 400 : -400);
  const Count c = play(s);
  EXPECT_EQ(c.knockKnocks + c.singles, 0);
}

TEST(Scripted, AKnockThatLeavesTheBadgePointingElsewhereWasABump) {
  Segment s;
  s.samples = held({1000, 1300}, 3000);
  for (auto& [ms, b] : s.samples)
    if (ms > 1340) b.ax = 500, b.ay = 860;   // turned 30 degrees by it: set down, not knocked
  const Count c = play(s);
  EXPECT_EQ(c.knockKnocks + c.singles, 0);
}

// "One more" (KnockSense::waitingSecond, what GestureCues lights): on from the
// first knock until the second lands, never before a knock, never for a badge
// that was moving, and off once the burst is reported.
TEST(Waiting, LightsBetweenTheFirstKnockAndTheSecond) {
  KnockSense k;
  bool before = false, between = false, afterSecond = false, afterReport = false;
  bool reported = false;
  for (const auto& [ms, b] : held({1000, 1300}, 3000)) {
    if (k.sample(b, ms)) reported = true;
    const bool w = k.waitingSecond();
    if (ms < 1000) before |= w;
    else if (ms >= 1040 && ms < 1300) {
      between |= w;
      EXPECT_TRUE(w) << ms;
    }
    else if (ms >= 1300 && !reported) afterSecond |= w;
    else if (reported) afterReport |= w;
  }
  EXPECT_FALSE(before);
  EXPECT_TRUE(between);
  EXPECT_FALSE(afterSecond) << "the second knock landed: nothing more is wanted";
  EXPECT_FALSE(afterReport);
}

TEST(Waiting, ASingleKnockWaitsOnlyAsLongAsTheWindow) {
  KnockSense k;
  uint32_t lastWaiting = 0;
  for (const auto& [ms, b] : held({1000}, 3000)) {
    k.sample(b, ms);
    if (k.waitingSecond()) lastWaiting = ms;
  }
  EXPECT_GE(lastWaiting, 1000 + KnockSense::kChainMs);
  EXPECT_LE(lastWaiting, 1040 + KnockSense::kChainMs + 20);
}

TEST(Waiting, NotForABadgeThatWasMoving) {
  KnockSense k;
  auto v = held({1000, 1300}, 3000);
  for (auto& [ms, b] : v)
    if (ms >= 500 && ms < 990) b.ax = int16_t(ms % 40 ? 400 : -400);
  for (const auto& [ms, b] : v) {
    k.sample(b, ms);
    EXPECT_FALSE(k.waitingSecond()) << ms;
  }
}

// Against the hands: every knock knock lit "one more" before it was reported,
// and handling with no knocks rarely lit it at all.
TEST(Waiting, TheHandsSawOneMoreBeforeEveryKnockKnock) {
  for (const Segment& s : loadHands()) {
    KnockSense k;
    bool lit = false, prev = false;
    int litKk = 0, kk = 0, episodes = 0, reports = 0;
    for (const auto& [ms, b] : s.samples) {
      const uint8_t code = k.sample(b, ms);
      const bool w = k.waitingSecond();
      episodes += w && !prev;
      prev = w;
      lit |= w;
      if (code) ++reports;
      if (code == 2) ++kk, litKk += lit;
      if (code) lit = false;
    }
    std::printf("[ waiting ] %-22s one-more %d times, %d reported (%d knock knocks, %d lit first)\n", s.label.c_str(),
                episodes, reports, kk, litKk);
    EXPECT_EQ(litKk, kk) << s.label;
    if (s.label[0] >= '4' && s.label[0] <= '9') EXPECT_LE(episodes, 1) << s.label << ": handling lit \"one more\"";
  }
}

// "Heard" (KnockSense::knockKnock, what the rim flashes on): on once the
// second knock has rung out, 0.5 s before the knock knock is reported, and
// off once it is; never for one knock, never for a burst that ran on into
// hammering, and never for a shake from a still badge.
TEST(Heard, LightsOnTheSecondKnockLongBeforeTheReport) {
  KnockSense k;
  uint32_t lit = 0, reported = 0;
  for (const auto& [ms, b] : held({1000, 1300}, 3000)) {
    if (k.sample(b, ms)) reported = ms;
    if (k.knockKnock() && !lit) lit = ms;
    if (reported) EXPECT_FALSE(k.knockKnock()) << ms;
  }
  EXPECT_GE(lit, 1300u + KnockSense::kHeardQuietMs);
  EXPECT_LE(lit, 1340u + KnockSense::kHeardQuietMs + 20) << "once the second knock has rung out";
  EXPECT_GE(reported, lit + KnockSense::kChainMs - KnockSense::kHeardQuietMs - 60) << "well before the report";
}

TEST(Heard, NeverForOneKnockOrForHammering) {
  for (std::initializer_list<uint32_t> at : {std::initializer_list<uint32_t>{1000},
                                             std::initializer_list<uint32_t>{1000, 1240, 1480, 1720, 1960}}) {
    KnockSense k;
    bool litAtEnd = false, litForOne = false;
    for (const auto& [ms, b] : held(at, 4000)) {
      k.sample(b, ms);
      if (at.size() == 1) litForOne |= k.knockKnock();
      if (ms > 1960 + 40) litAtEnd |= k.knockKnock();
    }
    EXPECT_FALSE(litForOne);
    EXPECT_FALSE(litAtEnd) << "a fifth knock is hammering: the flash promised nothing more";
  }
}

TEST(Heard, NeverForAShakeFromAStillBadge) {
  KnockSense k;
  for (uint32_t ms = 0; ms < 4000; ms += 20) {
    BodySample b;
    b.az = 1000;
    if (ms >= 1000 && ms < 2200) b.ax = int16_t((ms / 60) % 2 ? 1500 : -1500);   // the sim's !shake
    k.sample(b, ms);
    EXPECT_FALSE(k.knockKnock()) << ms;
  }
}

// Against the hands: every knock knock was heard before it was reported, and
// handling with no knocks was never heard as one.
TEST(Heard, TheHandsHeardEveryKnockKnockFirstAndNoHandling) {
  for (const Segment& s : loadHands()) {
    KnockSense k;
    bool heard = false, prev = false;
    int heardFirst = 0, kk = 0, episodes = 0;
    for (const auto& [ms, b] : s.samples) {
      const uint8_t code = k.sample(b, ms);
      const bool h = k.knockKnock();
      episodes += h && !prev;
      prev = h;
      heard |= h;
      if (code == 2) ++kk, heardFirst += heard;
      if (code) heard = false;
    }
    EXPECT_EQ(heardFirst, kk) << s.label;
    if (s.label[0] >= '3' && s.label[0] <= '9') EXPECT_EQ(episodes, 0) << s.label << ": no knock knock here";
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
