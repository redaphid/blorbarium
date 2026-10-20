#include "blorb/gesture_cues.h"

#include <algorithm>

#include "blorb/controls.h"

namespace blorb {

void GestureCues::sample(const BodySample& s, bool sideHolding, uint32_t nowMs) {
  if (s.buttonDown && !buttonDown_) {
    gapMs_ = released_ ? nowMs - tapReleaseMs_ : UINT32_MAX;
    buttonSinceMs_ = nowMs;
    tapSecond_ = tapArmed_ && nowMs - tapReleaseMs_ <= kDoublePressMs;
  } else if (!s.buttonDown && buttonDown_) {
    pressMs_ = nowMs - buttonSinceMs_;
    tapArmed_ = pressMs_ < kButtonHoldMs && !tapSecond_;
    tapReleaseMs_ = nowMs;
    released_ = true;
  }
  buttonDown_ = s.buttonDown;
  const bool hold = buttonDown_ || (kSideHoldHolds && sideHolding);
  if (hold && !holdDown_) holdSinceMs_ = nowMs;
  holdDown_ = hold;
  knockWaiting_ = kKnockKnockPresses && s.knockWaiting;
  lastMs_ = nowMs;
}

void GestureCues::dress(BootHint& h, const BootMeaning& m, Fx hangCharge) const {
  // The hold: the ring fills toward the row's hold. Where the hold means
  // nothing, a grey ring says the grip was felt. Where a hold is the press
  // itself (the update's yes, a plea), holding just presses: no ring.
  const uint32_t held = heldMs();
  if (h.hasHold && held > 0) h.holdFill = uint8_t(std::min<uint32_t>(255, held * 255 / m.holdMs));
  else if (m.hold == BootAct::Nothing && held >= kVoidRingAfterMs) h.holdVoid = true, h.holdFill = 128;

  // The double: a first BOOT press lights the bars, fading as its window
  // closes; a hang on its way up lights them as far as it has got.
  if (h.hasDouble) {
    if (tapArmed_ && !buttonDown_ && lastMs_ - tapReleaseMs_ <= kDoublePressMs)
      h.doubleFill = uint8_t(std::max<uint32_t>(1, 255 - (lastMs_ - tapReleaseMs_) * 255 / kDoublePressMs));
    else if (hangCharge > Fx::zero())
      h.doubleFill = uint8_t(std::max<int64_t>(1, int64_t(clamp01(hangCharge).raw) * 255 / Fx::one().raw));
  }

  // The press: one knock is in and the second can still come.
  h.knockWait = knockWaiting_ && m.press != BootAct::Nothing;
}

}  // namespace blorb
