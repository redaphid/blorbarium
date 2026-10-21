#pragma once
// Knocks from the raw 50 Hz samples, in place of the QMI8658's tap engine.
//
// Measured with real hands on the test board (2026-10-19, tools/imu_record.py,
// test/fixtures/hands_2026-10-19.txt): the tap engine missed every one of five
// deliberate knock knocks (it reported two singles) and reported about 34
// double taps while the badge was only picked up, turned, carried and
// pocketed, each of which dropped a pellet. The samples themselves tell the
// two apart plainly:
//   - a knock on the face jerks the axis through the screen: one sample to the
//     next, az moves 1,600 to 4,000 mg (often to the +-2 g rail), and rings for
//     about 100 ms; handling never moved it more than about 1,100 mg;
//   - a kid knocks on a badge held still: in the 400 ms before the first knock
//     the magnitude wobbles under 30 mg (handling: 130 and up) and the badge
//     still points the same way afterwards.
// So a knock is a sample that jumps far from where gravity sat (kDevMg) in one
// step (kJerkMg) -- at 50 Hz a knock's peak often falls between samples, and
// one on the rim moves the in-plane axes as much as az, so it is the whole
// vector, not az's jerk alone; the next kMergeMs is its ringing; knocks
// within kChainMs (moreMs once two are in) make one burst; the burst counts only if the badge was steady
// before it and points the same way after; and it is reported when it is over,
// as the tap engine's code: 1 a single knock, 2 a knock knock (two to four
// knocks: kids knock thrice). More is hammering, and counts as nothing. While a burst is under way (busy()), a knock's bounce in the hand
// is no shake (main.cpp sets BodySample::knocking). Pure, so it is tested
// natively against the recordings.
#include <cstdint>

#include "blorb/senses.h"

namespace blorb {

class KnockSense {
 public:
  static constexpr int32_t kDevMg = 950;         // that far from gravity...
  static constexpr int32_t kJerkMg = 1000;       // ...reached in one 20 ms step: a knock
  static constexpr int32_t kNextDevMg = 900;     // the second and third knock of a burst may be softer
  static constexpr uint32_t kMergeMs = 170;      // a knock rings this long: spikes within it are its bounce...
  static constexpr uint32_t kFastMs = 80;        // ...unless, this long after it, the badge had calmed
  static constexpr int32_t kCalmMg = 600;        //    (two samples this close to gravity)
  static constexpr int32_t kFastDevMg = 1500;    //    and the new spike is this hard: a quick second knock
  static constexpr uint8_t kMaxKnocks = 4;       // more in one burst is hammering
  static constexpr uint32_t kChainMs = 600;      // the next knock of a knock knock comes within this (hands: 0.18-0.46 s)
  // Once two knocks are in, a third comes in the kid's own rhythm: the burst
  // is over moreMs(gap) after its last knock, the gap between the first two
  // and some slack, not less than kMoreMinMs nor more than kChainMs. On the
  // recordings a fixed 350 changed nothing and 320 split a triple; 400 is the
  // floor with margin. A quick knock knock (0.25 s apart) feeds 0.2 s sooner
  // (docs/notes/controls-latency.md); a slow one waits as long as before.
  static constexpr uint32_t kMoreMinMs = 400;
  static constexpr uint32_t kMoreSlackMs = 150;
  static constexpr uint32_t moreMs(uint32_t gap) {
    return gap + kMoreSlackMs < kMoreMinMs ? kMoreMinMs : gap + kMoreSlackMs > kChainMs ? kChainMs : gap + kMoreSlackMs;
  }
  static constexpr int32_t kSteadySdMg = 110;    // before the first knock: the magnitude's spread...
  static constexpr int32_t kSteadyJerkMg = 300;  // ...and three in four samples' jerk under this
  static constexpr int32_t kDriftMg = 300;       // gravity before and after may differ this much (a rim knock shoves it)
  static constexpr uint32_t kSettleMs = 300;     // after the last knock, the ringing is over by then
  static constexpr uint8_t kHistory = 22;        // 440 ms of samples before a knock...
  static constexpr uint8_t kLead = 2;            // ...less the newest two, which may already be its rising edge

  // Every 50 Hz sample: 0, or on the sample a burst is decided, 1 or 2.
  uint8_t sample(const BodySample& s, uint32_t ms);
  // A burst from a steady badge is under way: its bounce is no shake.
  bool busy() const { return (inBurst_ && steady_) || restN_ > 0; }

  // One knock is in and a second could still make it a knock knock: a burst
  // of one that will count (steady before it, in a hand), or one blip on a
  // table. It turns true on the knock itself, well before sample() reports
  // anything (a burst is reported when it is over), and false once a second
  // knock lands or the window closes. The shell carries it on the sample
  // (BodySample::knockWaiting) and GestureCues shows it as "one more".
  bool waitingSecond() const {
    if (inBurst_ && steady_) return knocks_ == 1;
    return restN_ == 1;
  }

  // A second knock is in, in a burst that can still count, and it has rung
  // out: a knock knock is on its way, though sample() reports it only once
  // the burst is over (moreMs after its last knock). True from the first
  // two samples after the second knock with no spike (kHeardQuietMs) and
  // nothing knock-sized off gravity (kNextDevMg), until then; it stays true
  // through a third knock's ringing, and is false again if the burst runs on
  // into hammering. The shell carries it on the sample
  // (BodySample::knockKnock) and the rim flashes at once, so a kid sees his
  // knock knock heard as he makes it, not 0.6 s later: on the recorded hands
  // 40 ms after the second knock (p50; p95 140 ms, as a knock in a hand rings),
  // where waiting out 80 ms with no spike at all took 80/200 ms, and as long
  // as 580 ms when a bounce restarted the wait (docs/notes/controls-latency.md).
  // A shake from a still badge (the sim's: 1.5 g along x, flipping every 60
  // ms, each flip a "knock") never sits within kNextDevMg of gravity, so it
  // never flashes.
  static constexpr uint32_t kHeardQuietMs = 40;
  bool knockKnock() const {
    if (inBurst_ && steady_) return heard_;
    return restN_ >= 2 && restN_ <= kMaxKnocks;
  }

  // Lying on a table, a knock barely moves the badge: a 150-500 mg blip in one
  // or two 250 Hz reads (test board, T1 2026-10-19), where in a hand it is
  // 1-4 g. So when the badge was resting dead still (kRestSdMg), a much
  // smaller blip counts -- but only a short one with quiet around it: a desk
  // being typed on, a cup set down or a desk bumped rattles for 60 ms and more
  // (T2), and that is no knock (T3: 9 of 9 on a table, 0 from the desk). Only with the shell's 250 Hz peak.
  static constexpr int32_t kRestSdMg = 15;       // the magnitude's spread before: lying on something hard
  static constexpr int32_t kBlipMg = 150;        // a read this far from the last sample is part of a blip
  static constexpr int32_t kRestFirstMg = 150;   // the first knock's blip reaches this (kids knock softly first, then harder)...
  static constexpr int32_t kRestNextMg = 150;    // ...the next ones this
  static constexpr uint8_t kBlipMaxSamples = 5;  // a blip over 100 ms is a rattle (a hard knock on a table rings 80 ms)
  static constexpr uint32_t kRestQuietMs = 300;  // and the first comes after this long with none

 private:
  struct Past { int16_t ax, ay, az; int32_t mag, jerk; };
  Past hist_[kHistory]{};
  uint8_t histN_ = 0, histAt_ = 0;
  bool primed_ = false;
  int16_t px_ = 0, py_ = 0, pz_ = 0;
  // The burst under way.
  bool inBurst_ = false, steady_ = false, heard_ = false;
  uint8_t knocks_ = 0;
  uint32_t last_ = 0, onset_ = 0, gap_ = 0;   // gap_: from the first knock's onset to the second's
  int32_t dev1_ = 0, dev2_ = 0;                  // the last two samples' distance from gravity
  int32_t g0x_ = 0, g0y_ = 0, g0z_ = 0;          // gravity before it
  int32_t sumX_ = 0, sumY_ = 0, sumZ_ = 0, sumN_ = 0;   // after it settled

  // The table's bursts, judged blip by blip.
  struct Blip { uint32_t start = 0, end = 0; int32_t max = 0; uint8_t n = 0; bool steady = false; };
  Blip blip_{}, lastBlip_{};
  bool inBlip_ = false, hadBlip_ = false;
  uint8_t restN_ = 0;
  uint32_t restLast_ = 0, restGap_ = 0;
  int32_t restSd2_ = 0;
  void endBlip();
  uint8_t restSample(int32_t peakJerk, uint32_t ms);
  bool steadyBefore();
  int64_t spread2() const;
  void gravityBefore(int32_t& x, int32_t& y, int32_t& z) const;
  uint8_t decide();
};

}  // namespace blorb
