#pragma once
// Between ticks: the frames the screen draws while the pet's next 100 ms tick
// has not come. The board draws every 20 ms, so without this he moves in 10 Hz
// steps. A frame between ticks is a pure function of the tick before, the tick
// now and how far the clock is past it:
//   frame = draw(between(lookAtTickBefore, lookAtTickNow, tickAtMs, nowMs))
// It is the screen's alone: the engine, its ticks, its randomness, its saves
// and every replay hash never see any of it.
//
// Two kinds of channel, drawn two ways:
//  - Clocks (poseTick, exprTicks, a reflex's phase, a thought's phase, a duel
//    act's ticks) go up by a known step every tick, so they run on past the tick
//    now (extrapolated) and are exact: the next tick lands where the frame
//    before it already was. No lag, so a hop or a flinch starts the frame the
//    tick that started it is drawn.
//  - State that can change course any tick (the pellets, the glow, the night,
//    where he stands unless he walks) is drawn one tick behind, eased from the
//    tick before to the tick now: exact, never a guess that snaps back, 100 ms
//    late. What a kid's hands drive runs ahead instead, so it never lags them:
//    a walk or a flee (a tilt) at the step he just took, and the hang's ring
//    while it fills. A guess a tick turns out wrong is corrected at that tick:
//    a stop by at most the step (about 1.5 px at a walk), a turn by what it
//    turned, and never off the glass.
//  - Discrete things (which face, a blink, asleep, the pose) are the tick
//    now's, at once. The face crossfade runs on the exprTicks clock.
//
// Every field present() leaves alone draws exactly the tick's own frame, so
// the frame goldens and scripts/test/paint_bench.sh hold the renderer to the
// bytes it drew before this existed.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "blorb/appearance.h"
#include "blorb/ids.h"
#include "blorb/registry.h"
#include "paint/duel.h"

namespace paint {

// How fast the screen runs against the pet's life. At 1.2 every animation of
// him (his breath, blink and yawn, the idle layer, the hop and the flinch, the
// face crossfade, the halo's pulse, the eggs' rock, the feeding show, a duel
// act's staging and its gauges) plays a fifth faster than the ticks it is drawn from. His life (the 100 ms tick,
// hunger, sleep, aging, where he walks and how fast, every outcome) is untouched.
// Not scaled, on purpose: a thought's crawl (a six-year-old reads it), the
// rim's own rings and marquees, and the duel's clocks a kid reads (the turn
// timer, the mercy clock, a hold filling).
constexpr float kScreenSpeed = 1.2f;
constexpr uint32_t kScreenSpeedQ8 = uint32_t(kScreenSpeed * 256.0f + 0.5f);   // 307
static_assert(kScreenSpeedQ8 >= 128 && kScreenSpeedQ8 <= 1024, "the screen between half and four times the pet's pace");

// How far through a tick (0..256) the clock is `sinceMs` past it.
constexpr uint16_t tickShare(uint32_t sinceMs) {
  return sinceMs >= blorb::kTickMs ? 256 : uint16_t(sinceMs * 256 / blorb::kTickMs);
}

namespace tween {

// A tick clock `share` of a tick past `ticks`, at the screen's speed, as whole
// ticks and 1/256s. Wraps as the uint16_t it came from does, unless `sat`.
inline void clock(uint16_t ticks, uint16_t share, uint16_t& whole, uint16_t& sub, bool sat = false) {
  const uint32_t q = uint32_t(ticks) * kScreenSpeedQ8 + uint32_t(share) * kScreenSpeedQ8 / 256;
  whole = sat && (q >> 8) > 0xFFFFu ? uint16_t(0xFFFF) : uint16_t(q >> 8);
  sub = uint16_t(q & 255);
}

// `to` eased `behind`/256 of the way back toward `from`.
inline blorb::Fx back(blorb::Fx to, blorb::Fx from, uint16_t behind) {
  return blorb::Fx{int32_t(int64_t(to.raw) + (int64_t(from.raw) - to.raw) * behind / 256)};
}

}  // namespace tween

// A pet with nothing to move between ticks: asleep (his breath is slow and
// deep enough at the ticks' own 10 Hz), sealed, the vigil, an egg not yet
// rocking. His frames between ticks are the tick's own, so a frame that is
// the same bytes as the last one can be skipped (the loop's frame memo).
inline bool stillBetweenTicks(const blorb::Appearance& a) {
  using Kind = blorb::Appearance::Kind;
  if (a.sealed || a.kind == Kind::Remains) return true;
  if (a.kind == Kind::Egg) return a.wobble == blorb::Fx::zero() && a.eggProgress < blorb::Fx::ratio(2, 3);
  return a.kind == Kind::Creature && a.asleep && !a.reflexActive && !a.thinking;
}

// The frame `nowMs` is, the tick that drew `now` having been due at `tickMs`
// and `was` being the tick before it (pass `now` again when there is none: a
// first frame, a warp, a slow frame that ran two ticks). The wall clock the
// idle layer runs on is `nowMs`. Every field of the result is a function of
// those alone, so two frames with nothing between them to move compare equal.
inline blorb::Appearance between(const blorb::Appearance& was, const blorb::Appearance& now, uint32_t tickMs,
                                 uint32_t nowMs) {
  using blorb::Fx;
  blorb::Appearance a = now;
  const bool still = stillBetweenTicks(now);
  if (still) nowMs = tickMs;   // the tick's own frame until the next
  const uint16_t share = tickShare(nowMs - tickMs);
  a.lively = true;
  a.liveMs = uint32_t(uint64_t(nowMs) * kScreenSpeedQ8 >> 8);
  // The clocks, run on past the tick now.
  tween::clock(now.poseTick, share, a.poseTick, a.poseSub);
  tween::clock(now.exprTicks, share, a.exprTicks, a.exprSub, true);   // saturates: a wrap would crossfade again
  if (now.reflexActive) {
    int64_t step = 0;
    for (const blorb::ReflexInfo& r : blorb::REFLEXES)
      if (r.id == now.reflex && r.ticks) step = Fx::one().raw / r.ticks;
    const int64_t phase = int64_t(now.reflexPhase.raw) + step * share / 256;
    a.reflexPhase = blorb::clamp01(Fx::sat(phase * kScreenSpeedQ8 / 256));
  }
  // Feeding's show (feeding.cpp) runs on ages in wall ms, so it is smooth
  // already; at the screen's speed, the toss, the chomp and the meter too.
  auto fast = [](uint16_t ms) {
    return ms == blorb::FeedView::kNever ? ms : uint16_t(std::min<uint32_t>(blorb::FeedView::kNever - 1, uint32_t(ms) * kScreenSpeedQ8 >> 8));
  };
  a.feed.heardMs = fast(now.feed.heardMs);
  a.feed.droppedMs = fast(now.feed.droppedMs);
  a.feed.refusedMs = fast(now.feed.refusedMs);
  a.feed.chompMs = fast(now.feed.chompMs);
  a.feed.meterMs = fast(now.feed.meterMs);
  // The egg picker's show (clutch.cpp) is on ages in wall ms as well: its
  // slide, the aimed egg's rock, the hints and the chosen moment at the
  // screen's speed. A knock's ring is a hold filling, which a kid reads
  // against his own knock: it keeps the wall's pace.
  a.clutch.aimMs = fast(now.clutch.aimMs);
  a.clutch.chosenMs = fast(now.clutch.chosenMs);
  a.clutch.shownMs = uint16_t(uint32_t(now.clutch.shownMs) * kScreenSpeedQ8 / 256 % 60000);
  // The same line a tick on: it crawls at the step it took last tick (a reading pace, not sped up).
  if (now.thinking && was.thinking && was.thought == now.thought && now.thoughtPhase > was.thoughtPhase &&
      std::memcmp(was.line, now.line, sizeof now.line) == 0) {
    const Fx step = now.thoughtPhase - was.thoughtPhase;
    a.thoughtPhase = blorb::fxMin(Fx::one(), Fx{int32_t(now.thoughtPhase.raw + int64_t(step.raw) * share / 256)});
  }
  // The hang's ring is a kid's hands made visible, so it never lags them:
  // filling, it runs on ahead at the step it took last tick (it can only
  // overshoot by a step toward full, where fight mode opens on the next tick
  // anyway); emptying, it eases behind like the rest below.
  const bool filling = now.hangCharge > was.hangCharge && was.kind == now.kind;
  if (filling)
    a.hangCharge = blorb::fxMin(Fx::one(), now.hangCharge + Fx{int32_t(int64_t((now.hangCharge - was.hangCharge).raw) * share / 256)});
  // Where he stands. Walking (a tilt walks him, fleeing too), he runs on ahead
  // at the step he just took, so his feet answer a kid's hands with no lag; a
  // stop or a turn at the next tick moves him by what the guess missed (a stop:
  // at most the step, about 1.5 px at a walk). The tick he stops he is where
  // he stopped, at once. Whatever else moves him is eased a tick behind.
  auto walking = [](const blorb::Appearance& x) { return x.pose == blorb::pose::walk || x.pose == blorb::pose::flee; };
  const bool moved = now.kind == blorb::Appearance::Kind::Creature && was.kind == now.kind && !now.sealed &&
                     (was.at.x != now.at.x || was.at.y != now.at.y);
  if (moved && walking(now)) a.from = was.at, a.ahead = share;
  // What can change course, one tick behind.
  const uint16_t behind = uint16_t(256 - share);
  if (!behind || was.kind != now.kind || was.sealed != now.sealed) return a;
  if (moved && !walking(now) && !walking(was)) a.from = was.at, a.behind = behind;
  a.glow = tween::back(now.glow, was.glow, behind);
  a.intensity = tween::back(now.intensity, was.intensity, behind);
  if (!filling) a.hangCharge = tween::back(now.hangCharge, was.hangCharge, behind);
  a.night = tween::back(now.night, was.night, behind);
  a.wobble = tween::back(now.wobble, was.wobble, behind);
  a.eggProgress = tween::back(now.eggProgress, was.eggProgress, behind);
  a.remainsFade = tween::back(now.remainsFade, was.remainsFade, behind);
  a.hintUrgency = tween::back(now.hintUrgency, was.hintUrgency, behind);
  // A pellet sliding on a tilt: the same pellets in the same order, each eased.
  bool samePellets = was.itemCount == now.itemCount;
  for (uint8_t i = 0; samePellets && i < now.itemCount && i < 8; ++i) samePellets = was.items[i].what == now.items[i].what;
  for (uint8_t i = 0; samePellets && i < now.itemCount && i < 8; ++i) {
    a.items[i].at.x = tween::back(now.items[i].at.x, was.items[i].at.x, behind);
    a.items[i].at.y = tween::back(now.items[i].at.y, was.items[i].at.y, behind);
  }
  return a;
}

// The duel's act, `share` of a tick past its tick, at the screen's speed: every
// motion of the staging is a function of the act's ticks and their 1/256s.
inline void between(DuelScene& sc, uint16_t share) {
  tween::clock(sc.ticks, share > 256 ? 256 : share, sc.ticks, sc.sub, true);
}

}  // namespace paint
