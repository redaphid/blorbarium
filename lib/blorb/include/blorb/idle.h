#pragma once
// His idle repertoire (defs/idles.def): the many small things he does on his
// own when he is awake and nothing presses, so a kid watching for two minutes
// sees several different ones. The engine owns which and for how long (it is
// behaviour: it moves him, eases his drives, shows in STATE as action=), all
// deterministic from his rng; paint owns how each looks.
#include <cstdint>
#include "blorb/fixed.h"
#include "blorb/habitat.h"
#include "blorb/registry.h"
#include "blorb/species.h"

namespace blorb {

using IdleId = Id<struct IdleTag>;

enum class IdleFamily : uint8_t { Romp, Nose, Show, Muse, Night };
enum class IdleMove : uint8_t { Stay, Rim, Spin, Home, Roam, Push, Zoom };

struct IdleInfo {
  IdleId id;
  const char* name;
  IdleFamily family;
  uint8_t seconds;
  uint16_t cooldownS;
  ExprId face;
  IdleMove move;
};

namespace idles {
inline constexpr IdleId none{255};
#define BLORB_IDLE(id, name, family, seconds, cooldown, face, move) inline constexpr IdleId name{id};
#include "blorb/defs/idles.def"
#undef BLORB_IDLE
}  // namespace idles

inline constexpr IdleInfo IDLES[] = {
#define BLORB_IDLE(id, name, family, seconds, cooldown, face, move) \
  {IdleId{id}, #name, IdleFamily::family, seconds, cooldown, expr::face, IdleMove::move},
#include "blorb/defs/idles.def"
#undef BLORB_IDLE
};
inline constexpr size_t kIdleCount = countOf(IDLES);

constexpr bool idleIdsAreDense() {
  for (size_t i = 0; i < kIdleCount; ++i) if (IDLES[i].id.v != i) return false;
  return true;
}
static_assert(idleIdsAreDense(), "idles.def: ids are the rows' order, from 0");

inline const IdleInfo* idleInfo(IdleId id) { return id.v < kIdleCount ? &IDLES[id.v] : nullptr; }
inline const char* idleName(IdleId id) { return id.v < kIdleCount ? IDLES[id.v].name : "none"; }

// The act he is in, and when he last did each one. The creature's, saved and hashed.
struct IdleState {
  IdleId now = idles::none;
  uint16_t ticks = 0;                  // into it
  uint16_t length = 0;                 // how long it runs
  IdleId last = idles::none;            // the one before, never chosen twice running
  DishPos anchor{};                    // where it began: a spin's centre
  uint32_t startedAt[kIdleCount]{};    // ageTicks + 1 when each last began; 0 = never
  bool greet = false;                  // a kid's hand: the next play greets him (pickIdle)
  uint32_t greetedAt = 0;              // ageTicks + 1 at the last greeting; 0 = never
  bool active() const { return now != idles::none; }
  // A hand on the badge asks for a greeting, at most once a minute.
  void touched(uint32_t age) {
    if (!greetedAt || age + 1 - greetedAt >= 60 * 1000 / kTickMs) greet = true;
  }
};

// What the chooser weighs.
struct IdleMood {
  SpeciesId species = kGrungo;
  uint32_t genomeHash = 0;             // his own favourites, about his species'
  Fx boredom, loneliness, needTouch, sleepiness, explore;
  bool dark = false;                   // night, lidded or dim (PetClock::dark): the night idles
  uint32_t age = 0;                    // ageTicks, for the cooldowns
  bool greeting = false;               // a kid's hand just now: a Show idle or a dance, cooldowns aside
  // #33 (pets near other pets) hooks here: how many friends' pets he can
  // hear. Nothing sets it yet, and no idle reads it.
  uint8_t friendsNear = 0;
};

// How likely (0..1) a plain rest or wander he picked becomes play: his
// species' liveliness, more when he is bored.
Fx idleLiveliness(const IdleMood&);

// One act, by his favourites, drives and cooldowns; idles::none when every one
// is cooling down. Draws from `rng` only.
IdleId pickIdle(const IdleState&, const IdleMood&, Rng&);

// Starts `id` in `s`: its length (seconds 0.8x to 1.3x, from `rng`), its
// anchor, and the cooldown's start.
void beginIdle(IdleState& s, IdleId id, DishPos at, uint32_t age, Rng& rng);

// The face he wears `ticks` into an act `length` long: the row's, moved
// through the act for a few (a stretch ends in a yawn, a sneeze builds).
ExprId idleFace(IdleId, uint16_t ticks, uint16_t length);

// What a tick of it does to him, as drive changes (drive, amount) applied by
// the creature: a share of the whole act's effect each tick.
struct IdleEffect { DriveId drive; Fx perTick; };
uint8_t idleEffects(IdleId, uint16_t length, IdleEffect (&out)[3]);

}  // namespace blorb
