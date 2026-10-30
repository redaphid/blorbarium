#include "ota/session.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ota {
namespace {

constexpr const char* kLastNames[] = {"none",     "no_confirm", "busy",      "save_failed", "aborted", "timeout",
                                      "overflow", "bad_hash",   "bad_image", "flash_error", "on_trial"};

std::optional<uint32_t> hexId(std::string_view& rest) {
  if (rest.size() < 2 || rest[0] != '#') return std::nullopt;
  uint32_t id = 0;
  size_t i = 1;
  for (; i < rest.size() && i <= 7; ++i) {
    const char c = rest[i];
    const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (v < 0) break;
    id = id << 4 | uint32_t(v);
  }
  if (i == 1 || i > 7 || i >= rest.size() || rest[i] != ' ') return std::nullopt;
  rest.remove_prefix(i);
  return id;
}

// What the signature covers (docs/ota.md "Signing"), rebuilt from the parsed
// fields so a page cannot make the dish verify one text and act on another.
size_t signedText(const Image& im, char* out, size_t cap) {
  char sha[44];
  b64url(im.sha256, sizeof im.sha256, sha);
  const int n = std::snprintf(out, cap, "blorbota2 %s %u.%u.%u %u %u %u %s", im.board, unsigned(im.version.major),
                              unsigned(im.version.minor), unsigned(im.version.patch), unsigned(im.flags),
                              unsigned(im.fmt), unsigned(im.size), sha);
  return n > 0 && size_t(n) < cap ? size_t(n) : 0;
}

uint32_t idOf(const Image& im) {
  return uint32_t(im.sha256[0]) << 24 | uint32_t(im.sha256[1]) << 16 | uint32_t(im.sha256[2]) << 8 | im.sha256[3];
}

bool is(const std::optional<std::string_view>& w, const char* s) { return w && *w == s; }

}  // namespace

const char* lastName(Last l) { return kLastNames[size_t(l)]; }

bool Image::operator==(const Image& o) const {
  return std::strcmp(board, o.board) == 0 && version == o.version && flags == o.flags && fmt == o.fmt &&
         size == o.size && std::memcmp(sha256, o.sha256, sizeof sha256) == 0 && std::memcmp(sig, o.sig, sizeof sig) == 0;
}

Session::Session(Slot& slot, Host& host, blorb::Link& link, Config config)
    : slot_(slot), host_(host), link_(link), config_(config), state_(Idle{}) {}

bool Session::handle(std::string_view line, uint32_t now, Peer peer) {
  if (line.size() > 200) return false;   // the protocol answers 413
  std::string_view rest = line;
  const std::optional<uint32_t> id = hexId(rest);
  if (!id) return false;
  blorb::Args args{rest};
  if (!is(args.word(), "OTA")) return false;

  blorb::Reply reply(link_, *id);
  const std::optional<std::string_view> sub = args.word();
  if (is(sub, "BEGIN")) return begin(args, reply, now, peer), true;
  if (is(sub, "STATUS")) return status(reply), true;

  // The rest name the page's token. A missing or malformed one is nobody's.
  const std::optional<std::string_view> word = args.word();
  const std::optional<Token> token = word ? parseToken(*word) : std::nullopt;
  if (is(sub, "CONFIRM")) {
    // The new image's half of the trial a dish cannot check alone: this page reached it over Bluetooth.
    if (!token) return reply.err(403, "NOT_YOURS"), true;
    switch (host_.confirmTrial(*token)) {
      case Confirm::NotOnTrial: return reply.ok("trial=0"), true;
      case Confirm::Confirmed: return reply.ok("trial=1"), true;
      case Confirm::NotYours: return reply.err(403, "NOT_YOURS"), true;
    }
  }
  if (!is(sub, "ABORT") && !is(sub, "REBOOT")) return reply.err(400, "BAD_ARGS"), true;
  Owner* o = owner();
  if (o && (!token || *token != o->token)) return reply.err(403, "NOT_YOURS"), true;
  if (is(sub, "ABORT")) return abort(), reply.ok("state=idle"), true;
  Ready* r = std::get_if<Ready>(&state_);
  if (!r) return reply.err(409, "NOT_READY"), true;
  r->rebootAt = now + kRebootDelayMs;
  reply.ok("rebooting");
  return true;
}

void Session::begin(blorb::Args& args, blorb::Reply& reply, uint32_t now, Peer peer) {
  Image im;
  const std::optional<std::string_view> board = args.word(), version = args.word();
  const std::optional<uint32_t> flags = args.number(0xffff), fmt = args.number(0xffff),
                                size = args.number(0xffffffffu);
  const std::optional<std::string_view> sha = args.word(), sig = args.word(), tok = args.word();
  const std::optional<Version> v = version ? parseVersion(*version) : std::nullopt;
  const std::optional<Token> token = tok ? parseToken(*tok) : std::nullopt;
  if (!board || board->size() >= sizeof im.board || !v || !flags || !fmt || !size || !sha || !sig || !token ||
      !args.done() || !unb64url(*sha, im.sha256, sizeof im.sha256) || !unb64url(*sig, im.sig, sizeof im.sig))
    return reply.err(400, "BAD_ARGS");
  std::memcpy(im.board, board->data(), board->size());
  im.version = *v;
  im.flags = *flags;
  im.fmt = *fmt;
  im.size = *size;

  // A session is open: its own image and token is the resume, and moves the
  // data to the connection that sent it. The image is public, so the same
  // image under another token is someone else's page.
  if (const Image* open = current()) {
    if (!(*open == im)) return reply.err(409, "OTA_ACTIVE");
    Owner* o = owner();
    if (o->token != *token) return reply.err(403, "NOT_YOURS");
    o->peer = peer;
    return status(reply);
  }

  if (const std::optional<Check> no = check(im, now, im.flags & kAllowOlder)) return reply.err(no->code, no->text);
  state_ = Asking{im, Owner{*token, peer}, now};
  status(reply);
}

std::optional<Session::Check> Session::check(const Image& im, uint32_t now, bool allowOlder) {
  // The cheap refusals first: the verify below stalls the loop for a moment.
  if (std::strcmp(im.board, config_.board) != 0) return Check{403, "WRONG_BOARD"};
  if (!(config_.firmware < im.version) && !allowOlder) return Check{409, "NOT_NEWER"};
  if (im.size == 0 || im.size > slot_.capacity()) return Check{413, "TOO_BIG"};
  // An update on trial would overwrite the only slot known to boot: there
  // is no factory image, so a failed trial after that has nowhere to go.
  if (host_.onTrial()) return Check{409, "TRIAL"};
  if (host_.busy()) return Check{409, "BUSY"};
  if (lastVerify_ && now - *lastVerify_ < kVerifyEveryMs) return Check{429, "TOO_SOON"};
  lastVerify_ = now;
  char text[128];
  const size_t n = signedText(im, text, sizeof text);
  if (!n || !verifyP256(config_.publicKey, reinterpret_cast<const uint8_t*>(text), n, im.sig))
    return Check{403, "BAD_SIGNATURE"};
  return std::nullopt;
}

std::optional<Refusal> Session::install(const Image& im, const Token& token, uint32_t now, Peer peer) {
  if (active()) return Refusal{"OTA_ACTIVE"};
  if (const std::optional<Check> no = check(im, now, false)) return Refusal{no->text};
  state_ = Asking{im, Owner{token, peer}, now};
  accept(now);
  if (std::holds_alternative<Receiving>(state_)) return std::nullopt;
  return Refusal{lastName(last())};
}

void Session::abort() {
  if (std::holds_alternative<Receiving>(state_) || std::holds_alternative<Ready>(state_)) slot_.discard();
  if (active()) fail(Last::Aborted);
}

bool Session::ownedBy(Peer peer) const {
  if (const Asking* a = std::get_if<Asking>(&state_)) return a->owner.peer == peer;
  if (const Receiving* r = std::get_if<Receiving>(&state_)) return r->owner.peer == peer;
  if (const Ready* r = std::get_if<Ready>(&state_)) return r->owner.peer == peer;
  return false;
}

Last Session::last() const {
  const Idle* idle = std::get_if<Idle>(&state_);
  return idle ? idle->last : Last::None;
}

void Session::status(blorb::Reply& reply) {
  const unsigned chunk = host_.chunk();
  if (const Idle* idle = std::get_if<Idle>(&state_)) {
    const Version& fw = config_.firmware;
    char rolled[20] = "none";
    if (const std::optional<Version> rb = host_.rolledBack())
      std::snprintf(rolled, sizeof rolled, "%u.%u.%u", unsigned(rb->major), unsigned(rb->minor), unsigned(rb->patch));
    return reply.ok("state=idle board=%s fw=%u.%u.%u chunk=%u trial=%d last=%s rolledback=%s", config_.board,
                    unsigned(fw.major), unsigned(fw.minor), unsigned(fw.patch), chunk, host_.onTrial() ? 1 : 0,
                    lastName(idle->last), rolled);
  }
  const char* name = asking() ? "confirm" : std::holds_alternative<Receiving>(state_) ? "receiving" : "ready";
  const Image& im = *current();
  const Receiving* r = std::get_if<Receiving>(&state_);
  const uint32_t at = r ? r->at : std::holds_alternative<Ready>(state_) ? im.size : 0;
  reply.ok("state=%s at=%u size=%u id=%08x chunk=%u", name, unsigned(at), unsigned(im.size), unsigned(idOf(im)), chunk);
}

void Session::press(uint32_t now) {
  const Asking* a = std::get_if<Asking>(&state_);
  if (!a || now - a->since < kMinAskMs) return;
  accept(now);
}

void Session::accept(uint32_t now) {
  const Asking& a = std::get<Asking>(state_);
  const Image im = a.image;
  const Owner owner = a.owner;
  if (host_.onTrial()) return fail(Last::OnTrial);
  if (host_.busy()) return fail(Last::Busy);
  if (!host_.save()) return fail(Last::SaveFailed);
  if (!slot_.open(im.size)) return fail(Last::FlashError);
  state_ = Receiving{im, owner, 0, now, now - kGapEveryMs, Sha256{}};
  event("state=receiving at=0");
}

void Session::data(const uint8_t* packet, size_t len, uint32_t now, Peer peer) {
  Receiving* r = std::get_if<Receiving>(&state_);
  if (!r || len < 5 || peer != r->owner.peer) return;   // bytes from any other connection never reach the slot
  const uint32_t offset = uint32_t(packet[0]) | uint32_t(packet[1]) << 8 | uint32_t(packet[2]) << 16 |
                          uint32_t(packet[3]) << 24;
  const uint8_t* bytes = packet + 4;
  const size_t n = len - 4;
  if (offset < r->at) return;   // a repeat of something already written
  if (offset > r->at) {         // one went missing: say where to go back to
    if (now - r->gapSaid >= kGapEveryMs) {
      r->gapSaid = now;
      event("at=%u gap", unsigned(r->at));
    }
    return;
  }
  if (n > r->image.size - r->at) {
    slot_.discard();
    return fail(Last::Overflow);
  }
  if (!slot_.write(bytes, n)) {
    slot_.discard();
    return fail(Last::FlashError);
  }
  r->hash.update(bytes, n);
  const uint32_t before = r->at;
  r->at += uint32_t(n);
  r->heard = now;
  if (r->at == r->image.size) return finish(*r, now);
  if (r->at / kAckBytes != before / kAckBytes) event("at=%u", unsigned(r->at));
}

void Session::finish(Receiving& r, uint32_t now) {
  uint8_t got[32];
  r.hash.finish(got);
  Last why = Last::None;
  if (std::memcmp(got, r.image.sha256, sizeof got) != 0) why = Last::BadHash;
  else if (!slot_.close()) why = Last::BadImage;
  // Before the switch: a power cut from here on boots the new image, which
  // must find this record to know which pet, version and page it was handed.
  else if (!host_.handOver(r.image.version, r.owner.token)) why = Last::FlashError;
  else if (!slot_.activate()) why = Last::FlashError;
  if (why != Last::None) {
    slot_.discard();
    return fail(why);
  }
  state_ = Ready{r.image, r.owner, now, std::nullopt};
  event("at=%u", unsigned(std::get<Ready>(state_).image.size));
  event("state=ready");
}

void Session::decline(uint32_t) {
  if (asking()) fail(Last::NoConfirm);
}

void Session::tick(uint32_t now) {
  if (const Asking* a = std::get_if<Asking>(&state_)) {
    if (now - a->since >= kConfirmMs) fail(Last::NoConfirm);
  } else if (const Receiving* r = std::get_if<Receiving>(&state_)) {
    if (now - r->heard >= kSilenceMs) {
      slot_.discard();
      fail(Last::Timeout);
    }
  }
}

bool Session::rebootDue(uint32_t now) {
  const Ready* r = std::get_if<Ready>(&state_);
  if (!r) return false;
  const bool due = r->rebootAt ? int32_t(now - *r->rebootAt) >= 0 : now - r->since >= kReadyMs;
  return due && !host_.busy();
}

bool Session::prepareRestart() {
  const Ready* r = std::get_if<Ready>(&state_);
  if (r && (host_.save() || host_.save())) {
    host_.handOver(r->image.version, r->owner.token);   // again: the pet may have moved on since ready, and a stale record only rolls back
    return true;
  }
  slot_.discard();
  fail(Last::SaveFailed);
  return false;
}

std::optional<View> Session::view(uint32_t now) const {
  if (const Asking* a = std::get_if<Asking>(&state_)) {
    const uint32_t gone = now - a->since;
    return View{View::Phase::Asking, 0, a->image.size, gone < kConfirmMs ? kConfirmMs - gone : 0};
  }
  if (const Receiving* r = std::get_if<Receiving>(&state_)) return View{View::Phase::Receiving, r->at, r->image.size, 0};
  if (const Ready* r = std::get_if<Ready>(&state_)) return View{View::Phase::Ready, r->image.size, r->image.size, 0};
  return std::nullopt;
}

void Session::fail(Last why) {
  // Leaving receiving or ready without a restart: a record left behind would
  // read, at the next boot, as an update that rolled back.
  if (std::holds_alternative<Receiving>(state_) || std::holds_alternative<Ready>(state_)) host_.forgetHandover();
  state_ = Idle{why};
  event("state=idle last=%s", lastName(why));
}

const Image* Session::current() const {
  if (const Asking* a = std::get_if<Asking>(&state_)) return &a->image;
  if (const Receiving* r = std::get_if<Receiving>(&state_)) return &r->image;
  if (const Ready* r = std::get_if<Ready>(&state_)) return &r->image;
  return nullptr;
}

Session::Owner* Session::owner() {
  if (Asking* a = std::get_if<Asking>(&state_)) return &a->owner;
  if (Receiving* r = std::get_if<Receiving>(&state_)) return &r->owner;
  if (Ready* r = std::get_if<Ready>(&state_)) return &r->owner;
  return nullptr;
}

void Session::event(const char* fmt, ...) {
  char body[120];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(body, sizeof body, fmt, ap);
  va_end(ap);
  char line[140];
  const int n = std::snprintf(line, sizeof line, "! OTA %s", body);
  if (n > 0) link_.writeLine(std::string_view(line, size_t(n)));
}

}  // namespace ota
