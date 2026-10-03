// The firmware loop, and the only file that includes Arduino, LovyanGFX or
// NimBLE. sim/sim.cpp includes this file whole, so the simulator runs this
// very loop and draws with these very objects.
#include <Arduino.h>
#include <esp_mac.h>

#include <optional>
#include <string_view>

#include "blorb/dish.h"
#include "grungo_pack.h"
#include "paint/sprite_pack.h"

#if defined(BADGE_BOARD_SIM)
#include "board_sim.h"
#include "mem_storage.h"
using PetStorage = blorbtest::MemStorage;   // every run founds the same pet: the goldens need it
#elif defined(BADGE_BOARD_LCD146)
#error "the 1.46's panel comes after build unit 20, which brings up the 1.28"
#else
#include <esp_heap_caps.h>
#include "hw/board_lcd128.h"
#include "hw/storage_nvs_fs.h"
#include "hw/system_clock.h"
using PetStorage = hw::NvsFsStorage;
#endif

#include "hw/imu_qmi8658.h"
#include "debug_verbs.h"

// The protocol over the serial line. A cable has no connect event, so the
// owner counts as near once a line has arrived. A `DEBUG ` line is held back
// from the protocol for the loop to run after the tick (src/debug_verbs.h).
class SerialLink : public blorb::Link {
  char line_[256];   // past the protocol's 200, so an overlong line still reads as overlong
  size_t len_ = 0;
  bool heard_ = false;
  char debug_[sizeof(line_) + 1] = "";
  char taken_[sizeof(line_) + 1] = "";

 public:
  bool connected() override { return heard_; }
  const char* takeDebug() {
    if (!debug_[0]) return nullptr;
    std::memcpy(taken_, debug_, sizeof(taken_));
    debug_[0] = 0;
    return taken_;
  }
  std::optional<std::string_view> readLine() override {
    while (Serial.available() > 0) {
      const int c = Serial.read();
      if (c == '\r') continue;
      if (c != '\n') {
        if (len_ < sizeof(line_)) line_[len_++] = char(c);
        continue;
      }
      heard_ = true;
      const size_t n = len_;
      len_ = 0;
      if (n > 6 && std::memcmp(line_, "DEBUG ", 6) == 0) {
        std::memcpy(debug_, line_ + 6, n - 6);
        debug_[n - 6] = 0;
        continue;
      }
      return std::string_view(line_, n);
    }
    return std::nullopt;
  }
  void writeLine(std::string_view s) override {
    Serial.write(s.data(), s.size());
    Serial.write("\n", 1);
  }
};

static constexpr uint32_t kFrameMs = 40;   // 25 fps: the SPI push is 23 ms of every frame

static PetStorage store;
static SerialLink phone;
static std::optional<blorb::Dish> dish;    // built in setup(), once storage is up
static BoardDisplay display;
static paint::Canvas240 canvas;
static bool imuOk = false, tapReady = false;
static uint32_t lastSample = 0, lastFrame = 0;
static blorb::BodySample lastBody;

static blorb::BodySample readBody() {
  blorb::BodySample s;
  if (!imuOk || !imu::read(s)) {
    s = blorb::BodySample{};
    s.az = 1000;   // no IMU: lying flat and still, so no gesture fires by accident
  }
  s.buttonDown = boardButtonDown();
  return s;
}

// The engine's half of the loop: sense at 50 Hz, then run whatever 100 ms
// ticks are due. The simulator's warp calls this with no frames between.
static void step(uint32_t now) {
  if (now - lastSample >= blorb::kSampleMs) {
    lastBody = readBody();
    dish->sample(lastBody, now);
    lastSample = now;
  }
  dish->tick(now, phone);
}

#if !defined(BADGE_BOARD_SIM)
// The loop task runs the Dish. The engine reached 4,816 B on x86-64 (DESIGN.md
// section 8); this is the starting size until the board's high-water mark sets it.
static constexpr size_t kDishStackBytes = 16 * 1024;
SET_LOOP_TASK_STACK_SIZE(kDishStackBytes);

// Diagnostics. Every line starts "[hw] " so nobody takes it for the protocol,
// whose lines start '#' or '!'.
static constexpr uint32_t kStatusMs = 10000;
static hw::StorageState storageState;
static hw::SystemClock systemClock;
static uint32_t lastStatus = 0;

static const char* bootName(blorb::Boot b) {
  constexpr const char* kNames[] = {"Resumed", "FellBack", "FromLineage", "Fresh", "ReadOnlyNewer"};
  return kNames[size_t(b)];
}

static const char* filesName(hw::StorageState::Files f) {
  switch (f) {
    case hw::StorageState::Files::Ok: return "ok";
    case hw::StorageState::Files::FormattedBlank: return "formatted-blank";
    case hw::StorageState::Files::Failed: return "FAILED: ";
  }
  return "?";
}

struct OccupantSummary { const char* kind; uint16_t gen; uint32_t genome, age; };

static OccupantSummary summarize(const blorb::Occupant& o) {
  struct Visit {
    OccupantSummary operator()(const blorb::Egg& e) const { return {"egg", e.generation(), e.genome().hash(), 0}; }
    OccupantSummary operator()(const blorb::Creature& c) const {
      return {"creature", c.generation(), c.genome().hash(), c.stats().ageTicks};
    }
    OccupantSummary operator()(const blorb::Clutch& k) const { return {"clutch", k.generation, k.parent.hash(), 0}; }
  };
  return std::visit(Visit{}, o);
}

static void reportBoot() {
  Serial.printf("[hw] blorbarium %s lcd128\n", BLORB_FW);
  Serial.printf("[hw] psram size=%u free=%u\n", unsigned(ESP.getPsramSize()), unsigned(ESP.getFreePsram()));
  Serial.printf("[hw] flash chip=%u\n", unsigned(ESP.getFlashChipSize()));
  const bool filesFailed = storageState.files == hw::StorageState::Files::Failed;
  Serial.printf("[hw] storage slots=%s files=%s%s\n", storageState.slots ? "ok" : "FAILED",
                filesName(storageState.files), filesFailed ? storageState.why : "");
  if (!storageState.slots) Serial.println("[hw] !!! the pet partition will not open: no snapshot loads or saves");
  if (std::optional<uint32_t> t = systemClock.unixSeconds()) Serial.printf("[hw] clock=%u\n", unsigned(*t));
  else Serial.println("[hw] clock=unset");
  Serial.printf("[hw] imu %s tap=%s\n", imuOk ? "ok" : "missing", tapReady ? "ok" : "off");
  const OccupantSummary o = summarize(dish->occupant());
  Serial.printf("[hw] boot=%s occupant=%s gen=%u genome=%08x age=%u\n", bootName(dish->boot()), o.kind,
                unsigned(o.gen), unsigned(o.genome), unsigned(o.age));
}

// From the last BodySample the loop took: no second I2C read.
static void reportStatus(uint32_t now) {
  if (now - lastStatus < kStatusMs) return;
  lastStatus = now;
  const OccupantSummary o = summarize(dish->occupant());
  char temp[12] = "?";
  if (lastBody.tempCx10 != INT16_MIN) std::snprintf(temp, sizeof temp, "%.1f", lastBody.tempCx10 / 10.0);
  Serial.printf(
      "[hw] up=%u heap=%u minheap=%u big=%u psram_free=%u stack_hwm=%u writes=%u occupant=%s gen=%u genome=%08x "
      "age=%u imu ax=%d ay=%d az=%d mg t=%sC tap=%u button=%d\n",
      unsigned(now / 1000), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      unsigned(ESP.getFreePsram()), unsigned(uxTaskGetStackHighWaterMark(nullptr)),
      unsigned(store.writes()), o.kind, unsigned(o.gen), unsigned(o.genome), unsigned(o.age), lastBody.ax,
      lastBody.ay, lastBody.az, temp, unsigned(lastBody.tapCode), lastBody.buttonDown ? 1 : 0);
}
#endif

void setup() {
  Serial.begin(115200);
  boardButtonBegin();
  imuOk = imu::begin(PIN_IMU_SDA, PIN_IMU_SCL, tapReady);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  uint64_t lineage = 0;
  for (uint8_t b : mac) lineage = lineage << 8 | b;
  blorb::DishOptions options;
#if !defined(BADGE_BOARD_SIM)
  storageState = store.begin();
  options.rtc = &systemClock;
#endif
  dish.emplace(store, blorb::fnv1a(mac, sizeof(mac)), lineage, options);
  display.init();
#if !defined(BADGE_BOARD_SIM)
  display.setBrightness(dish->settings().brightness);
  reportBoot();
#endif
}

#if !defined(BADGE_BOARD_SIM)
// Powered time a DEBUG warp ran ahead of the wall: the loop's clock is millis() plus this.
static uint32_t warpMs = 0;

static void warp(uint32_t ticks) {
  for (uint32_t done = 0; done < ticks; done += 10) {
    warpMs += 10 * blorb::kTickMs;
    dish->tick(millis() + warpMs, phone);   // at most 10 ticks a call
    if (done % 2000 == 0) delay(1);          // let the idle task run
  }
}

static void runDebug(const char* line) {
  char verb[16] = "", a[32] = "";
  std::sscanf(line, "%15s %31s", verb, a);
  if (std::strcmp(verb, "stage") != 0) {
    Serial.printf("[hw] debug: unknown verb '%s' (try: DEBUG stage adult)\n", verb);
    return;
  }
  const uint32_t t0 = millis();
  const char* why = debugverbs::growTo(*dish, a, warp);
  const OccupantSummary o = summarize(dish->occupant());
  const auto* c = std::get_if<blorb::Creature>(&dish->occupant());
  constexpr const char* kStage[] = {"hatchling", "child", "adult", "elder"};
  Serial.printf("[hw] debug stage %s: %s stage=%s occupant=%s gen=%u genome=%08x age=%u writes=%u in %u ms\n", a,
                why ? why : "ok", c ? kStage[size_t(c->stage())] : "-", o.kind, unsigned(o.gen), unsigned(o.genome),
                unsigned(o.age), unsigned(store.writes()), unsigned(millis() - t0));
}
#endif

void loop() {
#if !defined(BADGE_BOARD_SIM)
  if (const char* line = phone.takeDebug()) runDebug(line);
  const uint32_t now = millis() + warpMs;
#else
  const uint32_t now = millis();
#endif
  step(now);
  if (now - lastFrame >= kFrameMs) {
    paint::draw(dish->appearance(), grungoPack(), canvas);
    boardPresent(display, canvas);
    lastFrame = now;
  }
#if !defined(BADGE_BOARD_SIM)
  // The same anchor the Dish holds, so a soft reset's rtc reading counts only the unsaved gap.
  if (dish->timeKnown() && !hw::SystemClock::isSet())
    if (std::optional<uint32_t> wall = dish->wallNow()) hw::SystemClock::set(*wall);
  reportStatus(now);
#endif
}
