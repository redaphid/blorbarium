// Renders one frame of the dish with the time-unknown marquee showing, as a
// binary PPM on stdout. render_marquee.py builds it against a copy of
// draw.cpp whose marquee string is swapped for a dialogue line.
#include <cstdio>
#include <cstdlib>
#include <memory>
#include "blorb/appearance.h"
#include "paint/sprite_pack.h"

int main(int argc, char** argv) {
  blorb::Appearance a{};
  a.stage = blorb::Stage::Adult;
  a.timeUnknown = true;
  a.poseTick = uint16_t(argc > 1 ? std::atoi(argv[1]) : 0);
  auto cv = std::make_unique<paint::Canvas240>();
  paint::draw(a, paint::placeholderPack(), *cv);
  std::printf("P6\n240 240\n255\n");
  for (uint16_t p : cv->px) {
    unsigned char rgb[3] = {uint8_t(((p >> 11) & 31) * 255 / 31), uint8_t(((p >> 5) & 63) * 255 / 63),
                            uint8_t((p & 31) * 255 / 31)};
    std::fwrite(rgb, 1, 3, stdout);
  }
}
