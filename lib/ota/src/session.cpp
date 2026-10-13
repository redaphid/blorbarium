#include "ota/session.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ota {
namespace {

constexpr const char* kLastNames[] = {"none",    "no_confirm", "busy",    "save_failed", "aborted",
                                      "timeout", "overflow",   "bad_hash", "bad_image",  "flash_error"};

int b64urlValue(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

// Unpadded base64url of exactly `n` bytes, or false.
bool unb64url(std::string_view s, uint8_t* out, size_t n) {
  if (s.size() != (n * 8 + 5) / 6) return false;
  uint32_t acc = 0;
  int bits = 0;
  size_t k = 0;
  for (char c : s) {
    const int v = b64urlValue(c);
    if (v < 0) return false;
    acc = acc << 6 | uint32_t(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (k < n) out[k++] = uint8_t(acc >> bits);
    }
  }
  return k == n && (acc & ((1u << bits) - 1)) == 0;   // leftover bits are zero in a canonical encoding
}

void b64url(const uint8_t* p, size_t n, char* out) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < n; ++i) {
    acc = acc << 8 | p[i];
    bits += 8;
    while (bits >= 6) *out++ = kAlphabet[(acc >> (bits -= 6)) & 63];
  }
  if (bits) *out++ = kAlphabet[(acc << (6 - bits)) & 63];
  *out = 0;
}

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
  const int n = std::snprintf(out, cap, "blorbota1 %s %u.%u.%u %u %u %s", im.board, unsigned(im.version.major),
                              unsigned(im.version.minor), unsigned(im.version.patch), unsigned(im.flags),
                              unsigned(im.size), sha);
  return n > 0 && size_t(n) < cap ? size_t(n) : 0;
}

uint32_t idOf(const Image& im) {
  return uint32_t(im.sha256[0]) << 24 | uint32_t(im.sha256[1]) << 16 | uint32_t(im.sha256[2]) << 8 | im.sha256[3];
}

}  // namespace

const char* lastName(Last l) { return kLastNames[size_t(l)]; }

std::optional<Version> parseVersion(std::string_view s) {
  uint32_t part[3] = {};
  size_t p = 0;
  bool digit = false;
  for (char c : s) {
    if (c == '.') {
      if (!digit || ++p > 2) return std::nullopt;
      digit = false;
    } else if (c >= '0' && c <= '9') {
      part[p] = part[p] * 10 + uint32_t(c - '0');
      if (part[p] > 0xffff) return std::nullopt;
      digit = true;
    } else {
      return std::nullopt;
    }
  }
  if (p != 2 || !digit) return std::nullopt;
  return Version{uint16_t(part[0]), uint16_t(part[1]), uint16_t(part[2])};
}

bool Image::operator==(const Image& o) const {
  return std::strcmp(board, o.board) == 0 && version == o.version && flags == o.flags && size == o.size &&
         std::memcmp(sha256, o.sha256, sizeof sha256) == 0 && std::memcmp(sig, o.sig, sizeof sig) == 0;
}

Session::Session(Slot& slot, Host& host, blorb::Link& link, Config config)
    : slot_(slot), host_(host), link_(link), config_(config) {}

bool Session::handle(std::string_view line, uint32_t now) {
  if (line.size() > 200) return false;   // the protocol answers 413
  std::string_view rest = line;
  const std::optional<uint32_t> id = hexId(rest);
  if (!id) return false;
  blorb::Args args{rest};
  if (args.word() != std::optional<std::string_view>("OTA")) return false;

  blorb::Reply reply(link_, *id);
  const std::optional<std::string_view> sub = args.word();
  if (sub == std::optional<std::string_view>("BEGIN")) {
    begin(args, reply, now);
  } else if (sub == std::optional<std::string_view>("STATUS")) {
    status(reply);
  } else if (sub == std::optional<std::string_view>("ABORT")) {
    if (std::holds_alternative<Receiving>(state_) || std::holds_alternative<Ready>(state_)) slot_.discard();
    if (!std::holds_alternative<Idle>(state_)) fail(Last::Aborted);
    reply.ok("state=idle");
  } else if (sub == std::optional<std::string_view>("REBOOT")) {
    if (Ready* r = std::get_if<Ready>(&state_)) {
      r->rebootAt = now + kRebootDelayMs;
      reply.ok("rebooting");
    } else {
      reply.err(409, "NOT_READY");
    }
  } else {
    reply.err(400, "BAD_ARGS");
  }
  return true;
}

void Session::begin(blorb::Args& args, blorb::Reply& reply, uint32_t now) {
  Image im;
  const std::optional<std::string_view> board = args.word(), version = args.word();
  const std::optional<uint32_t> flags = args.number(0xffff), size = args.number(0xffffffffu);
  const std::optional<std::string_view> sha = args.word(), sig = args.word();
  const std::optional<Version> v = version ? parseVersion(*version) : std::nullopt;
  if (!board || board->size() >= sizeof im.board || !v || !flags || !size || !sha || !sig || !args.done() ||
      !unb64url(*sha, im.sha256, sizeof im.sha256) || !unb64url(*sig, im.sig, sizeof im.sig))
    return reply.err(400, "BAD_ARGS");
  std::memcpy(im.board, board->data(), board->size());
  im.version = *v;
  im.flags = *flags;
  im.size = *size;

  // A session is open: its own BEGIN is the resume, anything else waits for ABORT.
  if (const Image* open = current()) {
    if (!(*open == im)) return reply.err(409, "OTA_ACTIVE");
    return status(reply);
  }

  char text[128];
  const size_t n = signedText(im, text, sizeof text);
  if (!n || !verifyP256(config_.publicKey, reinterpret_cast<const uint8_t*>(text), n, im.sig))
    return reply.err(403, "BAD_SIGNATURE");
  if (std::strcmp(im.board, config_.board) != 0) return reply.err(403, "WRONG_BOARD");
  if (!(config_.firmware < im.version) && !(im.flags & kAllowOlder)) return reply.err(409, "NOT_NEWER");
  if (im.size == 0 || im.size > slot_.capacity()) return reply.err(413, "TOO_BIG");
  if (host_.busy()) return reply.err(409, "BUSY");
  state_ = Asking{im, now};
  status(reply);
}

void Session::status(blorb::Reply& reply) {
  const unsigned chunk = host_.chunk();
  if (const Idle* idle = std::get_if<Idle>(&state_)) {
    const Version& fw = config_.firmware;
    return reply.ok("state=idle board=%s fw=%u.%u.%u chunk=%u trial=%d last=%s", config_.board, unsigned(fw.major),
                    unsigned(fw.minor), unsigned(fw.patch), chunk, host_.onTrial() ? 1 : 0, lastName(idle->last));
  }
  const char* name = asking() ? "confirm" : std::holds_alternative<Receiving>(state_) ? "receiving" : "ready";
  const Image& im = *current();
  const Receiving* r = std::get_if<Receiving>(&state_);
  const uint32_t at = r ? r->at : std::holds_alternative<Ready>(state_) ? im.size : 0;
  reply.ok("state=%s at=%u size=%u id=%08x chunk=%u", name, unsigned(at), unsigned(im.size), unsigned(idOf(im)), chunk);
}

void Session::press(uint32_t now) {
  Asking* a = std::get_if<Asking>(&state_);
  if (!a) return;
  const Image im = a->image;
  if (host_.busy()) return fail(Last::Busy);
  if (!host_.save()) return fail(Last::SaveFailed);
  if (!slot_.open(im.size)) return fail(Last::FlashError);
  state_ = Receiving{im, 0, now, now - kGapEveryMs, Sha256{}};
  event("state=receiving at=0");
}

void Session::data(const uint8_t* packet, size_t len, uint32_t now) {
  Receiving* r = std::get_if<Receiving>(&state_);
  if (!r || len < 5) return;
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
  else if (!slot_.activate()) why = Last::FlashError;
  if (why != Last::None) {
    slot_.discard();
    return fail(why);
  }
  state_ = Ready{r.image, now, std::nullopt};
  event("at=%u", unsigned(std::get<Ready>(state_).image.size));
  event("state=ready");
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
  state_ = Idle{why};
  event("state=idle last=%s", lastName(why));
}

const Image* Session::current() const {
  if (const Asking* a = std::get_if<Asking>(&state_)) return &a->image;
  if (const Receiving* r = std::get_if<Receiving>(&state_)) return &r->image;
  if (const Ready* r = std::get_if<Ready>(&state_)) return &r->image;
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
