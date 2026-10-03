// The 1.28 as a window: a panel, the pins the firmware reaches for outside
// it, and the blit. Nothing above this header knows it is not glass.
#pragma once

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include "paint/sprite_pack.h"

static constexpr int CANVAS_W = 240;
static constexpr int CANVAS_H = 240;
static constexpr int PIN_BOOT_BUTTON = 0;
static constexpr int PIN_IMU_SDA = 6;
static constexpr int PIN_IMU_SCL = 7;
static constexpr int ORIENT_R0 = 1;   // sim/Wire.h's hand reads in the panel's own axes

#ifndef SIM_SCALE
#define SIM_SCALE 2   // 240 px of dish is a postage stamp on a desktop display
#endif

// LovyanGFX rescales x and y separately when its window is resized, so
// dragging a corner turns the dish into an oval. Take the smaller scale for
// both and the dish stays round.
struct SquarePanel : public lgfx::Panel_sdl {
  void keepSquare() {
    const float s = monitor.scaling_x < monitor.scaling_y ? monitor.scaling_x : monitor.scaling_y;
    if (monitor.scaling_x == s && monitor.scaling_y == s) return;
    monitor.scaling_x = monitor.scaling_y = s;
    sdl_invalidate();
  }
};

class BoardDisplay : public lgfx::LGFX_Device {
  SquarePanel panel_;

 public:
  BoardDisplay() {
    auto cfg = panel_.config();
    cfg.memory_width = cfg.panel_width = CANVAS_W;
    cfg.memory_height = cfg.panel_height = CANVAS_H;
    panel_.config(cfg);
    panel_.setScaling(SIM_SCALE, SIM_SCALE);
    panel_.setWindowTitle("blorbarium");
    setPanel(&panel_);
  }
  void keepSquare() { panel_.keepSquare(); }
};

static void boardButtonBegin() { pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP); }
static bool boardButtonDown() { return digitalRead(PIN_BOOT_BUTTON) == LOW; }

// The canvas is native-endian RGB565 (lib/paint), which is rgb565_t to LGFX.
static void boardPresent(BoardDisplay& display, const paint::Canvas240& canvas) {
  display.keepSquare();
  display.pushImage(0, 0, CANVAS_W, CANVAS_H, reinterpret_cast<const lgfx::rgb565_t*>(canvas.px));
}
