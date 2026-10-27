#pragma once
// The wall time a Wi-Fi window brings back, so a badge knows the time without
// a phone: the HTTPS reply's Date header, and the zone the site read from where
// the request came from (X-Blorb-Tz, web/src/app.js). The firmware hands both
// to Dish::netTime (src/wifi_ota.h).
#include <cstdint>
#include <optional>
#include <string_view>

namespace wifiota {

inline constexpr const char* kTzHeader = "X-Blorb-Tz";
// 2024-01-01: a clock that says earlier was never set (hw::SystemClock::kPlausible).
inline constexpr uint32_t kPlausibleUnix = 1704067200;

// An IMF-fixdate (RFC 9110: "Tue, 27 Oct 2026 12:34:56 GMT") as seconds since
// 1970. Nothing when it is malformed or before kPlausibleUnix.
std::optional<uint32_t> parseHttpDate(std::string_view);

// X-Blorb-Tz: the zone's offset from UTC in minutes, east positive, in the
// range TIME takes (-720 to 840). Nothing when it is malformed or out of range.
std::optional<int16_t> parseTzMinutes(std::string_view);

}  // namespace wifiota
