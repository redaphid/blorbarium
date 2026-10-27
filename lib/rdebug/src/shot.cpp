#include "rdebug/shot.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace rdebug {

void halve565(const uint16_t* src, int w, int h, uint16_t* dst) {
  for (int y = 0; y + 1 < h; y += 2)
    for (int x = 0; x + 1 < w; x += 2) {
      const uint16_t q[4] = {src[y * w + x], src[y * w + x + 1], src[(y + 1) * w + x], src[(y + 1) * w + x + 1]};
      unsigned r = 0, g = 0, b = 0;
      for (uint16_t p : q) r += p >> 11 & 31, g += p >> 5 & 63, b += p & 31;
      dst[(y / 2) * (w / 2) + x / 2] = uint16_t((r + 2) / 4 << 11 | (g + 2) / 4 << 5 | (b + 2) / 4);
    }
}

uint32_t crc565(const uint16_t* px, size_t n) {
  uint32_t c = 0xffffffffu;
  for (size_t i = 0; i < n; ++i)
    for (uint8_t byte : {uint8_t(px[i]), uint8_t(px[i] >> 8)}) {
      c ^= byte;
      for (int k = 0; k < 8; ++k) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
  return ~c;
}

size_t packBits565(const uint16_t* px, size_t n, uint8_t* out, size_t cap) {
  size_t o = 0, i = 0;
  const auto put = [&](uint16_t p) {
    out[o++] = uint8_t(p);
    out[o++] = uint8_t(p >> 8);
  };
  while (i < n) {
    size_t run = 1;
    while (i + run < n && run < 128 && px[i + run] == px[i]) ++run;
    if (run >= 2) {
      if (o + 3 > cap) return 0;
      out[o++] = uint8_t(run - 1);
      put(px[i]);
      i += run;
      continue;
    }
    size_t lit = 1;   // literals until a run of two starts, at most 128
    while (i + lit < n && lit < 128 && !(i + lit + 1 < n && px[i + lit] == px[i + lit + 1])) ++lit;
    if (o + 1 + 2 * lit > cap) return 0;
    out[o++] = uint8_t(127 + lit);
    for (size_t k = 0; k < lit; ++k) put(px[i + k]);
    i += lit;
  }
  return o;
}

size_t shotLines(const uint8_t* packed, size_t n, int w, int h, uint32_t crc, char* out, size_t cap) {
  static constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  const auto room = [&](size_t k) { return o + k < cap; };
  int k = std::snprintf(out, cap, "[hw] shot begin w=%d h=%d rle565\n", w, h);
  if (k < 0 || !room(size_t(k))) return 0;
  o = size_t(k);
  for (size_t at = 0; at < n; at += 96) {
    const size_t len = n - at < 96 ? n - at : 96;
    if (!room(10 + (len + 2) / 3 * 4 + 1)) return 0;
    std::memcpy(out + o, "[hw] shot ", 10);
    o += 10;
    for (size_t i = 0; i < len; i += 3) {
      const uint32_t v = uint32_t(packed[at + i]) << 16 | (i + 1 < len ? uint32_t(packed[at + i + 1]) << 8 : 0) |
                         (i + 2 < len ? packed[at + i + 2] : 0);
      out[o++] = kB64[v >> 18 & 63];
      out[o++] = kB64[v >> 12 & 63];
      out[o++] = i + 1 < len ? kB64[v >> 6 & 63] : '=';
      out[o++] = i + 2 < len ? kB64[v & 63] : '=';
    }
    out[o++] = '\n';
  }
  k = std::snprintf(out + o, cap - o, "[hw] shot end bytes=%u crc=%08x\n", unsigned(n), unsigned(crc));
  if (k < 0 || !room(size_t(k))) return 0;
  return o + size_t(k);
}

}  // namespace rdebug
