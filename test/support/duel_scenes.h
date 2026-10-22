#pragma once
// One canonical DuelScene for each moment of a duel, for any two species: what
// the struct goldens, the PNG goldens, tools/duel_sheet.py and the demo film
// all draw, so they agree on what "the mercy screen" is.
#include <algorithm>
#include <cstring>
#include <string>
#include "blorb/appearance.h"
#include "blorb/duel.h"
#include "blorb/species.h"
#include "paint/duel.h"

namespace blorbtest {

// The moments, in the order a duel reaches them, each at a tick worth seeing.
struct DuelMoment { const char* name; paint::DuelAct act; uint16_t ticks; };
inline constexpr DuelMoment kDuelMoments[] = {
    {"meeting", paint::DuelAct::Meeting, 14},
    {"staking", paint::DuelAct::Staking, 6},
    {"staking_mortal", paint::DuelAct::Staking, 6},
    {"choosing", paint::DuelAct::Choosing, 30},
    {"locked", paint::DuelAct::Choosing, 52},
    {"clash", paint::DuelAct::Clash, 6},
    {"hit", paint::DuelAct::Clash, 14},
    {"bonk", paint::DuelAct::Clash, 16},
    {"mercy", paint::DuelAct::Mercy, 20},
    {"fallen", paint::DuelAct::Fallen, 30},
    {"spared", paint::DuelAct::Fallen, 20},
    {"outcome", paint::DuelAct::Outcome, 24},
    // The queued special (BATTLES.md, "Amendment"): a telegraph and its four fates.
    {"windup_quick", paint::DuelAct::Choosing, 30},
    {"windup_heavy", paint::DuelAct::Choosing, 30},
    {"landing", paint::DuelAct::Clash, 15},
    {"riposte", paint::DuelAct::Clash, 14},
    {"stagger", paint::DuelAct::Clash, 14},
    {"whiff", paint::DuelAct::Clash, 16},
};

// boot.h's BootGlyph values the scenes use (D9 owns the enum).
namespace glyph {
inline constexpr uint8_t Pellet = 1, Moon = 2, Peek = 4, Flower = 5, Arrow = 6, Check = 7, Lock = 8, Download = 9,
                         Friend = 10, Door = 11, Tier = 12, Skull = 13, SpecialQuick = 14, SpecialHeavy = 15,
                         OpenHand = 16, Plead = 17, Candle = 18, Rematch = 19, Heart = 20, Countdown = 21;
}

// Each kBoot row's hint, as boot.h's table draws it: one per BootContext, in order.
struct HintRow { const char* context; paint::RimHint hint; };
inline const HintRow kHintRows[] = {
    {"ota", {glyph::Download, 0, false, 0, false, 0, false}},
    {"sealed", {glyph::Lock, 0, false, 0, false, 0, false}},
    {"staking", {glyph::Tier, 1, false, 0, true, 0, false}},
    {"turn", {glyph::SpecialQuick, 3, false, 0, true, 0, false}},
    {"winding", {glyph::Countdown, 0, false, 2, false, 0, false}},
    {"mercy_winner", {glyph::OpenHand, 0, false, 0, true, 120, false}},
    {"mercy_loser", {glyph::Plead, 0, false, 0, false, 0, false}},
    {"fallen", {glyph::Candle, 0, false, 0, false, 0, false}},
    {"outcome", {glyph::Rematch, 0, false, 0, true, 0, true}},
    {"lobby", {glyph::Friend, 0, false, 0, false, 0, true}},
    {"nest", {glyph::Arrow, 0, false, 0, true, 0, false}},
    {"clutch", {glyph::Arrow, 0, false, 0, true, 200, false}},
    {"vigil", {glyph::Flower, 0, false, 0, false, 0, false}},
    {"egg", {glyph::Peek, 0, false, 0, false, 0, false}},
    {"asleep", {glyph::Pellet, 0, true, 0, true, 0, false}},
    {"awake", {glyph::Pellet, 0, false, 0, true, 0, true}},
};

// A pet of `species` as the duel would carry him: the starter's look in that
// species' art, and (for the friend) his colours moved by his genes.
inline blorb::Appearance duelLook(blorb::SpeciesId species, uint32_t seed, int8_t hue) {
  blorb::Appearance a = blorb::portrait(blorb::starterGenome(seed), blorb::Stage::Adult, 1, 0);
  a.species = species;
  a.lifeSeed = seed;
  a.regions[blorb::region::skin.v].hue = hue;
  return a;
}

// My pet carries two old scars from earlier Scraps: a claw down his left
// flank and a notch on his right shoulder.
inline blorb::Scars veteran() {
  blorb::Scars s;
  s.mark[0] = {blorb::Scar::Kind::Claw, 40, 150, 230};
  s.mark[1] = {blorb::Scar::Kind::Notch, 220, 40, 200};
  s.count = 2;
  return s;
}

inline paint::DuelScene duelScene(const char* moment, blorb::SpeciesId mine, blorb::SpeciesId theirs) {
  using namespace paint;
  using blorb::Stakes;
  DuelScene s;
  const DuelMoment* m = &kDuelMoments[0];
  for (const DuelMoment& d : kDuelMoments)
    if (!std::strcmp(d.name, moment)) m = &d;
  s.act = m->act;
  s.ticks = m->ticks;
  s.me.look = duelLook(mine, 0x5EED0001u, 0);
  s.me.look.scars = veteran();
  s.them.look = duelLook(theirs, 0x5EED0002u, 20);
  const std::string n = m->name;
  // Each has lost one clash, at duel.h's numbers, so the gauges read as a
  // real duel does. My kid has been losing: one step of assist hearts, drawn
  // with the pet's cheer.
  using blorb::DuelRules;
  using blorb::SpecialRules;
  s.me.assist = DuelRules::kAssistStepHearts;
  s.me.maxHearts = uint8_t(DuelRules::kHearts + s.me.assist);
  s.me.hearts = uint8_t(s.me.maxHearts - DuelRules::kClashHearts);
  s.them.maxHearts = DuelRules::kHearts;
  s.them.hearts = uint8_t(DuelRules::kHearts - DuelRules::kClashHearts);
  s.me.sparks = 4;
  s.them.sparks = 2;
  const uint8_t power = uint8_t(blorb::powerInfo(blorb::speciesInfo(mine).power).glyph);
  using G = paint::RimHint;
  switch (s.act) {
    case DuelAct::Meeting: s.boot = G{glyph::Friend, 0, false, 0, false, 0, true}; break;
    case DuelAct::Staking: s.boot = G{glyph::Tier, 1, false, 0, true, 0, false}; break;
    case DuelAct::Choosing: s.boot = G{glyph::SpecialQuick, power, false, 0, true, 0, false}; break;
    case DuelAct::Clash: s.boot = G{glyph::SpecialQuick, power, true, 1, true, 0, false}; break;
    case DuelAct::Mercy: s.boot = G{glyph::OpenHand, 0, false, 0, true, 0, false}; break;
    case DuelAct::Fallen: s.boot = G{glyph::Candle, 0, false, 0, false, 0, false}; break;
    case DuelAct::Outcome: s.boot = G{glyph::Rematch, 0, false, 0, true, 0, true}; break;
  }

  if (n == "staking" || n == "staking_mortal") {
    s.me.chose = n == "staking" ? Stakes::Scrap : Stakes::Mortal;
    s.them.chose = Stakes::Mortal;
    s.me.skull = 70;
    s.them.skull = 150;
    s.me.hold = n == "staking" ? 0 : 140;
    s.them.confirmed = true;
    s.me.ceiling = n == "staking" ? Stakes::Scrap : Stakes::Mortal;
    s.me.confirmed = n == "staking";
    s.stakes = n == "staking" ? Stakes::Scrap : Stakes::Mortal;   // the lower of the two
  } else if (n == "choosing" || n == "locked") {
    s.aim = int8_t(blorb::Stance::Guard);
    s.lean = int8_t(blorb::Stance::Trick);
    s.whisper = 200;
    s.ticksLeft = n == "choosing" ? 70 : 48;
    s.me.locked = n == "locked";
    s.them.locked = n == "locked";
  } else if (n == "clash" || n == "hit" || n == "bonk") {
    s.won = n == "bonk" ? Side::Neither : Side::Me;
    s.me.stance = int8_t(n == "bonk" ? blorb::Stance::Pounce : blorb::Stance::Guard);
    s.them.stance = int8_t(blorb::Stance::Pounce);
    s.them.hit = n == "bonk" ? 0 : DuelRules::kClashHearts;
    s.me.fired = n == "clash";
    s.super = n == "hit";
  } else if (n == "mercy") {
    s.stakes = Stakes::Mortal;
    s.won = Side::Me;
    s.them.hearts = 0;
    s.finishHold = 120;
    s.mercyLeft = 90;
  } else if (n == "fallen" || n == "spared") {
    s.stakes = Stakes::Mortal;
    s.won = Side::Me;
    s.them.hearts = 0;
    s.verdict = n == "fallen" ? blorb::Verdict::Finish : blorb::Verdict::Spare;
  } else if (n == "windup_quick" || n == "windup_heavy") {
    // My kid queued a special; the friend's is landing this turn: both telegraphs at once.
    const bool heavy = n == "windup_heavy";
    s.me.windup = heavy ? blorb::Windup::Heavy : blorb::Windup::Quick;
    s.me.landsIn = heavy ? 2 : 1;
    s.me.locked = true;
    s.me.sparks = 1;
    s.them.windup = blorb::Windup::Quick;
    s.them.landsIn = 0;
    s.ticksLeft = 60;
    s.boot = paint::RimHint{glyph::Countdown, 0, false, s.me.landsIn, false, 0, false};
  } else if (n == "landing" || n == "riposte" || n == "stagger" || n == "whiff") {
    using Sp = blorb::TurnReport::Special;
    s.me.windup = blorb::Windup::Heavy;
    s.me.stance = int8_t(blorb::Stance::Pounce);
    s.them.stance = int8_t(blorb::Stance::Trick);
    if (n == "landing") {
      s.me.special = Sp::Landed;
      s.me.fired = true;
      s.won = Side::Me;
      s.them.hit = std::min(SpecialRules::hearts(blorb::Windup::Heavy), s.them.hearts);   // it downs him
      s.them.hearts = 0;
    } else if (n == "riposte") {
      s.me.special = Sp::Riposted;
      s.me.windup = blorb::Windup::Quick;   // a heavy riposted would down him: this one he survives
      s.me.stance = int8_t(blorb::Stance::Trick);
      s.them.stance = int8_t(blorb::Stance::Pounce);
      s.won = Side::Them;
      s.me.hit = SpecialRules::hearts(blorb::Windup::Quick);
      s.me.hearts = uint8_t(s.me.hearts - s.me.hit);
    } else if (n == "stagger") {
      s.me.special = Sp::Staggered;
      s.me.landsIn = 1;
      s.them.stance = int8_t(blorb::Stance::Trick);
      s.won = Side::Them;
      s.me.hit = DuelRules::kClashHearts;
    } else {
      s.me.special = Sp::Whiffed;
      s.them.stance = int8_t(blorb::Stance::Guard);
      s.won = Side::Them;
    }
  } else if (n == "outcome") {
    s.stakes = Stakes::Scrap;
    s.won = Side::Them;
    s.me.hearts = 0;
    s.me.look.scars.mark[2] = {blorb::Scar::Kind::Scorch, 210, 200, 255};
    s.me.look.scars.count = 3;
    s.newScars = 1;
  }
  return s;
}

}  // namespace blorbtest
