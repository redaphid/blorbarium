// The Waveshare ESP32-S3-LCD-1.28 (SKU 26541, non-touch): 240x240 GC9A01 over
// single SPI. claude-notification-screen's board_lcd128.h, proven on this
// board, cut to the seam sim/board_sim.h defines. Pins are the wiki's:
//   https://www.waveshare.com/wiki/ESP32-S3-LCD-1.28
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "paint/sprite_pack.h"

static constexpr int CANVAS_W = 240;
static constexpr int CANVAS_H = 240;
static constexpr const char* BOARD_NAME = "lcd128";   // what update images call this board (docs/ota.md)
static constexpr int PIN_LCD_SCLK = 10;
static constexpr int PIN_LCD_MOSI = 11;
static constexpr int PIN_LCD_DC = 8;
static constexpr int PIN_LCD_CS = 9;
static constexpr int PIN_LCD_RST = 12;
static constexpr int PIN_LCD_BL = 40;   // not 2: that is the touch SKU
static constexpr int PIN_BOOT_BUTTON = 0;
static constexpr int PIN_IMU_SDA = 6;
static constexpr int PIN_IMU_SCL = 7;
// How the IMU sits relative to the panel's own up (src/hw/orient.h). Only the
// board can settle this: if the screen lands a quarter turn off, add 1 (mod 4).
static constexpr int ORIENT_R0 = 1;
// Which settled rotation is the right way up as the kids hold the badge: in
// their cases the USB-C port faces up, which is rotation 2 (native bottom up).
// The hang is the opposite (USB-C down), the side hold a quarter turn either
// way. The screen turns with the badge whatever this says; only the gestures
// read it (main.cpp feel()). Some cases hold the board the other way round
// (USB-C down is up): env badge128_flip builds with BLORB_ORIENT_HOME=0
// (./flash.sh --flip).
#ifndef BLORB_ORIENT_HOME
#define BLORB_ORIENT_HOME 2
#endif
static_assert(BLORB_ORIENT_HOME == 0 || BLORB_ORIENT_HOME == 2, "home is USB-C up (2) or USB-C down (0)");
static constexpr uint8_t ORIENT_HOME = BLORB_ORIENT_HOME;

// The panel's SPI write clock, in MHz. 40 is proven on this board; the wiki
// allows 80, which halves a whole frame's push (23 ms at 40, 11.5 at 80). The
// clock is the 80 MHz APB divided, so 80, 40, 26, 20 are what it can be. A
// build may pin it without touching the code:
//   PLATFORMIO_BUILD_FLAGS="-DBLORB_LCD_SPI_MHZ=40" pio run -e badge128
// and `DEBUG lcd <MHz>` changes it until reset, to set the two side by side.
#ifndef BLORB_LCD_SPI_MHZ
#define BLORB_LCD_SPI_MHZ 80
#endif
static_assert(BLORB_LCD_SPI_MHZ >= 1 && BLORB_LCD_SPI_MHZ <= 80, "BLORB_LCD_SPI_MHZ: 1 to 80");

class BoardDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;

 public:
  BoardDisplay() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = BLORB_LCD_SPI_MHZ * 1000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_LCD_SCLK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = PIN_LCD_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = PIN_LCD_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = CANVAS_W;
      cfg.panel_height = CANVAS_H;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;   // without it the IPS panel shows colour negatives
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    setPanel(&panel_);
  }
  // Between frames only: the bus works out its divider again at the next write.
  void setWriteMhz(uint32_t mhz) {
    auto cfg = bus_.config();
    cfg.freq_write = mhz * 1000000;
    bus_.config(cfg);
  }
  uint32_t writeMhz() const { return bus_.config().freq_write / 1000000; }
};

static void boardButtonBegin() { pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP); }
static bool boardButtonDown() { return digitalRead(PIN_BOOT_BUTTON) == LOW; }

// The panel keeps what it was last sent in its own memory, so a frame sends
// only the rows that changed since the last: the line crossing, his breath, a
// hint filling, and none at all when nothing moved (between his 100 ms ticks,
// most frames). A row is known by a hash of its pixels, so no copy of the last
// frame is kept (115 KB the RAM does not have). Every kFullEvery frames, and
// after the panel sleeps, every row goes, so nothing a hash missed lasts.
namespace panelrows {
constexpr uint16_t kFullEvery = 64;
static uint32_t hash[CANVAS_H];
static uint16_t sinceFull = 0;
static bool stale = true;     // the next frame sends every row
static uint32_t sent = 0;     // rows sent since boot, for DEBUG fps
// FNV-1a over the row two pixels at a time: any one word changed always changes it.
inline uint32_t of(const uint16_t* p) {
  uint32_t h = 2166136261u;
  for (int x = 0; x < CANVAS_W; x += 2) h = (h ^ (uint32_t(p[x]) | uint32_t(p[x + 1]) << 16)) * 16777619u;
  return h;
}
}  // namespace panelrows

static void boardRepaintAll() { panelrows::stale = true; }
static uint32_t boardRowsSent() { return panelrows::sent; }

// The canvas is native-endian RGB565, which is rgb565_t to LGFX: pushImage
// converts it to the panel's byte-swapped wire order on the way out, a run of
// changed rows at a time.
static void boardPresent(BoardDisplay& display, const paint::Canvas240& canvas) {
  using namespace panelrows;
  const bool all = stale || ++sinceFull >= kFullEvery;
  if (all) stale = false, sinceFull = 0;
  const auto* px = reinterpret_cast<const lgfx::rgb565_t*>(canvas.px);
  display.startWrite();
  int from = -1;   // the first row of the run being gathered
  for (int y = 0; y <= CANVAS_H; ++y) {
    bool changed = false;
    if (y < CANVAS_H) {
      const uint32_t h = of(&canvas.px[y * CANVAS_W]);
      changed = all || h != hash[y];
      hash[y] = h;
    }
    if (changed && from < 0) from = y;
    if (!changed && from >= 0) {
      display.pushImage(0, from, CANVAS_W, y - from, px + from * CANVAS_W);
      sent += uint32_t(y - from);
      from = -1;
    }
  }
  display.endWrite();
}
