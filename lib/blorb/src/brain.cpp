#include "blorb/brain.h"

#include <algorithm>
#include <cstdlib>

namespace blorb {
namespace {

// Temperament genes arrive as 0..1 (contracts.md); these turn them into rates.
// Learning is normalised LMS, so one update moves the prediction for the
// features that were on exactly `rate` of the way to what was observed. Any
// rate below 1 converges without overshoot, and mid temperament (0.25) gets
// within 5 percent of the truth in about 10 trials of one situation.
constexpr Fx kLearnScale = Fx::ratio(1, 2);
// Per dream. A mid forgetRate halves an unreinforced weight in about 2800 dreams.
constexpr Fx kForgetScale = Fx::ratio(1, 2048);
// Exploration noise per action is uniform in [0, kNoiseScale * (explore + arousal)).
constexpr Fx kNoiseScale = Fx::ratio(1, 4);
// Each fresh pick of an action adds habituation * kHabitGain to its habit,
// and every think fades all habits by kHabitFade.
constexpr Fx kHabitGain = Fx::ratio(1, 4);
constexpr Fx kHabitFade = Fx::ratio(1, 16);
// Confidence is |effect| / kConfidentEffect, saturating at 1: a belief that
// moves a drive by a quarter of its range or more is held with certainty.
constexpr Fx kConfidentEffect = Fx::ratio(1, 4);

constexpr size_t kEpisodes = 16;

using Weights = Q15[kFeatureCount][kActionCount][kDriveCount];

template <class Row, size_t N, class IdT>
int rowIndex(const Row (&rows)[N], IdT id) {
  for (size_t i = 0; i < N; ++i) if (rows[i].id == id) return int(i);
  return -1;
}

int featureIndex(LocusId id) {
  for (size_t i = 0; i < kFeatureCount; ++i) if (FEATURES[i] == id) return int(i);
  return -1;
}

Fx absFx(Fx v) { return v < Fx::zero() ? -v : v; }
Fx divide(Fx a, Fx b) { return Fx::sat(int64_t(a.raw) * Fx::kOne / b.raw); }

Fx normSq(const Fx f[kFeatureCount]) {
  Fx s;
  for (size_t i = 0; i < kFeatureCount; ++i) s += f[i] * f[i];
  return s;
}

Fx predicted(const Weights& w, const Fx f[kFeatureCount], size_t a, size_t d) {
  Fx p;
  for (size_t i = 0; i < kFeatureCount; ++i) p += f[i] * fromQ15(w[i][a][d]);
  return p;
}

void nudge(Weights& w, const Fx f[kFeatureCount], Fx norm, size_t a, size_t d, Fx observed, Fx rate) {
  Fx step = divide(rate * (observed - predicted(w, f, a, d)), norm);
  for (size_t i = 0; i < kFeatureCount; ++i)
    if (f[i] != Fx::zero()) w[i][a][d] = toQ15(fromQ15(w[i][a][d]) + step * f[i]);
}

// False when no feature was on, since then there is nothing to attribute the change to.
bool learn(Weights& w, const Fx f[kFeatureCount], size_t a, const Fx observed[kDriveCount], Fx rate) {
  Fx norm = normSq(f);
  if (norm == Fx::zero()) return false;
  for (size_t d = 0; d < kDriveCount; ++d) nudge(w, f, norm, a, d, observed[d], rate);
  return true;
}

// Ids no registry row names are legal and inert (ids.h).
bool applyInstinct(Weights& w, const Instinct& in) {
  int a = rowIndex(ACTIONS, in.action), d = rowIndex(DRIVES, in.drive);
  if (a < 0 || d < 0) return false;
  Fx cues[kFeatureCount]{};
  for (uint8_t i = 0; i < in.cueCount && i < 3; ++i) {
    int f = featureIndex(in.cue[i]);
    if (f >= 0) cues[f] = Fx::one();
  }
  Fx norm = normSq(cues);
  if (norm == Fx::zero()) return false;
  nudge(w, cues, norm, size_t(a), size_t(d), in.level, in.strength);
  return true;
}

// Rounds the decrement up, so every weight reaches zero and negative weights
// fade exactly as fast as positive ones.
void forget(Weights& w, Fx rate) {
  if (rate <= Fx::zero()) return;
  for (auto& plane : w)
    for (auto& row : plane)
      for (Q15& q : row) {
        int32_t mag = std::abs(int32_t(q.v));
        int32_t dec = int32_t((int64_t(mag) * rate.raw + Fx::kOne - 1) >> Fx::kFrac);
        int32_t left = std::max(0, mag - dec);
        q.v = int16_t(q.v < 0 ? -left : left);
      }
}

}  // namespace

Decision Brain::think(const Fx features[kFeatureCount], const Fx drives[kDriveCount], Fx arousal,
                      const Temperament& t, bool actionFinished, Rng& rng) {
  heldTicks_ = heldTicks_ > UINT32_MAX - kBrainEvery ? UINT32_MAX : heldTicks_ + kBrainEvery;
  trace_ = applyDecayTicks(Fx::one(), t.trace, heldTicks_);
  for (Fx& h : habit_) h -= h * kHabitFade;

  int cur = rowIndex(ACTIONS, current_);
  uint32_t minTicks = cur < 0 ? 0 : ACTIONS[cur].minTicks;
  if (!actionFinished && heldTicks_ < minTicks) return {current_, false};

  if (cur >= 0) {
    Fx observed[kDriveCount];
    for (size_t d = 0; d < kDriveCount; ++d) observed[d] = drives[d] - startDrives_[d];
    if (learn(w_, startFeatures_, size_t(cur), observed, t.learnRate * kLearnScale * trace_)) {
      Episode& e = episodes_[episodeHead_];
      std::copy(startFeatures_, startFeatures_ + kFeatureCount, e.features);
      e.action = current_;
      std::copy(observed, observed + kDriveCount, e.driveDelta);
      episodeHead_ = uint8_t((episodeHead_ + 1) % kEpisodes);
    }
  }

  Fx noise = (t.explore + arousal) * kNoiseScale;
  size_t best = 0;
  Fx bestScore;
  for (size_t a = 0; a < kActionCount; ++a) {
    Fx s = -habit_[a];
    for (size_t d = 0; d < kDriveCount; ++d) s -= drives[d] * predicted(w_, features, a, d);
    s += rng.unit() * noise;
    if (a == 0 || s > bestScore) { best = a; bestScore = s; }
  }

  ActionId next = ACTIONS[best].id;
  bool changed = next != current_;
  current_ = next;
  habit_[best] += t.habituation * kHabitGain;
  std::copy(features, features + kFeatureCount, startFeatures_);
  std::copy(drives, drives + kDriveCount, startDrives_);
  heldTicks_ = 0;
  trace_ = Fx::one();
  return {next, changed};
}

bool Brain::dream(const Temperament& t, Rng& rng) {
  bool dreamt;
  if (!instinctQueue_.empty()) {
    dreamt = applyInstinct(w_, instinctQueue_.front());
    instinctQueue_.erase(instinctQueue_.begin());
  } else {
    const Episode& e = episodes_[rng.below(kEpisodes)];
    int a = rowIndex(ACTIONS, e.action);
    dreamt = a >= 0 && learn(w_, e.features, size_t(a), e.driveDelta, t.learnRate * kLearnScale);
  }
  forget(w_, t.forgetRate * kForgetScale);
  return dreamt;
}

void Brain::queueInstinct(const Instinct& in) { instinctQueue_.push_back(in); }

std::vector<Belief> Brain::strongestBeliefs(uint8_t n) const {
  std::vector<Belief> out;
  for (size_t f = 0; f < kFeatureCount; ++f)
    for (size_t a = 0; a < kActionCount; ++a)
      for (size_t d = 0; d < kDriveCount; ++d) {
        Fx effect = fromQ15(w_[f][a][d]);
        if (effect == Fx::zero()) continue;
        Fx confidence = clamp01(divide(absFx(effect), kConfidentEffect));
        out.push_back({FEATURES[f], ACTIONS[a].id, DRIVES[d].id, effect, confidence});
      }
  std::stable_sort(out.begin(), out.end(), [](const Belief& x, const Belief& y) {
    return absFx(x.effect) * x.confidence > absFx(y.effect) * y.confidence;
  });
  if (out.size() > n) out.resize(n);
  return out;
}

bool Brain::setWeight(LocusId feature, ActionId action, DriveId drive, Fx effect) {
  int f = featureIndex(feature), a = rowIndex(ACTIONS, action), d = rowIndex(DRIVES, drive);
  if (f < 0 || a < 0 || d < 0) return false;
  w_[f][a][d] = toQ15(effect);
  return true;
}

Fx Brain::predict(LocusId feature, ActionId action, DriveId drive) const {
  int f = featureIndex(feature), a = rowIndex(ACTIONS, action), d = rowIndex(DRIVES, drive);
  if (f < 0 || a < 0 || d < 0) return Fx::zero();
  return fromQ15(w_[f][a][d]);
}

Brain::Axes Brain::currentAxes() {
  Axes axes;
  axes.features.assign(FEATURES.begin(), FEATURES.end());
  for (const ActionInfo& a : ACTIONS) axes.actions.push_back(a.id);
  for (const DriveInfo& d : DRIVES) axes.drives.push_back(d.id);
  return axes;
}

// `w` is [feature][action][drive], row-major in the saved axes' order.
void Brain::loadWeights(const Axes& saved, const Q15* w) {
  for (auto& plane : w_)
    for (auto& row : plane)
      for (Q15& q : row) q = Q15{};
  size_t nA = saved.actions.size(), nD = saved.drives.size();
  for (size_t sf = 0; sf < saved.features.size(); ++sf) {
    int f = featureIndex(saved.features[sf]);
    if (f < 0) continue;
    for (size_t sa = 0; sa < nA; ++sa) {
      int a = rowIndex(ACTIONS, saved.actions[sa]);
      if (a < 0) continue;
      for (size_t sd = 0; sd < nD; ++sd) {
        int d = rowIndex(DRIVES, saved.drives[sd]);
        if (d >= 0) w_[f][a][d] = w[(sf * nA + sa) * nD + sd];
      }
    }
  }
}

// Field by field, so struct padding never reaches the hash.
uint32_t Brain::hash() const {
  uint32_t h = fnv1a(w_, sizeof w_);
  h = fnv1a(habit_, sizeof habit_, h);
  h = fnv1a(startFeatures_, sizeof startFeatures_, h);
  h = fnv1a(startDrives_, sizeof startDrives_, h);
  h = fnv1a(&trace_, sizeof trace_, h);
  h = fnv1a(&current_, sizeof current_, h);
  h = fnv1a(&heldTicks_, sizeof heldTicks_, h);
  for (const Episode& e : episodes_) {
    h = fnv1a(e.features, sizeof e.features, h);
    h = fnv1a(&e.action, sizeof e.action, h);
    h = fnv1a(e.driveDelta, sizeof e.driveDelta, h);
  }
  h = fnv1a(&episodeHead_, sizeof episodeHead_, h);
  uint32_t queued = uint32_t(instinctQueue_.size());
  h = fnv1a(&queued, sizeof queued, h);
  for (const Instinct& in : instinctQueue_) {
    h = fnv1a(in.cue, sizeof in.cue, h);
    h = fnv1a(&in.cueCount, sizeof in.cueCount, h);
    h = fnv1a(&in.action, sizeof in.action, h);
    h = fnv1a(&in.drive, sizeof in.drive, h);
    h = fnv1a(&in.level, sizeof in.level, h);
    h = fnv1a(&in.strength, sizeof in.strength, h);
  }
  return h;
}

}  // namespace blorb
