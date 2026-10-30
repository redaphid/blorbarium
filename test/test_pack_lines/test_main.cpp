#include <gtest/gtest.h>
#include <string>
#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/pack_lines.h"
#include "blorb/thoughts.h"

using namespace blorb;

namespace {

std::string said(ThoughtId id, uint8_t pick) {
  char out[kThoughtLineCap];
  const size_t n = thoughtLine(id, 3, VOICES[0].id, pick, out, kSharedOnly, Words{});
  return std::string(out, n);
}

}  // namespace

TEST(PackLines, ReadsThePackTheSiteSends) {
  std::string text =
      "blorbpack1 0123456789abcdef 7 4\n"
      "peckish\tTHE BUGS OWE ME RENT.\n"
      "peckish\tFEED THE VOID. IT IS ME.\n"
      "no_such_thought\tNEVER SAID\n"
      "moon\tTHIS LINE IS FAR TOO LONG TO FIT ON THE SCREEN AT ALL\n";
  std::vector<PackLines::Entry> e(8);
  PackLines p;
  ASSERT_TRUE(p.parse(text.data(), text.size(), e.data(), e.size()));
  EXPECT_STREQ(p.genome(), "0123456789abcdef");
  EXPECT_EQ(p.version(), 7u);
  EXPECT_EQ(p.size(), 2u);   // the unknown thought and the too-long line are skipped
  const ThoughtId peckish = THOUGHTS[0].id;
  ASSERT_STREQ(THOUGHTS[0].name, "peckish");
  EXPECT_EQ(p.count(peckish), 2);
  EXPECT_STREQ(p.line(peckish, 1), "FEED THE VOID. IT IS ME.");
}

TEST(PackLines, RefusesWhatIsNotAPack) {
  std::string text = "<html>nope</html>\n";
  std::vector<PackLines::Entry> e(4);
  PackLines p;
  EXPECT_FALSE(p.parse(text.data(), text.size(), e.data(), e.size()));
}

TEST(PackLines, WhileSetHeSaysOnlyItsLinesForThoughtsItHas) {
  const ThoughtId peckish = THOUGHTS[0].id, other = THOUGHTS[1].id;
  const std::string before = said(other, 0);
  std::string text = "blorbpack1 0123456789abcdef 1 1\npeckish\tTHE BUGS OWE ME RENT.\n";
  std::vector<PackLines::Entry> e(4);
  PackLines p;
  ASSERT_TRUE(p.parse(text.data(), text.size(), e.data(), e.size()));
  setPackLines(&p);
  for (uint8_t pick = 0; pick < 12; ++pick) EXPECT_EQ(said(peckish, pick), "THE BUGS OWE ME RENT.");
  EXPECT_EQ(said(other, 0), before);   // a thought the pack has nothing for keeps the built-in line
  setPackLines(nullptr);
  EXPECT_NE(said(peckish, 0), "THE BUGS OWE ME RENT.");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
