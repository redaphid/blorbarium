#pragma once
// The presentation seam. present() is a pure function from state to a frame
// description: symbolic ids and a few continuous channels, never pixels. A
// sprite pack (grungo) owns timing, transitions and procedural motion; the
// engine owns what he is feeling and doing.
//
// Plain fixed-size data, so golden tests compare it as a struct and the sim
// renders it byte-identically run after run.
#include <cstdint>
#include "blorb/boot.h"
#include "blorb/creature.h"
#include "blorb/duel_card.h"
#include "blorb/feed_show.h"
#include "blorb/habitat.h"
#include "blorb/registry.h"
#include "blorb/senses.h"
#include "blorb/species.h"
#include "blorb/thoughts.h"

namespace blorb {

// Foresee is grungo's signature, so it must always read (ask 7): while the
// Foresee action runs, glow is at least this, whatever the genes say. Genes
// make it brighter and tint it; they cannot make it invisible.
constexpr Fx kForeseeGlowFloor = Fx::ratio(6, 10);

struct Appearance {
  // Kind selects the stage art: Egg, Creature (with `stage`: Baby/Child =
  // hatchling, Adult, Elder = old), Remains (the vigil), Clutch (the egg choice).
  enum class Kind : uint8_t { Egg, Creature, Remains, Clutch };
  Kind kind = Kind::Creature;
  uint16_t generation = 0;
  Stage stage = Stage::Baby;
  uint32_t lifeSeed = 0;            // genome hash: mottling layout, blink and bob jitter
  // Whose art draws him (paint::packFor): the phenotype's body species, so
  // grungo for a genome with no species gene.
  SpeciesId species = kGrungo;

  // Where and how
  DishPos at{};
  Fx facing{};
  uint8_t scalePct = 100;           // 100 = adult base; hatchlings ~55; size genes and locus add
  PoseId pose{};
  uint16_t poseTick = 0;

  // Face, with intensity. Below a pack threshold a weak face shows neutral;
  // `previous` and `exprTicks` let the pack crossfade (no glitch transition).
  ExprId expression{};
  ExprId previous{};
  Fx intensity{};
  uint16_t exprTicks = 0;

  // A running reflex overrides pose and face: hop = alarmed face, then
  // squash, leap (height from strength), land (ask 8).
  bool reflexActive = false;
  ReflexId reflex{};
  Fx reflexPhase{};                 // 0..1 through the reflex
  Fx reflexStrength{};

  // Colour. One tint per palette region, already including chemical-bound
  // shifts and the hue_shift locus; the pack clamps each to its band.
  Tint regions[kRegionCount]{};
  Fx glow{};                        // halo strength, floored while foreseeing
  bool foreseeing = false;

  struct Mark { uint8_t layer, variant; Tint tint; };
  Mark marks[8]{};
  uint8_t markCount = 0;

  bool asleep = false, dreaming = false, calling = false;
  Mouthful mouth = Mouthful::Nothing;   // drawn at his mouth, as the dish item it was
  Fx injury{}, wobble{};
  Fx night{};                       // 0..1 background tint from the pet clock

  // Battles. The scars he carries this life, drawn into his skin where they
  // landed; and what his line's duel book puts on the rim at home: the newest
  // trophies (the fallen's species, newest first) and the debt and vendetta marks.
  Scars scars{};
  static constexpr uint8_t kRimTrophies = 3;
  SpeciesId trophies[kRimTrophies]{};
  uint8_t trophyCount = 0;
  bool owesDebt = false, vendetta = false;

  // The dish
  struct Item { enum class What : uint8_t { Pellet, RottenPellet } what; DishPos at; };
  Item items[8]{};
  uint8_t itemCount = 0;
  uint8_t pantry = 0;               // drawn as rim pips so feeding is legible with no phone
  // Feeding as the eyes see it (feed_show.h): the knock knock heard, the
  // pellet's flight, a refusal and why, the chomp, and his belly. Ages in ms.
  FeedView feed{};

  // Egg, remains and clutch
  Fx eggProgress{};                 // whole -> cracking -> hatching
  Fx remainsFade{};                 // 0..1 through the vigil
  EggPreview eggs[kMaxClutch]{};
  uint8_t eggCount = 0, cursor = 0;

  // Care hint: the gesture that relieves the most pressing unmet drive (care.def).
  bool hasHint = false;
  CareId hint{};
  Fx hintUrgency{};

  StimId lastStim{};                // acknowledges a gesture even when the brain ignores it
  uint16_t ticksSinceStim = 0;
  // Boot::Sealed: a keepsake is on the flash and could not be opened. Drawn as
  // the pet asleep in amber with a line for a grown-up, never as a new life.
  bool sealed = false;

  // BOOT (boot.h): what a press does now, drawn on the rim beside the button
  // in every state (paint::drawBootHint), so a kid never has to guess.
  BootHint boot{};
  // A press only for the eyes, `echoTicks` ago: an egg peeked into
  // (PeekEgg), a flower laid on the remains (LayFlower).
  BootAct echo = BootAct::Nothing;
  uint16_t echoTicks = 0;
  // A double press that opened no duel, `refusedTicks` ago: why, as a picture
  // (zzz, a bowl, a hug), or None when the radio was the update's and he
  // shakes his head.
  bool refused = false;
  Decline declined = Decline::None;
  uint16_t refusedTicks = 0;
  // Why a double press opened no fight mode, in words on the strip (an egg,
  // the nest, too sleepy to wake, an update finishing), noFightTicks ago.
  NoFight noFight = NoFight::None;
  uint16_t noFightTicks = 0;
  uint16_t restTicksLeft = 0;   // resting after a fight: shown in fight mode
  // A duel is open: the duel scene replaces the dish, drawn from Dish::duelView().
  bool dueling = false;
  // Fight mode, which keeps the dish: it says it is looking (or connecting,
  // or why he is not up for it), how many friends it hears, and how long
  // until it gives up.
  bool lobby = false, lobbyConnecting = false;
  uint8_t lobbyFriends = 0;
  uint16_t lobbyTicksLeft = 0;
  Decline lobbyDecline = Decline::None;
  // A hang filling up, 0..1 (the hanging locus): paint draws it as a red
  // arc sweeping the rim, so the kid holding him upside down can see the
  // ask landing before fight mode opens. Zero whenever he is not hanging.
  Fx hangCharge{};

  // A line scrolling once across the dish (thoughts.h): the row, the line as
  // his voice says it, and how far through its pass (0..1).
  bool thinking = false, prophecy = false;
  ThoughtId thought{};
  char line[kThoughtLineCap]{};
  Fx thoughtPhase{};
};

Appearance present(const Occupant&, const Habitat&, const PetClock&, uint32_t tick);

// A genome with no creature: phenotype at `stage`, at rest. The family tree
// and the clutch preview use it.
Appearance portrait(const Genome&, Stage, uint16_t generation, uint32_t legacyFeats);

}  // namespace blorb
