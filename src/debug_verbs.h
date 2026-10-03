// `DEBUG <verb> <a> <b>` lines, for a person at a serial console or a sim
// script, never the phone: they never cross the protocol, so a phone cannot
// feed him or skip his childhood. Each one reaches into the engine through
// the Dish's own accessors and runs real ticks, so whatever state it leaves
// is one the engine itself could have reached, and it saves cleanly.
#pragma once

#include <cstdint>
#include <cstring>
#include <variant>

#include "blorb/dish.h"
#include "blorb/registry.h"

namespace debugverbs {

// Runs `ticks` 100 ms ticks of the Dish, faster than real time.
using Warp = void (*)(uint32_t ticks);

// "0.8" or "1" to Fx, in integers.
inline blorb::Fx parseLevel(const char* s) {
  int32_t whole = 0, frac = 0, scale = 1;
  const bool neg = *s == '-';
  if (neg) ++s;
  for (; *s >= '0' && *s <= '9'; ++s) whole = whole * 10 + (*s - '0');
  if (*s == '.')
    for (++s; *s >= '0' && *s <= '9' && scale < 100000; ++s) { frac = frac * 10 + (*s - '0'); scale *= 10; }
  const blorb::Fx v = blorb::Fx::ratio(whole * scale + frac, scale);
  return neg ? -v : v;
}

inline bool chemByName(const char* name, blorb::ChemId& out) {
  for (const auto& c : blorb::CHEMICALS)
    if (!std::strcmp(c.name, name)) { out = c.id; return true; }
  for (const auto& d : blorb::DRIVES)
    if (!std::strcmp(d.name, name)) { out = blorb::driveChem(d.id); return true; }
  return false;
}

struct StageName { const char* name; blorb::Stage stage; };
inline constexpr StageName kStageNames[] = {
    {"hatchling", blorb::Stage::Baby},
    {"child", blorb::Stage::Child},
    {"adult", blorb::Stage::Adult},
    {"elder", blorb::Stage::Elder},
};

// Hatches the egg if there is one, then ages him into `stage` by lowering
// life a little at a time and letting his own genome's stage receptors fire,
// whatever thresholds his genes carry. Then every drive is settled (fed,
// rested, unhurt), a few ticks let his face catch up, and the Dish saves.
// nullptr on success, else why not.
inline const char* growTo(blorb::Dish& dish, const char* name, Warp warp) {
  using blorb::Fx;
  const StageName* want = nullptr;
  for (const StageName& s : kStageNames)
    if (!std::strcmp(s.name, name)) want = &s;
  if (!want) return "stage is one of hatchling, child, adult, elder";
  for (uint32_t t = 0; std::holds_alternative<blorb::Egg>(dish.occupant()) && t < 100000; t += 100) warp(100);
  auto* c = std::get_if<blorb::Creature>(&dish.occupant());
  if (!c) return "no creature in the dish (a clutch waits for a pick)";
  if (c->stage() > want->stage) return "he is already past that stage";
  const Fx nudge = Fx::ratio(1, 50);
  while (c->stage() < want->stage) {
    const Fx life = c->chemistry().chem[blorb::chem::life.v];
    if (life <= nudge) return "life ran out before the stage fired";
    c->inject(blorb::chem::life, life - nudge);
    warp(2);
    c = std::get_if<blorb::Creature>(&dish.occupant());
    if (!c) return "he died on the way";
  }
  for (const auto& d : blorb::DRIVES) c->inject(blorb::driveChem(d.id), Fx::zero());
  c->inject(blorb::chem::injury, Fx::zero());
  c->inject(blorb::chem::toxin, Fx::zero());
  c->inject(blorb::chem::adrenaline, Fx::zero());
  c->inject(blorb::chem::energy, Fx::one());
  warp(20);
  dish.flush();
  return nullptr;
}

}  // namespace debugverbs
