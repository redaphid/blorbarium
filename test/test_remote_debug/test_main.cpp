// Remote debug (lib/rdebug, docs/remote-debug.md): the serial log's ring, the
// JSON escaping of what goes home, and the commands the site may send: only
// the allowlist, only in bounds, each at most once, only from the site.
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "rdebug/remote.h"
#include "rdebug/shot.h"

using rdebug::Command;
using rdebug::LogRing;
using rdebug::Verb;

static std::string tailOf(const LogRing& r, size_t cap = 1024) {
  std::string s(cap, '\0');
  s.resize(r.tail(s.data(), cap));
  return s;
}

TEST(LogRing, KeepsEverythingUntilFull) {
  char buf[16];
  LogRing r;
  r.attach(buf, sizeof buf);
  r.write("hello ", 6);
  r.write("world", 5);
  EXPECT_EQ(tailOf(r), "hello world");
  EXPECT_EQ(r.total(), 11u);
  EXPECT_EQ(r.held(), 11u);
}

TEST(LogRing, KeepsTheLastBytesInOrderAcrossTheWrap) {
  char buf[8];
  LogRing r;
  r.attach(buf, sizeof buf);
  r.write("abcdef", 6);
  r.write("ghij", 4);
  EXPECT_EQ(tailOf(r), "cdefghij");
  r.write("klm", 3);
  EXPECT_EQ(tailOf(r), "fghijklm");
  EXPECT_EQ(r.total(), 13u);
  EXPECT_EQ(tailOf(r, 3), "klm");   // a smaller cap: the newest
}

TEST(LogRing, AWriteLongerThanTheRingKeepsItsEnd) {
  char buf[4];
  LogRing r;
  r.attach(buf, sizeof buf);
  r.write("x", 1);
  r.write("0123456789", 10);
  EXPECT_EQ(tailOf(r), "6789");
  EXPECT_EQ(r.total(), 11u);
}

TEST(LogRing, SinceFindsWhatWasPrintedAfterTheMark) {
  char buf[16];
  LogRing r;
  r.attach(buf, sizeof buf);
  r.write("before ", 7);
  const uint64_t mark = r.total();
  r.write("STATE ok", 8);
  std::string s(64, '\0');
  s.resize(r.since(mark, s.data(), s.size()));
  EXPECT_EQ(s, "STATE ok");
  s.assign(4, '\0');
  s.resize(r.since(mark, s.data(), 4));   // capped: the first bytes
  EXPECT_EQ(s, "STAT");
  r.write("0123456789abcdef", 16);         // the mark's bytes are gone: what is held
  s.assign(64, '\0');
  s.resize(r.since(mark, s.data(), s.size()));
  EXPECT_EQ(s, "0123456789abcdef");
  EXPECT_EQ(r.since(r.total(), s.data(), 64), 0u);
}

TEST(LogRing, UnattachedIsSilent) {
  LogRing r;
  r.write("x", 1);
  char out[4];
  EXPECT_EQ(r.tail(out, 4), 0u);
  EXPECT_FALSE(r.ready());
}

TEST(JsonEscape, EscapesWhatJsonNeeds) {
  const std::string in = "a\"b\\c\nd\te\x01" "f\r";
  char out[64];
  const size_t n = rdebug::jsonEscape(in.data(), in.size(), out, sizeof out);
  EXPECT_EQ(std::string(out, n), "a\\\"b\\\\c\\nd\\te\\u0001f\\u000d");
}

TEST(JsonEscape, NeverCutsAnEscapeAndSkipsACutUtf8Start) {
  char out[6];
  size_t n = rdebug::jsonEscape("abc\"d", 5, out, sizeof out);
  EXPECT_EQ(std::string(out, n), "abc\\\"");
  n = rdebug::jsonEscape("abcd\"", 5, out, sizeof out);
  EXPECT_EQ(std::string(out, n), "abcd");   // the escape would not fit with the NUL
  const std::string cut = "\xa9\xb1ok \xc3\xa9";   // the tail of a sequence, then a whole one
  char big[32];
  n = rdebug::jsonEscape(cut.data(), cut.size(), big, sizeof big);
  EXPECT_EQ(std::string(big, n), "ok \xc3\xa9");
}

TEST(JsonEscapeTail, KeepsTheNewestBytesThatFit) {
  const std::string log = "old line\nnewer \"line\"\nnewest\n";
  char out[16];
  const size_t n = rdebug::jsonEscapeTail(log.data(), log.size(), out, sizeof out);
  EXPECT_EQ(std::string(out, n), "ine\\\"\\nnewest\\n");   // 15 bytes and the NUL
  EXPECT_LT(n, sizeof out);
  char big[128];
  EXPECT_EQ(std::string(big, rdebug::jsonEscapeTail(log.data(), log.size(), big, sizeof big)),
            "old line\\nnewer \\\"line\\\"\\nnewest\\n");
  EXPECT_EQ(rdebug::jsonEscapeTail(log.data(), log.size(), big, 1), 0u);
}

// The badge's report: 8 KB of the log, all of it a worst case for escaping,
// never takes the log's part of the report past its cap.
TEST(JsonEscapeTail, AWorstCaseLogStaysInItsRoom) {
  const std::string log(8192, '\x01');   // every byte becomes \u0001
  std::string out(rdebug::kLogTailMax + 1, '\0');
  const size_t n = rdebug::jsonEscapeTail(log.data(), log.size(), out.data(), out.size());
  EXPECT_LE(n, rdebug::kLogTailMax);
  EXPECT_EQ(n % 6, 0u);   // whole escapes only
  EXPECT_LT(rdebug::kReportMax, 5 * 1024u);
}

// 0.7.35 on a board: a provision's check-in, a 4.8 KB report, took internal
// RAM down to 3.9 KB. A provision's and a cable's check send a light one: a
// KB of the log, no state capture, no screenshot after (they wait for a Check).
TEST(ReportShape, ALightReportCarriesAKilobyteOfLogAndNothingHeavy) {
  constexpr rdebug::ReportShape light = rdebug::reportShape(true), full = rdebug::reportShape(false);
  EXPECT_FALSE(light.state);
  EXPECT_FALSE(light.shot);
  EXPECT_TRUE(full.state);
  EXPECT_TRUE(full.shot);
  EXPECT_EQ(rdebug::logRoom(700, light), rdebug::kLightLogTailMax) << "a usual report's rest is about 700 bytes";
  EXPECT_EQ(rdebug::logRoom(700, full), rdebug::kLogTailMax);
  EXPECT_EQ(rdebug::logRoom(1500, light), rdebug::kLightReportMax - 1500 - 2) << "eight results and a crash: less log";
  EXPECT_EQ(rdebug::logRoom(rdebug::kLightReportMax - 2, light), 0u);
  EXPECT_EQ(rdebug::logRoom(rdebug::kLightReportMax + 300, light), 0u) << "the results still go; the log does not";
  EXPECT_EQ(rdebug::logRoom(4000, full), rdebug::kReportMax - 4000 - 2);
  // A worst-case log escaped into the light room stays in it.
  const std::string log(8192, '\x01');
  std::string out(rdebug::logRoom(700, light) + 1, '\0');
  EXPECT_LE(rdebug::jsonEscapeTail(log.data(), log.size(), out.data(), out.size()), rdebug::kLightLogTailMax);
  EXPECT_LE(700 + rdebug::kLightLogTailMax + 2, rdebug::kLightReportMax) << "a light report stays under 2 KB";
}

// ---- the body in small writes (hw/net_job.h ChunkedTls) ----

TEST(WriteChunked, NeverHandsTlsMoreThanAChunk) {
  std::vector<uint8_t> body(8870);
  for (size_t i = 0; i < body.size(); ++i) body[i] = uint8_t(i * 7);
  std::vector<uint8_t> sent;
  std::vector<size_t> sizes;
  int pauses = 0;
  const size_t n = rdebug::writeChunked(
      body.data(), body.size(), rdebug::kWriteChunk,
      [&](const uint8_t* p, size_t k) { sizes.push_back(k), sent.insert(sent.end(), p, p + k); return k; },
      [&] { ++pauses; });
  EXPECT_EQ(n, body.size());
  EXPECT_EQ(sent, body);
  EXPECT_EQ(pauses, 0);
  for (size_t k : sizes) EXPECT_LE(k, rdebug::kWriteChunk);
  EXPECT_EQ(sizes.size(), 9u);
}

TEST(WriteChunked, AFailedWriteIsTriedOnceMore) {
  const std::vector<uint8_t> body(3000, 0x5a);
  int calls = 0, pauses = 0;
  size_t n = rdebug::writeChunked(
      body.data(), body.size(), 1024, [&](const uint8_t*, size_t k) { return ++calls == 2 ? size_t(0) : k; },
      [&] { ++pauses; });
  EXPECT_EQ(n, body.size());   // the second piece failed once, then went
  EXPECT_EQ(pauses, 1);
  EXPECT_EQ(calls, 4);
  calls = pauses = 0;
  n = rdebug::writeChunked(
      body.data(), body.size(), 1024, [&](const uint8_t*, size_t k) { return ++calls >= 2 ? size_t(0) : k; },
      [&] { ++pauses; });
  EXPECT_EQ(n, 1024u);   // twice in a row: it stops, and says how far it got
  EXPECT_EQ(calls, 3);
  EXPECT_EQ(pauses, 1);
}

TEST(WriteChunked, APartialWriteCarriesOn) {
  const std::vector<uint8_t> body(2500, 1);
  std::vector<size_t> sizes;
  const size_t n = rdebug::writeChunked(
      body.data(), body.size(), 1024, [&](const uint8_t*, size_t k) { sizes.push_back(k); return k > 600 ? size_t(600) : k; },
      [] {});
  EXPECT_EQ(n, body.size());
  for (size_t k : sizes) EXPECT_LE(k, 1024u);
}

TEST(Pace, WaitsForRamToComeBackBeforeThePiece) {
  std::vector<uint32_t> free{9000, 11000, 13000, 15000, 20000};
  size_t i = 0;
  uint32_t slept = 0;
  const uint32_t waited = rdebug::pace([&] { return free[i < free.size() - 1 ? i : free.size() - 1]; },
                                       [&](uint32_t ms) { slept += ms, ++i; });
  EXPECT_EQ(i, 3u) << "three steps, until 15000 is over the floor";
  EXPECT_EQ(waited, 3 * rdebug::kPaceStepMs);
  EXPECT_EQ(slept, waited);
  EXPECT_EQ(rdebug::pace([] { return 30000u; }, [](uint32_t) { FAIL() << "enough RAM: no wait"; }), 0u);
}

TEST(Pace, ASlowLinkIsSlowedNeverStopped) {
  uint32_t slept = 0;
  const uint32_t waited = rdebug::pace([] { return 6000u; }, [&](uint32_t ms) { slept += ms; });
  EXPECT_EQ(waited, rdebug::kPaceMaxMs);
  EXPECT_EQ(slept, rdebug::kPaceMaxMs);
}

// ---- the screenshot (rdebug/shot.h) ----

// tools/shot.py's decoder, here: base64 lines to PackBits to pixels.
static std::vector<uint16_t> decodeShot(const std::string& text, int& w, int& h, uint32_t& crc) {
  std::vector<uint8_t> bytes;
  std::vector<uint16_t> px;
  size_t at = 0;
  auto b64 = [](char c) -> int {
    const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char* f = std::strchr(k, c);
    return c && f ? int(f - k) : -1;
  };
  while (at < text.size()) {
    const size_t end = text.find('\n', at);
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    EXPECT_EQ(line.rfind("[hw] shot ", 0), 0u) << line;
    const std::string rest = line.substr(10);
    if (std::sscanf(rest.c_str(), "begin w=%d h=%d", &w, &h) == 2) continue;
    if (rest.rfind("end ", 0) == 0) {
      unsigned n = 0;
      std::sscanf(rest.c_str(), "end bytes=%u crc=%x", &n, &crc);
      EXPECT_EQ(n, bytes.size());
      break;
    }
    for (size_t i = 0; i + 3 < rest.size(); i += 4) {
      const int a = b64(rest[i]), b = b64(rest[i + 1]), c = b64(rest[i + 2]), d = b64(rest[i + 3]);
      bytes.push_back(uint8_t(a << 2 | b >> 4));
      if (c >= 0) bytes.push_back(uint8_t(b << 4 | c >> 2));
      if (d >= 0) bytes.push_back(uint8_t(c << 6 | d));
    }
  }
  for (size_t i = 0; i < bytes.size();) {
    const uint8_t hd = bytes[i++];
    if (hd < 128) {
      const uint16_t p = uint16_t(bytes[i] | bytes[i + 1] << 8);
      i += 2;
      for (int k = 0; k <= hd; ++k) px.push_back(p);
    } else {
      for (int k = 0; k < hd - 127; ++k, i += 2) px.push_back(uint16_t(bytes[i] | bytes[i + 1] << 8));
    }
  }
  return px;
}

TEST(Shot, HalvesEachPixelToTheMeanOfFour) {
  std::vector<uint16_t> src(4 * 2);
  // Left 2x2: red 31,31,0,0 and green 0..; right 2x2: one white, three black.
  const uint16_t red = 31 << 11, white = 0xffff;
  src[0] = red, src[1] = red, src[4] = 0, src[5] = 0;
  src[2] = white, src[3] = 0, src[6] = 0, src[7] = 0;
  uint16_t dst[2];
  rdebug::halve565(src.data(), 4, 2, dst);
  EXPECT_EQ(dst[0], uint16_t(16 << 11));                    // (31+31+0+0+2)/4 = 16
  EXPECT_EQ(dst[1], uint16_t(8 << 11 | 16 << 5 | 8));       // a quarter of white, rounded
  std::vector<uint16_t> big(240 * 240, 0x1234), small(120 * 120, 0);
  rdebug::halve565(big.data(), 240, 240, small.data());
  for (uint16_t p : small) ASSERT_EQ(p, 0x1234);
}

TEST(Shot, TheCrcIsZlibs) {
  const uint16_t px[] = {0x3231, 0x3433, 0x3635, 0x3837};   // "12345678" little-endian
  EXPECT_EQ(rdebug::crc565(px, 4), 0x9ae0daafu);
}

TEST(Shot, ItDecodesToTheHalvedFrameAndFitsAtWorst) {
  std::vector<uint16_t> frame(240 * 240);
  for (size_t i = 0; i < frame.size(); ++i) frame[i] = uint16_t(i % 240 < 120 ? 0x07e0 : (i * 2654435761u) >> 7);
  std::vector<uint16_t> small(120 * 120);
  rdebug::halve565(frame.data(), 240, 240, small.data());
  std::vector<uint8_t> packed(120 * 120 * 2 + 512);
  const size_t n = rdebug::packBits565(small.data(), small.size(), packed.data(), packed.size());
  ASSERT_GT(n, 0u);
  std::string text(rdebug::kShotMax, '\0');
  text.resize(rdebug::shotLines(packed.data(), n, 120, 120, rdebug::crc565(small.data(), small.size()), text.data(), text.size()));
  ASSERT_FALSE(text.empty());
  int w = 0, h = 0;
  uint32_t crc = 0;
  EXPECT_EQ(decodeShot(text, w, h, crc), small);
  EXPECT_EQ(w, 120);
  EXPECT_EQ(h, 120);
  EXPECT_EQ(crc, rdebug::crc565(small.data(), small.size()));
  // The worst frame, no two pixels alike in a row, still fits kShotMax (its own POST stays under the site's 64 KB).
  for (size_t i = 0; i < small.size(); ++i) small[i] = uint16_t(i * 7 + 1);
  const size_t worst = rdebug::packBits565(small.data(), small.size(), packed.data(), packed.size());
  ASSERT_GT(worst, 0u);
  text.assign(rdebug::kShotMax, '\0');
  const size_t len = rdebug::shotLines(packed.data(), worst, 120, 120, 0, text.data(), text.size());
  EXPECT_GT(len, 0u);
  EXPECT_LT(len + len / 128 + 512, 64u * 1024);   // escaped newlines and the JSON around it
  EXPECT_EQ(rdebug::shotLines(packed.data(), worst, 120, 120, 0, text.data(), 1000), 0u);   // too small: nothing
}

TEST(Shot, ItGoesOnceAndWaitsOnlyForRam) {
  rdebug::ShotSlot s;
  EXPECT_FALSE(s.waiting());
  EXPECT_FALSE(s.go(100000, 50000));
  s.hold(42, 30000);
  EXPECT_TRUE(s.waiting());
  EXPECT_FALSE(s.go(20000, 15000));   // short of 24 KB free: it waits, still held
  EXPECT_FALSE(s.go(30000, 8000));    // no 9 KB block: it waits
  EXPECT_TRUE(s.waiting());
  EXPECT_TRUE(s.go(33168, 15348));    // the badge's numbers before the failed report
  s.tried();                          // one try: it failed (or went), and either way it is gone
  EXPECT_FALSE(s.waiting());
  EXPECT_FALSE(s.go(100000, 50000));  // never again, in any later window
  EXPECT_EQ(s.id(), 42u);             // its result still names the command
}

// ---- commands ----

static std::vector<Command> parse(const std::string& reply, uint32_t last = 0, size_t max = 8) {
  std::vector<Command> out(max);
  out.resize(rdebug::parseCommands(reply, last, out.data(), max));
  return out;
}

TEST(Commands, TheAllowlistParses) {
  const auto c = parse("blorb-cmds 1\n1 check\n2 interval 60\n3 state\n4 shot\n5 reboot\n6 marquee hello\n");
  ASSERT_EQ(c.size(), 6u);
  EXPECT_EQ(c[0].verb, Verb::Check);
  EXPECT_EQ(c[1].verb, Verb::Interval);
  EXPECT_EQ(c[1].arg, 60u);
  EXPECT_EQ(c[2].verb, Verb::State);
  EXPECT_EQ(c[3].verb, Verb::Shot);
  EXPECT_EQ(c[4].verb, Verb::Reboot);
  for (const Command& x : c) EXPECT_TRUE(x.ok) << rdebug::verbName(x.verb);
}

TEST(Commands, AnythingElseIsRefusedAndReported) {
  // A cable verb comes only inside `debug` (DebugCarriesACableLine); a bare word is no command.
  for (const char* verb : {"wifi", "forget", "off", "erase", "ota", "flash", "auto", "DEBUG", "CHECK", "rm"}) {
    const auto c = parse(std::string("blorb-cmds 1\n7 ") + verb + "\n");
    ASSERT_EQ(c.size(), 1u) << verb;
    EXPECT_FALSE(c[0].ok) << verb;
    EXPECT_EQ(c[0].verb, Verb::Unknown) << verb;
    EXPECT_EQ(c[0].id, 7u);
    EXPECT_STRNE(c[0].why, "");
  }
}

TEST(Commands, TheIntervalStaysInBounds) {
  EXPECT_FALSE(parse("blorb-cmds 1\n1 interval 29\n")[0].ok);
  EXPECT_TRUE(parse("blorb-cmds 1\n1 interval 30\n")[0].ok);
  EXPECT_TRUE(parse("blorb-cmds 1\n1 interval 720\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n1 interval 721\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n1 interval 0\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n1 interval\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n1 interval -5\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n1 interval 99999999999\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n1 reboot now\n")[0].ok);   // the others take no argument
}

TEST(Commands, AMarqueeCarriesItsTextWithinTheLimit) {
  auto c = parse("blorb-cmds 1\n9 marquee Hi kids! Love, Dad  \n");
  ASSERT_EQ(c.size(), 1u);
  EXPECT_TRUE(c[0].ok);
  EXPECT_EQ(c[0].verb, Verb::Marquee);
  EXPECT_STREQ(c[0].text, "Hi kids! Love, Dad");
  EXPECT_TRUE(parse("blorb-cmds 1\n9 marquee " + std::string(rdebug::kMarqueeMax, 'x') + "\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n9 marquee " + std::string(rdebug::kMarqueeMax + 1, 'x') + "\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n9 marquee\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n9 marquee    \n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n9 marquee caf\xc3\xa9\n")[0].ok);   // the font is ASCII
  EXPECT_FALSE(parse("blorb-cmds 1\n9 marquee a\tb\n")[0].ok);
  c = parse("blorb-cmds 1\n9 marquee one\n10 reboot\n");   // a marquee ends at its line
  ASSERT_EQ(c.size(), 2u);
  EXPECT_STREQ(c[0].text, "one");
  EXPECT_EQ(c[1].verb, Verb::Reboot);
}

// CLAUDE.md: every cable verb over Wi-Fi too. `debug <line>` carries one, as
// the cable takes it (a leading "DEBUG " is dropped); the badge runs it.
TEST(Commands, DebugCarriesACableLine) {
  auto c = parse("blorb-cmds 1\n11 debug wifi forget 2 \n12 debug DEBUG wifi\n13 reboot\n");
  ASSERT_EQ(c.size(), 3u);
  EXPECT_TRUE(c[0].ok);
  EXPECT_EQ(c[0].verb, Verb::Debug);
  EXPECT_STREQ(c[0].text, "wifi forget 2");
  EXPECT_STREQ(c[1].text, "wifi") << "the cable's own prefix is taken off";
  EXPECT_EQ(c[2].verb, Verb::Reboot) << "a debug line ends at its line";
  EXPECT_STREQ(rdebug::verbName(Verb::Debug), "debug");
  EXPECT_TRUE(parse("blorb-cmds 1\n9 debug " + std::string(rdebug::kDebugMax, 'x') + "\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n9 debug " + std::string(rdebug::kDebugMax + 1, 'x') + "\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n9 debug\n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n9 debug DEBUG \n")[0].ok);
  EXPECT_FALSE(parse("blorb-cmds 1\n9 debug wifi\tset\n")[0].ok);
}

// Past the owner's token, the badge still keeps a way home: a `debug` line
// never forgets the last network, nor all of them, and never restarts
// outside the quiet moment the reboot command waits for.
TEST(Commands, DebugNeverLeavesTheBadgeWithoutAWayHome) {
  EXPECT_EQ(rdebug::debugRefusal("wifi forget 2", 2), nullptr);
  EXPECT_EQ(rdebug::debugRefusal("wifi forget \"Grandma Net\"", 3), nullptr);
  EXPECT_NE(rdebug::debugRefusal("wifi forget 1", 1), nullptr) << "the last one";
  EXPECT_NE(rdebug::debugRefusal("wifi forget", 3), nullptr) << "bare forgets every one";
  EXPECT_NE(rdebug::debugRefusal("wifi  forget  ", 3), nullptr);
  EXPECT_NE(rdebug::debugRefusal("reboot", 2), nullptr);
  EXPECT_NE(rdebug::debugRefusal("reboot quiet", 2), nullptr);
  EXPECT_EQ(rdebug::debugRefusal("wifi set Home pw", 0), nullptr) << "adding one is a way home";
  EXPECT_EQ(rdebug::debugRefusal("wifi", 0), nullptr);
  EXPECT_EQ(rdebug::debugRefusal("power stats", 0), nullptr);
}

// ---- the log, masked ----

static std::string masked(const std::string& in, std::initializer_list<rdebug::Secret> s, size_t cap = 256) {
  std::vector<char> out(cap);
  const size_t n = rdebug::maskSecrets(in, s.begin(), s.size(), out.data(), cap);
  EXPECT_EQ(n, std::strlen(out.data()));
  return std::string(out.data(), n);
}

TEST(Mask, EveryRememberedSsidAndPasswordIsReplaced) {
  EXPECT_EQ(masked("[wifi] joined HomeNet with hunter22, HomeNet again",
                   {{"HomeNet", "<net #1>"}, {"hunter22", "<password>"}}),
            "[wifi] joined <net #1> with <password>, <net #1> again");
  EXPECT_EQ(masked("nothing secret here", {{"HomeNet", "<net #1>"}}), "nothing secret here");
  EXPECT_EQ(masked("x Home5G y", {{"Home", "<net #2>"}, {"Home5G", "<net #1>"}}), "x <net #1> y") << "the longest wins";
  EXPECT_EQ(masked("a abc b", {{"abc", "<net #1>"}}), "a abc b") << "too short to mask safely";
  EXPECT_EQ(masked("HomeNet HomeNet", {{"HomeNet", "<net #1>"}}, 12), "<net #1> <n") << "cut at cap, NUL kept";
  EXPECT_EQ(masked("", {{"HomeNet", "<net #1>"}}), "");
}

TEST(Commands, EachIdRunsAtMostOnce) {
  EXPECT_TRUE(parse("blorb-cmds 1\n5 reboot\n", 5).empty());   // seen already
  EXPECT_TRUE(parse("blorb-cmds 1\n4 reboot\n", 5).empty());   // older
  const auto c = parse("blorb-cmds 1\n6 check\n6 reboot\n3 shot\n7 state\n", 5);
  ASSERT_EQ(c.size(), 2u);
  EXPECT_EQ(c[0].id, 6u);
  EXPECT_EQ(c[0].verb, Verb::Check);
  EXPECT_EQ(c[1].id, 7u);
}

TEST(Commands, NoHeaderNoCommands) {
  EXPECT_TRUE(parse("1 reboot\n").empty());
  EXPECT_TRUE(parse("{\"cmd\":[{\"id\":1,\"verb\":\"reboot\"}]}").empty());
  EXPECT_TRUE(parse("<html>1 reboot</html>\n1 reboot\n").empty());
  EXPECT_TRUE(parse("").empty());
  EXPECT_TRUE(parse("blorb-cmds 2\n1 reboot\n").empty());
}

TEST(Commands, MalformedLinesAreSkippedAndTheCountIsBounded) {
  const auto c = parse("blorb-cmds 1\r\nx reboot\r\n0 reboot\r\n9\r\n\r\n10 shot\r\n11 interval 60 70\r\n");
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0].id, 10u);
  EXPECT_TRUE(c[0].ok);
  EXPECT_EQ(parse("blorb-cmds 1\n1 check\n2 check\n3 check\n", 0, 2).size(), 2u);
}

TEST(Commands, OnlyTheSiteOverHttpsIsTrusted) {
  EXPECT_TRUE(rdebug::trustedReplyUrl("https://blorb.hypnodroid.com/api/badge/report"));
  EXPECT_FALSE(rdebug::trustedReplyUrl("http://blorb.hypnodroid.com/api/badge/report"));
  EXPECT_FALSE(rdebug::trustedReplyUrl("https://blorb.hypnodroid.com.evil.example/api/badge/report"));
  EXPECT_FALSE(rdebug::trustedReplyUrl("https://192.168.1.5/api/badge/report"));
  EXPECT_FALSE(rdebug::trustedReplyUrl(nullptr));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
