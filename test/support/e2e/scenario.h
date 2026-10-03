#pragma once
// A learning scenario is data: a timeline of what a hand, the world, the
// clock and a phone do to the dish, in wall milliseconds since the dish was
// founded (at wall midnight, so the pet day starts in step with the wall day).
// Each row says which arm it belongs to. CONTROL is the same table with the
// Treatment rows left out (and Control rows put in), so the two arms differ
// only by the stimulus. Jitter is drawn per seed and per row, never per arm,
// so a row both arms share lands at the same moment in both.
#include <algorithm>
#include <cstdint>
#include <vector>
#include "blorb/fixed.h"
#include "blorb/ids.h"

namespace e2e {

constexpr uint64_t kS = 1000, kM = 60 * kS, kH = 60 * kM, kD = 24 * kH;

enum class Arm : uint8_t { Control, Treatment };
enum class Who : uint8_t { Both, Treatment, Control };

enum class Do : uint8_t {
  Shake,        // swung along x for ms, as the sim's !shake
  Knock,        // the IMU's tap engine reports one tap
  DoubleKnock,
  Press,        // BOOT held for ms: under 1.2 s drops a pellet, longer is a hold
  Cradle,       // held still in a palm, tipped 30 degrees, for ms (the sim's !hold)
  Lid,          // face down on the table for ms
  Tilt,         // gravity of (x, y) milli-g in the screen plane for ms
  Phone,        // a phone connects for ms and sends `line`
  Unplug,       // power is cut for ms; the battery RTC keeps counting
  Watch,        // record every tick for ms; watch windows are numbered in time order
};

struct Gesture {
  Do what;
  uint64_t ms = 0;
  int16_t x = 0, y = 0;
  const char* line = nullptr;
};

inline Gesture shake(uint64_t ms = 1200) { return {Do::Shake, ms}; }
inline Gesture knock() { return {Do::Knock}; }
inline Gesture doubleKnock() { return {Do::DoubleKnock}; }
inline Gesture press(uint64_t ms = 200) { return {Do::Press, ms}; }
inline Gesture cradle(uint64_t ms) { return {Do::Cradle, ms}; }
inline Gesture lid(uint64_t ms) { return {Do::Lid, ms}; }
inline Gesture tilt(int16_t x, int16_t y, uint64_t ms) { return {Do::Tilt, ms, x, y}; }
inline Gesture phone(const char* line, uint64_t ms) { return {Do::Phone, ms, 0, 0, line}; }
inline Gesture unplug(uint64_t ms) { return {Do::Unplug, ms}; }
inline Gesture watch(uint64_t ms) { return {Do::Watch, ms}; }

// `g` at `at`, then every `every` until `until` (once when every is 0), each
// occurrence moved later by a seeded draw in [0, jitter).
struct Row {
  Who who;
  uint64_t at;
  Gesture g;
  uint64_t every = 0, until = 0, jitter = 0;
};

inline Row once(Who who, uint64_t at, Gesture g) { return {who, at, g}; }
inline Row repeat(Who who, uint64_t from, uint64_t until, uint64_t every, Gesture g, uint64_t jitter = 0) {
  return {who, from, g, every, until, jitter};
}
// At `hourOfDay` (in ms past midnight) on each day in [firstDay, lastDay].
inline Row daily(Who who, int firstDay, int lastDay, uint64_t hourOfDay, Gesture g, uint64_t jitter = 0) {
  return {who, uint64_t(firstDay) * kD + hourOfDay, g, kD, uint64_t(lastDay + 1) * kD, jitter};
}

// An owner who answers what they see: while [from, until), each time he is
// seen starting `when`, they make gesture `g`, at most once per `cooldown`.
struct Reaction {
  Who who;
  uint64_t from, until;
  blorb::ActionId when;
  Gesture g;
  uint64_t cooldown = 0;
  bool in(Arm arm) const { return who == Who::Both || (who == Who::Treatment) == (arm == Arm::Treatment); }
};

struct Event { uint64_t at; Gesture g; };

inline std::vector<Event> timeline(const std::vector<Row>& rows, Arm arm, uint32_t seed) {
  std::vector<Event> out;
  for (size_t i = 0; i < rows.size(); ++i) {
    const Row& r = rows[i];
    if (r.who == Who::Treatment && arm != Arm::Treatment) continue;
    if (r.who == Who::Control && arm != Arm::Control) continue;
    blorb::Rng rng = blorb::Rng::seeded(uint64_t(seed) << 20 ^ i);
    for (uint64_t t = r.at; !r.every || t < r.until; t += r.every) {
      out.push_back({t + (r.jitter ? rng.below(uint32_t(r.jitter)) : 0), r.g});
      if (!r.every) break;
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
  return out;
}

}  // namespace e2e
