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
static constexpr int PIN_LCD_SCLK = 10;
static constexpr int PIN_LCD_MOSI = 11;
static constexpr int PIN_LCD_DC = 8;
static constexpr int PIN_LCD_CS = 9;
static constexpr int PIN_LCD_RST = 12;
static constexpr int PIN_LCD_BL = 40;   // not 2: that is the touch SKU
static constexpr int PIN_BOOT_BUTTON = 0;
static constexpr int PIN_IMU_SDA = 6;
static constexpr int PIN_IMU_SCL = 7;

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
      cfg.freq_write = 40000000;   // proven on this board; the wiki allows 80, untried
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
};

static void boardButtonBegin() { pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP); }
static bool boardButtonDown() { return digitalRead(PIN_BOOT_BUTTON) == LOW; }

// The canvas is native-endian RGB565, which is rgb565_t to LGFX: pushImage
// converts it to the panel's byte-swapped wire order on the way out.
static void boardPresent(BoardDisplay& display, const paint::Canvas240& canvas) {
  display.pushImage(0, 0, CANVAS_W, CANVAS_H, reinterpret_cast<const lgfx::rgb565_t*>(canvas.px));
}
