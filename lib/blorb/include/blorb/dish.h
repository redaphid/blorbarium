#pragma once
// The Dish: the one object firmware and tests talk to. It owns the occupant,
// the habitat, the pet clock, the lineage, the keepsake and the protocol, and
// paces the simulation off the time it is handed. The firmware loop uses five
// calls: sample, tick, appearance, flush, and the constructor.
#include <cstdint>
#include <optional>
#include <vector>
#include "blorb/air.h"
#include "blorb/carry_show.h"
#include "blorb/appearance.h"
#include "blorb/creature.h"
#include "blorb/dish_duel.h"
#include "blorb/feed_show.h"
#include "blorb/gesture_cues.h"
#include "blorb/habitat.h"
#include "blorb/keepsake.h"
#include "blorb/lineage.h"
#include "blorb/protocol.h"
#include "blorb/seams.h"
#include "blorb/senses.h"
#include "blorb/taught.h"
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
  // The pet hour a fresh founding's clock starts at. A clock that starts at
  // 0 opens a new life with seven hours of night, so the firmware, which has
  // no battery and so no wall time, founds at kUnaskedFoundingHour instead:
  // he hatches into his morning. A phone's TIME still snaps it to the wall.
  uint8_t foundingHour = 0;
  // The duel radio (air.h). Null: this board has no duel radio, and a double
  // press only shakes his head. The firmware passes ESP-NOW behind OTA's
  // interlock, the sim its SerialAir, tests a FakeAir.
  Air* air = nullptr;
  // An egg holds still, neither incubating nor hatching, until the owner
  // adopts it from the page (ADOPT), or the debug link's FREEHATCH lifts it.
  // The firmware sets it; the sim and tests keep eggs on their own clock.
  bool hatchNeedsPhone = false;
};

// The morning a no-phone board founds into (DishOptions::foundingHour): a
// pet day ahead before pet night at 21:00 (docs/notes/first-hours.md).
constexpr uint8_t kUnaskedFoundingHour = 9;

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
  // When, on tick()'s clock, the newest tick was due: the screen draws between
  // ticks from it (paint/tween.h). Read only; nothing here moves the life clock.
  uint32_t lastTickMs() const { return lastTickMs_; }
  void markDirty() { eventDirty_ = true; }
  bool pick(uint8_t egg);          // the pick twist and the body's clutch choice share this
  // As if a detector saw it, landing at the next tick, but marked as the
  // phone's (SenseOut::remote): the pet feels it, and a duel never takes it as a gesture.
  void fire(StimId);
  void think(ThoughtId);           // sim scripts and tests: he says that line now (thoughts.h)
  bool say(std::string_view line); // TWIST say: he croaks the phone's line now; false with no creature
  // TWIST teach: the line (in the font, 1 to kMaxTaughtText) joins what he
  // says now and then (taught.h), kept on the flash, and he says it now if he
  // has hatched. False, with nothing changed, when it cannot be written: a
  // sealed pet, or an image on trial.
  bool teach(std::string_view line);
  bool unteach();                  // TWIST unteach: he forgets every taught line; false as teach
  const Taught& taught() const { return taught_; }
  // One body byte of the creature's genome, recorded in the lineage. False
  // when there is no creature, the uid is missing or the offset is past the body.
  bool editGene(GeneUid, uint8_t offset, uint8_t value);
  void rename(const char* name);
  // A pet from before species becomes `species` (founding.h adoptSpecies),
  // recorded as one lineage Checkpoint and saved at once; a line still named
  // for grungo takes the species' name, and an owner's name stays. NotYet
  // while nothing could be written (an OTA trial's held storage), a duel is
  // open, or the pet is an egg or a clutch, so the caller asks again.
  // NotNeeded once he has a species, and on a sealed board. Grungo is adopted
  // like any species. Running it again after a power cut finds the saved pet
  // adopted, or adopts the unsaved one the same way.
  enum class Adoption : uint8_t { Adopted, NotNeeded, NotYet };
  Adoption adoptSpecies(SpeciesId, const LookBands&);
  const Snapshot& live() const { return live_; }   // what a save and SNAPSHOT see

  // Duels (dish_duel.h). The view is plain data for the duel scene and the
  // films; Closed when none is open.
  bool duelActive() const { return duel_.active(); }
  // One `! DEBUG` line (without the prefix): BOOT's presses and why a fight did or did not start.
  // For the cable's `DEBUG trace`: the body stimuli (ids < 64) the last sample fired, and the side hold's level.
  uint64_t firedLastSample() const { return lastFired_; }
  bool sideHolding() const { return detectors_.side.holding(); }
  // For the cable's `DEBUG latency` (src/latency.h): the hang's progress
  // above 0, and the aim the tilt reads as of the last sample, not the last
  // tick (-1 level). Reads only.
  bool hangCharging() const { return detectors_.hang.charge() > Fx::zero(); }
  // The last sample fired a shake hard enough to dismiss what is on the
  // screen (#35): the shell's questions (the update's, the debug unlock's)
  // take it for a no. The Dish's own (fight mode before a duel, a refusal's
  // words) it dismisses at the next tick.
  bool shakeDismisses() const { return (lastFired_ >> stim::shake.v & 1) && detectors_.motion.vigorous(); }
  int8_t aimNow() const { return detectors_.aim.aim(); }
  // Carried about, as the eyes see it (carry_show.h): presentation only. The
  // cable's `DEBUG carry <kind>` plays one now; tests count what started.
  const CarryShow& carry() const { return carry_; }
  void forceCarry(Carry k);
  bool takeDiag(char (&out)[DiagLines::kLine]) { return diag_.take(out) || duel_.takeDiag(out); }
  // Fight mode for STATUS: searching, paired, waiting (on an update) or off; and the last gate.
  const char* fightState() const;
  const char* lastFightGate() const { return duel_.lastGate(); }
  // The duel as the screen shows it: the session's, with the aim as of the
  // last sample (liveAim), not the last tick.
  DuelView duelView() const;
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
  // TIME's zone. From then on this boot his wall-clock lines read the owner's
  // local time; before it (or after a reboot) they stay silent.
  void setZone(int16_t tzMinutes) { zone_ = tzMinutes; }
  std::optional<uint32_t> localNow() const;          // wallNow() in that zone
  const CatchUp& lastCatchUp() const { return lastCatchUp_; }
  // The debug link's WALL (docs/debug-ble.md): forget the wall time and its
  // saved anchor, as a board that never met a phone. The next reading anchors
  // afresh and lives no gap. A time source still set will be read again.
  void forgetWall();
  // True while an egg is held back until the owner adopts it from the page
  // (ADOPT; DishOptions::hatchNeedsPhone). Adoption is kept in its own file,
  // so a replug does not ask again. A phone merely connecting does not count.
  bool hatchWaits() const { return hatchNeedsPhone_ && !hatchFree_ && !adopted_; }
  bool adopted() const { return adopted_; }
  bool adopt();                    // false when the flag cannot be written (a trial, a sealed pet)
  // The debug link's FREEHATCH: eggs run without a phone until the next reset. Never saved.
  void setHatchFree(bool free) { hatchFree_ = free; }
  bool hatchFree() const { return hatchFree_; }

 private:
  void runOneTick(Link&);
  // Powered ticks since a phone last gave the time. Across a replug the saved
  // anchor stands in: with no battery only a phone can have set it.
  uint32_t phoneAwayTicks() const;
  FromPhone fromPhone() const;     // what his lines may use beyond his own clock
  void welcomePhone();
  void onDeath(Creature&);         // record death, lay the clutch, save
  bool onPicked(Clutch&, uint8_t); // record birth, the egg replaces the clutch, save; false: not now
  void routeBoot(BootContext, SenseOut&);
  void feedView(Appearance&) const;
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
  // Every BOOT word in flight, from BOOT or its body twin: the hold as a
  // level (what the duel times its 3 s holds by), a first press or knock
  // waiting for its second, and how the rim's hint shows it (gesture_cues.h).
  GestureCues cues_;
  // Feeding as the eyes see it (feed_show.h): presentation only, never saved
  // or hashed. nowMs_ is the newest time the shell has handed in, a sample's
  // or a tick call's: the frame drawn next is about then.
  FeedShow feed_;
  // Carried about (carry_show.h): presentation only, never saved or hashed.
  CarryShow carry_;
  CarryContext carryContext(const BodySample&) const;
  uint32_t nowMs_ = 0;
  uint64_t lastFired_ = 0;
  // The duel's aim while turned onto a side is the aim from before the turn
  // (turning him tips the tilt): the last kAimHistory ticks of tilt.
  static constexpr uint8_t kAimHistory = 16;
  static constexpr uint8_t kAimBeforeTurn = 15;   // 1.5 s: the turn and orient.h's settle
  Fx tiltHist_[kAimHistory][2]{};
  Fx sideTilt_[2]{};
  bool sideWas_ = false, histPrimed_ = false;
  // The aim a knock is made at: the aim on the first knock's sample, which
  // the lock takes when the knock is decided ~0.6 s later (a hand drifts).
  int8_t knockAim_ = -1;
  // When a hard shake last dismissed something (#35), for the whoosh.
  uint32_t dismissedAtMs_ = 0;
  bool dismissed_ = false;
  bool knockWaitWas_ = false;
  int8_t liveAim() const;
  DiagLines diag_;
  // The last press that is only for the eyes (an egg peeked at, a flower laid).
  BootAct echo_ = BootAct::Nothing;
  uint32_t echoTick_ = 0;
  PhoneTime phone_;
  TimeSource* rtc_ = nullptr;
  CatchUp lastCatchUp_;
  std::optional<uint32_t> lastPhoneTick_;
  std::optional<int16_t> zone_;
  Taught taught_;                  // its own file, read once at boot; never saved with him or hashed
  Fx tiltX_ = Fx::ratio(1, 2), tiltY_ = Fx::ratio(1, 2);
  Boot boot_ = Boot::Fresh;
  uint32_t tick_ = 0, lastTickMs_ = 0, lastSaveTick_ = 0, lastWallCheckTick_ = 0;
  bool eventDirty_ = false, readOnly_ = false, wallKnown_ = false, wasConnected_ = false;
  bool hatchNeedsPhone_ = false, hatchFree_ = false, adopted_ = false;
};

}  // namespace blorb
