#pragma once
// Choosing an egg, as the eyes see it. The owner (2026-10-21): "It also was
// difficult for me to understand how to pick an egg", and then: "Use the
// same mechanism as we do in fights: tilt the badge ... reacting more
// quickly, with more frames, making the delay between user gesture and
// action as low as possible."
//
// The engine's part is pickEgg (creature.h): the tilt picker's sector at
// each tick moves the cursor, a knock (or a knock knock, or a hold) keeps the
// egg tilted toward. This is what the screen shows of it, sooner than the
// engine's 100 ms tick can:
//
//   aim      the egg the newest sample's tilt points at (the same rule,
//            tilt_aim.h, on the 50 Hz sample rather than the tick), and when
//            it last changed, so the picker slides to it at once and every
//            frame of the slide is drawn;
//   knock    a knock landed while an egg is aimed at: the first knock's jolt
//            (KnockSense::waitingSecond, carried on BodySample::knockWaiting),
//            about 0.6 s before a lone knock is decided, so the ring round it
//            starts closing as the knuckle lands;
//   kept     the egg was kept: which, and when. The clutch is gone by then
//            (its egg replaced it), so the eggs it showed are kept here for
//            the moment the chosen one is chosen and the others fade.
//
// Presentation only, like FeedShow and the Thinker: never saved, never
// hashed, and nothing here reaches the engine. Ages are in ms, so the
// drawing moves at any frame rate, and they change every frame while
// something moves, so the frame memo (main.cpp) draws each of those frames.
#include <cstdint>

#include "blorb/creature.h"
#include "blorb/senses.h"

namespace blorb {

struct ClutchView {
  static constexpr uint16_t kNever = UINT16_MAX;
  bool picking = false;          // the eggs are out (the vigil is over) and none is kept yet
  int8_t aim = -1;               // the egg the tilt points at now, or -1
  int8_t aimWas = -1;            // the egg it pointed at before, or -1: the slide is from it
  uint16_t aimMs = kNever;       // since the aim last changed (the slide)
  uint8_t aimChanges = 0;        // how often it has changed since the eggs came out (saturating): has he browsed yet?
  uint16_t knockMs = kNever;     // since a knock landed on the aimed egg, while it may still keep it
  uint16_t shownMs = 0;          // since the eggs came out: the hints' clock (it wraps)
  int8_t chosen = -1;            // the egg kept, while its moment shows
  uint16_t chosenMs = kNever;    // since it was kept
  uint8_t count = 0;             // the clutch's eggs, for that moment (Appearance::eggs holds them)
};

class ClutchShow {
 public:
  // How long the chosen moment shows (the egg screen draws it over the egg).
  static constexpr uint32_t kChosenMs = 2400;
  // A knock's ring stays this long after the knock's flags fall, for the
  // tick that keeps the egg to land; then, if none did, it opens again.
  static constexpr uint32_t kKnockGraceMs = 250;

  // Every sample. `aim`: the egg the newest tilt points at (eggAt of
  // tiltAim), -1 for none; `picking`: the eggs are out.
  void sample(const BodySample& s, int8_t aim, bool picking, uint32_t ms);
  // The eggs came out (the vigil ended): the hints' clock starts.
  void shown(uint32_t ms);
  // The clutch kept egg i: its eggs, for the chosen moment.
  void kept(const Clutch& k, uint8_t i, uint32_t ms);

  // The view at `nowMs`. The kept clutch's previews go to `eggs` while the
  // chosen moment shows.
  ClutchView view(uint32_t nowMs, EggPreview (&eggs)[kMaxClutch], uint8_t& eggCount) const;

 private:
  static uint16_t age(uint32_t since, uint32_t nowMs, uint32_t limit);
  int8_t aim_ = -1, aimWas_ = -1;
  uint32_t aimMs_ = 0, shownMs_ = 0, knockMs_ = 0, knockEndMs_ = 0, keptMs_ = 0;
  bool aimSeen_ = false, knocking_ = false, knockSeen_ = false, kept_ = false, picking_ = false;
  int8_t knockEgg_ = -1;
  uint8_t chosen_ = 0, count_ = 0, aimChanges_ = 0;
  EggPreview eggs_[kMaxClutch]{};
};

}  // namespace blorb
