#pragma once
// The Dish: the one object firmware and tests talk to. It owns the occupant,
// the habitat, the pet clock, the lineage, the keepsake and the protocol, and
// paces the simulation off the time it is handed. The firmware loop uses five
// calls: sample, tick, appearance, flush, and the constructor.
#include <cstdint>
#include "blorb/appearance.h"
#include "blorb/creature.h"
#include "blorb/habitat.h"
#include "blorb/keepsake.h"
#include "blorb/lineage.h"
#include "blorb/protocol.h"
#include "blorb/seams.h"
#include "blorb/senses.h"

namespace blorb {

enum class Boot : uint8_t {
  Resumed,         // newest valid snapshot
  FellBack,        // newest slot torn; the older one loaded
  FromLineage,     // both slots unreadable: an egg of the latest genome, rebuilt from lineage.log
  Fresh,           // nothing stored: a founding egg from starterGenome(speciesSeed)
  ReadOnlyNewer,   // a newer firmware's keepsake: runs a throwaway egg, writes nothing
};

class Dish {
 public:
  // Loads the keepsake, or recovers from the lineage, or founds one. Never formats.
  Dish(Storage&, uint32_t speciesSeed, uint64_t lineageId);
  Boot boot() const { return boot_; }

  // Every IMU read, at 50 Hz. Runs the detectors only; effects land at the next tick.
  void sample(const BodySample&, uint32_t nowMs);

  // As often as you like. Runs zero or more 100 ms ticks to catch nowMs up
  // (at most 10 per call, so a frame is never starved), handles inbound
  // lines, pumps events, and saves when SavePolicy says so.
  void tick(uint32_t nowMs, Link&);

  Appearance appearance() const;
  void flush();                    // explicit save: disconnect, a phone request

  // For the command handlers (protocol.cpp).
  Occupant& occupant() { return occ_; }
  const Occupant& occupant() const { return occ_; }
  Habitat& habitat() { return habitat_; }
  Lineage& lineage() { return lineage_; }
  Settings& settings() { return settings_; }
  PetClock& clock() { return clock_; }
  uint32_t tickCount() const { return tick_; }
  void markDirty() { dirty_ = true; }
  bool pick(uint8_t egg);          // PICK and the body's clutch choice share this
  void armConsent();               // a kConsent verb arrived; a button hold in 20 s grants it
  bool takeConsent();              // true once per grant

 private:
  void runOneTick(Link&);
  void onDeath(Creature&);         // record death, lay the clutch, save
  void onPicked(Clutch&, uint8_t); // record birth, the egg replaces the clutch, save
  void onHatch(Egg&);

  Storage& store_;
  Keepsake keep_;
  Lineage lineage_;
  Protocol proto_;
  Occupant occ_;
  Habitat habitat_;
  PetClock clock_;
  Settings settings_;
  Detectors detectors_;
  Behaviours behaviours_;
  SenseOut pending_;               // what the detectors saw since the last tick
  Boot boot_ = Boot::Fresh;
  uint32_t tick_ = 0, lastTickMs_ = 0, lastSaveTick_ = 0, consentUntilTick_ = 0;
  bool dirty_ = false, consentGranted_ = false;
  Rng rng_{};
};

}  // namespace blorb
