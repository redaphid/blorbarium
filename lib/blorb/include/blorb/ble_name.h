// The name the badge advertises over Bluetooth (docs/ble.md): always the
// pet's name, from the first advertisement of every boot, OTA trial included,
// and again whenever the name changes (a rename, an old pet adopting his
// board's species). Lowercase, as every badge has advertised since 0.7.0, so
// a pet nobody renamed advertises exactly what he always did ("mossby" for
// Mossby) and every page and tool that looks for that still finds him.
//
// The one board with no readable pet name is a sealed one (its pet was not
// read: blorb::Dish::sealed()). It advertises its board's species, the name
// its pet would carry (blorb::advertisedSpecies). An egg, a fresh board and
// one whose storage was empty all have a name: a line is named for its
// founder's species from the moment it is founded (Lineage::open).
//
// The scan response holds the name (2 + its length) and, when it fits beside
// it, Improv's 128-bit UUID (2 + 16), in 31 bytes: a name of up to 11
// characters keeps the UUID; 12 to 15 (a pet's longest) leave it out, never
// the name. Free of NimBLE so the native suite checks it (test/test_ble_id).
#pragma once

#include <cstddef>
#include <cstdint>

namespace blorb::blename {

inline constexpr size_t kMax = 15;            // a pet's longest name (twists.cpp kNameMax)
inline constexpr size_t kScanMax = 31;        // a legacy scan response
inline constexpr size_t kUuid128 = 2 + 16;    // Improv's service UUID, as an AD structure
inline constexpr size_t nameBytes(size_t len) { return 2 + len; }
inline constexpr bool uuidFits(size_t len) { return nameBytes(len) + kUuid128 <= kScanMax; }
static_assert(nameBytes(kMax) <= kScanMax, "a pet's longest name must fit the scan response");
static_assert(uuidFits(11) && !uuidFits(12), "Improv's UUID fits beside a name of up to 11 characters");

// `pet`: the pet's name (Lineage::currentName), or nullptr when the board
// could not read its pet. `species`: the species slug to fall back on. Only
// the characters a rename allows (letters, digits, '_' and '-') are kept,
// letters lowercased; a name with none of them falls back to the species.
// Returns `out`.
inline const char* make(const char* pet, const char* species, char (&out)[kMax + 1]) {
  size_t n = 0;
  for (const char* p = pet; p && *p && n < kMax; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') out[n++] = c;
  }
  if (n == 0)
    for (const char* p = species; p && *p && n < kMax; ++p) out[n++] = *p;
  out[n] = 0;
  return out;
}

}  // namespace blorb::blename

namespace blorb {
class Dish;
// What the badge advertises (src/main.cpp at boot and once a second after):
// blename::make over the pet's name, or nullptr for a sealed board, and the
// species advertisedSpecies gives for his genome and this board's MAC.
const char* advertisedName(Dish&, const uint8_t mac[6], char (&out)[blename::kMax + 1]);
}  // namespace blorb
