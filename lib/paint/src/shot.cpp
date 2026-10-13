#include "paint/shot.h"

#include <cstdio>
#include <cstring>

namespace paint {

namespace {

constexpr uint32_t kPixels = 240 * 240;

uint32_t crcByte(uint32_t crc, uint8_t b) {
  crc ^= b;
  for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  return crc;
}

size_t base64(const uint8_t* in, size_t n, char* out) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < n ? uint32_t(in[i + 1]) << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = kAlphabet[v >> 18 & 63];
    out[o++] = kAlphabet[v >> 12 & 63];
    out[o++] = i + 1 < n ? kAlphabet[v >> 6 & 63] : '=';
    out[o++] = i + 2 < n ? kAlphabet[v & 63] : '=';
  }
  out[o] = 0;
  return o;
}

}  // namespace

// One PackBits packet from pos_. Two equal pixels already pay for a run
// (3 bytes against 4), so a literal stops where a pair begins.
void ShotEncoder::packet() {
  const uint16_t* px = canvas_.px;
  uint32_t n = 1;
  while (n < 128 && pos_ + n < kPixels && px[pos_ + n] == px[pos_]) ++n;
  const bool run = n >= 2;
  if (!run)
    while (n < 128 && pos_ + n < kPixels && !(pos_ + n + 1 < kPixels && px[pos_ + n] == px[pos_ + n + 1])) ++n;
  bytes_[held_++] = uint8_t(run ? n - 1 : n + 127);
  for (uint32_t i = 0; i < n; ++i) {
    const uint16_t p = px[pos_ + i];
    crc_ = crcByte(crcByte(crc_, uint8_t(p)), uint8_t(p >> 8));
    if (run && i > 0) continue;
    bytes_[held_++] = uint8_t(p);
    bytes_[held_++] = uint8_t(p >> 8);
  }
  pos_ += n;
}

const char* ShotEncoder::next() {
  switch (part_) {
    case Part::Begin:
      part_ = Part::Data;
      return "[hw] shot begin w=240 h=240 rle565";
    case Part::Data: {
      while (held_ < kLineBytes && pos_ < kPixels) packet();
      if (held_ == 0) {
        part_ = Part::End;
        return next();
      }
      const size_t take = held_ < kLineBytes ? held_ : kLineBytes;
      std::memcpy(line_, "[hw] shot ", 10);
      base64(bytes_, take, line_ + 10);
      std::memmove(bytes_, bytes_ + take, held_ - take);
      held_ -= take;
      total_ += uint32_t(take);
      return line_;
    }
    case Part::End:
      part_ = Part::Done;
      std::snprintf(line_, sizeof line_, "[hw] shot end bytes=%u crc=%08x", unsigned(total_), unsigned(~crc_));
      return line_;
    case Part::Done:
      break;
  }
  return nullptr;
}

}  // namespace paint
