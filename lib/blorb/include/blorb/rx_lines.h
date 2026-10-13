// Bytes a radio task writes and the loop reads as lines, each line tagged
// with the connection it came on. A new connection drops whatever the last
// one left unread, half a line included, so no line is ever made of two
// connections' bytes or credited to the wrong one. Free of NimBLE so the
// native suite can drive it.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace blorb {

template <size_t N>
class RxLines {
  static_assert(N < 0x10000, "positions are packed into 16 bits beside the connection");

 public:
  struct Line {
    std::string_view text;
    uint32_t connection;
  };

  // Radio task: bytes from here on belong to `connection`.
  void connected(uint32_t connection) {
    mark_.store(uint32_t(connection & 0xffff) << 16 | uint32_t(head_.load(std::memory_order_relaxed)),
                std::memory_order_release);
  }
  // Radio task. A full ring drops the rest, so that line reads as garbled.
  void push(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      const size_t head = head_.load(std::memory_order_relaxed), next = (head + 1) % N;
      if (next == tail_.load(std::memory_order_acquire)) return;
      ring_[head] = char(p[i]);
      head_.store(next, std::memory_order_release);
    }
  }

  // Loop: the next whole line, '\r' dropped. Longer than the buffer keeps
  // its first bytes, so an overlong line still reads as overlong.
  std::optional<Line> line() {
    const uint32_t mark = mark_.load(std::memory_order_acquire);
    if (mark != seenMark_) {
      seenMark_ = mark;
      connection_ = mark >> 16;
      tail_.store(mark & 0xffff, std::memory_order_release);   // the last connection's unread bytes
      len_ = 0;                                                 // and its half line
    }
    // The radio task marks a connection before it pushes any of its bytes.
    // An unchanged mark after reading the head means every byte up to that
    // head is this connection's; a changed one is taken on the next call.
    const size_t head = head_.load(std::memory_order_acquire);
    if (mark_.load(std::memory_order_acquire) != mark) return std::nullopt;
    size_t tail = tail_.load(std::memory_order_relaxed);
    while (tail != head) {
      const char c = ring_[tail];
      tail = (tail + 1) % N;
      tail_.store(tail, std::memory_order_release);
      if (c == '\r') continue;
      if (c != '\n') {
        if (len_ < sizeof line_) line_[len_++] = c;
        continue;
      }
      const size_t n = len_;
      len_ = 0;
      return Line{std::string_view(line_, n), connection_};
    }
    return std::nullopt;
  }

 private:
  char ring_[N];
  std::atomic<size_t> head_{0}, tail_{0};
  std::atomic<uint32_t> mark_{0};
  uint32_t seenMark_ = 0, connection_ = 0;
  char line_[256];   // past the protocol's 200, so an overlong line still reads as overlong
  size_t len_ = 0;
};

}  // namespace blorb
