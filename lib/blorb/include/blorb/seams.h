#pragma once
// The two hardware seams the engine calls out through. Time comes in as an
// argument and the IMU as data (Dish::sample), so neither needs a seam.
// Firmware implements these over LittleFS and NimBLE; tests over memory.
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

class Link {
 public:
  virtual ~Link() = default;
  virtual bool connected() = 0;
  virtual std::optional<std::string_view> readLine() = 0;   // one complete line, newline stripped
  virtual void writeLine(std::string_view) = 0;             // appends the newline, splits at the MTU
};

}  // namespace blorb
