#include "wifiota/net_clock.h"

namespace wifiota {
namespace {

// Exactly `n` digits at `at`, or nothing.
std::optional<uint32_t> digits(std::string_view s, size_t at, size_t n) {
  if (at + n > s.size()) return std::nullopt;
  uint32_t v = 0;
  for (size_t i = at; i < at + n; ++i) {
    if (s[i] < '0' || s[i] > '9') return std::nullopt;
    v = v * 10 + uint32_t(s[i] - '0');
  }
  return v;
}

// Days from 1970-01-01 to y-m-d in the proleptic Gregorian calendar (Hinnant's days_from_civil).
int64_t daysFromCivil(int64_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = uint32_t(y - era * 400);
  const uint32_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + int64_t(doe) - 719468;
}

}  // namespace

std::optional<uint32_t> parseHttpDate(std::string_view s) {
  // "Tue, 27 Oct 2026 12:34:56 GMT": 29 characters, every field at a fixed place.
  if (s.size() != 29 || s[3] != ',' || s[4] != ' ' || s[7] != ' ' || s[11] != ' ' || s[16] != ' ' ||
      s[19] != ':' || s[22] != ':' || s.substr(25) != " GMT")
    return std::nullopt;
  static constexpr std::string_view kMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const size_t at = kMonths.find(s.substr(8, 3));
  if (at == std::string_view::npos || at % 3) return std::nullopt;
  const uint32_t month = uint32_t(at / 3 + 1);
  const auto day = digits(s, 5, 2), year = digits(s, 12, 4), h = digits(s, 17, 2), mi = digits(s, 20, 2),
             sec = digits(s, 23, 2);
  if (!day || !year || !h || !mi || !sec || *day < 1 || *day > 31 || *h > 23 || *mi > 59 || *sec > 60)
    return std::nullopt;
  const int64_t t = daysFromCivil(*year, month, *day) * 86400 + int64_t(*h) * 3600 + int64_t(*mi) * 60 + *sec;
  if (t < int64_t(kPlausibleUnix) || t > int64_t(UINT32_MAX)) return std::nullopt;
  return uint32_t(t);
}

std::optional<int16_t> parseTzMinutes(std::string_view s) {
  bool minus = false;
  if (!s.empty() && (s[0] == '-' || s[0] == '+')) minus = s[0] == '-', s.remove_prefix(1);
  if (s.empty() || s.size() > 3) return std::nullopt;
  const std::optional<uint32_t> v = digits(s, 0, s.size());
  if (!v) return std::nullopt;
  const int32_t m = minus ? -int32_t(*v) : int32_t(*v);
  if (m < -12 * 60 || m > 14 * 60) return std::nullopt;
  return int16_t(m);
}

}  // namespace wifiota
