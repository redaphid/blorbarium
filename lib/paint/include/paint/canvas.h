#pragma once
// The square every drawing here lands in: the round 240x240 panel's pixels,
// RGB565, row-major. Nothing in this header knows about the creature.
#include <cstdint>

namespace paint {

struct Canvas240 { uint16_t px[240 * 240]; };   // round mask applied at draw time

// `over` laid on `under` at alpha/256, mixed in 8-bit channels.
inline uint16_t blend(uint16_t under, uint16_t over, int alpha) {
  auto r = [](uint16_t p) { int v = (p >> 11) & 31; return (v << 3) | (v >> 2); };
  auto g = [](uint16_t p) { int v = (p >> 5) & 63; return (v << 2) | (v >> 4); };
  auto b = [](uint16_t p) { int v = p & 31; return (v << 3) | (v >> 2); };
  const int mr = r(under) + (r(over) - r(under)) * alpha / 256;
  const int mg = g(under) + (g(over) - g(under)) * alpha / 256;
  const int mb = b(under) + (b(over) - b(under)) * alpha / 256;
  return uint16_t(((mr >> 3) << 11) | ((mg >> 2) << 5) | (mb >> 3));
}

}  // namespace paint
