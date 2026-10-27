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

// scarShade, a row at a time, the same numbers: each stamp's reach worked out
// once a blit and the stamps that cross a row once a row, so a hide pixel away
// from every scar costs two compares a stamp, with no divide and no walk of
// the scar's art from its first row.
class ScarRows {
 public:
  static constexpr int kMaxArtRows = 32;   // the tallest scar art (scars.cpp checks)
  void begin(const ScarLayer&);
  void row(int ry);
  int shade(int rx) const;

 private:
  struct Reach { int x0, x1, y0, y1; uint16_t rowAt[kMaxArtRows]; };
  struct Live { const ScarStamp* s; const uint8_t* row; int x0, x1; };
  const ScarLayer* layer_ = nullptr;
  Reach reach_[blorb::kMaxScars];
  Live live_[blorb::kMaxScars];
  int liveCount_ = 0;
};

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

// One egg of a clutch (blorb::EggPreview) in its species' art and colours,
// its middle at (cx, cy), heightPx tall, rocked by `shear` (Q12).
void drawEggPreview(Canvas240&, const blorb::EggPreview&, int cx, int cy, int heightPx, int shear, int alpha = 256);
// The height of a species' whole egg frame, px: the egg screen draws it 1:1.
int eggArtHeight(blorb::SpeciesId);

// ---- duel_scene.cpp -----------------------------------------------------------------

// One of the duel's props alone, as the duel draws it: the tilt picker's
// reticle and lean glow, and the knock a kid's knuckle gives, for the clutch.
void drawPickerProp(Canvas240&, const Prop&);

// ---- clutch.cpp ---------------------------------------------------------------------

// The picker's words: TILT TO PICK, KNOCK TO KEEP (test_paint checks them
// against the font).
extern const char* const kClutchWords[2];
// The eggs to choose from, laid out as the duel's tilt picker (blorb/clutch_show.h).
void drawClutchPicker(const blorb::Appearance&, Canvas240&);
// The moment after an egg is kept, over the egg screen: false once it has
// handed over to the plain egg (the caller draws that).
bool drawChosen(const blorb::Appearance&, Canvas240&);
// The vigil: the eggs on their way, coming clearer as it ends.
void drawVigilEggs(const blorb::Appearance&, Canvas240&);

// ---- feeding.cpp --------------------------------------------------------------------

// Where draw.cpp put him and the newest pellet this frame, for what
// feeding.cpp draws over the dish. Canvas pixels.
struct DishSpots {
  bool him = false;
  int x = 0, y = 0;                         // his stand point, between his feet
  int top = 0;                              // the top of his body frame, standing
  bool mouth = false;
  int mouthX = 0, mouthY = 0;               // the pellet in his mouth, while he chews
  bool landing = false;
  int landX = 0, landY = 0;                 // the newest pellet's stand point in the dish
};

// The newest pellet in the air (blorb::FeedView::flying), on its way from
// the rim's pellet glyph to (landX, landY): where it is now and how big.
// False once it has landed, and then draw.cpp draws it as any item.
bool pelletAloft(const blorb::FeedView&, int landX, int landY, int& x, int& y, int& scalePct);
// It, aloft: a cookie with a trail, and its shadow waiting where it lands.
void foodAloft(Canvas240&, const blorb::FeedView&, int landX, int landY, bool rotten);

// The food, wherever it shows: on the floor, in the air, at his mouth, in a
// bubble. THE seam for food art: a fat cookie drawn in code now; a sprite
// can replace the body of drawFood (feeding.cpp) without touching the rest.
// (cx, cy) its centre, r its radius in px.
struct Food {
  uint8_t bites = 0;        // 0..3 taken out of it (he is chewing it)
  bool rotten = false;      // grey-green, no highlight
  bool grey = false;        // none left: the pantry is empty
  bool biteLeft = false;    // which side his bites come from
};
constexpr int kFoodFloorR = 9;   // on the floor
void drawFood(Canvas240&, int cx, int cy, int r, const Food&, int alpha = 256);

// Feeding over the dish (blorb/feed_show.h): the rim's flash, the landing,
// a refusal, the chomp and the belly meter. Before the round mask.
void drawFeeding(Canvas240&, const blorb::Appearance&, const DishSpots&);

// ---- idle_props.cpp -----------------------------------------------------------------

// What he plays with while he plays an idle (blorb/idle.h): the notes of a
// song or a dance, the pebble he juggles, the dirt he digs, the ball he
// pushes, a daydream's bubble, the stars he gazes at. Drawn in code, over him,
// on the idle's clock (Appearance::idleTicks), so it moves between ticks.
void drawIdleProps(Canvas240&, const blorb::Appearance&, const DishSpots&);

// Hunger, like hearts: five cookies up the left rim, each a fifth of how full
// he is, whole, bitten, or a faint grey one gone; an empty pantry shakes them
// red. Over the dish, before the round mask. pantryColour (`full` 0..256) is
// the green-to-red ramp, kept for anything that shows the pantry.
void hungerCookies(Canvas240&, const blorb::Appearance&);
uint16_t pantryColour(int full);

}  // namespace paint
