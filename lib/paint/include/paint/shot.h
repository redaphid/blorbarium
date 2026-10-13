// A Canvas240 as serial text lines, so a host can see the screen of a board
// nobody can look at (tools/shot.py turns the lines back into a PNG).
//
//   [hw] shot begin w=240 h=240 rle565
//   [hw] shot <base64 of up to 96 bytes>          as many as it takes
//   [hw] shot end bytes=<encoded bytes> crc=<crc32 of the raw pixels, 8 hex>
//
// The bytes are PackBits over 16-bit pixels, little-endian: a header h < 128
// is a run of h+1 copies of the one pixel after it, h >= 128 is h-127 literal
// pixels. The crc is zlib's crc32 over the decoded pixels, row-major and
// little-endian, so it checks the decoder too, not just the wire. Every line
// carries the "[hw] shot " prefix, so a decoder skips whatever else the board
// prints between them.
//
// The encoder reads the canvas as it goes and holds no copy (a copy is 115 KB
// of a heap that has 136 KB), so the canvas must not be repainted until
// next() has returned nullptr.
#pragma once

#include <cstddef>
#include <cstdint>

#include "paint/canvas.h"

namespace paint {

class ShotEncoder {
 public:
  static constexpr size_t kLineBytes = 96;   // 128 base64 characters a line

  explicit ShotEncoder(const Canvas240& canvas) : canvas_(canvas) {}

  // The next line, without its newline, or nullptr once "end" has been given.
  // Valid until the next call.
  const char* next();

 private:
  void packet();

  enum class Part : uint8_t { Begin, Data, End, Done };
  const Canvas240& canvas_;
  Part part_ = Part::Begin;
  uint32_t pos_ = 0, total_ = 0, crc_ = 0xffffffffu;
  uint8_t bytes_[kLineBytes + 1 + 2 * 128];   // one line's worth plus the largest packet
  size_t held_ = 0;
  char line_[48 + 4 * kLineBytes / 3];
};

}  // namespace paint
