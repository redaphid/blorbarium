#pragma once
// The one button, as data (docs/design/BATTLES.md, "BOOT, everywhere").
//
// Owner (2026-10-13): "People should be able to push the 'boot' button to do
// contextual things." Every screen state names what a press, a hold and a
// double press do there, in one table, and the renderer always draws the
// press's hint glyph on the rim beside the physical button, with a ring that
// fills while the button is held when a hold means something different. A kid
// never has to guess: what the glyph shows is what the button does now.
//
// The table is the single source: the Dish routes the button by it (the
// stimuli it fires, or the duel input it becomes) and Appearance carries the
// hint from it, so the glyph cannot drift from the behaviour. Where a row's
// double press is Nothing, the second press of a pair counts as a press
// again (Dish::routeBoot), so a kid tapping through the clutch is not slowed.
#include <cstddef>
#include <cstdint>

namespace blorb {

// Where the button is, from the occupant, the duel phase and the OTA question.
// Ordered by precedence: the first true one wins (bootContext() in dish_duel.h;
// OtaQuestion is main.cpp's, which owns the update session).
enum class BootContext : uint8_t {
  OtaQuestion,   // ota::Session is asking: the update owns the button (main.cpp)
  Sealed,        // the keepsake would not open: nothing moves, nothing is written
  Staking,       // duel: choosing the stakes
  DuelTurn,      // duel: choosing this turn's stance
  WindingUp,     // duel: my special is queued; my stance is locked
  MercyWinner,   // duel: I downed them in a Mortal duel
  MercyLoser,    // duel: I am down
  Fallen,        // duel: a pet fell (either side)
  DuelOutcome,   // duel: the friends screen
  Lobby,         // fight mode: searching (a press still feeds him; a double press leaves)
  FoundingNest,  // first boot: three eggs
  ClutchPick,    // the eggs after a vigil
  Vigil,         // his remains, before the eggs
  Egg,           // incubating
  Asleep,
  Awake,         // idle, playing, eating: the ordinary pet
};

enum class BootAct : uint8_t {
  Nothing,
  DropPellet,        // stim::button -> Habitat::dropPellet (today's behaviour)
  TuckIn,            // stim::button_hold -> the tuck-in stimulus gene (today's behaviour)
  OpenDuel,          // button_double (ButtonDetector) -> DishDuel opens with a LocalConsent
  NextEgg,           // stim::button in a Clutch or the nest (today's behaviour)
  KeepEgg,           // stim::button_hold in a Clutch or the nest (today's behaviour)
  PeekEgg,           // presentation only: who is inside, for 3 s
  LayFlower,         // presentation only: a flower on the remains
  OtaYes,            // ota::Session::press (after kMinAskMs): yes, update
  CloseDuel,         // leave: fight mode back to the dish, or the friends screen back to fight mode
  ConfirmTier,       // step the pick: Spar, Scrap, Mortal (the tilt never picks); unheld, it locks after 8 s
  ConfirmMortal,     // a 3 s hold locks the pick now; Mortal only ever this way
  QueueQuick,        // lock the aimed stance as a quick special's hidden landing (1 turn wind-up)
  QueueHeavy,        // the same, held 1.2 s: a heavy special (2 turns wind-up)
  Spare,             // the winner's mercy
  Finish,            // a 3 s hold while the skull fills: the winner ends him
  Plead,             // the downed side's only voice: a cheer floats up on the winner's screen
  AskRematch,
  Hug,               // a 3 s hold: yes to a friend egg (a cradle still works)
};

// The rim glyph for each act (art unit ART4; the tier, stance and power glyphs
// come from ART3). A greyed glyph means "not now" and says why with its pips
// (the meter short of the power's cost, an empty pantry).
enum class BootGlyph : uint8_t {
  None, Pellet, Moon, Swords, Peek, Flower, Arrow, Check, Lock, Download, Friend, Door, Tier, Skull,
  SpecialQuick, SpecialHeavy, OpenHand, Plead, Candle, Rematch, Heart, Countdown,
};

struct BootMeaning {
  BootContext context;
  BootAct press, hold, doublePress;
  BootGlyph hint;          // what the press does; a hold ring is drawn when `hold` differs and is not Nothing
  uint16_t holdMs;         // how long the hold is (the firmware's button_hold is 1,200 ms; the dramatic ones are 3,000)
};

inline constexpr BootMeaning kBoot[] = {
    {BootContext::OtaQuestion, BootAct::OtaYes, BootAct::OtaYes, BootAct::OtaYes, BootGlyph::Download, 0},
    {BootContext::Sealed, BootAct::Nothing, BootAct::Nothing, BootAct::Nothing, BootGlyph::Lock, 0},
    {BootContext::Staking, BootAct::ConfirmTier, BootAct::ConfirmMortal, BootAct::Nothing, BootGlyph::Tier, 3000},
    {BootContext::DuelTurn, BootAct::QueueQuick, BootAct::QueueHeavy, BootAct::Nothing, BootGlyph::SpecialQuick, 1200},
    {BootContext::WindingUp, BootAct::Nothing, BootAct::Nothing, BootAct::Nothing, BootGlyph::Countdown, 0},
    {BootContext::MercyWinner, BootAct::Spare, BootAct::Finish, BootAct::Nothing, BootGlyph::OpenHand, 3000},
    {BootContext::MercyLoser, BootAct::Plead, BootAct::Plead, BootAct::Nothing, BootGlyph::Plead, 0},
    {BootContext::Fallen, BootAct::Nothing, BootAct::Nothing, BootAct::Nothing, BootGlyph::Candle, 0},
    {BootContext::DuelOutcome, BootAct::AskRematch, BootAct::Hug, BootAct::CloseDuel, BootGlyph::Rematch, 3000},
    {BootContext::Lobby, BootAct::DropPellet, BootAct::Nothing, BootAct::CloseDuel, BootGlyph::Pellet, 0},
    {BootContext::FoundingNest, BootAct::NextEgg, BootAct::KeepEgg, BootAct::Nothing, BootGlyph::Arrow, 1200},
    {BootContext::ClutchPick, BootAct::NextEgg, BootAct::KeepEgg, BootAct::Nothing, BootGlyph::Arrow, 1200},
    {BootContext::Vigil, BootAct::LayFlower, BootAct::Nothing, BootAct::Nothing, BootGlyph::Flower, 0},
    {BootContext::Egg, BootAct::PeekEgg, BootAct::Nothing, BootAct::Nothing, BootGlyph::Peek, 0},
    {BootContext::Asleep, BootAct::DropPellet, BootAct::TuckIn, BootAct::OpenDuel, BootGlyph::Pellet, 1200},
    {BootContext::Awake, BootAct::DropPellet, BootAct::TuckIn, BootAct::OpenDuel, BootGlyph::Pellet, 1200},
};

template <class T, size_t N> constexpr size_t bootCount(const T (&)[N]) { return N; }

// BootContext as the debug link names it.
inline constexpr const char* kBootContextNames[] = {
    "ota_question", "sealed", "staking", "turn", "winding_up", "mercy_winner", "mercy_loser", "fallen",
    "outcome",      "lobby",  "nest",    "clutch", "vigil",    "egg",          "asleep",      "awake"};
static_assert(bootCount(kBootContextNames) == size_t(BootContext::Awake) + 1, "a name per BootContext");
constexpr const char* bootContextName(BootContext c) { return kBootContextNames[size_t(c)]; }
constexpr bool everyContextOnce() {
  if (bootCount(kBoot) != size_t(BootContext::Awake) + 1) return false;
  for (size_t i = 0; i < bootCount(kBoot); ++i)
    if (size_t(kBoot[i].context) != i) return false;
  return true;
}
constexpr bool deathIsAlwaysAHold() {
  for (const BootMeaning& m : kBoot) {
    if (m.press == BootAct::Finish || m.doublePress == BootAct::Finish) return false;
    if (m.press == BootAct::ConfirmMortal || m.doublePress == BootAct::ConfirmMortal) return false;
    if ((m.hold == BootAct::Finish || m.hold == BootAct::ConfirmMortal) && m.holdMs < 3000) return false;
  }
  return true;
}
constexpr bool everyStateShowsAHint() {
  for (const BootMeaning& m : kBoot) if (m.hint == BootGlyph::None) return false;
  return true;
}
static_assert(everyContextOnce(), "boot.h: one row per BootContext, in order");
static_assert(deathIsAlwaysAHold(), "a press can never finish a pet or agree to a mortal duel: only a 3 s hold");
static_assert(everyStateShowsAHint(), "every state draws a hint: a kid never has to guess");

constexpr const BootMeaning& bootMeaning(BootContext c) { return kBoot[size_t(c)]; }

// Why a double press did not open fight mode, shown in words for a few
// seconds (a reason a Decline already names shows in fight mode itself).
enum class NoFight : uint8_t {
  None,
  Egg,         // still an egg: hatching soon
  Nest,        // the eggs to choose from (founding, or after a vigil)
  Remains,     // the vigil
  TooSleepy,   // asleep, and too sleepy to be woken for it
  Updating,    // an update holds the radio (its question, or this image on trial): fight mode opens once it keeps
};

// What Appearance carries every frame, for paint::drawBootHint.
struct BootHint {
  BootGlyph glyph = BootGlyph::Pellet;
  bool greyed = false;        // not now (meter short, pantry empty)
  uint8_t pips = 0;           // greyed: how many pips short; WindingUp: turns left
  uint8_t art = 0;            // Tier: the tier under the tilt; a special: the pet's PowerGlyph
  bool hasHold = false;       // a hold does something else: the ring is drawn
  uint8_t holdFill = 0;       // 0..255 while held
  bool hasDouble = false;     // two dots under the glyph when a double press does something
  uint8_t doubleFill = 0;     // 0..255: a first press is waiting for its second, draining as the window closes
};

}  // namespace blorb
