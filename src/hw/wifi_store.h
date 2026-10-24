// The remembered Wi-Fi networks (up to three) and the badge's own Wi-Fi switch
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

  // The remembered networks (wifiota/remembered.h): the "nets" blob, or the
  // first branch's one-network "net" blob, or none.
  static std::optional<wifiota::Networks> load() {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READONLY, &n) != ESP_OK) return std::nullopt;   // never given one: nothing to read
    uint8_t blob[wifiota::kNetworksMax];
    size_t len = sizeof blob;
    std::optional<wifiota::Networks> out;
    if (nvs_get_blob(n, "nets", blob, &len) == ESP_OK) {
      out = wifiota::Networks::decode(blob, len);
    } else {
      len = sizeof blob;
      if (nvs_get_blob(n, "net", blob, &len) == ESP_OK) out = wifiota::Networks::decode(blob, len);
    }
    nvs_close(n);
    return out;
  }

  // Every network at once, newest first; an empty list erases them. The old "net" goes.
  static bool save(const wifiota::Networks& nets) {
    uint8_t blob[wifiota::kNetworksMax];
    const size_t len = nets.encode(blob, sizeof blob);
    nvs_handle_t n;
    if (!len || nvs_open(kNamespace, NVS_READWRITE, &n) != ESP_OK) return false;
    const esp_err_t old = nvs_erase_key(n, "net");
    const bool ok = (old == ESP_OK || old == ESP_ERR_NVS_NOT_FOUND) && nvs_set_blob(n, "nets", blob, len) == ESP_OK &&
                    nvs_commit(n) == ESP_OK;
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

  // The manifest URL an update over Wi-Fi came from, when it was not the
  // site's (a cable's `DEBUG ota check <url>`, a test release): its trial
  // confirms against the same place. Written before the restart, read on the
  // trial's boot, erased once it has confirmed. Empty: the site's.
  static bool setSource(const char* url) {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READWRITE, &n) != ESP_OK) return false;
    const esp_err_t e = *url ? nvs_set_str(n, "src", url) : nvs_erase_key(n, "src");
    const bool ok = (e == ESP_OK || (!*url && e == ESP_ERR_NVS_NOT_FOUND)) && nvs_commit(n) == ESP_OK;
    nvs_close(n);
    return ok;
  }
  static bool source(char* out, size_t cap) {
    out[0] = 0;
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READONLY, &n) != ESP_OK) return false;
    size_t len = cap;
    const bool ok = nvs_get_str(n, "src", out, &len) == ESP_OK;
    nvs_close(n);
    if (!ok) out[0] = 0;
    return ok;
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
