// Reply bytes waiting to leave as BLE notifications. A notify the stack
// refuses (no mbufs, a full connection queue) leaves its chunk at the front
// to try again, so a long reply is delayed, never cut. Free of NimBLE so the
// native suite can drive it with a sink that fails on cue.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace blorb {

template <size_t N>
class NotifyQueue {
 public:
  // The whole line and its '\n', or nothing when they do not fit yet.
  bool push(std::string_view line) {
    if (line.size() + 1 > N - (end_ - begin_)) return false;
    if (begin_ == end_) chunks_ = retries_ = burst_ = 0;
    if (end_ + line.size() + 1 > N) {
      std::memmove(data_, data_ + begin_, end_ - begin_);
      end_ -= begin_;
      begin_ = 0;
    }
    std::memcpy(data_ + end_, line.data(), line.size());
    end_ += line.size();
    data_[end_++] = '\n';
    burst_ += line.size() + 1;
    return true;
  }

  // Sends until a send fails or nothing is left; true once empty. Each line
  // goes as pieces of at most `payload` bytes, then '\n' alone (docs/ble.md
  // "Framing"). `send(const uint8_t*, size_t)` returns whether it was taken.
  template <class Send>
  bool drain(size_t payload, Send&& send) {
    while (begin_ != end_) {
      const uint8_t* at = data_ + begin_;
      size_t len = 1;
      if (*at != '\n') {
        const void* nl = std::memchr(at, '\n', end_ - begin_);
        len = static_cast<const uint8_t*>(nl) - at;
        if (len > payload) len = payload;
      }
      if (!send(at, len)) {
        ++retries_;
        return false;
      }
      ++chunks_;
      begin_ += len;
    }
    return true;
  }

  void clear() { begin_ = end_ = 0; }
  bool empty() const { return begin_ == end_; }

  // Since the queue last ran empty: notifications sent, refusals retried, bytes queued.
  uint32_t chunks() const { return chunks_; }
  uint32_t retries() const { return retries_; }
  size_t burst() const { return burst_; }

 private:
  uint8_t data_[N];
  size_t begin_ = 0, end_ = 0;
  uint32_t chunks_ = 0, retries_ = 0;
  size_t burst_ = 0;
};

}  // namespace blorb
