#pragma once
// A scenario run end to end: both arms on seeds 1..N in parallel, every
// metric compared pair by pair, and, when E2E_OUT names a directory, the
// numbers as JSON plus the probe window of one representative seed rendered
// frame by frame with paint::draw and the grungo pack (the call
// src/main.cpp's loop makes, so these are the simulator's frames).
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include "e2e/run.h"
#include "e2e/stats.h"
#include "grungo_pack.h"
#include "paint/sprite_pack.h"

namespace e2e {

struct Metric {
  const char* name;
  int direction;                       // +1: treatment predicted higher, -1: lower
  double (*of)(const RunLog&);         // NaN when the run never met the probe situation
};

struct Scenario {
  const char* name;
  const char* stimulus;                // what the treatment adds
  const char* prediction;              // written before the first run
  std::vector<Row> rows;
  uint64_t endAt;
  std::vector<Reaction> reactions = {};   // gestures made in answer to what he does
  std::vector<Metric> metrics;         // metrics[0] decides the verdict; the rest are reported
  Clip clip;                           // the moment the contact sheet and the video show
  bool knownFailing = false;           // measured without a clear effect: the test records it until the engine shows one
};

struct Result {
  const Scenario* scenario = nullptr;
  int seeds = 0;
  std::vector<std::vector<double>> t, c;   // [metric][seed]
  std::vector<Effect> effects;
  double diverged = 0;                     // share of seeds whose arms ended in different states
  uint32_t shownSeed = 0;
};

inline int seedsFromEnv(int fallback) {
  const char* s = std::getenv("E2E_SEEDS");
  return s && std::atoi(s) > 0 ? std::atoi(s) : fallback;
}

inline Result evaluate(const Scenario& s, int seeds) {
  std::vector<RunLog> logs(size_t(2 * seeds));
  std::atomic<int> next{0};
  auto work = [&] {
    for (int job; (job = next++) < 2 * seeds;)
      logs[size_t(job)] = run(s.rows, s.endAt, job % 2 ? Arm::Treatment : Arm::Control, uint32_t(job / 2 + 1), nullptr,
                              {}, s.reactions);
  };
  std::vector<std::thread> pool;
  unsigned n = std::max(1u, std::min(std::thread::hardware_concurrency(), unsigned(2 * seeds)));
  for (unsigned i = 0; i < n; ++i) pool.emplace_back(work);
  for (std::thread& th : pool) th.join();

  Result r;
  r.scenario = &s;
  r.seeds = seeds;
  r.t.assign(s.metrics.size(), {});
  r.c.assign(s.metrics.size(), {});
  for (int i = 0; i < seeds; ++i) {
    const RunLog& c = logs[size_t(2 * i)];
    const RunLog& t = logs[size_t(2 * i + 1)];
    r.diverged += c.finalHash != t.finalHash;
    for (size_t m = 0; m < s.metrics.size(); ++m) {
      r.c[m].push_back(s.metrics[m].of(c));
      r.t[m].push_back(s.metrics[m].of(t));
    }
  }
  r.diverged /= seeds;
  for (size_t m = 0; m < s.metrics.size(); ++m) r.effects.push_back(compare(r.t[m], r.c[m], s.metrics[m].direction));

  // The seed whose primary difference sits nearest the mean difference.
  double best = INFINITY;
  for (int i = 0; i < seeds; ++i) {
    double d = r.t[0][size_t(i)] - r.c[0][size_t(i)];
    if (std::isnan(d)) continue;
    double off = std::fabs(d - r.effects[0].diff);
    if (off < best) best = off, r.shownSeed = uint32_t(i + 1);
  }
  return r;
}

inline std::string fmt(const char* f, double v) {
  char b[64];
  std::snprintf(b, sizeof b, f, v);
  return b;
}

inline std::string summary(const Result& r) {
  std::string out;
  for (size_t m = 0; m < r.effects.size(); ++m) {
    const Effect& e = r.effects[m];
    out += std::string(m ? "    " : "") + r.scenario->metrics[m].name + ": T " + fmt("%.4g", e.meanT) + " vs C " +
           fmt("%.4g", e.meanC) + ", T-C " + fmt("%+.4g", e.diff) + " [" + fmt("%.4g", e.lo) + ", " +
           fmt("%.4g", e.hi) + "], " + fmt("%.0f%%", 100 * e.towards) + " of " + std::to_string(e.n) +
           " seeds as predicted, " + verdictName(e.verdict) + "\n";
  }
  out += "    arms diverged in " + fmt("%.0f%%", 100 * r.diverged) + " of seeds\n";
  return out;
}

namespace detail {

inline void mkdirs(const std::string& path) {
  for (size_t i = 1; i <= path.size(); ++i)
    if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

inline void ppm(const std::string& path, const paint::Canvas240& c) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  std::fprintf(f, "P6\n240 240\n255\n");
  for (uint16_t p : c.px) {
    const uint8_t rgb[3] = {uint8_t((p >> 11 & 31) * 255 / 31), uint8_t((p >> 5 & 63) * 255 / 63), uint8_t((p & 31) * 255 / 31)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
}

inline void list(FILE* f, const std::vector<double>& v) {
  std::fputc('[', f);
  for (size_t i = 0; i < v.size(); ++i) {
    if (std::isnan(v[i])) std::fprintf(f, "%snull", i ? "," : "");
    else std::fprintf(f, "%s%.6g", i ? "," : "", v[i]);
  }
  std::fputc(']', f);
}

inline const char* actionName(uint8_t a) {
  for (const auto& row : blorb::ACTIONS) if (row.id.v == a) return row.name;
  return "?";
}

}  // namespace detail

inline void write(const Result& r, const std::string& dir) {
  const Scenario& s = *r.scenario;
  detail::mkdirs(dir);
  FILE* f = std::fopen((dir + "/" + s.name + ".json").c_str(), "w");
  if (!f) return;
  std::fprintf(f, "{\"name\":\"%s\",\"stimulus\":\"%s\",\"prediction\":\"%s\",\"seeds\":%d,\"diverged\":%.4f,"
               "\"knownFailing\":%s,\"shownSeed\":%u,\"metrics\":[",
               s.name, s.stimulus, s.prediction, r.seeds, r.diverged, s.knownFailing ? "true" : "false", r.shownSeed);
  for (size_t m = 0; m < r.effects.size(); ++m) {
    const Effect& e = r.effects[m];
    std::fprintf(f, "%s{\"name\":\"%s\",\"direction\":%d,\"n\":%d,\"meanT\":%.6g,\"meanC\":%.6g,\"sdC\":%.6g,"
                 "\"diff\":%.6g,\"lo\":%.6g,\"hi\":%.6g,\"towards\":%.4f,\"identical\":%.4f,\"verdict\":\"%s\",\"t\":",
                 m ? "," : "", s.metrics[m].name, s.metrics[m].direction, e.n, e.meanT, e.meanC, e.sdC, e.diff, e.lo,
                 e.hi, e.towards, e.identical, verdictName(e.verdict));
    detail::list(f, r.t[m]);
    std::fprintf(f, ",\"c\":");
    detail::list(f, r.c[m]);
    std::fputc('}', f);
  }
  std::fprintf(f, "],\"frames\":{");

  // Both arms of the shown seed, again, rendering the probe window.
  static paint::Canvas240 canvas;
  for (Arm arm : {Arm::Control, Arm::Treatment}) {
    const char* side = arm == Arm::Treatment ? "treatment" : "control";
    RunLog log = run(s.rows, s.endAt, arm, r.shownSeed, &s.clip, {}, s.reactions);
    if (s.clip.window >= log.windows.size()) continue;
    const Window& w = log.windows[s.clip.window];
    const std::string out = dir + "/frames/" + s.name + "/" + side;
    detail::mkdirs(out);
    FILE* cap = std::fopen((out + "/captions.txt").c_str(), "w");
    for (size_t i = 0; i < w.frames.size(); ++i) {
      paint::draw(w.frames[i], grungoPack(), canvas);
      char name[32];
      std::snprintf(name, sizeof name, "/%05zu.ppm", i);
      detail::ppm(out + name, canvas);
      const Tick& t = w.frameTicks[i];
      if (cap) std::fprintf(cap, "%s%s\n", t.phase == Phase::Creature ? detail::actionName(t.action) : "egg/clutch",
                            t.hop ? " +hop" : t.flinch ? " +flinch" : "");
    }
    if (cap) std::fclose(cap);
    std::fprintf(f, "%s\"%s\":{\"at\":%llu,\"count\":%zu}", arm == Arm::Treatment ? "," : "", side,
                 (unsigned long long)(w.at + s.clip.fromMs), w.frames.size());
  }
  std::fprintf(f, ",\"everyMs\":%llu}}\n", (unsigned long long)s.clip.everyMs);
  std::fclose(f);
}

}  // namespace e2e
