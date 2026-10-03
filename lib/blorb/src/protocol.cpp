#include "blorb/protocol.h"

#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include "blorb/seams.h"

namespace blorb {
namespace {

constexpr size_t kMaxLine = 200;

// "#<id> <kind>[ <payload>]", cut at the line cap.
void emit(Link& link, uint32_t id, const char* kind, const char* fmt, va_list ap) {
  char payload[kMaxLine + 1];
  std::vsnprintf(payload, sizeof payload, fmt, ap);
  char line[kMaxLine + 1];
  int n = std::snprintf(line, sizeof line, "#%x %s%s%s", unsigned(id), kind, payload[0] ? " " : "", payload);
  link.writeLine(std::string_view(line, n < 0 ? 0 : std::min<size_t>(size_t(n), kMaxLine)));
}

}  // namespace

Reply::Reply(Link& link, uint32_t id) : link_(link), id_(id) {}

Reply& Reply::line(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(link_, id_, "+", fmt, ap);
  va_end(ap);
  return *this;
}

void Reply::chunks(const uint8_t*, size_t) {}

void Reply::ok(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(link_, id_, "OK", fmt, ap);
  va_end(ap);
}

void Reply::err(uint16_t code, const char* text) {
  char line[kMaxLine + 1];
  int n = std::snprintf(line, sizeof line, "#%x ERR %u %s", unsigned(id_), unsigned(code), text);
  link_.writeLine(std::string_view(line, n < 0 ? 0 : std::min<size_t>(size_t(n), kMaxLine)));
}

void Describe::field(const char* name, int value) { out.line("%s=%d", name, value); }
void Describe::field(const char* name, const char* value) { out.line("%s=%s", name, value); }

}  // namespace blorb
