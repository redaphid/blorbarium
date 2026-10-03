#pragma once
// The learning scenarios. Each names its stimulus and its predicted effect,
// written before it was first run, then the timeline and the metrics that
// test the prediction through what the owner can see.
//
// Every arm gets the same care: a pellet at 11:00, 13:30, 16:00, 18:30 and
// 21:00, the hours he is awake (Harness.BaselineDay: asleep about 23:00 to
// 10:00). A pellet dropped while he sleeps rots in 90 minutes and he eats it
// on waking, so a breakfast feed kills a grungo in two days.
#include <cmath>
#include <vector>
#include "e2e/check.h"

namespace e2e {
namespace sc {

using blorb::action::curl;
using blorb::action::eat;
using blorb::action::chase;
using blorb::action::rest;
using blorb::action::sleep;

inline void care(std::vector<Row>& rows, Who who, int firstDay, int lastDay) {
  for (uint64_t at : {11 * kH, 13 * kH + 30 * kM, 16 * kH, 18 * kH + 30 * kM, 21 * kH})
    rows.push_back(daily(who, firstDay, lastDay, at, press()));
}

// ---- reading a window -------------------------------------------------------------

constexpr double kNaN = NAN;
constexpr size_t kTicksPerS = 10;

// Share of ticks in [fromS, toS) of the window for which `on` holds.
template <class F>
double share(const Window& w, double fromS, double toS, F on) {
  size_t a = size_t(fromS * kTicksPerS), b = std::min(w.ticks.size(), size_t(toS * kTicksPerS)), n = 0, hit = 0;
  for (size_t i = a; i < b; ++i, ++n) hit += on(w.ticks[i]) ? 1 : 0;
  return n ? double(hit) / double(n) : kNaN;
}

// Seconds from `fromS` to the first tick for which `on` holds; capped at the window's end.
template <class F>
double secondsUntil(const Window& w, double fromS, F on) {
  for (size_t i = size_t(fromS * kTicksPerS); i < w.ticks.size(); ++i)
    if (on(w.ticks[i])) return double(i) / kTicksPerS - fromS;
  return double(w.ticks.size()) / kTicksPerS - fromS;
}

// Mean of `f` over windows [first, last], skipping NaNs; NaN if none measured.
template <class F>
double meanOver(const RunLog& log, size_t first, size_t last, F f) {
  double s = 0;
  int n = 0;
  for (size_t i = first; i <= last && i < log.windows.size(); ++i) {
    double v = f(log.windows[i]);
    if (!std::isnan(v)) s += v, ++n;
  }
  return n ? s / n : kNaN;
}

inline bool alive(const Tick& t) { return t.phase == Phase::Creature; }

// ---- 1. frequent shaking ------------------------------------------------------------
// Probe: one shake at 30 s into each 60 s window. Avoidance is how much more
// he curls in the 20 s after it than in the 30 s before it.
constexpr size_t kShakeProbes = 8;
constexpr double kProbeAtS = 30;

inline void shakeProbes(std::vector<Row>& rows, int day) {
  for (uint64_t h : {12, 13, 14, 15, 17, 18, 19, 20}) {
    const uint64_t at = uint64_t(day) * kD + h * kH + 15 * kM;
    rows.push_back(once(Who::Both, at - 30 * kS, watch(60 * kS)));
    rows.push_back(once(Who::Both, at, shake()));
  }
}

inline double curlRise(const Window& w) {
  if (w.ticks.size() <= size_t(kProbeAtS * kTicksPerS) || !alive(w.ticks[size_t(kProbeAtS * kTicksPerS)])) return kNaN;
  auto curling = [](const Tick& t) { return t.is(curl); };
  double after = share(w, kProbeAtS, kProbeAtS + 20, curling), before = share(w, 0, kProbeAtS, curling);
  return std::isnan(after) || std::isnan(before) ? kNaN : after - before;
}
inline double hopped(const Window& w) {
  if (w.ticks.size() <= size_t(kProbeAtS * kTicksPerS) || !alive(w.ticks[size_t(kProbeAtS * kTicksPerS)])) return kNaN;
  return share(w, kProbeAtS, kProbeAtS + 3, [](const Tick& t) { return t.hop; }) > 0 ? 1 : 0; }
inline double fearPeak(const Window& w) {
  int16_t peak = 0;
  for (size_t i = size_t(kProbeAtS * kTicksPerS); i < w.ticks.size(); ++i) peak = std::max(peak, w.ticks[i].drive[4]);
  return w.ticks.empty() ? kNaN : peak;
}

inline Scenario shaking() {
  Scenario s{"shaking",
             "shaken for 1.2 s about every 30 min while awake (11:00-22:00), days 1-3",
             "he learns that curling after a shake lowers fear, so after a probe shake on day 4 he curls more than the "
             "control; the hop is a reflex of adrenaline and should not change",
             {}, 5 * kD, {}};
  care(s.rows, Who::Both, 0, 4);
  s.rows.push_back(repeat(Who::Treatment, 1 * kD + 11 * kH, 1 * kD + 22 * kH, 30 * kM, shake(), 20 * kM));
  s.rows.push_back(repeat(Who::Treatment, 2 * kD + 11 * kH, 2 * kD + 22 * kH, 30 * kM, shake(), 20 * kM));
  s.rows.push_back(repeat(Who::Treatment, 3 * kD + 11 * kH, 3 * kD + 22 * kH, 30 * kM, shake(), 20 * kM));
  shakeProbes(s.rows, 4);
  s.metrics = {
      {"curl share rise after a probe shake", +1, [](const RunLog& l) { return meanOver(l, 0, kShakeProbes - 1, curlRise); }},
      {"share of probe shakes that make him hop", +1, [](const RunLog& l) { return meanOver(l, 0, kShakeProbes - 1, hopped); }},
      {"fear peak after a probe shake (permille)", +1, [](const RunLog& l) { return meanOver(l, 0, kShakeProbes - 1, fearPeak); }},
      {"curl share over day 4, 11:00-22:00", +1, [](const RunLog& l) { return l.share(curl, 4, 4, 11, 22); }},
  };
  s.clip = Clip{0, 25 * kS, 80, 300};
  s.knownFailing = true;
  return s;
}

// ---- 2. feeding at the same hour ----------------------------------------------------
// Both arms are fed four times a day on days 1-4; the treatment always at
// 11, 14, 17 and 20, the control at seeded random times between 10:30 and
// 21:30. On day 5 both are fed at 11 and 14 and watched from 16:30 to 17:30
// with the 17:00 pellet dropped in both.
inline Scenario feedingHour() {
  Scenario s{"feeding_hour",
             "fed at the same four hours every day (11, 14, 17, 20) instead of at random hours, days 1-4",
             "he anticipates the 17:00 meal: in the half hour before it he tries to eat more often than the control, "
             "and he reaches the 17:00 pellet sooner",
             {}, 5 * kD + 18 * kH, {}};
  care(s.rows, Who::Both, 0, 0);
  for (uint64_t h : {11, 14, 17, 20}) s.rows.push_back(daily(Who::Treatment, 1, 4, h * kH, press()));
  for (int i = 0; i < 4; ++i) s.rows.push_back(daily(Who::Control, 1, 4, 10 * kH + 30 * kM, press(), 11 * kH));
  s.rows.push_back(once(Who::Both, 5 * kD + 11 * kH, press()));
  s.rows.push_back(once(Who::Both, 5 * kD + 14 * kH, press()));
  s.rows.push_back(once(Who::Both, 5 * kD + 16 * kH + 30 * kM, watch(60 * kM)));
  s.rows.push_back(once(Who::Both, 5 * kD + 17 * kH, press()));
  s.metrics = {
      {"eat share, 16:30-17:00 with no pellet down (per mille)", +1, [](const RunLog& l) {
         if (l.windows.empty()) return kNaN;
         return 1000 * share(l.windows[0], 0, 30 * 60, [](const Tick& t) { return t.is(eat) && t.pellets == 0; });
       }},
      {"seconds from the 17:00 pellet to his bite", -1, [](const RunLog& l) {
         if (l.windows.empty()) return kNaN;
         return secondsUntil(l.windows[0], 30 * 60, [](const Tick& t) { return t.mouth != blorb::Mouthful::Nothing; });
       }},
  };
  s.clip = Clip{0, 29 * kM, 400, 300};
  s.knownFailing = true;
  return s;
}

// ---- 3. marble play when bored --------------------------------------------------------
// He wakes bored (boredom about 0.6 at 10:00). The treatment's owner rolls
// the marble at him for two minutes after he wakes, days 1-3, by tipping the
// dish left and right. The probe on day 4 is one short roll in both arms.
inline void roll(std::vector<Row>& rows, Who who, uint64_t at, uint64_t forMs) {
  rows.push_back(repeat(who, at, at + forMs, 6 * kS, tilt(350, 0, 3 * kS)));
  rows.push_back(repeat(who, at + 3 * kS, at + forMs, 6 * kS, tilt(-350, 0, 3 * kS)));
}

inline Scenario marble() {
  Scenario s{"marble_play",
             "the marble rolled at him for 2 min after he wakes bored (10:30), days 1-3",
             "chasing the marble relieves his morning boredom, so when it is rolled on day 4 he chases it more than "
             "the control",
             {}, 4 * kD + 12 * kH, {}};
  care(s.rows, Who::Both, 0, 4);
  for (int d = 1; d <= 3; ++d) roll(s.rows, Who::Treatment, d * kD + 10 * kH + 30 * kM, 2 * kM);
  s.rows.push_back(once(Who::Both, 4 * kD + 10 * kH + 29 * kM, watch(4 * kM)));
  roll(s.rows, Who::Both, 4 * kD + 10 * kH + 30 * kM, 12 * kS);
  s.metrics = {
      {"chase share in the 3 min after the day-4 roll", +1, [](const RunLog& l) {
         return l.windows.empty() ? kNaN : share(l.windows[0], 60, 240, [](const Tick& t) { return t.is(chase); });
       }},
      {"chase share over day 4 until noon", +1, [](const RunLog& l) { return l.share(chase, 4, 4, 10, 12); }},
  };
  s.clip = Clip{0, 55 * kS, 400, 300};
  s.knownFailing = true;
  return s;
}

// ---- 4. rotten pellets ----------------------------------------------------------------
// Both arms find a pellet in the dish when they wake, days 1-3: the
// treatment's dropped at 23:30 (rotten by morning), the control's at 09:55
// (fresh). Day 4 both wake to a fresh one, day 5 both to a rotten one, so the
// fresh probe comes before the control has ever bitten rot.
inline double minutesAwakeBeforeBite(const RunLog& l, size_t w, blorb::Mouthful what) {
  if (w >= l.windows.size()) return kNaN;
  const Window& win = l.windows[w];
  double woke = secondsUntil(win, 0, [](const Tick& t) { return alive(t) && !t.asleep; });
  if (woke >= double(win.ticks.size()) / kTicksPerS) return kNaN;   // never woke in the window
  return secondsUntil(win, woke, [what](const Tick& t) { return t.mouth == what; }) / 60;
}

inline Scenario rotten() {
  Scenario s{"rotten_pellets",
             "the pellet waiting for him at waking is rotten (dropped at 23:30) instead of fresh (09:55), days 1-3",
             "he learns that rotten food makes him ill, so on day 5 he waits longer before biting a rotten pellet, "
             "while a fresh pellet on day 4 he takes as quickly as the control",
             {}, 5 * kD + 11 * kH, {}};
  care(s.rows, Who::Both, 0, 4);
  for (int d = 0; d <= 2; ++d) s.rows.push_back(once(Who::Treatment, d * kD + 23 * kH + 30 * kM, press()));
  for (int d = 1; d <= 3; ++d) s.rows.push_back(once(Who::Control, d * kD + 9 * kH + 55 * kM, press()));
  s.rows.push_back(once(Who::Both, 4 * kD + 9 * kH + 55 * kM, press()));
  s.rows.push_back(once(Who::Both, 4 * kD + 9 * kH, watch(2 * kH - kS)));
  s.rows.push_back(once(Who::Both, 4 * kD + 23 * kH + 30 * kM, press()));
  s.rows.push_back(once(Who::Both, 5 * kD + 9 * kH, watch(2 * kH - kS)));
  s.metrics = {
      {"minutes awake before biting the rotten pellet (day 5)", +1,
       [](const RunLog& l) { return minutesAwakeBeforeBite(l, 1, blorb::Mouthful::RottenPellet); }},
      {"minutes awake before biting a fresh pellet (day 4)", 0,
       [](const RunLog& l) { return minutesAwakeBeforeBite(l, 0, blorb::Mouthful::Pellet); }},
  };
  s.clip = Clip{1, 60 * kM, 3 * kS, 300};
  s.knownFailing = true;
  return s;
}

// ---- 5. comfort by holding ----------------------------------------------------------
// Probe: held for 20 s at 30 s into a 60 s window. Settling is how much more
// he rests while held than in the 30 s before.
inline double restRise(const Window& w) {
  auto resting = [](const Tick& t) { return t.is(rest); };
  double during = share(w, kProbeAtS + 3, kProbeAtS + 20, resting), before = share(w, 0, kProbeAtS, resting);
  return std::isnan(during) || std::isnan(before) ? kNaN : during - before;
}

inline Scenario holding() {
  Scenario s{"comfort_holding",
             "held still in a palm for 30 s four times a day (12:00, 15:00, 18:00, 21:30), days 1-3",
             "being held relieves his need for touch, so when held on day 4 he settles (rests) more than the control",
             {}, 5 * kD, {}};
  care(s.rows, Who::Both, 0, 4);
  for (uint64_t at : {12 * kH, 15 * kH, 18 * kH, 21 * kH + 30 * kM}) s.rows.push_back(daily(Who::Treatment, 1, 3, at, cradle(30 * kS)));
  for (uint64_t h : {12, 14, 17, 20}) {
    const uint64_t at = 4 * kD + h * kH + 15 * kM;
    s.rows.push_back(once(Who::Both, at - 30 * kS, watch(60 * kS)));
    s.rows.push_back(once(Who::Both, at, cradle(20 * kS)));
  }
  s.metrics = {
      {"rest share rise while held", +1, [](const RunLog& l) { return meanOver(l, 0, 3, restRise); }},
      {"need_touch just before a probe hold (permille)", -1, [](const RunLog& l) {
         return meanOver(l, 0, 3, [](const Window& w) { return w.ticks.size() > 290 ? double(w.ticks[290].drive[7]) : kNaN; });
       }},
  };
  s.clip = Clip{0, 25 * kS, 80, 300};
  s.knownFailing = true;
  return s;
}

// ---- 6. face-down nights ------------------------------------------------------------
// The treatment's owner lays him face down at 19:00 and rights him at 09:00,
// days 1-4. Days 5 and 6 nobody does. When does he fall asleep on day 5?
inline double sleepOnsetHour(const RunLog& l, int day) {
  for (int h = 17; h < 24; ++h)
    if (l.creatureTicks[size_t(day)][size_t(h)] && l.asleepTicks[size_t(day)][size_t(h)] * 2 > l.creatureTicks[size_t(day)][size_t(h)])
      return h;
  return 24;
}

inline std::vector<Metric> sleepOnset() {
  return {
      {"first evening hour mostly asleep, day 5", -1, [](const RunLog& l) { return sleepOnsetHour(l, 5); }},
      {"share asleep 19:00-23:00, day 5", +1, [](const RunLog& l) {
         uint64_t a = 0, n = 0;
         for (int h = 19; h < 23; ++h) a += l.asleepTicks[5][size_t(h)], n += l.creatureTicks[5][size_t(h)];
         return n ? double(a) / double(n) : kNaN;
       }},
  };
}

inline Scenario faceDown() {
  Scenario s{"face_down_nights",
             "laid face down from 19:00 to 09:00, days 1-4",
             "a long dark stretch entrains his night to the owner's, so on day 5 with no lid he falls asleep earlier "
             "in the evening than the control",
             {}, 6 * kD, {}};
  care(s.rows, Who::Both, 0, 5);
  s.rows.push_back(daily(Who::Treatment, 1, 4, 19 * kH, lid(14 * kH)));
  s.metrics = sleepOnset();
  s.rows.push_back(once(Who::Both, 5 * kD + 21 * kH, watch(3 * kH - kS)));
  s.clip = Clip{0, 90 * kM, 18 * kS, 300};
  s.knownFailing = true;
  return s;
}

// The same owner, but righting him at 23:00: dark only in his pet evening.
inline Scenario faceDownEvenings() {
  Scenario s{"face_down_evenings",
             "laid face down from 19:00 to 23:00, days 1-4",
             "dark in his evening pulls his night earlier, so on day 5 he falls asleep earlier than the control",
             {}, 6 * kD, {}};
  care(s.rows, Who::Both, 0, 5);
  s.rows.push_back(daily(Who::Treatment, 1, 4, 19 * kH, lid(4 * kH)));
  s.metrics = sleepOnset();
  s.rows.push_back(once(Who::Both, 5 * kD + 21 * kH, watch(3 * kH - kS)));
  s.clip = Clip{0, 90 * kM, 18 * kS, 300};
  return s;
}

// ---- 7. neglect ---------------------------------------------------------------------
// The treatment's owner forgets him for a working day: no pellet at 11:00,
// 13:30 or 16:00 on day 1. Care is the same before and after.
inline double meanBiteLatency(const RunLog& l) {
  return meanOver(l, 0, l.windows.size() - 1, [](const Window& w) {
    return secondsUntil(w, 1, [](const Tick& t) { return t.mouth != blorb::Mouthful::Nothing; });
  });
}

inline Scenario neglect() {
  Scenario s{"neglect",
             "no pellet at 11:00, 13:30 or 16:00 on day 1 (a day away)",
             "a hungry day teaches him how much eating relieves hunger, so on days 2-3 he goes for a dropped pellet "
             "sooner than the control",
             {}, 4 * kD, {}};
  care(s.rows, Who::Both, 0, 0);
  care(s.rows, Who::Both, 2, 3);
  for (uint64_t at : {11 * kH, 13 * kH + 30 * kM, 16 * kH}) s.rows.push_back(daily(Who::Control, 1, 1, at, press()));
  for (uint64_t at : {18 * kH + 30 * kM, 21 * kH}) s.rows.push_back(daily(Who::Both, 1, 1, at, press()));
  for (int d = 2; d <= 3; ++d)
    for (uint64_t at : {11 * kH, 13 * kH + 30 * kM, 16 * kH, 18 * kH + 30 * kM, 21 * kH})
      s.rows.push_back(once(Who::Both, d * kD + at - kS, watch(10 * kM)));
  s.metrics = {
      {"seconds from a dropped pellet to his bite, days 2-3", -1, meanBiteLatency},
      {"hunger when the pellet drops, days 2-3 (permille)", 0, [](const RunLog& l) {
         return meanOver(l, 0, l.windows.size() - 1, [](const Window& w) { return w.ticks.empty() ? kNaN : double(w.ticks[0].drive[0]); });
       }},
      {"share asleep 00:00-07:00 the night after (day 2)", 0, [](const RunLog& l) {
         uint64_t a = 0, n = 0;
         for (int h = 0; h < 7; ++h) a += l.asleepTicks[2][size_t(h)], n += l.creatureTicks[2][size_t(h)];
         return n ? double(a) / double(n) : kNaN;
       }},
      {"injury at 11:00 on day 2 (permille)", +1, [](const RunLog& l) {
         return l.windows.empty() || l.windows[0].ticks.empty() ? kNaN : double(l.windows[0].ticks[0].injury);
       }},
  };
  s.clip = Clip{0, 0, 400, 300};
  // The bite came 50 s sooner only because the hungry arm stayed awake that
  // night and skipped a flat 0.14 of forgetting the control suffered. With
  // forgetting proportional both arms keep the eat lesson and bite alike.
  s.knownFailing = true;
  return s;
}

// ---- 9. punished for chasing ---------------------------------------------------------
// An owner who tells him off: whenever they see him start chasing the marble
// (11:00-22:00, days 1-3) they give the dish a shake. Day 4 nobody does.
inline Scenario punishedChasing() {
  Scenario s{"shaken_for_chasing",
             "shaken each time he is seen starting to chase the marble (11:00-22:00), days 1-3",
             "being shaken right after he starts chasing teaches him chasing is frightening, so he chases less while "
             "it goes on (day 3) and still less than the control on day 4 when it has stopped",
             {}, 5 * kD, {}};
  care(s.rows, Who::Both, 0, 4);
  for (int d = 1; d <= 3; ++d)
    s.reactions.push_back(Reaction{Who::Treatment, d * kD + 11 * kH, d * kD + 22 * kH, chase, shake(), 10 * kS});
  s.rows.push_back(once(Who::Both, 4 * kD + 12 * kH, watch(20 * kM)));
  s.rows.push_back(once(Who::Both, 3 * kD + 12 * kH, watch(20 * kM)));
  s.metrics = {
      {"chase share on day 3, 11:00-22:00 (while shaken for it)", -1, [](const RunLog& l) { return l.share(chase, 3, 3, 11, 22); }},
      {"chase share on day 1, 11:00-22:00 (first day of it)", -1, [](const RunLog& l) { return l.share(chase, 1, 1, 11, 22); }},
      {"chase share on day 4, 11:00-22:00 (no shaking)", -1, [](const RunLog& l) { return l.share(chase, 4, 4, 11, 22); }},
  };
  s.clip = Clip{0, 0, 4 * kS, 300};   // windows are numbered in time order: day 3 is window 0, day 4 window 1
  return s;
}

// The same owner and days, asked the other question: once the shaking stops,
// does the lesson last the night?
inline Scenario punishedChasingNextDay() {
  Scenario s = punishedChasing();
  s.name = "shaken_for_chasing_next_day";
  s.prediction = "the lesson outlasts the shaking: on day 4, after a night with no shaking, he still chases less "
                 "than the control";
  std::swap(s.metrics[0], s.metrics[2]);
  s.clip = Clip{1, 0, 4 * kS, 300};
  s.knownFailing = true;
  return s;
}

// ---- 8. heirloom transfer -----------------------------------------------------------
// The parent is shaken as in `shaking` (or not), then the dish is unplugged
// from 23:30 on day 4 for nine days. He dies in the gap of old age; the
// owner plugs in at 11:00 on day 14 and holds BOOT to pick the first egg,
// which hatches about 30 minutes later. The hatchling is probed with four
// shakes before he has learned anything of his own.
inline Scenario heirloom() {
  Scenario s{"heirloom",
             "the parent was shaken about every 30 min while awake on days 1-3",
             "the parent's learned answer to shaking (curl lowers fear) is born into the hatchling as an heirloom "
             "instinct, so the shaken parent's hatchling curls after a shake more than the control parent's",
             {}, 14 * kD + 13 * kH, {}};
  care(s.rows, Who::Both, 0, 4);
  for (int d = 1; d <= 3; ++d)
    s.rows.push_back(repeat(Who::Treatment, d * kD + 11 * kH, d * kD + 22 * kH, 30 * kM, shake(), 20 * kM));
  s.rows.push_back(once(Who::Both, 4 * kD + 23 * kH + 30 * kM, unplug(9 * kD + 11 * kH + 30 * kM)));
  s.rows.push_back(once(Who::Both, 14 * kD + 11 * kH + 1 * kM, press(1500)));
  for (uint64_t m : {45, 55, 65, 75}) {
    const uint64_t at = 14 * kD + 11 * kH + m * kM;
    s.rows.push_back(once(Who::Both, at - 30 * kS, watch(60 * kS)));
    s.rows.push_back(once(Who::Both, at, shake()));
  }
  s.metrics = {
      {"hatchling's curl share rise after a probe shake", +1, [](const RunLog& l) { return meanOver(l, 0, 3, curlRise); }},
      {"hatchling's curl share rise after the first probe shake", +1, [](const RunLog& l) { return meanOver(l, 0, 0, curlRise); }},
      {"parent died in the gap (1 = yes)", 0, [](const RunLog& l) { return double(!l.deaths.empty()); }},
  };
  s.clip = Clip{0, 25 * kS, 80, 300};
  s.knownFailing = true;
  return s;
}

inline std::vector<Scenario> all() {
  return {shaking(), punishedChasing(), punishedChasingNextDay(), feedingHour(), marble(), rotten(),
          holding(), faceDown(), faceDownEvenings(), neglect(), heirloom()};
}

}  // namespace sc
}  // namespace e2e
