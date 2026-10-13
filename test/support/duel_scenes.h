#pragma once
// One canonical DuelScene for each moment of a duel, for any two species: what
// the struct goldens, the PNG goldens, tools/duel_sheet.py and the demo film
// all draw, so they agree on what "the mercy screen" is.
#include <cstring>
#include <string>
#include "blorb/appearance.h"
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
    {"bonk", paint::DuelAct::Clash, 14},
    {"mercy", paint::DuelAct::Mercy, 20},
    {"fallen", paint::DuelAct::Fallen, 30},
    {"spared", paint::DuelAct::Fallen, 20},
    {"outcome", paint::DuelAct::Outcome, 24},
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
  s.me.hearts = 4;
  s.them.hearts = 3;
  s.me.maxHearts = 6;   // my kid has been losing: one assist heart, drawn with the pet's cheer
  s.me.assist = 1;
  s.me.sparks = 4;
  s.them.sparks = 2;

  if (n == "staking" || n == "staking_mortal") {
    s.me.chose = n == "staking" ? Stakes::Scrap : Stakes::Mortal;
    s.them.chose = Stakes::Mortal;
    s.me.skull = 70;
    s.them.skull = 150;
    s.me.hold = n == "staking" ? 0 : 140;
    s.them.confirmed = true;
    s.me.ceiling = n == "staking" ? Stakes::Scrap : Stakes::Mortal;
    s.me.confirmed = n == "staking";
  } else if (n == "choosing" || n == "locked") {
    s.aim = int8_t(blorb::Stance::Guard);
    s.lean = int8_t(blorb::Stance::Trick);
    s.whisper = 200;
    s.ticksLeft = n == "choosing" ? 70 : 48;
    s.me.locked = n == "locked";
    s.me.armed = n == "locked";
    s.them.locked = n == "locked";
  } else if (n == "clash" || n == "hit" || n == "bonk") {
    s.won = n == "bonk" ? Side::Neither : Side::Me;
    s.me.stance = int8_t(n == "bonk" ? blorb::Stance::Pounce : blorb::Stance::Guard);
    s.them.stance = int8_t(blorb::Stance::Pounce);
    s.them.hit = n == "bonk" ? 0 : 1;
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
