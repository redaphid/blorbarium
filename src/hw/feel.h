// What the shell makes of each 50 Hz sample before the Dish feels it
// (main.cpp feel()): the knocks found in the raw samples (blorb/knocks.h), the
// power policy's input, the settled rotation and the poses, and the sample
// turned into the screen's frame at home. Pure: no Arduino, no clock, no
// board, so test_feeding replays a real knuckle through the same steps the
// badge takes, at either home (ORIENT_HOME 2, or 0 on badge128_flip).
//
// What it holds back from the Dish, and why:
//   - the press that wakes a dark badge, and the knock knock that does (its
//     twin, controls.h): they only light it, so nobody feeds him unseen. A
//     knock knock decided once the badge is already lit (a hand's first knock
//     jolts it awake) feeds him as usual: the kid is looking at him by then.
//   - a knock knock while a question owns BOOT (the update's "Turn me
//     around!", the debug unlock's): main.cpp gives that question the button,
//     and its twin goes with it. Before 2026-10-21 it went to the Dish, which
//     dropped a pellet behind the update's screen, where nobody saw it and it
//     rotted.
#pragma once

#include <cstdint>

#include "blorb/knocks.h"
#include "blorb/senses.h"
#include "orient.h"   // beside this file, so a test that includes it by path finds them too
#include "power.h"

namespace hw {

struct Felt {
  blorb::BodySample body;     // in the IMU's frame, with the knocks KnockSense found (STATUS, DEBUG trace)
  blorb::BodySample screen;   // what the Dish feels: the screen's frame at home, the poses and the knocks
  uint8_t rot = 0;            // the settled rotation (the update's turn reads it)
};

class Feel {
 public:
  Feel(int r0, uint8_t home, power::Policy& power) : up_(r0), home_(home), power_(power) {}

  // One sample, in the IMU's frame. `asking`: a question owns BOOT now.
  Felt sample(const blorb::BodySample& sensed, uint32_t ms, bool asking) {
    Felt f;
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
    f.screen.invertedDown = up_.inverted(felt, home_);   // upside down from home: the hang
    f.screen.sideDown = up_.sideways(felt, home_);       // the hold's twin (SideHoldDetector, controls.h)
    f.screen.knocking = knocks_.busy();                  // a knock's bounce is no shake
    // One knock landed: the rim's "one more"; a second landed: the rim's flash.
    f.screen.knockWaiting = !asking && knocks_.waitingSecond();
    f.screen.knockKnock = !asking && knocks_.knockKnock();
    return f;
  }

  const orient::Up& up() const { return up_; }

 private:
  blorb::KnockSense knocks_;
  orient::Up up_;
  uint8_t home_;
  power::Policy& power_;
  bool wakePress_ = false;
};

}  // namespace hw
