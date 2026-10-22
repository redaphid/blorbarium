// The renderer's second core (paint/parallel.h): the loop draws on core 1, and
// a split background or big sprite hands its other half to a task on core 0,
// rows apart, so the frame is the same bytes with one core or two.
//
// The task sits at priority 1: under the sense task (2) and far under the
// radio's (NimBLE, Wi-Fi, ESP-NOW), level with the panel's send task, which it
// shares core 0 with. Between halves it blocks, so IDLE0 runs (the task
// watchdog watches it), and in low power no frame is drawn, so it stays
// blocked and light sleep and the pm locks behave as before.
#pragma once
#include <Arduino.h>

#include "paint/parallel.h"

namespace paintcore0 {
static TaskHandle_t task = nullptr;
static SemaphoreHandle_t done = nullptr;
static paint::Half half = nullptr;
static void* job = nullptr;
static bool on = false;   // DEBUG cores 1|2

static void loop(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    half(job, 0);
    xSemaphoreGive(done);
  }
}

static void run(paint::Half h, void* j) {
  half = h, job = j;
  xTaskNotifyGive(task);
  h(j, 1);
  xSemaphoreTake(done, portMAX_DELAY);
}

// Two cores (true) or one: only between frames, from the loop.
static void use(bool two) {
  on = two && task;
  paint::parallel.run = on ? run : nullptr;
}

static void begin() {
  done = xSemaphoreCreateBinary();
  if (done && xTaskCreatePinnedToCore(loop, "paint0", 4096, nullptr, 1, &task, 0) != pdPASS) task = nullptr;
  use(true);
}
}  // namespace paintcore0
