// Where a badge keeps its home (src/hw/home.h): the default `nvs` partition,
// namespace "badge", key "home", a u8 (0 or 2). Not `pet` (a pet wipe erases
// that partition) and not the app slots (an update rewrites those); flash.sh's
// merged image covers neither. nvs_* directly, not Preferences (ota_esp.h says why).
#pragma once

#include <nvs.h>

#include <optional>

#include "hw/home.h"

namespace hw {

struct HomeStore {
  static constexpr const char* kNamespace = "badge";

  static std::optional<uint8_t> load() {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READONLY, &n) != ESP_OK) return std::nullopt;   // never set
    uint8_t v = 0xff;
    const bool ok = nvs_get_u8(n, "home", &v) == ESP_OK;
    nvs_close(n);
    return ok ? std::optional<uint8_t>(v) : std::nullopt;
  }

  static bool save(uint8_t rot) {
    if (!home::valid(rot)) return false;
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READWRITE, &n) != ESP_OK) return false;
    const bool ok = nvs_set_u8(n, "home", rot) == ESP_OK && nvs_commit(n) == ESP_OK;
    nvs_close(n);
    return ok;
  }
};

}  // namespace hw
