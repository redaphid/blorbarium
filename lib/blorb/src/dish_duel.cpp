#include "blorb/dish_duel.h"

#include <algorithm>
#include <vector>
#include "blorb/friend_egg.h"
#include "blorb/genes.h"
#include "blorb/starter_duel.h"

namespace blorb {
namespace {

bool fired(const SenseOut& s, StimId id) {
  for (uint8_t i = 0; i < s.stimCount; ++i) if (s.stimuli[i] == id) return true;
  return false;
}
bool fromBody(const SenseOut& s, StimId id) { return fired(s, id) && !(s.remote & duelStimBit(id)); }

void drop(SenseOut& s, uint64_t bits) {
  uint8_t kept = 0;
  for (uint8_t i = 0; i < s.stimCount; ++i)
    if (s.stimuli[i].v >= 64 || !(bits & duelStimBit(s.stimuli[i]))) s.stimuli[kept++] = s.stimuli[i];
  s.stimCount = kept;
  s.remote &= ~bits;
}

constexpr bool isDuelStim(uint8_t id) {
  for (const StimInfo& s : STIMULI) if (s.id.v == id) return s.source == StimSource::Duel;
  return false;
}

// Reads a Duel-source stimulus: a line that already feels duels.
bool feelsDuels(const Genome& g) {
  bool any = false;
  g.forEach([&](const GeneView& v) {
    any = any || (v.header.type == GeneKindOf<StimulusGene>::value && v.header.len >= 1 && isDuelStim(v.body[0]));
  });
  return any;
}

}  // namespace

bool graftDuelGenes(Creature& c, Lineage& lineage) {
  if (feelsDuels(c.genome())) return false;
  GenomeBuilder b = GenomeBuilder::from(c.genome());
  uint16_t uid = c.genome().nextUid().v;
  for (size_t i = 0; i < kDuelGeneCount; ++i) {
    GeneHeader h = kDuelGenes[i].header;
    h.uid = GeneUid{uid++};
    b.append(h, kDuelGenes[i].body);
  }
  std::optional<Genome> grown = b.build();
  if (!grown || !c.extendGenome(std::move(*grown))) return false;
  lineage.recordEdit(c.generation(), c.genome());
  return true;
}

BootContext bootContext(const Occupant& o, const DuelView* v) {
  if (v && v->phase != DuelPhase::Closed) {
    switch (v->phase) {
      case DuelPhase::Lobby:
      case DuelPhase::Greeting: return BootContext::Lobby;
      case DuelPhase::Staking: return BootContext::Staking;
      case DuelPhase::Choosing:
      case DuelPhase::Waiting:
      case DuelPhase::Revealing: return v->mine.queued != Windup::None ? BootContext::WindingUp : BootContext::DuelTurn;
      case DuelPhase::Mercy: return v->iAmDown ? BootContext::MercyLoser : BootContext::MercyWinner;
      case DuelPhase::Fallen: return BootContext::Fallen;
      case DuelPhase::Fainting:
      case DuelPhase::Friends:
      case DuelPhase::Closed: return BootContext::DuelOutcome;
    }
  }
  if (std::holds_alternative<Egg>(o)) return BootContext::Egg;
  if (const Clutch* k = std::get_if<Clutch>(&o))
    return k->sinceDeath < Clutch::kVigilTicks ? BootContext::Vigil : BootContext::ClutchPick;
  return std::get<Creature>(o).body().asleep ? BootContext::Asleep : BootContext::Awake;
}

BootHint bootHint(BootContext ctx, const DuelView* v, uint32_t heldMs) {
  const BootMeaning& m = bootMeaning(ctx);
  BootHint h;
  h.glyph = m.hint;
  h.hasDouble = m.doublePress != BootAct::Nothing;
  h.hasHold = m.hold != BootAct::Nothing && m.hold != m.press && m.holdMs > 0;
  if (h.hasHold && heldMs > 0) h.holdFill = uint8_t(std::min<uint32_t>(255, heldMs * 255 / m.holdMs));
  if (!v) return h;
  switch (ctx) {
    case BootContext::Staking:
      h.art = uint8_t(v->aim >= 0 && v->aim <= 2 ? v->aim : uint8_t(v->chosen));
      h.greyed = v->confirmed;
      break;
    case BootContext::DuelTurn:
      h.art = uint8_t(powerInfo(v->me.power).glyph);
      h.greyed = v->phase != DuelPhase::Choosing || v->locked || !v->specialReady;
      h.pips = v->phase == DuelPhase::Choosing && !v->locked ? v->pipsShort : 0;
      break;
    case BootContext::WindingUp:
      h.art = uint8_t(powerInfo(v->me.power).glyph);
      h.pips = v->mine.landsOn > v->lastTurn ? uint8_t(v->mine.landsOn - v->lastTurn) : 0;
      break;
    case BootContext::DuelOutcome: h.greyed = v->phase == DuelPhase::Fainting; break;
    default: break;
  }
  return h;
}

void DishDuel::attach(Storage& s) {
  store_ = &s;
  book_ = DuelBook::open(s);
}

bool DishDuel::open(Occupant& o, Lineage& lineage, Air& air, uint64_t lineageId, uint16_t petDay, uint32_t tick,
                    std::optional<DuelSetup::Pin> pin, DuelStepped& out) {
  Creature* c = std::get_if<Creature>(&o);
  // A rematch is the same sitting: the rest after a duel is between duels.
  const uint32_t since = pin || !dueledThisBoot_ ? UINT32_MAX : tick - lastEndTick_;
  const Decline why = readiness(o, book_.lossesInARow(), since, faintedLast_);
  if (c && why == Decline::None && graftDuelGenes(*c, lineage)) out.save = true;
  DuelSetup setup;
  setup.decline = why;
  setup.book = &book_;
  setup.petDay = petDay;
  setup.manualTurns = book_.manualTurns();
  setup.pin = pin;
  setup.nonce = fnvWord(fnvWord(fnvWord(0x811c9dc5u, uint32_t(lineageId)), tick), ++opens_);
  if (c) {
    genome_ = c->genome();
    setup.genome = &genome_;
    setup.card = cardOf(*c, lineageId, book_.level(c->generation()), book_.prowess());
    setup.skill = book_.skill(c->generation());
    setup.ceiling = ceilingFor(*c, deviceCeiling_);
    for (Stakes s : {Stakes::Spar, Stakes::Scrap, Stakes::Mortal}) setup.skull[uint8_t(s)] = forecast(*c, s).skull;
  }
  // A pet who declines still opens: his picture beacons to a friend nearby,
  // and he is never matched (duel_session.h). Only a pet who is no creature stays shut.
  if (why == Decline::NotACreature || !session_.open(LocalConsent{}, air, setup, tick)) {
    refusal_ = Refusal{why, tick, true};
    return false;
  }
  if (why != Decline::None) {
    refusal_ = Refusal{why, tick, true};
  } else {
    refusal_ = Refusal{};
    if (!pin) faintedLast_ = false;   // read by readiness above: the rest after the last duel's faint
  }
  return true;
}

DuelStepped DishDuel::step(Occupant& o, Lineage& lineage, Air* air, SenseOut& raw, Fx tiltX, Fx tiltY, bool bootDown,
                           uint64_t lineageId, uint16_t petDay, uint32_t tick) {
  DuelStepped out;
  Creature* c = std::get_if<Creature>(&o);
  // Three losses in a row want a cuddle, and a cuddle is all it takes.
  if (c && fromBody(raw, stim::cradle) && book_.lossesInARow() >= kLossesBeforeACuddle) book_.recordCuddle(tick);

  if (!session_.active()) {
    if (!fromBody(raw, stim::button_double) || bootContext(o, nullptr) != BootContext::Awake) return out;
    drop(raw, duelStimBit(stim::button_double));
    if (!air) {
      refusal_ = Refusal{Decline::None, tick, true};
      return out;
    }
    if (!open(o, lineage, *air, lineageId, petDay, tick, std::nullopt, out)) return out;
    c = std::get_if<Creature>(&o);
  }
  if (!air) return out;

  // Where a double press also means something (the lobby: close; the friends
  // screen: leave), a press waits out the double's window first, so a kid
  // leaving never asks for a rematch on the way out.
  {
    const DuelView v = session_.view(tick);
    const bool both = bootMeaning(bootContext(o, &v)).doublePress != BootAct::Nothing;
    const uint32_t window = kDoublePressMs / kTickMs + 1;
    if (fired(raw, stim::button_double)) heldPress_.reset();
    if (both && fromBody(raw, stim::button)) {
      drop(raw, duelStimBit(stim::button));
      heldPress_ = tick;
    }
    // A second press still down may yet be the double: it fires on release.
    if (heldPress_ && (!both || (tick - *heldPress_ >= window && !bootDown))) {
      raw.fire(stim::button);
      heldPress_.reset();
    }
  }
  const DuelInput in = DuelInput::from(raw, raw.remote, tiltX, tiltY, bootDown);
  DuelEffects fx = session_.tick(*air, in, tick);
  drop(raw, fx.captured);
  for (uint8_t i = 0; i < fx.stimCount; ++i) raw.fire(fx.stimuli[i]);

  if (c && session_.active()) {
    const DuelView v = session_.view(tick);
    if (uint8_t(v.phase) > uint8_t(DuelPhase::Staking)) c->setDuelStakes(v.agreed);
  }
  for (uint8_t i = 0; c && i < fx.woundCount; ++i) {
    c->wound(fx.wounds[i].injury);
    if (fx.wounds[i].scar) c->addScar(fx.wounds[i].mark);
    out.save = true;
  }
  for (uint8_t i = 0; c && i < fx.stimCount; ++i)
    if (fx.stimuli[i] == stim::duel_fainted) {
      c->force(action::sleep);   // daze holds the sleep gate open; this starts the nap
      faintedLast_ = true;
    }
  if (fx.downed) {
    book_.recordDowned(fx.battle, fx.victor, tick);
    out.save = true;
  }
  if (fx.fall && c) {
    c->fall();
    fell_ = true;
    out.save = true;
  }
  if (fx.met) {
    battled_ = true;
    const uint16_t gen = c ? c->generation() : lineage.currentGeneration();
    book_.record(*fx.met, gen, tick, petDay);
    lastMet_ = fx.met;
    fell_ = fell_ || fx.met->iFell || fx.met->iSlew;
    out.save = true;
  }
  if (fx.egg && store_) {
    FriendEggStore::save(*store_, *fx.egg);
    out.save = true;
  }
  // The rest is after a battle: a lobby that only beaconed a picture, or
  // found nobody, asks none.
  if (fx.radioClosed && battled_) {
    lastEndTick_ = tick;
    dueledThisBoot_ = true;
    battled_ = false;
  }
  if (fx.rematch && !fell_ && std::holds_alternative<Creature>(o))
    open(o, lineage, *air, lineageId, petDay, tick, fx.rematch, out);
  if (!session_.active()) fell_ = false;
  return out;
}

}  // namespace blorb
