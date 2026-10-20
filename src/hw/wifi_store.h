// The remembered Wi-Fi network and the badge's own Wi-Fi switch
// (docs/wifi-ota.md "Credentials"), in the default `nvs` partition under
// namespace "wifi": beside the OTA trial's "ota" records, never in `pet` or
// `petfs`. Boot only reads, opening read-only, so a badge that was never given
// a network has no "wifi" namespace at all; the first network sent creates it.
// nvs_* directly, not Preferences (src/hw/ota_esp.h says why).
#pragma once

#include <nvs.h>

#include <optional>

#include "wifiota/remembered.h"

namespace hw {

class WifiStore {
 public:
  static constexpr const char* kNamespace = "wifi";

  static std::optional<wifiota::improv::Credentials> load() {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READONLY, &n) != ESP_OK) return std::nullopt;   // never given one: nothing to read
    uint8_t blob[wifiota::kRememberedMax];
    size_t len = sizeof blob;
    const bool ok = nvs_get_blob(n, "net", blob, &len) == ESP_OK;
    nvs_close(n);
    return ok ? wifiota::decodeRemembered(blob, len) : std::nullopt;
  }

  static bool save(const wifiota::improv::Credentials& c) {
    uint8_t blob[wifiota::kRememberedMax];
    const size_t len = wifiota::encodeRemembered(c, blob, sizeof blob);
    nvs_handle_t n;
    if (!len || nvs_open(kNamespace, NVS_READWRITE, &n) != ESP_OK) return false;
    const bool ok = nvs_set_blob(n, "net", blob, len) == ESP_OK && nvs_commit(n) == ESP_OK;
    nvs_close(n);
    return ok;
  }

  // `DEBUG wifi forget`: the network gone. Nothing to do on a badge never given one.
  static bool forget() {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READONLY, &n) != ESP_OK) return true;
    nvs_close(n);
    if (nvs_open(kNamespace, NVS_READWRITE, &n) != ESP_OK) return false;
    const esp_err_t e = nvs_erase_key(n, "net");
    const bool ok = (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(n) == ESP_OK;
    nvs_close(n);
    return ok;
  }

  // Automatic checks and installs: on unless `DEBUG wifi off` turned them off.
  // Provisioning and `DEBUG ota check` work either way.
  static bool autoOn() {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READONLY, &n) != ESP_OK) return true;
    uint8_t off = 0;
    nvs_get_u8(n, "off", &off);
    nvs_close(n);
    return !off;
  }

  static bool setAuto(bool on) {
    if (on == autoOn()) return true;   // and a badge never given a network gets no namespace for it
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READWRITE, &n) != ESP_OK) return false;
    const esp_err_t e = on ? nvs_erase_key(n, "off") : nvs_set_u8(n, "off", 1);
    const bool ok = (e == ESP_OK || (on && e == ESP_ERR_NVS_NOT_FOUND)) && nvs_commit(n) == ESP_OK;
    nvs_close(n);
    return ok;
  }
};

}  // namespace hw
