#pragma once
// Treatment against control, paired by seed. A metric that could not be
// measured in a run (the probe situation never came up) is NaN, and that
// seed drops out of the pair.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "blorb/fixed.h"

namespace e2e {

enum class Verdict : uint8_t { Yes, Weak, No, Opposite, NoProbe };

inline const char* verdictName(Verdict v) {
  switch (v) {
    case Verdict::Yes: return "yes";
    case Verdict::Weak: return "weak";
    case Verdict::No: return "no";
    case Verdict::Opposite: return "no (opposite)";
    case Verdict::NoProbe: return "no (probe never recurs)";
  }
  return "?";
}

struct Effect {
  int n = 0;                  // seeds where both arms measured
  double meanT = 0, meanC = 0, sdC = 0;
  double diff = 0, lo = 0, hi = 0;   // mean of paired (T - C), 95% bootstrap CI
  double towards = 0;         // share of seeds where T - C has the predicted sign
  double identical = 0;       // share of seeds where T == C exactly
  Verdict verdict = Verdict::No;
};

// The rules, fixed before any scenario ran:
//   yes        the CI excludes 0 on the predicted side and 75% or more of seeds move that way
//   weak       the CI excludes 0 on the predicted side, fewer than 75% of seeds move that way
//   no         the CI includes 0
//   opposite   the CI excludes 0 on the other side
//   no probe   fewer than half the seeds measured the probe in both arms
inline Effect compare(const std::vector<double>& t, const std::vector<double>& c, int direction) {
  Effect e;
  std::vector<double> d;
  double sumC = 0, sumC2 = 0;
  for (size_t i = 0; i < t.size() && i < c.size(); ++i) {
    if (std::isnan(t[i]) || std::isnan(c[i])) continue;
    d.push_back(t[i] - c[i]);
    e.meanT += t[i];
    e.meanC += c[i];
    sumC += c[i];
    sumC2 += c[i] * c[i];
    e.towards += (t[i] - c[i]) * direction > 0;
    e.identical += t[i] == c[i];
  }
  e.n = int(d.size());
  if (e.n * 2 < int(t.size()) || e.n == 0) {
    e.verdict = Verdict::NoProbe;
    return e;
  }
  e.meanT /= e.n;
  e.meanC /= e.n;
  e.sdC = std::sqrt(std::max(0.0, sumC2 / e.n - (sumC / e.n) * (sumC / e.n)));
  e.towards /= e.n;
  e.identical /= e.n;
  for (double x : d) e.diff += x;
  e.diff /= e.n;

  constexpr int kResamples = 10000;
  std::vector<double> means(kResamples);
  blorb::Rng rng = blorb::Rng::seeded(0xB007);
  for (double& m : means) {
    double s = 0;
    for (int i = 0; i < e.n; ++i) s += d[rng.below(uint32_t(e.n))];
    m = s / e.n;
  }
  std::sort(means.begin(), means.end());
  e.lo = means[kResamples * 25 / 1000];
  e.hi = means[kResamples * 975 / 1000 - 1];

  const double near = direction > 0 ? e.lo : -e.hi;   // the CI's bound nearest zero, in the predicted sense
  const double far = direction > 0 ? e.hi : -e.lo;
  if (near > 0) e.verdict = e.towards >= 0.75 ? Verdict::Yes : Verdict::Weak;
  else if (far < 0) e.verdict = Verdict::Opposite;
  else e.verdict = Verdict::No;
  return e;
}

}  // namespace e2e
