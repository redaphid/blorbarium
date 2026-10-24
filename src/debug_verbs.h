// `DEBUG <verb> <a> <b>` lines, for a person at a serial console or a sim
// script, never the phone: they never cross the protocol, so a phone cannot
// feed him or skip his childhood. Each one reaches into the engine through
// the Dish's own accessors and runs real ticks, so whatever state it leaves
// is one the engine itself could have reached, and it saves cleanly.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
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

// A drive or a chemical by name (or a chemical by number), as the debug link's CHEM reads it.
inline bool chemByName(const char* name, blorb::ChemId& out) {
  const std::optional<blorb::ChemId> c = debugble::chemNamed(name);
  if (c) out = *c;
  return bool(c);
}

// `DEBUG chem <name> <permille>`: one chemical or drive (hunger, sleepiness,
// injury, toxin...) set as the debug link's CHEM sets it (debugble::setChems),
// on the cable with no unlock: a cable is a person's hands on the badge.
// nullptr on success, with `level` what he holds now (permille), else why not.
inline const char* chem(blorb::Dish& dish, const char* name, const char* permille, int& level) {
  const std::optional<blorb::ChemId> id = debugble::chemNamed(name);
  if (!*name || !id) return "not a drive or chemical (hunger, sleepiness, injury, toxin, ...)";
  long v = 0;
  const char* p = permille;
  for (; *p >= '0' && *p <= '9' && v <= 1000; ++p) v = v * 10 + (*p - '0');
  if (p == permille || *p || v > 1000) return "want <permille 0..1000>";
  const debugble::ChemSet set{*id, blorb::Fx::ratio(int32_t(v), 1000)};
  if (debugble::setChems(dish, &set, 1) != debugble::Jump::Done) return "no creature in the dish";
  const blorb::Fx now = std::get<blorb::Creature>(dish.occupant()).chemistry().chem[id->v];
  level = int((int64_t(now.raw) * 1000 + (now.raw >= 0 ? blorb::Fx::kOne / 2 : -blorb::Fx::kOne / 2)) / blorb::Fx::kOne);
  return nullptr;
}

// `DEBUG pickup`: the raw IMU sample (milli-g, the IMU's frame) of a badge
// lying still on its back, then lifted and carried, then set down, `since` ms
// in; false once it is over. `s` comes in as the IMU read it and goes out
// posed: the first and last kPickupEaseMs blend from and back to the real
// reading, so neither end is a jolt. The lift tips it toward `home` (2: raw
// +y up, 0: raw -y up), as a hand lifts a badge to look at it, well past
// HeldDetector's 0.3 g in the plane for its picked_up, while the face stays
// up past 0.5 g so no hang or side pose is ever read. The carry sways and
// bobs gently: moving, so never a cradle, and never a jolt (no knock, no
// shake). `noise`: any random word, for a table's and a hand's tremor.
constexpr uint32_t kPickupStillMs = 2000;   // past HeldDetector's 1.5 s let-go: whatever came before, he is put down
constexpr uint32_t kPickupEaseMs = 300;     // from the real reading at the start, back to it at the end
constexpr uint32_t kPickupLiftMs = 400, kPickupCarryMs = 2400, kPickupSetMs = 400;
constexpr uint32_t kPickupMs = kPickupStillMs + kPickupLiftMs + kPickupCarryMs + kPickupSetMs + kPickupEaseMs;
inline bool pickupPose(uint32_t since, uint8_t home, uint32_t noise, blorb::BodySample& s) {
  if (since >= kPickupMs) return false;
  const int32_t up = home == 2 ? 600 : -600;   // raw y toward home's up, at the carry's tilt
  const auto tri = [](uint32_t t, uint32_t period, int32_t amp) {   // a triangle wave, -amp..amp
    const int32_t ph = int32_t(t % period) * 4 * amp / int32_t(period);
    return ph < 2 * amp ? ph - amp : 3 * amp - ph;
  };
  const uint32_t carryAt = kPickupStillMs + kPickupLiftMs, setAt = carryAt + kPickupCarryMs;
  int32_t lift = 0;   // 0 flat .. 1000 lifted
  if (since >= kPickupStillMs && since < carryAt) lift = int32_t(since - kPickupStillMs) * 1000 / int32_t(kPickupLiftMs);
  else if (since >= carryAt && since < setAt) lift = 1000;
  else if (since >= setAt && since < setAt + kPickupSetMs) lift = 1000 - int32_t(since - setAt) * 1000 / int32_t(kPickupSetMs);
  int32_t ax = 0, ay = up * lift / 1000, az = 1000 - 200 * lift / 1000;
  // The carry: whole swings and bobs (kPickupCarryMs is 3 and 4 of them) that
  // start and end level. The bob takes the body past 1 g +- 0.12 every swing,
  // so HeldDetector never finds him still long enough for a cradle.
  if (since >= carryAt && since < setAt) ax += tri(since - carryAt + 200, 800, 150), az += tri(since - carryAt + 150, 600, 200);
  const int32_t tremor = lift ? 8 : 2, span = 2 * tremor + 1;
  ax += int32_t(noise % span) - tremor;
  ay += int32_t((noise >> 8) % span) - tremor;
  az += int32_t((noise >> 16) % span) - tremor;
  // How much of the real reading is left: all of it at 0, none once eased in, all again at the end.
  const uint32_t outAt = kPickupMs - kPickupEaseMs;
  const int32_t real = since < kPickupEaseMs ? 1000 - int32_t(since * 1000 / kPickupEaseMs)
                       : since >= outAt     ? int32_t((since - outAt) * 1000 / kPickupEaseMs)
                                            : 0;
  s.ax = int16_t((s.ax * real + ax * (1000 - real)) / 1000);
  s.ay = int16_t((s.ay * real + ay * (1000 - real)) / 1000);
  s.az = int16_t((s.az * real + az * (1000 - real)) / 1000);
  return true;
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
