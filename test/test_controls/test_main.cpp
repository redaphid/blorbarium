// Every control against real hands (test/fixtures/hands_2026-10-19.txt): how
// soon each one is acknowledged and decided, in sample time, and what it fires
// that the hands did not mean. The badge's own numbers (the loop, the tick, the
// frame, the panel) come from `DEBUG latency` (src/latency.h) and
// scripts/test/latency_probe.py; this is the part a recording can answer, and
// the guard that a faster control never buys a false fire.
//
// Replayed as the badge does it: the shell (hw::Feel: the knocks, the poses,
// the turn into the screen's frame at home), then every detector. The
// recording was made with USB-C up as home (rotation 2).
//
// For each gesture, the decisive moment is read off the samples themselves:
//   knock knock   the second knock (KnockSense::knockKnock rising)
//   side hold     the first steady sample with gravity along the screen's x,
//                 past 45 degrees (the raw pose), of the run that held
//   hang          the same, gravity up the screen
// and from it: the acknowledgement (the rim's flash, the hold ring, the hang's
// charge: what the next frame can draw) and the word (press, hold, hang).
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "blorb/knocks.h"
#include "blorb/senses.h"
#include "blorb/dish_duel.h"
#include "../../src/hw/feel.h"
#include "../../src/latency.h"

using namespace blorb;

namespace {

constexpr int kR0 = 1;          // ORIENT_R0 on the 1.28
constexpr uint8_t kHome = 2;    // the recording's home: USB-C up

struct Segment {
  std::string label;
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
      std::string eq;
      in >> eq >> s.label;
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

// The raw pose of one sample in the screen's frame at home: what a hand did,
// before any filter. 0 none, 1 on a side, 2 upside down.
int rawPose(const BodySample& h) {
  const int32_t m2 = int32_t(h.ax) * h.ax + int32_t(h.ay) * h.ay + int32_t(h.az) * h.az;
  if (m2 < 750 * 750 || m2 > 1250 * 1250) return -1;   // a jolt: says nothing
  if (std::abs(h.az) > 500) return 0;                    // lying, not on edge
  if (std::abs(h.ax) >= 700) return 1;
  if (h.ay >= 700) return 2;
  return 0;
}

// A raw pose's run: from its first steady sample until 100 ms of steady
// samples in another (a jolt neither starts nor ends one, and a hand
// wobbling on the 45 degree line keeps it going).
struct PoseRun {
  int pose = 0, cand = -1;
  uint32_t at = 0, candAt = 0;
  void sample(int p, uint32_t ms) {
    if (p < 0) return;
    if (p == pose) return void(cand = -1);
    if (p != cand) cand = p, candAt = ms;
    if (ms - candAt >= 100) pose = cand, at = candAt, cand = -1;
  }
  // When pose p began: the run in force, or one just starting.
  bool since(int p, uint32_t& t) const {
    if (pose == p) return t = at, true;
    if (cand == p) return t = candAt, true;
    return false;
  }
};

struct Lat {
  std::vector<uint32_t> v;
  void add(uint32_t ms) { v.push_back(ms); }
  uint32_t pct(int p) const {
    if (v.empty()) return 0;
    std::vector<uint32_t> s = v;
    std::sort(s.begin(), s.end());
    return s[std::min<size_t>(s.size() - 1, s.size() * size_t(p) / 100)];
  }
  std::string str() const {
    if (v.empty()) return "-";
    char b[48];
    std::snprintf(b, sizeof b, "%u/%u/%u (n=%zu)", pct(50), pct(95), pct(100), v.size());
    return b;
  }
};

struct Result {
  int kk = 0, singles = 0, holds = 0, hangs = 0, shakes = 0;
  std::string poses;   // each pose the shell held, and for how long: S1.42 a side 1.42 s, H.. upside down
  int heard = 0, oneMore = 0, rings = 0, charges = 0;
  Lat kkAck, kkWord, sideAck, sideWord, hangAck, hangWord;
};

Result play(const Segment& seg) {
  ::power::Policy policy;
  hw::Feel shell(kR0, kHome, policy);
  Detectors d;
  SenseOut out;
  Result r;
  bool heardWas = false, waitWas = false, ringWas = false, chargeWas = false;
  uint32_t heardAt = 0;
  bool kkOpen = false;
  PoseRun run;
  int heldPose = 0;
  uint32_t heldAt = 0;
  uint32_t t = 0, sideRun = UINT32_MAX, hangRun = UINT32_MAX;   // the run each was last acknowledged in: once a run
  auto feed = [&](uint32_t ms, const BodySample& b) {
    const hw::Felt f = shell.sample(b, ms, false);
    out.clear();
    d.sample(f.screen, ms, out);
    run.sample(rawPose(f.screen), ms);
    const int held = f.screen.sideDown ? 1 : f.screen.invertedDown ? 2 : 0;
    if (held != heldPose) {
      if (heldPose) {
        char b[24];
        std::snprintf(b, sizeof b, " %c%.2f", heldPose == 1 ? 'S' : 'H', (ms - heldAt) / 1000.0);
        r.poses += b;
      }
      heldPose = held, heldAt = ms;
    }
    // Knock knock: heard on the second knock, decided when the burst is over.
    if (f.screen.knockKnock && !heardWas) ++r.heard, heardAt = ms, kkOpen = true, r.kkAck.add(0);
    heardWas = f.screen.knockKnock;
    if (f.screen.knockWaiting && !waitWas) ++r.oneMore;
    waitWas = f.screen.knockWaiting;
    if (f.body.tapCode == 2 && kkOpen) r.kkWord.add(ms - heardAt), kkOpen = false;
    // The side hold's ring and the hang's charge, from the raw pose's start.
    const bool ring = d.side.holding();
    if (ring && !ringWas) {
      ++r.rings;
      if (run.since(1, t) && t != sideRun) r.sideAck.add(ms - t), sideRun = t;
    }
    ringWas = ring;
    bool charge = chargeWas;
    for (uint8_t i = 0; i < out.lociCount; ++i)
      if (out.loci[i].locus == locus::hanging) charge = out.loci[i].value > Fx::zero();
    if (charge && !chargeWas) {
      ++r.charges;
      if (run.since(2, t) && t != hangRun) r.hangAck.add(ms - t), hangRun = t;
    }
    chargeWas = charge;
    for (uint8_t i = 0; i < out.stimCount; ++i) {
      const StimId s = out.stimuli[i];
      if (s == stim::double_knock) ++r.kk;
      if (s == stim::knock) ++r.singles;
      if (s == stim::shake) ++r.shakes;
      if (s == stim::button_hold) {
        ++r.holds;
        if (run.since(1, t)) r.sideWord.add(ms - t);
      }
      if (s == stim::hang) {
        ++r.hangs;
        if (run.since(2, t)) r.hangWord.add(ms - t);
      }
    }
  };
  uint32_t last = 0;
  for (const auto& [ms, b] : seg.samples) feed(ms, b), last = ms;
  // Still for two seconds after: whatever was under way is decided.
  BodySample still = seg.samples.back().second;
  still.hasPeak = false;
  for (uint32_t ms = last + 20; ms <= last + 2000; ms += 20) feed(ms, still);
  return r;
}

// What each segment's hands meant, for the false fires: -1 any.
struct Want { const char* prefix; int holds, hangs; };
constexpr Want kWant[] = {
    {"1_", 0, 0},  {"2b_", 0, 0}, {"3_", 0, 0}, {"4_", 4, 0}, {"5_", 4, 0}, {"6_", -1, 0},
    {"7_", 0, 2},  {"8_", 0, -1}, {"9_", 1, 0}, {"L", 0, 0},  {"T", 0, 0},
};

const Want* want(const std::string& label) {
  for (const Want& w : kWant)
    if (label.rfind(w.prefix, 0) == 0) return &w;
  return nullptr;
}

}  // namespace

TEST(Controls, EveryRecordingAcknowledgedFastAndNothingFiresThatTheHandsDidNotMean) {
  const std::vector<Segment> segs = loadHands();
  ASSERT_GE(segs.size(), 15u);
  Result all;
  auto merge = [](Lat& to, const Lat& from) { to.v.insert(to.v.end(), from.v.begin(), from.v.end()); };
  std::printf("[ controls ] %-26s %3s %3s %5s %5s %6s | %5s %5s %5s %7s\n", "segment", "kk", "k", "holds", "hangs",
              "shakes", "heard", "1more", "rings", "charges");
  for (const Segment& s : segs) {
    const Result r = play(s);
    std::printf("[ controls ] %-26s %3d %3d %5d %5d %6d | %5d %5d %5d %7d\n", s.label.c_str(), r.kk, r.singles, r.holds,
                r.hangs, r.shakes, r.heard, r.oneMore, r.rings, r.charges);
    if (std::getenv("CONTROLS_POSES")) std::printf("[ poses ] %-26s%s\n", s.label.c_str(), r.poses.c_str());
    merge(all.kkAck, r.kkAck), merge(all.kkWord, r.kkWord), merge(all.sideAck, r.sideAck);
    merge(all.sideWord, r.sideWord), merge(all.hangAck, r.hangAck), merge(all.hangWord, r.hangWord);
    const Want* w = want(s.label);
    ASSERT_NE(w, nullptr) << s.label;
    if (w->holds >= 0) EXPECT_EQ(r.holds, w->holds) << s.label;
    if (w->hangs >= 0) EXPECT_EQ(r.hangs, w->hangs) << s.label;
  }
  // Sample time, ms, p50/p95/max: from the decisive moment to what the next
  // frame can show (ack), and to the word the engine gets (word).
  std::printf("[ latency ] knock knock  ack %-22s word %s\n", all.kkAck.str().c_str(), all.kkWord.str().c_str());
  std::printf("[ latency ] side hold    ack %-22s word %s\n", all.sideAck.str().c_str(), all.sideWord.str().c_str());
  std::printf("[ latency ] hang         ack %-22s word %s\n", all.hangAck.str().c_str(), all.hangWord.str().c_str());
}

// ---- the badge's harness (src/latency.h), driven by hand ---------------------------

TEST(Latency, AKnockKnockIsTimedFromItsSecondKnockToThePanel) {
  using namespace latency;
  Recorder r;
  Watch w;
  Sample s;
  auto at = [&](uint32_t us) { s.readUs = s.peakUs = us, s.seenUs = us + 3000; };
  at(1000000), s.knockWaiting = true;
  w.sample(s, r);   // the first knock
  EXPECT_TRUE(r.isOpen(Ctl::Knock));
  at(1300000), s.knockWaiting = false;
  w.sample(s, r);   // the second knock: "one more" goes out
  at(1380000), s.knockKnock = true;
  w.sample(s, r);   // rung out: KnockSense says knock knock, timed from the second knock
  EXPECT_FALSE(r.isOpen(Ctl::Knock)) << "the single knock was its first";
  EXPECT_TRUE(r.isOpen(Ctl::KnockKnock));
  r.framed(1400000, 7, flag(Ctl::KnockKnock));   // the rim's flash
  r.panelDone(7, 1405000);
  at(1920000), s.knockKnock = false, s.knockKnockWord = true, s.press = true;
  w.sample(s, r);   // reported once the burst is over
  r.framed(1930000, 8, 0);   // before the tick: nothing to show yet
  r.ticked(1990000);
  r.framed(2000000, 0, 0);   // a frame that sent nothing shows nothing
  r.framed(2010000, 9, 0);
  r.panelDone(8, 2012000);
  EXPECT_TRUE(r.isOpen(Ctl::KnockKnock)) << "frame 9 is not out yet";
  r.panelDone(9, 2015000);
  ASSERT_EQ(r.count(Ctl::KnockKnock), 1u);
  EXPECT_EQ(r.lastStage(Ctl::KnockKnock, St::Seen), 30);   // 0.1 ms
  EXPECT_EQ(r.lastStage(Ctl::KnockKnock, St::Detect), 830);
  EXPECT_EQ(r.lastStage(Ctl::KnockKnock, St::Ack), 1000);
  EXPECT_EQ(r.lastStage(Ctl::KnockKnock, St::AckPanel), 1050);
  EXPECT_EQ(r.lastStage(Ctl::KnockKnock, St::Word), 6230);
  EXPECT_EQ(r.lastStage(Ctl::KnockKnock, St::Tick), 6900);
  EXPECT_EQ(r.lastStage(Ctl::KnockKnock, St::Frame), 7100);
  EXPECT_EQ(r.lastStage(Ctl::KnockKnock, St::Panel), 7150);
  char line[400];
  ASSERT_TRUE(r.line(uint8_t(Ctl::KnockKnock), line, sizeof line));
  EXPECT_STREQ(line, "knock_knock n=1 words=1 silent=0 noack=0 seen=3.0/3.0/3.0 detect=83.0/83.0/83.0 ack=100.0/100.0/100.0 "
                     "ack_panel=105.0/105.0/105.0 word=623.0/623.0/623.0 tick=690.0/690.0/690.0 "
                     "frame=710.0/710.0/710.0 panel=715.0/715.0/715.0");
}

TEST(Latency, ASideHoldIsTimedFromTheTurnAndATurnLetGoSoonerIsNoWord) {
  using namespace latency;
  Recorder r;
  Watch w;
  Sample s;
  for (uint32_t ms = 0; ms < 3000; ms += 20) {
    s.readUs = s.peakUs = ms * 1000, s.seenUs = ms * 1000 + 2000;
    s.rawPose = ms >= 1000 && ms < 1400 ? 1 : 0;    // a short turn: 0.4 s
    s.sideHolding = ms >= 1040 && ms < 1550;
    w.sample(s, r);
  }
  ASSERT_EQ(r.count(Ctl::Side), 1u);
  EXPECT_EQ(r.lastStage(Ctl::Side, St::Detect), 420) << "from the turn's first sample to the level";
  EXPECT_EQ(r.lastStage(Ctl::Side, St::Word), Recorder::kNone);
  char line[400];
  ASSERT_TRUE(r.line(uint8_t(Ctl::Side), line, sizeof line));
  EXPECT_NE(std::string(line).find("words=0 silent=1"), std::string::npos) << line << ": felt, never shown";
}

TEST(Latency, ARawAimIsTheDuelsSectors) {
  using latency::rawAim;
  for (const AimTilt& t : kAimTilt) EXPECT_EQ(rawAim(t.ax, t.ay, t.az), t.aim);
  EXPECT_EQ(rawAim(0, 0, 1000), -1) << "level";
  EXPECT_EQ(rawAim(500, 0, 2000), -1) << "a jolt says nothing";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
