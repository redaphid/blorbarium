// The Wi-Fi station (docs/wifi-ota.md). Off unless a window is open
// (wifiota/schedule.h): up() brings the radio up in station mode, joined to
// nothing, with the credentials kept in RAM only (WiFi.persistent(false): the
// core would otherwise copy them into the driver's own NVS namespace); down()
// gives the heap back. ESP-NOW's duel radio (src/hw/air_espnow.h) runs the
// same radio and never at the same time: src/wifi_ota.h keeps the two apart.
//
// Nothing here waits: the scan runs on the driver's task and is polled, the
// join is polled, and the HTTPS GET runs on its own task (net_job.h).
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
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
  // Active: about 2 s for the 13 channels. The core calls a scan failed after
  // 20 times this (3 s), so it must leave room for Bluetooth's share of the radio.
  static constexpr uint32_t kScanMsPerChannel = 150;
  static constexpr uint8_t kSeenMax = 24;              // the strongest this many a scan finds are looked at

  bool up() {
    if (up_) return true;
    const uint32_t before = ESP.getFreeHeap();
    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_STA)) return false;
    WiFi.setAutoReconnect(false);   // the window decides when to try again
    up_ = true;
#if defined(CONFIG_PM_ENABLE)
    // Light sleep between packets would hold a download to the AP's beacons.
    if (!lock_) esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "wifi", &lock_);
    if (lock_) esp_pm_lock_acquire(lock_);
#endif
    Serial.printf("[wifi] radio up heap %u -> %u big=%u\n", unsigned(before), unsigned(ESP.getFreeHeap()),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    return true;
  }

  void down() {
    if (!up_) return;
    WiFi.scanDelete();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    up_ = false;
    seen_ = 0;
#if defined(CONFIG_PM_ENABLE)
    if (lock_) esp_pm_lock_release(lock_);
#endif
    Serial.printf("[wifi] radio down heap=%u\n", unsigned(ESP.getFreeHeap()));
  }

  bool isUp() const { return up_; }

  // ---- the scan: what is in range ----
  bool scan() {
    seen_ = 0;
    if (!aps_) {   // PSRAM, made at the first scan: internal RAM is the scarce kind
      aps_ = static_cast<Ap*>(heap_caps_calloc(kSeenMax, sizeof(Ap), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      views_ = static_cast<wifiota::Seen*>(heap_caps_calloc(kSeenMax, sizeof(wifiota::Seen), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!aps_ || !views_) return false;
    }
    return WiFi.scanNetworks(true, false, false, kScanMsPerChannel) == WIFI_SCAN_RUNNING;
  }
  // -1 still scanning, -2 failed, else how many networks it found (the
  // strongest kSeenMax of them copied for seen()).
  int scanned() {
    const int16_t n = WiFi.scanComplete();
    if (n < 0 || !aps_) return n;
    seen_ = 0;
    for (int16_t i = 0; i < n; ++i) {
      Ap a;
      std::snprintf(a.ssid, sizeof a.ssid, "%s", WiFi.SSID(i).c_str());
      a.rssi = int(WiFi.RSSI(i));
      a.channel = uint8_t(WiFi.channel(i));
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
  const wifiota::Seen* seen() const { return views_; }
  uint8_t seenCount() const { return seen_; }

  // ---- the join ----
  enum class Join : uint8_t { Trying, Joined, Failed };
  // The network's strongest access point from the last scan, straight to its
  // channel and BSSID, so no second scan is needed.
  void join(const wifiota::improv::Credentials& c) {
    const Ap* best = nullptr;
    for (uint8_t i = 0; i < seen_; ++i)
      if (!std::strcmp(aps_[i].ssid, c.ssid) && (!best || aps_[i].rssi > best->rssi)) best = &aps_[i];
    const char* pass = c.password[0] ? c.password : nullptr;
    if (best) WiFi.begin(c.ssid, pass, best->channel, best->bssid);
    else WiFi.begin(c.ssid, pass);
  }
  Join joining() const {
    if (!up_) return Join::Failed;
    switch (WiFi.status()) {
      case WL_CONNECTED: return Join::Joined;
      case WL_NO_SSID_AVAIL:
      case WL_CONNECT_FAILED: return Join::Failed;
      default: return Join::Trying;
    }
  }
  bool joined() const { return up_ && WiFi.status() == WL_CONNECTED; }

 private:
  struct Ap {
    char ssid[33] = "";
    int rssi = -127;
    uint8_t channel = 0;
    uint8_t bssid[6] = {};
  };
  bool up_ = false;
  Ap* aps_ = nullptr;
  wifiota::Seen* views_ = nullptr;
  uint8_t seen_ = 0;
#if defined(CONFIG_PM_ENABLE)
  esp_pm_lock_handle_t lock_ = nullptr;
#endif
};

}  // namespace hw
