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
      h.art = uint8_t(v->chosen);
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

void DishDuel::refuse(NoFight why, uint32_t tick, const char* gate, bool hang) {
  refusal_ = Refusal{Decline::None, tick, true, why};
  static constexpr BootContext kCtx[] = {BootContext::Awake, BootContext::Egg, BootContext::FoundingNest, BootContext::Vigil,
                                         BootContext::Asleep, BootContext::Awake};
  static constexpr const char* kGate[] = {"ok", "egg", "nest", "remains", "asleep", "radio"};
  report(true, hang, kCtx[uint8_t(why)], gate ? gate : kGate[uint8_t(why)], "refused");
}

void DishDuel::report(bool asked, bool hang, BootContext ctx, const char* gate, const char* result) {
  std::snprintf(lastGate_, sizeof lastGate_, "%s", gate);
  diag_.say("fight double=%d hang=%d ctx=%s gate=%s result=%s", int(asked && !hang), int(hang), bootContextName(ctx), gate,
            result);
}

// The gate as the debug link says it: "ok", or what stood in the way and how
// far (a drive as 0.00-1.00, a rest in seconds). No spaces, so STATUS can carry it.
void DishDuel::gateOf(const Occupant& o, Decline why, uint32_t tick, char (&out)[32]) const {
  const Creature* c = std::get_if<Creature>(&o);
  const auto drive2 = [&](DriveId d) {
    const int64_t v = c ? int64_t(c->chemistry().drive(d).raw) * 100 / Fx::kOne : 0;
    return unsigned(v < 0 ? 0 : v);
  };
  switch (why) {
    case Decline::None: std::snprintf(out, sizeof out, "ok"); break;
    case Decline::Asleep: std::snprintf(out, sizeof out, "asleep"); break;
    case Decline::Sleepy: std::snprintf(out, sizeof out, "sleepy:%u.%02u", drive2(drive::sleepiness) / 100, drive2(drive::sleepiness) % 100); break;
    case Decline::Hungry: std::snprintf(out, sizeof out, "hungry:%u.%02u", drive2(drive::hunger) / 100, drive2(drive::hunger) % 100); break;
    case Decline::Resting: std::snprintf(out, sizeof out, "resting:%us", unsigned(restTicksLeft(tick) / 10)); break;
    case Decline::WantsACuddle: std::snprintf(out, sizeof out, "cuddle"); break;
    case Decline::NotACreature: std::snprintf(out, sizeof out, "no_pet"); break;
    default: std::snprintf(out, sizeof out, "declined:%u", unsigned(why)); break;
  }
}

uint32_t DishDuel::restTicksLeft(uint32_t tick) const {
  if (!dueledThisBoot_ || session_.pinned()) return 0;
  const uint32_t rest = faintedLast_ ? kRestAfterFaintTicks : kRestTicks, since = tick - lastEndTick_;
  return since >= rest ? 0 : rest - since;
}

Decline DishDuel::ready(const Occupant& o, uint32_t tick) const {
  const uint32_t since = session_.pinned() || !dueledThisBoot_ ? UINT32_MAX : tick - lastEndTick_;
  return readiness(o, book_.lossesInARow(), since, faintedLast_);
}

DuelSetup DishDuel::setupFor(const Creature* c, uint64_t lineageId, uint16_t petDay, uint32_t tick, Decline why) {
  DuelSetup setup;
  setup.decline = why;
  setup.book = &book_;
  setup.petDay = petDay;
  setup.manualTurns = book_.manualTurns();
  setup.nonce = fnvWord(fnvWord(fnvWord(0x811c9dc5u, uint32_t(lineageId)), tick), ++opens_);
  if (c) {
    genome_ = c->genome();
    setup.genome = &genome_;
    setup.card = cardOf(*c, lineageId, book_.level(c->generation()), book_.prowess());
    setup.skill = book_.skill(c->generation());
    setup.ceiling = ceilingFor(*c, deviceCeiling_);
    for (Stakes s : {Stakes::Spar, Stakes::Scrap, Stakes::Mortal}) setup.skull[uint8_t(s)] = forecast(*c, s).skull;
  }
  return setup;
}

bool DishDuel::open(Occupant& o, Lineage& lineage, Air& air, uint64_t lineageId, uint16_t petDay, uint32_t tick,
                    DuelStepped& out) {
  Creature* c = std::get_if<Creature>(&o);
  const Decline why = ready(o, tick);
  if (c && why == Decline::None && graftDuelGenes(*c, lineage)) out.save = true;
  // A pet who declines still opens: his picture beacons to a friend nearby,
  // and he is never claimed (duel_session.h) until he is ready. Only a pet
  // who is no creature stays shut.
  if (why == Decline::NotACreature || !session_.open(LocalConsent{}, air, setupFor(c, lineageId, petDay, tick, why), tick)) {
    refusal_ = Refusal{why, tick, true};
    return false;
  }
  refusal_ = why != Decline::None ? Refusal{why, tick, true} : Refusal{};
  return true;
}

DuelStepped DishDuel::step(Occupant& o, Lineage& lineage, Air* air, SenseOut& raw, Fx tiltX, Fx tiltY, bool bootDown,
                           uint64_t lineageId, uint16_t petDay, uint32_t tick) {
  DuelStepped out;
  Creature* c = std::get_if<Creature>(&o);
  // He dozed off in the lobby: fight mode is an awake pet's state, so it
  // ends as he sleeps, before anything else this tick reads the session.
  if (c && air && session_.active() && session_.view(tick).phase == DuelPhase::Lobby && c->body().asleep)
    session_.close(*air, EndReason::Finished);
  // Three losses in a row want a cuddle, and a cuddle is all it takes.
  if (c && fromBody(raw, stim::cradle) && book_.lossesInARow() >= kLossesBeforeACuddle) book_.recordCuddle(tick);

  if (!session_.active()) {
    const bool askedHang = fromBody(raw, stim::hang);
    const bool asked = fromBody(raw, stim::button_double) || askedHang;
    const BootContext ctx = bootContext(o, nullptr);
    // Fight mode is the resting state: an awake creature with a radio asks
    // for himself, so two badges near each other find the fight with no
    // gesture at all. The hang and the double press remain the wake-up and
    // the shortcut; leaving the lobby just reopens it on the next tick.
    const bool autoAsk = air && c && ctx == BootContext::Awake;
    if (asked) drop(raw, kFightAskMask);
    if (pendingSince_ && (asked || tick - *pendingSince_ >= kPendingTicks)) {
      pendingSince_.reset();   // a second double press while it waits takes the ask back
      refusal_ = Refusal{};
      report(asked, askedHang, ctx, asked ? "cancelled" : "gave_up", "off");
      return out;
    }
    bool tryOpen = false, woke = false;
    if (asked && c && ctx == BootContext::Asleep) {
      // Asleep: woken for it, unless he is too sleepy to fight anyway.
      if (c->chemistry().drive(drive::sleepiness) >= kDeclineSleepy) {
        char gate[32];
        gateOf(o, Decline::Sleepy, tick, gate);
        char asleep[40];
        std::snprintf(asleep, sizeof asleep, "asleep:%s", gate);
        refuse(NoFight::TooSleepy, tick, asleep, askedHang);
        return out;
      }
      c->wake();
      woke = tryOpen = true;
    } else if ((asked || autoAsk) && ctx == BootContext::Awake) {
      tryOpen = true;
    } else if (pendingSince_ && ctx == BootContext::Awake && tick - lastTry_ >= 1000 / kTickMs) {
      tryOpen = true;
    }
    if (asked && !tryOpen) report(true, askedHang, ctx, bootContextName(ctx), "refused");
    if (!tryOpen) return out;
    lastTry_ = tick;
    const bool retry = pendingSince_.has_value();
    if (!air || !open(o, lineage, *air, lineageId, petDay, tick, out)) {
      // The radio said no with the pet ready: an update holds it. Ask again each second.
      if (!air || refusal_.why == Decline::None) {
        if (!retry) report(asked, askedHang, ctx, air ? air->refusal() : "no_radio", "waiting");
        if (!pendingSince_) pendingSince_ = tick;
        refusal_ = Refusal{Decline::None, *pendingSince_, true, NoFight::Updating};
      } else {
        report(asked, askedHang, ctx, "no_pet", "refused");
      }
      return out;
    }
    pendingSince_.reset();
    c = std::get_if<Creature>(&o);
    {
      char gate[32];
      const Decline why = ready(o, tick);
      if (woke && why == Decline::Asleep) std::snprintf(gate, sizeof gate, "woken");   // he wakes on the next tick
      else gateOf(o, why, tick, gate);
      report(asked, askedHang, ctx, gate, "entered");
    }
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
  if ((fx.radioClosed || fx.backToLobby) && battled_) {
    lastEndTick_ = tick;
    dueledThisBoot_ = true;
    battled_ = false;
  }
  // Fight mode between fights: he is re-read (wounds lower his ceiling), and
  // whether he is up for it is asked every tick, so a rest ends by itself.
  if (session_.searching()) {
    const Decline why = ready(o, tick);
    if (why == Decline::NotACreature) {
      session_.close(*air, EndReason::Finished);   // he fell, or is remains: fight mode ends with him
    } else {
      // The graft rides readiness, not the open: with fight mode always on
      // he may have opened it as a child, and the starter's duel genes are
      // owed to the first tick he is actually ready in the lobby.
      if (c && why == Decline::None && graftDuelGenes(*c, lineage)) out.save = true;
      // Refreshed every tick he searches, not only on the way back from a
      // fight: with fight mode always on he may have opened it as a child,
      // or hungry, and a friend must meet the pet he is now.
      session_.refresh(setupFor(std::get_if<Creature>(&o), lineageId, petDay, tick, why));
      session_.setDecline(why);
      if (why == Decline::None) faintedLast_ = false;   // the rest after a faint is over
    }
  }
  if (!session_.active()) fell_ = false;
  return out;
}

}  // namespace blorb
