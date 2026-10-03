#pragma once
// v2 FINAL SKETCH. Games: rule overlays on the World, one games.def row plus
// one struct here each. The Dish steps the ambient row every tick and at most
// one wheel game. A game sees every routed stimulus, may capture some (its
// mask), moves only pieces the World owns, asks his body to go somewhere or
// do something, and reaches his drives only through World stimuli. So the
// brain and the oracle learn a game like anything else, and deleting a game
// touches nothing in the creature.
//
// How he gets better at a game is its Skill table (candidate A): contexts x
// moves, played and learned only through chooseMove and learnMove. A game is
// its rules and its context, nothing more.
#include <cstdint>
#include "blorb/genes_v2.h"
#include "blorb/v2_registry.h"
#include "blorb/world.h"

namespace blorb {

// Per game, saved with the creature; summarised into the lineage at death.
struct Skill {
  Q15 win[kMaxContexts][kMaxMoves]{};   // chance the move scores in this context
  Fx fun{};                             // running mean boredom relief per session: the invitation's favourite
  uint16_t sessions = 0, points = 0, lost = 0, best = 0;
};

struct Talent { Fx tongueReach, explore, learnRate; uint16_t tempoTicks; Fx copyFidelity; };

// The shared policy: argmax of win plus exploration noise, and the NLMS step
// toward the outcome. Every game calls these and nothing else to play and learn.
uint8_t chooseMove(const Skill&, uint8_t context, uint8_t moves, const Talent&, Rng&);
void learnMove(Skill&, uint8_t context, uint8_t move, bool scored, const Talent&);

// What a game asks of his body this tick. The creature honours it after a
// reflex and before any behaviour, like a reflex would.
struct GameAsk {
  enum class Kind : uint8_t { None, GoTo, Snap, Croak, Hop } kind = Kind::None;
  DishPos at{};                         // GoTo: the game names where he stands (keeper: the pond arc)
};

struct GameCtx {
  World& world;
  const SenseOut& in;                   // this tick's routed stimuli and loci, before capture
  SenseOut& out;                        // game_point, game_lost, fly_caught ...
  DishPos him;
  Skill& skill;
  const Talent& talent;
  GameAsk& ask;
  uint32_t tick;
  Rng& rng;
};

enum class GameStatus : uint8_t { Running, Over };

namespace game {
struct Base {
  void start(GameCtx&) {}
  void stop(GameCtx&) {}
};
// Row 0, ambient: the marble is always a toy. Bonks, and he may nose it home.
struct MarbleToy : Base { GameStatus step(GameCtx&); };
// You tilt the marble at his pond on the bottom arc; he guards a third of it.
// Context: the marble's heading sector (3) x the tilt's direction (4). Moves:
// guard left, middle, right. He learns to read your wrist, not the marble.
struct Keeper : Base { uint8_t you = 0, him = 0, guard = 1; GameStatus step(GameCtx&); };
// Tilt to a pad, press to hide the fly; he hops onto one. Context: your last
// two hides (9). A patterned owner gets beaten, and he says so.
struct Pads : Base { uint8_t round = 0, hidden = 0, last[2]{1, 1}, found = 0; GameStatus step(GameCtx&); };
// A fly loops the dish; you press when it crosses his tongue line. Context:
// distance (4) x speed (4). Moves: snap or wait. He learns your timing, then
// snaps first. A caught fly is a small meal.
struct Snap : Base { uint8_t caught = 0, missed = 0; GameStatus step(GameCtx&); };
// You knock a beat; he croaks on the onset he predicts. Context: time since
// your last knock in 12 bins. In-sync croaks build the family rhythm.
struct Drum : Base { uint32_t lastKnock = 0; uint16_t interval = 0, onBeat = 0; uint8_t pattern = 0; GameStatus step(GameCtx&); };
// The bell drill: a knock is the cue, he acts, you ring, a pellet follows if
// the jar has one. No Skill: what he learns is in the brain (the clicker).
struct Drill : Base { uint32_t cueTick = 0; uint16_t rung = 0; GameStatus step(GameCtx&); };
}  // namespace game

// A finished game's line for the lineage book and the score on the rim.
struct GameResult { GameId game; uint16_t score; uint16_t best; bool newBest; };

struct Games {
#define BLORB_GAME(id, name, Type, caps, secs, unlock, ctx, moves, glyph) game::Type name;
#include "blorb/defs/games.def"
#undef BLORB_GAME
  Skill skills[kGameCount]{};
  GameId active{255};                   // 255 = no wheel game
  uint32_t startedTick = 0;
  bool wheelOpen = false;
  uint8_t pointer = 1;

  // The input mode the gesture routes use.
  Mode mode(bool creature, bool egg) const;
  // Ambient row, then the active game. Returns the stimuli it captured, which
  // the Dish clears before the creature ticks. `finished` is set on Over.
  StimMask step(GameCtx&, bool& finished, GameResult&);
  // What he invites you to: the unlocked game with the highest fun, ties to
  // the least recently played.
  GameId favourite(uint32_t legacyFeats) const;
};

}  // namespace blorb
