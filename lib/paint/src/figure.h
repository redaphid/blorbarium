#pragma once
// Inside lib/paint: what draw.cpp lends the duel scene (a pet drawn anywhere,
// a battle-art frame drawn anywhere) and what scars.cpp lends draw.cpp (where
// his scars sit on a body frame, and how they shade his skin).
#include <cstdint>
#include "blorb/appearance.h"
#include "paint/canvas.h"
#include "paint/duel.h"
#include "paint/sprite_pack.h"

namespace paint {

// ---- scars.cpp ----------------------------------------------------------------------

// His hide: where a scar may cut. Skin, and the cloak and shell a species
// wears as its outer coat (an owl is almost all cloak). Never his belly, his
// eyes, his mouth or his glow.
inline bool isHide(uint8_t region) {
  return region == blorb::region::skin.v || region == blorb::region::cloak.v || region == blorb::region::shell.v;
}

// One scar placed on one body frame: its art and its centre in frame pixels
// from the stand point, so it rides every squash, lift and lean of the body.
struct ScarStamp {
  const FrameRef* art;   // into duel_art::kScars
  int16_t x, y;
  uint8_t strength;     // 0..255 of its art's crease: the scar's fade, and coming in
};
struct ScarLayer {
  ScarStamp stamp[blorb::kMaxScars];
  uint8_t count = 0;
  uint16_t zoom = 256;   // frame px per art px, x256: a scar keeps its own size on screen
};

// Where his scars sit on `body`: never on the front of him. Each hugs the edge
// of his hide on its side at its height (a flank, a shoulder, a limb, the side
// of his head), and only hide pixels are ever shaded, so the belly region and
// anything that is not him stay clean whatever bytes the save holds.
// `scale` is how big the body is drawn (Q12): a scar is sized for the pet as
// he appears and never drawn smaller than its art, so a hatchling's scar is
// small but still there. `newest` of them (the last ones added) are drawn at
// `newStrength` of their fade.
ScarLayer placeScars(const blorb::Scars&, const FrameRef& body, const PaletteEntry* pal, uint16_t palCount, int scale,
                     uint8_t newest = 0, uint8_t newStrength = 255);

// The hide multiplier at a frame pixel (from the stand point), x/128: 128
// where no scar is. A crease is darker, its ridge a little lighter.
int scarShade(const ScarLayer&, int rx, int ry);

// ---- the round panel ----------------------------------------------------------------

// Row y's first column inside the round panel (a pixel whose centre is within
// 120 px of the middle), so the last is 239 - it: every pixel of the row left
// of it and right of the last is outside. Worked out at compile time, so a
// mask is two memsets a row, not a test a pixel.
struct DishSpans { uint8_t from[240]; };
constexpr DishSpans dishSpans() {
  DishSpans s{};
  for (int y = 0; y < 240; ++y) {
    int x = 0;
    const int dy = 2 * y + 1 - 240;
    while (x < 120 && (2 * x + 1 - 240) * (2 * x + 1 - 240) + dy * dy > 240 * 240) ++x;
    s.from[y] = uint8_t(x);
  }
  return s;
}
inline constexpr DishSpans kDish = dishSpans();

// ---- draw.cpp -----------------------------------------------------------------------

// A pet where the duel stands him: his own Appearance (colours, marks, scars,
// mottling, stage, glow), placed and moved by the Figure.
void drawFigure(const blorb::Appearance&, const SpritePack&, Canvas240&, const Figure&);

// A battle-art frame (pets/duel/duel_art.h, fixed colours), centred at (x, y).
// `fillRows` < 255 draws only that share of its rows, from the bottom up: the skull filling.
void drawArt(Canvas240&, const FrameRef&, int x, int y, int scalePct, int alpha, int fillRows = 255);

}  // namespace paint
