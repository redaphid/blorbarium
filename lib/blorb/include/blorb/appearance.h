#pragma once
// The presentation seam. present() is a pure function from state to a frame
// description: symbolic ids and a few continuous channels, never pixels. A
// sprite pack (grungo) owns timing, transitions and procedural motion; the
// engine owns what he is feeling and doing.
//
// Plain fixed-size data, so golden tests compare it as a struct and the sim
// renders it byte-identically run after run.
#include <cstdint>
#include "blorb/creature.h"
#include "blorb/habitat.h"
#include "blorb/registry.h"
#include "blorb/senses.h"

namespace blorb {

// Foresee is grungo's signature, so it must always read (ask 7): while the
// Foresee action runs, glow is at least this, whatever the genes say. Genes
// make it brighter and tint it; they cannot make it invisible.
constexpr Fx kForeseeGlowFloor = Fx::ratio(6, 10);
// An elder greys and slows as his life falls from where the starter turns
// elder to where it dies (its become_elder and die receptors), so the ageing
// eases in from the stage switch and is complete at the end.
constexpr Fx kElderLife = Fx::ratio(46, 255), kLastLife = Fx::ratio(26, 255);

struct Appearance {
  // Kind selects the stage art: Egg, Creature (with `stage`: Baby/Child =
  // hatchling, Adult, Elder = old), Remains (the vigil), Clutch (the egg choice).
  enum class Kind : uint8_t { Egg, Creature, Remains, Clutch };
  Kind kind = Kind::Creature;
  uint16_t generation = 0;
  Stage stage = Stage::Baby;
  uint32_t lifeSeed = 0;            // genome hash: mottling layout, blink and bob jitter

  // Where and how
  DishPos at{};
  Fx facing{};
  uint8_t scalePct = 100;           // 100 = adult base; hatchlings ~55; size genes and locus add
  Fx elderly{};                     // 0 until he is an elder, then rising to 1 as his life runs out
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

  // The dish
  struct Item { enum class What : uint8_t { Pellet, RottenPellet, Marble } what; DishPos at; };
  Item items[8]{};
  uint8_t itemCount = 0;
  uint8_t pantry = 0;               // drawn as rim pips so feeding is legible with no phone

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
  bool timeUnknown = false;   // no wall-time source yet: the renderer asks for a phone visit
};

Appearance present(const Occupant&, const Habitat&, const PetClock&, uint32_t tick);

// A genome with no creature: phenotype at `stage`, at rest. The family tree
// and the clutch preview use it.
Appearance portrait(const Genome&, Stage, uint16_t generation, uint32_t legacyFeats);

}  // namespace blorb
