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
// each with his gauges hugging the rim on his own side of the disc: hearts as
// a segmented arc, the spark meter as a thinner one inside it, and the turn's
// clock as a ring round the glass (docs/notes/duel-hud.md). Every state a kid
// must read has a shape, not only a colour: a lost heart is an empty track, a
// locked stance's reticle closes, a forbidden tier is struck through.
#include <cstdint>
#include "blorb/appearance.h"
#include "blorb/duel.h"
#include "blorb/duel_session.h"
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
  int16_t power = -1;               // PowerId from his card (a hybrid's); -1: his species' own
  uint8_t hearts = 5, maxHearts = 5;
  uint8_t assist = 0;               // of maxHearts, the ones the fairness rule gave: drawn with the pet's cheer
  uint8_t sparks = 0;               // 0..kMeterMax
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
  // The queued special: telegraphed on both screens while it winds up, so it
  // can be read and punished. When it lands shows; the landing stance never does.
  blorb::Windup windup = blorb::Windup::None;
  uint8_t landsIn = 0;              // turns until it lands: 0 = this turn
  blorb::TurnReport::Special special = blorb::TurnReport::Special::None;   // Clash: what became of it this turn
};

// The BOOT hint on the rim beside the button: boot.h's BootHint, as drawn.
// What it shows is what a press does now; a ring around it fills while the
// button is held when a hold does something else.
struct RimHint {
  uint8_t glyph = 1;                // blorb::BootGlyph, which indexes duel_art::kBootGlyphs
  uint8_t art = 0;                  // Tier: the tier under the tilt; a special: the pet's PowerGlyph
  bool greyed = false;              // not now: grey, with `pips` beside it saying how far short
  uint8_t pips = 0;                 // greyed: how many short; Countdown: turns left
  bool hasHold = false;             // a hold does something else: the ring, filling with holdFill
  uint8_t holdFill = 0;
  bool hasDouble = false;           // a double press does something: two short bars under it
  uint8_t doubleFill = 0;           // a first press waits for its second, or a hang is on its way up: the bars light
  bool holdVoid = false;            // a hold is felt but does nothing here: the ring, grey
  bool knockWait = false;           // a first knock is in, the second can still come: the glyph glows, "one more"
};

// blorb::BootHint as drawn, field by field (the one place they are matched up).
RimHint rimHint(const blorb::BootHint&);

// The hint at its place on the rim, for any screen (the dish draws it too).
void drawBootHint(Canvas240&, const RimHint&);

// How long until the badge sleeps (docs/power.md): a 1 px arc one pixel in
// from the rim, clockwise from the top, `permille` of the way round. Full
// at any input, shrinking to nothing as the idle timeout runs out. Over
// everything else: main.cpp draws it last.
void drawIdleBar(Canvas240&, uint16_t permille);

// A hard shake dismissed something (#35): a white ring sweeps out from the
// middle and fades, kDismissedMs long, `ageMs` into it. UINT16_MAX or past
// it: nothing.
constexpr uint16_t kDismissedMs = 300;
void drawDismissed(Canvas240&, uint16_t ageMs);

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
  RimHint boot;                     // every act shows what BOOT does now
  // Last, so a renderer built before it reads the rest where it always was (paint_bench REF=).
  uint16_t sub = 0;                 // 1/256 ticks past `ticks` (paint/tween.h): the motion runs smooth between ticks
};

// What D4's Outcome says, from my side: hearts and meters, the last resolved
// turn as the clash plays it (stances shown, hearts lost, powers fired, who
// won it, SUPER), and, once it has ended, who won the duel, its stakes and the
// verdict. The act, the clock and everything a kid is doing right now (aim,
// lock, the holds) come from the session's view, not from here.
void fromOutcome(DuelScene&, const blorb::Outcome&, blorb::Side me);

// The session's view of the duel (D8) as the screen draws it, with my pet's
// live Appearance: the phase as the act, both fighters from the cards and the
// side states, the last turn as the clash, the holds, the mercy clock, the
// verdict. What main.cpp and the sim draw every frame:
//   paint::drawDuel(paint::sceneOf(view, a), paint::packFor(a.species),
//                   paint::packFor(view.them.species), canvas, now);
DuelScene sceneOf(const blorb::DuelView&, const blorb::Appearance& mine);

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
  Heart,      // unused since the gauges (DuelStaging::gauge); kept so the kinds keep their numbers
  Pip,        // unused since the gauges, likewise
  Stance,     // variant: blorb::Stance
  Power,      // variant: blorb::PowerGlyph
  Tier,       // variant: blorb::Stakes; `fill` is how full a Mortal skull is
  Element,    // variant: blorb::Element
  Trophy,     // variant: SpeciesId
  Debt,
  Plaster,    // the Scrap risk, stuck on his head
  Burst,      // a spiky star: BONK, VS, SUPER, BOINK (heads), WHAM (a landing), NOPE (a riposte), OOF (a stagger), WHIFF
  Aura,       // a special winding up: light in its power's colour behind him; variant: PowerGlyph
  Countdown,  // the turns until it lands, a ring of that many arcs round its glyph (at its scale); variant: how many, 0 = now (`fill` its pulse)
  Arrow,      // "beats": from (x, y) toward (x2, y2)
  Tick,       // unused in the duel since the reticle and the lock ring
  Dots,       // unused in the duel since the lock ring
  Strike,     // a bar across a forbidden tier
  Timer,      // the clock round the rim: `fill` of the circle left, the gap opening at 12 and sweeping clockwise
  Stars,      // dizzy stars orbiting a downed or fainted head
  Glow,       // the lean: soft light under a glyph
  Button,     // gesture hint: hold BOOT (variant 0), knock (variant 1)
  Plea,       // a cheer heart floating up from the downed pet
  Reticle,    // round the icon a tilt points at (sized by scalePct as the icon is): variant 0 aiming (four
              // brackets), 1 locked or confirmed (the ring closes), 2 a hold alone; `fill` < 255: a hold filling it
  Lock,       // the friend's choice, over his head: variant 0..2 still choosing (a three-arc spinner, that arc lit), 3 made
};

struct Prop {
  PropKind kind;
  uint8_t variant = 0;
  int16_t x = 0, y = 0, x2 = 0, y2 = 0;
  uint8_t scalePct = 100;
  uint8_t alpha = 255;
  uint8_t fill = 255;
};

// A rectangle on the canvas, px, inclusive; empty when x0 > x1.
struct Box { int16_t x0 = 1, y0 = 1, x1 = 0, y1 = 0; };

// One pet's gauges on his side of the rim: hearts as a segmented arc, the
// spark meter inside it with a spoke where his power is paid for. Mine run down
// the lower right, the friend's up the upper left, the same arc turned half round.
struct Gauges {
  bool shown = false;
  bool meter = false;               // the spark meter too (the fight itself)
  uint8_t hearts = 0, maxHearts = 0, assist = 0;   // assist: the last of maxHearts, the fairness rule's
  uint8_t losing = 0;               // hearts past `hearts` the hit on screen is taking
  uint8_t drain = 0;                // 0..255 how far those have drained (0: not landed yet, still full)
  uint8_t flash = 0;                // 0..255 the hit's flash on them
  uint8_t sparks = 0, cost = 0;     // the meter, and where the spoke stands
  uint8_t pulse = 0;                // 0..255 breathing: the last heart, a power that is ready
  uint8_t sweep = 255;              // 0..255 of the arc drawn: the gauges sweep in at the meeting
};

struct DuelStaging {
  static constexpr int kMaxProps = 48;
  Figure fig[2];                    // [me, them]
  Gauges gauge[2];                  // [me, them]
  Box face[2];                      // where each pet's face is: a burst never covers one
  Prop props[kMaxProps];
  uint8_t propCount = 0;
};

// Where everything goes for this scene. The packs give each pet his height,
// so what sits over his head sits over his head.
DuelStaging stageDuel(const DuelScene&, const SpritePack& mine, const SpritePack& theirs);

// Pure over (scene, nowMs) like draw(): the same scene is the same bytes.
void drawDuel(const DuelScene&, const SpritePack& mine, const SpritePack& theirs, Canvas240&, uint32_t nowMs);
// The same, from a staging already made (what a test holds the staging to).
void drawStaged(const DuelScene&, const DuelStaging&, const SpritePack& mine, const SpritePack& theirs, Canvas240&);

}  // namespace paint
