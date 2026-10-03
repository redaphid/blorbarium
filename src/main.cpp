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
#else
#error "the 1.28's panel, flash storage and BLE link arrive in build unit 20"
#endif

#include "hw/imu_qmi8658.h"

// The protocol over the serial line. A cable has no connect event, so the
// owner counts as near once a line has arrived.
class SerialLink : public blorb::Link {
  char line_[256];   // past the protocol's 200, so an overlong line still reads as overlong
  size_t len_ = 0;
  bool heard_ = false;

 public:
  bool connected() override { return heard_; }
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
    dish->sample(readBody(), now);
    lastSample = now;
  }
  dish->tick(now, phone);
}

void setup() {
  Serial.begin(115200);
  boardButtonBegin();
  imuOk = imu::begin(PIN_IMU_SDA, PIN_IMU_SCL, tapReady);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  uint64_t lineage = 0;
  for (uint8_t b : mac) lineage = lineage << 8 | b;
  dish.emplace(store, blorb::fnv1a(mac, sizeof(mac)), lineage);
  display.init();
}

void loop() {
  const uint32_t now = millis();
  step(now);
  if (now - lastFrame >= kFrameMs) {
    paint::draw(dish->appearance(), grungoPack(), canvas);
    boardPresent(display, canvas);
    lastFrame = now;
  }
}
