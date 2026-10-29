// The Wi-Fi station (docs/wifi-ota.md). Off unless a window is open
// (wifiota/schedule.h): up() brings the radio up in station mode, joined to
// nothing, with the credentials kept in RAM only (WiFi.persistent(false): the
// core would otherwise copy them into the driver's own NVS namespace). Going
// down is two steps, stop() then, once the driver is quiet, off(): the
// driver must not be torn down mid-scan or mid-connect ("wifi:timeout when
// WiFi un-init"). ESP-NOW's duel radio (src/hw/air_espnow.h) runs the same
// radio and never at the same time: src/wifi_ota.h keeps the two apart.
//
// Nothing here waits: the scan runs on the driver's task and is polled, the
// join is polled, and the HTTPS GET runs on its own task (net_job.h). The
// driver's events (scan done, disconnected and why, got an IP) are counted
// by a handler on the core's event task, for the loop to read.
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_coexist.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_bt.h>
#include <lwip/dhcp.h>
#include <lwip/netif.h>
#include <sdkconfig.h>
#if defined(CONFIG_PM_ENABLE)
#include <esp_pm.h>
#endif

#include <algorithm>
#include <cstring>
#include <new>
#include <optional>

#include "wifiota/improv.h"
#include "wifiota/remembered.h"
#include "wifiota/sightings.h"

namespace hw {

// `DEBUG wpa3 ...` (src/wifi_ota.h): how the WPA3 joins after it are made, to
// find what a WPA3 network's router wants (docs/ota.md "WPA3"). Until reset;
// untouched, the join is option B: SAE both, PMF capable, Wi-Fi first.
struct Wpa3Cell {
  bool asked = false;    // set by the verb: the join logs its cell, and where DHCP stands every 2 s
  bool plain = false;    // the core's WiFi.begin as 0.7.24: no SAE fields set
  uint8_t pwe = WPA3_SAE_PWE_BOTH;
  bool cpuMax = false;   // 240 MHz from the join on
  bool psNone = false;   // WIFI_PS_NONE: refused unless Bluetooth's controller is off
  bool setMac = false;   // join as another badge's MAC (is the router letting only known devices have an address?)
  uint8_t mac[6] = {};
};
inline Wpa3Cell wpa3Cell;

class WifiStation {
 public:
  // Active, at most this long on each channel. The scan's end is the driver's
  // SCAN_DONE event, not the core's scanComplete(), which calls a scan failed
  // after 20 times this whatever the driver is doing: with Bluetooth sharing
  // the radio a scan takes longer than that, and the "failed" scan then went
  // on under the join and refused it.
  static constexpr uint32_t kScanMsPerChannel = 120;
  static constexpr uint8_t kSeenMax = wifiota::Sightings::kMax;   // the strongest this many a scan finds are looked at
  static constexpr uint32_t kQuietMinMs = 200, kQuietAfterJoinMs = 1000, kQuietMaxMs = 3000;   // stop() to off()

  bool up() {
    if (up_) return true;
    if (!handler_) {
      WiFi.onEvent(&WifiStation::onEvent);
      handler_ = true;
    }
    const uint32_t before = ESP.getFreeHeap();
    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_STA)) return false;
    WiFi.setAutoReconnect(false);   // the window decides when to try again
    up_ = true;
    everUp_ = true;
    stopping_ = false;
    attempted_ = false;
    joinedOnce_ = false;
    fresh_ = false;   // this window's join looks only at this window's scan
#if defined(CONFIG_PM_ENABLE)
    // Light sleep between packets would hold a download to the AP's beacons.
    if (!lock_) esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "wifi", &lock_);
    if (lock_) esp_pm_lock_acquire(lock_);
#endif
    Serial.printf("[wifi] radio up heap %u -> %u big=%u\n", unsigned(before), unsigned(ESP.getFreeHeap()),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    return true;
  }

  // Step one of going down: the scan stopped, the connection left. off() once quiet().
  void stop() {
    if (!up_ || stopping_) return;
    stopping_ = true;
    stopAt_ = millis();
    disconnectsAtStop_ = disconnects_;
    if (WiFi.getStatusBits() & WIFI_SCANNING_BIT) esp_wifi_scan_stop();
    if (attempted_) esp_wifi_disconnect();
  }
  // The driver has finished what it was doing: a connection attempt answered
  // with its disconnect, or long enough has passed.
  bool quiet() const {
    if (!up_) return true;
    const uint32_t since = millis() - stopAt_;
    // After a connection, longer: TCP's last segments and the disconnect must
    // be done before the driver goes ("wifi:timeout when WiFi un-init").
    if (since < (joinedOnce_ ? kQuietAfterJoinMs : kQuietMinMs)) return false;
    if (since >= kQuietMaxMs) return true;
    const bool scanning = WiFi.getStatusBits() & WIFI_SCANNING_BIT;
    return !scanning && (!attempted_ || disconnects_ != disconnectsAtStop_);
  }
  void off() {
    if (!up_) return;
    if (!stopping_) stop();
    const uint32_t waited = millis() - stopAt_;
    holdCoex(false);
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    up_ = false;
    stopping_ = false;
    fresh_ = false;   // what it heard stays, for `DEBUG wifi` after the window (wifiota/sightings.h)
#if defined(CONFIG_PM_ENABLE)
    if (lock_) esp_pm_lock_release(lock_);
#endif
    Serial.printf("[wifi] radio down after %u ms quiet heap=%u\n", unsigned(waited), unsigned(ESP.getFreeHeap()));
  }

  bool isUp() const { return up_; }
  bool everUp() const { return everUp_; }   // this boot: the core's tasks and netifs exist already

  // ---- the scan: what is in range ----
  bool scan() {
    fresh_ = false;
    if (!sightings()) return false;
    heard_->beginScan();
    scansAtStart_ = scansDone_;
    const int16_t r = WiFi.scanNetworks(true, false, false, kScanMsPerChannel);
    if (r != WIFI_SCAN_RUNNING) Serial.printf("[wifi] the scan would not start (%d)\n", int(r));
    return r == WIFI_SCAN_RUNNING;
  }
  // -1 still scanning, -2 the driver said it failed, else how many networks
  // it found (the strongest kSeenMax of them copied for seen()).
  int scanned() {
    if (scansDone_ == scansAtStart_ || !heard_) return WIFI_SCAN_RUNNING;
    if (scanStatus_ != 0) return WIFI_SCAN_FAILED;
    const int16_t n = WiFi.scanComplete();   // the driver's count: the core reset its own clock at SCAN_DONE
    if (n < 0) return n;
    heard_->beginScan();
    for (int16_t i = 0; i < n; ++i) {
      wifiota::Ap a;
      std::snprintf(a.ssid, sizeof a.ssid, "%s", WiFi.SSID(i).c_str());
      a.rssi = int(WiFi.RSSI(i));
      a.channel = uint8_t(WiFi.channel(i));
      a.auth = uint8_t(WiFi.encryptionType(i));
      if (const uint8_t* b = WiFi.BSSID(i)) std::memcpy(a.bssid, b, 6);
      heard_->add(a);   // the strongest kSeenMax kept
    }
    WiFi.scanDelete();
    fresh_ = true;
    return n;
  }
  uint32_t scanStatus() const { return scanStatus_; }
  const wifiota::Seen* seen() const { return heard_ ? heard_->seen() : nullptr; }
  uint8_t seenCount() const { return heard_ ? heard_->count() : 0; }
  // A network's access point as last heard, for `DEBUG wifi` (never its
  // SSID: the caller names it by number): the one its join reached since
  // the last scan, else the scan's strongest. Nothing: not heard.
  struct Heard {
    int rssi;
    uint8_t channel;
    const char* security;
    bool joined;
  };
  std::optional<Heard> heard(const char* ssid) const {
    if (!heard_) return std::nullopt;
    const std::optional<wifiota::Sightings::Heard> h = heard_->heard(ssid);
    if (!h) return std::nullopt;
    return Heard{h->rssi, h->channel, authName(h->auth), h->joined};
  }
  // Joined: the access point it reached is heard, a provision's (no scan) too.
  void heardJoin(const char* ssid) {
    wifi_ap_record_t ap;
    if (!sightings() || esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return;
    wifiota::Ap a;
    std::snprintf(a.ssid, sizeof a.ssid, "%s", ssid);
    a.rssi = ap.rssi;
    a.channel = ap.primary;
    a.auth = uint8_t(ap.authmode);
    std::memcpy(a.bssid, ap.bssid, 6);
    heard_->joined(a);
  }
  // A scan that has run too long: stopped, so nothing is left running under the join.
  // Before joining another network in the same window: the driver drops the
  // last attempt (it may still be scanning for a hidden SSID), so the next
  // begin starts clean; give its disconnect a moment to land before join()
  // counts its own (0.7.39: a begin straight over one never started, and
  // its retry associated with no DHCP).
  void leave() { esp_wifi_disconnect(); }

  void abandonScan() {
    if (WiFi.getStatusBits() & WIFI_SCANNING_BIT) esp_wifi_scan_stop();
  }

  // ---- the join ----
  enum class Join : uint8_t { Trying, Joined, Failed };
  // The network's strongest access point from the last scan, straight to its
  // channel and BSSID, so no second scan is needed. Logs what it chose, with
  // the SSID's and password's lengths only.
  void join(const wifiota::improv::Credentials& c) {
    abandonScan();
    const wifiota::Ap* best = fresh_ && heard_ ? heard_->strongest(c.ssid) : nullptr;   // this window's scan only
    const char* pass = c.password[0] ? c.password : nullptr;
    attempted_ = true;
    reason_ = 0;
    disconnectsAtJoin_ = disconnects_;
    wpa3_ = best && (best->auth == WIFI_AUTH_WPA3_PSK || best->auth == WIFI_AUTH_WPA2_WPA3_PSK);
    attemptAt_ = millis();
    wl_status_t r;
    if (!wpa3_) {
      r = best ? WiFi.begin(c.ssid, pass, best->channel, best->bssid) : WiFi.begin(c.ssid, pass);
    } else {
      // WPA3 (SAE): the core's config with SAE's own fields set, then connect.
      // Disconnecting first and setting the config again drops what the
      // driver kept from a failed attempt (its PMK). Wi-Fi has the radio over
      // Bluetooth until the join settles: SAE's commit and confirm frames
      // must not wait behind it.
      esp_wifi_disconnect();
      const Wpa3Cell& cell = wpa3Cell;
      if (cell.cpuMax) setCpuFrequencyMhz(240);
      if (cell.setMac) {
        const esp_err_t e = esp_wifi_set_mac(WIFI_IF_STA, cell.mac);
        esp_netif_t* ni = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        const esp_err_t e2 = ni ? esp_netif_set_mac(ni, const_cast<uint8_t*>(cell.mac)) : -1;
        Serial.printf("[wifi] wpa3 cell: mac set ..:%02x:%02x err=%d netif=%d\n", cell.mac[4], cell.mac[5], int(e), int(e2));
      }
      r = WiFi.begin(c.ssid, pass, best->channel, best->bssid, cell.plain);
      wifi_config_t conf;
      if (cell.plain) {
        // as 0.7.24
      } else if (r != WL_CONNECT_FAILED && esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        conf.sta.sae_pwe_h2e = wifi_sae_pwe_method_t(cell.pwe);
        conf.sta.pmf_cfg.capable = true;
        conf.sta.pmf_cfg.required = false;
        conf.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        if (esp_wifi_set_config(WIFI_IF_STA, &conf) != ESP_OK) r = WL_CONNECT_FAILED;
      } else {
        r = WL_CONNECT_FAILED;
      }
      if (r != WL_CONNECT_FAILED && !cell.plain) {
        holdCoex(true);
        if (esp_wifi_connect() != ESP_OK) r = WL_CONNECT_FAILED;
      }
      if (cell.asked) {
        esp_err_t psErr = ESP_OK;
        if (cell.psNone)   // the IDF aborts on PS_NONE while Bluetooth's controller is up
          psErr = esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE ? esp_wifi_set_ps(WIFI_PS_NONE)
                                                                                 : ESP_ERR_INVALID_STATE;
        wifi_ps_type_t ps = WIFI_PS_MIN_MODEM;
        esp_wifi_get_ps(&ps);
        Serial.printf("[wifi] wpa3 cell: %s pwe=%u cpu=%u MHz ps=%d (ps none err %d) bt=%d\n", cell.plain ? "plain" : "option-b",
                      unsigned(cell.pwe), unsigned(getCpuFrequencyMhz()), int(ps), int(psErr),
                      int(esp_bt_controller_get_status()));
        dhcpAt_ = 0;
      }
    }
    if (best)
      Serial.printf("[wifi] join: ssid %u B, password %u B, its strongest AP ch=%u rssi=%d auth=%s%s; begin=%d\n",
                    unsigned(std::strlen(c.ssid)), unsigned(std::strlen(c.password)), unsigned(best->channel),
                    best->rssi, authName(best->auth), wpa3_ ? " (sae both, pmf capable, wifi first)" : "", int(r));
    else
      Serial.printf("[wifi] join: ssid %u B, password %u B, not in the scan: the driver scans for it; begin=%d\n",
                    unsigned(std::strlen(c.ssid)), unsigned(std::strlen(c.password)), int(r));
    beginStatus_ = r;
  }
  Join joining() const {
    if (!up_) return Join::Failed;
    if (beginStatus_ == WL_CONNECT_FAILED) return holdCoex(false), Join::Failed;   // esp_wifi_connect refused at once
    if (WiFi.status() == WL_CONNECTED) return holdCoex(false), joinedOnce_ = true, Join::Joined;
    if (wpa3_ && wpa3Cell.asked && millis() - dhcpAt_ >= 2000) {   // where DHCP stands (lwIP: 6 is SELECTING), every 2 s
      dhcpAt_ = millis();
      wifi_ap_record_t ap;
      const bool assoc = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
      esp_netif_t* ni = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
      struct netif* n = ni ? netif_get_by_index(esp_netif_get_netif_impl_index(ni)) : nullptr;
      const struct dhcp* d = n ? netif_dhcp_data(n) : nullptr;
      Serial.printf("[wifi] wpa3 cell: t=%u assoc=%d rssi=%d lwip_dhcp_state=%d tries=%d cpu=%u\n", unsigned(millis() - attemptAt_),
                    int(assoc), assoc ? int(ap.rssi) : 0, d ? int(d->state) : -1, d ? int(d->tries) : -1,
                    unsigned(getCpuFrequencyMhz()));
    }
    // A disconnect since the join, other than the core's own one retry's: the reason says why.
    if (disconnects_ != disconnectsAtJoin_ && (reason_ == WIFI_REASON_AUTH_FAIL || reason_ == WIFI_REASON_NO_AP_FOUND ||
                                               reason_ == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                                               reason_ == WIFI_REASON_HANDSHAKE_TIMEOUT ||
                                               reason_ == WIFI_REASON_ASSOC_FAIL || reason_ == WIFI_REASON_CONNECTION_FAIL)) {
      // The core retries once after the first failure: give the retry its chance.
      if (disconnects_ - disconnectsAtJoin_ < 2) return Join::Trying;
      holdCoex(false);
      return Join::Failed;
    }
    return Join::Trying;
  }
  // How long one try at the join may take, and the least time from one try's
  // start to the next. A WPA3 access point counts a station's SAE attempts and
  // makes one that comes back too soon wait (anti-clogging), and SAE itself
  // takes longer: so 30 s apart.
  static constexpr uint32_t kJoinMs = 15000, kJoinWpa3Ms = 30000;
  uint32_t joinBudgetMs() const { return wpa3_ ? kJoinWpa3Ms : kJoinMs; }
  uint32_t retryWaitMs() const {
    const uint32_t since = millis() - attemptAt_;
    return wpa3_ && since < kJoinWpa3Ms ? kJoinWpa3Ms - since : 0;
  }
  bool wpa3() const { return wpa3_; }
  // In the access point's company (authenticated and keyed) but maybe without an address yet.
  static bool associated() {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
  }
  bool joined() {
    const bool j = up_ && WiFi.status() == WL_CONNECTED;
    joinedOnce_ = joinedOnce_ || j;
    return j;
  }
  // The last disconnect's reason (the driver's wifi_err_reason_t), 0 for none, and how many since the join.
  uint8_t reason() const { return reason_; }
  const char* reasonName() const { return reason_ ? WiFi.disconnectReasonName(wifi_err_reason_t(reason_)) : "none"; }
  uint32_t disconnectsSinceJoin() const { return disconnects_ - disconnectsAtJoin_; }
  int beginStatus() const { return beginStatus_; }

 private:
  mutable uint32_t dhcpAt_ = 0;   // DEBUG wpa3's DHCP line

  // PSRAM, made at the first scan or join: internal RAM is the scarce kind.
  bool sightings() {
    if (heard_) return true;
    void* p = heap_caps_malloc(sizeof(wifiota::Sightings), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) heard_ = new (p) wifiota::Sightings;
    return heard_;
  }

  static const char* authName(uint8_t a) {
    switch (wifi_auth_mode_t(a)) {
      case WIFI_AUTH_OPEN: return "open";
      case WIFI_AUTH_WEP: return "wep";
      case WIFI_AUTH_WPA_PSK: return "wpa";
      case WIFI_AUTH_WPA2_PSK: return "wpa2";
      case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
      case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2-enterprise";
      case WIFI_AUTH_WPA3_PSK: return "wpa3";
      case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
      default: return "other";
    }
  }

  // Wi-Fi first on the shared radio while a WPA3 join runs, then back to the balance.
  void holdCoex(bool on) const {
    if (on == coexHeld_) return;
    coexHeld_ = on;
    esp_coex_preference_set(on ? ESP_COEX_PREFER_WIFI : ESP_COEX_PREFER_BALANCE);
  }

  // On the core's event task: counts and the last reason, read by the loop.
  static void onEvent(arduino_event_id_t e, arduino_event_info_t info) {
    switch (e) {
      case ARDUINO_EVENT_WIFI_SCAN_DONE:
        scanStatus_ = info.wifi_scan_done.status;
        ++scansDone_;
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        reason_ = info.wifi_sta_disconnected.reason;
        ++disconnects_;
        break;
      default: break;
    }
  }
  static inline volatile uint32_t scansDone_ = 0, scanStatus_ = 0, disconnects_ = 0;
  static inline volatile uint8_t reason_ = 0;

  bool up_ = false, everUp_ = false, handler_ = false, stopping_ = false, attempted_ = false;
  mutable bool joinedOnce_ = false, coexHeld_ = false;
  bool wpa3_ = false;
  uint32_t attemptAt_ = 0;
  uint32_t stopAt_ = 0, scansAtStart_ = 0, disconnectsAtStop_ = 0, disconnectsAtJoin_ = 0;
  int beginStatus_ = 0;
  wifiota::Sightings* heard_ = nullptr;
  bool fresh_ = false;   // heard_ holds this window's scan
#if defined(CONFIG_PM_ENABLE)
  esp_pm_lock_handle_t lock_ = nullptr;
#endif
};

}  // namespace hw
