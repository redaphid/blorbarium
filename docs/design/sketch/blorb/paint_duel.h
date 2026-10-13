#pragma once
// SKETCH (docs/design/BATTLES.md) of lib/paint additions (forward-declares paint's real
// types so this compiles against lib/blorb alone).
//
// Every device carries all nine species' packs (the 2 MiB raw `art`
// partition, memory-mapped), because a friend of any species must be drawn
// on my screen, and because a friend egg can hatch any of them.
#include <cstdint>
#include "blorb/appearance.h"
#include "blorb/duel_session.h"
#include "blorb/species.h"

namespace paint {
class SpritePack;                 // paint/sprite_pack.h
struct Canvas240;                 // paint/canvas.h

// The pack for a species row; an unknown or not-yet-drawn species gets
// grungo's, tinted by its own palette genes, so a missing pack degrades to a
// recoloured frog, never a blank.
const SpritePack& packFor(blorb::SpeciesId);

// The duel scene, drawn instead of the dish while Appearance::dueling. Pure
// over (view, nowMs) like draw(), so the sim's shots are byte-identical.
//   me: bottom, facing up, live face and reflexes from my own Appearance
//   them: top, facing down, smaller, drawn from their CardLook
//   rim: my hearts lower-left arc, theirs upper-right; spark pips beside each;
//        the three stance glyphs at 8, 4 and 6 o'clock while choosing; the
//        shrinking ring is the timer; a tick mark when they have locked
//   centre: the reveal (two glyphs, the "beats" arrow, BONK / SUPER! / a bonk
//           of heads), the power glyph and its effect, the faint, the friends photo
//   above them: the prophecy bubble (the stance he foresaw) and incoming cheers
void drawDuel(const blorb::DuelView&, const blorb::Appearance& me, const SpritePack& mine, const SpritePack& theirs,
              Canvas240&, uint32_t nowMs);

}  // namespace paint
