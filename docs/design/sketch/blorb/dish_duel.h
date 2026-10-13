#pragma once
// SKETCH (docs/design/BATTLES.md). The Dish's duel: one member of Dish and one call in
// runOneTick, so dish.cpp grows by a few lines and nothing else in the engine
// learns what a duel is.
//
// dish.h gains:
//   DishOptions::air          Air* (null: this board has no duel radio; the sim and tests pass a FakeAir)
//   private: DishDuel duel_;  stepped in runOneTick after the detectors and before the occupant
//   public:  DuelView duelView() const;  bool duelActive() const;  const DuelBook& duels() const;
// Appearance gains `SpeciesId species` (every frame: which pack draws him) and
// `bool dueling; DuelView duel;` (the duel scene replaces the dish while set).
#include <cstdint>
#include "blorb/creature.h"
#include "blorb/duel_book.h"
#include "blorb/duel_session.h"
#include "blorb/lineage.h"

namespace blorb {

// Graft from candidate B, narrowed: a lineage founded before duels has no
// duel feeling genes, so its pet would not feel a battle. On the first duel
// open (never at boot, never for a pet that never duels) the missing genes
// are appended once at nextUid() (DEVIATIONS 6: no uid is ever reused),
// idempotently by (gene kind, stimulus id), and recorded as a lineage
// Checkpoint, the path TWIST gene_edit already uses. Grafted genes sit off the
// fixed uids, so they never cross; they are not a crossable kind anyway. This
// edits a living keepsake genome and is called out in BATTLES.md (open
// question 6).
bool graftDuelGenes(Creature&, Lineage&);

class DishDuel {
 public:
  explicit DishDuel(Storage&);   // opens duels.log

  // One tick, between the detectors and the occupant:
  //  1 open: a body-detected `button_double` (v2: the wheel's duel row) while no
  //    duel runs and readiness() says None. A LocalConsent is made here and
  //    nowhere else. A refused open shows the decline's picture, or the pet
  //    shaking his head when the radio is leased (OTA, trial image).
  //    First open in a lineage that predates duels: graftDuelGenes (below).
  //  2 session.tick(air, DuelInput::from(raw, tiltX, tiltY), tick). Both read
  //    only stimuli a detector fired: Dish::fire (the twists' path) marks what
  //    it fires in SenseOut::remote, and DuelInput::from and the open gesture
  //    ignore those bits, so a phone can neither open a duel nor lock a move.
  //  3 removes `captured` from raw; appends the duel stimuli to raw (they
  //    reach the creature as StimSource::Duel, through kidSafe)
  //  4 on a Meeting: book.record (idempotent by BattleId)
  //  5 on a friend egg: FriendEggStore::save (the clutch reads it at the next death)
  // Returns true when the keepsake should be saved now (a genome graft went in).
  bool step(Occupant&, Lineage&, Air*, SenseOut& raw, Fx tiltX, Fx tiltY, uint64_t lineageId, uint16_t petDay,
            uint32_t tick);

  bool active() const { return session_.active(); }
  DuelView view(uint32_t tick) const { return session_.view(tick); }
  const DuelBook& book() const { return book_; }

 private:
  DuelSession session_;
  DuelBook book_;
  uint32_t opens_ = 0;
  uint32_t lastEndTick_ = 0;
  bool faintedLast_ = false;   // nonce = fnv(lineageId, tick, opens_): fresh per open, still replayable in tests
};

}  // namespace blorb
