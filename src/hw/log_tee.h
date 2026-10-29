// Everything printed to the cable, mirrored into a ring in PSRAM, so the last
// few KB of the log go home in each Wi-Fi window (docs/remote-debug.md). The
// badge's code prints with Serial.printf and friends; main.cpp includes this
// right after Arduino.h, and from there on `Serial` is this tee, which writes
// to the real port first and then copies the bytes into the ring.
//
// The ring is PSRAM, made at the first byte: nothing in internal RAM but this
// object and a spinlock. Any task may print, so the copy is under the lock;
// it is a memcpy of one line. Only what goes through Serial is caught: the
// IDF's own ESP_LOG lines and a panic's dump are not (a panic's backtrace
// comes home from the core dump instead, src/remote_debug.h).
#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>

#include <cstring>

#include "rdebug/remote.h"

namespace hw {

class LogTee : public Stream {
 public:
  static constexpr size_t kRingBytes = 8 * 1024;

  explicit LogTee(HardwareSerial& port) : port_(port) {}

  // The real port's setup, as the code called it before the tee.
  void begin(unsigned long baud) { port_.begin(baud); }
  size_t setTxBufferSize(size_t n) { return port_.setTxBufferSize(n); }
  void flush() override { port_.flush(); }
  int available() override { return port_.available(); }
  int read() override { return port_.read(); }
  int peek() override { return port_.peek(); }

  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* p, size_t n) override {
    const size_t sent = port_.write(p, n);
    mirror(reinterpret_cast<const char*>(p), n);
    return sent;
  }
  using Print::write;

  // The last bytes, oldest first, under the lock (the copy is short).
  size_t tail(char* out, size_t cap) {
    portENTER_CRITICAL(&lock_);
    const size_t n = ring_.tail(out, cap);
    portEXIT_CRITICAL(&lock_);
    return n;
  }
  // The WPA3 DHCP hunt: the [wifi] and [hw] ble lines since the last window
  // opened, in a ring of their own (BLE's WIFILOG reads it; no cable needed).
  static constexpr size_t kWifiRingBytes = 4 * 1024;
  void wifiClear() {
    portENTER_CRITICAL(&lock_);
    if (wifi_.ready()) wifi_.attach(wifiBuf_, kWifiRingBytes);
    portEXIT_CRITICAL(&lock_);
  }
  size_t wifiTail(char* out, size_t cap) {
    portENTER_CRITICAL(&lock_);
    const size_t n = wifi_.ready() ? wifi_.tail(out, cap) : 0;
    portEXIT_CRITICAL(&lock_);
    return n;
  }
  uint64_t mark() {
    portENTER_CRITICAL(&lock_);
    const uint64_t t = ring_.total();
    portEXIT_CRITICAL(&lock_);
    return t;
  }
  size_t since(uint64_t m, char* out, size_t cap) {
    portENTER_CRITICAL(&lock_);
    const size_t n = ring_.since(m, out, cap);
    portEXIT_CRITICAL(&lock_);
    return n;
  }

 private:
  void mirror(const char* p, size_t n) {
    if (!ring_.ready()) {
      if (tried_) return;
      tried_ = true;
      if (void* b = heap_caps_malloc(kRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
        ring_.attach(static_cast<char*>(b), kRingBytes);
      else
        return;
    }
    const bool wifi = (n >= 6 && !std::memcmp(p, "[wifi]", 6)) || (n >= 8 && !std::memcmp(p, "[hw] ble", 8));
    if (wifi && !wifi_.ready() && !wifiTried_) {
      wifiTried_ = true;
      if (void* b = heap_caps_malloc(kWifiRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
        wifiBuf_ = static_cast<char*>(b), wifi_.attach(wifiBuf_, kWifiRingBytes);
    }
    portENTER_CRITICAL(&lock_);
    ring_.write(p, n);
    if (wifi && wifi_.ready()) wifi_.write(p, n);
    portEXIT_CRITICAL(&lock_);
  }

  HardwareSerial& port_;
  rdebug::LogRing ring_;
  bool tried_ = false;
  rdebug::LogRing wifi_;
  char* wifiBuf_ = nullptr;
  bool wifiTried_ = false;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
};

inline LogTee logTee(Serial);

}  // namespace hw

#define Serial ::hw::logTee
