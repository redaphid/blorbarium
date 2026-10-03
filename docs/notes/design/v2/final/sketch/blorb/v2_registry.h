#pragma once
// v2 FINAL SKETCH. Expands the v2 registries (topics, omens, cues, gestures,
// games, stations) the way registry.h expands the old ones: one row gives a
// typed constant, a table row and a derived count. Every rule a maintainer
// could break by adding a row is a static_assert here.
#include <cstddef>
#include <cstdint>
#include "blorb/registry.h"

namespace blorb {

using OmenId = Id<struct OmenTag>;
using TopicId = Id<struct TopicTag>;   // the thoughts branch declares it in ids.h; v2 uses that one
using GameId = Id<struct GameTag>;
using OmenMask = uint16_t;             // one bit per omen
using StimMask = uint64_t;             // one bit per stimulus id (ids are < 64)

constexpr StimMask stimBit(StimId s) { return StimMask(1) << s.v; }

enum class Glyph : uint8_t { knuckle, knuckles, bell, hand, cupped, bolt, swirl, star, pellet, fly, marble, wheel, lid, moon, door, phone, pad, drum };
enum class Mode : uint8_t { Egg, Live, Wheel, Game, Clutch };
enum class Station : uint8_t { Home, Here, Roam, Food, Bell, Rim, Downhill, Uphill, Marble, Fly, Game };

// OracleGene's topic weights are this many bytes, frozen with its layout.
inline constexpr size_t kOracleTopicBytes = 12;

namespace topic {
#define BLORB_TOPIC(id, name, grounded, loc) inline constexpr TopicId name{id};
#include "blorb/defs/topics.def"
#undef BLORB_TOPIC
}  // namespace topic
namespace omen {
#define BLORB_OMEN(id, name, s, t, g) inline constexpr OmenId name{id};
#include "blorb/defs/omens.def"
#undef BLORB_OMEN
}  // namespace omen
namespace gameid {
#define BLORB_GAME(id, name, Type, caps, secs, unlock, ctx, moves, glyph) inline constexpr GameId name{id};
#include "blorb/defs/games.def"
#undef BLORB_GAME
}  // namespace gameid

struct TopicInfo { TopicId id; const char* name; bool grounded; LocusId expect; };
struct OmenInfo { OmenId id; const char* name; StimId stim; TopicId topic; Glyph glyph; };
struct RouteInfo { Mode mode; StimId from; StimId to; };
struct GameInfo { GameId id; const char* name; StimMask captures; uint16_t seconds; uint32_t unlock; uint8_t contexts, moves; Glyph glyph; };
struct StationInfo { ActionId action; Station station; };

inline constexpr TopicInfo TOPICS[] = {
#define BLORB_TOPIC(id, name, grounded, loc) {TopicId{id}, #name, grounded != 0, locus::loc},
#include "blorb/defs/topics.def"
#undef BLORB_TOPIC
};
inline constexpr OmenInfo OMENS[] = {
#define BLORB_OMEN(id, name, s, t, g) {OmenId{id}, #name, stim::s, topic::t, Glyph::g},
#include "blorb/defs/omens.def"
#undef BLORB_OMEN
};
inline constexpr LocusId ORACLE_CUES[] = {
#define BLORB_CUE(l) l,
#include "blorb/defs/cues.def"
#undef BLORB_CUE
};
inline constexpr RouteInfo ROUTES[] = {
#define BLORB_ROUTE(mode, from, to) {Mode::mode, stim::from, stim::to},
#include "blorb/defs/gestures.def"
#undef BLORB_ROUTE
};
inline constexpr GameInfo GAMES[] = {
#define BLORB_GAME(id, name, Type, caps, secs, unlock, ctx, moves, glyph) \
  {GameId{id}, #name, StimMask(caps), secs, uint32_t(unlock), ctx, moves, Glyph::glyph},
#include "blorb/defs/games.def"
#undef BLORB_GAME
};
inline constexpr StationInfo STATIONS[] = {
#define BLORB_STATION(a, s) {action::a, Station::s},
#include "blorb/defs/stations.def"
#undef BLORB_STATION
};

inline constexpr size_t kTopicCount = countOf(TOPICS);
inline constexpr size_t kOmenCount = countOf(OMENS);
inline constexpr size_t kGameCount = countOf(GAMES);

// The expect band is whatever topics.def names, never a hard-coded id range.
constexpr bool isExpectLocus(LocusId l) {
  for (const TopicInfo& t : TOPICS)
    if (t.grounded && t.expect == l) return true;
  return false;
}

// Pure: what a raw body stimulus becomes in a mode. No row means itself.
constexpr StimId route(Mode m, StimId s) {
  for (const RouteInfo& r : ROUTES)
    if (r.mode == m && r.from == s) return r.to;
  return s;
}

constexpr Station stationOf(ActionId a) {
  for (const StationInfo& s : STATIONS)
    if (s.action == a) return s.station;
  return Station::Home;
}

// ---- compile-time checks: a bad row fails the build ------------------------------
constexpr bool routesUnambiguous() {
  for (size_t i = 0; i < countOf(ROUTES); ++i)
    for (size_t j = i + 1; j < countOf(ROUTES); ++j)
      if (ROUTES[i].mode == ROUTES[j].mode && ROUTES[i].from == ROUTES[j].from) return false;
  return true;
}
constexpr bool holdNeverRouted() {
  for (const RouteInfo& r : ROUTES)
    if (r.from == stim::button_hold) return false;
  return true;
}
constexpr bool omensNameRealStimuli() {
  for (const OmenInfo& o : OMENS) {
    bool found = false;
    for (const StimInfo& s : STIMULI) found = found || s.id == o.stim;
    if (!found) return false;
  }
  return true;
}
constexpr bool omensOnGroundedTopics() {
  for (const OmenInfo& o : OMENS)
    for (const TopicInfo& t : TOPICS)
      if (t.id == o.topic && !t.grounded) return false;
  return true;
}
constexpr bool everyGroundedTopicHasAnOmen() {
  for (const TopicInfo& t : TOPICS) {
    if (!t.grounded) continue;
    bool found = false;
    for (const OmenInfo& o : OMENS) found = found || o.topic == t.id;
    if (!found) return false;
  }
  return true;
}
// A game may not capture what ends games or what he must always feel.
constexpr bool gamesCaptureSafely() {
  constexpr StimMask never = stimBit(stim::button_hold) | stimBit(stim::dropped) | stimBit(stim::shake) |
                             stimBit(stim::lid_down);
  for (const GameInfo& g : GAMES)
    if (g.captures & never) return false;
  return GAMES[0].captures == 0;
}
inline constexpr uint8_t kMaxContexts = 16, kMaxMoves = 4;
constexpr bool gamesFitSkill() {
  for (const GameInfo& g : GAMES) {
    const bool none = g.contexts == 0 && g.moves == 0;
    const bool fits = g.contexts >= 1 && g.contexts <= kMaxContexts && g.moves >= 2 && g.moves <= kMaxMoves;
    if (!none && !fits) return false;
  }
  return true;
}
constexpr bool everyActionHasOneStation() {
  for (const ActionInfo& a : ACTIONS) {
    size_t n = 0;
    for (const StationInfo& s : STATIONS) n += s.action == a.id ? 1 : 0;
    if (n != 1) return false;
  }
  return countOf(STATIONS) == kActionCount;
}

static_assert(routesUnambiguous(), "gestures.def: a (mode, stimulus) pair has two routes");
static_assert(holdNeverRouted(), "gestures.def: BOOT hold feeds him in every mode; do not route it");
static_assert(kOmenCount <= 16, "an OmenMask is 16 bits");
static_assert(kTopicCount <= kOracleTopicBytes, "a new topic past OracleGene's frozen width needs a new gene type");
static_assert(omensNameRealStimuli(), "omens.def names a stimulus that is not in stimuli.def");
static_assert(omensOnGroundedTopics(), "omens.def puts an omen on an improv topic");
static_assert(everyGroundedTopicHasAnOmen(), "a grounded topic with no omen can never be foreseen");
static_assert(gamesCaptureSafely(), "a game captures BOOT hold, a drop, a shake or the lid, or the ambient row captures");
static_assert(gamesFitSkill(), "a game's contexts x moves must fit Skill, or be 0 x 0");
static_assert(everyActionHasOneStation(), "every action needs exactly one station");
static_assert(idsUnique(OMENS) && idsUnique(TOPICS) && idsUnique(GAMES), "a v2 registry has a duplicate id");

}  // namespace blorb
