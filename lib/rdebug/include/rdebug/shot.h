// Remote debug's `shot` (docs/remote-debug.md): the screen's last frame, made
// small enough to go home safely. A 240x240 frame as the cable's shot lines
// was 132 KB in the report and failed twice on the badge with internal RAM
// down to 2.6 KB. Now it is halved to 120x120 (each pixel the mean of four),
// PackBits-coded in the cable's own shot-line format (paint/shot.h, so
// tools/shot.py and /admin read it as they read the cable's), at most
// kShotMax bytes, and sent in its own POST, once.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rdebug {

constexpr int kShotSide = 120;
constexpr size_t kShotMax = 48 * 1024;   // 120x120 at worst (no runs): 28.9 KB packed, 38.5 KB of base64, the prefixes

// `src` (w x h RGB565, w and h even) to `dst` ((w/2) x (h/2)): each pixel the
// mean of the four it covers, channel by channel, rounded.
void halve565(const uint16_t* src, int w, int h, uint16_t* dst);

// zlib's crc32 over the pixels as little-endian bytes, as the shot's end line carries.
uint32_t crc565(const uint16_t* px, size_t n);

// PackBits over 16-bit pixels, little-endian: a header h < 128 is a run of h+1
// copies of the one pixel after it, h >= 128 is h-127 literal pixels. Returns
// the bytes written, 0 when `cap` is too small.
size_t packBits565(const uint16_t* px, size_t n, uint8_t* out, size_t cap);

// The shot lines, '\n' after each:
//   [hw] shot begin w=<w> h=<h> rle565
//   [hw] shot <base64 of up to 96 packed bytes>    as many as it takes
//   [hw] shot end bytes=<packed bytes> crc=<crc565, 8 hex>
// Returns the length written (NUL-terminated), 0 when `cap` is too small.
size_t shotLines(const uint8_t* packed, size_t n, int w, int h, uint32_t crc, char* out, size_t cap);

// One screenshot waiting to go: taken by the `shot` command, sent in a window
// of its own POST, and gone after one try, whatever came of it. It never
// rides in a report, and is never tried twice.
class ShotSlot {
 public:
  // Internal RAM a shot's POST wants free (its GET task's stack, the TLS
  // handshake's buffers); short of it, it waits for a later window.
  static constexpr uint32_t kNeedFree = 24 * 1024, kNeedBlock = 9 * 1024;

  void hold(uint32_t id, size_t bytes) { id_ = id, bytes_ = bytes; }
  bool waiting() const { return bytes_ > 0; }
  uint32_t id() const { return id_; }
  size_t bytes() const { return bytes_; }
  // Whether to send it now, with this much internal RAM free and this largest block.
  bool go(uint32_t free, uint32_t block) const { return waiting() && free >= kNeedFree && block >= kNeedBlock; }
  // It went, or it failed: either way it is gone.
  void tried() { bytes_ = 0; }

 private:
  uint32_t id_ = 0;
  size_t bytes_ = 0;
};

}  // namespace rdebug
