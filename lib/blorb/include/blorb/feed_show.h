#pragma once
// Feeding, as the eyes see it. The owner (2026-10-21): "The 'feeding'
// mechanism on double knock either doesn't work, or its effects are not
// obvious." The engine's part stays exactly as it was: a press (or its twin,
// the knock knock, controls.h) drops a pellet from the pantry, and he eats it
// if he wants it. This is what the screen shows of that, so a six-year-old
// sees every step and never a silent nothing:
//
//   heard    the knock knock landed: the rim flashes on the second knock
//            (KnockSense::knockKnock), 0.3-0.5 s before it is decided, or
//            at once for a BOOT press;
//   dropped  a pellet left the pantry: it flies from the rim's pellet glyph
//            down to where it lands in the dish, and the pantry's empty slot
//            shows where it came from;
//   refused  the press fed nobody now, and why: the pantry is empty (its
//            empty slots shake, the next one filling), he is asleep (zzz: the
//            pellet waits for him), or he is full (no thanks);
//   chomp    he bit a pellet: crumbs and hearts (a rotten one: yuck), and his
//            belly meter rises.
//
// Presentation only, like the Thinker: never saved, never hashed, and
// nothing here reaches him. The Dish reports each event with its time; the
// Appearance carries their ages (FeedView) and paint (feeding.cpp) draws them.
// Ages are in ms, so the drawing moves at any frame rate.
#include <cstdint>

#include "blorb/fixed.h"
#include "blorb/senses.h"

namespace blorb {

enum class FeedRefusal : uint8_t { None, Empty, Asleep, Full };

struct FeedView {
  static constexpr uint16_t kNever = UINT16_MAX;
  uint16_t heardMs = kNever;        // since a knock knock (or a press) was heard: the rim's flash
  uint16_t droppedMs = kNever;      // since the newest pellet left the pantry: its flight
  int8_t flying = -1;               // that pellet's index in Appearance::items, while it is the newest
  uint16_t refusedMs = kNever;      // since a press fed nobody...
  FeedRefusal refused = FeedRefusal::None;   // ...and why
  uint16_t chompMs = kNever;        // since he bit a pellet
  bool rotten = false;              // that bite was rotten: yuck
  uint16_t meterMs = kNever;        // since the last of the above: the belly meter shows a while after
  Fx fullness{};                    // his belly now: 1 - hunger
  Fx fullnessFrom{};                // and just before the bite, for the meter to rise from
  uint8_t pantryMax = 0;            // the pantry's slots (the metabolism gene)
  Fx refill{};                      // how far the next pellet is to coming back, 0..1
};

class FeedShow {
 public:
  // An event older than this is no longer drawn; the meter shows this long
  // after the last one.
  static constexpr uint32_t kShowMs = 4000;
  // A press for BOOT flashes the rim unless a knock knock flashed it this recently.
  static constexpr uint32_t kHeardAgainMs = 1500;
  // Hunger below this when a pellet drops: he is full and will not want it now.
  static constexpr Fx kFull = Fx::ratio(15, 100);

  // Every sample. `pressed`: the detectors fired stim::button on it.
  void sample(const BodySample& s, bool pressed, uint32_t ms);
  // On the tick a press reached a pet: whether a pellet dropped (the tick it
  // did is `tick`), and how he was when it did.
  void pressed(uint32_t ms, bool dropped, uint32_t tick, bool asleep, Fx hunger);
  // On the tick he bit a pellet. `hunger`: before the bite.
  void ate(uint32_t ms, bool rotten, Fx hunger);

  // The ages at `nowMs`; the Dish adds what the habitat and he say now
  // (flying, fullness, pantryMax, refill).
  FeedView view(uint32_t nowMs) const;
  // The tick the newest pellet dropped on, while it is still news.
  bool dropTick(uint32_t nowMs, uint32_t& tick) const;

 private:
  struct Event { uint32_t ms = 0; bool seen = false; };
  static uint16_t age(const Event& e, uint32_t nowMs);
  Event heard_, dropped_, refused_, chomp_;
  FeedRefusal why_ = FeedRefusal::None;
  bool rotten_ = false, knockKnockWas_ = false;
  uint32_t dropTick_ = 0;
  Fx fullnessFrom_{};
};

}  // namespace blorb
