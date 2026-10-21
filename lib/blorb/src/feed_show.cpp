#include "blorb/feed_show.h"

namespace blorb {

void FeedShow::sample(const BodySample& s, bool pressed, uint32_t ms) {
  // The second knock: the rim flashes now, not when the burst is decided.
  if (s.knockKnock && !knockKnockWas_) heard_ = {ms, true};
  knockKnockWas_ = s.knockKnock;
  // A press, BOOT's or a knock knock's decided: heard, unless its knocks were.
  if (pressed && (!heard_.seen || ms - heard_.ms > kHeardAgainMs)) heard_ = {ms, true};
}

void FeedShow::pressed(uint32_t ms, bool dropped, uint32_t tick, bool asleep, Fx hunger) {
  if (dropped) dropped_ = {ms, true}, dropTick_ = tick;
  why_ = !dropped ? FeedRefusal::Empty : asleep ? FeedRefusal::Asleep : hunger < kFull ? FeedRefusal::Full : FeedRefusal::None;
  refused_ = {ms, why_ != FeedRefusal::None};
}

void FeedShow::ate(uint32_t ms, bool rotten, Fx hunger) {
  chomp_ = {ms, true};
  rotten_ = rotten;
  fullnessFrom_ = Fx::one() - clamp01(hunger);
}

uint16_t FeedShow::age(const Event& e, uint32_t nowMs) {
  if (!e.seen) return FeedView::kNever;
  const int32_t d = int32_t(nowMs - e.ms);
  if (d < 0) return 0;   // a sample's time may run a little ahead of the frame's
  return d >= int32_t(kShowMs) ? FeedView::kNever : uint16_t(d);
}

FeedView FeedShow::view(uint32_t nowMs) const {
  FeedView v;
  v.heardMs = age(heard_, nowMs);
  v.droppedMs = age(dropped_, nowMs);
  v.refusedMs = age(refused_, nowMs);
  if (v.refusedMs != FeedView::kNever) v.refused = why_;
  v.chompMs = age(chomp_, nowMs);
  v.rotten = rotten_;
  v.fullnessFrom = fullnessFrom_;
  for (uint16_t a : {v.droppedMs, v.refusedMs, v.chompMs})   // not on the knocks alone: food first
    if (a < v.meterMs) v.meterMs = a;
  return v;
}

bool FeedShow::dropTick(uint32_t nowMs, uint32_t& tick) const {
  if (age(dropped_, nowMs) == FeedView::kNever) return false;
  tick = dropTick_;
  return true;
}

}  // namespace blorb
