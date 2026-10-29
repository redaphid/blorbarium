// The QMI8658 on the 1.28, as raw reads: claude-notification-screen's
// orient.h with every gesture removed, because the engine's detectors
// (lib/blorb/src/senses.cpp) own the gestures now. This only fills a
// BodySample. Registers and tap settings are orient.h's, which were measured
// on the board: https://www.waveshare.com/wiki/ESP32-S3-LCD-1.28
#pragma once

#include <Arduino.h>
#include <cstring>
#include <Wire.h>

#include "blorb/senses.h"

namespace imu {

// SA0 decides the address and Waveshare ties it high, but a board that came
// out the other way still answers, so ask both once at boot.
constexpr uint8_t kAddrs[2] = {0x6B, 0x6A};
constexpr uint8_t kWhoAmI = 0x00, kWhoAmIValue = 0x05;
constexpr uint8_t kCtrl1 = 0x02, kCtrl2 = 0x03, kCtrl7 = 0x08, kCtrl8 = 0x09, kCtrl9 = 0x0A;
constexpr uint8_t kCal1L = 0x0B;   // CAL1_L..CAL4_H run 0x0B..0x12
constexpr uint8_t kStatusInt = 0x2D, kStatus0 = 0x2E, kStatus1 = 0x2F, kTimestampL = 0x30, kTempL = 0x33, kTapStatus = 0x59;
constexpr uint8_t kReset = 0x60, kResetValue = 0xB0, kResetDone = 0x4D, kResetDoneValue = 0x80;
constexpr uint8_t kCmdConfigureTap = 0x0C, kCmdAck = 0x00;
constexpr int32_t kLsbPerG = 16384;   // +-2 g

constexpr uint8_t kTapPeakWindow = 10;     // 40 ms
constexpr uint16_t kTapWindow = 25;        // 100 ms quiet after a tap
constexpr uint16_t kTapDtapWindow = 125;   // second tap within 500 ms
constexpr uint8_t kTapAlpha = 8;           // 0.0625 * 128
constexpr uint8_t kTapGamma = 32;          // 0.25 * 128
constexpr uint16_t kTapPeakMag = 512;      // 0.5 g^2
constexpr uint16_t kTapUdm = 0x0190;       // 0.4 g^2

inline uint8_t addr = 0;

inline uint8_t probe(uint8_t a, uint8_t reg) {
  Wire.beginTransmission(a);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom(a, uint8_t(1)) != 1) return 0;
  return uint8_t(Wire.read());
}

inline void put(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// CTRL9 protocol, datasheet 5.10: write the command, wait for CmdDone, ack,
// wait for it to clear. A timeout leaves the tap engine unconfigured, which
// costs only knocks.
inline bool command(uint8_t cmd) {
  put(kCtrl9, cmd);
  uint32_t t0 = millis();
  while (!(probe(addr, kStatusInt) & 0x80)) {
    if (millis() - t0 > 200) return false;
    delay(1);
  }
  put(kCtrl9, kCmdAck);
  t0 = millis();
  while (probe(addr, kStatusInt) & 0x80) {
    if (millis() - t0 > 200) return false;
    delay(1);
  }
  return true;
}

// Datasheet table 36: two parameter sets through CAL1..CAL4, CAL4_H naming
// the set, each followed by CTRL_CMD_CONFIGURE_TAP.
inline bool configureTap() {
  put(kCal1L + 0, kTapPeakWindow);
  put(kCal1L + 1, 4);   // priority Z > X > Y: a desk tap arrives through the face
  put(kCal1L + 2, kTapWindow & 0xFF);
  put(kCal1L + 3, kTapWindow >> 8);
  put(kCal1L + 4, kTapDtapWindow & 0xFF);
  put(kCal1L + 5, kTapDtapWindow >> 8);
  put(kCal1L + 7, 0x01);
  if (!command(kCmdConfigureTap)) return false;
  put(kCal1L + 0, kTapAlpha);
  put(kCal1L + 1, kTapGamma);
  put(kCal1L + 2, kTapPeakMag & 0xFF);
  put(kCal1L + 3, kTapPeakMag >> 8);
  put(kCal1L + 4, kTapUdm & 0xFF);
  put(kCal1L + 5, kTapUdm >> 8);
  put(kCal1L + 7, 0x02);
  return command(kCmdConfigureTap);
}

// What the chip was doing when the ESP booted, before the soft reset: an ESP
// reset (a flash, a crash) does not reset the QMI8658, and moth's kept
// returning one frozen sample (ax=228 ay=109 az=941 t=30.9C, to the digit)
// through four reboots and a pick-up after a reset caught it in wake on motion
// (2026-10-28). The boot line says whether it was converting.
struct BootState {
  uint8_t ctrl1 = 0, ctrl2 = 0, ctrl7 = 0, ctrl8 = 0, status0 = 0;
  uint32_t tsStep = 0;   // the chip's sample counter over 30 ms: 0 is a chip that stopped
  bool dataMoved = false;
  bool resetOk = false;
};
inline BootState bootState;
inline bool tapOk = false;
inline uint32_t resets = 0;   // soft resets after boot's: frozen data found by read()

inline bool block(uint8_t reg, uint8_t* b, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; ++i) b[i] = uint8_t(Wire.read());
  return true;
}

// Datasheet 5.12: 0xB0 to RESET puts every register back as at power on; 0x4D
// reads 0x80 once it is done (about 15 ms).
inline bool softReset() {
  put(kReset, kResetValue);
  delay(15);
  for (const uint32_t t0 = millis(); millis() - t0 < 100; delay(2))
    if (probe(addr, kResetDone) == kResetDoneValue) return true;
  return false;
}

inline void configure() {
  put(kCtrl1, 0x40);   // auto-increment, so one read walks the block
  put(kCtrl2, 0x05);   // +-2 g at 250 Hz: the tap engine wants more than 200 Hz
  put(kCtrl7, 0x00);   // sensors off while the tap engine is configured
  put(kCtrl8, 0x80);   // CTRL9 handshake via STATUSINT.bit7, not a pin
  tapOk = configureTap();
  put(kCtrl7, 0x01);   // accelerometer only; the gyro is heat we do not need
  put(kCtrl8, 0x81);   // ...and the tap engine on
}

// False when no QMI8658 answers. Knocks need the tap engine; the rest works without it.
inline bool begin(int sda, int scl, bool& tapReady) {
  Wire.begin(sda, scl, 400000);
  for (uint8_t a : kAddrs) {
    if (probe(a, kWhoAmI) != kWhoAmIValue) continue;
    addr = a;
    break;
  }
  if (!addr) return false;
  BootState& b = bootState;
  b.ctrl1 = probe(addr, kCtrl1), b.ctrl2 = probe(addr, kCtrl2), b.ctrl7 = probe(addr, kCtrl7);
  b.ctrl8 = probe(addr, kCtrl8), b.status0 = probe(addr, kStatus0);
  uint8_t t0[3] = {}, t1[3] = {}, d0[8] = {}, d1[8] = {};
  block(kTimestampL, t0, 3), block(kTempL, d0, 8);
  delay(30);
  block(kTimestampL, t1, 3), block(kTempL, d1, 8);
  b.tsStep = ((uint32_t(t1[0]) | uint32_t(t1[1]) << 8 | uint32_t(t1[2]) << 16) -
              (uint32_t(t0[0]) | uint32_t(t0[1]) << 8 | uint32_t(t0[2]) << 16)) & 0xFFFFFF;
  b.dataMoved = std::memcmp(d0, d1, 8) != 0;
  b.resetOk = softReset();
  configure();
  tapReady = tapOk;
  return true;
}

// The nap (main.cpp, docs/power.md "The nap"): wake on motion. INT1 is GPIO47
// on the 1.28 (tools/wom_probe, measured on badge 1) and toggles on every
// event, so the chip waits for the level it does not have. Not an RTC pin: it
// wakes light sleep, not deep sleep. read() goes on working: the axes come at
// 21 Hz instead of 250, on the same +-2 g.
constexpr int kIntPin = 47;
constexpr uint8_t kCmdWom = 0x08;   // CTRL_CMD_WRITE_WOM_SETTING

// Accelerometer only, 21 Hz low power, INT1 on any axis moving `mg` (1 mg/LSB).
inline bool wakeOnMotion(uint8_t mg) {
  if (!addr) return false;
  put(kCtrl7, 0x00);   // sensors off while it is set
  put(kCtrl8, 0x80);
  put(kCtrl2, 0x0D);   // +-2 g, 21 Hz low power
  put(kCal1L, mg);
  put(kCal1L + 1, 0x02);   // INT1, starting low, the first 2 samples blanked
  const bool ok = command(kCmdWom);
  put(kCtrl1, 0x48);   // auto-increment, INT1 out
  put(kCtrl7, 0x01);
  return ok;
}

// Back to begin()'s: 250 Hz, wake on motion off (a threshold of 0), no INT pin.
inline bool awake() {
  if (!addr) return false;
  put(kCtrl7, 0x00);
  put(kCtrl8, 0x80);
  put(kCal1L, 0);
  put(kCal1L + 1, 0);
  const bool ok = command(kCmdWom);
  put(kCtrl1, 0x40);
  put(kCtrl2, 0x05);
  put(kCtrl7, 0x01);
  put(kCtrl8, 0x81);
  return ok;
}

// A live QMI8658 never returns the same 8 bytes for long: the temperature's
// 1/256 C and three axes' noise move every sample. kFrozenReads identical
// reads over kFrozenMs is a chip that stopped converting (moth, 2026-10-28),
// and every hand and knock after it is lost; read() soft-resets it and sets
// begin()'s 250 Hz again. In the nap a listen reads at 250 Hz, so a frozen
// chip is found in its first second and the rest of the listen has a hand.
constexpr uint32_t kFrozenReads = 50, kFrozenMs = 1000;
inline uint8_t lastBlock[8] = {};
inline uint32_t sameReads = 0, sameSince = 0, recoveredAt = 0;
constexpr uint32_t kRecoverEveryMs = 10000;   // a chip a reset does not mend is not reset every second

inline void recover() {
  if (resets && millis() - recoveredAt < kRecoverEveryMs) return;
  ++resets, recoveredAt = millis();
  const bool ok = softReset();
  configure();
  sameReads = 0;
  Serial.printf("[hw] !!! imu frozen %u reads: soft reset %s, tap=%s, resets=%u\n", unsigned(kFrozenReads),
                ok ? "ok" : "NOT CONFIRMED", tapOk ? "ok" : "off", unsigned(resets));
}

// Temperature and the three axes are one 8-byte block from TEMP_L. Each byte
// is read into its own local: Wire.read() is a moving cursor, and the order
// of `lo | hi << 8` operands is unspecified.
inline bool read(blorb::BodySample& s) {
  uint8_t b[8];
  if (!block(kTempL, b, 8)) return false;
  if (std::memcmp(b, lastBlock, 8) != 0) {
    std::memcpy(lastBlock, b, 8);
    sameReads = 0;
  } else if (sameReads++ == 0) {
    sameSince = millis();
  } else if (sameReads >= kFrozenReads && millis() - sameSince >= kFrozenMs) {
    recover();
  }
  auto le16 = [&](int i) { return int32_t(int16_t(uint16_t(b[i] | uint16_t(b[i + 1]) << 8))); };
  s.tempCx10 = int16_t(le16(0) * 10 / 256);   // 1/256 C per LSB
  s.ax = int16_t(le16(2) * 1000 / kLsbPerG);
  s.ay = int16_t(le16(4) * 1000 / kLsbPerG);
  // The QMI8658's z points INTO the screen on the 1.28 (measured on the test
  // board, 2026-10-19: face up reads -1 g, face down +1 g), and every detector
  // takes +z out of the screen (senses.h). Before this, a badge lying face up
  // on a desk was "lidded" after 30 s (bedtime, the pet clock pulled toward
  // night) and face down was "righted"; a cuddle wanted the screen tipped away.
  s.az = int16_t(-(le16(6) * 1000 / kLsbPerG));
  // The tap engine's reports are no longer read: they missed real knocks and
  // reported handling (blorb/knocks.h), and two more I2C reads a sample cost
  // time the 250 Hz reads need. Knocks come from the samples.
  s.tapCode = 0;
  return true;
}

}  // namespace imu
