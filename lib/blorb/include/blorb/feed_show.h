#pragma once
// Feeding, as the eyes see it. The owner (2026-10-21): "The 'feeding'
// mechanism on double knock either doesn't work, or its effects are not
// obvious." The engine's part stays exactly as it was: a press (or its twin,
// the knock knock, controls.h) drops a pellet from the pantry, and he eats it
// if he wants it. This is what the screen shows of that, so a six-year-old
// sees every step and never a silent nothing:
//
//   heard    the knock knock landed: on the second knock (KnockSense::
//            knockKnock), 0.3-0.5 s before it is decided, or at once for a
//            BOOT press, the rim's pellet glyph pops and a cookie springs out;
//   dropped  a pellet left the pantry: that cookie is tossed down to where
//            it lands in the dish, and the pantry's empty slot shows where it
//            came from;
//   refused  the press fed nobody now, and why: the pantry is empty (a
//            cookie jar knocked over and empty, and the pantry's empty slots
//            shaking, the next one filling), or he says ZZZ... (the pellet
//            waits for him) or I'M FULL! in a bubble;
//   chomp    he bit a pellet: crumbs and hearts (a rotten one: yuck), and his
//            belly meter rises.
//
// And where food comes from (#57, food.h), so a kid knows how to get more:
//   pantry   the bar on the rim (#18), its next pellet's sliver; while he is
//            hungry with none left it breathes warm and the time until the
//            next one is written under it, m:ss (waiting);
//   found    a snack he dug up sparkles where it lands;
//   topped   coming home after a long time away: the part it filled glows;
//   spoils   a battle won: a cookie flies in over the rim to the pantry; lost:
//            one flies out of it and away (the friend's screen shows it land).
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
  uint16_t meterMs = kNever;        // since the last drop, refusal or bite: the belly meter shows a while after
  Fx fullness{};                    // his belly now: 1 - hunger
  Fx fullnessFrom{};                // and just before the bite, for the meter to rise from
  uint8_t pantryMax = 0;            // the pantry's slots (the metabolism gene)
  Fx refill{};                      // how far the next pellet is to coming back, 0..1
  uint16_t refillSecs = 0;          // seconds until it is back (rounded up); 0 while the pantry is full
  bool waiting = false;             // hungry, awake, the pantry empty and nothing he would eat in the dish
  uint16_t foundMs = kNever;        // since he turned up a snack of his own (foraging)
  int8_t found = -1;                // that snack's index in Appearance::items, while it is news
  uint16_t toppedMs = kNever;       // since coming home topped the pantry up...
  uint8_t topped = 0;               // ...by this many: the newest slots
  uint16_t spoilsMs = kNever;       // since a battle's spoils were shown...
  int8_t spoils = 0;                // ...won (> 0: into the pantry from the rim) or lost (< 0: out of it)
  uint8_t emptyTries = 0;           // knock knocks that found the pantry empty, this boot: the sad thought's variant
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
  // A hard shake dismisses a refusal's words (#35). True when one was shown.
  bool dismiss(uint32_t nowMs) {
    if (age(refused_, nowMs) == FeedView::kNever) return false;
    refused_.seen = false;
    return true;
  }
  // On the tick he bit a pellet. `hunger`: before the bite.
  void ate(uint32_t ms, bool rotten, Fx hunger);
  // On the tick a snack he dug up landed (its pellet's droppedTick is `tick`).
  void found(uint32_t ms, uint32_t tick);
  // On the tick coming home put `n` pellets in the pantry.
  void toppedUp(uint32_t ms, uint8_t n);
  // A battle's spoils, once the dish is on the screen: `n` won (> 0) or lost (< 0).
  void spoils(uint32_t ms, int8_t n);

  // The ages at `nowMs`; the Dish adds what the habitat and he say now
  // (flying, fullness, pantryMax, refill).
  FeedView view(uint32_t nowMs) const;
  // The tick the newest pellet dropped on, while it is still news.
  bool dropTick(uint32_t nowMs, uint32_t& tick) const;
  // The tick the newest snack he found landed on, while it is still news.
  bool foundTick(uint32_t nowMs, uint32_t& tick) const;

 private:
  struct Event { uint32_t ms = 0; bool seen = false; };
  static uint16_t age(const Event& e, uint32_t nowMs);
  Event heard_, dropped_, refused_, chomp_, found_, topped_, spoils_;
  FeedRefusal why_ = FeedRefusal::None;
  bool rotten_ = false, knockKnockWas_ = false;
  uint8_t emptyTries_ = 0;
  uint32_t dropTick_ = 0, foundTick_ = 0;
  uint8_t toppedN_ = 0;
  int8_t spoilsN_ = 0;
  Fx fullnessFrom_{};
};

}  // namespace blorb
