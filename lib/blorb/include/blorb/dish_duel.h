#pragma once
// The Dish's duel (docs/design/BATTLES.md, units D9): one member of Dish and
// one call in runOneTick, so dish.cpp grows by a few lines and nothing else in
// the engine learns what a duel is. The session (duel_session.h) plays the
// duel; this applies what it decides to the pet he is: wounds, scars, a fall,
// the book, the friend egg.
#include <cstdint>
#include <optional>
#include "blorb/air.h"
#include "blorb/boot.h"
#include "blorb/creature.h"
#include "blorb/diag.h"
#include "blorb/duel_book.h"
#include "blorb/duel_card.h"
#include "blorb/duel_session.h"
#include "blorb/lineage.h"
#include "blorb/stakes.h"

namespace blorb {

// The tilts that aim, as one IMU reading held still (milli-g, the device frame
// the Dish is handed, after the panel's rotation): what DuelInput::from reads
// as aim 0 Pounce (tilt left), 1 Guard (right), 2 Trick (toward the kid) and
// 3 the lobby's fourth friend (away). In Staking the first three are Spar,
// Scrap and Mortal. A bot or a tool holds one of these to aim;
// test_two_dishes holds each through the real detectors and checks the aim.
struct AimTilt { int8_t aim; int16_t ax, ay, az; };
inline constexpr AimTilt kAimTilt[4] = {
    {0, 500, 0, 866}, {1, -500, 0, 866}, {2, 0, -500, 866}, {3, 0, 500, 866}};

// Graft from candidate B, narrowed: a lineage founded before duels has no
// duel feeling genes, so its pet would not feel a battle. The first tick he
// is ready in fight mode the starter's duel genes (kDuelGenes) are appended at nextUid() (no uid
// is ever reused, DEVIATIONS 6), never at their fixed uids, so they never
// cross; recorded as one lineage Checkpoint, the path TWIST gene_edit uses.
// A genome that already reads any Duel-source stimulus is left alone, so a
// second call, or a line whose evolution later dropped one gene, is untouched.
// True when the genome changed (save at once).
bool graftDuelGenes(Creature&, Lineage&);

// Where BOOT is now, from the occupant and the open duel (null: none). The
// first rows of kBoot are not here: OtaQuestion is main.cpp's, which owns the
// update, and Sealed is the Dish's own. FoundingNest waits for unit D10.
BootContext bootContext(const Occupant&, const DuelView*);
// What the rim shows for it: the row's glyph, greyed when a press does nothing
// now, and the hold ring filling while BOOT is down.
BootHint bootHint(BootContext, const DuelView*, uint32_t heldMs);

// What one tick of the duel asks of the Dish.
struct DuelStepped {
  bool save = false;        // a wound, a scar, a downing, a fall, a graft, a meeting: save the keepsake now
};

class DishDuel {
 public:
  DishDuel() = default;
  // duels.log, and where friend.egg goes. Not called for a keepsake the Dish
  // will not write (sealed, newer firmware's): opening the book can truncate a torn tail.
  void attach(Storage&);

  // One tick, after the detectors and before the occupant:
  //  1 a body-sourced `button_double` (or `hang`: held upside down, the same
  //    ask from a case that covers BOOT) while BOOT means OpenDuel: readiness(),
  //    then open with a LocalConsent (made here and nowhere else). A refusal
  //    shows its picture; a radio OTA holds shakes his head. Nothing else opens
  //    it: fight mode is never the resting state.
  //  2 session.tick, with DuelInput from what a detector fired only
  //  3 the captured stimuli leave `raw`; the duel's own join it (Duel-sourced,
  //    through duelFilter at the agreed stakes)
  //  4 wounds and scars as each turn resolves; a downing, a fall, a Meeting,
  //    a friend egg, each written at once
  //  5 fight mode between fights: the pet's card refreshed, his readiness
  //    rechecked every tick; a pet who is no creature any more leaves it
  DuelStepped step(Occupant&, Lineage&, Air*, SenseOut& raw, Fx tiltX, Fx tiltY, bool bootDown,
                   uint64_t lineageId, uint16_t petDay, uint32_t tick);

  bool active() const { return session_.active(); }
  DuelView view(uint32_t tick) const { return session_.view(tick); }
  const DuelBook& book() const { return book_; }
  // The newest battle this boot recorded, for a log line that two boards'
  // logs can be compared by (its outcomeHash).
  const std::optional<Meeting>& lastMeeting() const { return lastMet_; }
  // A grown-up's ceiling (TWIST stakes, unit D14's duel.cfg). Mortal: every tier.
  void setDeviceCeiling(Stakes s) { deviceCeiling_ = s; }

  // The last ask that did not open a duel, for the screen: why (Decline), or
  // None when the radio was refused (an update holds it). A double press and
  // a hang share it; the log line says which one asked.
  struct Refusal { Decline why = Decline::None; uint32_t tick = 0; bool any = false; NoFight kind = NoFight::None; };
  const Refusal& refusal() const { return refusal_; }
  // An ask where BOOT means no fight (an egg, the nest, the vigil): the Dish's
  // routing says so, and the screen shows why.
  void refuse(NoFight, uint32_t tick, const char* gate = nullptr, bool hang = false);
  bool searching() const { return session_.searching(); }
  bool takeDiag(char (&out)[DiagLines::kLine]) { return diag_.take(out); }
  const char* lastGate() const { return lastGate_; }
  // Fight mode asked for while an update held the radio: it opens by itself
  // once the radio is free (an update that keeps, kPendingTicks at most).
  bool pending() const { return pendingSince_.has_value(); }
  static constexpr uint32_t kPendingTicks = 6000;   // 10 min: a trial keeps or rolls back within 5
  // A pet resting after his last fight: how long until he is up for another.
  uint32_t restTicksLeft(uint32_t tick) const;

 private:
  bool open(Occupant&, Lineage&, Air&, uint64_t lineageId, uint16_t petDay, uint32_t tick, DuelStepped&);
  // The rest after a duel is between sittings: a rematch searching for its friend skips it.
  Decline ready(const Occupant&, uint32_t tick) const;
  // This pet as he is now, for open() and for fight mode between fights.
  DuelSetup setupFor(const Creature*, uint64_t lineageId, uint16_t petDay, uint32_t tick, Decline);

  Storage* store_ = nullptr;
  DuelSession session_;
  DuelBook book_;
  // The session reads my genome after both hugs, maybe after he has fallen, so
  // it points at this copy, never at the creature.
  Genome genome_;
  Stakes deviceCeiling_ = Stakes::Mortal;
  uint32_t opens_ = 0;
  uint32_t lastEndTick_ = 0;
  bool dueledThisBoot_ = false, faintedLast_ = false, fell_ = false, battled_ = false;
  Refusal refusal_;
  std::optional<uint32_t> heldPress_;
  std::optional<uint32_t> pendingSince_;
  uint32_t lastTry_ = 0;
  DiagLines diag_;
  char lastGate_[40] = "none";
  // One ask, for the debug link and STATUS: `double` and `hang` say which
  // gesture asked (0 for both when fight mode opened by itself).
  void report(bool asked, bool hang, BootContext, const char* gate, const char* result);
  void gateOf(const Occupant&, Decline, uint32_t tick, char (&out)[32]) const;
  std::optional<Meeting> lastMet_;   // a press waiting to see if a second one follows
};

}  // namespace blorb
