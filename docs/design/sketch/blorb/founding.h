#pragma once
// SKETCH (docs/design/SPECIES.md). How a device gets its species: a founding
// nest of three eggs, one of each element, rolled from the MAC
// (foundingNest), picked by the kid with the clutch gestures they will use
// for the rest of the pet's life. Graft from candidates B and C: three, not
// nine, so a family of devices spreads across the wheel (nine kids picking
// from nine converge on the coolest one, and mirror matches are the dullest).
//
// First boot only (nothing in `pet`, no lineage.log). Tilt or knock moves the
// glint, a double knock or a BOOT hold keeps the lit egg, and the Dish is
// founded from starterGenomeOf(pick). Nobody choosing for 60 s keeps the
// first egg, so a board that boots alone on a shelf still founds. The owner
// can pin a species over the cable first: `DEBUG found <id>` writes
// `found.pin` on petfs, and the next fresh founding's nest is that one egg
// (open question 1). The website cannot: the nest runs before the Dish and
// its Bluetooth exist.
// Every later egg inherits its species; a species changes only in a friend
// egg (friend_egg.h).
//
// Existing pets are untouched: a keepsake or a lineage means no chooser, and
// grungo d30f00d1 has no SpeciesGene, so he reads as slot 0 forever.
#include <cstdint>
#include <optional>
#include "blorb/seams.h"
#include "blorb/senses.h"
#include "blorb/species.h"

namespace blorb {

// True when the Dish would boot Fresh: no readable snapshot slot and no
// lineage.log. Reads only; the Dish's own boot logic is unchanged.
bool needsFounding(Storage&);

// Pure over the samples it is fed (50 Hz), like the detectors, so the sim
// films it and a test scripts it.
class FoundingChooser {
 public:
  static constexpr uint32_t kTimeoutMs = 60000;
  explicit FoundingChooser(uint32_t speciesSeed);   // the nest is foundingNest(speciesSeed)
  // The chosen species once (a knock on the lit egg, or the timeout), then nullopt.
  std::optional<SpeciesId> sample(const BodySample&, uint32_t nowMs);
  uint8_t lit() const { return lit_; }   // which of the three eggs the glint is on, for the renderer
  const FoundingNest& nest() const { return nest_; }
 private:
  FoundingNest nest_;
  uint32_t startMs_ = 0;
  uint8_t lit_ = 0;
  bool started_ = false, done_ = false;
};

}  // namespace blorb
