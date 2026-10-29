// The badge's durable Bluetooth id (docs/ble.md "Advertisement"): its MAC
// in manufacturer-specific data, so a page can ask Chrome's chooser for one
// badge by a link that never changes (an NFC sticker, docs/nfc.md). The name
// is the pet's species and changes with him; Web Bluetooth never shows a MAC.
//
// The MAC is the Wi-Fi station MAC, the one defs/badges.def, blorb-xxxx and
// /admin's debug uploads all print (the Bluetooth address is that plus 2).
// The record is the AD structure's payload: the company id, little-endian,
// then the six bytes in print order, so a page filters on
// {companyIdentifier: kCompanyId, dataPrefix: the six bytes}.
//
// The advertisement's 31 bytes: flags (3) + the UART service's 128-bit UUID
// (18) + this record (2 + 8 = 10) = 31. The scan response keeps the name
// and, when it fits, Improv's UUID (src/hw/improv_ble.h). Free of NimBLE so
// the native suite checks the layout.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace blorb::bleid {

// 0xFFFF: the Bluetooth SIG's id for testing and internal use, never assigned to a company.
inline constexpr uint16_t kCompanyId = 0xFFFF;
inline constexpr size_t kMacBytes = 6;
inline constexpr size_t kPayloadBytes = 2 + kMacBytes;   // what setManufacturerData takes

inline std::array<uint8_t, kPayloadBytes> payload(const uint8_t mac[kMacBytes]) {
  std::array<uint8_t, kPayloadBytes> p{};
  p[0] = uint8_t(kCompanyId & 0xff);
  p[1] = uint8_t(kCompanyId >> 8);
  for (size_t i = 0; i < kMacBytes; i++) p[2 + i] = mac[i];
  return p;
}

// Each AD structure costs a length byte and a type byte beside its data.
inline constexpr size_t kAdvMax = 31;
inline constexpr size_t kFlags = 2 + 1;
inline constexpr size_t kUuid128 = 2 + 16;
inline constexpr size_t kRecord = 2 + kPayloadBytes;
inline constexpr size_t kAdvUsed = kFlags + kUuid128 + kRecord;
static_assert(kAdvUsed <= kAdvMax, "the id must fit beside the flags and the UART service UUID");

}  // namespace blorb::bleid
