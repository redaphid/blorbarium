// Remote debug (lib/rdebug, docs/remote-debug.md): the serial log's ring, the
// JSON escaping of what goes home, and the commands the site may send: only
// the allowlist, only in bounds, each at most once, only from the site.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "rdebug/remote.h"

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
  // Nothing that could switch updates off, forget a network, erase or flash anything.
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
