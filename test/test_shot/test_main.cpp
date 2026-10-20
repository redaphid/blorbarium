#include <gtest/gtest.h>
#include "blorb/appearance.h"   // first, so PlatformIO's dependency finder links lib/blorb (lib/paint needs it)
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "paint/shot.h"

using paint::Canvas240;
using paint::ShotEncoder;

namespace {

constexpr int kPixels = 240 * 240;
constexpr const char* kFixture = "test/fixtures/shot_v1.txt";
constexpr const char* kPrefix = "[hw] shot ";

std::vector<std::string> encode(const Canvas240& c) {
  ShotEncoder enc(c);
  std::vector<std::string> lines;
  while (const char* l = enc.next()) lines.push_back(l);
  return lines;
}

std::vector<uint8_t> unbase64(const std::string& s) {
  std::vector<uint8_t> out;
  uint32_t v = 0;
  int bits = 0;
  for (char ch : s) {
    const char* at = std::strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", ch);
    if (ch == '=' || !at) continue;
    v = v << 6 | uint32_t(at - "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
    bits += 6;
    if (bits >= 8) { bits -= 8; out.push_back(uint8_t(v >> bits)); }
  }
  return out;
}

uint32_t crc32(const std::vector<uint16_t>& px) {
  uint32_t crc = 0xffffffffu;
  for (uint16_t p : px)
    for (uint8_t b : {uint8_t(p), uint8_t(p >> 8)}) {
      crc ^= b;
      for (int k = 0; k < 8; ++k) crc = crc & 1 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
  return ~crc;
}

// What tools/shot.py does, in C++: the lines back to pixels, checked against
// the byte count and crc the end line carries.
struct Decoded { std::vector<uint16_t> px; uint32_t bytes = 0, crc = 0; bool ended = false; };

Decoded decode(const std::vector<std::string>& lines) {
  Decoded d;
  std::vector<uint8_t> data;
  for (const std::string& l : lines) {
    const std::string body = l.substr(std::strlen(kPrefix));
    if (body.rfind("begin", 0) == 0) continue;
    unsigned bytes = 0, crc = 0;
    if (std::sscanf(body.c_str(), "end bytes=%u crc=%x", &bytes, &crc) == 2) {
      d.bytes = bytes, d.crc = crc, d.ended = true;
      break;
    }
    const std::vector<uint8_t> chunk = unbase64(body);
    data.insert(data.end(), chunk.begin(), chunk.end());
  }
  EXPECT_EQ(data.size(), d.bytes);
  for (size_t i = 0; i < data.size();) {
    const uint8_t h = data[i++];
    const size_t n = h < 128 ? h + 1 : h - 127;
    for (size_t k = 0; k < n; ++k) {
      const size_t at = h < 128 ? i : i + 2 * k;
      d.px.push_back(uint16_t(data[at] | data[at + 1] << 8));
    }
    i += h < 128 ? 2 : 2 * n;
  }
  return d;
}

// Flat areas, runs of exactly 1, 2, 128 and 129, a gradient that never
// repeats, and a lone odd pixel last: each edge of PackBits a slip would hit.
std::unique_ptr<Canvas240> sampler() {
  auto c = std::make_unique<Canvas240>();
  for (int i = 0; i < kPixels; ++i) c->px[i] = 0x0841;
  int at = 240 * 10;
  for (int run : {1, 2, 1, 128, 129, 3, 1})
    for (int k = 0; k < run; ++k) c->px[at++] = uint16_t(0xf800 + run);
  for (int i = 0; i < 240 * 4; ++i) c->px[240 * 40 + i] = uint16_t(i * 7);
  for (int y = 100; y < 116; ++y)
    for (int x = 100; x < 116; ++x) c->px[y * 240 + x] = uint16_t(0x07e0 ^ (x * 31 + y * 17));
  c->px[kPixels - 1] = 0xffff;
  return c;
}

void expectRoundTrip(const Canvas240& c) {
  const std::vector<std::string> lines = encode(c);
  ASSERT_GE(lines.size(), 2u);
  EXPECT_EQ(lines.front(), "[hw] shot begin w=240 h=240 rle565");
  for (const std::string& l : lines) {
    EXPECT_EQ(l.rfind(kPrefix, 0), 0u) << l;
    EXPECT_LE(l.size(), 140u) << "a line the board writes whole, in one blocking write";
  }
  const Decoded d = decode(lines);
  ASSERT_TRUE(d.ended);
  ASSERT_EQ(d.px.size(), size_t(kPixels));
  EXPECT_EQ(std::memcmp(d.px.data(), c.px, sizeof c.px), 0);
  EXPECT_EQ(d.crc, crc32(d.px));
}

}  // namespace

TEST(Shot, FlatCanvasIsAFewHundredBytes) {
  auto c = std::make_unique<Canvas240>();
  for (int i = 0; i < kPixels; ++i) c->px[i] = 0x1234;
  expectRoundTrip(*c);
  EXPECT_EQ(decode(encode(*c)).bytes, 450u * 3) << "57,600 pixels are 450 full runs of 128";
}

TEST(Shot, NoiseRoundTripsWithinTheLiteralBound) {
  auto c = std::make_unique<Canvas240>();
  uint32_t x = 12345;
  for (int i = 0; i < kPixels; ++i) {
    x = x * 1103515245u + 12345u;
    c->px[i] = uint16_t(x >> 16);
  }
  expectRoundTrip(*c);
  EXPECT_LE(decode(encode(*c)).bytes, 2u * kPixels + kPixels / 128 + 1);
}

TEST(Shot, EveryPackBitsEdgeRoundTrips) { expectRoundTrip(*sampler()); }

TEST(Shot, NextIsNullAfterTheEndAndStaysNull) {
  auto c = sampler();
  ShotEncoder enc(*c);
  const char* last = nullptr;
  while (const char* l = enc.next()) last = l;
  ASSERT_NE(last, nullptr);
  EXPECT_EQ(std::string(last).rfind("[hw] shot end ", 0), 0u);
  EXPECT_EQ(enc.next(), nullptr);
}

// The wire format, held byte for byte: tools/shot.py decodes this very file in
// tests/test_shot.py, so the board's encoder and the host's decoder cannot
// drift apart unnoticed. BLORB_WRITE_FIXTURE=1 rewrites it, for a deliberate
// format change.
TEST(Shot, TheCommittedFixtureIsWhatTheEncoderWrites) {
  std::ostringstream want;
  for (const std::string& l : encode(*sampler())) want << l << '\n';
  if (std::getenv("BLORB_WRITE_FIXTURE")) std::ofstream(kFixture) << want.str();
  std::ifstream f(kFixture);
  ASSERT_TRUE(f) << kFixture << " is missing (run from the project root)";
  std::stringstream got;
  got << f.rdbuf();
  EXPECT_EQ(got.str(), want.str());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
