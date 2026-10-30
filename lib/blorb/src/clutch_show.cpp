#include "blorb/clutch_show.h"

#include <algorithm>

namespace blorb {

void ClutchShow::sample(const BodySample& s, int8_t aim, bool picking, uint32_t ms) {
  if (picking && !picking_) shown(ms);
  picking_ = picking;
  if (!picking) {
    aim_ = aimWas_ = knockEgg_ = -1;
    aimSeen_ = knocking_ = knockSeen_ = false;
    return;
  }
  if (aim != aim_) {
    aimWas_ = aim_;
    aim_ = aim;
    aimMs_ = ms;
    aimSeen_ = true;
    if (aimChanges_ < 255) ++aimChanges_;
  }
  // The knuckle lands: the first knock (waiting for a second that may never
  // come) or a knock knock heard. Either keeps the aimed egg once decided.
  const bool knock = s.knockWaiting || s.knockKnock;
  if (knock && !knocking_ && aim_ >= 0) {
    knockMs_ = ms;
    knockEgg_ = aim_;
    knockSeen_ = true;
  }
  if (!knock && knocking_) knockEndMs_ = ms;
  knocking_ = knock;
  // A knock that kept nothing (tilted away before it was decided, or the
  // knocks ran on into hammering): its ring opens again.
  if (knockSeen_ && (aim_ != knockEgg_ || (!knocking_ && int32_t(ms - knockEndMs_) > int32_t(kKnockGraceMs))))
    knockSeen_ = false;
}

void ClutchShow::shown(uint32_t ms) {
  shownMs_ = ms;
  kept_ = false;
  aimChanges_ = 0;
}

void ClutchShow::kept(const Clutch& k, uint8_t i, uint32_t ms) {
  kept_ = true;
  keptMs_ = ms;
  chosen_ = i;
  count_ = std::min<uint8_t>(k.previewed, kMaxClutch);
  std::copy(k.previews, k.previews + count_, eggs_);
  picking_ = false;
  aim_ = aimWas_ = knockEgg_ = -1;
  knocking_ = knockSeen_ = false;
}

uint16_t ClutchShow::age(uint32_t since, uint32_t nowMs, uint32_t limit) {
  const int32_t d = int32_t(nowMs - since);
  if (d < 0) return 0;   // a sample's time may run a little ahead of the frame's
  return uint32_t(d) >= limit ? ClutchView::kNever : uint16_t(d);
}

ClutchView ClutchShow::view(uint32_t nowMs, EggPreview (&eggs)[kMaxClutch], uint8_t& eggCount) const {
  ClutchView v;
  if (kept_) {
    v.chosenMs = age(keptMs_, nowMs, kChosenMs);
    if (v.chosenMs != ClutchView::kNever) {
      v.chosen = int8_t(chosen_);
      v.count = count_;
      std::copy(eggs_, eggs_ + count_, eggs);
      eggCount = count_;
    }
    return v;
  }
  if (!picking_) return v;
  v.picking = true;
  v.aim = aim_;
  v.aimWas = aimWas_;
  v.aimChanges = aimChanges_;
  if (aimSeen_) v.aimMs = age(aimMs_, nowMs, 60000);
  if (knockSeen_) v.knockMs = age(knockMs_, nowMs, 60000);
  // The hints' clock: it moves every frame, so a still picker is still drawn
  // (its egg rocks and its hint plays).
  v.shownMs = uint16_t(uint32_t(std::max<int32_t>(0, int32_t(nowMs - shownMs_))) % 60000);
  return v;
}

}  // namespace blorb
