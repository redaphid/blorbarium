#pragma once
// The hardware seams the engine calls out through. Powered time comes in as
// an argument and the IMU as data (Dish::sample). Wall time is a seam because
// the dish lives on through unpowered gaps (DEVIATIONS.md 3).
// Firmware implements these over LittleFS, NimBLE and an RTC; tests over memory.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace blorb {

class Storage {
 public:
  virtual ~Storage() = default;
  virtual std::optional<size_t> read(const char* name, uint8_t* buf, size_t cap) = 0;
  virtual bool writeAtomic(const char* name, const uint8_t* data, size_t len) = 0;   // temp + rename
  virtual bool append(const char* name, const uint8_t* data, size_t len) = 0;
  virtual bool rename(const char* from, const char* to) = 0;                        // quarantine, never delete
  virtual size_t size(const char* name) = 0;
};

// Wall time in seconds since 1970, or nullopt when this source has none now.
// The Dish asks its sources in priority order: an RTC that kept counting
// while unpowered, then the phone's TIME, then none. A firmware that only
// knows how long it slept implements this as Dish::wallNow() saved before
// deep sleep plus the time slept.
class TimeSource {
 public:
  virtual ~TimeSource() = default;
  virtual std::optional<uint32_t> unixSeconds() = 0;
};

class Link {
 public:
  virtual ~Link() = default;
  virtual bool connected() = 0;
  virtual std::optional<std::string_view> readLine() = 0;   // one complete line, newline stripped
  virtual void writeLine(std::string_view) = 0;             // appends the newline, splits at the MTU
};

}  // namespace blorb
