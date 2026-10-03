// A QMI8658 moved by a hand: the script's `!` lines (sim.cpp) and, with a
// window, the keyboard. src/hw/imu_qmi8658.h is the real driver and never
// knows: it probes WHO_AM_I, runs the CTRL9 handshake and reads the
// temperature, accelerometer and tap registers, and this answers all of it.
// The engine's detectors then see shakes, knocks, holds and lids for real at
// 50 Hz rather than being stubbed past.
#pragma once

#include <Arduino.h>

// Keys, as emulated pins. LovyanGFX maps the arrows itself (36..39); the rest
// are registered by sim.cpp. Pressed is LOW, like a pull-up.
static constexpr int SIM_PIN_UP = 36;
static constexpr int SIM_PIN_RIGHT = 37;
static constexpr int SIM_PIN_DOWN = 38;
static constexpr int SIM_PIN_LEFT = 39;
static constexpr int SIM_PIN_KNOCK = 21;   // k
static constexpr int SIM_PIN_DTAP = 22;    // d
static constexpr int SIM_PIN_SHAKE = 23;   // s, held: being shaken

static inline bool simKey(int pin) { return lgfx::v1::gpio_in(pin) == 0; }
static inline bool simBefore(uint32_t now, uint32_t until) { return int32_t(until - now) > 0; }

// What the hand is doing, in milli-g and milliseconds. Rest is the dish lying
// flat, screen up. Each gesture is chosen to cross the detectors' measured
// thresholds (lib/blorb/src/senses.cpp) with room to spare.
struct SimHand {
  int32_t tiltX = 0, tiltY = 0;        // !tilt: gravity in the screen plane
  bool lidded = false;                 // !lid: face down until a !tilt
  uint32_t lidUntil = 0, flipUntil = 0, holdUntil = 0, shakeUntil = 0;
  uint8_t tap = 0;                     // 1 knock, 2 double; latched until read
  int32_t dieMilliC = 25000;           // the IMU die warms in a hand
  uint32_t dieAt = 0;

  static constexpr int32_t kShakeMg = 1500;      // swung along x, flipping every 60 ms
  static constexpr int32_t kHoldY = -500, kHoldZ = 866;   // tipped 30 degrees toward you in a palm
  static constexpr int32_t kKeyTiltMg = 700;
  static constexpr int32_t kRoomMilliC = 25000, kHandMilliC = 29000;

  static int32_t isqrt(int32_t v) {
    if (v <= 0) return 0;
    int32_t r = 0, bit = 1 << 30;
    while (bit > v) bit >>= 2;
    for (; bit; bit >>= 2) {
      if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
    }
    return r;
  }

  bool faceDown(uint32_t now) const { return lidded || simBefore(now, lidUntil) || simBefore(now, flipUntil); }
  bool holding(uint32_t now) const { return simBefore(now, holdUntil); }
  bool shaking(uint32_t now) const { return simBefore(now, shakeUntil) || simKey(SIM_PIN_SHAKE); }

  void accel(uint32_t now, int32_t& x, int32_t& y, int32_t& z) const {
    int32_t tx = tiltX, ty = tiltY;
    if (simKey(SIM_PIN_LEFT)) tx = -kKeyTiltMg;
    if (simKey(SIM_PIN_RIGHT)) tx = kKeyTiltMg;
    if (simKey(SIM_PIN_UP)) ty = kKeyTiltMg;
    if (simKey(SIM_PIN_DOWN)) ty = -kKeyTiltMg;
    if (faceDown(now)) { x = 0; y = 0; z = -1000; }
    else if (holding(now)) { x = 0; y = kHoldY; z = kHoldZ; }
    else {
      x = tx;
      y = ty;
      z = isqrt(1000000 - tx * tx - ty * ty);
    }
    if (shaking(now)) x += (now / 60) % 2 ? kShakeMg : -kShakeMg;
  }

  // One degree a second up to the hand's warmth, a quarter of that back down.
  int32_t die(uint32_t now) {
    const int32_t dt = int32_t(now - dieAt);
    dieAt = now;
    if (holding(now)) dieMilliC = dieMilliC + dt < kHandMilliC ? dieMilliC + dt : kHandMilliC;
    else dieMilliC = dieMilliC - dt / 4 > kRoomMilliC ? dieMilliC - dt / 4 : kRoomMilliC;
    return dieMilliC;
  }
};

class SimWire {
  static constexpr uint8_t kAddr = 0x6B;   // SA0 high, the way Waveshare wires it
  static constexpr int32_t kLsbPerG = 16384;

  uint8_t addr_ = 0, reg_ = 0;
  uint8_t tx_[8], txn_ = 0;
  uint8_t rx_[16], rxn_ = 0, rxat_ = 0;
  bool cmdPending_ = false;   // CTRL9 issued, CmdDone not yet acknowledged
  bool wasKnock_ = false, wasDtap_ = false;
  int16_t sample_[4] = {};    // temperature, ax, ay, az, latched per read

  void pollKeys() {
    const bool knock = simKey(SIM_PIN_KNOCK), dtap = simKey(SIM_PIN_DTAP);
    if (knock && !wasKnock_) hand.tap = 1;
    if (dtap && !wasDtap_) hand.tap = 2;
    wasKnock_ = knock;
    wasDtap_ = dtap;
  }

  void latch() {
    const uint32_t now = millis();
    int32_t x, y, z;
    hand.accel(now, x, y, z);
    sample_[0] = int16_t(hand.die(now) * 256 / 1000);
    sample_[1] = int16_t(x * kLsbPerG / 1000);
    sample_[2] = int16_t(y * kLsbPerG / 1000);
    sample_[3] = int16_t(z * kLsbPerG / 1000);
  }

  uint8_t reg8(uint8_t reg) {
    if (reg == 0x00) return 0x05;                        // WHO_AM_I
    if (reg == 0x2D) return cmdPending_ ? 0x80 : 0x00;   // STATUSINT: CmdDone
    if (reg == 0x2F) return hand.tap ? 0x02 : 0x00;      // STATUS1: a tap was reported
    if (reg == 0x59) { const uint8_t t = hand.tap; hand.tap = 0; return t; }   // TAP_STATUS
    if (reg >= 0x33 && reg <= 0x3A) {                    // TEMP_L .. AZ_H
      const uint8_t off = reg - 0x33;
      const uint16_t raw = uint16_t(sample_[off / 2]);
      return (off & 1) ? uint8_t(raw >> 8) : uint8_t(raw & 0xFF);
    }
    return 0;
  }

 public:
  SimHand hand;

  void begin(int, int, uint32_t) {}
  void beginTransmission(uint8_t addr) { addr_ = addr; txn_ = 0; }
  size_t write(uint8_t b) {
    if (txn_ < sizeof(tx_)) tx_[txn_++] = b;
    return 1;
  }

  // 0 is an ack. The other address on the bus never answers, which is how the
  // driver's probe picks this one.
  uint8_t endTransmission(bool = true) {
    if (addr_ != kAddr) return 2;
    if (txn_) reg_ = tx_[0];
    if (txn_ >= 2 && tx_[0] == 0x0A) cmdPending_ = tx_[1] != 0x00;   // CTRL9 handshake
    return 0;
  }

  uint8_t requestFrom(uint8_t addr, uint8_t n) {
    if (addr != kAddr || n > sizeof(rx_)) return 0;
    pollKeys();
    if (reg_ <= 0x33 + 7 && reg_ + n > 0x33) latch();   // one consistent sample per block read
    for (uint8_t i = 0; i < n; i++) rx_[i] = reg8(uint8_t(reg_ + i));
    rxn_ = n;
    rxat_ = 0;
    return n;
  }

  int read() { return rxat_ < rxn_ ? rx_[rxat_++] : -1; }
};

static SimWire Wire;
