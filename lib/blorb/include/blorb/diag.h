#pragma once
// Diagnostic lines for the debug link (docs/debug-ble.md, `! DEBUG ...`):
// what BOOT did and why a fight did or did not start, queued by the engine
// and drained by the firmware to the cable and the debug characteristic. A
// full queue drops the oldest: a log, never a stall.
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace blorb {

class DiagLines {
 public:
  static constexpr size_t kLine = 96, kDepth = 8;

  void say(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(lines_[head_ % kDepth], kLine, fmt, ap);
    va_end(ap);
    if (++head_ - tail_ > kDepth) tail_ = head_ - kDepth;
  }
  bool take(char (&out)[kLine]) {
    if (tail_ == head_) return false;
    std::memcpy(out, lines_[tail_++ % kDepth], kLine);
    return true;
  }

 private:
  char lines_[kDepth][kLine]{};
  uint32_t head_ = 0, tail_ = 0;
};

}  // namespace blorb
