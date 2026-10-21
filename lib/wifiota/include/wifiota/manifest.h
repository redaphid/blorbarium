#pragma once
// The release manifest the site serves (docs/ota.md "Release layout"), read
// by the badge itself over Wi-Fi, and whether to install what it names
// without anyone there (docs/wifi-ota.md "Self-update").
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "ota/session.h"
#include "ota/types.h"

namespace wifiota {

inline constexpr const char* kManifestUrl = "https://blorb.hypnodroid.com/fw/lcd128/manifest.json";
inline constexpr size_t kManifestMax = 2048;   // today's is about 330 bytes

struct Manifest {
  ota::Image image;          // board, version, flags, fmt, size, sha256, sig: what BEGIN carries
  char file[64] = "";        // "image": beside the manifest
  bool autoInstall = true;   // "auto": false is the owner's kill switch for every badge's unattended install
};

// The manifest's flat JSON object. Unknown keys are skipped, so a later field
// does not stop an older badge. Nothing when a field is missing or malformed.
std::optional<Manifest> parseManifest(std::string_view json);

// The image's URL: the manifest's, with its last path part replaced. False when it does not fit.
bool imageUrl(std::string_view manifestUrl, const char* file, char* out, size_t cap);

enum class Offer : uint8_t {
  Install,      // newer, for this board, never rolled back here, and the owner allows it
  UpToDate,     // not newer than what runs: an unattended update never goes back
  WrongBoard,
  OlderFormat,  // writes an older save format than this badge's: it could not read the pet and would roll back
  RolledBack,   // this very version switched in here and rolled back: only a hand retries it (Bluetooth)
  AutoOff,      // the manifest says "auto": false
};
const char* offerName(Offer);

// `fmt` is the running image's save format (keepsake.h kFormatVersion, the
// boot line's fmt=). A newer one is fine: the new image migrates the pet, as
// it would after an update over Bluetooth (docs/ota.md "Save formats").
Offer judge(const Manifest&, const char* board, ota::Version running, uint32_t fmt,
            std::optional<ota::Version> rolledBack);

// The token an unattended update hands over (ota::Handover::token): "WIFI"
// and 12 random bytes. The new image confirms its trial over Wi-Fi only for
// a token like this, so a Wi-Fi fetch never confirms a page's Bluetooth update.
ota::Token wifiToken(const uint8_t random[12]);
bool isWifiToken(const ota::Token&);

}  // namespace wifiota
