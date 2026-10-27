// Remote debug (docs/remote-debug.md): what a badge far from the owner sends
// home in each Wi-Fi window, and the few things the owner may ask of it.
// Pure, so it is tested natively (test/test_remote_debug); src/remote_debug.h
// is the badge's side.
//
//   LogRing      the serial log's last bytes, mirrored as they are printed
//   jsonEscape   a byte run as the inside of a JSON string
//   parseCommands  the site's reply: only the allowlist, only new ids
//   trustedReplyUrl  commands are taken only from the site, over HTTPS
//
// Commands are authenticated by TLS: the reply comes from an HTTPS POST to the
// site, whose chain the badge checks against its pinned roots and whose name
// it checks against the certificate (hw/net_job.h, hw/tls_roots.h). So only
// the site can send them, and the site queues them only for the owner's
// ADMIN_TOKEN. And none of them can turn updates off, forget a network, or
// touch the firmware: the allowlist has no such verb.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rdebug {

// A byte ring over a buffer the caller owns (PSRAM on the badge). It keeps the
// last cap() bytes written; total() counts every byte ever written, so a
// mark taken before something prints finds exactly what it printed.
class LogRing {
 public:
  void attach(char* buf, size_t cap) { buf_ = buf, cap_ = cap, total_ = 0; }
  bool ready() const { return buf_ && cap_; }
  size_t cap() const { return cap_; }
  uint64_t total() const { return total_; }
  size_t held() const { return total_ < cap_ ? size_t(total_) : cap_; }

  void write(const char* p, size_t n);
  // The last min(cap, held()) bytes, oldest first. Returns how many.
  size_t tail(char* out, size_t cap) const;
  // The bytes written since `mark` (a total() from before) that are still
  // held, oldest first, at most `cap` of them (the first ones). Returns how many.
  size_t since(uint64_t mark, char* out, size_t cap) const;

 private:
  size_t copyFrom(uint64_t from, size_t n, char* out) const;
  char* buf_ = nullptr;
  size_t cap_ = 0;
  uint64_t total_ = 0;
};

// `in` (n bytes) as the inside of a JSON string, into `out` (cap bytes, always
// NUL-terminated). Quotes, backslashes and control characters are escaped,
// \n kept as \n, other bytes go as they are; an escape is never cut. A UTF-8
// sequence cut at the start (a ring's oldest byte) is skipped. Returns the
// length written (without the NUL).
size_t jsonEscape(const char* in, size_t n, char* out, size_t cap);
// The same, but of the newest bytes: the longest end of `in` whose escaped
// form fits `cap` (with its NUL). For a log's tail, whose newest lines matter.
size_t jsonEscapeTail(const char* in, size_t n, char* out, size_t cap);

// ---- the report's size and how it goes out --------------------------------------
//
// On the badge at RSSI -77 a 8.8 KB report failed ("Failed to send chunk!",
// send payload failed) and took internal RAM down to 9.9 KB: it went to TLS
// as one write, so one record and its buffers. Now the report stays under
// kReportMax (the log's tail gets what the rest leaves, at most kLogTailMax),
// and every HTTPS body goes out kWriteChunk bytes a write (hw/net_job.h).
constexpr size_t kReportMax = 5000;
constexpr size_t kLogTailMax = 4096;
constexpr size_t kStateMax = 2048;     // a `state` capture: it shares the cap with the log
constexpr size_t kWriteChunk = 1024;

// `n` bytes through `write(p, k)` (returns the bytes it took, 0 when it
// failed), at most `chunk` a call. A call that takes nothing is tried once
// more after `pause()`; a second failure ends it. Returns the bytes written.
template <class Write, class Pause>
size_t writeChunked(const uint8_t* p, size_t n, size_t chunk, Write&& write, Pause&& pause) {
  size_t done = 0;
  while (done < n) {
    const size_t k = n - done < chunk ? n - done : chunk;
    size_t got = write(p + done, k);
    if (!got) {
      pause();
      got = write(p + done, k);
      if (!got) break;
    }
    done += got < k ? got : k;
  }
  return done;
}

// ---- commands ----------------------------------------------------------------

// The allowlist. Nothing else is ever run.
enum class Verb : uint8_t {
  Unknown,    // not on the list: refused, and the refusal reported
  Check,      // the next update check is due now (it still waits to be dark and alone)
  Interval,   // the time between update checks, in minutes, within kIntervalMin..kIntervalMax, until a restart
  State,      // what the cable's STATE and DEBUG status verbs print, in the next report
  Shot,       // the screen's last frame, in the next report
  Reboot,     // restart, once dark and alone
  Marquee,    // a message for the kids: full-screen scrolling text (showMarquee), at most kMarqueeMax characters
};
const char* verbName(Verb v);

// The interval's bounds: never so short it drains the battery, never so long
// that updates as good as stop (owner: automatic updates never switch off).
constexpr uint32_t kIntervalMinMinutes = 30;
constexpr uint32_t kIntervalMaxMinutes = 12 * 60;

// The marquee's text: printable ASCII (what the badge's font draws), at most
// this many characters; web/src/reports.js clamps to the same.
constexpr size_t kMarqueeMax = 120;

struct Command {
  uint32_t id = 0;
  Verb verb = Verb::Unknown;
  uint32_t arg = 0;
  char text[kMarqueeMax + 1] = "";   // Marquee's
  bool ok = false;           // on the list and its argument in bounds
  const char* why = "";      // when not ok: the refusal, for the report
};

// The site's reply to a report, plain text:
//
//   blorb-cmds 1
//   <id> <verb> [arg]
//   <id> marquee <text to the end of the line>
//   ...
//
// Each line with an id above `lastId` is one command (a repeat or an older
// id is dropped: each runs at most once), in order, at most `max`. A line
// that is not "<id> <word>" is skipped. A verb off the list, or an argument
// out of bounds, comes back as a Command with ok false, so its refusal is
// reported. Returns how many were written to `out`; nothing at all unless
// the first line is the header.
size_t parseCommands(std::string_view reply, uint32_t lastId, Command* out, size_t max);

// Commands are taken only from a reply to this URL's site over HTTPS (a
// cable's `DEBUG ota check http://...` points the manifest elsewhere; the
// report never goes there).
constexpr const char* kSite = "https://blorb.hypnodroid.com/";
bool trustedReplyUrl(const char* url);

}  // namespace rdebug
