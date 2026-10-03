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
// Verbs are defs/commands.def. Feeding is not a verb, and STIM fires only
// Phone-source stimuli (petted, played, spoken_to): the body owns every care
// loop. A kConsent verb (RESTORE) answers "ERR 428 NEEDS_CONSENT window=20"
// and arms a window; holding the button on the device pushes "! CONSENT
// granted" and the phone resends. A grant is single-use.
#include <cstdint>
#include <string_view>
#include <vector>
#include "blorb/ids.h"

namespace blorb {

class Dish;   // dish.h
class Link;   // seams.h

// Parsed at the boundary; handlers never see the raw line.
struct Request { uint32_t id; std::string_view verb; std::string_view args; };

// Line builder that enforces the framing and the 200-byte cap.
class Reply {
 public:
  Reply(Link&, uint32_t id);
  Reply& line(const char* fmt, ...);               // "+ ..." continuation
  void chunks(const uint8_t* data, size_t len);    // base64 lines, then OK <len> <crc>
  void ok(const char* fmt = "", ...);
  void err(uint16_t code, const char* text);
 private:
  Link& link_;
  uint32_t id_;
};

// The text sink gene describe_<name>() and describeDiff() write into.
struct Describe {
  Reply& out;
  void field(const char* name, int value);
  void field(const char* name, const char* value);
};

enum CmdFlags : uint8_t { kMutating = 1, kConsent = 2 };
struct CommandInfo { const char* verb; void (*run)(Dish&, const Request&, Reply&); uint8_t flags; };

#define BLORB_CMD(VERB, name, flags) void cmd_##name(Dish&, const Request&, Reply&);
#include "blorb/defs/commands.def"
#undef BLORB_CMD

inline constexpr CommandInfo COMMANDS[] = {
#define BLORB_CMD(VERB, name, flags) {#VERB, &cmd_##name, flags},
#include "blorb/defs/commands.def"
#undef BLORB_CMD
};

class Protocol {
 public:
  // Parse and dispatch one line. Garbage or overlong input gets ERR, never a crash.
  void handle(std::string_view line, Dish&, Link&);
  // Push subscribed events (STATE every 2 s, STIM, STAGE, DIED, CLUTCH, HATCHED, CONSENT).
  void pump(Dish&, Link&, uint32_t tick);
  void disconnected();             // drops subscriptions and any RESTORE in progress
 private:
  uint32_t subMask_ = 0;
  uint32_t lastStateTick_ = 0;
  std::vector<uint8_t> inbound_;   // RESTORE assembly; cleared on ERR or disconnect
  uint32_t inboundExpect_ = 0, inboundCrc_ = 0;
};

}  // namespace blorb
