#pragma once
// The BLE wire, engine side. Lines in, lines out, over Nordic UART; the same
// grammar works over USB serial and from nRF Connect by hand.
//
//   request   "#<id> <VERB> [args]"         id = 1..6 hex digits chosen by the phone; none = id 0
//   reply     "#<id> + <payload>"           zero or more continuation lines
//             "#<id> OK [k=v ...]"          final line on success
//             "#<id> ERR <code> <text>"
//   event     "! <NAME> [k=v ...]"          unsolicited, after SUB
//   Lines are at most 200 bytes, one notification at MTU 247. Longer input is
//   ERR 413, never truncated. Binary payloads are base64 in "+ <offset> <b64>"
//   lines closed by "OK <total> <crc32hex>".
//
// Verbs are defs/commands.def. The board is the one writer (DEVIATIONS.md 4):
// the phone reads STATE or the whole SNAPSHOT, and changes the live state only
// through TWIST ops (defs/twists.def), which the board applies between ticks
// after checking only that they are well formed. On connect it pushes nothing.
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
#include "blorb/ids.h"

namespace blorb {

class Dish;   // dish.h
class Link;   // seams.h

// Parsed at the boundary; handlers never see the raw line.
struct Request { uint32_t id; std::string_view verb; std::string_view args; };

// A request's arguments, consumed word by word. Each reader returns nullopt
// for a missing or ill-formed word, so a handler checks every word it takes.
struct Args {
  std::string_view rest;
  std::optional<std::string_view> word();
  std::optional<uint32_t> number(uint32_t max);              // decimal, 0..max
  std::optional<int32_t> signedNumber(int32_t min, int32_t max);
  bool done();                                                // nothing left but spaces
};

// Line builder that enforces the framing and the 200-byte cap.
class Reply {
 public:
  Reply(Link&, uint32_t id);
  Reply& line(const char* fmt, ...);               // "+ ..." continuation
  void chunks(const uint8_t* data, size_t len);    // base64 lines, then OK <len> <crc>
  void ok(const char* fmt = "", ...);
  void err(uint16_t code, const char* text);
  bool succeeded() const { return ok_; }
 private:
  Link& link_;
  uint32_t id_;
  bool ok_ = false;
};

// The text sink gene describe_<name>() and describeDiff() write into.
struct Describe {
  Reply& out;
  void field(const char* name, int value);
  void field(const char* name, const char* value);
};

enum CmdFlags : uint8_t { kMutating = 1 };   // a successful run marks the keepsake dirty
struct CommandInfo { const char* verb; void (*run)(Dish&, const Request&, Reply&); uint8_t flags; };

#define BLORB_CMD(VERB, name, flags) void cmd_##name(Dish&, const Request&, Reply&);
#include "blorb/defs/commands.def"
#undef BLORB_CMD

inline constexpr CommandInfo COMMANDS[] = {
#define BLORB_CMD(VERB, name, flags) {#VERB, &cmd_##name, flags},
#include "blorb/defs/commands.def"
#undef BLORB_CMD
};

// A twist parses all of its args before it touches the dish, so a refused op
// leaves the state exactly as it was.
enum class TwistStatus : uint8_t { Applied, Malformed, OutOfRange, NotNow };
struct TwistInfo { const char* name; const char* args; TwistStatus (*apply)(Dish&, Args&); };

#define BLORB_TWIST(name, args) TwistStatus twist_##name(Dish&, Args&);
#include "blorb/defs/twists.def"
#undef BLORB_TWIST

inline constexpr TwistInfo TWISTS[] = {
#define BLORB_TWIST(name, args) {#name, args, &twist_##name},
#include "blorb/defs/twists.def"
#undef BLORB_TWIST
};

class Protocol {
 public:
  // Parse and dispatch one line. Garbage or overlong input gets ERR, never a crash.
  void handle(std::string_view line, Dish&, Link&);
  // Push subscribed events: STATE every 2 s, and STAGE, DIED, CLUTCH, PICKED, HATCHED as they happen.
  void pump(Dish&, Link&, uint32_t tick);
  void disconnected();             // drops subscriptions
  void subscribe(uint32_t eventMask) { subMask_ = eventMask; }   // SUB
 private:
  uint32_t subMask_ = 0;
  uint32_t lastStateTick_ = 0;
  // What pump saw last, so an event is an edge: occupant kind, stage, eggs shown.
  uint8_t seenKind_ = 255;
  uint8_t seenStage_ = 255;
  bool seenEggs_ = false;
};

}  // namespace blorb
