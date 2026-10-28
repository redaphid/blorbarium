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
#include <sdkconfig.h>
#if defined(CONFIG_PM_ENABLE)
#include <esp_pm.h>
#endif

#include <algorithm>
#include <cstring>

#include "wifiota/improv.h"
#include "wifiota/remembered.h"

namespace hw {

class WifiStation {
 public:
  // Active, at most this long on each channel. The scan's end is the driver's
  // SCAN_DONE event, not the core's scanComplete(), which calls a scan failed
  // after 20 times this whatever the driver is doing: with Bluetooth sharing
  // the radio a scan takes longer than that, and the "failed" scan then went
  // on under the join and refused it.
  static constexpr uint32_t kScanMsPerChannel = 120;
  static constexpr uint8_t kSeenMax = 24;   // the strongest this many a scan finds are looked at
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
    seen_ = 0;
#if defined(CONFIG_PM_ENABLE)
    if (lock_) esp_pm_lock_release(lock_);
#endif
    Serial.printf("[wifi] radio down after %u ms quiet heap=%u\n", unsigned(waited), unsigned(ESP.getFreeHeap()));
  }

  bool isUp() const { return up_; }
  bool everUp() const { return everUp_; }   // this boot: the core's tasks and netifs exist already

  // ---- the scan: what is in range ----
  bool scan() {
    seen_ = 0;
    if (!aps_) {   // PSRAM, made at the first scan: internal RAM is the scarce kind
      aps_ = static_cast<Ap*>(heap_caps_calloc(kSeenMax, sizeof(Ap), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      views_ = static_cast<wifiota::Seen*>(heap_caps_calloc(kSeenMax, sizeof(wifiota::Seen), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!aps_ || !views_) return false;
    }
    scansAtStart_ = scansDone_;
    const int16_t r = WiFi.scanNetworks(true, false, false, kScanMsPerChannel);
    if (r != WIFI_SCAN_RUNNING) Serial.printf("[wifi] the scan would not start (%d)\n", int(r));
    return r == WIFI_SCAN_RUNNING;
  }
  // -1 still scanning, -2 the driver said it failed, else how many networks
  // it found (the strongest kSeenMax of them copied for seen()).
  int scanned() {
    if (scansDone_ == scansAtStart_ || !aps_) return WIFI_SCAN_RUNNING;
    if (scanStatus_ != 0) return WIFI_SCAN_FAILED;
    const int16_t n = WiFi.scanComplete();   // the driver's count: the core reset its own clock at SCAN_DONE
    if (n < 0) return n;
    seen_ = 0;
    for (int16_t i = 0; i < n; ++i) {
      Ap a;
      std::snprintf(a.ssid, sizeof a.ssid, "%s", WiFi.SSID(i).c_str());
      a.rssi = int(WiFi.RSSI(i));
      a.channel = uint8_t(WiFi.channel(i));
      a.auth = uint8_t(WiFi.encryptionType(i));
      if (const uint8_t* b = WiFi.BSSID(i)) std::memcpy(a.bssid, b, 6);
      if (seen_ < kSeenMax) aps_[seen_++] = a;
      else {   // keep the strongest
        Ap* weakest = std::min_element(aps_, aps_ + seen_, [](const Ap& x, const Ap& y) { return x.rssi < y.rssi; });
        if (a.rssi > weakest->rssi) *weakest = a;
      }
    }
    WiFi.scanDelete();
    for (uint8_t i = 0; i < seen_; ++i) views_[i] = wifiota::Seen{aps_[i].ssid, aps_[i].rssi};
    return n;
  }
  uint32_t scanStatus() const { return scanStatus_; }
  const wifiota::Seen* seen() const { return views_; }
  uint8_t seenCount() const { return seen_; }
  // A scan that has run too long: stopped, so nothing is left running under the join.
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
    const Ap* best = nullptr;
    for (uint8_t i = 0; i < seen_; ++i)
      if (!std::strcmp(aps_[i].ssid, c.ssid) && (!best || aps_[i].rssi > best->rssi)) best = &aps_[i];
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
      r = WiFi.begin(c.ssid, pass, best->channel, best->bssid, false);
      wifi_config_t conf;
      if (r != WL_CONNECT_FAILED && esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        conf.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
        conf.sta.pmf_cfg.capable = true;
        conf.sta.pmf_cfg.required = false;
        conf.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        if (esp_wifi_set_config(WIFI_IF_STA, &conf) != ESP_OK) r = WL_CONNECT_FAILED;
      } else {
        r = WL_CONNECT_FAILED;
      }
      if (r != WL_CONNECT_FAILED) {
        holdCoex(true);
        if (esp_wifi_connect() != ESP_OK) r = WL_CONNECT_FAILED;
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
  struct Ap {
    char ssid[33] = "";
    int rssi = -127;
    uint8_t channel = 0;
    uint8_t auth = 0;
    uint8_t bssid[6] = {};
  };

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
  Ap* aps_ = nullptr;
  wifiota::Seen* views_ = nullptr;
  uint8_t seen_ = 0;
#if defined(CONFIG_PM_ENABLE)
  esp_pm_lock_handle_t lock_ = nullptr;
#endif
};

}  // namespace hw
