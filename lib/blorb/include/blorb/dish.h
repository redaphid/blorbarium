#pragma once
// The Dish: the one object firmware and tests talk to. It owns the occupant,
// the habitat, the pet clock, the lineage, the keepsake and the protocol, and
// paces the simulation off the time it is handed. The firmware loop uses five
// calls: sample, tick, appearance, flush, and the constructor.
#include <cstdint>
#include <optional>
#include <vector>
#include "blorb/air.h"
#include "blorb/appearance.h"
#include "blorb/creature.h"
#include "blorb/dish_duel.h"
#include "blorb/habitat.h"
#include "blorb/keepsake.h"
#include "blorb/lineage.h"
#include "blorb/protocol.h"
#include "blorb/seams.h"
#include "blorb/senses.h"
#include "blorb/thoughts.h"

namespace blorb {

enum class Boot : uint8_t {
  Resumed,         // newest valid snapshot
  FellBack,        // newest slot torn; the older one loaded
  FromLineage,     // no readable snapshot: an egg of the latest genome, rebuilt from lineage.log
  Fresh,           // nothing stored: a founding egg from starterGenome(speciesSeed)
  ReadOnlyNewer,   // a newer firmware's keepsake: sealed (below) until the firmware catches up
  Sealed,          // a keepsake boot could not open (DishOptions::sealed): holds still, writes nothing
};

// The wall time a phone's TIME carried. A visit is one reading, so it is read
// once: unixSeconds() hands the value to the Dish and forgets it.
class PhoneTime : public TimeSource {
 public:
  void set(uint32_t unixSeconds) { pending_ = unixSeconds; }
  std::optional<uint32_t> unixSeconds() override {
    std::optional<uint32_t> t = pending_;
    pending_.reset();
    return t;
  }
 private:
  std::optional<uint32_t> pending_;
};

// The last unpowered gap the Dish simulated, how much of it the cost cap
// dropped, how long he was away, and whether that was long enough to hurt him.
struct CatchUp {
  uint32_t ticks = 0, clampedTicks = 0, awaySeconds = 0;
  bool hurt = false;
};

struct DishOptions {
  TimeSource* rtc = nullptr;           // first in priority: a clock that kept counting while unpowered
  const Genome* founder = nullptr;     // a fresh lineage's first genome; default starterGenome(speciesSeed)
  // The flash holds a pet that would not open and was left alone (provision.h).
  // The Dish reads nothing, writes nothing and runs no life, so a reset
  // finds the keepsake exactly as it was; the screen shows it sealed.
  bool sealed = false;
  // A board nobody can answer (no phone, no battery) shows no time-unknown
  // marquee and lets him think and prophesy without wall time.
  bool askForTime = true;
  // The duel radio (air.h). Null: this board has no duel radio, and a double
  // press only shakes his head. The firmware passes ESP-NOW behind OTA's
  // interlock, the sim its SerialAir, tests a FakeAir.
  Air* air = nullptr;
};

class Dish {
 public:
  // Loads the keepsake, or recovers from the lineage, or founds one. Never formats.
  Dish(Storage&, uint32_t speciesSeed, uint64_t lineageId, DishOptions = {});
  Boot boot() const { return boot_; }
  // The pet is on the flash and this firmware will not touch it: no life runs,
  // nothing is written, and the screen shows him asleep in amber.
  bool sealed() const { return boot_ == Boot::Sealed || boot_ == Boot::ReadOnlyNewer; }

  // Every IMU read, at 50 Hz. Runs the detectors only; effects land at the next tick.
  void sample(const BodySample&, uint32_t nowMs);

  // As often as you like. Runs zero or more 100 ms ticks to catch nowMs up
  // (at most 10 per call, so a frame is never starved), handles inbound
  // lines, pumps events, and saves when SavePolicy says so.
  void tick(uint32_t nowMs, Link&);

  Appearance appearance() const;
  bool flush();                    // explicit save (an update, a phone request): false when it did not land

  // For the command and twist handlers (protocol.cpp, twists.cpp).
  Occupant& occupant() { return live_.occupant; }
  const Occupant& occupant() const { return live_.occupant; }
  Habitat& habitat() { return live_.habitat; }
  const Habitat& habitat() const { return live_.habitat; }
  Lineage& lineage() { return lineage_; }
  Protocol& protocol() { return proto_; }
  const Settings& settings() const { return live_.settings; }
  PetClock& clock() { return live_.clock; }
  const PetClock& clock() const { return live_.clock; }
  uint32_t tickCount() const { return tick_; }
  void markDirty() { eventDirty_ = true; }
  bool pick(uint8_t egg);          // the pick twist and the body's clutch choice share this
  // As if a detector saw it, landing at the next tick, but marked as the
  // phone's (SenseOut::remote): the pet feels it, and a duel never takes it as a gesture.
  void fire(StimId);
  void think(ThoughtId);           // sim scripts and tests: he says that line now (thoughts.h)
  bool say(std::string_view line); // TWIST say: he croaks the phone's line now; false with no creature
  // One body byte of the creature's genome, recorded in the lineage. False
  // when there is no creature, the uid is missing or the offset is past the body.
  bool editGene(GeneUid, uint8_t offset, uint8_t value);
  void rename(const char* name);
  const Snapshot& live() const { return live_; }   // what a save and SNAPSHOT see

  // Duels (dish_duel.h). The view is plain data for the duel scene and the
  // films; Closed when none is open.
  bool duelActive() const { return duel_.active(); }
  DuelView duelView() const { return duel_.view(tick_); }
  const DuelBook& duels() const { return duel_.book(); }
  const std::optional<Meeting>& lastMeeting() const { return duel_.lastMeeting(); }
  const DishDuel::Refusal& duelRefusal() const { return duel_.refusal(); }
  void setDuelCeiling(Stakes s) { duel_.setDeviceCeiling(s); }
  // Where BOOT is now (boot.h). Never OtaQuestion: that is main.cpp's.
  BootContext bootContext() const;
  uint32_t hash() const { return live_.hash(); }   // the replay check and HASH

  // Wall time (DEVIATIONS.md 3). checkWall reads the sources in order; on an
  // unaccounted gap it fast-forwards it, re-anchors and saves at once.
  PhoneTime& phoneTime() { return phone_; }
  void checkWall();
  bool timeKnown() const { return wallKnown_; }      // a source has given wall time this boot
  std::optional<uint32_t> wallNow() const;           // the anchor plus powered time since
  const CatchUp& lastCatchUp() const { return lastCatchUp_; }

 private:
  void runOneTick(Link&);
  bool askingForTime() const { return askForTime_ && !wallKnown_; }   // the marquee shows and he keeps quiet
  // Powered ticks since a phone last gave the time. Across a replug the saved
  // anchor stands in: with no battery only a phone can have set it.
  uint32_t phoneAwayTicks() const;
  void welcomePhone();
  void onDeath(Creature&);         // record death, lay the clutch, save
  bool onPicked(Clutch&, uint8_t); // record birth, the egg replaces the clutch, save; false: not now
  void routeBoot(BootContext, SenseOut&);
  void onHatch(Egg&);
  void catchUp(uint32_t ticks);
  void advanceClock(uint32_t ticks);
  bool save();

  Storage& store_;
  Keepsake keep_;
  Lineage lineage_;
  Protocol proto_;
  // The saved state, held whole so a save, a hash or SNAPSHOT reads it in
  // place. Its seq stays 0: the keepsake numbers its own saves.
  Snapshot live_{};
  Detectors detectors_;
  Thinker thinker_;                // presentation only: never saved or hashed
  Behaviours behaviours_;
  SenseOut pending_;               // what the detectors saw since the last tick
  DishDuel duel_;
  Air* air_ = nullptr;
  // BOOT's level, from the samples: a duel times its 3 s holds by it, and the
  // rim's hold ring fills with it.
  bool buttonDown_ = false;
  uint32_t buttonSinceMs_ = 0, lastSampleMs_ = 0;
  // The last press that is only for the eyes (an egg peeked at, a flower laid).
  BootAct echo_ = BootAct::Nothing;
  uint32_t echoTick_ = 0;
  PhoneTime phone_;
  TimeSource* rtc_ = nullptr;
  CatchUp lastCatchUp_;
  std::optional<uint32_t> lastPhoneTick_;
  Fx tiltX_ = Fx::ratio(1, 2), tiltY_ = Fx::ratio(1, 2);
  Boot boot_ = Boot::Fresh;
  uint32_t tick_ = 0, lastTickMs_ = 0, lastSaveTick_ = 0, lastWallCheckTick_ = 0;
  bool eventDirty_ = false, readOnly_ = false, wallKnown_ = false, wasConnected_ = false, askForTime_ = true;
};

}  // namespace blorb
