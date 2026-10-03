#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "paint/sprite_pack.h"  // named here so PlatformIO's dependency finder links lib/paint
#include "grungo_pack.h"

using namespace blorb;
using paint::FrameRef;

namespace {

const paint::SpritePack& pack = grungoPack();

struct Blob { const uint8_t* data; size_t size; };

// Every RLE array the header defines. A frame the interface hands out that is not
// listed here fails Lookup, so a new frame cannot dodge the stream checks.
const std::vector<Blob>& blobs() {
  using namespace grungo_pack;
  static const std::vector<Blob> all = {
#define B(n) {n, sizeof(n)}
      B(kBodyRle), B(kFace_neutralRle), B(kFace_happyRle), B(kFace_alarmedRle), B(kFace_annoyedRle),
      B(kFace_croakRle), B(kFace_blepRle), B(kFace_foreseeRle), B(kFace_sleepyRle), B(kFace_asleepRle),
      B(kFace_yawnRle), B(kEgg0Rle), B(kEgg1Rle), B(kEgg2Rle), B(kRemainsRle), B(kItemPelletRle),
      B(kItemRottenRle), B(kItemMarbleRle),
#undef B
  };
  return all;
}

size_t streamSize(const FrameRef& f) {
  for (const Blob& b : blobs()) if (b.data == f.rle) return b.size;
  return 0;
}

// Decodes a frame, or returns why its stream is malformed.
std::string decode(const FrameRef& f, std::vector<uint8_t>& px) {
  size_t size = streamSize(f);
  if (!size) return "rle points at no known array";
  px.clear();
  size_t i = 0;
  for (uint16_t y = 0; y < f.h; ++y) {
    uint32_t x = 0;
    while (x < f.w) {
      if (i + 2 > size) return "stream ends inside row " + std::to_string(y);
      uint8_t run = f.rle[i], index = f.rle[i + 1];
      i += 2;
      if (run == 0) return "zero run in row " + std::to_string(y);
      x += run;
      px.insert(px.end(), run, index);
    }
    if (x != f.w) return "row " + std::to_string(y) + " runs sum to " + std::to_string(x);
  }
  if (i != size) return std::to_string(size - i) + " bytes after the last row";
  return "";
}

uint8_t regionAt(const FrameRef& f, int x, int y) {
  std::vector<uint8_t> px;
  EXPECT_EQ(decode(f, px), "");
  uint16_t count = 0;
  const paint::PaletteEntry* pal = pack.palette(count);
  return pal[px[size_t(y) * f.w + size_t(x)]].region;
}

std::vector<std::pair<std::string, FrameRef>> everyFrame() {
  std::vector<std::pair<std::string, FrameRef>> out;
  for (const PoseInfo& p : POSES)
    for (uint8_t s = 0; s < kStageCount; ++s) out.push_back({std::string("body ") + p.name, pack.body(p.id, Stage(s), 0)});
  for (const ExprInfo& e : EXPRESSIONS)
    for (uint8_t s = 0; s < kStageCount; ++s) out.push_back({std::string("face ") + e.name, pack.face(e.id, Stage(s))});
  for (int pct : {0, 30, 59, 60, 70, 84, 85, 100}) out.push_back({"egg " + std::to_string(pct), pack.egg(Fx::ratio(pct, 100))});
  out.push_back({"remains", pack.remains()});
  for (auto w : {Appearance::Item::What::Pellet, Appearance::Item::What::RottenPellet, Appearance::Item::What::Marble})
    out.push_back({"item " + std::to_string(int(w)), pack.item(w)});
  return out;
}

bool seesThere(const FrameRef& f, int x, int y) {
  uint8_t r = regionAt(f, x, y);
  return r == region::eye.v || r == region::glow.v;
}

}  // namespace

TEST(GrungoPack, EveryFrameStreamIsWellFormed) {
  uint16_t count = 0;
  pack.palette(count);
  for (auto& [name, f] : everyFrame()) {
    ASSERT_GT(f.w, 0) << name;
    ASSERT_GT(f.h, 0) << name;
    std::vector<uint8_t> px;
    ASSERT_EQ(decode(f, px), "") << name;
    for (uint8_t i : px) ASSERT_LT(i, count) << name << " uses an index past the palette";
  }
}

TEST(GrungoPack, PaletteFitsAndEveryEntryHasARegion) {
  uint16_t count = 0;
  const paint::PaletteEntry* pal = pack.palette(count);
  ASSERT_GE(count, 2);
  ASSERT_LE(count, 256);
  std::vector<bool> used(kRegionCount, false);
  for (uint16_t i = 0; i < count; ++i) {
    ASSERT_TRUE(pal[i].region == 255 || pal[i].region < kRegionCount) << "entry " << i;
    if (pal[i].region < kRegionCount) used[pal[i].region] = true;
  }
  for (const RegionInfo& r : REGIONS) EXPECT_TRUE(used[r.id.v]) << r.name << " has no palette entries to recolour";
}

TEST(GrungoPack, EveryRegionHasABandAroundItsAuthoredColour) {
  for (const RegionInfo& r : REGIONS) {
    paint::RegionBand b = pack.band(r.id);
    EXPECT_LE(b.hueMin, 0) << r.name;
    EXPECT_GE(b.hueMax, 0) << r.name;
    EXPECT_TRUE(b.satMin <= 128 && 128 <= b.satMax) << r.name;
    EXPECT_TRUE(b.valMin <= 128 && 128 <= b.valMax) << r.name;
  }
}

TEST(GrungoPack, EveryAnchorSitsOnAnEyeOrGlowPixelOfItsFrame) {
  int anchors = 0;
  for (auto& [name, f] : everyFrame()) {
    for (uint8_t i = 0; i < f.eyeCount; ++i, ++anchors) {
      const paint::EyeAnchor& e = f.eyes[i];
      ASSERT_TRUE(e.x >= 0 && e.x < f.w && e.y >= 0 && e.y < f.h) << name << " anchor " << int(i);
      EXPECT_GT(e.r, 0) << name;
      EXPECT_TRUE(seesThere(f, e.x, e.y)) << name << " anchor " << int(i) << " at " << e.x << "," << e.y;
    }
  }
  EXPECT_GT(anchors, 0);
  EXPECT_EQ(pack.body(pose::idle, Stage::Adult, 0).eyeCount, 2);
  EXPECT_EQ(pack.egg(Fx::zero()).eyeCount, 2);
}

TEST(GrungoPack, FacePatchesRegisterInsideTheBody) {
  FrameRef body = pack.body(pose::idle, Stage::Adult, 0);
  for (const ExprInfo& e : EXPRESSIONS) {
    FrameRef p = pack.face(e.id, Stage::Adult);
    int x0 = body.originX - p.originX, y0 = body.originY - p.originY;
    EXPECT_TRUE(x0 >= 0 && y0 >= 0 && x0 + p.w <= body.w && y0 + p.h <= body.h)
        << e.name << " lands at " << x0 << "," << y0 << " size " << p.w << "x" << p.h;
  }
}

TEST(GrungoPack, BodyAnchorsStillFindTheEyesThroughOpenEyedPatches) {
  FrameRef body = pack.body(pose::idle, Stage::Adult, 0);
  for (ExprId id : {expr::neutral, expr::blep, expr::foresee}) {
    FrameRef p = pack.face(id, Stage::Adult);
    ASSERT_EQ(p.eyeCount, 0) << EXPRESSIONS[id.v].name;
    for (uint8_t i = 0; i < body.eyeCount; ++i) {
      int x = body.eyes[i].x - (body.originX - p.originX), y = body.eyes[i].y - (body.originY - p.originY);
      EXPECT_TRUE(seesThere(p, x, y)) << EXPRESSIONS[id.v].name << " eye " << int(i);
    }
  }
}

TEST(GrungoPack, EveryExpressionHasItsOwnPatchAndUnknownOnesShowNeutral) {
  FrameRef neutral = pack.face(expr::neutral, Stage::Adult);
  for (const ExprInfo& e : EXPRESSIONS) {
    if (e.id == expr::neutral) continue;
    EXPECT_NE(pack.face(e.id, Stage::Adult).rle, neutral.rle) << e.name << " falls back to neutral";
  }
  EXPECT_EQ(pack.face(ExprId{200}, Stage::Adult).rle, neutral.rle);
}

TEST(GrungoPack, TheEggCracksPastSixTenths) {
  EXPECT_EQ(pack.egg(Fx::ratio(59, 100)).rle, pack.egg(Fx::zero()).rle);
  EXPECT_NE(pack.egg(Fx::ratio(60, 100)).rle, pack.egg(Fx::zero()).rle);
  EXPECT_NE(pack.egg(Fx::one()).rle, pack.egg(Fx::ratio(60, 100)).rle);
}

TEST(GrungoPack, TheBodyIsOneHundredTwentyPixelsTallStandingOnItsFeet) {
  FrameRef body = pack.body(pose::idle, Stage::Adult, 0);
  EXPECT_EQ(body.h, 120);
  EXPECT_EQ(body.originY, body.h - 1);
  std::vector<uint8_t> px;
  ASSERT_EQ(decode(body, px), "");
  int left = body.w, right = -1;
  for (int x = 0; x < body.w; ++x)
    if (px[size_t(body.originY) * body.w + size_t(x)]) { left = std::min(left, x); right = x; }
  EXPECT_TRUE(left < body.originX && body.originX < right) << "feet span " << left << ".." << right;
  EXPECT_LT(std::abs((left + right) - 2 * body.originX), 2) << "the origin is not centred between the feet";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
