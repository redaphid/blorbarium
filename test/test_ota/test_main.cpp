#include <gtest/gtest.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <string>
#include <vector>

#include "blorb/dish.h"
#include "links.h"
#include "mem_storage.h"
#include "ota/held_storage.h"
#include "ota/trial_guard.h"
#include "ota_fixture.h"
#include "ota/screen.h"
#include "ota/session.h"
#include "ota/trial.h"

using namespace ota;
using blorbtest::ScriptedLink;
using Bytes = std::vector<uint8_t>;
using Lines = std::vector<std::string>;

namespace {

// A P-256 key made fresh for the run: the tests sign their own images.
struct Signer {
  EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
  uint8_t pub[65] = {};
  Signer() {
    size_t n = 0;
    EVP_PKEY_get_octet_string_param(key, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, pub, sizeof pub, &n);
  }
  ~Signer() { EVP_PKEY_free(key); }
  Bytes sign(const std::string& text) const {
    EVP_MD_CTX* md = EVP_MD_CTX_new();
    size_t len = 0;
    EVP_DigestSignInit(md, nullptr, EVP_sha256(), nullptr, key);
    EVP_DigestSign(md, nullptr, &len, reinterpret_cast<const uint8_t*>(text.data()), text.size());
    Bytes der(len);
    EVP_DigestSign(md, der.data(), &len, reinterpret_cast<const uint8_t*>(text.data()), text.size());
    EVP_MD_CTX_free(md);
    const uint8_t* p = der.data();
    ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, long(len));
    Bytes raw(64);
    BN_bn2binpad(ECDSA_SIG_get0_r(sig), raw.data(), 32);
    BN_bn2binpad(ECDSA_SIG_get0_s(sig), raw.data() + 32, 32);
    ECDSA_SIG_free(sig);
    return raw;
  }
};

std::string b64url(const Bytes& b) {
  static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  uint32_t acc = 0;
  int bits = 0;
  for (uint8_t c : b) {
    acc = acc << 8 | c;
    bits += 8;
    while (bits >= 6) out += k[(acc >> (bits -= 6)) & 63];
  }
  if (bits) out += k[(acc << (6 - bits)) & 63];
  return out;
}

Bytes sha256(const Bytes& b) {
  Sha256 h;
  h.update(b.data(), b.size());
  Bytes out(32);
  h.finish(out.data());
  return out;
}

Bytes imageBytes(size_t n, uint32_t seed = 1) {
  Bytes b(n);
  for (auto& c : b) c = uint8_t((seed = seed * 1103515245u + 12345u) >> 16);
  return b;
}

// Two pages' tokens: 16 bytes each, as base64url.
const std::string kTokenA = b64url(Bytes{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
const std::string kTokenB = b64url(Bytes{9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9});
Token tokenOf(const std::string& t) { return *parseToken(t); }

// A release as tools/ota_release.py makes one: the BEGIN arguments for `bytes`.
struct Release {
  std::string board = "lcd128", version = "0.5.0";
  uint32_t flags = 0;
  Bytes bytes = imageBytes(10000);
  uint32_t fmt = 1;
  std::string token = kTokenA;
  std::string text() const {
    return "blorbota2 " + board + " " + version + " " + std::to_string(flags) + " " + std::to_string(fmt) + " " +
           std::to_string(bytes.size()) +
           " " + b64url(sha256(bytes));
  }
  // ECDSA signatures are randomised; a page resends the manifest's one.
  mutable std::string signedText;
  mutable Bytes sig;
  std::string begin(const Signer& s, uint32_t id = 1) const {
    if (signedText != text()) {
      signedText = text();
      sig = s.sign(signedText);
    }
    char hex[8];
    std::snprintf(hex, sizeof hex, "%x", unsigned(id));
    return std::string("#") + hex + " OTA BEGIN " + board + " " + version + " " + std::to_string(flags) + " " +
           std::to_string(fmt) + " " +
           std::to_string(bytes.size()) + " " + b64url(sha256(bytes)) + " " + b64url(sig) + " " + token;
  }
  std::string idHex() const {
    const Bytes h = sha256(bytes);
    char out[9];
    std::snprintf(out, sizeof out, "%02x%02x%02x%02x", h[0], h[1], h[2], h[3]);
    return out;
  }
};

// The signature is BEGIN's second-to-last field; the page's token is the last.
std::string sigOf(const std::string& line) {
  const size_t end = line.rfind(' '), start = line.rfind(' ', end - 1) + 1;
  return line.substr(start, end - start);
}
std::string withSig(const std::string& line, const std::string& sig) {
  const size_t end = line.rfind(' '), start = line.rfind(' ', end - 1) + 1;
  return line.substr(0, start) + sig + line.substr(end);
}
std::string tamperSig(const std::string& line) {
  std::string sig = sigOf(line);
  sig.back() = sig.back() == 'A' ? 'Q' : 'A';
  return withSig(line, sig);
}

struct MemSlot : Slot {
  Bytes written;
  bool opened = false, closeOk = true, failOpen = false, failWrite = false, activated = false, discarded = false;
  int activations = 0;   // never reset, so a discard cannot hide an activate
  uint32_t cap = 65536;
  std::vector<std::string>* order = nullptr;
  uint32_t capacity() override { return cap; }
  bool open(uint32_t) override {
    if (order) order->push_back("open");
    if (failOpen) return false;
    opened = true;
    written.clear();
    return true;
  }
  bool write(const uint8_t* p, size_t n) override {
    if (failWrite) return false;
    written.insert(written.end(), p, p + n);
    return true;
  }
  bool close() override { return closeOk; }
  bool activateOk = true;
  bool activate() override {
    if (order) order->push_back("activate");
    ++activations;
    return activated = activateOk;
  }
  void discard() override { discarded = true; activated = false; }
};

struct FakeHost : Host {
  bool isBusy = false, saveOk = true, trial = false, handOverOk = true;
  int saves = 0, handOvers = 0, forgets = 0, saveFailures = 0;   // saveFailures: that many saves fail before saveOk applies
  std::optional<Token> handedToken, confirmedWith;
  std::optional<Version> handedTo, rolled;
  Confirm confirmAnswer = Confirm::Confirmed;
  std::vector<std::string>* order = nullptr;
  bool busy() override { return isBusy; }
  bool save() override {
    if (order) order->push_back("save");
    ++saves;
    if (saveFailures > 0) return --saveFailures, false;
    return saveOk;
  }
  uint16_t chunk() override { return 240; }
  bool onTrial() override { return trial; }
  bool handOver(Version to, const Token& t) override {
    if (order) order->push_back("handover");
    ++handOvers;
    handedTo = to;
    handedToken = t;
    return handOverOk;
  }
  void forgetHandover() override { ++forgets; }
  Confirm confirmTrial(const Token& t) override {
    if (!trial) return Confirm::NotOnTrial;
    confirmedWith = t;
    return confirmAnswer;
  }
  std::optional<Version> rolledBack() override { return rolled; }
};

struct Rig {
  Signer signer;
  MemSlot slot;
  FakeHost host;
  ScriptedLink link;
  Session session{slot, host, link, Config{"lcd128", Version{0, 4, 0}, signer.pub}};
  uint32_t now = 1000;
  Peer peer = 1;

  // Each BEGIN a test sends comes after the verify rate limit; TooSoon holds the limit itself.
  Lines send(const std::string& line) {
    if (line.find(" OTA BEGIN ") != std::string::npos) now += Session::kVerifyEveryMs;
    return sendNow(line);
  }
  Lines sendNow(const std::string& line, std::optional<Peer> from = std::nullopt) {
    link.sent.clear();
    EXPECT_TRUE(session.handle(line, now, from.value_or(peer))) << line;
    return link.sent;
  }
  std::string last() { return link.sent.empty() ? "" : link.sent.back(); }
  Lines packet(const Bytes& b, uint32_t offset, size_t n, std::optional<Peer> from = std::nullopt) {
    Bytes p{uint8_t(offset), uint8_t(offset >> 8), uint8_t(offset >> 16), uint8_t(offset >> 24)};
    p.insert(p.end(), b.begin() + offset, b.begin() + offset + n);
    link.sent.clear();
    session.data(p.data(), p.size(), now, from.value_or(peer));
    return link.sent;
  }
  void pressWhenAllowed() {
    now += Session::kMinAskMs;
    session.press(now);
  }
  // Sends [from, to) in 240-byte writes, as the page does.
  void stream(const Bytes& b, uint32_t from, uint32_t to) {
    for (uint32_t at = from; at < to; at += 240) {
      packet(b, at, std::min<size_t>(240, to - at));
      now += 2;
    }
  }
  void confirm(const Release& r) {
    ASSERT_EQ(send(r.begin(signer)).at(0).substr(0, 18), "#1 OK state=confir");
    pressWhenAllowed();
  }
};

}  // namespace

// ---- the whole path ------------------------------------------------------------------

TEST(OtaSession, SignedImageAsksThenReceivesThenIsReady) {
  Rig rig;
  Release r;
  EXPECT_EQ(rig.send(r.begin(rig.signer)),
            (Lines{"#1 OK state=confirm at=0 size=10000 id=" + r.idHex() + " chunk=240"}));
  EXPECT_TRUE(rig.session.asking());
  EXPECT_FALSE(rig.slot.opened) << "nothing is written before the button";

  rig.link.sent.clear();
  rig.pressWhenAllowed();
  EXPECT_EQ(rig.link.sent, (Lines{"! OTA state=receiving at=0"}));
  EXPECT_TRUE(rig.slot.opened);

  rig.stream(r.bytes, 0, 10000);
  EXPECT_EQ(rig.slot.written, r.bytes);
  EXPECT_TRUE(rig.slot.activated);
  EXPECT_EQ(rig.last(), "! OTA state=ready");
  EXPECT_EQ(rig.send("#2 OTA STATUS"),
            (Lines{"#2 OK state=ready at=10000 size=10000 id=" + r.idHex() + " chunk=240"}));

  EXPECT_EQ(rig.send("#3 OTA REBOOT " + kTokenA), (Lines{"#3 OK rebooting"}));
  EXPECT_FALSE(rig.session.rebootDue(rig.now + Session::kRebootDelayMs - 1)) << "the reply leaves first";
  EXPECT_TRUE(rig.session.rebootDue(rig.now + Session::kRebootDelayMs));
}

TEST(OtaSession, AcksEvery4096BytesWritten) {
  Rig rig;
  Release r;
  rig.confirm(r);
  Lines acks;
  for (uint32_t at = 0; at < 9000; at += 240) {
    for (const std::string& l : rig.packet(r.bytes, at, 240)) acks.push_back(l);
  }
  EXPECT_EQ(acks, (Lines{"! OTA at=4320", "! OTA at=8400"}));
}

// ---- refusals at BEGIN ---------------------------------------------------------------

TEST(OtaSession, RefusesAnImageForAnotherBoard) {
  Rig rig;
  Release r;
  r.board = "lcd146";
  EXPECT_EQ(rig.send(r.begin(rig.signer)), (Lines{"#1 ERR 403 WRONG_BOARD"}));
  EXPECT_FALSE(rig.session.asking());
}

TEST(OtaSession, RefusesABadSignature) {
  Rig rig;
  Release r;
  EXPECT_EQ(rig.send(tamperSig(r.begin(rig.signer))), (Lines{"#1 ERR 403 BAD_SIGNATURE"}));
  EXPECT_FALSE(rig.session.asking());
}

TEST(OtaSession, RefusesAnImageSignedByAnotherKey) {
  Rig rig;
  Signer stranger;
  EXPECT_EQ(rig.send(Release{}.begin(stranger)), (Lines{"#1 ERR 403 BAD_SIGNATURE"}));
}

TEST(OtaSession, RefusesSignedFieldsThatWereChanged) {
  Rig rig;
  Release r;
  const std::string sig = sigOf(r.begin(rig.signer));
  Release bigger = r;
  bigger.bytes = imageBytes(20000);
  // The size and hash of another image under this image's signature.
  EXPECT_EQ(rig.send(withSig(bigger.begin(rig.signer), sig)), (Lines{"#1 ERR 403 BAD_SIGNATURE"}));
  Release older = r;
  older.flags = kAllowOlder;
  EXPECT_EQ(rig.send(withSig(older.begin(rig.signer), sig)), (Lines{"#1 ERR 403 BAD_SIGNATURE"}))
      << "the downgrade flag is signed";
  Release otherFormat = r;
  otherFormat.fmt = 2;
  EXPECT_EQ(rig.send(withSig(otherFormat.begin(rig.signer), sig)), (Lines{"#1 ERR 403 BAD_SIGNATURE"}))
      << "the save format is signed";
}

TEST(OtaSession, RefusesAnImageThatIsNotNewerUnlessTheSignedFlagAllowsIt) {
  Rig rig;
  Release same;
  same.version = "0.4.0";
  EXPECT_EQ(rig.send(same.begin(rig.signer)), (Lines{"#1 ERR 409 NOT_NEWER"}));
  Release older;
  older.version = "0.3.9";
  EXPECT_EQ(rig.send(older.begin(rig.signer)), (Lines{"#1 ERR 409 NOT_NEWER"}));
  older.flags = kAllowOlder;
  EXPECT_EQ(rig.send(older.begin(rig.signer)).at(0).substr(0, 21), "#1 OK state=confirm a");
}

TEST(OtaSession, RefusesAnImageBiggerThanTheSlot) {
  Rig rig;
  rig.slot.cap = 9999;
  EXPECT_EQ(rig.send(Release{}.begin(rig.signer)), (Lines{"#1 ERR 413 TOO_BIG"}));
}

TEST(OtaSession, RefusesMalformedArguments) {
  Rig rig;
  EXPECT_EQ(rig.send("#1 OTA BEGIN lcd128 0.5 0 10 AAAA BBBB"), (Lines{"#1 ERR 400 BAD_ARGS"}));
  const std::string good = Release{}.begin(rig.signer);
  EXPECT_EQ(rig.send(good + " extra"), (Lines{"#1 ERR 400 BAD_ARGS"}));
  EXPECT_EQ(rig.send("#1 OTA FLY"), (Lines{"#1 ERR 400 BAD_ARGS"}));
}

TEST(OtaSession, LeavesOtherLinesToTheProtocol) {
  Rig rig;
  EXPECT_FALSE(rig.session.handle("#1 HELLO", rig.now, rig.peer));
  EXPECT_FALSE(rig.session.handle("OTA STATUS", rig.now, rig.peer)) << "no id: the protocol says BAD_ID";
  EXPECT_FALSE(rig.session.handle("#zz OTA STATUS", rig.now, rig.peer));
  EXPECT_TRUE(rig.link.sent.empty());
}

TEST(OtaSession, StatusWhenIdleSaysWhatAnUpdateNeeds) {
  Rig rig;
  EXPECT_EQ(rig.send("#7 OTA STATUS"), (Lines{"#7 OK state=idle board=lcd128 fw=0.4.0 chunk=240 trial=0 last=none rolledback=none"}));
  rig.host.trial = true;
  EXPECT_EQ(rig.send("#7 OTA STATUS"), (Lines{"#7 OK state=idle board=lcd128 fw=0.4.0 chunk=240 trial=1 last=none rolledback=none"}));
}

// ---- the button ----------------------------------------------------------------------

TEST(OtaSession, NoPressWithinThirtySecondsEndsIt) {
  Rig rig;
  rig.send(Release{}.begin(rig.signer));
  rig.session.tick(rig.now + Session::kConfirmMs - 1);
  EXPECT_TRUE(rig.session.asking());
  rig.link.sent.clear();
  rig.session.tick(rig.now + Session::kConfirmMs);
  EXPECT_FALSE(rig.session.asking());
  EXPECT_EQ(rig.link.sent, (Lines{"! OTA state=idle last=no_confirm"}));
  rig.session.press(rig.now + Session::kConfirmMs + 10);
  EXPECT_FALSE(rig.slot.opened) << "a late press starts nothing";
  EXPECT_EQ(rig.host.saves, 0);
}

// A hard shake is a no (#35): the question lapses at once, as one left
// unanswered, and nothing is written.
TEST(OtaSession, ADeclineEndsTheQuestionAsUnanswered) {
  Rig rig;
  rig.send(Release{}.begin(rig.signer));
  ASSERT_TRUE(rig.session.asking());
  rig.link.sent.clear();
  rig.session.decline(rig.now + 2000);
  EXPECT_FALSE(rig.session.asking());
  EXPECT_EQ(rig.link.sent, (Lines{"! OTA state=idle last=no_confirm"}));
  rig.session.press(rig.now + 2100);
  EXPECT_FALSE(rig.slot.opened) << "a press after the no starts nothing";
  rig.session.decline(rig.now + 2200);   // nothing asked: nothing to say
}

TEST(OtaSession, DataBeforeThePressIsIgnored) {
  Rig rig;
  Release r;
  rig.send(r.begin(rig.signer));
  rig.packet(r.bytes, 0, 240);
  EXPECT_TRUE(rig.slot.written.empty());
}

TEST(OtaSession, SavesThePetBeforeOpeningTheSlot) {
  Rig rig;
  std::vector<std::string> order;
  rig.slot.order = rig.host.order = &order;
  rig.confirm(Release{});
  EXPECT_EQ(order, (std::vector<std::string>{"save", "open"}));
}

TEST(OtaSession, AFailedSaveStartsNothing) {
  Rig rig;
  rig.host.saveOk = false;
  rig.confirm(Release{});
  EXPECT_FALSE(rig.slot.opened);
  EXPECT_EQ(rig.send("#2 OTA STATUS").at(0), "#2 OK state=idle board=lcd128 fw=0.4.0 chunk=240 trial=0 last=save_failed rolledback=none");
}

// ---- busy ----------------------------------------------------------------------------

TEST(OtaSession, RefusedWhileBusy) {
  Rig rig;
  rig.host.isBusy = true;
  EXPECT_EQ(rig.send(Release{}.begin(rig.signer)), (Lines{"#1 ERR 409 BUSY"}));
}

TEST(OtaSession, BusyAtThePressEndsIt) {
  Rig rig;
  rig.send(Release{}.begin(rig.signer));
  rig.host.isBusy = true;
  rig.pressWhenAllowed();
  EXPECT_FALSE(rig.slot.opened);
  EXPECT_EQ(rig.host.saves, 0);
  EXPECT_EQ(rig.last(), "! OTA state=idle last=busy");
}

TEST(OtaSession, TheRestartWaitsWhileBusy) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 10000);
  rig.send("#3 OTA REBOOT " + kTokenA);
  rig.host.isBusy = true;
  EXPECT_FALSE(rig.session.rebootDue(rig.now + 5000));
  rig.host.isBusy = false;
  EXPECT_TRUE(rig.session.rebootDue(rig.now + 5000));
}

// ---- bad bytes -----------------------------------------------------------------------

TEST(OtaSession, ACorruptedImageIsNeverActivated) {
  Rig rig;
  Release r;
  rig.confirm(r);
  Bytes bad = r.bytes;
  bad[5000] ^= 1;
  rig.stream(bad, 0, 10000);
  EXPECT_EQ(rig.slot.activations, 0) << "never switched to, not even for a moment";
  EXPECT_EQ(rig.host.handOvers, 0);
  EXPECT_TRUE(rig.slot.discarded);
  EXPECT_EQ(rig.last(), "! OTA state=idle last=bad_hash");
}

TEST(OtaSession, AnImageTheSlotRejectsIsNeverActivated) {
  Rig rig;
  Release r;
  rig.slot.closeOk = false;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 10000);
  EXPECT_FALSE(rig.slot.activated);
  EXPECT_EQ(rig.last(), "! OTA state=idle last=bad_image");
}

TEST(OtaSession, MoreBytesThanSignedIsAnOverflow) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 9840);
  Bytes longer = r.bytes;
  longer.resize(10240, 0);
  rig.packet(longer, 9840, 400);
  EXPECT_FALSE(rig.slot.activated);
  EXPECT_TRUE(rig.slot.discarded) << "the half-written slot is let go";
  EXPECT_EQ(rig.last(), "! OTA state=idle last=overflow");
}

TEST(OtaSession, ATruncatedImageTimesOutAndIsDiscarded) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 9600);
  const uint32_t lastHeard = rig.now - 2;
  rig.session.tick(lastHeard + Session::kSilenceMs - 1);
  EXPECT_EQ(rig.send("#2 OTA STATUS").at(0).substr(0, 30), "#2 OK state=receiving at=9600 ");
  rig.link.sent.clear();
  rig.session.tick(lastHeard + Session::kSilenceMs);
  EXPECT_EQ(rig.link.sent, (Lines{"! OTA state=idle last=timeout"}));
  EXPECT_TRUE(rig.slot.discarded);
  EXPECT_FALSE(rig.slot.activated);
}

TEST(OtaSession, AFlashWriteErrorEndsIt) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.slot.failWrite = true;
  rig.packet(r.bytes, 0, 240);
  EXPECT_EQ(rig.last(), "! OTA state=idle last=flash_error");
  EXPECT_TRUE(rig.slot.discarded);
}

// ---- duplicates, order, loss ---------------------------------------------------------

TEST(OtaSession, ARepeatedWriteIsWrittenOnce) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.packet(r.bytes, 0, 240);
  rig.packet(r.bytes, 0, 240);
  rig.packet(r.bytes, 240, 240);
  rig.packet(r.bytes, 0, 240);
  rig.stream(r.bytes, 480, 10000);
  EXPECT_EQ(rig.slot.written, r.bytes);
  EXPECT_TRUE(rig.slot.activated);
}

TEST(OtaSession, AWriteAheadOfAMissingOneIsDroppedAndSaysWhereToGoBack) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.packet(r.bytes, 0, 240);
  EXPECT_EQ(rig.packet(r.bytes, 480, 240), (Lines{"! OTA at=240 gap"})) << "240..480 never arrived";
  EXPECT_EQ(rig.packet(r.bytes, 720, 240), (Lines{})) << "at most one gap event every 200 ms";
  rig.now += Session::kGapEveryMs;
  EXPECT_EQ(rig.packet(r.bytes, 720, 240), (Lines{"! OTA at=240 gap"}));
  EXPECT_EQ(rig.slot.written.size(), 240u);
  rig.stream(r.bytes, 240, 10000);
  EXPECT_EQ(rig.slot.written, r.bytes);
  EXPECT_TRUE(rig.slot.activated);
}

TEST(OtaSession, OutOfOrderWritesNeverLandOutOfPlace) {
  Rig rig;
  Release r;
  rig.confirm(r);
  for (uint32_t at : {240u, 0u, 720u, 480u, 240u, 480u, 720u}) rig.packet(r.bytes, at, 240);
  const Bytes want(r.bytes.begin(), r.bytes.begin() + 960);
  EXPECT_EQ(rig.slot.written.size(), 960u);
  EXPECT_EQ(rig.slot.written, want);
}

// ---- dropped connections -------------------------------------------------------------

TEST(OtaSession, TheSameBeginAfterADropResumesWhereItStopped) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 4800);
  // The connection drops: nothing reaches the session. The page comes back.
  rig.now += 60000;
  EXPECT_EQ(rig.send(r.begin(rig.signer, 0x2a)),
            (Lines{"#2a OK state=receiving at=4800 size=10000 id=" + r.idHex() + " chunk=240"}));
  EXPECT_FALSE(rig.session.asking()) << "a resume does not ask again";
  rig.stream(r.bytes, 4800, 10000);
  EXPECT_EQ(rig.slot.written, r.bytes);
  EXPECT_TRUE(rig.slot.activated);
}

TEST(OtaSession, ADifferentImageWaitsForAbort) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 2400);
  Release other;
  other.bytes = imageBytes(10000, 99);
  EXPECT_EQ(rig.send(other.begin(rig.signer)), (Lines{"#1 ERR 409 OTA_ACTIVE"}));
  EXPECT_EQ(rig.send("#2 OTA ABORT " + kTokenA), (Lines{"! OTA state=idle last=aborted", "#2 OK state=idle"}));
  EXPECT_TRUE(rig.slot.discarded);
  EXPECT_EQ(rig.send("#3 OTA ABORT " + kTokenA), (Lines{"#3 OK state=idle"})) << "ABORT is safe to repeat";
  EXPECT_EQ(rig.send(other.begin(rig.signer)).at(0).substr(0, 21), "#1 OK state=confirm a");
}

TEST(OtaSession, ALostReadyEventIsAnsweredByStatusAndBegin) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 10000);   // "! OTA state=ready" went out and was lost
  const std::string ready = "OK state=ready at=10000 size=10000 id=" + r.idHex() + " chunk=240";
  EXPECT_EQ(rig.send("#4 OTA STATUS"), (Lines{"#4 " + ready}));
  EXPECT_EQ(rig.send("#4 OTA STATUS"), (Lines{"#4 " + ready})) << "STATUS changes nothing";
  EXPECT_EQ(rig.send(r.begin(rig.signer, 5)), (Lines{"#5 " + ready}));
}

TEST(OtaSession, ReadyRestartsByItselfWhenRebootNeverComes) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 10000);
  const uint32_t readyAt = rig.now - 2;   // stream() steps the clock past its last write
  EXPECT_FALSE(rig.session.rebootDue(readyAt + Session::kReadyMs - 1));
  EXPECT_TRUE(rig.session.rebootDue(readyAt + Session::kReadyMs));
}

TEST(OtaSession, RebootBeforeReadyIsRefused) {
  Rig rig;
  EXPECT_EQ(rig.send("#3 OTA REBOOT " + kTokenA), (Lines{"#3 ERR 409 NOT_READY"}));
  EXPECT_FALSE(rig.session.rebootDue(rig.now + 60000));
}

TEST(OtaSession, AbortWhenReadyBootsTheRunningImage) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 10000);
  rig.send("#2 OTA ABORT " + kTokenA);
  EXPECT_FALSE(rig.slot.activated);
  EXPECT_FALSE(rig.session.rebootDue(rig.now + 60000));
}

// ---- the screen ----------------------------------------------------------------------

TEST(OtaSession, TheViewFollowsThePhase) {
  Rig rig;
  Release r;
  EXPECT_FALSE(rig.session.view(rig.now));
  EXPECT_FALSE(rig.session.active());
  rig.send(r.begin(rig.signer));
  EXPECT_TRUE(rig.session.active());
  std::optional<View> v = rig.session.view(rig.now + 10000);
  ASSERT_TRUE(v);
  EXPECT_EQ(v->phase, View::Phase::Asking);
  EXPECT_EQ(v->msLeft, 20000u);
  rig.pressWhenAllowed();
  rig.stream(r.bytes, 0, 2400);
  v = rig.session.view(rig.now);
  EXPECT_EQ(v->phase, View::Phase::Receiving);
  EXPECT_EQ(v->at, 2400u);
}

// ---- parsing -------------------------------------------------------------------------

TEST(OtaVersion, ParsesOnlyThreeNumbers) {
  EXPECT_TRUE(parseVersion("0.4.0") == (Version{0, 4, 0}));
  EXPECT_TRUE(parseVersion("12.0.65535") == (Version{12, 0, 65535}));
  for (const char* bad : {"0.4", "0.4.0.1", "0..4", ".4.0", "0.4.", "a.b.c", "0.4.65536", ""})
    EXPECT_FALSE(parseVersion(bad)) << bad;
  EXPECT_TRUE((Version{0, 4, 0}) < (Version{0, 10, 0}));
}

// ---- the trial ----------------------------------------------------------------------

namespace {
const PetMark kPet{3, 0xd30f00d1};
TrialFacts healthy(uint32_t ranMs, bool confirmed) { return {true, true, true, true, kPet, kPet, ranMs, confirmed}; }
}  // namespace

TEST(OtaTrial, KeepsAnImageAPageReachedThatResumedTheSamePetAfterThirtySeconds) {
  EXPECT_EQ(judge(healthy(kTrialMs - 1, true)), Verdict::Wait);
  EXPECT_EQ(judge(healthy(kTrialMs, true)), Verdict::Keep);
}

TEST(OtaTrial, WaitsForAPageThenRollsBackWithoutOne) {
  EXPECT_EQ(judge(healthy(kTrialMs, false)), Verdict::Wait) << "the dish cannot prove Bluetooth alone";
  EXPECT_EQ(judge(healthy(kConfirmWithinMs - 1, false)), Verdict::Wait);
  EXPECT_EQ(judge(healthy(kConfirmWithinMs, false)), Verdict::RollBack);
}

TEST(OtaTrial, RollsBackAnImageThatLostThePet) {
  TrialFacts f = healthy(kTrialMs, true);
  f.now = PetMark{0, 0x12345678};
  EXPECT_EQ(judge(f), Verdict::RollBack) << "a fresh founder";
  f.now = PetMark{4, 0xd30f00d1};
  EXPECT_EQ(judge(f), Verdict::RollBack) << "another generation";
  f = healthy(kTrialMs, true);
  f.resumed = false;
  EXPECT_EQ(judge(f), Verdict::RollBack) << "fell back, from the lineage, or read-only";
}

TEST(OtaTrial, APendingImageWithNoHandoverRecordRollsBack) {
  TrialFacts f = healthy(kTrialMs, true);
  f.handedOver = false;   // a power cut can not lose it: the record is written before the switch
  EXPECT_EQ(judge(f), Verdict::RollBack);
}

// A sealed or newer-format keepsake was never read by the old image, so its
// record names no pet: an update is how such a pet wakes.
TEST(OtaTrial, ARecordThatNamesNoPetKeepsAnyPetTheNewImageResumes) {
  TrialFacts f = healthy(kTrialMs, true);
  f.pet.reset();
  f.now = PetMark{6, 0xabcdef01};
  EXPECT_EQ(judge(f), Verdict::Keep);
  f.resumed = false;
  EXPECT_EQ(judge(f), Verdict::RollBack) << "still sealed: back to the image that owns the flash";
}

TEST(OtaTrial, RollsBackAnImageWhosePanelDriverFailed) {
  TrialFacts f = healthy(kTrialMs, true);
  f.panelUp = false;
  EXPECT_EQ(judge(f), Verdict::RollBack);
}

namespace {
struct FakePort : TrialPort {
  bool isPending = true, rollBackWorks = true;
  std::optional<Handover> record;
  std::optional<Version> rolled;
  int keeps = 0, rollBacks = 0;
  std::vector<std::string> said;
  bool pending() override { return isPending; }
  std::optional<Handover> handover() override { return record; }
  void forgetHandover() override { record.reset(); }
  void noteRolledBack(std::optional<Version> v) override { rolled = v; }
  void keep() override { ++keeps; }
  void rollBack() override {
    ++rollBacks;
    if (rollBackWorks) throw std::runtime_error("restarted");   // the real one does not return
  }
  void say(const char* line) override { said.emplace_back(line); }
};
const Handover kHanded{kPet, Version{0, 5, 0}, tokenOf(kTokenA)};
}  // namespace

TEST(OtaTrialRun, KeepsAfterItsPageConfirmsAndClearsAnyEarlierRollback) {
  FakePort port;
  port.record = kHanded;
  port.rolled = Version{0, 4, 9};
  Trial trial;
  trial.begin(port, true, true, kPet);
  EXPECT_EQ(trial.poll(kTrialMs), Trial::Poll::Running) << "no page yet";
  EXPECT_EQ(trial.confirm(tokenOf(kTokenB)), Confirm::NotYours) << "another page cannot keep it";
  EXPECT_EQ(trial.confirm(tokenOf(kTokenA)), Confirm::Confirmed);
  EXPECT_EQ(trial.poll(kTrialMs), Trial::Poll::Kept);
  EXPECT_EQ(port.keeps, 1);
  EXPECT_FALSE(port.record) << "the handover is spent";
  EXPECT_FALSE(port.rolled) << "a later update that kept clears rolledback";
  EXPECT_FALSE(trial.onTrial());
  EXPECT_EQ(trial.confirm(tokenOf(kTokenA)), Confirm::NotOnTrial);
}

TEST(OtaTrialRun, TheOldImageNamesTheUpdateThatRolledBack) {
  FakePort port;
  port.isPending = false;   // the bootloader came back here
  port.record = kHanded;
  Trial trial;
  trial.begin(port, true, true, kPet);
  ASSERT_TRUE(port.rolled);
  EXPECT_TRUE(*port.rolled == (Version{0, 5, 0}));
  EXPECT_FALSE(port.record);
  EXPECT_FALSE(trial.onTrial());
}

TEST(OtaTrialRun, ARollbackThatCannotHappenLetsThePetCarryOn) {
  FakePort port;
  port.record = kHanded;
  port.rollBackWorks = false;
  Trial trial;
  trial.begin(port, true, true, PetMark{9, 9});   // another pet: roll back
  EXPECT_EQ(trial.poll(10), Trial::Poll::GaveUp) << "release the hold and keep running";
  EXPECT_FALSE(trial.onTrial());
  EXPECT_EQ(trial.poll(20), Trial::Poll::Running);
  EXPECT_EQ(port.rollBacks, 1) << "tried once, not every loop";
  EXPECT_EQ(port.keeps, 1) << "the only image that boots is marked valid, not left pending";
}

TEST(OtaTrialRun, ARollbackRestarts) {
  FakePort port;
  port.record = kHanded;
  Trial trial;
  trial.begin(port, true, true, kPet);
  EXPECT_THROW(trial.poll(kConfirmWithinMs), std::runtime_error) << "no page within 5 minutes";
}

TEST(OtaTrial, AnImageNotOnTrialIsLeftAlone) {
  EXPECT_EQ(judge({false, false, false, false, std::nullopt, PetMark{0, 0}, 0, false}), Verdict::NotOnTrial);
}

// ---- the release tool's output -------------------------------------------------------

TEST(OtaRelease, TheDishAcceptsWhatToolsOtaReleasePySigned) {
  MemSlot slot;
  FakeHost host;
  ScriptedLink link;
  Session session{slot, host, link, Config{"lcd128", Version{0, 4, 0}, otafixture::kPublicKey}};
  ASSERT_TRUE(session.handle(std::string("#1 ") + otafixture::kBegin + " " + kTokenA, 0, 1));
  ASSERT_EQ(link.sent.size(), 1u);
  EXPECT_EQ(link.sent[0].substr(0, 35), "#1 OK state=confirm at=0 size=1000 ");
  session.press(Session::kMinAskMs);
  Bytes image(otafixture::kImageSize);
  for (uint32_t i = 0; i < image.size(); ++i) image[i] = otafixture::imageByte(i);
  for (uint32_t at = 0; at < image.size(); at += 200) {
    Bytes p{uint8_t(at), uint8_t(at >> 8), 0, 0};
    p.insert(p.end(), image.begin() + at, image.begin() + std::min<size_t>(at + 200, image.size()));
    session.data(p.data(), p.size(), Session::kMinAskMs, 1);
  }
  EXPECT_TRUE(slot.activated) << "the tool's sha256 matches the dish's";
}

TEST(OtaRelease, ToolSignaturesFailUnderAnotherKey) {
  Signer other;
  MemSlot slot;
  FakeHost host;
  ScriptedLink link;
  Session session{slot, host, link, Config{"lcd128", Version{0, 4, 0}, other.pub}};
  session.handle(std::string("#1 ") + otafixture::kBegin + " " + kTokenA, 0, 1);
  EXPECT_EQ(link.sent, (Lines{"#1 ERR 403 BAD_SIGNATURE"}));
}

// ---- the screen ----------------------------------------------------------------------

namespace {
std::unique_ptr<paint::Canvas240> frame(const View& v) {
  auto cv = std::make_unique<paint::Canvas240>();
  for (int i = 0; i < 240 * 240; ++i) cv->px[i] = uint16_t(((i / 240) >> 3) << 11 | 0x0400);   // a stand-in for the pet
  drawUpdate(*cv, v);
  return cv;
}
uint16_t at(const paint::Canvas240& cv, int x, int y) { return cv.px[y * 240 + x]; }
}  // namespace

// OTA_DUMP=<dir> writes a PPM per phase for review.
TEST(OtaScreen, TheRingFollowsProgress) {
  const auto quarter = frame({View::Phase::Receiving, 300, 1000, 0});   // 30%: past three o'clock, short of six
  const auto blank = frame({View::Phase::Receiving, 0, 1000, 0});
  EXPECT_NE(at(*quarter, 235, 120), at(*blank, 235, 120)) << "three o'clock is lit at 30%";
  EXPECT_EQ(at(*quarter, 120, 235), at(*blank, 120, 235)) << "six o'clock is not";
  const auto asking = frame({View::Phase::Asking, 0, 1000, Session::kConfirmMs / 2});
  EXPECT_NE(at(*asking, 120, 5), at(*blank, 120, 5)) << "the question's countdown starts at twelve";
  if (const char* dir = std::getenv("OTA_DUMP")) {
    const std::pair<const char*, View> shots[] = {{"asking", {View::Phase::Asking, 0, 1000, 20000}},
                                                  {"receiving", {View::Phase::Receiving, 420, 1000, 0}},
                                                  {"ready", {View::Phase::Ready, 1000, 1000, 0}}};
    for (const auto& [name, v] : shots) {
      const auto cv = frame(v);
      std::ofstream f(std::string(dir) + "/ota-" + name + ".ppm", std::ios::binary);
      f << "P6 240 240 255" << char(10);
      for (uint16_t p : cv->px) f << char((p >> 11) << 3) << char(((p >> 5) & 63) << 2) << char((p & 31) << 3);
    }
  }
}

// ---- on trial, ownership, the restart --------------------------------------------------------------

TEST(OtaSession, RefusesAnUpdateWhileThisImageIsOnTrial) {
  Rig rig;
  rig.host.trial = true;
  EXPECT_EQ(rig.send(Release{}.begin(rig.signer)), (Lines{"#1 ERR 409 TRIAL"}))
      << "it would overwrite the only slot known to boot";
}

TEST(OtaSession, APressWhileOnTrialStartsNothing) {
  Rig rig;
  rig.send(Release{}.begin(rig.signer));
  rig.host.trial = true;
  rig.pressWhenAllowed();
  EXPECT_FALSE(rig.slot.opened);
  EXPECT_EQ(rig.last(), "! OTA state=idle last=on_trial");
}

TEST(OtaSession, HandsOverThePetBeforeSwitchingSlots) {
  Rig rig;
  Release r;
  std::vector<std::string> order;
  rig.slot.order = rig.host.order = &order;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 10000);
  EXPECT_EQ(order, (std::vector<std::string>{"save", "open", "handover", "activate"}));
}

TEST(OtaSession, AnUpdateThatEndsWithoutARestartForgetsItsHandover) {
  {
    Rig rig;   // the slot will not switch
    Release r;
    rig.slot.activateOk = false;
    rig.confirm(r);
    rig.stream(r.bytes, 0, 10000);
    EXPECT_EQ(rig.host.handOvers, 1);
    EXPECT_EQ(rig.last(), "! OTA state=idle last=flash_error");
    EXPECT_EQ(rig.host.forgets, 1);
  }
  {
    Rig rig;   // ABORT when ready
    Release r;
    rig.confirm(r);
    rig.stream(r.bytes, 0, 10000);
    ASSERT_EQ(rig.host.handOvers, 1);
    rig.send("#2 OTA ABORT " + kTokenA);
    EXPECT_EQ(rig.host.forgets, 1) << "else the next boot says it rolled back";
  }
  {
    Rig rig;   // the save before the restart fails twice
    Release r;
    rig.confirm(r);
    rig.stream(r.bytes, 0, 10000);
    rig.host.saveFailures = 2;
    EXPECT_FALSE(rig.session.prepareRestart());
    EXPECT_EQ(rig.host.forgets, 1);
  }
  {
    Rig rig;   // a restart keeps it: the new image reads it
    Release r;
    rig.confirm(r);
    rig.stream(r.bytes, 0, 10000);
    EXPECT_TRUE(rig.session.prepareRestart());
    EXPECT_EQ(rig.host.forgets, 0);
  }
}

TEST(OtaSession, AHandoverThatFailsNeverSwitches) {
  Rig rig;
  Release r;
  rig.host.handOverOk = false;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 10000);
  EXPECT_EQ(rig.slot.activations, 0);
  EXPECT_EQ(rig.last(), "! OTA state=idle last=flash_error");
}

TEST(OtaSession, ConfirmTellsTheTrialAPageGotThrough) {
  Rig rig;
  EXPECT_EQ(rig.send("#9 OTA CONFIRM " + kTokenA), (Lines{"#9 OK trial=0"}));
  EXPECT_FALSE(rig.host.confirmedWith) << "nothing to confirm off trial";
  rig.host.trial = true;
  EXPECT_EQ(rig.send("#9 OTA CONFIRM " + kTokenA), (Lines{"#9 OK trial=1"}));
  EXPECT_TRUE(rig.host.confirmedWith == tokenOf(kTokenA)) << "the trial checks the token, not the session";
  rig.host.confirmAnswer = Confirm::NotYours;
  EXPECT_EQ(rig.send("#9 OTA CONFIRM " + kTokenB), (Lines{"#9 ERR 403 NOT_YOURS"}));
  EXPECT_EQ(rig.send("#9 OTA CONFIRM"), (Lines{"#9 ERR 403 NOT_YOURS"})) << "no token is nobody's";
}

TEST(OtaSession, APressInTheFirstMomentsIsNotAYes) {
  Rig rig;
  rig.send(Release{}.begin(rig.signer));
  rig.session.press(rig.now + Session::kMinAskMs - 1);
  EXPECT_TRUE(rig.session.asking()) << "a child mid-press when the question appeared";
  EXPECT_FALSE(rig.slot.opened);
  rig.session.press(rig.now + Session::kMinAskMs);
  EXPECT_TRUE(rig.slot.opened);
}

TEST(OtaSession, AnotherPageCannotTakeAbortOrRebootTheSession) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 2400);
  Release theirs = r;
  theirs.token = kTokenB;   // the same public image, another page
  EXPECT_EQ(rig.send(theirs.begin(rig.signer)), (Lines{"#1 ERR 403 NOT_YOURS"}));
  EXPECT_EQ(rig.sendNow("#5 OTA ABORT " + kTokenB), (Lines{"#5 ERR 403 NOT_YOURS"}));
  EXPECT_EQ(rig.sendNow("#6 OTA REBOOT " + kTokenB), (Lines{"#6 ERR 403 NOT_YOURS"}));
  EXPECT_EQ(rig.sendNow("#6 OTA ABORT"), (Lines{"#6 ERR 403 NOT_YOURS"})) << "no token is nobody's";
  EXPECT_EQ(rig.sendNow("#7 OTA STATUS").at(0).substr(0, 30), "#7 OK state=receiving at=2400 ");
  rig.stream(r.bytes, 2400, 10000);
  EXPECT_TRUE(rig.slot.activated) << "the transfer carried on";
}

TEST(OtaSession, AStrangersDataNeverReachesTheSlot) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 2400);
  rig.packet(r.bytes, 2400, 240, Peer{2});
  EXPECT_EQ(rig.slot.written.size(), 2400u);
}

TEST(OtaSession, ThePageWithTheTokenTakesItsSessionToANewConnection) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 2400);
  rig.peer = 2;   // dropped, and back on another connection
  rig.packet(r.bytes, 2400, 240);
  EXPECT_EQ(rig.slot.written.size(), 2400u) << "not until it says who it is";
  EXPECT_EQ(rig.send(r.begin(rig.signer)).at(0).substr(0, 30), "#1 OK state=receiving at=2400 ");
  rig.packet(r.bytes, 2400, 240, Peer{1});
  EXPECT_EQ(rig.slot.written.size(), 2400u) << "the old connection no longer writes";
  rig.stream(r.bytes, 2400, 10000);
  EXPECT_TRUE(rig.slot.activated);
}

TEST(OtaSession, TheHandoverNamesTheVersionAndThePage) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 10000);
  ASSERT_TRUE(rig.host.handedTo && rig.host.handedToken);
  EXPECT_TRUE(*rig.host.handedTo == (Version{0, 5, 0}));
  EXPECT_TRUE(*rig.host.handedToken == tokenOf(kTokenA)) << "only this page may CONFIRM the new image";
}

TEST(OtaSession, StatusNamesAnUpdateThatRolledBack) {
  Rig rig;
  rig.host.rolled = Version{0, 5, 0};
  EXPECT_EQ(rig.send("#7 OTA STATUS"),
            (Lines{"#7 OK state=idle board=lcd128 fw=0.4.0 chunk=240 trial=0 last=none rolledback=0.5.0"}));
}

TEST(OtaSession, ABeginWithNoTokenIsMalformed) {
  Rig rig;
  std::string line = Release{}.begin(rig.signer);
  line = line.substr(0, line.rfind(' '));
  EXPECT_EQ(rig.send(line), (Lines{"#1 ERR 400 BAD_ARGS"}));
}

TEST(OtaSession, CheapRefusalsComeBeforeTheSignatureCheck) {
  Rig rig;
  Release r;
  r.board = "lcd146";
  EXPECT_EQ(rig.send(tamperSig(r.begin(rig.signer))), (Lines{"#1 ERR 403 WRONG_BOARD"}));
}

TEST(OtaSession, AtMostOneSignatureCheckEveryTwoSeconds) {
  Rig rig;
  Signer stranger;
  const std::string forged = Release{}.begin(stranger);
  EXPECT_EQ(rig.sendNow(forged), (Lines{"#1 ERR 403 BAD_SIGNATURE"}));
  rig.now += Session::kVerifyEveryMs - 1;
  EXPECT_EQ(rig.sendNow(forged), (Lines{"#1 ERR 429 TOO_SOON"})) << "a flood cannot stall the pet";
  rig.now += 1;
  EXPECT_EQ(rig.sendNow(forged), (Lines{"#1 ERR 403 BAD_SIGNATURE"}));
}

TEST(OtaSession, TheRestartSavesTwiceThenHandsOver) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 10000);
  const int before = rig.host.handOvers;
  rig.host.saveFailures = 1;
  EXPECT_TRUE(rig.session.prepareRestart());
  EXPECT_EQ(rig.host.handOvers, before + 1) << "the record names the pet as he is now";
}

TEST(OtaSession, ARestartWhoseSaveFailsTwiceKeepsTheOldImage) {
  Rig rig;
  Release r;
  rig.confirm(r);
  rig.stream(r.bytes, 0, 10000);
  rig.host.saveFailures = 2;
  EXPECT_FALSE(rig.session.prepareRestart());
  EXPECT_TRUE(rig.slot.discarded);
  EXPECT_FALSE(rig.slot.activated) << "the running image boots next time";
  EXPECT_EQ(rig.last(), "! OTA state=idle last=save_failed");
  EXPECT_FALSE(rig.session.rebootDue(rig.now + 60000));
}

// ---- the pet while an image is on trial ----------------------------------------------

namespace {
void live(blorb::Dish& dish, blorb::Link& link, uint32_t& ms, uint32_t forMs) {
  for (const uint32_t end = ms + forMs; ms < end; ms += 100) dish.tick(ms, link);
}
}  // namespace

TEST(OtaTrial, AnImageOnTrialWritesNothingUntilItKeepsItself) {
  blorbtest::MemStorage disk;
  ScriptedLink phone;
  uint32_t ms = 0;
  {
    blorb::Dish old(disk, 7, 0x9f31c2d04a7bull);
    live(old, phone, ms, 60000);
    ASSERT_TRUE(old.flush()) << "the save the old image hands over";
  }
  const auto handedOver = disk.files;

  HeldStorage held(disk);
  held.hold(true);
  blorb::Dish onTrial(held, 7, 0x9f31c2d04a7bull);
  EXPECT_EQ(onTrial.boot(), blorb::Boot::Resumed);
  // What a page does on its way to CONFIRM: a clock, and a rename that writes the lineage.
  phone.inbound = {"#1 TIME 1760000000 0", "#2 TWIST rename Trialist"};
  live(onTrial, phone, ms, 6 * 60000);
  EXPECT_FALSE(onTrial.flush());
  EXPECT_TRUE(disk.files == handedOver) << "the keepsake and lineage.log are as the old image left them";

  held.hold(false);
  EXPECT_TRUE(onTrial.flush());
  EXPECT_FALSE(disk.files == handedOver) << "kept: he is saved again";
}

TEST(OtaTrial, AnImageOnTrialThatCannotReadThePetFoundsNothingOnDisk) {
  blorbtest::MemStorage disk;
  HeldStorage held(disk);
  held.hold(true);
  blorb::Dish onTrial(held, 7, 0x9f31c2d04a7bull);
  EXPECT_EQ(onTrial.boot(), blorb::Boot::Fresh);
  EXPECT_TRUE(disk.files.empty()) << "a fresh egg is not written over anything";
}

// ---- what an image on trial may not do ------------------------------------------------

namespace {
// Counts the keepsake writes a Dish even tries, before a hold refuses them.
struct Attempts : blorb::Storage {
  blorb::Storage& inner;
  int snapshotWrites = 0;
  explicit Attempts(blorb::Storage& s) : inner(s) {}
  std::optional<size_t> read(const char* n, size_t o, uint8_t* b, size_t c) override { return inner.read(n, o, b, c); }
  bool writeAtomic(const char* n, const uint8_t* d, size_t l) override {
    if (std::strncmp(n, "snap.", 5) == 0) ++snapshotWrites;
    return inner.writeAtomic(n, d, l);
  }
  bool append(const char* n, const uint8_t* d, size_t l) override { return inner.append(n, d, l); }
  bool rename(const char* f, const char* t) override { return inner.rename(f, t); }
  size_t size(const char* n) override { return inner.size(n); }
  bool writable() override { return inner.writable(); }
};
}  // namespace

TEST(OtaTrial, AHeldDishBuildsNoSaveItCannotWrite) {
  blorbtest::MemStorage disk;
  ScriptedLink phone;
  uint32_t ms = 0;
  {
    blorb::Dish old(disk, 7, 0x9f31c2d04a7bull);
    live(old, phone, ms, 1000);
    ASSERT_TRUE(old.flush());
  }
  HeldStorage held(disk);
  held.hold(true);
  Attempts attempts(held);
  blorb::Dish onTrial(attempts, 7, 0x9f31c2d04a7bull);
  phone.inbound = {"#1 TIME 1760000000 0"};   // an urgent save, retried every tick
  live(onTrial, phone, ms, 60000);
  EXPECT_EQ(attempts.snapshotWrites, 0) << "no 9 KB encode thrown away each tick";
  held.hold(false);
  EXPECT_TRUE(onTrial.flush());
  EXPECT_EQ(attempts.snapshotWrites, 1);
}

TEST(OtaTrial, AHeldDishLeavesATornSlotWhereItIs) {
  blorbtest::MemStorage disk;
  ScriptedLink phone;
  uint32_t ms = 0;
  {
    blorb::Dish old(disk, 7, 0x9f31c2d04a7bull);
    live(old, phone, ms, 1000);
    ASSERT_TRUE(old.flush());
    live(old, phone, ms, 1000);
    ASSERT_TRUE(old.flush());
  }
  for (const char* slot : {"snap.a", "snap.b"}) disk.files[slot].back() ^= 0xff;   // tear the newest, whichever it is
  disk.files["snap.a"].back() ^= 0xff;                                               // and mend one again
  const auto before = disk.files;
  HeldStorage held(disk);
  held.hold(true);
  blorb::Dish onTrial(held, 7, 0x9f31c2d04a7bull);
  EXPECT_NE(onTrial.boot(), blorb::Boot::Fresh);
  EXPECT_TRUE(disk.files == before) << "the torn slot is not moved to rescue/ while held";
}

TEST(OtaTrial, ASealedKeepsakeIsUntouchedAcrossATrial) {
  blorbtest::MemStorage disk;
  ScriptedLink phone;
  uint32_t ms = 0;
  {
    blorb::Dish old(disk, 7, 0x9f31c2d04a7bull);
    live(old, phone, ms, 1000);
    ASSERT_TRUE(old.flush());
  }
  const auto keepsake = disk.files;
  HeldStorage held(disk);
  held.hold(true);
  blorb::DishOptions sealed;
  sealed.sealed = true;   // the new image cannot open the keepsake either
  blorb::Dish onTrial(held, 7, 0x9f31c2d04a7bull, sealed);
  EXPECT_EQ(onTrial.boot(), blorb::Boot::Sealed);
  phone.inbound = {"#1 TIME 1760000000 0", "#2 TWIST rename Trialist"};
  live(onTrial, phone, ms, 60000);
  onTrial.flush();
  held.hold(false);   // even were the trial kept, sealed writes nothing
  onTrial.flush();
  live(onTrial, phone, ms, 1000);
  EXPECT_TRUE(disk.files == keepsake);
}

TEST(OtaTrialGuard, TwistsThatWriteTheFlashWaitWhileOnTrial) {
  ScriptedLink inner;
  bool trial = true;
  TrialGuard guard(inner, [&] { return trial; });
  inner.inbound = {"#4 TWIST rename Bob", "#5 TWIST gene_edit 3 0 9", "#6 TWIST pick 1", "#a TWIST teach HI",
                   "#b TWIST unteach", "TWIST rename Bob", "#7 TWIST say HI", "#8 HELLO"};
  EXPECT_EQ(guard.readLine(), std::optional<std::string_view>("#7 TWIST say HI")) << "say writes nothing";
  EXPECT_EQ(inner.sent, (Lines{"#4 ERR 409 TRIAL", "#5 ERR 409 TRIAL", "#6 ERR 409 TRIAL", "#a ERR 409 TRIAL",
                               "#b ERR 409 TRIAL", "#0 ERR 409 TRIAL"}))
      << "a line with no id is id 0 to the protocol, so it is to the guard";
  EXPECT_EQ(guard.readLine(), std::optional<std::string_view>("#8 HELLO"));
  trial = false;
  inner.inbound = {"#9 TWIST rename Bob"};
  EXPECT_EQ(guard.readLine(), std::optional<std::string_view>("#9 TWIST rename Bob"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
