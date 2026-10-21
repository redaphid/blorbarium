// How long each control takes to show, stage by stage, on the badge
// (`DEBUG latency`, scripts/test/latency_probe.py, docs/notes/controls-latency.md).
// Pure: no Arduino, no clock of its own; main.cpp hands every time in, in
// microseconds (micros(), so differences survive its wrap).
//
// One event per gesture, from its decisive moment (In: the raw IMU read, or
// BOOT's level, where the hand did the thing that counts) through:
//
//   seen      the loop took that sample off the sense task's queue (feel())
//   detect    the detector first acted on it: the second knock counted, the
//             side hold's level, the hang's charge, the aim off the dead zone
//   ack       the first frame drawn that acknowledges it before the engine
//             has (the rim's flash, "one more", the hold ring, the hang's
//             charge), and ack_panel: that frame's rows out on the panel
//   word      the stimulus controls.h names (press, hold, hang, ...) fired
//   tick      the engine's 100 ms tick that took it
//   frame     the first frame drawn after that tick that sent the panel
//             anything (a frame that changed nothing, or one skipped, shows
//             nothing), and panel: its rows out (the core-0 send task done)
//
// Each stage is kept as its time after In. A gesture that never reached a
// stage leaves it out of that stage's numbers. `silent` counts the ones that
// were detected or spoke and yet showed nothing at all (no ack, no frame).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace latency {

enum class Ctl : uint8_t { KnockKnock, Knock, Side, Hang, Press, Double, Hold, Shake, Tilt, Wake, Count };
enum class St : uint8_t { Seen, Detect, Ack, AckPanel, Word, Tick, Frame, Panel, Count };

constexpr uint8_t kCtls = uint8_t(Ctl::Count), kStages = uint8_t(St::Count);

inline const char* name(Ctl c) {
  constexpr const char* k[] = {"knock_knock", "knock", "side", "hang", "press", "double", "hold", "shake", "tilt", "wake"};
  return k[uint8_t(c)];
}
inline const char* name(St s) {
  constexpr const char* k[] = {"seen", "detect", "ack", "ack_panel", "word", "tick", "frame", "panel"};
  return k[uint8_t(s)];
}
constexpr uint32_t flag(Ctl c) { return uint32_t(1) << uint8_t(c); }

class Recorder {
 public:
  static constexpr uint8_t kKeep = 16;              // the last this many of each control, for p50/p95/max
  static constexpr uint32_t kGiveUpUs = 6000000;    // an event still open this long is closed as it stands
  static constexpr uint16_t kNone = UINT16_MAX;     // a stage never reached

  // A gesture's decisive moment, read at inUs and taken by the loop at
  // seenUs. One of a kind at a time: an open one is closed first.
  void begin(Ctl c, uint32_t inUs, uint32_t seenUs) {
    Open& o = open_[uint8_t(c)];
    if (o.on) finish(c);
    o = Open{};
    o.on = true, o.in = inUs;
    for (uint32_t& a : o.at) a = kUnset;
    set(o, St::Seen, seenUs);
  }
  bool isOpen(Ctl c) const { return open_[uint8_t(c)].on; }
  bool has(Ctl c, St s) const { return isOpen(c) && open_[uint8_t(c)].at[uint8_t(s)] != kUnset; }
  // A gesture that turned out to be another (a knock that became a knock
  // knock) or none (a pose let go before it counted): forgotten.
  void cancel(Ctl c) { open_[uint8_t(c)].on = false; }
  // A press that turned out to be a double or a hold: the same event, filed under it.
  void rekey(Ctl from, Ctl to) {
    if (!isOpen(from)) return;
    if (isOpen(to)) finish(to);
    open_[uint8_t(to)] = open_[uint8_t(from)];
    open_[uint8_t(from)].on = false;
  }
  void mark(Ctl c, St s, uint32_t us) {
    Open& o = open_[uint8_t(c)];
    if (o.on) set(o, s, us);
  }
  // Closes it now, with whatever stages it reached.
  void close(Ctl c) {
    if (isOpen(c)) finish(c);
  }

  // Every engine tick: what spoke and waited for one is taken.
  void ticked(uint32_t us) {
    for (Open& o : open_)
      if (o.on && o.at[uint8_t(St::Word)] != kUnset) set(o, St::Tick, us);
  }
  // Every frame handed to the panel: seq is that frame's (0: it sent no
  // rows, so it showed nothing new); acks has a bit for each control whose
  // acknowledgement it draws.
  void framed(uint32_t us, uint32_t seq, uint32_t acks) {
    if (!seq) return;
    for (uint8_t c = 0; c < kCtls; ++c) {
      Open& o = open_[c];
      if (!o.on) continue;
      if ((acks & (uint32_t(1) << c)) && set(o, St::Ack, us)) o.ackSeq = seq;
      if (o.at[uint8_t(St::Tick)] != kUnset && set(o, St::Frame, us)) o.frameSeq = seq;
    }
  }
  // The panel's send task finished frame `seq` (and every one before it) at us.
  void panelDone(uint32_t seq, uint32_t us) {
    for (uint8_t c = 0; c < kCtls; ++c) {
      Open& o = open_[c];
      if (!o.on) continue;
      if (o.ackSeq && int32_t(seq - o.ackSeq) >= 0) set(o, St::AckPanel, us), o.ackSeq = 0;
      if (o.frameSeq && int32_t(seq - o.frameSeq) >= 0) {
        set(o, St::Panel, us);
        finish(Ctl(c));
      }
    }
  }
  // Every loop: an event open too long is closed as it stands.
  void expire(uint32_t us) {
    for (uint8_t c = 0; c < kCtls; ++c)
      if (open_[c].on && us - open_[c].in > kGiveUpUs) finish(Ctl(c));
  }
  void clear() { *this = Recorder{}; }

  // One line per control that has events: n, silent, then p50/p95/max ms of
  // each stage after In ("-": none reached it). Returns false past the last.
  //   knock_knock n=5 words=5 silent=0 seen=0.4/1.1/1.3 detect=... panel=...
  bool line(uint8_t i, char* out, size_t cap) const {
    if (i >= kCtls) return false;
    const Done& d = done_[i];
    int at = std::snprintf(out, cap, "%s n=%u words=%u silent=%u noack=%u", name(Ctl(i)), unsigned(d.n),
                           unsigned(d.words), unsigned(d.silent), unsigned(d.noAck));
    for (uint8_t s = 0; s < kStages && at > 0 && size_t(at) < cap; ++s) {
      uint16_t v[kKeep];
      uint8_t k = 0;
      for (uint8_t j = 0; j < std::min<uint32_t>(d.n, kKeep); ++j)
        if (d.at[j][s] != kNone) v[k++] = d.at[j][s];
      if (!k) {
        at += std::snprintf(out + at, cap - size_t(at), " %s=-", name(St(s)));
        continue;
      }
      std::sort(v, v + k);
      const uint16_t p50 = v[k / 2], p95 = v[std::min<int>(k - 1, (k * 95 + 99) / 100 - 1)], mx = v[k - 1];
      at += std::snprintf(out + at, cap - size_t(at), " %s=%u.%u/%u.%u/%u.%u", name(St(s)), unsigned(p50 / 10),
                          unsigned(p50 % 10), unsigned(p95 / 10), unsigned(p95 % 10), unsigned(mx / 10), unsigned(mx % 10));
    }
    return true;
  }
  // The newest event of control i, each stage in ms after In; false if none yet.
  bool last(uint8_t i, char* out, size_t cap) const {
    if (i >= kCtls || !done_[i].n) return false;
    const Done& d = done_[i];
    const uint16_t* a = d.at[(d.n - 1) % kKeep];
    int at = std::snprintf(out, cap, "last %s", name(Ctl(i)));
    for (uint8_t s = 0; s < kStages && at > 0 && size_t(at) < cap; ++s) {
      if (a[s] == kNone) at += std::snprintf(out + at, cap - size_t(at), " %s=-", name(St(s)));
      else at += std::snprintf(out + at, cap - size_t(at), " %s=%u.%u", name(St(s)), unsigned(a[s] / 10), unsigned(a[s] % 10));
    }
    return true;
  }
  uint32_t count(Ctl c) const { return done_[uint8_t(c)].n; }
  // A stage of the newest event of c, in 0.1 ms; kNone if not reached (tests).
  uint16_t lastStage(Ctl c, St s) const {
    const Done& d = done_[uint8_t(c)];
    return d.n ? d.at[(d.n - 1) % kKeep][uint8_t(s)] : kNone;
  }

 private:
  static constexpr uint32_t kUnset = UINT32_MAX;
  struct Open {
    bool on = false;
    uint32_t in = 0;
    uint32_t at[kStages]{};
    uint32_t ackSeq = 0, frameSeq = 0;
  };
  struct Done {
    uint32_t n = 0, words = 0, silent = 0, noAck = 0;
    uint16_t at[kKeep][kStages]{};   // 0.1 ms after In
  };
  // The first time a stage is reached counts. True when this was it.
  static bool set(Open& o, St s, uint32_t us) {
    uint32_t& a = o.at[uint8_t(s)];
    if (a != kUnset) return false;
    a = int32_t(us - o.in) < 0 ? 0 : us - o.in;
    return true;
  }
  void finish(Ctl c) {
    Open& o = open_[uint8_t(c)];
    o.on = false;
    Done& d = done_[uint8_t(c)];
    uint16_t* row = d.at[d.n % kKeep];
    for (uint8_t s = 0; s < kStages; ++s)
      row[s] = o.at[s] == kUnset ? kNone : uint16_t(std::min<uint32_t>(o.at[s] / 100, kNone - 1));
    ++d.n;
    const bool spoke = o.at[uint8_t(St::Word)] != kUnset, felt = o.at[uint8_t(St::Detect)] != kUnset;
    const bool acked = o.at[uint8_t(St::Ack)] != kUnset, shown = o.at[uint8_t(St::Frame)] != kUnset;
    d.words += spoke;
    if ((spoke || felt) && !acked) ++d.noAck;
    if ((spoke || felt) && !acked && !shown) ++d.silent;
  }
  Open open_[kCtls]{};
  Done done_[kCtls]{};
};

// What one 50 Hz sample says about the controls, as main.cpp feel() saw it.
struct Sample {
  uint32_t readUs = 0;      // the sense task's read handed on (the 5th of 250 Hz)
  uint32_t peakUs = 0;      // the read its peak came from: where a knock landed
  uint32_t seenUs = 0;      // the loop took it
  bool button = false;      // BOOT's raw level
  bool knockWaiting = false, knockKnock = false;   // KnockSense: one knock in; a second
  int8_t rawPose = -1;      // hw::Poses::raw of the screen's sample: -1 a jolt, 0 none, 1 a side, 2 upside down
  int8_t rawAim = -1;       // the aim its tilt points at (rawAim below), -1 none or a jolt
  bool sideHolding = false; // SideHoldDetector's level
  bool hangCharging = false;// HangDetector's progress above 0
  int8_t aimNow = -1;       // the aim the tilt detector reads now (Dish::aimNow)
  bool woke = false;        // low power ended on this sample
  // The words it fired (Dish::firedLastSample).
  bool press = false, dbl = false, hold = false, hang = false, shake = false, knock = false, knockKnockWord = false;
};

// The aim a sample in the screen's frame points at, as DuelInput::from reads
// the tilt loci (0.15 off level: 300 mg in the plane), with no smoothing:
// where the hand is, not where the detector has got to. -1: level, a jolt,
// or not face up (a badge on edge is a pose, not an aim: the side hold and
// the hang are timed as theirs).
inline int8_t rawAim(int16_t ax, int16_t ay, int16_t az) {
  const int32_t m2 = int32_t(ax) * ax + int32_t(ay) * ay + int32_t(az) * az;
  if (m2 < 750 * 750 || m2 > 1250 * 1250 || az < 500) return -1;
  const int32_t x = ax < 0 ? -ax : ax, y = ay < 0 ? -ay : ay;
  if (x < 300 && y < 300) return -1;
  return x >= y ? (ax > 0 ? 0 : 1) : (ay < 0 ? 2 : 3);
}

// Opens and marks the events from the samples (the frames and the panel mark
// the rest, in main.cpp). Pure, like the Recorder.
class Watch {
 public:
  void sample(const Sample& s, Recorder& r) {
    if (s.woke) {   // the wake: from the read that woke it to the first frame and the light
      r.begin(Ctl::Wake, s.readUs, s.seenUs);
      r.mark(Ctl::Wake, St::Detect, s.seenUs);
      r.mark(Ctl::Wake, St::Tick, s.seenUs);   // nothing waits for the engine: the next frame is it
    }
    // A knock: one in, waiting for its second; then the knock knock.
    // The second knock is where "one more" goes out with no word: In is that
    // read, though KnockSense says knockKnock only once it has rung out.
    if (s.knockWaiting && !waitWas_) r.begin(Ctl::Knock, s.peakUs, s.seenUs), r.mark(Ctl::Knock, St::Detect, s.seenUs);
    if (!s.knockWaiting && waitWas_ && !s.knock) second_ = true, secondUs_ = s.peakUs, secondSeen_ = s.seenUs;
    if (s.knockKnock && !kkWas_) {
      r.cancel(Ctl::Knock);   // it was the first of two
      if (second_) r.begin(Ctl::KnockKnock, secondUs_, secondSeen_);
      else r.begin(Ctl::KnockKnock, s.peakUs, s.seenUs);
      r.mark(Ctl::KnockKnock, St::Detect, s.seenUs);
      second_ = false;
    }
    if (s.knock || s.knockKnockWord) second_ = false;
    waitWas_ = s.knockWaiting, kkWas_ = s.knockKnock;
    if (s.knock) r.mark(Ctl::Knock, St::Word, s.seenUs);
    if (s.knockKnockWord) r.mark(Ctl::KnockKnock, St::Word, s.seenUs);
    // BOOT: from the press down; a double or a hold is filed as one.
    if (s.button && !buttonWas_) r.begin(Ctl::Press, s.readUs, s.seenUs), r.mark(Ctl::Press, St::Detect, s.seenUs);
    buttonWas_ = s.button;
    if (s.dbl) r.rekey(Ctl::Press, Ctl::Double), r.mark(Ctl::Double, St::Word, s.seenUs);
    else if (s.hold && !s.sideHolding) r.rekey(Ctl::Press, Ctl::Hold), r.mark(Ctl::Hold, St::Word, s.seenUs);
    else if (s.press && !s.knockKnockWord) r.mark(Ctl::Press, St::Word, s.seenUs);
    // The poses: In is the raw pose's first steady sample, of the run that held.
    run_.sample(s.rawPose, s.readUs, s.seenUs);
    pose(Ctl::Side, 1, s.sideHolding, sideWas_, s, r);
    pose(Ctl::Hang, 2, s.hangCharging, chargeWas_, s, r);
    if (s.hold && s.sideHolding) r.mark(Ctl::Side, St::Word, s.seenUs);
    if (s.hang) r.mark(Ctl::Hang, St::Word, s.seenUs);
    // A shake is decided on its last jolt.
    if (s.shake) {
      r.begin(Ctl::Shake, s.readUs, s.seenUs);
      r.mark(Ctl::Shake, St::Detect, s.seenUs);
      r.mark(Ctl::Shake, St::Word, s.seenUs);
    }
    // A tilt: from the hand reaching an aim to the detector reading it.
    if (s.rawAim >= 0 && s.rawAim != aim_) {
      if (!r.has(Ctl::Tilt, St::Frame)) r.cancel(Ctl::Tilt);   // a new aim before the last showed: it was passed through
      r.begin(Ctl::Tilt, s.readUs, s.seenUs);
      target_ = s.rawAim;
    } else if (s.rawAim < 0 && s.rawPose >= 0 && r.isOpen(Ctl::Tilt) && !r.has(Ctl::Tilt, St::Word)) {
      r.cancel(Ctl::Tilt);   // back to level before it counted
      target_ = -1;
    }
    if (s.rawAim >= 0 || s.rawPose >= 0) aim_ = s.rawAim;
    if (target_ >= 0 && s.aimNow == target_ && r.isOpen(Ctl::Tilt)) {
      r.mark(Ctl::Tilt, St::Detect, s.seenUs);
      r.mark(Ctl::Tilt, St::Word, s.seenUs);
    }
  }
  // The aim a tilt under way points at, for the frame's acknowledgement.
  int8_t tiltTarget() const { return target_; }

 private:
  // A raw pose's run: from its first steady sample until 100 ms of steady
  // samples in another, so a jolt or a wobble on the 45 degree line keeps it.
  struct Run {
    int8_t pose = 0, cand = -1;
    uint32_t at = 0, seen = 0, candAt = 0, candSeen = 0;
    void sample(int8_t p, uint32_t us, uint32_t seenUs) {
      if (p < 0) return;
      if (p == pose) return void(cand = -1);
      if (p != cand) cand = p, candAt = us, candSeen = seenUs;
      if (us - candAt >= 100000) pose = cand, at = candAt, seen = candSeen, cand = -1;
    }
    bool since(int8_t p, uint32_t& us, uint32_t& seenUs) const {
      if (pose == p) return us = at, seenUs = seen, true;
      if (cand == p) return us = candAt, seenUs = candSeen, true;
      return false;
    }
  };
  // A pose's level (the side hold, the hang's charge): its rise is the
  // detector acting; a fall before the word is a turn let go.
  void pose(Ctl c, int8_t which, bool level, bool& was, const Sample& s, Recorder& r) {
    if (level && !was) {
      uint32_t in = s.readUs, seen = s.seenUs;
      run_.since(which, in, seen);
      r.begin(c, in, seen);
      r.mark(c, St::Detect, s.seenUs);
    }
    if (!level && was && !r.has(c, St::Word)) r.close(c);
    was = level;
  }
  Run run_;
  bool waitWas_ = false, kkWas_ = false, buttonWas_ = false, sideWas_ = false, chargeWas_ = false, second_ = false;
  uint32_t secondUs_ = 0, secondSeen_ = 0;   // the read of a second knock, and when the loop took it
  int8_t aim_ = -1, target_ = -1;
};

}  // namespace latency
