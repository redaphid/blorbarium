#pragma once
// His idle repertoire (defs/idles.def): the many small things he does on his
// own when he is awake and nothing presses, so a kid watching for two minutes
// sees several different ones. The engine owns which and for how long (it is
// behaviour: it moves him, eases his drives, shows in STATE as action=), all
// deterministic from his rng; paint owns how each looks.
#include <cstdint>
#include "blorb/fixed.h"
#include "blorb/ids.h"
#include "blorb/habitat.h"
#include "blorb/registry.h"
#include "blorb/species.h"

namespace blorb {

using IdleId = Id<struct IdleTag>;

enum class IdleFamily : uint8_t { Romp, Nose, Show, Muse, Night, Feel };
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
  // His day (#61): ageTicks + 1 when he last woke from a nap (kNapTicks or
  // more: groggy a while), and from a night's sleep (kNightSleepTicks or more:
  // his morning); 0 = not since he hatched (his hatching was his morning).
  uint32_t wokeAt = 0, dayAt = 0;
  // How briskly he plays the act now, percent of its own pace (idlePace): set
  // when it begins, from his genes, his age and how he feels. Paint runs the
  // act's motion at this pace, and the act lasts the longer for a slow one.
  uint8_t pace = 100;
  bool active() const { return now != idles::none; }
  // A hand on the badge asks for a greeting, at most once a minute.
  void touched(uint32_t age) {
    if (!greetedAt || age + 1 - greetedAt >= 60 * 1000 / kTickMs) greet = true;
  }
};

// The part of his day (#61), which leans what he does (idle.cpp kWhen):
// Waking (just up from a long sleep: yawns, a stretch, a wash), Morning
// (exercises, a song, tidying), Midday (romps, the sun), Afternoon (dreams,
// finds), Evening (winding down, fireflies), Night (awake in the dark: the
// stars, fireflies). By the wall's hour when a phone set it (PetClock
// trusted); with no clock, by his own day: how long since he woke.
enum class DayPart : uint8_t { Waking, Morning, Midday, Afternoon, Evening, Night };
inline constexpr uint8_t kDayPartCount = 6;
const char* dayPartName(DayPart);

// Up from a sleep this long he is groggy a while (Waking); from one this long,
// a night's, his day starts again (no clock: his morning).
inline constexpr uint32_t kNapTicks = 20u * kTicksPerMinute;
inline constexpr uint32_t kNightSleepTicks = 3u * 60u * kTicksPerMinute;

// What tells him the part of his day.
struct DayCues {
  int8_t hour = -1;          // the wall's hour, 0..23, when known (PetClock trusted); -1 none
  bool dark = false;         // night, lidded or dim (PetClock::dark)
  uint32_t upTicks = 0;      // since he last woke from a nap or a night (IdleState::wokeAt), or hatched
  uint32_t dayTicks = 0;     // since he woke from a night's sleep (IdleState::dayAt), or hatched
  Fx sleepiness{};
};
DayPart dayPartOf(const DayCues&);
// The wall's hour from the day loci (senses.cpp DayDetector); -1 with no clock (both 0.5).
int8_t hourOfDayLoci(Fx daySin, Fx dayCos);

// What the chooser weighs.
struct IdleMood {
  SpeciesId species = kGrungo;
  uint32_t genomeHash = 0;             // his own favourites, about his species'
  Fx boredom, loneliness, needTouch, sleepiness, explore;
  bool dark = false;                   // night, lidded or dim (PetClock::dark): the night idles
  uint32_t age = 0;                    // ageTicks, for the cooldowns
  bool greeting = false;               // a kid's hand just now: a Show idle or a dance, cooldowns aside
  // #61: the part of his day, how he is, his age, and what his genes make him.
  DayPart part = DayPart::Midday;
  Fx hunger, pain, discomfort, injury, toxin;
  Fx content;                          // 1 when no drive is past its mark (Appearance::moods' content)
  Stage stage = Stage::Adult;
  bool pressed = false;                // something presses (not calm): only how he feels shows (feels())
  // Genes (#61, owner: "the behavior should represent it's genetics"):
  Fx habituation;                      // temperament: tires of a thing sooner (longer cooldowns, less of one family)
  uint16_t dreamLenTicks = 30;         // temperament: a dreamer muses more
  uint8_t bulk = 128;                  // size gene base: a big one romps less and moves slower, a small one more and quicker
  // #33 (pets near other pets) hooks here: how many friends' pets he can
  // hear. Nothing sets it yet, and no idle reads it.
  uint8_t friendsNear = 0;
};

// How likely (0..1) a plain rest or wander he picked becomes play: his
// species' liveliness, more when he is bored.
Fx idleLiveliness(const IdleMood&);

// One act, by his favourites, drives and cooldowns; idles::none when every one
// is cooling down. Draws from `rng` only. Pressed (m.pressed), only the acts
// that show what presses (feels()).
IdleId pickIdle(const IdleState&, const IdleMood&, Rng&);

// How much (permille, 0 = not now) `id` shows how he feels now: the Feel
// acts (hungry, sleepy, hurt, sick) and a yawn. Only these play while
// something presses; a Feel act goes on only while its feeling lasts.
int32_t feels(IdleId, const IdleMood&);

// The pace he plays at now, percent (idlePace): brisk when young, small or
// content; slow when old, big, sleepy, hurt, sick or hungry.
uint8_t idlePace(const IdleMood&);

// Starts `id` in `s`: its length (seconds 0.8x to 1.3x, from `rng`, and longer
// for a slow pace), its anchor, the pace, and the cooldown's start.
void beginIdle(IdleState& s, IdleId id, DishPos at, uint32_t age, Rng& rng, uint8_t pace = 100);

// The face he wears `ticks` into an act `length` long: the row's, moved
// through the act for a few (a stretch ends in a yawn, a sneeze builds).
ExprId idleFace(IdleId, uint16_t ticks, uint16_t length);

// What a tick of it does to him, as drive changes (drive, amount) applied by
// the creature: a share of the whole act's effect each tick.
struct IdleEffect { DriveId drive; Fx perTick; };
uint8_t idleEffects(IdleId, uint16_t length, IdleEffect (&out)[3]);

}  // namespace blorb
