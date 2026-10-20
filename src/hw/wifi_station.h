// The Wi-Fi station and its HTTPS fetches (docs/wifi-ota.md). The station is
// off unless a window is open (wifiota/schedule.h): up() brings the radio up
// in station mode, joined to nothing, with the credentials kept in RAM only
// (WiFi.persistent(false): the core would otherwise copy them into the
// driver's own NVS namespace); down() gives the heap back. ESP-NOW's duel
// radio (src/hw/air_espnow.h) runs the same radio and never at the same time:
// src/wifi_ota.h keeps the two apart.
//
// A fetch runs on a task of its own, so a TLS handshake (a second or two at
// 80 MHz) or a slow server never stalls the loop and the pet. A manifest
// lands in a buffer; an image goes straight to a PacketRing as
// offset-prefixed packets, which the loop hands to ota::Session::data(), the
// same path a page's writes take.
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

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

#include "hw/ota_esp.h"
#include "hw/tls_roots.h"
#include "wifiota/manifest.h"

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
    WiFi.scanDelete();
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
  // Drops the association, so a fetch blocked on its socket fails fast.
  void leave() {
    if (up_) WiFi.disconnect(false);
  }

  bool scanStart() { return up_ && WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING; }
  // -1 while it runs, -2 when it failed, else how many it heard (read with heard()).
  int16_t scanDone() { return WiFi.scanComplete(); }
  struct Heard {
    char ssid[33];
    int8_t rssi;
    bool secured;
  };
  Heard heard(int16_t i) {
    Heard h{};
    std::snprintf(h.ssid, sizeof h.ssid, "%s", WiFi.SSID(i).c_str());
    h.rssi = int8_t(WiFi.RSSI(i));
    h.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    return h;
  }
  void scanForget() { WiFi.scanDelete(); }

 private:
  bool up_ = false;
#if defined(CONFIG_PM_ENABLE)
  esp_pm_lock_handle_t lock_ = nullptr;
#endif
};

class HttpsFetch {
 public:
  enum class Status : uint8_t { Idle, Running, Done, Failed };
  static constexpr uint32_t kStackBytes = 12 * 1024;   // a TLS handshake's ECDSA and RSA checks run deep
  static constexpr size_t kPacketBytes = 240;          // a page's write size: Session::data takes any

  // A GET whose body (up to wifiota::kManifestMax bytes) lands in text().
  bool getText(const char* url) { return start(url, nullptr, 0, 0); }
  // A GET whose body goes to `ring` from `peer` as 4-byte offset and data
  // packets. `size` must match the server's Content-Length, when it sends one.
  bool getImage(const char* url, uint32_t size, PacketRing& ring, ota::Peer peer) { return start(url, &ring, size, peer); }

  Status status() const { return status_; }
  const char* text() const { return text_; }
  const char* error() const { return error_; }
  uint32_t bytes() const { return got_; }
  // Ask the task to stop; status() leaves Running once it has.
  void cancel() { cancel_ = true; }

 private:
  bool start(const char* url, PacketRing* ring, uint32_t size, ota::Peer peer) {
    if (status_ == Status::Running) return false;
    std::snprintf(url_, sizeof url_, "%s", url);
    ring_ = ring, size_ = size, peer_ = peer;
    got_ = 0, textLen_ = 0, text_[0] = 0, error_[0] = 0, cancel_ = false;
    status_ = Status::Running;
    if (xTaskCreate(&HttpsFetch::run, "wifi-fetch", kStackBytes, this, 1, nullptr) != pdPASS) {
      std::snprintf(error_, sizeof error_, "no task (heap %u)", unsigned(ESP.getFreeHeap()));
      status_ = Status::Failed;
      return false;
    }
    return true;
  }

  static void run(void* self) {
    auto* f = static_cast<HttpsFetch*>(self);
    const bool ok = f->work();
    f->status_ = ok ? Status::Done : Status::Failed;
    vTaskDelete(nullptr);
  }

  // The response body, as HTTPClient::writeToStream hands it over (chunked or not).
  struct Sink : Stream {
    HttpsFetch& f;
    uint8_t packet[4 + kPacketBytes];
    size_t fill = 0;
    explicit Sink(HttpsFetch& fetch) : f(fetch) {}
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* p, size_t n) override {
      if (f.cancel_) return 0;
      if (!f.ring_) {   // the manifest
        if (f.textLen_ + n > wifiota::kManifestMax) return 0;   // not a manifest: stop reading
        std::memcpy(f.text_ + f.textLen_, p, n);
        f.textLen_ += n;
        f.text_[f.textLen_] = 0;
        f.got_ += uint32_t(n);
        return n;
      }
      for (size_t i = 0; i < n;) {
        if (fill == 0) {
          const uint32_t at = f.got_;
          packet[0] = uint8_t(at), packet[1] = uint8_t(at >> 8), packet[2] = uint8_t(at >> 16), packet[3] = uint8_t(at >> 24);
          fill = 4;
        }
        const size_t take = std::min(n - i, sizeof packet - fill);
        std::memcpy(packet + fill, p + i, take);
        fill += take, i += take, f.got_ += uint32_t(take);
        if (fill == sizeof packet && !send()) return 0;
      }
      return n;
    }
    // The loop drains the ring; a full one waits for it.
    bool send() {
      while (!f.ring_->push(packet, fill, f.peer_)) {
        if (f.cancel_) return false;
        vTaskDelay(pdMS_TO_TICKS(2));
      }
      fill = 0;
      return true;
    }
  };

  bool fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(error_, sizeof error_, fmt, ap);
    va_end(ap);
    return false;
  }

  bool work() {
    const bool tls = std::strncmp(url_, "https://", 8) == 0;
    std::unique_ptr<WiFiClient> client;
    if (tls) {
      auto* secure = new WiFiClientSecure;
      secure->setCACert(kTlsRoots);   // the pinned roots, nothing else (tls_roots.h)
      secure->setHandshakeTimeout(20);
      client.reset(secure);
    } else {
      client.reset(new WiFiClient);   // only a cable's `DEBUG ota check http://...` asks for this
    }
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(10000);
    http.setTimeout(15000);
    http.setUserAgent("blorbarium");
    if (!http.begin(*client, url_)) return fail("bad url");
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
      http.end();
      return code < 0 ? fail("%s", HTTPClient::errorToString(code).c_str()) : fail("http %d", code);
    }
    const int len = http.getSize();
    if (ring_ && len >= 0 && uint32_t(len) != size_) {
      http.end();
      return fail("server says %d bytes, the manifest %u", len, unsigned(size_));
    }
    Sink sink(*this);
    const int wrote = http.writeToStream(&sink);
    const bool flushed = !ring_ || sink.fill == 0 || sink.send();
    http.end();
    if (cancel_) return fail("cancelled");
    if (wrote < 0) return fail("%s after %u bytes", HTTPClient::errorToString(wrote).c_str(), unsigned(got_));
    if (!flushed) return fail("cancelled");
    if (ring_ && got_ != size_) return fail("short: %u of %u bytes", unsigned(got_), unsigned(size_));
    return true;
  }

  char url_[160] = "";
  PacketRing* ring_ = nullptr;
  uint32_t size_ = 0;
  ota::Peer peer_ = 0;
  std::atomic<Status> status_{Status::Idle};
  std::atomic<bool> cancel_{false};
  std::atomic<uint32_t> got_{0};
  size_t textLen_ = 0;
  char text_[wifiota::kManifestMax + 1] = "";
  char error_[80] = "";
};

}  // namespace hw
