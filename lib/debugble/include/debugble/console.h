#pragma once
// The debug link (docs/debug-ble.md): lines on their own characteristic,
// 6E400005, that jump the pet between states so a person can test the
// game. In every build, release included. Free of Arduino and NimBLE: what
// it needs from the board comes in through Board, so the native suite drives
// every command and every refusal.
//
// The grammar is the protocol's (protocol.h): `#<id> VERB args`, answered by
// `#<id> + ...` lines and one `#<id> OK ...` or `#<id> ERR <code> <TEXT>`.
// Events are `! DEBUG ...`.
//
// These are children's pets, and a stranger's phone in range can connect.
// So every verb has a reach, and the reach decides who may run it and when:
#include <cstdint>
#include <optional>
#include <string_view>
#include "blorb/dish.h"
#include "blorb/protocol.h"
#include "blorb/seams.h"
#include "blorb/species.h"
#include "debugble/time_scale.h"
#include "debugble/unlock.h"

namespace debugble {

enum class Reach : uint8_t {
  Look,    // reads, or asks for the unlock: answers in every state
  Show,    // changes only what the screen shows: nothing saved
  // Changes what the save holds, no more than his own life or a phone's TIME
  // and TWIST stimulus could. Refused while sealed (nothing may write), on
  // trial (nothing it changes would be kept) and during an update.
  Write,
  // Could hurt, end, age, replace or rewrite him. As Write, and also needs
  // this connection's unlock: a press on the dish's own button.
  Harm,
};

// What the console needs from the firmware around the Dish.
class Board {
 public:
  virtual ~Board() = default;
  virtual bool onTrial() = 0;      // this image has not kept itself: its storage writes nothing (docs/ota.md)
  virtual bool otaActive() = 0;    // an update session is open: it owns the button, and the pet holds still
  virtual void warp(uint32_t ticks) = 0;                    // that many 100 ms ticks of the Dish, now
  virtual void look(std::optional<blorb::SpeciesId>) = 0;   // drawn as this species; nullopt: his own. RAM only
  virtual std::optional<blorb::SpeciesId> looks() = 0;
  virtual void setClock(std::optional<uint32_t> unixSeconds) = 0;   // the clock the Dish reads before a phone's
  // Once the reply has gone: erase the pet, provision as a fresh board does,
  // pin `found` for the founding (none: the three-egg nest), and restart.
  // False when this board cannot.
  virtual bool wipe(std::optional<blorb::SpeciesId> found) = 0;
};

class Console {
 public:
  static constexpr uint32_t kMaxWarpMinutes = 120;

  explicit Console(Board& board) : board_(board) {}

  // One line from the debug characteristic, on connection `connection`
  // (NusLink::generation()), answered on `out`.
  void handle(std::string_view line, uint32_t connection, uint32_t nowMs, blorb::Dish&, blorb::Link& out);
  // Every loop. `live` is the connection up now, if any: the end of the one
  // that unlocked locks again, and the end of the one that set a time scale
  // puts him back on real time. An update opening does both.
  void tick(uint32_t nowMs, std::optional<uint32_t> live, blorb::Link& out);
  // The dish's button. True when the unlock's question took it.
  bool press(uint32_t nowMs) { return unlock_.press(nowMs); }

  Unlock& unlock() { return unlock_; }
  TimeScale& scale() { return scale_; }

  // Per-verb context, for the handlers in console.cpp.
  struct Call {
    Console& console;
    Board& board;
    blorb::Dish& dish;
    uint32_t connection, now;
  };

 private:
  Board& board_;
  Unlock unlock_;
  TimeScale scale_;
};

struct VerbInfo { const char* verb; void (*run)(Console::Call&, blorb::Args&, blorb::Reply&); Reach reach; const char* args; };

#define DEBUG_VERB(VERB, name, reach, args) void verb_##name(Console::Call&, blorb::Args&, blorb::Reply&);
#include "debugble/verbs.def"
#undef DEBUG_VERB

inline constexpr VerbInfo VERBS[] = {
#define DEBUG_VERB(VERB, name, reach, args) {#VERB, &verb_##name, Reach::reach, args},
#include "debugble/verbs.def"
#undef DEBUG_VERB
};

}  // namespace debugble
