#pragma once
// The duel screen: two pets on one round panel (docs/design/BATTLES.md, "What
// each kid sees"). Drawn instead of the dish while a duel is open.
//
// A DuelScene is the duel flattened to what is on screen, so the drawing reads
// no session state and a test, the contact sheet or a demo film can build one
// by hand. stageDuel() turns a scene into a display list (two figures and the
// props round them) and drawDuel() paints that list, so where everything goes
// in each act is plain data a golden can hold.
//
// The layout is the battle layout kids already know: the friend small in the
// upper right, mine large in the lower left, each leaning toward the other,
// each with their hearts on the rim on their own side of the disc. Every
// state a kid must read has a shape, not only a colour: a lost heart is
// hollow, a locked stance is stamped, a forbidden tier is struck through.
#include <cstdint>
#include "blorb/appearance.h"
#include "blorb/duel.h"
#include "blorb/species.h"
#include "blorb/stakes.h"
#include "paint/canvas.h"
#include "paint/sprite_pack.h"

namespace paint {

// The beat of the duel the screen is on.
enum class DuelAct : uint8_t {
  Meeting,    // VS: both pets bounce into their corners
  Staking,    // three tiers on the rim; each pet wears his risk: a feather, a plaster, a skull filled to his forecast
  Choosing,   // stance glyphs at 9, 3 and 6 o'clock (tilt left, right, toward you); the rim ring is the timer
  Clash,      // the reveal: both stances pop, each pet plays his stance, the loser is hit (or heads bonk)
  Mercy,      // Mortal: the loser is down; the skull over him fills with the winner's hold
  Fallen,     // the verdict: finished (his remains) or spared (the debt heart between them)
  Outcome,    // the end: the winner proud, the loser fainted, my new scars, the trophy
};

enum class Side : uint8_t { Me, Them, Neither };

struct Fighter {
  blorb::Appearance look;           // mine: my live Appearance; theirs: their CardLook as one
  uint8_t hearts = 5, maxHearts = 5;
  uint8_t assist = 0;               // of maxHearts, the ones the fairness rule gave: drawn with the pet's cheer
  uint8_t sparks = 0;               // 0..kMeterMax
  bool armed = false;               // the power is called for this turn
  // Staking
  blorb::Stakes chose = blorb::Stakes::Spar;
  blorb::Stakes ceiling = blorb::Stakes::Mortal;   // tiers above it are struck through
  bool confirmed = false;
  uint8_t hold = 0;                 // 0..255: the Mortal confirm, BOOT held 3 s
  uint8_t skull = 0;                // 0..255: his forecast at the tier he aims (how full his skull is)
  // Clash
  int8_t stance = -1;               // as revealed (blorb::Stance), -1 dozed
  bool fired = false;               // his power went off this turn
  uint8_t hit = 0;                  // hearts lost this turn
  bool locked = false;              // Choosing: he has locked (for the friend: a tick, never the stance)
};

struct DuelScene {
  DuelAct act = DuelAct::Meeting;
  uint16_t ticks = 0;               // 10 a second since the act began; every motion is a function of it
  Fighter me, them;
  blorb::Stakes stakes = blorb::Stakes::Spar;   // the agreed tier (Staking: shown once both confirmed)
  // Choosing
  int8_t aim = -1;                  // the stance under the tilt
  int8_t lean = -1;                 // the stance my pet would pick; glows brighter with the assist
  uint8_t whisper = 0;              // 0..255 how bright
  uint16_t ticksLeft = 100, turnTicks = 100;
  // Clash, Mercy, Fallen, Outcome: who won the clash, or the duel (Neither: a bonk of heads, a draw)
  Side won = Side::Neither;
  bool super = false;               // Clash: the winner's element beats the loser's
  // Mercy and Fallen
  uint8_t finishHold = 0;           // 0..255 the skull filling while the winner holds BOOT
  uint16_t mercyLeft = 150, mercyTicks = 150;
  blorb::Verdict verdict = blorb::Verdict::None;
  bool pleading = false;            // the downed pet's kid sent a cheer: it floats up
  // Outcome
  uint8_t newScars = 0;             // my last n scars are new: drawn coming in
};

// What D4's Outcome says, from my side: hearts and meters, the last resolved
// turn as the clash plays it (stances shown, hearts lost, powers fired, who
// won it, SUPER), and, once it has ended, who won the duel, its stakes and the
// verdict. The act, the clock and everything a kid is doing right now (aim,
// lock, the holds) come from the session's view, not from here.
void fromOutcome(DuelScene&, const blorb::Outcome&, blorb::Side me);

// ---- the display list -------------------------------------------------------------

// One pet as the duel stands him: where, how big, his procedural motion (the
// lunge, the curl, the sidestep, the downed slump) and the face the act gives him.
struct Figure {
  int16_t x = 0, y = 0;             // the stand point (his feet), canvas px
  uint8_t scalePct = 100;           // of his size at home
  uint8_t widePct = 100, tallPct = 100;   // squash
  int8_t lift = 0;                  // px above the stand point
  int8_t leanPct = 0;               // shear toward +x, percent of his height
  uint8_t flash = 0;                // 0..255 toward white: just hit
  uint8_t alpha = 255;
  uint8_t remainsIn = 0;            // 0..255 his remains fading in as he fades out: a pet finished
  bool hasFace = false;
  blorb::ExprId face{};
  uint8_t newScars = 0;             // his last n scars coming in (0..255 each through `scarIn`)
  uint8_t scarIn = 255;
};

enum class PropKind : uint8_t {
  Heart,      // variant: 0 full, 1 hollow (lost), 2 assist (full, with a cheer star)
  Pip,        // spark meter: variant 0 full, 1 hollow
  Stance,     // variant: blorb::Stance
  Power,      // variant: blorb::PowerGlyph
  Tier,       // variant: blorb::Stakes; `fill` is how full a Mortal skull is
  Element,    // variant: blorb::Element
  Trophy,     // variant: SpeciesId
  Debt,
  Plaster,    // the Scrap risk, stuck on his head
  Burst,      // a spiky star: BONK (variant 0), VS (1), SUPER (2), a bonk of heads (3)
  Arrow,      // "beats": from (x, y) toward (x2, y2)
  Tick,       // locked or confirmed
  Dots,       // still choosing
  Strike,     // a bar across a forbidden tier
  Timer,      // the rim timer: `fill` of the circle left
  Stars,      // dizzy stars orbiting a downed or fainted head
  Glow,       // the lean: soft light under a glyph
  Button,     // gesture hint: hold BOOT (variant 0), knock (variant 1)
  Plea,       // a cheer heart floating up from the downed pet
};

struct Prop {
  PropKind kind;
  uint8_t variant = 0;
  int16_t x = 0, y = 0, x2 = 0, y2 = 0;
  uint8_t scalePct = 100;
  uint8_t alpha = 255;
  uint8_t fill = 255;
};

struct DuelStaging {
  static constexpr int kMaxProps = 48;
  Figure fig[2];                    // [me, them]
  Prop props[kMaxProps];
  uint8_t propCount = 0;
};

// Where everything goes for this scene. The packs give each pet his height,
// so what sits over his head sits over his head.
DuelStaging stageDuel(const DuelScene&, const SpritePack& mine, const SpritePack& theirs);

// Pure over (scene, nowMs) like draw(): the same scene is the same bytes.
void drawDuel(const DuelScene&, const SpritePack& mine, const SpritePack& theirs, Canvas240&, uint32_t nowMs);

}  // namespace paint
