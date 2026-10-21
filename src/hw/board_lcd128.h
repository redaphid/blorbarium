// The Waveshare ESP32-S3-LCD-1.28 (SKU 26541, non-touch): 240x240 GC9A01 over
// single SPI. claude-notification-screen's board_lcd128.h, proven on this
// board, cut to the seam sim/board_sim.h defines. Pins are the wiki's:
//   https://www.waveshare.com/wiki/ESP32-S3-LCD-1.28
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>

#include <cstring>

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

// The panel's SPI write clock, in MHz: 40. The panel's clock is on GPIO10,
// not SPI2's IO-MUX clock pin (GPIO12), so the bus goes through the GPIO
// matrix, which IDF 4.4 holds to 40 MHz (spi_master.h) and LovyanGFX does not
// clamp. A command byte (MADCTL, INVON, RASET) garbled at 80 is never healed by
// a repaint, so 80 is opt-in, to try on a bench badge:
//   PLATFORMIO_BUILD_FLAGS="-DBLORB_LCD_SPI_MHZ=80" pio run -e badge128
// or `DEBUG lcd 80` until reset. The clock is the 80 MHz APB divided by a
// whole number, so 80, 40, 26.7, 20 are what it can be; writeKhz() says which.
#ifndef BLORB_LCD_SPI_MHZ
#define BLORB_LCD_SPI_MHZ 40
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
  // The clock the bus really runs at, kHz, worked out as LovyanGFX's divider
  // is (FreqToClockDiv, platforms/esp32/common.cpp): the APB over (pre + 1)(n + 1).
  uint32_t writeKhz() const {
    const uint32_t apb = getApbFrequency(), asked = bus_.config().freq_write;
    if (asked >= apb) return apb / 1000;
    const uint32_t div = apb / (asked + 1), pre = std::min<uint32_t>(div / 64, 8191), n = std::min<uint32_t>(div / (pre + 1), 63);
    return apb / ((pre + 1) * (n + 1)) / 1000;
  }
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
// FNV-1a over the row two pixels at a time, with the high bits folded back
// down after each multiply: a multiply only carries upward, so without the fold
// a change in a word's top bits (red at an odd x) was lost to the next word's
// multiply ~3% of the time, and two of them 99%. scripts/test/row_hash.cpp
// throws high-bit-only changes at every word: no misses.
inline uint32_t of(const uint16_t* p) {
  uint32_t h = 2166136261u;
  for (int x = 0; x < CANVAS_W; x += 2) {
    h = (h ^ (uint32_t(p[x]) | uint32_t(p[x + 1]) << 16)) * 16777619u;
    h ^= h >> 15;
  }
  return h;
}
}  // namespace panelrows

static void boardRepaintAll() { panelrows::stale = true; }
static bool boardStale() { return panelrows::stale; }   // the next frame must send every row
static uint32_t boardRowsSent() { return panelrows::sent; }

// The send runs on core 0 while the loop draws the next frame on core 1: a
// frame's changed rows are copied into a shadow of the panel in PSRAM (the
// canvas is internal and drawn over at once), and a task sends them from
// there, so drawing waits on the panel only when the last frame's rows are
// still going out. At 40 MHz a hang's ~95 rows are ~9 ms of SPI the draw no
// longer waits through. No PSRAM, or `DEBUG lcd sync`: sent in place, as before.
namespace panelsend {
struct Run { uint8_t from, rows; };
static uint16_t* shadow = nullptr;          // CANVAS_W x CANVAS_H in PSRAM, the panel's rows as last copied
static Run runs[CANVAS_H / 2 + 1];          // runs of changed rows, at most every other row
static int runCount = 0;
static BoardDisplay* to = nullptr;
static TaskHandle_t task = nullptr;
static SemaphoreHandle_t idle = nullptr;    // held while a send is under way
static bool async = true;
static volatile uint32_t sendUs = 0;        // time on the bus since boot, us (the task's, for DEBUG fps)
// Frames that sent rows, numbered from 1, and the last one out on the panel
// and when (micros), for DEBUG latency: a frame's rows reach the glass when
// the send task is done with them, not when the loop hands them over.
// The send task writes a frame's time before its number, so a reader on the
// other core that sees the number finds its time (never the semaphore: the
// loop's next frame holds it while the send runs, nearly always).
static uint32_t queuedSeq = 0;
static volatile uint32_t doneSeq = 0, sendingSeq = 0;
static volatile uint32_t doneAt[8] = {};   // by seq % 8

static void send(BoardDisplay& display, const uint16_t* px) {
  display.startWrite();
  for (int i = 0; i < runCount; ++i)
    display.pushImage(0, runs[i].from, CANVAS_W, runs[i].rows,
                      reinterpret_cast<const lgfx::rgb565_t*>(px + runs[i].from * CANVAS_W));
  display.endWrite();
}

static void loop(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const uint32_t t0 = micros();
    send(*to, shadow);
    const uint32_t t1 = micros();
    sendUs += t1 - t0;
    const uint32_t seq = sendingSeq;
    doneAt[seq % 8] = t1;
    doneSeq = seq;
    xSemaphoreGive(idle);
  }
}
}  // namespace panelsend

// Before anything else talks to the panel (sleep, rotation, its clock): the last frame's send is done.
static void boardPanelIdle() {
  using namespace panelsend;
  if (idle && xSemaphoreTake(idle, portMAX_DELAY) == pdTRUE) xSemaphoreGive(idle);
}

// After display.init(): the shadow and the send task, if PSRAM has room.
static void boardPresentBegin(BoardDisplay& display) {
  using namespace panelsend;
  to = &display;
  shadow = static_cast<uint16_t*>(heap_caps_malloc(sizeof(uint16_t) * CANVAS_W * CANVAS_H, MALLOC_CAP_SPIRAM));
  idle = xSemaphoreCreateBinary();
  if (!shadow || !idle) return void(async = false);
  xSemaphoreGive(idle);
  // Core 0, beside the radio's tasks (which outrank it); the loop and its drawing are core 1's.
  if (xTaskCreatePinnedToCore(loop, "lcdsend", 3072, nullptr, 1, &task, 0) != pdPASS) async = false;
}
static bool boardSendAsync() { return panelsend::async && panelsend::task; }
static void boardSetSendAsync(bool on) {
  boardPanelIdle();
  panelsend::async = on;
}
static uint32_t boardSendUs() { return panelsend::sendUs; }
// The newest frame that sent the panel rows (0: none yet), the newest whose
// rows are out, and when each of the last eight went out (micros).
static uint32_t boardFrameSeq() { return panelsend::queuedSeq; }
static uint32_t boardPanelDoneSeq() { return panelsend::doneSeq; }
static uint32_t boardPanelDoneAt(uint32_t seq) { return panelsend::doneAt[seq % 8]; }

// The canvas is native-endian RGB565, which is rgb565_t to LGFX: pushImage
// converts it to the panel's byte-swapped wire order on the way out, a run of
// changed rows at a time.
static void boardPresent(BoardDisplay& display, const paint::Canvas240& canvas) {
  using namespace panelrows;
  using namespace panelsend;
  const bool all = stale || ++sinceFull >= kFullEvery;
  if (all) stale = false, sinceFull = 0;
  const bool background = boardSendAsync();
  if (background) xSemaphoreTake(idle, portMAX_DELAY);   // the last frame's rows are out; the shadow is free
  runCount = 0;
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
      runs[runCount++] = {uint8_t(from), uint8_t(y - from)};
      sent += uint32_t(y - from);
      from = -1;
    }
  }
  if (runCount) ++queuedSeq;
  if (!background) {
    if (runCount) send(display, canvas.px), doneAt[queuedSeq % 8] = micros(), doneSeq = queuedSeq;
    return;
  }
  if (!runCount) return void(xSemaphoreGive(idle));
  sendingSeq = queuedSeq;
  for (int i = 0; i < runCount; ++i)
    std::memcpy(shadow + runs[i].from * CANVAS_W, canvas.px + runs[i].from * CANVAS_W,
                sizeof(uint16_t) * CANVAS_W * runs[i].rows);
  xTaskNotifyGive(task);   // the task gives idle back when the rows are out
}
