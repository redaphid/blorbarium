#pragma once
// Body traces, as the IMU reports them (milli-g, +z out of the screen).
// runFor works with anything shaped like the Dish: sample(BodySample, ms)
// and tick(ms, Link&).
#include <cstdint>
#include "blorb/senses.h"

namespace blorbtest {

inline blorb::BodySample still() {
  blorb::BodySample s;
  s.az = 1000;
  return s;
}

// One half of a shake: alternate up and down every 80 ms, 4 or more in 900 ms.
inline blorb::BodySample jolt(bool up) {
  blorb::BodySample s;
  s.az = up ? 2600 : -600;
  return s;
}

inline blorb::BodySample pressed() {
  blorb::BodySample s = still();
  s.buttonDown = true;
  return s;
}

// Held still in a hand, tipped: picked up after 400 ms, a cradle after 3 s.
inline blorb::BodySample cradled() {
  blorb::BodySample s;
  s.ax = 400;
  s.az = 917;
  return s;
}

// Face down on the table: lidded after 2 s.
inline blorb::BodySample faceDown() {
  blorb::BodySample s;
  s.az = -1000;
  return s;
}

// Whole ticks with the dish at rest, sampled at 50 Hz like the firmware.
template <class Dish, class Link>
void runFor(Dish& dish, Link& link, uint32_t& ms, uint32_t forMs) {
  for (uint32_t end = ms + forMs; ms < end; ms += blorb::kSampleMs) {
    dish.sample(still(), ms);
    dish.tick(ms, link);
  }
}

}  // namespace blorbtest
