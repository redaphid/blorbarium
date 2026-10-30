// The full-screen marquee (paint/banner.h): what text it takes, how long it
// runs, and that what it draws is where its timing says.
#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>
#include "paint/banner.h"

namespace {

std::string clamp(const char* in) {
  char out[paint::kBannerMaxText + 1];
  const size_t n = paint::clampBannerText(out, in);
  EXPECT_EQ(n, std::strlen(out));
  return out;
}

int inkPixels(const paint::Canvas240& cv) {
  int n = 0;
  for (uint16_t p : cv.px) n += p == paint::kMarqueeBanner.ink;
  return n;
}

}  // namespace

TEST(BannerText, UpperCaseAndRemoteDebugsLength) {
  EXPECT_EQ(paint::kBannerMaxText, 120u);   // rdebug::kMarqueeMax, the remote command's limit
  EXPECT_EQ(clamp("hello, world!"), "HELLO, WORLD!");
  const std::string abc = "abcdefghijklmnopqrstuvwxyz0123456789";
  EXPECT_EQ(clamp((abc + abc + abc + abc).c_str()), "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                                                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKL");
}

TEST(BannerText, SpacesAreTidied) {
  EXPECT_EQ(clamp("   hi\t\tthere   friend  "), "HI THERE FRIEND");
  EXPECT_EQ(clamp(" \t "), "");
  EXPECT_EQ(clamp(""), "");
  EXPECT_EQ(clamp(nullptr), "");
}

TEST(BannerText, OnlyWhatTheFontHas) {
  EXPECT_EQ(clamp("caf\xc3\xa9 \xe2\x9d\xa4 <3"), "CAF <3");   // UTF-8 bytes go; ASCII stays
  EXPECT_EQ(clamp("a\x01" "b\x7f" "c"), "ABC");
}

TEST(BannerText, NeverEndsOnASpaceAtTheCut) {
  // 119 letters, a space, then more: the cut falls on the space, which is dropped.
  const std::string a119(119, 'A'), a118(118, 'A');
  EXPECT_EQ(clamp((a119 + " DEF").c_str()), a119);
  // 118 letters, a space, then a word: the space and one letter fit.
  EXPECT_EQ(clamp((a118 + " CDE").c_str()), a118 + " C");
}

TEST(BannerTiming, CyberPucksBanner) {
  EXPECT_EQ(paint::kMarqueeBanner.pxPerS, 110);
  EXPECT_EQ(paint::kMarqueeBanner.scaleQ8, 640);   // text size 2.5
  EXPECT_EQ(paint::kMarqueeBanner.font, &paint::kFreeSansBold24pt7b);
  // About 80 px a letter: three letters fill the glass.
  const int w = paint::textWidthScaled(paint::kFreeSansBold24pt7b, "WWW", 640);
  EXPECT_GT(w, 200);
  EXPECT_LT(w, 360);
  EXPECT_EQ(paint::textWidthScaled(paint::kFreeSansBold24pt7b, "HELLO", 256),
            paint::textWidth(paint::kFreeSansBold24pt7b, "HELLO"));
}

TEST(BannerTiming, TwoPassesOfALongLineEndWhenTheLastLeaves) {
  // In from the right edge (240 px), across its own width, then one more copy a period behind.
  const int w = 2400;
  EXPECT_EQ(paint::bannerPasses(w, 110), 2);
  EXPECT_EQ(paint::bannerShowMsForWidth(w, 110), uint32_t((240 + w + (w + 120)) * 1000 + 109) / 110);
}

TEST(BannerTiming, AShortWordGoesRoundUntilEightSeconds) {
  const int w = 100;   // period 220; two passes are 560 px, 5.1 s at 110 px/s
  EXPECT_EQ(paint::bannerPasses(w, 110), 4);   // 1000 px: the first count of passes reaching 880 px
  EXPECT_EQ(paint::bannerShowMsForWidth(w, 110), 9091u);
  for (int ww = 0; ww <= 3000; ww += 37) EXPECT_GE(paint::bannerShowMsForWidth(ww, 110), paint::kBannerMinMs) << ww;
}

TEST(BannerTiming, ALongerLineNeverRunsShorterAndAVeryLongOneGoesOnce) {
  uint32_t last = 0;
  for (int w = 400; w <= 2900; w += 10) {   // two passes all the way
    const uint32_t ms = paint::bannerShowMsForWidth(w, 110);
    EXPECT_GE(ms, last) << w;
    last = ms;
  }
  // Thirty Ws are about 3400 px: two passes would be 65 s, so it goes once, in from the right and out the left.
  const std::string thirty(30, 'W');
  const int w = paint::textWidthScaled(paint::kFreeSansBold24pt7b, thirty.c_str(), 640);
  EXPECT_EQ(paint::bannerPasses(w, 110), 1);
  EXPECT_EQ(paint::bannerShowMs(paint::kMarqueeBanner, thirty.c_str()), uint32_t(((240 + w) * 1000 + 109) / 110));
  // The longest line remote debug may send goes once too, and still ends.
  const std::string widest(paint::kBannerMaxText, 'W');
  EXPECT_EQ(paint::bannerPasses(paint::textWidthScaled(paint::kFreeSansBold24pt7b, widest.c_str(), 640), 110), 1);
  EXPECT_LT(paint::bannerShowMs(paint::kMarqueeBanner, widest.c_str()), 130000u);
  // A typical short message still gets both passes.
  EXPECT_EQ(paint::bannerPasses(paint::textWidthScaled(paint::kFreeSansBold24pt7b, "HAPPY BIRTHDAY, GRUNGO!", 640), 110), 2);
}

TEST(BannerDraw, ArrivesFromTheRightAndLeavesTheGroundBehind) {
  auto cv = std::make_unique<paint::Canvas240>();
  const char* s = "HELLO BLORB";
  const paint::Banner& b = paint::kMarqueeBanner;
  const uint32_t life = paint::bannerShowMs(b, s);

  paint::drawBanner(*cv, b, s, 0);   // still off the right edge: all ground
  EXPECT_EQ(inkPixels(*cv), 0);
  for (uint16_t p : cv->px) ASSERT_EQ(p, b.ground);

  paint::drawBanner(*cv, b, s, 1000);   // 110 px in: the first letter, on the right half only
  EXPECT_GT(inkPixels(*cv), 500);
  for (int y = 0; y < 240; ++y)
    for (int x = 0; x < 120; ++x) ASSERT_NE(cv->px[y * 240 + x], b.ink) << x << "," << y;

  paint::drawBanner(*cv, b, s, life / 2);
  EXPECT_GT(inkPixels(*cv), 500);

  paint::drawBanner(*cv, b, s, life);   // the last copy has gone off the left edge
  EXPECT_EQ(inkPixels(*cv), 0);
}

TEST(BannerDraw, SamePixelsForTheSameAge) {
  auto a = std::make_unique<paint::Canvas240>(), c = std::make_unique<paint::Canvas240>();
  std::memset(c->px, 0x5a, sizeof c->px);   // whatever was there before goes
  paint::drawBanner(*a, paint::kMarqueeBanner, "BLORB", 2345);
  paint::drawBanner(*c, paint::kMarqueeBanner, "BLORB", 2345);
  EXPECT_EQ(std::memcmp(a->px, c->px, sizeof a->px), 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
