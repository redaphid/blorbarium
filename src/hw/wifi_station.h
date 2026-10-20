// The Wi-Fi station and its one HTTPS GET at a time (docs/wifi-ota.md). The
// station is off unless a window is open (wifiota/schedule.h): up() brings the
// radio up in station mode, joined to nothing, with the credentials kept in
// RAM only (WiFi.persistent(false): the core would otherwise copy them into
// the driver's own NVS namespace); down() gives the heap back. ESP-NOW's duel
// radio (src/hw/air_espnow.h) runs the same radio and never at the same time:
// src/wifi_ota.h keeps the two apart.
//
// Everything runs on the loop, no task of its own. open() blocks the loop for
// the DNS lookup, the TCP connect, the TLS handshake and the response's
// headers, each bounded by a timeout below (a second or two in practice; the
// window logs how long). The body is then read without waiting: the socket
// is non-blocking, so read() takes what has arrived and returns.
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <sdkconfig.h>
#if defined(CONFIG_PM_ENABLE)
#include <esp_pm.h>
#endif

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

#include "hw/tls_roots.h"

namespace hw {

class WifiStation {
 public:
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
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    up_ = false;
#if defined(CONFIG_PM_ENABLE)
    if (lock_) esp_pm_lock_release(lock_);
#endif
    Serial.printf("[wifi] radio down heap=%u\n", unsigned(ESP.getFreeHeap()));
  }

  bool isUp() const { return up_; }
  void join(const char* ssid, const char* password) { WiFi.begin(ssid, password[0] ? password : nullptr); }
  bool joined() { return up_ && WiFi.status() == WL_CONNECTED; }

 private:
  bool up_ = false;
#if defined(CONFIG_PM_ENABLE)
  esp_pm_lock_handle_t lock_ = nullptr;
#endif
};

class HttpsGet {
 public:
  // The loop stalls for at most about the sum of these (and the DNS lookup)
  // in open(): under the trial watchdog's 20 s (src/hw/ota_esp.h).
  static constexpr uint16_t kConnectMs = 4000;
  static constexpr uint32_t kHandshakeS = 6;
  static constexpr uint16_t kHeadersMs = 4000;

  // Connect, send the GET, read the response's headers. False, with error()
  // saying why, for anything but a 200. https:// only with the pinned roots
  // (tls_roots.h); http:// only for a cable's `DEBUG ota check http://...`.
  bool open(const char* url) {
    close();
    error_[0] = 0;
    if (std::strncmp(url, "https://", 8) == 0) {
      auto* secure = new WiFiClientSecure;
      secure->setCACert(kTlsRoots);
      secure->setHandshakeTimeout(kHandshakeS);
      client_.reset(secure);
    } else {
      client_.reset(new WiFiClient);
    }
    http_.setReuse(false);
    http_.setConnectTimeout(kConnectMs);
    http_.setTimeout(kHeadersMs);
    http_.setUserAgent("blorbarium");
    if (!http_.begin(*client_, url)) return fail("bad url");
    const int code = http_.GET();
    if (code != HTTP_CODE_OK) {
      code < 0 ? fail("%s", HTTPClient::errorToString(code).c_str()) : fail("http %d", code);
      close();
      return false;
    }
    return true;
  }

  // Content-Length, or -1 when the server sent none (a chunked body).
  int size() { return http_.getSize(); }
  // The whole body as text, for the manifest: blocks until it is read (it is small).
  String text() { return http_.getString(); }

  // Up to `cap` bytes of the body that have already arrived, without waiting:
  // 0 when none have yet, -1 when the connection has closed.
  int read(uint8_t* out, size_t cap) {
    WiFiClient* s = http_.getStreamPtr();
    if (!s) return -1;
    const int avail = s->available();
    if (avail <= 0) return s->connected() ? 0 : -1;
    return s->read(out, std::min(size_t(avail), cap));
  }

  void close() {
    http_.end();
    client_.reset();
  }

  const char* error() const { return error_; }

 private:
  bool fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(error_, sizeof error_, fmt, ap);
    va_end(ap);
    return false;
  }

  std::unique_ptr<WiFiClient> client_;
  HTTPClient http_;
  char error_[80] = "";
};

}  // namespace hw
