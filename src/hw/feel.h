// What the shell makes of each 50 Hz sample before the Dish feels it
// (main.cpp feel()): the knocks found in the raw samples (blorb/knocks.h), the
// power policy's input, the settled rotation, the poses (poses.h), and the sample
// turned into the screen's frame at home. Pure: no Arduino, no clock, no
// board, so test_feeding replays a real knuckle through the same steps the
// badge takes, at either home (ORIENT_HOME 2, or 0 on badge128_flip).
//
// What it holds back from the Dish, and why:
//   - the press that wakes a dark badge, and the knock knock that does (its
//     twin, controls.h): they only light it, so nobody feeds him unseen. A
//     knock knock decided once the badge is already lit (a hand's first knock
//     jolts it awake) feeds him as usual: the kid is looking at him by then.
//   - a knock knock while a question owns BOOT (the update's "Tip me on my
//     side!", Wi-Fi's, the debug unlock's): main.cpp gives that question the
//     button, and its twin goes with it. Before 2026-10-21 it went to the
//     Dish, which dropped a pellet behind the update's screen, where nobody
//     saw it and it rotted.
//   - the hang and the side hold while a question shows, for kAnswerTailMs
//     after it goes, and after that until the badge is let back: the turn
//     that answers it is the question's (owner, 2026-10-29: the Wi-Fi
//     question's turn also opened fight mode, a duel took the radio, and the
//     phone heard only "couldn't connect").
#pragma once

#include <cstdint>

#include "blorb/knocks.h"
#include "blorb/senses.h"
#include "orient.h"   // beside this file, so a test that includes it by path finds them too
#include "poses.h"
#include "power.h"

namespace hw {

struct Felt {
  blorb::BodySample body;     // in the IMU's frame, with the knocks KnockSense found (STATUS, DEBUG trace)
  blorb::BodySample screen;   // what the Dish feels: the screen's frame at home, the poses and the knocks
  uint8_t rot = 0;            // the settled rotation (the update's turn reads it)
  bool owned = false;         // a question, or the tail of its answer, owns the gestures (and BOOT)
};

class Feel {
 public:
  // After a question goes (answered, lapsed or shaken off), its gestures
  // stay its own this long: the tail of the turn that answered it.
  static constexpr uint32_t kAnswerTailMs = 2000;

  Feel(int r0, uint8_t home, power::Policy& power) : up_(r0), home_(home), power_(power) {}

  // One sample, in the IMU's frame. `asking`: a question owns BOOT now.
  Felt sample(const blorb::BodySample& sensed, uint32_t ms, bool askingNow) {
    if (askingNow) tailUntil_ = ms + kAnswerTailMs, tail_ = true;
    else if (tail_ && int32_t(ms - tailUntil_) >= 0) tail_ = false;
    const bool asking = askingNow || tail_;
    Felt f;
    f.owned = asking;
    f.body = sensed;
    f.body.tapCode = knocks_.sample(sensed, ms);
    // In the IMU's own frame: a turn of the panel is no move.
    const bool wasLow = power_.low();
    power_.sample(f.body, ms);
    const bool woke = wasLow && !power_.low();
    if (woke && f.body.buttonDown) wakePress_ = true;
    if (!f.body.buttonDown) wakePress_ = false;
    blorb::BodySample felt = f.body;
    if ((woke || asking) && felt.tapCode) felt.tapCode = 0;
    felt.buttonDown = felt.buttonDown && !wakePress_;
    f.rot = up_.sample(f.body, ms);
    // The screen no longer turns with the badge (owner, 2026-10-19): the
    // engine feels it in the one frame of home, so left is left, always.
    f.screen = up_.toFrame(felt, home_);
    // The poses from gravity itself, not the settled rotation's lag (poses.h).
    poses_.sample(f.screen);
    // A pose held through a question is the question's until the badge is let back.
    const bool posed = poses_.inverted() || poses_.sideways();
    posedForQuestion_ = posed && (asking || posedForQuestion_);
    f.screen.invertedDown = !posedForQuestion_ && poses_.inverted();   // upside down from home: the hang
    f.screen.sideDown = !posedForQuestion_ && poses_.sideways();       // the hold's twin (SideHoldDetector, controls.h)
    f.screen.knocking = knocks_.busy();                  // a knock's bounce is no shake
    f.screen.knockRinging = knocks_.ringing();           // ...nor an aim
    // One knock landed: the rim's "one more"; a second landed: the rim's flash.
    f.screen.knockWaiting = !asking && knocks_.waitingSecond();
    f.screen.knockKnock = !asking && knocks_.knockKnock();
    return f;
  }

  // Home can change at run time: set at boot from NVS (hw/home.h), and by `DEBUG home`.
  void setHome(uint8_t home) { home_ = home; }
  uint8_t home() const { return home_; }

  const orient::Up& up() const { return up_; }
  const Poses& poses() const { return poses_; }

 private:
  blorb::KnockSense knocks_;
  orient::Up up_;
  Poses poses_;
  uint8_t home_;
  power::Policy& power_;
  bool wakePress_ = false;
  bool tail_ = false, posedForQuestion_ = false;
  uint32_t tailUntil_ = 0;
};

}  // namespace hw
