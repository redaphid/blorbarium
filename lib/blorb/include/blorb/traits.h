#pragma once
// What his genes make him, in words a kid (and the activity chooser) can use
// (#60). Each trait reads the genes that move it, as expressed so far, and
// says how far they sit from the shared starter recipe (starter_genome.cpp):
// 0 is the recipe, +100 is "twice as much", -100 "half as much". For a rate
// gene that is one doubling of its rate (12 bytes on the half-life scale);
// for the others the scale is said by each field.
//
// Derived, like the rest of the Phenotype: never saved, never hashed, never
// sent. Behaviour reads it (idle.cpp leans his idles by it; #61's day
// activities read it too), so two pets of one species in the same state do
// visibly different things when their genes differ, and a family that shares
// genes shares the habits. Names follow LINEAGE.md section 5 (#44) where the
// two overlap, so a v2 persona overlay can add to the same numbers.
#include <cstdint>
#include "blorb/genome.h"

namespace blorb {

struct Traits {
  static constexpr int16_t kMax = 400;   // four doublings either way, at most
  int16_t appetite = 0;     // the hunger tonic: hungry sooner
  int16_t sleepiness = 0;   // sleep pressure while awake: more naps (activity, #44, is its negative)
  int16_t nightOwl = 0;     // melatonin in the dark, slower: up later after dark
  int16_t playfulness = 0;  // the boredom tonic: wants play sooner
  int16_t affection = 0;    // the loneliness and need-touch tonics: wants company and hugs sooner
  int16_t nerves = 0;       // the fear tonic and a shake's fright: jumpier
  int16_t boldness = 0;     // the startle receptor's gain: leaps higher at a shake (+100 at the brashest row, -100 never)
  int16_t curiosity = 0;    // temperament explore: +100 at the byte's top, -200 at none
  int16_t cheer = 0;        // the happy face's weight: wears it more (+100 at the byte's top)
};

// The recipe's own bytes for the genes the traits read (starter_genome.cpp
// works them out from the same times it writes).
struct RecipeNorms {
  uint8_t hungerFill, sleepinessFill, melatoninFill, boredomFill, lonelinessFill, touchFill, fearFill;
  uint8_t startleGain, shakeFear, explore, happyWeight;
};
extern const RecipeNorms kRecipeNorms;

// Over the genes expressed at the stages in `stages` (a Phenotype's
// expressedStages): the later gene of a kind wins where the engine's does
// (temperament; a face shows by its strongest gene, as creature.cpp scores it), and a
// duplicated tonic adds its rate, as the chemistry does.
Traits traitsOf(const Genome&, uint8_t stages, uint32_t legacyFeats);

}  // namespace blorb
