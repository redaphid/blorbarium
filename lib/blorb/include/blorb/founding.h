#pragma once
// How a device gets its species (docs/design/BATTLES.md, unit D10): a
// founding nest of three eggs, one of each element, rolled from the MAC
// (species.h's foundingNest), picked by the kid with the clutch gestures they
// will use for the rest of the pet's life. Three, not nine, so a family of
// devices spreads across the wheel.
//
// First boot only. A keepsake or a lineage, in any state, means no nest ever,
// so the gift device's grungo is never founded again. The firmware asks
// nestFor() before the Dish exists, feeds a FoundingChooser until it names a
// species, and founds the Dish from starterGenomeOf(pick); the line takes the
// species' name (lineage.cpp).
//
// The owner can pin a species over the cable first: `DEBUG found <id>` writes
// found.pin, and a fresh founding's nest is then that one egg, which needs no
// choice, as a clutch of one does not. The website cannot pin: the nest runs
// before the Dish and its Bluetooth exist.
#include <cstdint>
#include <optional>
#include "blorb/seams.h"
#include "blorb/senses.h"
#include "blorb/species.h"

namespace blorb {

// The eggs a fresh founding offers, lit one at a time.
struct Nest {
  SpeciesId eggs[kCycle]{};
  uint8_t count = 0;
};

// True when the Dish would found: no snapshot slot, no rescued slot and no
// lineage.log. Reads only, because a Keepsake load quarantines a torn slot.
bool needsFounding(Storage&);

// The pinned egg, else foundingNest(speciesSeed); nullopt when a pet lives
// here. A sealed board must not ask: its flash is left unread, so it looks empty.
std::optional<Nest> nestFor(Storage&, uint32_t speciesSeed);

// found.pin on petfs (its bytes are in founding.cpp). A pin naming a species
// this firmware has no row for reads as no pin.
std::optional<SpeciesId> readFoundPin(Storage&);
bool writeFoundPin(Storage&, SpeciesId);
bool clearFoundPin(Storage&);   // moved aside to found.off: nothing on petfs is deleted

// Pure over the samples it is fed, like the detectors, so the sim films it
// and a test scripts it. BOOT pressed or a knock lights the next egg; BOOT
// held 1.2 s or a double knock keeps the lit one (Clutch's pickEgg).
class FoundingChooser {
 public:
  // Nobody choosing for this long keeps the first egg, so a board that boots
  // alone on a shelf still founds. Lighting another egg starts it again, so a
  // slow chooser is never hurried.
  static constexpr uint32_t kTimeoutMs = 60000;
  explicit FoundingChooser(const Nest& nest) : nest_(nest) {}
  // Every IMU read, at 50 Hz. The chosen species once, then nullopt.
  std::optional<SpeciesId> sample(const BodySample&, uint32_t nowMs);
  uint8_t lit() const { return lit_; }   // the egg the glint is on, for the renderer
  const Nest& nest() const { return nest_; }

 private:
  Nest nest_;
  Detectors detectors_;
  uint32_t idleSinceMs_ = 0;
  uint8_t lit_ = 0;
  bool started_ = false, done_ = false;
};

}  // namespace blorb
