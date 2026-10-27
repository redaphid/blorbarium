#pragma once
// What a family looks like: the ranges his look genes move in and how each
// shows on the glass (gene-looks). Each is inherited (mutate() steps it a
// little each egg, a friend cross blends it) and stays inside a range that
// still draws his species:
//
//   colour   each region's palette gene, a tint of the pack's own pixels
//            (paint builds one 256-entry colour table per draw, no per-pixel
//            maths), inside kLookBands: the bands every pack carries
//   size     SizeGene.base: the whole body, at every stage, 88% to 112%
//   build    SizeGene.growth: stocky (wider, shorter) to lanky (narrower, taller)
//   spots    MarkGene layer 0: how many spots his skin has (variant) and
//            their hue against the skin (tint); where they fall is his own
//
// A 0 byte in a gene means "as ever" (128), as the motion's bulk reads it.
#include <algorithm>
#include <cstdint>
#include "blorb/registry.h"

namespace blorb {

// Each region's band: hue as the palette gene's hue byte - 128, sat and val as
// the bytes. Every pack carries these (tools/sprite_pack.py BANDS); the gene
// looks test holds them equal to paint::bandsFor for every species.
inline constexpr LookBands kLookBands = {{
    {-24, 40, 96, 176, 96, 160},    // skin
    {-16, 16, 96, 160, 96, 160},    // belly
    {-16, 36, 80, 200, 80, 176},    // cloak
    {-8, 30, 96, 176, 96, 192},     // eye
    {-6, 6, 104, 152, 112, 144},    // mouth
    {-20, 20, 112, 160, 112, 160},  // glow
    {-24, 40, 96, 176, 96, 160},    // shell
}};

// The byte ranges inheritance keeps each look byte in, and founders roll across.
struct ByteRange { uint8_t lo, hi; };
inline constexpr ByteRange kSizeRange{104, 152};       // SizeGene.base
inline constexpr ByteRange kBuildRange{96, 160};       // SizeGene.growth
inline constexpr ByteRange kSpotsRange{0, 72};         // MarkGene layer 0 variant: spots per 256 skin cells
inline constexpr ByteRange kSpotHueRange{88, 184};     // MarkGene layer 0 tint: 128 = the skin's hue
inline constexpr ByteRange kFoundSize{112, 144}, kFoundBuild{104, 152}, kFoundSpots{4, 56}, kFoundSpotHue{96, 176};

constexpr int asByte(uint8_t gene) { return gene ? gene : 128; }
// The body at every stage, percent of the stage's own size: 2 bytes a percent.
constexpr int sizePct(uint8_t base) { return std::clamp(100 + (asByte(base) - 128) / 2, 88, 112); }
// Build, percent: a stocky pet is wider and a little shorter, a lanky one narrower and taller.
constexpr int buildWidePct(uint8_t growth) { return std::clamp(100 + (asByte(growth) - 128) / 4, 90, 110); }
constexpr int buildTallPct(uint8_t growth) { return std::clamp(100 - (asByte(growth) - 128) / 8, 95, 105); }

}  // namespace blorb
