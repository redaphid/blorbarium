#pragma once
// One firmware update, from the page's signed BEGIN to the restart. The
// protocol is docs/ota.md. Free of Arduino and NimBLE: the flash slot and the
// rest of the dish come in through Slot and Host, so the native suite drives
// every path with a slot in memory.
//
//   idle --BEGIN--> asking --button--> receiving --last byte--> ready --REBOOT--> restart
//   idle --install()---------------> receiving    (Wi-Fi, unattended: docs/wifi-ota.md)
//
// A disconnect is not an event here. The session waits, and the page resumes
// by sending the same BEGIN with the same token, which answers where the
// session stands.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include "blorb/protocol.h"
#include "blorb/seams.h"
#include "ota/crypto.h"
#include "ota/types.h"

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
  virtual bool busy() = 0;                                // the pet is doing something an update must not interrupt
  virtual bool save() = 0;                                // write the pet now; false when it did not land
  virtual uint16_t chunk() = 0;                           // image bytes one data write can carry on this link
  virtual bool onTrial() = 0;                             // this image has not yet kept itself (docs/ota.md "The trial")
  virtual bool handOver(Version to, const Token&) = 0;    // record which pet, version and page the new image must find
  virtual void forgetHandover() = 0;                      // the update ended here: no new image will look for it
  virtual Confirm confirmTrial(const Token&) = 0;         // a page reached this image over Bluetooth
  virtual std::optional<Version> rolledBack() = 0;        // the last update that switched and then rolled back
};

// Everything BEGIN carries. All of it but the signature is signed.
struct Image {
  char board[16] = "";
  Version version;
  uint32_t flags = 0, fmt = 0, size = 0;
  uint8_t sha256[32] = {};
  uint8_t sig[64] = {};
  bool operator==(const Image&) const;
};
constexpr uint32_t kAllowOlder = 1;

// How the last session ended, as STATUS's `last=` says it.
enum class Last : uint8_t {
  None, NoConfirm, Busy, SaveFailed, Aborted, Timeout, Overflow, BadHash, BadImage, FlashError, OnTrial
};
const char* lastName(Last);

struct Config {
  const char* board;          // this dish's board, as images name it
  Version firmware;           // what is running
  const uint8_t* publicKey;   // 65 bytes, uncompressed P-256
};

// Why install() did not start receiving: BEGIN's refusal text (docs/ota.md),
// or, past the checks, how the session ended (lastName).
struct Refusal {
  const char* why;
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
  static constexpr uint32_t kMinAskMs = 1500;        // a press sooner than this was already under way: not a yes
  static constexpr uint32_t kSilenceMs = 300000;     // a receiving session waits this long for the page to come back
  static constexpr uint32_t kReadyMs = 20000;        // restart by itself if REBOOT never comes
  static constexpr uint32_t kRebootDelayMs = 300;    // so the REBOOT reply leaves before the radio goes
  static constexpr uint32_t kAckBytes = 4096;
  static constexpr uint32_t kGapEveryMs = 200;
  static constexpr uint32_t kVerifyEveryMs = 2000;   // a P-256 verify stalls the loop; a flood must not

  // Replies and events go out on `link`: the Bluetooth link the page is on.
  Session(Slot&, Host&, blorb::Link& link, Config);

  // An `#<id> OTA ...` line from connection `peer` is handled and true; any
  // other line is false and left for the protocol.
  bool handle(std::string_view line, uint32_t now, Peer peer);
  // One data write: a 4-byte little-endian offset, then image bytes.
  void data(const uint8_t* packet, size_t len, uint32_t now, Peer peer);
  void press(uint32_t now);                 // the dish's button
  void decline(uint32_t now);               // a no (a hard shake, #35): the question lapses at once, as unanswered
  // The unattended update (Wi-Fi, docs/wifi-ota.md): BEGIN's checks and the
  // same signature check, then what a press does (save, open the slot), with
  // no question. Only a strictly newer image: a downgrade, signed or not, needs
  // a hand on the badge. Data then comes through data() from `peer`. Nothing
  // when it is receiving.
  std::optional<Refusal> install(const Image&, const Token&, uint32_t now, Peer peer);
  void abort();                             // as OTA ABORT by the owner: the slot is discarded
  void tick(uint32_t now);                  // lapses: the question, a silent page
  bool asking() const { return std::holds_alternative<Asking>(state_); }
  bool active() const { return !std::holds_alternative<Idle>(state_); }   // anything else that wants the radio waits
  bool ready() const { return std::holds_alternative<Ready>(state_); }
  bool ownedBy(Peer peer) const;            // an open session whose data comes from `peer`
  Last last() const;                        // how the last session ended; None while one is open
  std::optional<View> view(uint32_t now) const;
  bool rebootDue(uint32_t now);             // ready, asked or lapsed, and not busy
  // Just before the restart: save (twice if need be) and hand over. False,
  // with the update discarded, when the pet could not be saved.
  bool prepareRestart();

 private:
  // A session belongs to the page's token; its data also to the connection
  // that last presented it.
  struct Owner { Token token; Peer peer; };
  struct Idle { Last last = Last::None; };
  struct Asking { Image image; Owner owner; uint32_t since; };
  struct Receiving { Image image; Owner owner; uint32_t at, heard, gapSaid; Sha256 hash; };
  struct Ready { Image image; Owner owner; uint32_t since; std::optional<uint32_t> rebootAt; };
  using State = std::variant<Idle, Asking, Receiving, Ready>;

  void begin(blorb::Args&, blorb::Reply&, uint32_t now, Peer peer);
  // BEGIN's checks, cheap first, then the signature: the HTTP-ish code and text of the first that fails.
  struct Check { int code; const char* text; };
  std::optional<Check> check(const Image&, uint32_t now, bool allowOlder);
  void accept(uint32_t now);                // the yes: save, open the slot, receive
  void status(blorb::Reply&);
  void finish(Receiving&, uint32_t now);
  void fail(Last);
  const Image* current() const;
  Owner* owner();
  void event(const char* fmt, ...);

  Slot& slot_;
  Host& host_;
  blorb::Link& link_;
  Config config_;
  State state_;
  std::optional<uint32_t> lastVerify_;
};

}  // namespace ota
