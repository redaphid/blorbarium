// The QMI8658 on the 1.28, as raw reads: claude-notification-screen's
// orient.h with every gesture removed, because the engine's detectors
// (lib/blorb/src/senses.cpp) own the gestures now. This only fills a
// BodySample. Registers and tap settings are orient.h's, which were measured
// on the board: https://www.waveshare.com/wiki/ESP32-S3-LCD-1.28
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "blorb/senses.h"

namespace imu {

// SA0 decides the address and Waveshare ties it high, but a board that came
// out the other way still answers, so ask both once at boot.
constexpr uint8_t kAddrs[2] = {0x6B, 0x6A};
constexpr uint8_t kWhoAmI = 0x00, kWhoAmIValue = 0x05;
constexpr uint8_t kCtrl1 = 0x02, kCtrl2 = 0x03, kCtrl7 = 0x08, kCtrl8 = 0x09, kCtrl9 = 0x0A;
constexpr uint8_t kCal1L = 0x0B;   // CAL1_L..CAL4_H run 0x0B..0x12
constexpr uint8_t kStatusInt = 0x2D, kStatus1 = 0x2F, kTempL = 0x33, kTapStatus = 0x59;
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

// False when no QMI8658 answers. Knocks need the tap engine; the rest works without it.
inline bool begin(int sda, int scl, bool& tapReady) {
  Wire.begin(sda, scl, 400000);
  for (uint8_t a : kAddrs) {
    if (probe(a, kWhoAmI) != kWhoAmIValue) continue;
    addr = a;
    break;
  }
  if (!addr) return false;
  put(kCtrl1, 0x40);   // auto-increment, so one read walks the block
  put(kCtrl2, 0x05);   // +-2 g at 250 Hz: the tap engine wants more than 200 Hz
  put(kCtrl7, 0x00);   // sensors off while the tap engine is configured
  put(kCtrl8, 0x80);   // CTRL9 handshake via STATUSINT.bit7, not a pin
  tapReady = configureTap();
  put(kCtrl7, 0x01);   // accelerometer only; the gyro is heat we do not need
  put(kCtrl8, 0x81);   // ...and the tap engine on
  return true;
}

// Temperature and the three axes are one 8-byte block from TEMP_L. Each byte
// is read into its own local: Wire.read() is a moving cursor, and the order
// of `lo | hi << 8` operands is unspecified.
inline bool read(blorb::BodySample& s) {
  Wire.beginTransmission(addr);
  Wire.write(kTempL);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, uint8_t(8)) != 8) return false;
  uint8_t b[8];
  for (uint8_t& v : b) v = uint8_t(Wire.read());
  auto word = [&](int i) { return int32_t(int16_t(uint16_t(b[i] | uint16_t(b[i + 1]) << 8))); };
  s.tempCx10 = int16_t(word(0) * 10 / 256);   // 1/256 C per LSB
  s.ax = int16_t(word(2) * 1000 / kLsbPerG);
  s.ay = int16_t(word(4) * 1000 / kLsbPerG);
  s.az = int16_t(word(6) * 1000 / kLsbPerG);
  // STATUS1.bit1 says a tap was reported; TAP_STATUS[1:0] says which kind.
  s.tapCode = (probe(addr, kStatus1) & 0x02) ? uint8_t(probe(addr, kTapStatus) & 0x03) : 0;
  return true;
}

}  // namespace imu
