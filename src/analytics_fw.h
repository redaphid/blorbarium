// The firmware's half of the analytics (#50, lib/blorb analytics.h): what
// only the board knows, counted into the Dish's open bucket. Each call is an
// increment or a histogram bin; nothing here allocates, prints or writes
// flash except saveStats() as the badge goes dark (the panel is off then, so
// no frame waits on it). main.cpp includes this once, before setup(), after
// the statics it reads (dish, ble, updater).
#pragma once

#include <esp_system.h>

#include "blorb/analytics.h"
#include "latency.h"

namespace anxfw {

using namespace blorb;

// A gesture's latency, as DEBUG latency closes it (src/latency.h): to its
// result on the panel and to its first acknowledgement, in ms.
static void gestureDone(latency::Ctl, const uint16_t* tenthsMs, bool silent) {
  Analytics& a = dish->analytics();
  if (silent) a.add(anx::gestures_silent);
  const uint16_t panel = tenthsMs[uint8_t(latency::St::Panel)], ack = tenthsMs[uint8_t(latency::St::AckPanel)];
  if (panel != latency::Recorder::kNone) a.hist(hist::gesture_ms, panel / 10);
  if (ack != latency::Recorder::kNone) a.hist(hist::gesture_ack_ms, ack / 10);
}

// setup(), once the Dish is up: why the chip reset, whether the IMU answered,
// and whether the last update rolled back.
static void boot(bool imuFound, bool rolledBack) {
  Analytics& a = dish->analytics();
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: a.add(anx::reset_power_on); break;
    case ESP_RST_SW: a.add(anx::reset_software); break;
    case ESP_RST_PANIC: a.add(anx::reset_panic); break;
    case ESP_RST_INT_WDT: a.add(anx::reset_int_wdt); break;
    case ESP_RST_TASK_WDT: a.add(anx::reset_task_wdt); break;
    case ESP_RST_WDT: a.add(anx::reset_wdt); break;
    case ESP_RST_BROWNOUT: a.add(anx::reset_brownout); break;
    default: a.add(anx::reset_other); break;
  }
  if (!imuFound) a.add(anx::imu_missing);
  if (rolledBack) a.add(anx::ota_rolled_back);
  latency::onFinish = gestureDone;
}

// Every loop: a second of awake or low power, light sleep, the edges of low
// power, a phone connecting, and an update starting or ending.
static void loop(uint32_t now, bool dark, uint32_t slept) {
  static uint32_t secondAt = now, sleptWas = slept, bleGen = 0;
  static bool darkWas = false, otaWas = false;
  Analytics& a = dish->analytics();
  if (now - secondAt >= 1000) {
    const uint32_t secs = (now - secondAt) / 1000;
    secondAt += secs * 1000;
    a.add(dark ? anx::low_power : anx::awake, secs);
    if (slept - sleptWas >= 1000) {
      a.add(anx::light_sleep, (slept - sleptWas) / 1000);
      sleptWas += (slept - sleptWas) / 1000 * 1000;
    }
  }
  if (dark != darkWas) {
    darkWas = dark;
    a.add(dark ? anx::lows : anx::wakes);
    if (dark) dish->saveStats();   // nothing counted waits on the next save through a night in the dark
  }
  if (const uint32_t g = ble.generation(); g != bleGen) {
    if (ble.connected()) a.add(anx::ble_connects);
    bleGen = g;
  }
  const bool ota = updater.active();
  if (ota && !otaWas) a.add(anx::ota_begun);
  if (!ota && otaWas && !updater.rebootDue(millis())) a.add(anx::ota_failed);
  otaWas = ota;
}

}  // namespace anxfw
