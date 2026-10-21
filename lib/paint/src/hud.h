#pragma once
// The duel's infographics (docs/notes/duel-hud.md): arc gauges, segmented
// rings and spokes on the round panel, after cyber-puck's ring and gauges.
//
// Fast by construction. An arc is filled row by row: the band's two circles
// come from a table of half-chords made at compile time (kHalf, in flash), and
// the wedge's two edges are a half-plane each, so a row is two multiplies and
// two divides, then a run of stores. No per-pixel trig, no atan2, no double, no
// square root at run time.
//
// Angles are in 1/1024 turn, clockwise from 3 o'clock (y runs down), as
// duel_scene's 1/256 turn is, four times finer. A centre is given in half
// pixels: 240 is the panel's middle (a pixel corner), 2x+1 the middle of pixel x.
#include <cstdint>
#include "paint/canvas.h"
#include "paint/duel.h"

namespace paint {
namespace hud {

constexpr int kTurn = 1024;

// sin and cos of an angle in 1/1024 turn, Q14.
int sin1024(int a);
inline int cos1024(int a) { return sin1024(a + kTurn / 4); }

// The pixels whose middles lie more than r0 and at most r1 from (cx2, cy2)/2,
// between angles a0 and a1 (clockwise; a1 - a0 of a whole turn or more is the
// ring). alpha 256 stores, below it blends.
void arc(Canvas240&, int cx2, int cy2, int r0, int r1, int a0, int a1, uint16_t c, int alpha = 256);

// About `px` pixels of arc at radius r, in 1/1024 turn (at least 1).
int span(int px, int r);

// A colour from 8-bit channels, and two mixed at t/256.
constexpr uint16_t rgb(int r, int g, int b) { return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
inline uint16_t mix(uint16_t a, uint16_t b, int t) { return t <= 0 ? a : (t >= 256 ? b : blend(a, b, t)); }

// cyber-puck's cyberpunk palette (src/theme.h), one meaning a colour.
constexpr uint16_t kDust = rgb(68, 72, 92);       // a track: what is spent, or not there yet
constexpr uint16_t kShadow = rgb(10, 6, 20);      // the ink under every band
constexpr uint16_t kSpark = rgb(240, 251, 255);   // the flash of an event, a spoke, a reticle
constexpr uint16_t kText = rgb(184, 198, 217);    // the clock running
constexpr uint16_t kLife = rgb(124, 255, 61);     // hearts left (cyber-puck's DONE lime)
constexpr uint16_t kYou = rgb(255, 174, 0);       // a kid should act: two hearts left, the clock short, a hold
constexpr uint16_t kHurt = rgb(255, 31, 90);      // damage, the last heart, the clock out, a finish
constexpr uint16_t kPower = rgb(0, 229, 255);     // the spark meter (cyber-puck's read cyan)
constexpr uint16_t kCheer = rgb(255, 200, 60);    // a heart the fairness rule gave: the pet's cheer
constexpr uint16_t kLocked = rgb(124, 255, 61);   // a choice made: the ring closes

// ---- the duel's infographics ---------------------------------------------------------

// One pet's gauges on his side of the rim (side 0 mine, lower right; 1 the friend's).
void gauges(Canvas240&, const Gauges&, int side);
// The turn's clock round the glass: `left` 0..255 of it still to run.
void clock(Canvas240&, int left);
// The props drawn as rings: PropKind::Reticle, Lock and Countdown.
void reticle(Canvas240&, const Prop&);
void lock(Canvas240&, const Prop&);
void countdown(Canvas240&, const Prop&);

}  // namespace hud
}  // namespace paint
