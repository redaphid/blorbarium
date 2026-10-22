#include "blorb/duel_session.h"

#include <algorithm>
#include <cstring>
#include "blorb/species.h"
#include "blorb/tilt_aim.h"

namespace blorb {
namespace {

using T = DuelTimings;
using Special = TurnReport::Special;

// woundFor's own clamp is turned off here: the Dish's Creature::wound clamps
// to woundRoom, which only the Dish can read.
constexpr Fx kNoClamp = Fx::one();

constexpr uint8_t u8(Stakes s) { return uint8_t(s); }
uint8_t fill(uint16_t ticks, uint16_t of) { return uint8_t(std::min<uint32_t>(255, uint32_t(ticks) * 255 / of)); }
uint8_t popcount(uint64_t v) {
  uint8_t n = 0;
  for (; v; v &= v - 1) ++n;
  return n;
}
uint8_t chunksOf(size_t bytes) { return uint8_t((bytes + kGenomeChunk - 1) / kGenomeChunk); }
uint64_t allOf(uint8_t chunks) { return chunks >= 64 ? ~uint64_t(0) : (uint64_t(1) << chunks) - 1; }

Ending wonBy(uint8_t side) { return side == 0 ? Ending::AWon : Ending::BWon; }
bool won(const Outcome& o, uint8_t side) { return o.ending == wonBy(side); }

TurnReport mirrored(const TurnReport& r, Side me) {
  if (me == Side::A) return r;
  TurnReport m = r;
  const auto flip = [](auto (&a)[2]) { std::swap(a[0], a[1]); };
  flip(m.move), flip(m.shown), flip(m.dozed), flip(m.bracing), flip(m.special), flip(m.hit), flip(m.healed);
  flip(m.fired), flip(m.powered), flip(m.blocked), flip(m.super), flip(m.zone);
  m.clash = r.clash == Clash::AWins ? Clash::BWins : r.clash == Clash::BWins ? Clash::AWins : Clash::Bonk;
  return m;
}

void addStim(DuelEffects& fx, StimId s) {
  for (uint8_t i = 0; i < fx.stimCount; ++i)
    if (fx.stimuli[i].v == s.v) return;
  fx.stim(s);
}

}  // namespace

DuelInput DuelInput::from(const SenseOut& raw, uint64_t remote, Fx tiltX, Fx tiltY, bool bootDown) {
  DuelInput in;
  in.bootDown = bootDown;
  for (uint8_t i = 0; i < raw.stimCount; ++i) {
    const StimId s = raw.stimuli[i];
    if (s.v < 64 && (remote & duelStimBit(s))) continue;
    if (s.v == stim::knock.v) in.knock = true;
    else if (s.v == stim::double_knock.v) in.doubleKnock = true;
    else if (s.v == stim::button.v) in.press = true;
    else if (s.v == stim::button_hold.v) in.hold = true;
    else if (s.v == stim::button_double.v) in.doublePress = true;
    else if (s.v == stim::hang.v) in.hang = true;
    else if (s.v == stim::shake.v) in.shake = true;
    else if (s.v == stim::cradle.v) in.cradle = true;
    else if (s.v == stim::lid_down.v) in.lid = true;
    else if (s.v == stim::dropped.v) in.dropped = true;
  }
  in.aim = tiltAim(tiltX, tiltY);   // the tilt picker (tilt_aim.h), which the clutch shares
  return in;
}

Ceiling pairCeiling(const Ceiling& pet, const BattleCard& mine, const BattleCard& theirs, uint32_t myManualTurns,
                    DuelBook::HeadToHead h2h, uint8_t forKeepsToday) {
  Ceiling c = pet;
  const uint8_t assist = std::max(Assist::hearts(mine.prowess, theirs.prowess), Assist::hearts(theirs.prowess, mine.prowess));
  const uint16_t lower = std::min(h2h.myWins, h2h.theirWins);
  const bool lopsided = h2h.duels >= kHeadToHeadMin && uint32_t(lower) * 1000 < uint32_t(kHeadToHeadFloor) * h2h.duels;
  for (uint8_t tier = u8(Stakes::Scrap); tier <= u8(Stakes::Mortal) && tier <= u8(c.top); ++tier) {
    const StakesRule& r = rule(Stakes(tier));
    if (assist > r.maxAssistHearts || myManualTurns < r.minManualTurns || (r.headToHead && lopsided) ||
        forKeepsToday >= kForKeepsPerPairPerDay) {
      c.top = Stakes(tier - 1);
      break;
    }
  }
  for (uint8_t tier = uint8_t(u8(c.top) + 1); tier <= u8(Stakes::Mortal); ++tier)
    if (c.why[tier] == Decline::None) c.why[tier] = Decline::Unfair;
  return c;
}

bool DuelSession::open(LocalConsent, Air& air, const DuelSetup& s, uint32_t tick) {
  if (active()) return true;
  if (!air.open()) return false;
  setup_ = s;
  self_ = air.self();
  modeSince_ = tick;
  lastEnd_ = EndReason::None;
  enterLobby(tick);
  return true;
}

void DuelSession::refresh(const DuelSetup& s) {
  if (!searching()) return;
  const uint32_t nonce = setup_.nonce;
  const std::optional<DuelSetup::Pin> pin = setup_.pin;
  setup_ = s;
  setup_.nonce = nonce, setup_.pin = pin;
}

void DuelSession::setDecline(Decline d) { setup_.decline = d; }

// Each search starts with no friends heard, so a pairing is only ever locked
// on a beacon sent since, and with a fresh nonce, so the next bout's id is new
// and a late State of the last one never passes for it.
void DuelSession::enterLobby(uint32_t tick) {
  Lobby l;
  l.openedTick = tick;
  l.lastBeacon = tick - T::kBeaconEvery;
  if (setup_.pin) l.wants = setup_.pin->peer;
  l.wantsSince = tick;
  setup_.nonce = duelSeed(setup_.nonce, tick, 0, Stakes::Spar);
  state_ = l;
}

void DuelSession::backToLobby(Air& air, uint32_t tick, EndReason why, bool goodbye, DuelEffects& fx, bool fought) {
  Battle* b = std::get_if<Battle>(&state_);
  Farewell* f = std::get_if<Farewell>(&state_);
  if (f) b = &f->last;
  if (goodbye && b && b->phase != DuelPhase::Greeting)
    for (uint8_t i = 0; i < T::kLeavingBurst; ++i) sendState(*b, air, b->lastSent, f, true);
  if (fought) modeSince_ = tick;
  lastEnd_ = why;
  enterLobby(tick);
  fx.backToLobby = true;
}

DuelEffects DuelSession::tick(Air& air, const DuelInput& in, uint32_t tick) {
  DuelEffects fx;
  if (!active()) return fx;
  fx.captured = searching() ? kLobbyCaptures : kHoldPhaseCaptures;
  aim_ = in.aim;
  if (auto* l = std::get_if<Lobby>(&state_)) lobbyTick(*l, air, in, tick, fx);
  else if (auto* b = std::get_if<Battle>(&state_)) battleTick(*b, air, in, tick, fx);
  else if (auto* f = std::get_if<Farewell>(&state_)) farewellTick(*f, air, in, tick, fx);
  fx.radioClosed = !active();
  return fx;
}

// ---- the lobby ----------------------------------------------------------------------

bool DuelSession::setAside(const PeerAddr& a, uint32_t tick, bool hardOnly) const {
  for (const SetAside& s : setAside_)
    if (s.addr == a && int32_t(s.until - tick) > 0 && (s.hard || !hardOnly)) return true;
  return false;
}

void DuelSession::setAsideFor(const PeerAddr& a, uint32_t tick, bool hard) {
  SetAside* slot = &setAside_[0];
  for (SetAside& s : setAside_) {
    if (s.addr == a) { slot = &s; break; }
    if (int32_t(s.until - slot->until) < 0) slot = &s;   // else the one that lapses first
  }
  const bool wasHard = slot->addr == a && slot->hard && int32_t(slot->until - tick) > 0;
  *slot = SetAside{a, tick + (hard ? T::kGreetAsideTicks : T::kSetAsideTicks), hard || wasHard};
}

std::optional<uint8_t> DuelSession::claim(Lobby& l, uint32_t tick) {
  // A claim on someone else is "already paired" only once it is answered.
  // One I can see go unanswered (I hear whom he claims, and they claim
  // somebody else or nobody) is a claim still pending: he is free to be
  // claimed, and a lock still needs both claims to meet. Without this, two
  // badges who each first heard a badge that hears nobody looked taken to
  // each other forever.
  const auto pending = [&](const Lobby::Seen& s) {
    for (uint8_t j = 0; j < l.count; ++j)
      if (l.seen[j].addr == s.beacon.wants) return l.seen[j].beacon.wants != s.addr;
    return false;   // whom he claims is out of my hearing: take him at his word
  };
  const auto open = [&](const Lobby::Seen& s) {
    return s.beacon.roster == rosterHash() && s.beacon.decline == Decline::None &&
           (s.beacon.wants.none() || s.beacon.wants == self_ || pending(s)) && (!setup_.pin || s.addr == setup_.pin->peer);
  };
  // A rematch's pinned lobby claims its friend even before it hears him: he
  // may still be on the friends screen, where he beacons nothing, and my
  // claim in a beacon is how he learns I asked if my State saying so was lost
  // (#54: under loss he waited out his friends screen alone).
  const auto want = [&](std::optional<uint8_t> i) {
    const PeerAddr to = i ? l.seen[*i].addr : setup_.pin && setup_.decline == Decline::None ? setup_.pin->peer : PeerAddr{};
    if (to != l.wants) l.wantsSince = tick;
    l.wants = to;
  };
  if (setup_.decline != Decline::None) {
    want(std::nullopt);
    return std::nullopt;
  }
  uint8_t current = kMaxLobbyHeard;
  for (uint8_t i = 0; i < l.count; ++i)
    if (open(l.seen[i]) && l.seen[i].addr == l.wants) current = i;
  // My claim's patience: a friend who claims nobody and has not claimed me
  // back in kClaimPatienceTicks may hear nobody at all, so he is set aside. A
  // friend whose own claim is pending hears the room and is only busy; he is
  // given kBusyPatienceTicks, jittered per badge, so a ring of badges each
  // waiting on the next (partial hearing) always breaks.
  if (current < kMaxLobbyHeard && !setup_.pin) {
    const Lobby::Seen& c = l.seen[current];
    const uint32_t patience = c.beacon.wants.none() ? T::kClaimPatienceTicks
                                                    : T::kBusyPatienceTicks + (setup_.nonce ^ self_.mac[5]) % 30;
    if (c.beacon.wants != self_ && tick - l.wantsSince >= patience) {
      setAsideFor(c.addr, tick, false);
      current = kMaxLobbyHeard;
    }
  }
  // Who I may claim: the open, less the set aside. A friend set aside for
  // never answering is still taken once he claims me; one whose greeting fell
  // through only when nobody else is open.
  bool pool[kMaxLobbyHeard]{};
  uint8_t pooled = 0;
  for (uint8_t i = 0; i < l.count; ++i) {
    const Lobby::Seen& s = l.seen[i];
    pooled += pool[i] = open(s) && !setAside(s.addr, tick, true) && (s.beacon.wants == self_ || !setAside(s.addr, tick, false));
  }
  if (!pooled)
    for (uint8_t i = 0; i < l.count; ++i)
      pool[i] = open(l.seen[i]) && setAside(l.seen[i].addr, tick, true);
  // A claim is kept while he stays in the pool (a claim already out is never
  // dropped for a better one: that is what keeps a badge in one fight). A new
  // claim goes to a friend already claiming me, else to the lowest address,
  // the one order every badge who hears the same room agrees on.
  std::optional<uint8_t> held;
  if (current < kMaxLobbyHeard && pool[current]) held = current;
  const auto better = [&](uint8_t i, uint8_t h) {
    const bool mi = l.seen[i].beacon.wants == self_, mh = l.seen[h].beacon.wants == self_;
    return mi != mh ? mi : l.seen[i].addr < l.seen[h].addr;
  };
  for (uint8_t i = 0; i < l.count && (!held || *held != current); ++i)
    if (pool[i] && (!held || better(i, *held))) held = i;
  want(held);
  if (held && l.seen[*held].beacon.wants == self_) return held;
  return std::nullopt;
}

void DuelSession::lobbyTick(Lobby& l, Air& air, const DuelInput& in, uint32_t tick, DuelEffects&) {
  while (std::optional<Datagram> d = air.receive()) {
    const std::optional<Packet> p = decode(d->bytes, d->len);
    const BeaconPacket* beacon = p ? std::get_if<BeaconPacket>(&*p) : nullptr;
    if (!beacon) continue;
    uint8_t slot = l.count;
    for (uint8_t i = 0; i < l.count; ++i)
      if (l.seen[i].addr == d->from) slot = i;
    // A late beacon never overrides a newer one: a claim only moves forward.
    if (slot < l.count && int16_t(uint16_t(beacon->seq - l.seen[slot].beacon.seq)) <= 0) continue;
    if (slot == kMaxLobbyHeard) {
      slot = l.wants == l.seen[0].addr ? 1 : 0;
      for (uint8_t i = 0; i < l.count; ++i)
        if (l.seen[i].addr != l.wants && l.seen[i].heardTick < l.seen[slot].heardTick) slot = i;
    }
    if (slot == l.count) ++l.count;
    l.seen[slot] = Lobby::Seen{d->from, *beacon, tick, d->rssi};
  }
  uint8_t kept = 0;
  for (uint8_t i = 0; i < l.count; ++i)
    if (tick - l.seen[i].heardTick <= T::kLobbyForgetTicks) l.seen[kept++] = l.seen[i];
  l.count = kept;

  if (in.doublePress || in.hang) return closeWith(air, EndReason::Left, false);
  // A pet resting after a fight holds the clock: the 5 min are for searching.
  if (setup_.decline == Decline::Resting) modeSince_ = tick;
  if (tick - modeSince_ >= T::kFightModeTicks) return closeWith(air, EndReason::LobbyTimeout, false);
  if (setup_.pin && tick - l.openedTick >= T::kPinTicks) setup_.pin.reset();

  if (const std::optional<uint8_t> m = claim(l, tick)) {
    enterBattle(l, *m, tick);
    Battle& b = std::get<Battle>(state_);
    beacon(air, b.peer);   // my claim at once, so the friend locks too
    sendState(b, air, tick, nullptr, false);
    return;
  }
  if (tick - l.lastBeacon >= T::kBeaconEvery) {
    beacon(air, l.wants);
    l.lastBeacon = tick;
  }
}

void DuelSession::beacon(Air& air, PeerAddr wants) {
  BeaconPacket p;
  p.roster = rosterHash();
  p.nonce = setup_.nonce;
  p.wants = wants;
  p.seq = ++beaconSeq_;
  p.decline = setup_.decline;
  p.ceiling = setup_.ceiling.top;
  p.card = setup_.card;
  uint8_t out[kDatagramMax];
  air.broadcast(out, encode(Packet{p}, out));
}

void DuelSession::enterBattle(const Lobby& l, uint8_t peer, uint32_t tick) {
  const Lobby::Seen& s = l.seen[peer];
  Battle b;
  b.peer = s.addr;
  b.theirNonce = s.beacon.nonce;
  b.theirs = s.beacon.card;
  b.me = self_ < s.addr ? Side::A : Side::B;
  const uint8_t rematch = setup_.pin ? setup_.pin->rematch : 0;
  b.battle = BattleId{duelSeed(setup_.nonce, b.theirNonce, rematch, Stakes::Spar), rematch, Stakes::Spar};
  b.phaseTick = b.lastHeard = tick;
  b.lastBeacon = tick;
  b.lockSeq = s.beacon.seq;
  b.skill = setup_.skill;
  b.pair = setup_.ceiling;
  b.chosen = Stakes::Spar;
  state_ = std::move(b);
}

// ---- the battle ---------------------------------------------------------------------

bool DuelSession::sameBout(const Battle& b, BattleId id) const {
  return id.rematch == b.battle.rematch && id.seed == duelSeed(setup_.nonce, b.theirNonce, id.rematch, id.stakes);
}

Outcome DuelSession::resolveFor(const Battle& b) const {
  const bool a = b.me == Side::A;
  return resolve(a ? setup_.card : b.theirs, a ? b.theirs : setup_.card, b.battle.seed, b.battle.stakes,
                 a ? b.mine : b.heard, a ? b.heard : b.mine);
}

void DuelSession::onState(Battle& b, const StatePacket& st, uint32_t tick) {
  b.lastHeard = tick;
  if (b.phase == DuelPhase::Greeting) {
    if (st.battle != b.battle) return;
    b.greeted = true;
    if (!st.greeted) return;
    b.theirs = st.card;
    DuelBook::HeadToHead h2h{};
    uint8_t forKeeps = 0;
    if (setup_.book) {
      h2h = setup_.book->headToHead(st.card.lineageId);
      forKeeps = setup_.book->forKeepsOn(st.card.lineageId, setup_.petDay);
    }
    b.pair = pairCeiling(setup_.ceiling, setup_.card, st.card, setup_.manualTurns, h2h, forKeeps);
    b.chosen = Stakes(std::min(u8(b.chosen), u8(b.pair.top)));
    b.phase = DuelPhase::Staking;
    b.phaseTick = tick;
  }
  if (b.phase == DuelPhase::Staking) {
    b.theyChose = st.chosen;
    b.theyConfirmed = st.confirmed;
    b.theirCeiling = st.ceiling;
    std::copy(st.why, st.why + 3, b.theirWhy);
    std::copy(st.skull, st.skull + 3, b.theirSkull);
  }
  b.theirHold = st.hold;
  b.theyLeft = b.theyLeft || st.leaving;
  if (!b.emoteSeqKnown) {
    b.emoteSeqKnown = true;
    b.emoteSeqIn = st.emoteSeq;
  } else if (st.emoteSeq != b.emoteSeqIn && st.emote != Emote::None) {
    b.emoteSeqIn = st.emoteSeq;
    b.emoteIn = st.emote;
    b.emoteInTick = tick;
  }
  if (b.phase == DuelPhase::Staking || st.battle != b.battle) return;
  const MoveLog::Merge m = b.heard.adopt(st.moves, st.moveCount);
  if (m == MoveLog::Merge::Conflict) b.conflict = true;
  if (m == MoveLog::Merge::Grew) b.heardGrew = tick;
  b.theyHold = std::max(b.theyHold, std::min(st.heard, b.mine.count()));
}

void DuelSession::sendState(Battle& b, Air& air, uint32_t tick, const Farewell* f, bool leaving) {
  StatePacket st;
  st.roster = rosterHash();
  st.battle = b.battle;
  st.nonce = setup_.nonce;
  st.chosen = b.chosen;
  st.confirmed = b.confirmed;
  st.ceiling = b.pair.top;
  std::copy(b.pair.why, b.pair.why + 3, st.why);
  std::copy(setup_.skull, setup_.skull + 3, st.skull);
  st.hold = fill(b.holdTicks, kFinishHoldTicks);
  st.card = setup_.card;
  st.moveCount = b.mine.count();
  std::memcpy(st.moves, b.mine.packed(), DuelRules::kLogSlots);
  st.heard = b.heard.count();
  st.emoteSeq = b.emoteSeqOut;
  st.emote = b.emoteOut;
  st.leaving = leaving;
  st.greeted = b.greeted;
  if (f) {
    st.rematch = f->iAsked;
    st.hugged = f->iHugged;
    if (f->iHugged && setup_.genome) {
      st.genomeBytes = uint16_t(setup_.genome->bytes().size());
      st.genomeHash = setup_.genome->hash();
    }
  }
  uint8_t out[kDatagramMax];
  air.send(b.peer, out, encode(Packet{st}, out));
  b.lastSent = tick;
}

void DuelSession::battleTick(Battle& b, Air& air, const DuelInput& in, uint32_t tick, DuelEffects& fx) {
  while (std::optional<Datagram> d = air.receive()) {
    if (d->from != b.peer) continue;
    const std::optional<Packet> p = decode(d->bytes, d->len);
    if (!p) continue;
    if (const StatePacket* st = std::get_if<StatePacket>(&*p)) {
      if (st->roster == rosterHash() && sameBout(b, st->battle)) onState(b, *st, tick);
    } else if (const BeaconPacket* bp = std::get_if<BeaconPacket>(&*p)) {
      // A beacon newer than the one that locked us: still claiming me in this
      // pairing, or searching again (they gave up on it), which ends it here too.
      if (int16_t(uint16_t(bp->seq - b.lockSeq)) <= 0) continue;
      b.lockSeq = bp->seq;
      if (bp->wants != self_ || bp->nonce != b.theirNonce) b.theyLeft = true;
    }
  }
  // No lid, drop or face-down exit: a car or a pocket never ends a fight.
  // Only silence, a conflict, or the friend leaving does.
  if (b.conflict) return finish(b, air, tick, fx, EndReason::Conflict, false);
  if (b.theyLeft) return finish(b, air, tick, fx, EndReason::Left, true);
  if (tick - b.lastHeard >= T::kSilentTicks) return finish(b, air, tick, fx, EndReason::WanderedOff, true);

  if (b.phase == DuelPhase::Greeting) {
    if (tick - b.phaseTick >= T::kGreetTicks) {
      setAsideFor(b.peer, tick, true);   // his claim reached me but his States never did: others first for a while
      return backToLobby(air, tick, EndReason::WanderedOff, false, fx, false);
    }
    if (tick - b.lastBeacon >= T::kBeaconEvery) {
      beacon(air, b.peer);
      b.lastBeacon = tick;
    }
  } else if (b.phase == DuelPhase::Staking) {
    if (tick - b.phaseTick >= T::kStakingTicks) return backToLobby(air, tick, EndReason::WanderedOff, true, fx, true);
    stake(b, in, tick);
  } else {
    if (!play(b, air, in, tick, fx)) return;
  }

  const bool hurry = b.phase == DuelPhase::Greeting || b.phase == DuelPhase::Staking || b.phase == DuelPhase::Mercy ||
                     b.phase == DuelPhase::Fallen || b.theyHold < b.mine.count() ||
                     tick - b.heardGrew < T::kIdleEvery || tick - b.emoteOutTick < T::kIdleEvery;
  if (tick - b.lastSent >= (hurry ? T::kHurryEvery : T::kIdleEvery)) sendState(b, air, tick, nullptr, false);
}

void DuelSession::stake(Battle& b, const DuelInput& in, uint32_t tick) {
  // BOOT alone picks the stakes, never a tilt or a knock: a press steps to the
  // next tier this pair may enter (Spar, Scrap, Mortal, Spar), a 3 s hold
  // locks the pick, and after kStakeAutoTicks it locks itself. Mortal is only
  // ever agreed with the hold: unheld, it locks as Scrap, the tier its press
  // already agreed to on the way past.
  if (!b.confirmed) {
    if (in.press || in.doublePress) {
      const uint8_t next = uint8_t(u8(b.chosen) + 1);
      b.chosen = next > u8(b.pair.top) ? Stakes::Spar : Stakes(next);
      b.holdTicks = 0;
    }
    if (in.bootDown) {
      if (++b.holdTicks >= kFinishHoldTicks) b.confirmed = true;
    } else {
      b.holdTicks = 0;
    }
    if (!b.confirmed && tick - b.phaseTick >= T::kStakeAutoTicks) {
      if (b.chosen == Stakes::Mortal) b.chosen = Stakes::Scrap;
      b.confirmed = true;
    }
  }
  if (!b.confirmed || !b.theyConfirmed) return;
  const Stakes agreed = agreedStakes(b.chosen, b.theyChose);
  b.battle = BattleId{duelSeed(setup_.nonce, b.theirNonce, b.battle.rematch, agreed), b.battle.rematch, agreed};
  b.phase = DuelPhase::Choosing;
  b.phaseTick = tick;
  b.holdTicks = 0;
  b.outcome = resolveFor(b);
}

bool DuelSession::specialFits(const Battle& b, Windup w) const {
  const SideState& s = b.outcome.side[uint8_t(b.me)];
  return s.queued == Windup::None && s.spark >= powerInfo(setup_.card.power).cost &&
         b.mine.count() + uint8_t(w) < DuelRules::kMaxTurns;
}

void DuelSession::applyResolved(Battle& b, uint32_t tick, DuelEffects& fx) {
  const Outcome& o = b.outcome;
  if (o.resolved <= b.applied) return;
  const uint8_t me = uint8_t(b.me), them = uint8_t(1 - me);
  TurnHit hits[DuelRules::kMaxTurns]{};
  const bool wounds = rule(o.stakes).injuryPerHeart > Fx::zero();
  if (wounds) turnHits(o, b.me, b.battle.seed, setup_.card, b.theirs, hits);
  for (uint8_t t = b.applied; t < o.resolved; ++t) {
    const TurnReport& r = o.turns[t];
    learnTurn(b.skill, o, b.me, b.theirs, t);
    if (wounds && hits[t].hearts > 0 && fx.woundCount < DuelRules::kMaxTurns) {
      const TurnWound w = woundFor(o.stakes, hits, t, kNoClamp);
      fx.wounds[fx.woundCount++] = w;
      b.scarsTaken = uint8_t(b.scarsTaken + w.scar);
    }
    if (r.hit[me] > 0) addStim(fx, stim::duel_bonked);
    if (r.hit[them] > 0) addStim(fx, stim::duel_landed);
    if (b.prophecy && b.prophecyTurn == t) {
      b.prophecyJudged = true;
      b.prophecyKept = !r.dozed[them] && !r.bracing[them] && r.shown[them] == b.prophecy->stance;
    }
  }
  b.applied = o.resolved;
  b.revealing = true;
  b.revealTick = tick;
  if (o.over() && !b.endSeen) {
    b.endSeen = true;
    if (won(o, me)) addStim(fx, stim::duel_won);
    else if (o.side[me].hearts == 0) addStim(fx, stim::duel_fainted);
  }
  if (o.downed[me] && !b.downedSent) {
    b.downedSent = true;
    fx.downed = true;
    fx.battle = b.battle;
    fx.victor = b.theirs.lineageId;
  }
}

// The battle proper, from turn 0 to the friends screen. False when the
// session left the Battle state (the caller must not touch `b` again).
bool DuelSession::play(Battle& b, Air& air, const DuelInput& in, uint32_t tick, DuelEffects& fx) {
  const uint8_t me = uint8_t(b.me);
  const bool bootWasDown = b.bootWasDown;
  b.bootWasDown = in.bootDown;
  b.outcome = resolveFor(b);
  applyResolved(b, tick, fx);
  if (b.revealing && tick - b.revealTick >= T::kRevealTicks) b.revealing = false;
  const Outcome& o = b.outcome;

  if (!o.over()) {
    const bool canAct = !b.revealing && b.mine.count() == o.resolved;
    if (canAct && !b.choosing) b.chooseTick = tick;
    b.choosing = canAct;
    // A knock knock is a press too (controls.h): where it throws a special it
    // is not also a cheer; anywhere else it cheers, as it always has.
    const int8_t aimNow = in.aim <= 2 ? in.aim : -1;
    const bool throws = canAct && !o.side[me].dozing && aimNow >= 0 &&
                        ((in.hold && specialFits(b, Windup::Heavy)) || (in.press && specialFits(b, Windup::Quick)));
    if (in.doubleKnock && !throws) cheer(b, Emote(1 + b.emoteSeqOut % 4), tick);
    if (in.shake && !b.revealing && !(b.prophecy && b.prophecyTurn == o.resolved)) {
      b.prophecy = foretell(o, other(b.me), setup_.card, b.theirs, b.battle.seed);
      b.prophecyTurn = o.resolved;
      b.prophecyJudged = b.prophecyKept = false;
    }
    if (!canAct) return true;
    bool locked = false;
    if (o.side[me].dozing) {
      locked = b.mine.append(Move{Stance::Pounce, false, true});
    } else {
      const int8_t aim = in.aim <= 2 ? in.aim : -1;
      if (aim >= 0 && in.hold && specialFits(b, Windup::Heavy)) locked = b.mine.appendSpecial(Windup::Heavy, Stance(aim));
      else if (aim >= 0 && in.press && specialFits(b, Windup::Quick)) locked = b.mine.appendSpecial(Windup::Quick, Stance(aim));
      else if (aim >= 0 && in.knock) locked = b.mine.append(Move{Stance(aim)});
      // Time is up. Tilted, the aimed stance locks, as a knock would lock it
      // (the owner: "just tilting it and waiting until the time runs out
      // should select the action"); level, the pet picks, as it always has.
      if (!locked && aim >= 0 && in.aimMoved && tick - b.chooseTick >= T::chooseTicks(o.resolved))
        locked = b.mine.append(Move{Stance(aim)});
      if (!locked && tick - b.chooseTick >= T::chooseTicks(o.resolved)) {
        const Lean l = petChoice(b.skill, o, b.me, setup_.card, b.theirs, assistFor(b, me), b.battle.seed);
        locked = l.special != Windup::None && specialFits(b, l.special) && b.mine.appendSpecial(l.special, l.stance, true);
        if (!locked) locked = b.mine.append(Move{l.stance, false, true});
      }
    }
    if (locked) {
      b.choosing = false;
      b.outcome = resolveFor(b);
      applyResolved(b, tick, fx);
    }
    return true;
  }
  if (b.revealing) return true;

  if (o.awaitingVerdict()) {
    if (b.phase != DuelPhase::Mercy) {
      b.phase = DuelPhase::Mercy;
      b.phaseTick = tick;
      b.holdTicks = 0;
      b.heldIntoMercy = bootWasDown;
    }
    if (o.downed[me]) {
      if (in.press || in.knock || in.doubleKnock) cheer(b, Emote::Plead, tick);
      return true;
    }
    // Only the winner's device decides, in the slot right after the downing.
    // A hold finishes, a knock or a press spares, and so does silence.
    Verdict v = Verdict::None;
    if (in.bootDown) {
      if (!b.heldIntoMercy && ++b.holdTicks >= kFinishHoldTicks) v = Verdict::Finish;
    } else {
      b.heldIntoMercy = false;
      b.holdTicks = 0;
    }
    if (v == Verdict::None && (in.knock || in.press || tick - b.phaseTick >= kMercyTicks)) v = Verdict::Spare;
    if (v != Verdict::None && b.mine.count() == o.resolved) {
      b.mine.append(Move{Stance::Pounce, false, false, v});
      b.outcome = resolveFor(b);
    }
    return true;
  }

  if (o.verdict == Verdict::Finish) {
    if (b.phase != DuelPhase::Fallen) {
      b.phase = DuelPhase::Fallen;
      b.phaseTick = tick;
      if (o.downed[me]) {
        fx.fall = true;
        fx.met = meetingOf(b);
        b.metSent = true;
      }
    }
    // The victor takes the slaying only once the fallen side's ack covers
    // the verdict: then both devices know the same death.
    if (!o.downed[me] && !b.metSent && b.theyHold > o.resolved) {
      b.slain = true;
      addStim(fx, stim::duel_slew);
      fx.met = meetingOf(b);
      b.metSent = true;
      b.phaseTick = tick;
    }
    if (b.metSent && tick - b.phaseTick >= T::kFallenTicks) {
      backToLobby(air, tick, EndReason::Finished, false, fx, true);
      return false;
    }
    return true;
  }

  if (b.phase != DuelPhase::Fainting) {
    b.phase = DuelPhase::Fainting;
    b.phaseTick = tick;
    if (o.verdict == Verdict::Spare && o.downed[me]) addStim(fx, stim::duel_spared);
  }
  const bool someoneDown = o.side[0].hearts == 0 || o.side[1].hearts == 0;
  if (!someoneDown || tick - b.phaseTick >= T::kFaintTicks) {
    enterFarewell(b, tick, fx);
    return false;
  }
  return true;
}

uint8_t DuelSession::assistFor(const Battle& b, uint8_t side) const {
  const BattleCard& mine = setup_.card;
  return side == uint8_t(b.me) ? Assist::hearts(mine.prowess, b.theirs.prowess)
                               : Assist::hearts(b.theirs.prowess, mine.prowess);
}

void DuelSession::cheer(Battle& b, Emote e, uint32_t tick) {
  ++b.emoteSeqOut;
  b.emoteOut = e;
  b.emoteOutTick = tick;
}

Meeting DuelSession::meetingOf(const Battle& b) const {
  const Outcome& o = b.outcome;
  const uint8_t me = uint8_t(b.me), them = uint8_t(1 - me);
  Meeting m;
  m.friendCard = b.theirs;
  m.me = b.me;
  m.ending = o.over() ? o.ending : Ending::Draw;
  m.turns = o.resolved;
  m.battle = b.battle;
  m.outcomeHash = o.hash();
  // How well my kid picked: the turns the kid chose, weighted toward later
  // ones. A landed special is a won turn and a riposted one a lost turn;
  // wind-up turns were chosen at the commit, so they do not count again.
  uint32_t got = 0, of = 0;
  for (uint8_t t = 0; t < o.resolved; ++t) {
    const TurnReport& r = o.turns[t];
    if (r.move[me].byPet || r.bracing[me] || r.dozed[me]) continue;
    ++m.manualTurns;
    of += t + 1u;
    const bool good = r.special[me] == Special::Landed ||
                      (r.special[me] != Special::Riposted && (r.hit[me] == 0 || r.hit[them] > r.hit[me]));
    if (good) got += t + 1u;
  }
  m.performance = of ? uint8_t(got * 255 / of) : 128;
  m.skillAfter = b.skill;
  m.verdict = o.verdict;
  m.iFell = o.downed[me] && o.verdict == Verdict::Finish;
  m.iSlew = b.slain;
  m.iSpared = o.downed[them] && o.verdict == Verdict::Spare;
  m.wasSpared = o.downed[me] && o.verdict == Verdict::Spare;
  m.scarsTaken = b.scarsTaken;
  return m;
}

// The battle ended outside the rules' own ending: silence, a conflict or a
// goodbye. Before turn 0 nothing was applied, so nothing is recorded. After
// it, neither badge can know what the other last heard, so both call it a
// fizzle (a draw), whoever went quiet; wounds already taken stay. A downed
// pet whose duel ends before the verdict lives.
void DuelSession::finish(Battle& b, Air& air, uint32_t tick, DuelEffects& fx, EndReason why, bool theyLeft) {
  if (b.phase == DuelPhase::Greeting || b.phase == DuelPhase::Staking)
    return backToLobby(air, tick, why, !theyLeft, fx, b.phase == DuelPhase::Staking);
  if (!b.metSent) {
    const Outcome& o = b.outcome;
    const uint8_t me = uint8_t(b.me);
    Meeting m = meetingOf(b);
    if (why == EndReason::Conflict || !o.over()) {
      m.ending = Ending::Draw;
      m.fizzled = true;
    } else if (o.awaitingVerdict()) {
      // A downed pet who leaves before the verdict lives, and fled. The
      // victor leaving instead spares him, and nobody fled.
      m.fled = !theyLeft && o.downed[me];
    }
    fx.met = m;
    b.metSent = true;
  }
  backToLobby(air, tick, why, !theyLeft, fx, true);
}

void DuelSession::enterFarewell(Battle& b, uint32_t tick, DuelEffects& fx) {
  if (!b.metSent) fx.met = meetingOf(b);
  b.metSent = true;
  addStim(fx, stim::duel_befriended);
  Farewell f;
  f.last = b;
  f.since = tick;
  state_ = std::move(f);
}

// ---- the friends screen -------------------------------------------------------------

void DuelSession::farewellTick(Farewell& f, Air& air, const DuelInput& in, uint32_t tick, DuelEffects& fx) {
  Battle& b = f.last;
  const uint8_t mine = setup_.genome ? chunksOf(setup_.genome->bytes().size()) : 0;
  bool theyLeft = false;
  while (std::optional<Datagram> d = air.receive()) {
    if (d->from != b.peer) continue;
    const std::optional<Packet> p = decode(d->bytes, d->len);
    if (!p) continue;
    if (const StatePacket* st = std::get_if<StatePacket>(&*p)) {
      if (st->roster != rosterHash() || st->battle != b.battle) continue;
      b.lastHeard = tick;
      f.theyAsked = f.theyAsked || st->rematch;
      theyLeft = theyLeft || st->leaving;
      if (st->hugged && !f.theyHugged) {
        f.theyHugged = true;
        f.theirGenomeBytes = st->genomeBytes;
        f.theirGenomeHash = st->genomeHash;
      }
    } else if (const BeaconPacket* bp = std::get_if<BeaconPacket>(&*p)) {
      // They asked, saw mine, and are already back in the lobby, pinned to me.
      if (bp->wants == self_) f.theyAsked = true;
    } else if (const GenomePacket* g = std::get_if<GenomePacket>(&*p)) {
      if (g->battle != b.battle) continue;
      f.theyHaveMine |= g->have & allOf(mine);
      const uint8_t theirs = chunksOf(f.theirGenomeBytes);
      const size_t at = size_t(g->index) * kGenomeChunk;
      if (!f.swapping || g->index >= theirs || (f.haveTheirs >> g->index & 1)) continue;
      if (g->len != std::min(kGenomeChunk, size_t(f.theirGenomeBytes) - at)) continue;
      std::memcpy(f.theirGenome.data() + at, g->bytes, g->len);
      f.haveTheirs |= uint64_t(1) << g->index;
    }
  }

  // Leaving the friends screen is a double press (or a hang, its no-button
  // twin), back to searching; a lid, a drop or a knock never decides anything here.
  if (in.doublePress || in.hang) return backToLobby(air, tick, EndReason::Left, true, fx, true);
  if (theyLeft) return backToLobby(air, tick, EndReason::Left, false, fx, true);
  if (in.press) f.iAsked = true;
  if (in.bootDown) {
    if (++f.hugHold >= T::kHugHoldTicks) f.iHugged = true;
  } else {
    f.hugHold = 0;
  }
  if (in.cradle) f.iHugged = true;
  if (f.iAsked && f.theyAsked) {
    fx.rematch = DuelSetup::Pin{b.peer, uint8_t(b.battle.rematch + 1)};
    setup_.pin = fx.rematch;
    return backToLobby(air, tick, EndReason::Finished, false, fx, true);
  }

  if (f.iHugged && f.theyHugged && setup_.genome && f.theirGenomeBytes > 0 && !f.swapping && !f.eggDone) {
    f.swapping = true;
    f.swapSince = tick;
    f.theirGenome.assign(f.theirGenomeBytes, 0);
  }
  const uint8_t theirs = chunksOf(f.theirGenomeBytes);
  if (f.swapping && !f.eggDone && f.haveTheirs == allOf(theirs)) {
    f.eggDone = true;
    if (std::optional<Genome> g = acceptFriendGenome(f.theirGenome.data(), f.theirGenome.size(), f.theirGenomeHash,
                                                     *setup_.genome)) {
      fx.egg = FriendEgg{FriendEggRecord{b.theirs.lineageId, b.theirs.species, b.battle.seed, f.theirGenomeHash},
                         std::move(*g)};
    } else {
      f.eggFizzled = true;
      f.fizzledAt = tick;
    }
    std::vector<uint8_t>().swap(f.theirGenome);
  }
  const bool swapOpen = f.swapping && tick - f.swapSince < T::kGenomeTicks;
  if (f.swapping && !swapOpen && !f.eggDone) {
    f.eggDone = f.eggFizzled = true;
    f.fizzledAt = tick;
    std::vector<uint8_t>().swap(f.theirGenome);
  }

  if (tick - b.lastSent >= T::kHurryEvery) {
    sendState(b, air, tick, &f, false);
    for (uint8_t sent = 0, tries = 0; f.swapping && sent < T::kChunksPerSend && tries < mine; ++tries) {
      const uint8_t i = f.nextChunk;
      f.nextChunk = uint8_t((f.nextChunk + 1) % mine);
      if (f.theyHaveMine >> i & 1) continue;
      GenomePacket g;
      g.battle = b.battle;
      g.have = f.haveTheirs;
      g.index = i;
      const size_t at = size_t(i) * kGenomeChunk;
      g.len = uint8_t(std::min(kGenomeChunk, setup_.genome->bytes().size() - at));
      std::memcpy(g.bytes, setup_.genome->bytes().data() + at, g.len);
      uint8_t out[kDatagramMax];
      air.send(b.peer, out, encode(Packet{g}, out));
      ++sent;
    }
  }
  // A fizzled egg's sparkles get a reveal's length on screen before it closes.
  const bool theyLack = f.swapping && f.theyHaveMine != allOf(mine);
  const bool sparkling = f.eggFizzled && tick - f.fizzledAt < T::kRevealTicks;
  if (tick - f.since >= T::kFriendsTicks && !(swapOpen && (!f.eggDone || theyLack)) && !sparkling)
    backToLobby(air, tick, EndReason::Finished, false, fx, true);
}

// ---- closing ------------------------------------------------------------------------

void DuelSession::close(Air& air, EndReason why) { closeWith(air, why, true); }

void DuelSession::closeWith(Air& air, EndReason why, bool goodbye) {
  if (!active()) return;
  Battle* b = std::get_if<Battle>(&state_);
  Farewell* f = std::get_if<Farewell>(&state_);
  if (f) b = &f->last;
  if (goodbye && b && b->phase != DuelPhase::Greeting)
    for (uint8_t i = 0; i < T::kLeavingBurst; ++i) sendState(*b, air, b->lastSent, f, true);
  air.close();
  state_ = Closed{why};
}

// ---- the view -----------------------------------------------------------------------

int16_t DuelSession::choosingTurn() const {
  const Battle* b = std::get_if<Battle>(&state_);
  if (!b || b->phase != DuelPhase::Choosing || b->revealing || b->outcome.over()) return -1;
  return b->mine.count() == b->outcome.resolved ? int16_t(b->outcome.resolved) : int16_t(-1);
}

DuelView DuelSession::view(uint32_t tick) const {
  DuelView v;
  if (const Closed* c = std::get_if<Closed>(&state_)) {
    v.ended = c->why;
    return v;
  }
  const uint32_t searched = tick - modeSince_;
  v.fightTicksLeft = uint16_t(searched >= T::kFightModeTicks ? 0 : T::kFightModeTicks - searched);
  v.myDecline = setup_.decline;
  if (const Lobby* l = std::get_if<Lobby>(&state_)) {
    v.phase = DuelPhase::Lobby;
    v.ended = lastEnd_;
    v.phaseTicks = uint16_t(tick - l->openedTick);
    v.me = setup_.card;
    v.peerCount = std::min(l->count, kMaxLobbyPeers);
    v.heardCount = l->count;
    for (uint8_t i = 0; i < v.peerCount; ++i) {
      const Lobby::Seen& s = l->seen[i];
      v.peers[i] = DuelView::Peer{s.beacon.card.species, s.beacon.card.look, s.beacon.decline, s.beacon.wants == self_,
                                  l->wants == s.addr, s.beacon.roster == rosterHash()};
    }
    return v;
  }
  const Farewell* f = std::get_if<Farewell>(&state_);
  const Battle& b = f ? f->last : std::get<Battle>(state_);
  const Outcome& o = b.outcome;
  const uint8_t me = uint8_t(b.me), them = uint8_t(1 - me);

  v.phase = b.phase;
  v.phaseTicks = uint16_t(tick - b.phaseTick);
  v.me = setup_.card;
  v.them = b.theirs;
  v.ceiling[0] = b.pair.top, v.ceiling[1] = b.theirCeiling;
  for (uint8_t t = 0; t < 3; ++t) {
    v.why[0][t] = b.pair.why[t], v.why[1][t] = b.theirWhy[t];
    v.skull[0][t] = setup_.skull[t], v.skull[1][t] = b.theirSkull[t];
  }
  v.chosen = b.chosen, v.theyChose = b.theyChose, v.agreed = b.battle.stakes;
  v.confirmed = b.confirmed, v.theyConfirmed = b.theyConfirmed;
  v.confirmHold = fill(b.holdTicks, kFinishHoldTicks), v.theirHold = b.theirHold;
  v.mine = o.side[me], v.theirs = o.side[them];
  v.assistHearts[0] = assistFor(b, me), v.assistHearts[1] = assistFor(b, them);
  v.iAmDown = o.downed[me], v.theyAreDown = o.downed[them];
  v.verdict = o.verdict;
  v.ending = o.ending;
  v.iWon = won(o, me), v.draw = o.ending == Ending::Draw;
  v.newScars = b.scarsTaken;
  v.locked = b.mine.count() > o.resolved;
  v.theyLocked = b.heard.count() > o.resolved;
  v.specialReady = specialFits(b, Windup::Quick);
  const uint8_t cost = powerInfo(setup_.card.power).cost, spark = o.side[me].spark;
  v.pipsShort = spark < cost ? uint8_t(cost - spark) : 0;
  if (o.resolved > 0) {
    v.hasLast = true;
    v.lastTurn = uint8_t(o.resolved - 1);
    v.last = mirrored(o.turns[o.resolved - 1], b.me);
  }
  v.prophesied = b.prophecy.has_value();
  if (b.prophecy) v.prophecy = *b.prophecy;
  v.prophecyKept = b.prophecyKept, v.prophecyJudged = b.prophecyJudged;
  if (b.emoteIn != Emote::None) v.emoteIn = b.emoteIn, v.emoteTicks = uint16_t(tick - b.emoteInTick);

  if (b.phase == DuelPhase::Choosing) {
    if (b.revealing || o.over()) {
      v.phase = DuelPhase::Revealing;
      v.phaseTicks = uint16_t(tick - b.revealTick);
    } else if (!o.over() && v.locked) {
      v.phase = DuelPhase::Waiting;
    } else if (!o.over()) {
      v.phaseTicks = uint16_t(tick - b.chooseTick);
      const uint32_t spent = tick - b.chooseTick;
      v.chooseTicks = T::chooseTicks(o.resolved);
      v.chooseTicksLeft = uint16_t(spent >= v.chooseTicks ? 0 : v.chooseTicks - spent);
      v.lean = petChoice(b.skill, o, b.me, setup_.card, b.theirs, v.assistHearts[0], b.battle.seed);
    }
  }
  v.aim = aim_;
  if (b.phase == DuelPhase::Mercy) {
    const uint32_t spent = tick - b.phaseTick;
    v.mercyTicksLeft = uint16_t(spent >= kMercyTicks ? 0 : kMercyTicks - spent);
  }
  if (f) {
    v.phase = DuelPhase::Friends;
    v.phaseTicks = uint16_t(tick - f->since);
    v.rematchAsked = f->iAsked, v.theyAskedRematch = f->theyAsked;
    v.iHugged = f->iHugged, v.theyHugged = f->theyHugged;
    v.hugHold = fill(f->hugHold, T::kHugHoldTicks);
    const uint8_t theirs = chunksOf(f->theirGenomeBytes);
    v.eggProgress = f->eggDone ? 255 : theirs ? uint8_t(popcount(f->haveTheirs) * 255 / theirs) : 0;
    v.eggFizzled = f->eggFizzled;
    v.eggLaid = f->eggDone && !f->eggFizzled;
  }
  return v;
}

}  // namespace blorb
