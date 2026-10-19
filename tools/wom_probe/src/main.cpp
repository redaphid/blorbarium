// See platformio.ini. Prints everything on the serial line at 115200.
#include <Arduino.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include <esp_timer.h>

static uint8_t addr = 0;
static uint8_t rd(uint8_t reg) {
  Wire.beginTransmission(addr); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xEE;
  if (Wire.requestFrom(addr, uint8_t(1)) != 1) return 0xEE;
  return uint8_t(Wire.read());
}
static void wr(uint8_t reg, uint8_t v) { Wire.beginTransmission(addr); Wire.write(reg); Wire.write(v); Wire.endTransmission(); }
static bool cmd(uint8_t c) {
  wr(0x0A, c);
  uint32_t t = millis();
  while (!(rd(0x2D) & 0x80)) { if (millis() - t > 300) return false; delay(1); }
  wr(0x0A, 0x00);
  t = millis();
  while (rd(0x2D) & 0x80) { if (millis() - t > 300) return false; delay(1); }
  return true;
}

// Free pins (not flash/PSRAM 26-37, not UART0 43/44).
static const int kPins[] = {1,2,3,4,5,8,9,10,11,12,13,14,15,16,17,18,21,38,39,40,41,42,45,46,47,48};
static constexpr int kN = sizeof(kPins) / sizeof(kPins[0]);
static int lvl[kN];
static void snap(const char* tag) {
  Serial.printf("[probe] %s levels:", tag);
  for (int i = 0; i < kN; ++i) Serial.printf(" %d=%d", kPins[i], lvl[i] = digitalRead(kPins[i]));
  Serial.println();
}
// Watch for ms; report every edge and count per pin.
static int edges[kN];
static void watch(const char* tag, uint32_t ms) {
  for (int i = 0; i < kN; ++i) { lvl[i] = digitalRead(kPins[i]); edges[i] = 0; }
  const uint32_t t0 = millis();
  int printed = 0;
  while (millis() - t0 < ms) {
    for (int i = 0; i < kN; ++i) {
      const int v = digitalRead(kPins[i]);
      if (v != lvl[i]) {
        lvl[i] = v; ++edges[i];
        if (printed++ < 30) Serial.printf("[probe] %s t=%u gpio%d -> %d status1=%02x\n", tag, unsigned(millis() - t0), kPins[i], v, rd(0x2F));
      }
    }
    delay(1);
  }
  Serial.printf("[probe] %s edges:", tag);
  bool any = false;
  for (int i = 0; i < kN; ++i) if (edges[i]) { Serial.printf(" gpio%d=%d", kPins[i], edges[i]); any = true; }
  Serial.println(any ? "" : " none");
}
// Wake on motion: accel only, low-power ODR, threshold mg, routed to INT1 or INT2.
static bool wom(uint8_t mg, bool int2) {
  wr(0x08, 0x00);                 // CTRL7: sensors off
  wr(0x09, 0x80);                 // CTRL8: CTRL9 handshake by STATUSINT
  wr(0x03, 0x0D);                 // CTRL2: +-2 g, 21 Hz low-power
  wr(0x0B, mg);                   // CAL1_L: threshold, 1 mg/LSB
  wr(0x0C, uint8_t((int2 ? 0x80 : 0x00) | 0x02));   // CAL1_H: pin, initial 0, blank 2 samples
  const bool ok = cmd(0x08);      // CTRL_CMD_WRITE_WOM_SETTING
  wr(0x02, uint8_t(0x40 | (int2 ? 0x10 : 0x08)));   // CTRL1: auto-inc, INTx output enable
  wr(0x08, 0x01);                 // CTRL7: accel on
  Serial.printf("[probe] wom mg=%u pin=INT%d cmd=%d ctrl1=%02x ctrl2=%02x ctrl7=%02x\n", mg, int2 ? 2 : 1, int(ok), rd(0x02), rd(0x03), rd(0x08));
  return ok;
}
static void resetImu() { wr(0x60, 0xB0); delay(50); }   // RESET register: soft reset

RTC_DATA_ATTR static int wakePin = -1;
// Light sleep woken by the IMU's pin (any GPIO can wake light sleep).
// 2 mg: noise toggles it, so it should wake at once. 150 mg: a still badge
// should sleep to the 20 s timer.
static void lightOnce(uint8_t mg) {
  resetImu();
  wom(mg, false);
  delay(300);
  const int now = digitalRead(wakePin);
  gpio_wakeup_enable(gpio_num_t(wakePin), now ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(20ULL * 1000 * 1000);
  Serial.printf("[probe] light sleep mg=%u on gpio%d (now %d)\n", mg, wakePin, now);
  Serial.flush();
  const int64_t t0 = esp_timer_get_time();
  esp_light_sleep_start();
  const int64_t dt = esp_timer_get_time() - t0;
  const auto c = esp_sleep_get_wakeup_cause();
  gpio_wakeup_disable(gpio_num_t(wakePin));
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  Serial.printf("[probe] RESULT light mg=%u woke after %lld ms by %s\n", mg, dt / 1000,
                c == ESP_SLEEP_WAKEUP_GPIO ? "the IMU pin" : c == ESP_SLEEP_WAKEUP_TIMER ? "the timer" : "other");
}
static void lightSleepTrials() {
  if (wakePin < 0) return;
  lightOnce(2);
  lightOnce(2);
  lightOnce(150);
  Serial.println("[probe] now pick the badge up within 20 s:");
  lightOnce(150);
  Serial.println("[probe] done");
}
RTC_DATA_ATTR static int sleeps = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  const auto cause = esp_sleep_get_wakeup_cause();
  Serial.printf("\n[probe] boot cause=%d (2=ext0 3=ext1 4=timer) sleeps=%d wakePin=%d\n", int(cause), sleeps, wakePin);
  Wire.begin(6, 7, 400000);
  for (uint8_t a : {uint8_t(0x6B), uint8_t(0x6A)}) { addr = a; if (rd(0x00) == 0x05) break; addr = 0; }
  Serial.printf("[probe] imu addr=%02x\n", addr);
  if (!addr) return;
  for (int p : kPins) pinMode(p, INPUT);
  if (cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.printf("[probe] RESULT deep sleep woke by %s\n", cause == ESP_SLEEP_WAKEUP_EXT0 ? "the IMU pin (ext0)" : "the 20 s timer (no IMU wake)");
    Serial.println("[probe] done");
    return;
  }
  resetImu();
  wr(0x02, 0x40); wr(0x03, 0x05); wr(0x08, 0x01);   // normal: +-2 g 250 Hz, accel on, no INT outputs
  snap("normal");
  watch("normal", 3000);
  for (int route = 0; route < 2; ++route) {
    resetImu();
    wom(2, route == 1);
    snap(route ? "wom2-int2" : "wom2-int1");
    watch(route ? "wom2-int2" : "wom2-int1", 6000);
    for (int i = 0; i < kN; ++i) if (edges[i] >= 2 && wakePin < 0) wakePin = kPins[i];
  }
  resetImu();
  wom(150, true); watch("wom150-int2", 6000);
  resetImu();
  wom(150, false); watch("wom150-int1", 6000);
  Serial.printf("[probe] RESULT wom pin=%d (deep-sleep capable: %d)\n", wakePin, int(wakePin >= 0 && wakePin < 22));
  lightSleepTrials();
}
void loop() { delay(1000); }
