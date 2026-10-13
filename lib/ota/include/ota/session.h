#pragma once
// One firmware update, from the page's signed BEGIN to the restart. The
// protocol is docs/ota.md. Free of Arduino and NimBLE: the flash slot and the
// rest of the dish come in through Slot and Host, so the native suite drives
// every path with a slot in memory.
//
//   idle --BEGIN--> asking --button--> receiving --last byte--> ready --REBOOT--> restart
//
// A disconnect is not an event here. The session waits, and the page resumes
// by sending the same BEGIN, which answers where the session stands.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include "blorb/protocol.h"
#include "blorb/seams.h"
#include "ota/crypto.h"

namespace ota {

// The app slot that is not running. Firmware: esp_ota_*. Tests: memory.
class Slot {
 public:
  virtual ~Slot() = default;
  virtual uint32_t capacity() = 0;
  virtual bool open(uint32_t size) = 0;
  virtual bool write(const uint8_t*, size_t) = 0;
  virtual bool close() = 0;      // true when the bytes are a bootable image
  virtual bool activate() = 0;   // boot it at the next restart
  virtual void discard() = 0;    // forget it; the running image stays the one that boots
};

// What the rest of the dish tells an update.
class Host {
 public:
  virtual ~Host() = default;
  virtual bool busy() = 0;        // the pet is doing something an update must not interrupt
  virtual bool save() = 0;        // write the pet now; false when it did not land
  virtual uint16_t chunk() = 0;   // image bytes one data write can carry on this link
  virtual bool onTrial() = 0;     // this image has not yet kept itself (docs/ota.md "The trial")
};

struct Version {
  uint16_t major = 0, minor = 0, patch = 0;
  friend bool operator<(Version a, Version b) {
    if (a.major != b.major) return a.major < b.major;
    if (a.minor != b.minor) return a.minor < b.minor;
    return a.patch < b.patch;
  }
  friend bool operator==(Version a, Version b) { return !(a < b) && !(b < a); }
};
std::optional<Version> parseVersion(std::string_view);

// Everything BEGIN carries. All of it but the signature is signed.
struct Image {
  char board[16] = "";
  Version version;
  uint32_t flags = 0, size = 0;
  uint8_t sha256[32] = {};
  uint8_t sig[64] = {};
  bool operator==(const Image&) const;
};
constexpr uint32_t kAllowOlder = 1;

// How the last session ended, as STATUS's `last=` says it.
enum class Last : uint8_t { None, NoConfirm, Busy, SaveFailed, Aborted, Timeout, Overflow, BadHash, BadImage, FlashError };
const char* lastName(Last);

struct Config {
  const char* board;          // this dish's board, as images name it
  Version firmware;           // what is running
  const uint8_t* publicKey;   // 65 bytes, uncompressed P-256
};

// What the screen shows.
struct View {
  enum class Phase : uint8_t { Asking, Receiving, Ready } phase;
  uint32_t at, size;
  uint32_t msLeft;            // asking: until the question lapses
};

class Session {
 public:
  static constexpr uint32_t kConfirmMs = 30000;      // the button question
  static constexpr uint32_t kSilenceMs = 300000;     // a receiving session waits this long for the page to come back
  static constexpr uint32_t kReadyMs = 20000;        // restart by itself if REBOOT never comes
  static constexpr uint32_t kRebootDelayMs = 300;    // so the REBOOT reply leaves before the radio goes
  static constexpr uint32_t kAckBytes = 4096;
  static constexpr uint32_t kGapEveryMs = 200;

  // Replies and events go out on `link`: the Bluetooth link the page is on.
  Session(Slot&, Host&, blorb::Link& link, Config);

  // An `#<id> OTA ...` line is handled and true; any other line is false and
  // left for the protocol.
  bool handle(std::string_view line, uint32_t now);
  // One data write: a 4-byte little-endian offset, then image bytes.
  void data(const uint8_t* packet, size_t len, uint32_t now);
  void press(uint32_t now);                 // the dish's button
  void tick(uint32_t now);                  // lapses: the question, a silent page
  bool asking() const { return std::holds_alternative<Asking>(state_); }
  std::optional<View> view(uint32_t now) const;
  bool rebootDue(uint32_t now);             // ready, asked or lapsed, and not busy

 private:
  struct Idle { Last last = Last::None; };
  struct Asking { Image image; uint32_t since; };
  struct Receiving { Image image; uint32_t at, heard, gapSaid; Sha256 hash; };
  struct Ready { Image image; uint32_t since; std::optional<uint32_t> rebootAt; };
  using State = std::variant<Idle, Asking, Receiving, Ready>;

  void begin(blorb::Args&, blorb::Reply&, uint32_t now);
  void status(blorb::Reply&);
  void finish(Receiving&, uint32_t now);
  void fail(Last);
  const Image* current() const;
  void event(const char* fmt, ...);

  Slot& slot_;
  Host& host_;
  blorb::Link& link_;
  Config config_;
  State state_;
};

}  // namespace ota
