#include "rdebug/remote.h"

#include <cstdio>
#include <cstring>

namespace rdebug {

void LogRing::write(const char* p, size_t n) {
  if (!ready() || !n) return;
  if (n >= cap_) {   // only the last cap_ bytes can stay
    p += n - cap_;
    total_ += n - cap_;
    n = cap_;
  }
  const size_t at = size_t(total_ % cap_);
  const size_t first = n < cap_ - at ? n : cap_ - at;
  std::memcpy(buf_ + at, p, first);
  std::memcpy(buf_, p + first, n - first);
  total_ += n;
}

size_t LogRing::copyFrom(uint64_t from, size_t n, char* out) const {
  const size_t at = size_t(from % cap_);
  const size_t first = n < cap_ - at ? n : cap_ - at;
  std::memcpy(out, buf_ + at, first);
  std::memcpy(out + first, buf_, n - first);
  return n;
}

size_t LogRing::tail(char* out, size_t cap) const {
  if (!ready() || !cap) return 0;
  const size_t n = held() < cap ? held() : cap;
  return copyFrom(total_ - n, n, out);
}

size_t LogRing::since(uint64_t mark, char* out, size_t cap) const {
  if (!ready() || !cap || mark >= total_) return 0;
  const uint64_t oldest = total_ - held();
  const uint64_t from = mark > oldest ? mark : oldest;
  const uint64_t avail = total_ - from;
  return copyFrom(from, avail < cap ? size_t(avail) : cap, out);
}

size_t jsonEscape(const char* in, size_t n, char* out, size_t cap) {
  if (!cap) return 0;
  size_t i = 0, o = 0;
  while (i < n && (uint8_t(in[i]) & 0xC0) == 0x80) ++i;   // a sequence cut at the start
  for (; i < n; ++i) {
    const uint8_t c = uint8_t(in[i]);
    char esc[7];
    size_t k = 0;
    if (c == '"' || c == '\\') esc[0] = '\\', esc[1] = char(c), k = 2;
    else if (c == '\n') esc[0] = '\\', esc[1] = 'n', k = 2;
    else if (c == '\t') esc[0] = '\\', esc[1] = 't', k = 2;
    else if (c < 0x20 || c == 0x7f) k = size_t(std::snprintf(esc, sizeof esc, "\\u%04x", unsigned(c)));
    else esc[0] = char(c), k = 1;
    if (o + k + 1 > cap) break;
    std::memcpy(out + o, esc, k);
    o += k;
  }
  out[o] = 0;
  return o;
}

const char* verbName(Verb v) {
  switch (v) {
    case Verb::Check: return "check";
    case Verb::Interval: return "interval";
    case Verb::State: return "state";
    case Verb::Shot: return "shot";
    case Verb::Reboot: return "reboot";
    case Verb::Marquee: return "marquee";
    case Verb::Unknown: break;
  }
  return "unknown";
}

namespace {

bool parseU32(std::string_view s, uint32_t& v) {
  if (s.empty() || s.size() > 9) return false;
  v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + uint32_t(c - '0');
  }
  return true;
}

// The next space-separated word of `s`, consumed.
std::string_view word(std::string_view& s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  size_t n = 0;
  while (n < s.size() && s[n] != ' ') ++n;
  std::string_view w = s.substr(0, n);
  s.remove_prefix(n);
  return w;
}

Command judge(uint32_t id, std::string_view verb, std::string_view arg) {
  Command c;
  c.id = id;
  for (Verb v : {Verb::Check, Verb::Interval, Verb::State, Verb::Shot, Verb::Reboot, Verb::Marquee})
    if (verb == verbName(v)) c.verb = v;
  if (c.verb == Verb::Unknown) return c.why = "not an allowed command", c;
  if (c.verb == Verb::Marquee) {   // `arg` is the rest of the line
    while (!arg.empty() && arg.back() == ' ') arg.remove_suffix(1);
    if (arg.empty()) return c.why = "marquee wants text", c;
    if (arg.size() > kMarqueeMax) return c.why = "marquee text too long (120 characters)", c;
    for (char ch : arg)
      if (ch < 0x20 || ch > 0x7e) return c.why = "marquee text: printable ASCII only", c;
    std::memcpy(c.text, arg.data(), arg.size());
    c.text[arg.size()] = 0;
  } else if (c.verb == Verb::Interval) {
    if (!parseU32(arg, c.arg)) return c.why = "interval wants minutes", c;
    if (c.arg < kIntervalMinMinutes || c.arg > kIntervalMaxMinutes) return c.why = "interval out of bounds (30..720 min)", c;
  } else if (!arg.empty()) {
    return c.why = "takes no argument", c;
  }
  c.ok = true;
  return c;
}

}  // namespace

size_t parseCommands(std::string_view reply, uint32_t lastId, Command* out, size_t max) {
  size_t n = 0;
  bool header = false;
  while (!reply.empty() && n < max) {
    size_t end = reply.find('\n');
    std::string_view line = reply.substr(0, end);
    reply.remove_prefix(end == std::string_view::npos ? reply.size() : end + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!header) {
      if (line != "blorb-cmds 1") return 0;
      header = true;
      continue;
    }
    std::string_view rest = line;
    uint32_t id = 0;
    if (!parseU32(word(rest), id) || !id) continue;
    const std::string_view verb = word(rest);
    if (verb.empty()) continue;
    std::string_view arg;
    if (verb == verbName(Verb::Marquee)) {   // the text: the rest of the line, spaces and all
      while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
      arg = rest;
    } else {
      arg = word(rest);
      if (!word(rest).empty()) continue;   // more than one argument: not a command line
    }
    if (id <= lastId) continue;
    out[n++] = judge(id, verb, arg);
    lastId = id;
  }
  return n;
}

bool trustedReplyUrl(const char* url) { return url && std::strncmp(url, kSite, std::strlen(kSite)) == 0; }

}  // namespace rdebug
