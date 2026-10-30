#pragma once
// The whole screen as one line of text: a flat colour edge to edge and a few
// enormous letters marching across it, right to left, without stopping.
// cyber-puck's banner (src/widgets.h drawBanner, its ALERT_BANNER), ported off
// LovyanGFX onto a Canvas240: FreeSans Bold 24pt at text size 2.5 (about 80 px a
// letter, three letters on the glass), 110 px a second, black ink on crimson.
//
// The text is a ring, as paint/marquee.h's: the first copy arrives from off the
// right edge, and the next follows half a screen behind its tail. Unlike that
// ring this one is finite: it shows a set number of copies (passes) and is over
// when the last has left the left edge, so a caller knows when to give the
// screen back. Phase is `ageMs`, the time since it went up; it needs no state.
#include <cstddef>
#include <cstdint>
#include "paint/canvas.h"
#include "paint/text.h"

namespace paint {

struct Banner {
  const Font* font;
  int scaleQ8;   // text size, /256
  int pxPerS;
  uint16_t ground, ink;
};

// cyber-puck's: FreeSansBold24pt at 2.5, 110 px/s, its cyberpunk alertBg
// (208,0,75) and shadow (10,6,20) ink.
constexpr uint16_t kBannerGround = uint16_t(((208 >> 3) << 11) | ((0 >> 2) << 5) | (75 >> 3));
constexpr uint16_t kBannerInk = uint16_t(((10 >> 3) << 11) | ((6 >> 2) << 5) | (20 >> 3));
extern const Banner kMarqueeBanner;

// Remote debug's limit (rdebug::kMarqueeMax, lib/rdebug/include/rdebug/remote.h,
// and the site's web/src/reports.js): the remote `marquee` command, the phone's
// MARQUEE and the cable's DEBUG marquee all take the same 120 characters.
constexpr size_t kBannerMaxText = 120;
constexpr int kBannerPasses = 2;            // copies shown, at least
constexpr uint32_t kBannerMinMs = 8000;     // a short word goes round until this long has passed
constexpr uint32_t kBannerMaxMs = 60000;    // a line whose two passes would take longer goes once

// What goes up: upper case, printable ASCII only (the font has 0x20..0x7E),
// tabs and runs of spaces as one space, no space at either end, and at most
// kBannerMaxText characters. Returns its length; 0 means nothing to show.
size_t clampBannerText(char (&out)[kBannerMaxText + 1], const char* in);

// The ring's geometry for a line `w` px wide, as pure arithmetic.
constexpr int bannerPeriod(int w) { return w + 120; }   // head to head: the line and half a screen
// How many copies a line `w` px wide gets, and how long they take to cross.
int bannerPasses(int w, int pxPerS);
uint32_t bannerShowMsForWidth(int w, int pxPerS);
uint32_t bannerShowMs(const Banner&, const char*);

// One frame, `ageMs` after it went up: fills the canvas, draws the copies on it.
void drawBanner(Canvas240&, const Banner&, const char*, uint32_t ageMs);

}  // namespace paint
