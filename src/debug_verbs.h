// `DEBUG <verb> <a> <b>` lines, for a person at a serial console or a sim
// script, never the phone: they never cross the protocol, so a phone cannot
// feed him or skip his childhood. Each one reaches into the engine through
// the Dish's own accessors and runs real ticks, so whatever state it leaves
// is one the engine itself could have reached, and it saves cleanly.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <variant>

#include "blorb/dish.h"
#include "blorb/founding.h"
#include "blorb/registry.h"
#include "blorb/species.h"
#include "debugble/jump.h"

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

// A species by its species.def slug or its number. Any byte is accepted, so
// the fallback for an id this firmware has no row for can be looked at too.
inline bool speciesByName(const char* s, blorb::SpeciesId& out) {
  for (const blorb::SpeciesInfo& r : blorb::SPECIES)
    if (!std::strcmp(r.name, s)) { out = r.id; return true; }
  char* end = nullptr;
  const long n = std::strtol(s, &end, 10);
  if (end == s || *end || n < 0 || n > 255) return false;
  out = blorb::SpeciesId{uint8_t(n)};
  return true;
}

// `DEBUG found <slug|id|off>`, for an adult before a device is handed over:
// the species a fresh founding founds over the badge table's, kept in
// found.pin (founding.h); `off` gives the table back. Refused once a pet lives here, so it never
// writes to a keepsake's flash. nullptr on success, else why not.
inline const char* found(blorb::Storage& store, const char* arg) {
  if (!blorb::needsFounding(store)) return "a pet lives here: a pin only seeds a fresh founding";
  if (!std::strcmp(arg, "off")) return blorb::clearFoundPin(store) ? nullptr : "found.pin would not move aside";
  blorb::SpeciesId s;
  if (!speciesByName(arg, s) || s.v >= blorb::kSpeciesCount) return "not a registered species.def name or id";
  return blorb::writeFoundPin(store, s) ? nullptr : "found.pin was not written";
}

struct StageName { const char* name; blorb::Stage stage; };
inline constexpr StageName kStageNames[] = {
    {"hatchling", blorb::Stage::Baby},
    {"child", blorb::Stage::Child},
    {"adult", blorb::Stage::Adult},
    {"elder", blorb::Stage::Elder},
};

// Hatches the egg if there is one, then ages him into `stage` through his own
// stage receptors and settles his drives (debugble::growTo, which the debug
// link's STAGE shares). nullptr on success, else why not.
inline const char* growTo(blorb::Dish& dish, const char* name, Warp warp) {
  const StageName* want = nullptr;
  for (const StageName& s : kStageNames)
    if (!std::strcmp(s.name, name)) want = &s;
  if (!want) return "stage is one of hatchling, child, adult, elder";
  switch (debugble::growTo(dish, want->stage, warp)) {
    case debugble::Jump::Done: return nullptr;
    case debugble::Jump::PastStage: return "he is already past that stage";
    case debugble::Jump::Died: return "he died on the way";
    default: return "no creature in the dish (a clutch waits for a pick)";
  }
}

}  // namespace debugverbs
