// SKETCH (docs/design/BATTLES.md). The caller's view, compiled
// (-fsyntax-only). Five callers: the firmware's first boot and frame, the
// Dish's one duel call, a native test of two devices over a lossy ether, the
// friend egg at the clutch, and the pure core.
#include <cassert>
#include <optional>

#include "blorb/dish_duel.h"
#include "blorb/duel.h"
#include "blorb/duel_card.h"
#include "blorb/duel_session.h"
#include "blorb/founding.h"
#include "blorb/friend_egg.h"
#include "blorb/stakes.h"
#include "blorb/paint_duel.h"
#include "blorb/species.h"
#include "hw/radio.h"
#include "test/fake_air.h"
#include "tools/duel_balance.h"

using namespace blorb;

namespace blorb {
struct DuelTestAccess {   // test/support/duel_access.h
  static LocalConsent consent() { return LocalConsent{}; }
};
}  // namespace blorb

// ---- 1. firmware, first boot (src/main.cpp setup) -----------------------------------
// A fresh device shows its nest of three eggs; everyone else boots as today.
std::optional<Genome> chooseFounder(Storage& store, uint32_t speciesSeed, BodySample (*read)(), uint32_t (*nowMs)()) {
  if (!needsFounding(store)) return std::nullopt;   // a pet already lives here: keep him
  FoundingChooser chooser(speciesSeed);
  for (;;) {
    // (draws the three eggs of chooser.nest() with the glint on chooser.lit() every frame)
    if (std::optional<SpeciesId> picked = chooser.sample(read(), nowMs())) return starterGenomeOf(*picked, speciesSeed);
  }
}
// then: options.founder = &*founder; options.air = &espNowAir; dish.emplace(store, seed, lineage, options);

// ---- 2. firmware, one frame (src/main.cpp loop) ---------------------------------------
void frame(const Appearance& me, SpeciesId mySpecies, const DuelView* duel, paint::Canvas240& canvas, uint32_t now) {
  if (duel && duel->phase != DuelPhase::Closed)
    paint::drawDuel(*duel, me, paint::packFor(mySpecies), paint::packFor(duel->them.species), canvas, now);
  // else paint::draw(me, paint::packFor(mySpecies), canvas, now) as today
}

// OTA's own interlock (branch `ota`): an update refuses while busy(), and a
// duel holding the radio makes busy() true. The duel's open() asks the gate.
bool otaBusy(const hw::EspNowAir& air, bool dishBusy) { return dishBusy || air.isOpen(); }

// ---- 3. the Dish's one call (dish.cpp runOneTick) ----------------------------------------
void dishTick(DishDuel& duel, Occupant& occupant, Lineage& lineage, Air* air, SenseOut& raw, Fx tiltX, Fx tiltY,
              uint16_t petDay, uint32_t tick, bool& saveNow) {
  saveNow |= duel.step(occupant, lineage, air, raw, tiltX, tiltY, 0x9f31c2d04a7bull, petDay, tick);
  // raw now holds: shakes, holds, drops and lids untouched; lock-in knocks
  // removed; duel_bonked / duel_landed / duel_won / duel_fainted /
  // duel_befriended added. The creature applies the duel ones through
  // duelFilter() at the agreed stakes; wounds come from the Outcome, not genes:
  assert(duelFilter(Stakes::Mortal, chem::injury, Fx::ratio(1, 2)) == Fx::zero());
}

// A pet who is not up for it says why, on both screens.
bool mayOpen(const Occupant& o, const DuelBook& book, uint32_t ticksSinceLast) {
  return readiness(o, book.lossesInARow(), ticksSinceLast, false) == Decline::None;
}

// ---- 4. native test: two devices, one lossy ether, the same battle ----------------------
struct Script {   // a kid: aim, then knock; sometimes power up; sometimes shake
  uint32_t seed;
  DuelInput at(const DuelView& v, uint32_t tick) const {
    DuelInput in;
    if (v.phase == DuelPhase::Lobby && v.peerCount > 0) { in.aim = 0; in.knock = tick % 7 == 0; }
    if (v.phase == DuelPhase::Choosing) {
      in.aim = int8_t(fnv1a(&tick, sizeof tick, seed) % 3);
      in.press = v.mine.spark >= powerInfo(v.me.power).cost;
      in.shake = tick % 97 == 0;
      in.knock = tick % 13 == 0;
    }
    return in;
  }
};

bool twoDevicesAgree(uint64_t seed, const Genome& gA, const Genome& gB) {
  blorbtest::Ether ether(seed, blorbtest::Weather{300, 100, 200, 8});
  blorbtest::FakeAir airA(ether, PeerAddr{{2, 0, 0, 0, 0, 1}}), airB(ether, PeerAddr{{2, 0, 0, 0, 0, 2}});
  BattleCard grungo, mom;
  mom.species = SpeciesId{6};                  // fizz
  mom.power = speciesInfo(mom.species).power;
  mom.prowess = 220;                           // a parent who reads; grungo's kid is at 128
  assert(Assist::hearts(grungo.prowess, mom.prowess) == 2);

  uint8_t skulls[3] = {0, 0, 40};
  DuelSession a, b;
  if (!a.open(DuelTestAccess::consent(), airA, grungo, DuelSkill{}, gA, Stakes::Mortal, skulls, 11, 0)) return false;
  if (!b.open(DuelTestAccess::consent(), airB, mom, DuelSkill{}, gB, Stakes::Scrap, skulls, 22, 0)) return false;
  const Script kidA{1}, kidB{2};
  std::optional<Meeting> metA, metB;
  for (uint32_t t = 1; t < 30000 && !(metA && metB); ++t) {
    ether.advance(t);
    DuelEffects ea = a.tick(airA, kidA.at(a.view(t), t), t);
    DuelEffects eb = b.tick(airB, kidB.at(b.view(t), t), t);
    if (ea.met) metA = ea.met;
    if (eb.met) metB = eb.met;
  }
  // Mortal was asked on one side and Scrap on the other: the duel ran at Scrap.
  return metA && metB && metA->battle == metB->battle && metA->battle.stakes == Stakes::Scrap &&
         metA->outcomeHash == metB->outcomeHash && metA->me != metB->me;
}

// The consent rule, as a test: a device whose owner never opened hears
// nothing and sends nothing, however many friends beacon at it.
bool closedDeviceIsDeaf(const Genome& g) {
  blorbtest::Ether ether(7, blorbtest::Weather{});
  blorbtest::FakeAir eager(ether, PeerAddr{{2, 0, 0, 0, 0, 1}}), bystander(ether, PeerAddr{{2, 0, 0, 0, 0, 2}});
  DuelSession e, by;
  uint8_t skulls[3] = {};
  e.open(DuelTestAccess::consent(), eager, BattleCard{}, DuelSkill{}, g, Stakes::Spar, skulls, 1, 0);
  for (uint32_t t = 1; t < 600; ++t) {
    ether.advance(t);
    DuelInput knocking;
    knocking.aim = 0;
    knocking.knock = t % 5 == 0;
    e.tick(eager, knocking, t);
    by.tick(bystander, knocking, t);   // knocking away, never opened
  }
  return !by.active() && bystander.framesSent == 0 && !bystander.receive();
}

// ---- 5. the friend egg at the clutch (clutch.cpp layClutch) ------------------------------
std::optional<Genome> friendEggChild(Storage& store, const Genome& parent, Rng& rng) {
  Genome friends = parent;
  std::optional<FriendEggRecord> rec = FriendEggStore::load(store, &friends);
  if (!rec) return std::nullopt;
  const MutationDiff cross = crossDiff(parent, friends, rng);   // MutPoint ops only: replays without the friend
  return apply(parent, cross);   // then mutate() as for any egg, with viability and the fallback
}

// ---- 6. the pure core ------------------------------------------------------------------
Ending twoTurns() {
  BattleCard a, b;
  b.species = SpeciesId{3};                // gilly
  MoveLog ma, mb;
  ma.append(Move{Stance::Pounce});
  mb.append(Move{Stance::Trick});          // pounce beats trick: gilly loses a heart
  ma.append(Move{Stance::Guard});
  mb.append(Move{Stance::Guard});          // a bonk of heads: sparks for both
  ma.append(Move{Stance::Trick, true});    // asks for foresee with 3 of its 5 pips: plays plain
  const Outcome o = resolve(a, b, duelSeed(11, 22, 0, Stakes::Spar), Stakes::Spar, ma, mb);   // gilly's third move is unknown: 2 turns resolve
  assert(o.resolved == 2);
  return o.ending;
}

// A mortal ending: the downed pet waits for the winner's verdict, which is
// the winner's last log slot. Both devices read the same verdict; each applies
// its own pet's fate.
bool finishedHim(const BattleCard& a, const BattleCard& b, uint32_t seed, const MoveLog& winner, const MoveLog& loser) {
  const Outcome o = resolve(a, b, seed, Stakes::Mortal, winner, loser);
  if (o.awaitingVerdict()) return false;   // the skull is still filling, or nobody has decided
  return o.downed[uint8_t(Side::B)] && o.verdict == Verdict::Finish;   // -> DuelEffects::fall -> Creature::fall()
}

// One resolved turn's wound, clamped to the room he has: no wound ever kills.
Fx turnWound(const Creature& c, const Outcome& o, uint8_t turn) {
  return woundFor(Stakes::Scrap, o, Side::A, turn, woundRoom(c)).injury;
}

// The stakes screen: the highest tier on offer, and how wounded the worst case would leave him.
Stakes offer(const Creature& c, Stakes deviceCeiling, uint8_t& skull) {
  const Stakes top = ceilingFor(c, deviceCeiling);
  skull = forecast(c, top).skull;
  return top;
}

blorbtool::BalanceReport tune() { return blorbtool::tournament(2000, blorbtool::Player::Random, blorbtool::Player::Random, 1); }

static_assert(beats(Stance::Pounce, Stance::Trick), "the rule a six-year-old learns first");
static_assert(beats(Element::Splash, Element::Spark), "water puts out sparks");
static_assert(speciesInfo(SpeciesId{8}).power == power::gadget, "tinker pulls a gadget");
