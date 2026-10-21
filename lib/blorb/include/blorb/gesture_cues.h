#pragma once
// The gesture cues: what the kid's body is doing toward one of BOOT's three
// words right now, and how the rim's hint shows it. All the rules for that
// feedback live here and in gesture_cues.cpp. Nothing else needs to know them.
//
// Using it takes three calls:
//
//   GestureCues cues;
//   cues.sample(bodySample, sideHolding, nowMs);   // every sample, after the detectors
//   cues.holding();                                 // the hold as a level (the duel's 3 s holds)
//   cues.dress(hint, bootMeaning(ctx), hangCharge); // the progress drawn onto bootHint(ctx, view)
//
// What it shows (controls.h has the words and their twins):
//   press        knock knock  The first knock landed and the second can still
//                             come (KnockSense::waitingSecond, carried on
//                             BodySample::knockWaiting): the hint glows, "one
//                             more". It glows only where a press does
//                             something, and only while the window is open.
//                             It goes out when the knock knock fires.
//   hold         side hold    The ring fills toward the row's holdMs while BOOT
//                             is down or the badge is held on a side. Where the
//                             row's hold is Nothing, a grey ring shows that the
//                             grip was felt but does nothing here. A tap shows no
//                             ring (kVoidRingAfterMs).
//   double       the hang     A first BOOT press lights the double bars until
//                             its window closes, as a hang on its way up does
//                             (the hanging locus).
//
// Pure: no clock of its own, no I/O, so test_dish tests it natively.
#include <cstdint>

#include "blorb/boot.h"
#include "blorb/fixed.h"
#include "blorb/senses.h"

namespace blorb {

class GestureCues {
 public:
  // A hold that does nothing here draws its grey ring only after this long,
  // so a quick BOOT tap does not show it.
  static constexpr uint32_t kVoidRingAfterMs = 300;
  // The hold ring's fill on a hold's first sample: one of the eight steps
  // it is drawn in, so the ring answers the hand at once.
  static constexpr uint32_t kRingFirst = 255 / 8 + 1;

  // Every sample. sideHolding: SideHoldDetector::holding(). It counts only
  // when the side hold is enabled (controls.h kSideHoldHolds).
  void sample(const BodySample& s, bool sideHolding, uint32_t nowMs);

  // The hold as a level, whoever holds: BOOT down, or turned onto a side.
  bool holding() const { return holdDown_; }
  uint32_t heldMs() const { return holdDown_ ? lastMs_ - holdSinceMs_ : 0; }

  // BOOT's raw level, and its last press as the level drew it. These go to
  // the debug link: the press's length, and the gap since the press before it
  // (UINT32_MAX when there was none).
  bool buttonDown() const { return buttonDown_; }
  uint32_t lastPressMs() const { return pressMs_; }
  uint32_t lastGapMs() const { return gapMs_; }

  // Draws the progress onto a hint that bootHint() made from the same row:
  // holdFill and holdVoid, doubleFill, knockWait. hangCharge is
  // Appearance::hangCharge (0..1).
  void dress(BootHint& h, const BootMeaning& m, Fx hangCharge) const;

 private:
  uint32_t lastMs_ = 0;
  // The hold.
  bool holdDown_ = false;
  uint32_t holdSinceMs_ = 0;
  // BOOT, mirroring ButtonDetector's double window: a short press that could
  // still become a double.
  bool buttonDown_ = false, released_ = false, tapArmed_ = false, tapSecond_ = false;
  uint32_t buttonSinceMs_ = 0, tapReleaseMs_ = 0, pressMs_ = 0, gapMs_ = UINT32_MAX;
  // The knock knock's first knock, waiting for its second.
  bool knockWaiting_ = false;
};

}  // namespace blorb
