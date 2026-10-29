// The debug link's firmware half (docs/debug-ble.md): its characteristic, the
// Board the console asks, the unlock's question on the screen and its press,
// the time scale's extra ticks and WIPE's restart. The commands themselves
// are lib/debugble. main.cpp includes this once, after the loop's statics it
// uses (dish, ble, updater, trial, store, warp, pinnedSpecies), so it is part
// of that one translation unit and of every build, release included.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>

#include "blorb/founding.h"
#include "debugble/console.h"
#include "hw/debug_char.h"
#include "hw/system_clock.h"
#include "paint/text.h"

namespace dbgfw {

// Of each loop, at most this long goes to a time scale's extra ticks, so the
// frame, the radio and the trial's watchdog still get theirs.
constexpr uint32_t kScaleBudgetMs = 40;
constexpr uint32_t kScaleChunk = 100;      // ticks a warp() call, which yields once a call
constexpr uint32_t kWipeAfterMs = 500;     // so WIPE's OK leaves before the radio goes

static hw::DebugChar link(ble);   // joins the service when ble.begin() builds it
static std::optional<uint32_t> wipeAt;
static std::optional<blorb::SpeciesId> wipeFound;

struct Board : debugble::Board {
  bool onTrial() override { return trial.onTrial(); }
  bool otaActive() override { return updater.active(); }
  void warp(uint32_t ticks) override { ::warp(ticks); }
  void look(std::optional<blorb::SpeciesId> s) override { pinnedSpecies = s; }
  std::optional<blorb::SpeciesId> looks() override { return pinnedSpecies; }
  // 0 reads as unset (SystemClock::kPlausible), as after a power cut.
  void setClock(std::optional<uint32_t> t) override { hw::SystemClock::set(t.value_or(0)); }
  bool wipe(std::optional<blorb::SpeciesId> found) override {
    wipeFound = found;
    wipeAt = millis() + kWipeAfterMs;
    return true;
  }

  // The device itself: what the cable's DEBUG verbs do (main.cpp).
  bool hasDevice() override { return true; }
  bool power(PowerAsk ask, uint32_t idleMs, PowerView& v) override {
    if (!::powerAsk(ask, idleMs)) return false;
    const uint32_t now = millis() + warpMs;
    v = PowerView{powerPolicy.low(), power::whyName(powerPolicy.why()), powerPolicy.idleMs(), powerPolicy.msLeft(now),
                  cpuMaxMhz, framesDrawn, powerPolicy.changes(), powerHeld(), sleepState(), sleptMs()};
    return true;
  }
  bool press(uint32_t ms, bool twice) override { return remotePress(ms, twice); }
  bool gesture(Gesture g, uint32_t ms, uint8_t arg) override {
    if (g == Gesture::Hang) hangFor(ms, arg != 0);
    else if (g == Gesture::Side) sideFor(ms, arg != 0);
    else if (g == Gesture::Knock) knockOnce(arg);
    else if (g == Gesture::Cradle) cradleFor(ms);
    else leanFor(arg, ms);
    return true;
  }
  bool net(std::string_view args, char* report, size_t n) override {
    char line[96];
    std::snprintf(line, sizeof line, "net%.*s", int(args.size()), args.data());
    if (!::net(line)) return false;
    const hw::FaultAir::Config& c = faultAir->cfg;
    std::snprintf(report, n, "rx_drop=%u tx_drop=%u dup=%u delay=%u jitter=%u ghosts=%u blackout=%d open=%d",
                  c.rxDrop, c.txDrop, c.dup, c.delayMs, c.jitterMs, c.ghosts, int(c.deaf || faultAir->blackedOut()),
                  int(faultAir->isOpen()));
    return true;
  }
  bool reboot(bool quiet) override {
    rebootAt = millis() + kWipeAfterMs;   // after the reply has gone, as WIPE
    rebootQuiet = quiet;
    return true;
  }
  size_t wifiLog(char* out, size_t cap) override { return wifiLogText(out, cap); }
  std::optional<uint32_t> rebootAt;
  bool rebootQuiet = false;
};
static Board board;
static debugble::Console console(board);

// Erase, provision as a fresh board, pin, restart. Nothing ticks the Dish
// between the erase and the restart, so the old pet in RAM is never saved
// over the fresh flash.
[[noreturn]] static void wipeNow() {
  Serial.printf("[hw] debug wipe: erasing the pet, the founding egg is %s\n",
                wipeFound ? blorb::speciesInfo(*wipeFound).name : "the nest's choice");
  Serial.flush();
  const blorb::Provisioned p = store.wipe();
  Serial.printf("[hw] debug wipe: pet %s; petfs %s\n", blorb::describe(p.slots), blorb::describe(p.files));
  if (wipeFound)
    Serial.printf("[hw] debug wipe: found.pin %s\n", blorb::writeFoundPin(store, *wipeFound) ? "written" : "FAILED");
  Serial.println("[hw] debug wipe: restarting as a fresh board");
  Serial.flush();
  ESP.restart();
  for (;;) {}
}

// The scale's extra ticks, run as warps: real ticks, so his age is what he lived.
static void runScaled() {
  const uint32_t owed = console.scale().owed(millis());
  const uint32_t t0 = millis();
  uint32_t ran = 0;
  while (owed - ran >= 10 && millis() - t0 < kScaleBudgetMs) {
    const uint32_t n = std::min(kScaleChunk, (owed - ran) / 10 * 10);
    ::warp(n);
    ran += n;
  }
  console.scale().ran(ran);
}

// Every loop, after the Dish's own step.
static void poll() {
  const std::optional<uint32_t> live = ble.connected() ? std::optional<uint32_t>(ble.generation()) : std::nullopt;
  console.tick(millis(), live, link);
  while (std::optional<blorb::RxLines<hw::DebugChar::kRxMax>::Line> line = link.take()) {
    if (line->connection != live) continue;   // its phone is gone: nobody to answer
    ble.spoke();   // a debug session is a page talking: never evict it as a squatter
    Serial.printf("[hw] debug ble: %.60s\n", std::string(line->text).c_str());
    console.handle(line->text, line->connection, millis(), *dish, link);
  }
  runScaled();
  if (wipeAt && int32_t(millis() - *wipeAt) >= 0) wipeNow();
  if (board.rebootAt && int32_t(millis() - *board.rebootAt) >= 0) rebootNow(board.rebootQuiet);
}

// Whether draw() below draws anything (a frame with it is never skipped as unchanged).
static bool drawing() { return console.unlock().asking(); }

// The unlock's question, drawn as the update's is (lib/ota/src/screen.cpp):
// a band over the pet, a rim ring that runs down, amber because it asks.
static void draw(paint::Canvas240& cv) {
  const uint32_t left = console.unlock().msLeft(millis());
  if (!console.unlock().asking()) return;
  constexpr uint16_t kAsk = 0xFDC6, kInk = 0xFFFF;   // ota/screen.cpp's amber and white
  constexpr paint::Clip kAll{0, 0, 240, 240, 0};
  for (int y = 82; y < 158; ++y)
    for (int x = 0; x < 240; ++x) cv.px[y * 240 + x] = paint::blend(cv.px[y * 240 + x], 0, 200);
  const int steps = int(1200 * uint64_t(left) / debugble::Unlock::kAskMs);
  for (int i = 0; i < steps; ++i) {
    const float a = float(i) / 1200 * 6.2831853f - 1.5707963f;
    for (int r = 112; r <= 118; ++r) {
      const int x = 120 + int(std::lround(r * std::cos(a))), y = 120 + int(std::lround(r * std::sin(a)));
      if (x >= 0 && x < 240 && y >= 0 && y < 240) cv.px[y * 240 + x] = kAsk;
    }
  }
  for (const auto& [text, y, colour] : {std::tuple{"Debug unlock?", 106, kAsk}, std::tuple{"Press the button", 134, kInk}})
    paint::drawString(cv, paint::kFreeSans9pt7b, text, 120 - paint::textWidth(paint::kFreeSans9pt7b, text) / 2, y,
                      colour, kAll);
}

}  // namespace dbgfw

static bool debugAsks() { return dbgfw::console.unlock().asking(); }
static bool debugPress(uint32_t ms) { return dbgfw::console.press(ms); }
