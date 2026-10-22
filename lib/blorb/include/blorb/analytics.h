#pragma once
// Analytics (#50): how the badge itself is doing (power, resets, frames,
// gestures and their latency, duels, links, updates, errors), for the owner
// to improve the system. Owner: "whatever analytics so I can improve the
// system [...] As long as the badge stays responsive", and "They don't have
// to be realtime, as long as they are recorded."
//
// Cheap aggregates only: an event is one increment (add) or one histogram
// bin (hist) of the open bucket, in RAM; nothing is logged per event. A
// bucket is a day of powered time (kBucketSeconds), stamped with where it
// starts on the badge's own powered clock and the wall time while one was
// known, so the server can place it. The buckets ride the life stats' file
// (life_tally.h: same file, same saves) and leave the badge only when the
// server has them: STATS json (or the Wi-Fi upload) reads them, and only an
// acknowledgement (STATS ack <seq>) drops the closed ones it names. Offline
// for weeks, the oldest two closed buckets merge into one, so the file stays
// within kMaxBuckets and nothing counted is ever dropped.
//
// Closed buckets are kept encoded (about 100 bytes each), so RAM holds one
// open bucket and a few KB at most.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blorb {

enum class AnalyticUnit : uint8_t { Count, Secs };
struct AnalyticInfo { uint8_t slot; const char* name; AnalyticUnit unit; };

inline constexpr AnalyticInfo ANALYTICS[] = {
#define BLORB_ANALYTIC(slot, name, unit) {slot, #name, AnalyticUnit::unit},
#include "blorb/defs/analytics.def"
#undef BLORB_ANALYTIC
};
inline constexpr uint8_t kAnalyticCount = uint8_t(sizeof ANALYTICS / sizeof ANALYTICS[0]);

namespace anx {
enum : uint8_t {
#define BLORB_ANALYTIC(slot, name, unit) name = slot,
#include "blorb/defs/analytics.def"
#undef BLORB_ANALYTIC
};
}  // namespace anx

// Histograms: a value lands in the first bin whose edge it is under; the
// last bin is everything at or past the last edge. Edges never change once
// shipped (a new shape is a new histogram, added at the end).
inline constexpr uint8_t kMaxBins = 10;
struct HistogramInfo { uint8_t slot; const char* name; uint8_t edgeCount; uint16_t edges[kMaxBins - 1]; };
inline constexpr HistogramInfo HISTOGRAMS[] = {
    {0, "frame_ms", 6, {10, 20, 33, 50, 100, 200}},                  // (f) a frame drawn and sent, whole
    {1, "gesture_ms", 7, {50, 100, 150, 200, 300, 500, 1000}},       // (f) a gesture's decisive moment to its result on the panel
    {2, "gesture_ack_ms", 7, {50, 100, 150, 200, 300, 500, 1000}},   // (f) ...to the first frame that acknowledged it
    {3, "battle_turns", 5, {4, 7, 10, 13, 17}},                      // (e) rounds in one battle
    {4, "battle_s", 6, {30, 60, 120, 180, 300, 600}},                // (e) a battle, fight mode opened to the friends screen
    {5, "battery_mv", 8, {3300, 3500, 3600, 3700, 3800, 3900, 4000, 4100}},   // (w) a reading a minute, where a battery ADC is read
};
inline constexpr uint8_t kHistogramCount = uint8_t(sizeof HISTOGRAMS / sizeof HISTOGRAMS[0]);
namespace hist {
enum : uint8_t { frame_ms, gesture_ms, gesture_ack_ms, battle_turns, battle_s, battery_mv };
}

constexpr bool analyticsInOrder() {
  for (uint8_t i = 0; i < kAnalyticCount; ++i)
    if (ANALYTICS[i].slot != i) return false;
  for (uint8_t i = 0; i < kHistogramCount; ++i)
    if (HISTOGRAMS[i].slot != i || HISTOGRAMS[i].edgeCount >= kMaxBins) return false;
  return true;
}
static_assert(analyticsInOrder(), "analytics.def and HISTOGRAMS: slot = position, from 0, no gaps");

struct AnalyticsBucket {
  // The buckets this one holds, by sequence number: one, or a run merged
  // when the badge was offline too long. The server replaces whatever rows
  // it holds in [first, last] with this one.
  uint32_t first = 0, last = 0;
  uint32_t poweredStart = 0;   // seconds of the badge's powered clock (Analytics::poweredSeconds) at its start
  uint32_t span = 0;           // powered seconds it covers
  uint32_t wallFirst = 0, wallLast = 0;   // unix seconds of its first and last second with wall time known; 0: none
  uint32_t count[kAnalyticCount]{};
  std::vector<uint32_t> extra;            // a newer firmware's slots, carried
  uint32_t bins[kHistogramCount][kMaxBins]{};
  void merge(const AnalyticsBucket& newer);   // newer comes after this one
  bool operator==(const AnalyticsBucket&) const;
};

class Analytics {
 public:
  static constexpr uint32_t kBucketSeconds = 24 * 3600;
  static constexpr uint8_t kMaxBuckets = 16;   // closed and open; then the oldest two merge

  Analytics();

  // ---- the hot path: one increment ----
  void add(uint8_t slot, uint32_t n = 1) {
    uint32_t& c = open_.count[slot];
    c = c > UINT32_MAX - n ? UINT32_MAX : c + n;
  }
  void hist(uint8_t h, uint32_t value) {
    const HistogramInfo& info = HISTOGRAMS[h];
    uint8_t bin = 0;
    while (bin < info.edgeCount && value >= info.edges[bin]) ++bin;
    uint32_t& b = open_.bins[h][bin];
    if (b != UINT32_MAX) ++b;
  }
  // One powered second went by (the Dish calls it every ten ticks), with the
  // wall time if it is known. A bucket a day of it old closes here.
  void second(std::optional<uint32_t> wall);

  // ---- the upload ----
  // The server has every closed bucket up to `seq`: they go. The open one
  // never does (it is sent again, grown, next time). False: nothing to drop.
  bool ack(uint32_t seq);
  uint32_t poweredSeconds() const { return powered_; }
  // Oldest first, the open one last; each decoded once, for the JSON and tests.
  template <class F> void forEach(F&& f) const {
    for (const std::vector<uint8_t>& c : closed_) {
      AnalyticsBucket b;
      if (decodeBucket(c.data(), c.size(), b)) f(b, false);
    }
    f(open_, true);
  }
  const AnalyticsBucket& open() const { return open_; }
  size_t closedCount() const { return closed_.size(); }

  // ---- the file section (life_tally.h writes it) ----
  std::vector<uint8_t> encode() const;
  bool decode(const uint8_t*, size_t);   // false: left as constructed
  static std::vector<uint8_t> encodeBucket(const AnalyticsBucket&);
  static bool decodeBucket(const uint8_t*, size_t, AnalyticsBucket&);

 private:
  void close();
  uint32_t powered_ = 0, nextSeq_ = 1;
  AnalyticsBucket open_;
  std::vector<std::vector<uint8_t>> closed_;
};

// "analytics":{...}, appended to a JSON document (life_tally.cpp's lifeStatsJson).
void analyticsJson(const Analytics&, std::string& out);
// The server's answer to an upload, {"ack":<seq>,...}: the seq to hand
// Dish::ackStats (its "closed_upto" once it has stored them). nullopt when it
// names none: keep everything.
std::optional<uint32_t> statsAckOf(std::string_view body);

}  // namespace blorb
