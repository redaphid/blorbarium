#pragma once
// The renderer: an Appearance in, RGB565 pixels out. Plain C++17, no
// LovyanGFX, so the sim and host tests draw the same bytes as the device;
// firmware pushes the buffer with LGFX. Not part of the engine: lib/paint may
// be forked for another character.
//
// draw() is pure over (Appearance, pack, nowMs): every animation of him (bob,
// blink, yawn, crossfade, the hop's squash-leap-land, the halo pulse and
// sparks) is a function of poseTick, exprTicks, reflexPhase and lifeSeed (and,
// between ticks, their 1/256s and the idle layer's liveMs: paint/tween.h), and
// the dish's own furniture (the sealed board's marquee) of nowMs, elapsed real
// milliseconds, so it moves whatever the simulation is doing. That is what
// makes the sim's headless shots byte-identical and the goldens stable.
#include <cstdint>
#include "blorb/appearance.h"
#include "paint/canvas.h"

namespace paint {

// The pack's palette. Each entry is tagged with a region (regions.def) by the
// converter from the art's hue and the eye and mouth masks; 255 = invariant
// (outline). A recolour costs one 256-entry LUT rebuild per life, no flash.
struct PaletteEntry { uint8_t r, g, b; uint8_t region; };
// How far genes may move a region (blorb/registry.h, so a founder rolls inside it).
using RegionBand = blorb::RegionBand;

// Eye centre and radius in frame pixels. Per frame, so the halo tracks the
// eyes through the hop, the bob, scaling and a face patch that moves them.
struct EyeAnchor { int16_t x, y; uint8_t r; };

struct FrameRef {
  const uint8_t* rle;       // run-length rows of palette indices, 0 = clear
  uint16_t w, h;
  int16_t originX, originY; // the feet's centre, where the frame stands
  EyeAnchor eyes[2];
  uint8_t eyeCount;         // 0 on frames with no eyes (egg, remains)
};

class SpritePack {
 public:
  virtual ~SpritePack() = default;
  virtual const PaletteEntry* palette(uint16_t& count) const = 0;
  virtual RegionBand band(blorb::RegionId) const = 0;
  // Stage art: Baby/Child draw the hatchling (or the adult scaled with a
  // bigger head while no hatchling art exists), Adult the adult, Elder the old frog.
  virtual FrameRef body(blorb::PoseId, blorb::Stage, uint16_t poseTick) const = 0;   // missing pose -> idle
  virtual FrameRef face(blorb::ExprId, blorb::Stage) const = 0;                      // patch registered on body; missing -> neutral
  virtual FrameRef mark(uint8_t layer, uint8_t variant) const = 0;                   // variant taken modulo the pack's count
  virtual FrameRef egg(blorb::Fx progress) const = 0;                                // whole, cracking, hatching
  virtual FrameRef remains() const = 0;                                              // the end of a run, cosy, not grim
  virtual FrameRef item(blorb::Appearance::Item::What) const = 0;
};

// Draw order: night tint, dish items, body (squash and leap from the reflex),
// marks, face (crossfade from previous), then the foresee halo: a teal ring
// around each eye anchor, tinted by the glow region, radius r * (1.5 + 0.5 *
// glow), pulsing at 1.25 Hz, with up to six orbiting sparks once glow > 0.6;
// then the pantry bar on the rim, the care-hint glyph and the thought line;
// then the round mask.
void draw(const blorb::Appearance&, const SpritePack&, Canvas240&, uint32_t nowMs);

// The firmware version, tiny and dim at the dish's right rim, drawn just
// after the background so everything else lands on top of it: an adult
// looking closely reads it, a kid never notices it. The firmware sets it once
// from its stamp (src/build_info.cpp); unset (the native suites) draws nothing.
// The string must outlive every draw.
void setVersionTag(const char*);

// The species' sprite pack (species.def's rows; every device holds all nine).
// An id this firmware has no row for draws grungo, recoloured by the pet's own
// genes like any pack, so a gap in the art is a frog in its colours, never a blank.
const SpritePack& packFor(blorb::SpeciesId);
// packFor(species)'s band for every region: what foundingGenome rolls across.
blorb::LookBands bandsFor(blorb::SpeciesId);

// A procedural stand-in (circles, a ring, two eyes with anchors) used by the
// sim and tests until the grungo converter exists.
const SpritePack& placeholderPack();

}  // namespace paint
