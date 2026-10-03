// The Arduino the firmware uses, for the desktop simulator: Serial, millis,
// delay, pinMode and digitalRead. LovyanGFX's SDL backend already carries a
// clock and a bank of emulated GPIOs, and a key going down there pulls its
// pin LOW, which is what a button wired to a pull-up does.
#pragma once

#include <LovyanGFX.hpp>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

static constexpr int INPUT = 0x01;
static constexpr int OUTPUT = 0x03;
static constexpr int INPUT_PULLUP = 0x05;
static constexpr int HIGH = 1;
static constexpr int LOW = 0;

// The clock, and the one place the simulator may lie about time.
//
// `--clock fixed` makes millis() a counter only the simulator moves, so a
// shot lands on exactly the frame it names and one feed is the same bytes
// every run. It starts at a second, not zero, because firmware reads a zero
// timestamp as "never happened".
//
// `simWarpMs` is time a `DEBUG warp` fast-forwarded through. It counts on the
// firmware's clock (the Dish sees every tick of it) but not on the script's,
// so a line stamped after a warp still lands that long after the warp ends.
static bool simClockFixed = false;
static uint32_t simClockMs = 1000;
static uint32_t simWarpMs = 0;
static inline uint32_t millis() {
  return (simClockFixed ? simClockMs : uint32_t(lgfx::v1::millis())) + simWarpMs;
}
static inline void simTick(uint32_t ms) { simClockMs += ms; }
static inline void delay(uint32_t ms) {
  if (simClockFixed) { simTick(ms); return; }
  lgfx::v1::delay(ms);
}

// An unmapped emulated pin reads 0, and the firmware reads 0 as "pressed", so
// a pull-up pin has to be raised or the dish boots with BOOT held forever.
static inline void pinMode(int pin, int mode) {
  if (mode == INPUT_PULLUP) lgfx::v1::gpio_hi(pin);
}
static inline int digitalRead(int pin) { return lgfx::v1::gpio_in(pin) ? HIGH : LOW; }

// Lines for the simulator rather than the firmware: `!` drives the hand
// (sim.cpp), `DEBUG` reaches into the engine. They wait here and run between
// frames, never from inside the firmware's read of the line, because a warp
// runs the firmware loop and must not run it from inside itself.
static std::vector<std::string> simPending;
static inline bool simOwns(const char* line) { return line[0] == '!' || !strncmp(line, "DEBUG ", 6); }

// The serial line: protocol on stdin, replies on stdout. Input is put back
// together into whole lines first, so a sim line is never half-read by the
// protocol.
class SimSerial {
  int in_ = -1, out_ = -1;
  bool echo_ = true;
  char buf_[2048];   // whole lines, waiting for the firmware
  int len_ = 0, at_ = 0;
  char line_[512];   // the line still arriving
  int lineLen_ = 0;

  void take(const char* line, int n) {
    if (simOwns(line)) { simPending.emplace_back(line, n); return; }
    if (at_ >= len_) len_ = at_ = 0;
    if (len_ + n + 1 > int(sizeof(buf_))) {
      fprintf(stderr, "sim: more than %d bytes of protocol waiting; stamp some lines later\n", int(sizeof(buf_)));
      exit(2);
    }
    memcpy(buf_ + len_, line, n);
    buf_[len_ + n] = '\n';
    len_ += n + 1;
  }

  void fill() {
    if (at_ < len_ || in_ < 0) return;
    char raw[512];
    const ssize_t n = ::read(in_, raw, sizeof(raw));
    for (ssize_t i = 0; i < n; i++) {
      if (raw[i] != '\n') {
        if (lineLen_ < int(sizeof(line_)) - 1) line_[lineLen_++] = raw[i];
        continue;
      }
      line_[lineLen_] = 0;
      take(line_, lineLen_);
      lineLen_ = 0;
    }
  }

 public:
  void attach(int in, int out, bool echo) { in_ = in; out_ = out; echo_ = echo; }

  // A whole line from `--script`, as if it had come down the wire.
  void arrive(const char* line) { take(line, int(strlen(line))); }

  void begin(unsigned long) {}
  int available() { fill(); return len_ - at_; }
  int read() { fill(); return at_ < len_ ? (unsigned char)buf_[at_++] : -1; }

  size_t write(const char* s, size_t n) {
    if (out_ >= 0 && ::write(out_, s, n) < 0) return 0;
    if (echo_) fwrite(s, 1, n, stderr);
    return n;
  }
  size_t print(const char* s) { return write(s, strlen(s)); }
  size_t println(const char* s = "") { return print(s) + write("\n", 1); }
  size_t printf(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    return n > 0 ? write(line, size_t(n) < sizeof(line) ? size_t(n) : sizeof(line) - 1) : 0;
  }
};

static SimSerial Serial;

// The core's function-like macros, typedefs and std usings, copied from
// framework-arduinoespressif32 2.0.17 cores/esp32/Arduino.h (the S3's branch),
// so a name that collides with one breaks the sim build as it breaks the
// board's. word() turned imu_qmi8658.h's word(i) into makeWord(i) and zeroed
// every axis on the board while the sim read them fine.
#include <algorithm>
#include <cmath>

#ifndef __STRINGIFY
#define __STRINGIFY(a) #a
#endif

#define _min(a,b) ((a)<(b)?(a):(b))
#define _max(a,b) ((a)>(b)?(a):(b))
#define _abs(x) ((x)>0?(x):-(x))
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#define _round(x)     ((x)>=0?(long)((x)+0.5):(long)((x)-0.5))
#define radians(deg) ((deg)*DEG_TO_RAD)
#define degrees(rad) ((rad)*RAD_TO_DEG)
#define sq(x) ((x)*(x))

#define sei() portENABLE_INTERRUPTS()
#define cli() portDISABLE_INTERRUPTS()
#define interrupts() sei()
#define noInterrupts() cli()

#define clockCyclesPerMicrosecond() ( (long int)getCpuFrequencyMhz() )
#define clockCyclesToMicroseconds(a) ( (a) / clockCyclesPerMicrosecond() )
#define microsecondsToClockCycles(a) ( (a) * clockCyclesPerMicrosecond() )

#define lowByte(w) ((uint8_t) ((w) & 0xff))
#define highByte(w) ((uint8_t) ((w) >> 8))

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitToggle(value, bit) ((value) ^= (1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

#ifndef _NOP
#define _NOP() do { __asm__ volatile ("nop"); } while (0)
#endif

#define bit(b) (1UL << (b))
#define _BV(b) (1UL << (b))

#define digitalPinToTimer(pin)      (0)
#define analogInPinToBit(P)         (P)
#define digitalPinToPort(pin)       ((digitalPinToGPIONumber(pin)>31)?1:0)
#define digitalPinToBitMask(pin)    (1UL << (digitalPinToGPIONumber(pin)&31))
#define portOutputRegister(port)    ((volatile uint32_t*)((port)?GPIO_OUT1_REG:GPIO_OUT_REG))
#define portInputRegister(port)     ((volatile uint32_t*)((port)?GPIO_IN1_REG:GPIO_IN_REG))
#define portModeRegister(port)      ((volatile uint32_t*)((port)?GPIO_ENABLE1_REG:GPIO_ENABLE_REG))
#define analogInputToDigitalPin(p)  (((p)<NUM_ANALOG_INPUTS)?(analogChannelToDigitalPin(p)):-1)
#define digitalPinToInterrupt(p)    ((((uint8_t)digitalPinToGPIONumber(p))<NUM_DIGITAL_PINS)?(p):NOT_AN_INTERRUPT)
#define digitalPinHasPWM(p)         (((uint8_t)digitalPinToGPIONumber(p))<NUM_DIGITAL_PINS)

typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;

using std::abs;
using std::isinf;
using std::isnan;
using std::max;
using std::min;
using std::round;

static inline uint16_t makeWord(uint16_t w) { return w; }
static inline uint16_t makeWord(uint8_t h, uint8_t l) { return uint16_t(h << 8 | l); }

#define word(...) makeWord(__VA_ARGS__)
