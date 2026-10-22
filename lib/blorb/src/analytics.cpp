#include "blorb/analytics.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <iterator>

namespace blorb {
namespace {

// Unsigned LEB128: most counts are small, so a day's bucket is about 100 bytes.
void putVar(std::vector<uint8_t>& b, uint32_t v) {
  while (v >= 0x80) b.push_back(uint8_t(v | 0x80)), v >>= 7;
  b.push_back(uint8_t(v));
}

struct Reader {
  const uint8_t* p;
  size_t n, at = 0;
  bool ok = true;
  uint32_t var() {
    uint32_t v = 0;
    for (int shift = 0; shift < 35; shift += 7) {
      if (at >= n) return ok = false, 0;
      const uint8_t b = p[at++];
      v |= uint32_t(b & 0x7F) << shift;
      if (!(b & 0x80)) return v;
    }
    return ok = false, 0;
  }
};

uint32_t sum(uint32_t a, uint32_t b) { return a > UINT32_MAX - b ? UINT32_MAX : a + b; }

void appendf(std::string& s, const char* fmt, ...) {
  char b[96];
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  if (n > 0) s.append(b, std::min<size_t>(size_t(n), sizeof b - 1));
}

}  // namespace

void AnalyticsBucket::merge(const AnalyticsBucket& n) {
  last = n.last;
  span = sum(span, n.span);
  if (!wallFirst) wallFirst = n.wallFirst;
  if (n.wallLast) wallLast = n.wallLast;
  for (uint8_t i = 0; i < kAnalyticCount; ++i) count[i] = sum(count[i], n.count[i]);
  if (extra.size() < n.extra.size()) extra.resize(n.extra.size());
  for (size_t i = 0; i < n.extra.size(); ++i) extra[i] = sum(extra[i], n.extra[i]);
  for (uint8_t h = 0; h < kHistogramCount; ++h)
    for (uint8_t i = 0; i < kMaxBins; ++i) bins[h][i] = sum(bins[h][i], n.bins[h][i]);
}

bool AnalyticsBucket::operator==(const AnalyticsBucket& o) const {
  if (first != o.first || last != o.last || poweredStart != o.poweredStart || span != o.span ||
      wallFirst != o.wallFirst || wallLast != o.wallLast || extra != o.extra)
    return false;
  if (!std::equal(std::begin(count), std::end(count), std::begin(o.count))) return false;
  for (uint8_t h = 0; h < kHistogramCount; ++h)
    if (!std::equal(std::begin(bins[h]), std::end(bins[h]), std::begin(o.bins[h]))) return false;
  return true;
}

Analytics::Analytics() { open_.first = open_.last = nextSeq_++; }

void Analytics::second(std::optional<uint32_t> wall) {
  ++powered_;
  ++open_.span;
  if (wall) {
    if (!open_.wallFirst) open_.wallFirst = *wall;
    open_.wallLast = *wall;
  }
  if (open_.span >= kBucketSeconds) close();
}

void Analytics::close() {
  closed_.push_back(encodeBucket(open_));
  open_ = AnalyticsBucket{};
  open_.first = open_.last = nextSeq_++;
  open_.poweredStart = powered_;
  // Weeks offline: the oldest two become one, so the file has a fixed size and nothing counted goes.
  while (closed_.size() + 1 > kMaxBuckets) {
    AnalyticsBucket a, b;
    if (!decodeBucket(closed_[0].data(), closed_[0].size(), a) || !decodeBucket(closed_[1].data(), closed_[1].size(), b)) {
      closed_.erase(closed_.begin());   // unreadable cannot happen to bytes we wrote; never loop on it
      continue;
    }
    a.merge(b);
    closed_[1] = encodeBucket(a);
    closed_.erase(closed_.begin());
  }
}

bool Analytics::ack(uint32_t seq) {
  const size_t before = closed_.size();
  closed_.erase(std::remove_if(closed_.begin(), closed_.end(),
                               [&](const std::vector<uint8_t>& c) {
                                 AnalyticsBucket b;
                                 return decodeBucket(c.data(), c.size(), b) && b.last <= seq;
                               }),
                closed_.end());
  return closed_.size() != before;
}

// A bucket: first, last, poweredStart, span, wallFirst, wallLast, the count
// of slots and each, then the count of histograms and each one's bin count
// and bins; all LEB128.
std::vector<uint8_t> Analytics::encodeBucket(const AnalyticsBucket& b) {
  std::vector<uint8_t> o;
  for (uint32_t v : {b.first, b.last, b.poweredStart, b.span, b.wallFirst, b.wallLast}) putVar(o, v);
  putVar(o, uint32_t(kAnalyticCount + b.extra.size()));
  for (uint32_t c : b.count) putVar(o, c);
  for (uint32_t c : b.extra) putVar(o, c);
  putVar(o, kHistogramCount);
  for (uint8_t h = 0; h < kHistogramCount; ++h) {
    const uint8_t bins = uint8_t(HISTOGRAMS[h].edgeCount + 1);
    putVar(o, bins);
    for (uint8_t i = 0; i < bins; ++i) putVar(o, b.bins[h][i]);
  }
  return o;
}

bool Analytics::decodeBucket(const uint8_t* p, size_t n, AnalyticsBucket& b) {
  Reader in{p, n};
  b = AnalyticsBucket{};
  b.first = in.var(), b.last = in.var(), b.poweredStart = in.var(), b.span = in.var();
  b.wallFirst = in.var(), b.wallLast = in.var();
  const uint32_t slots = in.var();
  for (uint32_t i = 0; i < slots && in.ok; ++i) {
    const uint32_t v = in.var();
    if (i < kAnalyticCount) b.count[i] = v;
    else if (b.extra.size() < 255) b.extra.push_back(v);
  }
  const uint32_t hists = in.var();
  for (uint32_t h = 0; h < hists && in.ok; ++h) {
    const uint32_t bins = in.var();
    for (uint32_t i = 0; i < bins && in.ok; ++i) {
      const uint32_t v = in.var();
      // A histogram this firmware does not know, or bins it has no room for, cannot be kept.
      if (h < kHistogramCount && i < kMaxBins) b.bins[h][i] = v;
    }
  }
  return in.ok && in.at == n;
}

// The section: powered seconds, the next sequence number, the closed
// buckets (each length-prefixed), then the open one.
std::vector<uint8_t> Analytics::encode() const {
  std::vector<uint8_t> o;
  putVar(o, powered_);
  putVar(o, nextSeq_);
  putVar(o, uint32_t(closed_.size()));
  for (const std::vector<uint8_t>& c : closed_) {
    putVar(o, uint32_t(c.size()));
    o.insert(o.end(), c.begin(), c.end());
  }
  const std::vector<uint8_t> open = encodeBucket(open_);
  putVar(o, uint32_t(open.size()));
  o.insert(o.end(), open.begin(), open.end());
  return o;
}

bool Analytics::decode(const uint8_t* p, size_t n) {
  Reader in{p, n};
  Analytics a;
  a.powered_ = in.var();
  a.nextSeq_ = in.var();
  const uint32_t closed = in.var();
  if (!in.ok || closed >= kMaxBuckets) return false;
  for (uint32_t i = 0; i <= closed; ++i) {
    const uint32_t len = in.var();
    if (!in.ok || len > n - in.at) return false;
    AnalyticsBucket b;
    if (!decodeBucket(p + in.at, len, b)) return false;
    if (i < closed) a.closed_.emplace_back(p + in.at, p + in.at + len);
    else a.open_ = b;
    in.at += len;
  }
  if (in.at != n) return false;
  *this = std::move(a);
  return true;
}

void analyticsJson(const Analytics& a, std::string& s) {
  appendf(s, "\"analytics\":{\"powered_s\":%u,\"bucket_s\":%u,\"hist_edges\":{", unsigned(a.poweredSeconds()),
          unsigned(Analytics::kBucketSeconds));
  for (uint8_t h = 0; h < kHistogramCount; ++h) {
    appendf(s, "%s\"%s\":[", h ? "," : "", HISTOGRAMS[h].name);
    for (uint8_t i = 0; i < HISTOGRAMS[h].edgeCount; ++i) appendf(s, "%s%u", i ? "," : "", unsigned(HISTOGRAMS[h].edges[i]));
    s += ']';
  }
  uint32_t upto = 0;   // what the server echoes back as its ack once it has stored them all
  a.forEach([&](const AnalyticsBucket& b, bool open) {
    if (!open) upto = b.last;
  });
  appendf(s, "},\"closed_upto\":%u", unsigned(upto));
  s += ",\"buckets\":[";
  bool firstBucket = true;
  a.forEach([&](const AnalyticsBucket& b, bool open) {
    appendf(s, "%s{\"first\":%u,\"last\":%u,\"open\":%s,\"powered_start_s\":%u,\"span_s\":%u,\"wall_first\":%u,\"wall_last\":%u",
            firstBucket ? "" : ",", unsigned(b.first), unsigned(b.last), open ? "true" : "false",
            unsigned(b.poweredStart), unsigned(b.span), unsigned(b.wallFirst), unsigned(b.wallLast));
    firstBucket = false;
    // Only what happened: a slot or histogram absent is 0.
    s += ",\"counts\":{";
    bool any = false;
    for (uint8_t i = 0; i < kAnalyticCount; ++i) {
      if (!b.count[i]) continue;
      appendf(s, "%s\"%s%s\":%u", any ? "," : "", ANALYTICS[i].name, ANALYTICS[i].unit == AnalyticUnit::Secs ? "_s" : "",
              unsigned(b.count[i]));
      any = true;
    }
    s += "},\"hist\":{";
    any = false;
    for (uint8_t h = 0; h < kHistogramCount; ++h) {
      const uint8_t bins = uint8_t(HISTOGRAMS[h].edgeCount + 1);
      bool used = false;
      for (uint8_t i = 0; i < bins; ++i) used = used || b.bins[h][i];
      if (!used) continue;
      appendf(s, "%s\"%s\":[", any ? "," : "", HISTOGRAMS[h].name);
      for (uint8_t i = 0; i < bins; ++i) appendf(s, "%s%u", i ? "," : "", unsigned(b.bins[h][i]));
      s += ']';
      any = true;
    }
    s += "}}";
  });
  s += "]}";
}

std::optional<uint32_t> statsAckOf(std::string_view body) {
  constexpr std::string_view kKey = "\"ack\":";
  const size_t at = body.find(kKey);
  if (at == std::string_view::npos) return std::nullopt;
  size_t i = at + kKey.size();
  while (i < body.size() && body[i] == ' ') ++i;
  const size_t from = i;
  uint64_t v = 0;
  for (; i < body.size() && body[i] >= '0' && body[i] <= '9' && i - from < 10; ++i) v = v * 10 + uint64_t(body[i] - '0');
  if (i == from || v > UINT32_MAX) return std::nullopt;
  return uint32_t(v);
}

}  // namespace blorb
