#include <cstring>
#include <vector>
#include "blorb/registry.h"
#include "paint/sprite_pack.h"

namespace paint {
namespace {

using blorb::ExprId;
using blorb::Fx;
using blorb::PoseId;
using blorb::RegionId;
using blorb::Stage;
namespace expr = blorb::expr;
namespace region = blorb::region;

enum Ix : uint8_t {
  kClear, kOutline, kSkinDark, kSkin, kSkinLight, kBelly, kBellyLight,
  kCloakDark, kCloak, kCloakLight, kIrisDark, kIris, kPupil, kShine,
  kMouth, kTongue, kGlowDark, kGlow, kGlowLight, kShellDark, kShell, kShellLight,
  kSclera, kPellet, kPelletLight, kRot, kRotSpot, kMarble, kMarbleLight, kIndexCount
};

constexpr uint8_t kInvariant = 255;
constexpr PaletteEntry kPalette[kIndexCount] = {
    {0, 0, 0, kInvariant},
    {34, 24, 20, kInvariant},
    {66, 104, 40, region::skin.v}, {102, 146, 56, region::skin.v}, {148, 186, 88, region::skin.v},
    {178, 204, 124, region::belly.v}, {210, 228, 164, region::belly.v},
    {72, 44, 28, region::cloak.v}, {112, 72, 44, region::cloak.v}, {152, 102, 64, region::cloak.v},
    {104, 32, 22, region::eye.v}, {168, 64, 36, region::eye.v},
    {18, 10, 10, kInvariant}, {250, 246, 236, kInvariant},
    {112, 36, 40, region::mouth.v}, {206, 96, 108, region::mouth.v},
    {18, 140, 140, region::glow.v}, {56, 224, 206, region::glow.v}, {186, 255, 244, region::glow.v},
    {88, 128, 84, region::shell.v}, {136, 178, 124, region::shell.v}, {198, 228, 180, region::shell.v},
    {236, 230, 212, kInvariant},
    {196, 134, 62, kInvariant}, {236, 186, 104, kInvariant},
    {112, 112, 62, kInvariant}, {58, 68, 30, kInvariant},
    {84, 140, 220, kInvariant}, {206, 232, 255, kInvariant},
};

// Hue in 1/256 turn, sat and val x/128. Skin stays a frog green, the cloak a
// cloak colour (brown through rust to moss), the eyes red-brown through gold.
constexpr RegionBand kBands[] = {
    {-20, 24, 80, 170, 90, 150},   // skin
    {-10, 10, 60, 160, 100, 140},  // belly
    {-16, 36, 60, 170, 70, 150},   // cloak
    {0, 28, 90, 160, 90, 160},     // eye
    {-8, 8, 100, 150, 100, 140},   // mouth
    {-40, 40, 80, 180, 100, 180},  // glow
    {-24, 24, 70, 170, 90, 150},   // shell
};
static_assert(sizeof(kBands) / sizeof(kBands[0]) == blorb::kRegionCount, "one band per region");

// A paletted image to rasterize into before run-length encoding.
struct Raster {
  int w, h;
  std::vector<uint8_t> px;
  Raster(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * size_t(h_), kClear) {}

  uint8_t at(int x, int y) const { return (x >= 0 && y >= 0 && x < w && y < h) ? px[size_t(y * w + x)] : uint8_t(kClear); }
  void set(int x, int y, uint8_t c) {
    if (x >= 0 && y >= 0 && x < w && y < h) px[size_t(y * w + x)] = c;
  }

  // A filled ellipse lit from the upper left: `dark` on the lower right rim,
  // `light` on the upper left, `mid` between (0 = use mid). Rows outside
  // [yMin, yMax] are left alone, which cuts caps and hoods.
  void ellipse(int cx, int cy, int rx, int ry, uint8_t mid, uint8_t dark = 0, uint8_t light = 0,
               int yMin = -1000, int yMax = 1000) {
    const int64_t rr = int64_t(rx) * rx * ry * ry;
    for (int y = cy - ry; y <= cy + ry; ++y) {
      if (y < yMin || y > yMax) continue;
      for (int x = cx - rx; x <= cx + rx; ++x) {
        int64_t dx = x - cx, dy = y - cy;
        if (dx * dx * ry * ry + dy * dy * rx * rx > rr) continue;
        int64_t lean = dx * ry + dy * rx;   // toward the lower right
        uint8_t c = mid;
        if (dark && lean * 100 > int64_t(55) * rx * ry) c = dark;
        else if (light && lean * 100 < -int64_t(60) * rx * ry) c = light;
        set(x, y, c);
      }
    }
  }
  void disc(int cx, int cy, int r, uint8_t c) { ellipse(cx, cy, r, r, c); }

  void line(int x0, int y0, int x1, int y1, uint8_t c, int thick = 1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
      for (int t = 0; t < thick; ++t) set(x0 + t, y0, c);
      if (x0 == x1 && y0 == y1) return;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  void polyline(const int (*pts)[2], int n, uint8_t c, int thick = 1) {
    for (int i = 0; i + 1 < n; ++i) line(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], c, thick);
  }

  // The cel outline: every clear pixel touching the figure becomes outline.
  void outline() {
    std::vector<uint8_t> out = px;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        if (at(x, y) == kClear && (at(x - 1, y) || at(x + 1, y) || at(x, y - 1) || at(x, y + 1)))
          out[size_t(y * w + x)] = kOutline;
    px.swap(out);
  }
};

enum class Eyes : uint8_t { Open, Wide, Seeing, HalfLid, LowLid, Shut };
enum class Mouth : uint8_t { Smile, Flat, Open, Wide, Gape, Small, Blep };
struct FaceSpec { ExprId id; Eyes eyes; Mouth mouth; };
constexpr FaceSpec kFaces[] = {   // row 0 is the fallback for a face the pack lacks
    {expr::neutral, Eyes::Open, Mouth::Smile},   {expr::happy, Eyes::Open, Mouth::Open},
    {expr::alarmed, Eyes::Wide, Mouth::Small},   {expr::annoyed, Eyes::LowLid, Mouth::Flat},
    {expr::croak, Eyes::Open, Mouth::Wide},      {expr::blep, Eyes::Open, Mouth::Blep},
    {expr::foresee, Eyes::Seeing, Mouth::Smile}, {expr::sleepy, Eyes::HalfLid, Mouth::Flat},
    {expr::asleep, Eyes::Shut, Mouth::Flat},     {expr::yawn, Eyes::Shut, Mouth::Gape},
};
constexpr int kFaceCount = int(sizeof(kFaces) / sizeof(kFaces[0]));

// The adult: 112 x 120 (the contract's 120 px at scalePct 100), feet centre at the bottom row.
constexpr int kAdultW = 112, kAdultH = 120, kFeetX = 56, kFeetY = 119;
constexpr int kEyeL = 38, kEyeR = 74, kEyeY = 30, kEyeR0 = 8;
// The face patch: the part of the head any expression redraws.
constexpr int kPatchX = 24, kPatchY = 16, kPatchW = 65, kPatchH = 51;

void drawEye(Raster& r, int cx, int cy, Eyes style) {
  switch (style) {
    case Eyes::Wide:
      r.disc(cx, cy, kEyeR0 + 1, kOutline);
      r.disc(cx, cy, kEyeR0, kSclera);
      r.ellipse(cx, cy, 4, 4, kIris, kIrisDark);
      r.disc(cx, cy, 1, kPupil);
      r.set(cx - 2, cy - 2, kShine);
      return;
    case Eyes::Seeing:
      r.disc(cx, cy, kEyeR0, kOutline);
      r.disc(cx, cy, kEyeR0 - 1, kGlowLight);
      r.ellipse(cx, cy, kEyeR0 - 2, kEyeR0 - 2, kGlow, kGlowDark);
      r.disc(cx - 1, cy - 1, 2, kGlowLight);
      return;
    case Eyes::Shut:
      r.disc(cx, cy, kEyeR0, kOutline);
      r.ellipse(cx, cy, kEyeR0 - 1, kEyeR0 - 1, kSkin, kSkinDark, kSkinLight);
      for (int x = -5; x <= 5; ++x) r.set(cx + x, cy + 1 + (x * x) / 9, kOutline);
      return;
    default: break;
  }
  r.disc(cx, cy, kEyeR0, kOutline);
  r.ellipse(cx, cy, kEyeR0 - 1, kEyeR0 - 1, kIris, kIrisDark);
  r.disc(cx, cy + 1, 3, kPupil);
  r.set(cx - 3, cy - 3, kShine); r.set(cx - 2, cy - 3, kShine); r.set(cx - 3, cy - 2, kShine);
  if (style == Eyes::HalfLid || style == Eyes::LowLid) {
    int lid = style == Eyes::HalfLid ? cy - 1 : cy + 1;
    r.ellipse(cx, cy, kEyeR0 - 1, kEyeR0 - 1, kSkin, kSkinDark, kSkinLight, -1000, lid);
    for (int x = -kEyeR0 + 1; x <= kEyeR0 - 1; ++x) r.set(cx + x, lid, kOutline);
  }
}

void drawMouth(Raster& r, Mouth m) {
  const int cx = kFeetX, cy = 52;
  switch (m) {
    case Mouth::Smile:
      for (int x = -10; x <= 10; ++x) r.set(cx + x, cy + 2 - (x * x) / 34, kOutline);
      for (int x = -7; x <= 7; ++x) r.set(cx + x, cy + 3 - (x * x) / 34, kMouth);
      return;
    case Mouth::Flat:
      for (int x = -7; x <= 7; ++x) r.set(cx + x, cy + 2, kOutline);
      return;
    case Mouth::Open:
    case Mouth::Wide: {
      int rx = m == Mouth::Wide ? 11 : 9, ry = m == Mouth::Wide ? 6 : 5;
      r.ellipse(cx, cy + 3, rx + 1, ry + 1, kOutline);
      r.ellipse(cx, cy + 3, rx, ry, kMouth, 0, 0, cy - 1, 1000);
      for (int x = -rx; x <= rx; ++x) r.set(cx + x, cy + 3 - ry, kOutline);
      r.ellipse(cx, cy + 5, rx - 3, ry - 3, kTongue);
      return;
    }
    case Mouth::Gape:
      r.ellipse(cx, cy + 3, 6, 8, kOutline);
      r.ellipse(cx, cy + 3, 5, 7, kMouth);
      r.ellipse(cx, cy + 7, 3, 2, kTongue);
      return;
    case Mouth::Small:
      r.disc(cx, cy + 4, 3, kOutline);
      r.disc(cx, cy + 4, 2, kMouth);
      return;
    case Mouth::Blep:
      drawMouth(r, Mouth::Smile);
      r.ellipse(cx + 4, cy + 6, 3, 3, kOutline);
      r.ellipse(cx + 4, cy + 6, 2, 2, kTongue);
      return;
  }
}

// The whole adult, front on: antenna stalks, a hood framing a wide olive
// face with big eyes on top, a cloak and scarf, a pale belly, splayed feet.
Raster adult(const FaceSpec& face, bool curl) {
  Raster r(kAdultW, kAdultH);
  r.line(44, 22, 39, 6, kSkin, 2);
  r.line(67, 22, 72, 6, kSkin, 2);
  r.ellipse(39, 5, 4, 4, kSkin, kSkinDark, kSkinLight);
  r.ellipse(72, 5, 4, 4, kSkin, kSkinDark, kSkinLight);
  r.ellipse(56, curl ? 80 : 84, 50, curl ? 34 : 30, kCloak, kCloakDark, kCloakLight);
  r.ellipse(56, 40, curl ? 50 : 44, 30, kCloak, kCloakDark, kCloakLight);
  if (curl) {   // arms up, clutching the hood's edges
    r.ellipse(14, 66, 8, 13, kSkin, kSkinDark, kSkinLight);
    r.ellipse(98, 66, 8, 13, kSkin, kSkinDark, kSkinLight);
  } else {
    r.ellipse(16, 92, 9, 15, kSkin, kSkinDark, kSkinLight);
    r.ellipse(96, 92, 9, 15, kSkin, kSkinDark, kSkinLight);
  }
  r.ellipse(56, 88, 36, 28, kSkin, kSkinDark, kSkinLight);
  r.ellipse(56, 95, 22, 19, kBelly, 0, kBellyLight);
  r.ellipse(34, 111, 15, 7, kSkin, kSkinDark, kSkinLight);
  r.ellipse(78, 111, 15, 7, kSkin, kSkinDark, kSkinLight);
  r.ellipse(56, 42, 32, 24, kSkin, 0, kSkinLight);
  r.ellipse(56, 66, 34, 8, kCloak, kCloakDark, kCloakLight, 61, 1000);
  drawEye(r, kEyeL, kEyeY, face.eyes);
  drawEye(r, kEyeR, kEyeY, face.eyes);
  drawMouth(r, face.mouth);
  r.outline();
  return r;
}

// The egg, about 72 px tall: a hooded cap, a glassy shell, a froglet with
// teal eyes inside. Crack lines appear as it nears hatching.
constexpr int kEggW = 56, kEggH = 74, kFrogEyeY = 46, kFrogEyeR = 3;
Raster eggArt(int crack) {
  Raster r(kEggW, kEggH);
  r.line(23, 16, 18, 5, kSkin, 2);
  r.line(33, 16, 38, 5, kSkin, 2);
  r.disc(18, 4, 2, kSkinLight);
  r.disc(38, 4, 2, kSkinLight);
  r.ellipse(28, 46, 25, 26, kShell, kShellDark, kShellLight);
  r.ellipse(28, 59, 11, 9, kSkin, kSkinDark);
  r.ellipse(28, 49, 10, 7, kSkin, kSkinDark, kSkinLight);
  int eyeR = crack >= 2 ? kFrogEyeR + 1 : kFrogEyeR;
  for (int cx : {23, 33}) {
    r.disc(cx, kFrogEyeY, eyeR, kOutline);
    r.disc(cx, kFrogEyeY, eyeR - 1, kGlow);
    r.set(cx - 1, kFrogEyeY - 1, kGlowLight);
  }
  for (int x = 25; x <= 31; ++x) r.set(x, 53 + (x == 25 || x == 31 ? -1 : 0), kOutline);
  r.ellipse(28, 34, 27, 20, kCloak, kCloakDark, kCloakLight, -1000, 36);
  for (int x = 5; x <= 51; x += 8) r.ellipse(x, 36, 4, 3, kCloak, kCloakDark);
  if (crack >= 1) {
    const int a[][2] = {{8, 52}, {14, 48}, {18, 56}, {24, 51}};
    r.polyline(a, 4, kOutline);
    const int b[][2] = {{36, 62}, {42, 55}, {46, 60}, {51, 54}};
    r.polyline(b, 4, kOutline);
  }
  if (crack >= 2) {
    const int c[][2] = {{4, 46}, {10, 42}, {16, 50}, {23, 44}, {30, 48}};
    r.polyline(c, 5, kOutline, 2);
    const int d[][2] = {{30, 70}, {34, 64}, {40, 68}, {47, 63}, {53, 66}};
    r.polyline(d, 5, kOutline, 2);
  }
  r.outline();
  return r;
}

// The end of a run: the cloak folded in a heap, the hood empty, a sprout.
Raster remainsArt() {
  Raster r(84, 50);
  r.ellipse(42, 38, 38, 10, kCloak, kCloakDark, kCloakLight);
  r.ellipse(28, 36, 14, 6, kCloakLight, kCloak);
  r.ellipse(58, 39, 13, 5, kCloak, kCloakDark);
  r.ellipse(42, 28, 16, 11, kCloak, kCloakDark, kCloakLight);
  r.ellipse(42, 31, 8, 5, kCloakDark);
  r.line(42, 20, 42, 9, kSkin, 2);
  r.ellipse(35, 9, 6, 3, kSkinLight, kSkin);
  r.ellipse(50, 7, 6, 3, kSkin, kSkinDark, kSkinLight);
  r.outline();
  return r;
}

Raster pellet(uint8_t body, uint8_t spot) {
  Raster r(9, 8);
  r.ellipse(4, 4, 3, 2, body);
  r.set(3, 3, spot);
  r.set(5, 5, spot == kPelletLight ? body : spot);
  r.outline();
  return r;
}

Raster marble() {
  Raster r(11, 11);
  r.disc(5, 5, 4, kMarble);
  r.set(3, 3, kMarbleLight); r.set(4, 3, kMarbleLight); r.set(3, 4, kMarbleLight);
  r.outline();
  return r;
}

Raster crop(const Raster& src, int x0, int y0, int w, int h) {
  Raster r(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) r.set(x, y, src.at(x0 + x, y0 + y));
  return r;
}

class PlaceholderPack final : public SpritePack {
 public:
  PlaceholderPack() {
    idle_ = add(adult(kFaces[0], false), kFeetX, kFeetY);
    curl_ = add(adult(kFaces[0], true), kFeetX, kFeetY);
    idle_.eyeCount = curl_.eyeCount = 2;
    idle_.eyes[0] = curl_.eyes[0] = {kEyeL, kEyeY, kEyeR0};
    idle_.eyes[1] = curl_.eyes[1] = {kEyeR, kEyeY, kEyeR0};
    for (int i = 0; i < kFaceCount; ++i) {
      Stored& s = faces_[i] = add(crop(adult(kFaces[i], false), kPatchX, kPatchY, kPatchW, kPatchH),
                                  kFeetX - kPatchX, kFeetY - kPatchY);
      uint8_t r = kFaces[i].eyes == Eyes::Wide ? kEyeR0 + 1 : kEyeR0;
      s.eyeCount = 2;
      s.eyes[0] = {kEyeL - kPatchX, kEyeY - kPatchY, r};
      s.eyes[1] = {kEyeR - kPatchX, kEyeY - kPatchY, r};
    }
    for (int i = 0; i < 3; ++i) {
      eggs_[i] = add(eggArt(i), kEggW / 2, kEggH - 1);
      eggs_[i].eyeCount = 2;
      uint8_t r = i >= 2 ? kFrogEyeR + 1 : kFrogEyeR;
      eggs_[i].eyes[0] = {23, kFrogEyeY, r};
      eggs_[i].eyes[1] = {33, kFrogEyeY, r};
    }
    remains_ = add(remainsArt(), 42, 49);
    items_[0] = add(pellet(kPellet, kPelletLight), 4, 7);
    items_[1] = add(pellet(kRot, kRotSpot), 4, 7);
    items_[2] = add(marble(), 5, 10);
  }

  const PaletteEntry* palette(uint16_t& count) const override {
    count = kIndexCount;
    return kPalette;
  }
  RegionBand band(RegionId r) const override {
    return r.v < blorb::kRegionCount ? kBands[r.v] : RegionBand{0, 0, 128, 128, 128, 128};
  }
  FrameRef body(PoseId p, Stage, uint16_t) const override { return ref(p == blorb::pose::curl ? curl_ : idle_); }
  FrameRef face(ExprId e, Stage) const override {
    for (int i = 0; i < kFaceCount; ++i)
      if (kFaces[i].id == e) return ref(faces_[i]);
    return ref(faces_[0]);
  }
  FrameRef mark(uint8_t, uint8_t) const override { return FrameRef{}; }
  FrameRef egg(Fx progress) const override {
    int i = progress < Fx::ratio(1, 3) ? 0 : (progress < Fx::ratio(2, 3) ? 1 : 2);
    return ref(eggs_[i]);
  }
  FrameRef remains() const override { return ref(remains_); }
  FrameRef item(blorb::Appearance::Item::What w) const override {
    uint8_t i = uint8_t(w);
    return ref(items_[i < 3 ? i : 0]);
  }

 private:
  struct Stored {
    size_t offset = 0;
    uint16_t w = 0, h = 0;
    int16_t originX = 0, originY = 0;
    EyeAnchor eyes[2]{};
    uint8_t eyeCount = 0;
  };

  // Run-length rows per contracts.md: [run 1..255][index] pairs, each row summing to w.
  Stored add(const Raster& r, int originX, int originY) {
    Stored s;
    s.offset = blob_.size();
    s.w = uint16_t(r.w);
    s.h = uint16_t(r.h);
    s.originX = int16_t(originX);
    s.originY = int16_t(originY);
    for (int y = 0; y < r.h; ++y) {
      int x = 0;
      while (x < r.w) {
        uint8_t c = r.at(x, y);
        int run = 1;
        while (x + run < r.w && run < 255 && r.at(x + run, y) == c) ++run;
        blob_.push_back(uint8_t(run));
        blob_.push_back(c);
        x += run;
      }
    }
    return s;
  }

  FrameRef ref(const Stored& s) const {
    FrameRef f{};
    f.rle = blob_.data() + s.offset;
    f.w = s.w;
    f.h = s.h;
    f.originX = s.originX;
    f.originY = s.originY;
    f.eyes[0] = s.eyes[0];
    f.eyes[1] = s.eyes[1];
    f.eyeCount = s.eyeCount;
    return f;
  }

  std::vector<uint8_t> blob_;   // every frame's runs; complete before the first ref() is handed out
  Stored idle_, curl_, faces_[kFaceCount], eggs_[3], remains_, items_[3];
};

}  // namespace

const SpritePack& placeholderPack() {
  static const PlaceholderPack pack;
  return pack;
}

}  // namespace paint
