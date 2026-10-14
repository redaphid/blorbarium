#include "paint/duel_view.h"

#include <algorithm>

namespace paint {
namespace {

using blorb::DuelPhase;

DuelAct actOf(DuelPhase p) {
  switch (p) {
    case DuelPhase::Staking: return DuelAct::Staking;
    case DuelPhase::Choosing:
    case DuelPhase::Waiting: return DuelAct::Choosing;
    case DuelPhase::Revealing: return DuelAct::Clash;
    case DuelPhase::Mercy: return DuelAct::Mercy;
    case DuelPhase::Fallen: return DuelAct::Fallen;
    case DuelPhase::Fainting:
    case DuelPhase::Friends: return DuelAct::Outcome;
    default: return DuelAct::Meeting;
  }
}

uint8_t landsIn(const blorb::SideState& s, const blorb::DuelView& v) {
  const uint8_t next = v.hasLast ? uint8_t(v.lastTurn + 1) : 0;
  return s.queued != blorb::Windup::None && s.landsOn > next ? uint8_t(s.landsOn - next) : 0;
}

}  // namespace

blorb::Appearance lookOf(const blorb::BattleCard& c) {
  blorb::Appearance a;
  a.kind = blorb::Appearance::Kind::Creature;
  a.species = c.species;
  a.stage = c.look.stage;
  a.scalePct = c.look.scalePct;
  a.lifeSeed = c.look.lifeSeed;
  a.intensity = blorb::Fx::one();
  std::copy(std::begin(c.look.regions), std::end(c.look.regions), std::begin(a.regions));
  a.markCount = std::min<uint8_t>(c.look.markCount, 4);
  for (uint8_t i = 0; i < a.markCount; ++i) a.marks[i] = {c.look.marks[i].layer, c.look.marks[i].variant, {}};
  return a;
}

RimHint rimOf(const blorb::BootHint& h) {
  return RimHint{uint8_t(h.glyph), h.art, h.greyed, h.pips, h.hasHold, h.holdFill, h.hasDouble};
}

bool hasScene(DuelPhase p) { return p != DuelPhase::Closed && p != DuelPhase::Lobby; }

DuelScene sceneOf(const blorb::DuelView& v, const blorb::Appearance& me) {
  DuelScene s;
  s.act = actOf(v.phase);
  s.ticks = v.phaseTicks;
  s.me.look = me;
  s.them.look = lookOf(v.them);
  s.boot = rimOf(me.boot);
  s.stakes = v.agreed;

  Fighter* f[2] = {&s.me, &s.them};
  const blorb::SideState* side[2] = {&v.mine, &v.theirs};
  for (int i = 0; i < 2; ++i) {
    f[i]->hearts = side[i]->hearts;
    f[i]->maxHearts = side[i]->maxHearts;
    f[i]->assist = v.assistHearts[i];
    f[i]->sparks = side[i]->spark;
    f[i]->windup = side[i]->queued;
    f[i]->landsIn = landsIn(*side[i], v);
    f[i]->ceiling = v.ceiling[i];
  }

  // Staking: each kid's tier, hold and forecast skull at that tier.
  s.me.chose = v.chosen, s.them.chose = v.theyChose;
  s.me.confirmed = v.confirmed, s.them.confirmed = v.theyConfirmed;
  s.me.skull = v.skull[0][uint8_t(v.chosen) % 3], s.them.skull = v.skull[1][uint8_t(v.theyChose) % 3];
  if (v.phase == DuelPhase::Staking) {
    s.me.hold = v.confirmHold, s.them.hold = v.theirHold;
    if (v.aim >= 0 && v.aim <= 2) s.me.chose = blorb::Stakes(v.aim);
  }

  // Choosing: the aim, the lean my pet whispers, the clock, and who has locked.
  s.aim = v.aim >= 0 && v.aim <= 2 ? v.aim : int8_t(-1);
  s.lean = int8_t(v.lean.stance);
  s.whisper = v.lean.whisper;
  s.ticksLeft = v.chooseTicksLeft;
  s.turnTicks = blorb::DuelTimings::kChooseTicks;
  s.me.locked = v.locked, s.them.locked = v.theyLocked;

  // The last turn, as the clash plays it (the view mirrors it so index 0 is me).
  if (v.hasLast) {
    const blorb::TurnReport& r = v.last;
    for (int i = 0; i < 2; ++i) {
      f[i]->stance = r.dozed[i] ? int8_t(-1) : int8_t(r.shown[i]);
      f[i]->hit = r.hit[i];
      f[i]->fired = r.powered[i];
      f[i]->special = r.special[i];
    }
    s.won = r.clash == blorb::Clash::Bonk ? Side::Neither : r.clash == blorb::Clash::AWins ? Side::Me : Side::Them;
    s.super = s.won != Side::Neither && r.super[s.won == Side::Me ? 0 : 1];
  }

  // The end, the verdict and the scars it left.
  if (v.ending != blorb::Ending::Ongoing) s.won = v.draw ? Side::Neither : v.iWon ? Side::Me : Side::Them;
  if (v.phase == DuelPhase::Mercy || v.phase == DuelPhase::Fallen) s.won = v.iAmDown ? Side::Them : Side::Me;
  s.verdict = v.verdict;
  s.finishHold = v.iAmDown ? v.theirHold : v.confirmHold;
  s.mercyLeft = v.mercyTicksLeft;
  s.mercyTicks = blorb::kMercyTicks;
  s.pleading = v.emoteIn == blorb::Emote::Plead && v.emoteTicks < 20;
  s.newScars = v.newScars;
  return s;
}

}  // namespace paint
