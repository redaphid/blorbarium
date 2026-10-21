// One HTTPS GET at a time, on its own task, so the loop never waits for a DNS
// lookup, a TCP connect, a TLS handshake or a slow server (owner, 2026-10-21:
// "nothing that blocks the loop"). The loop starts it, polls state(), takes
// the body as it arrives, and cancels it; the task ends itself, and only once
// it has (busy() false) may the radio go down (src/wifi_ota.h).
//
// The task runs on core 1 at the loop's own priority (1), beside the loop and
// never above it, so core 0 stays Bluetooth's, Wi-Fi's own driver's, ESP-NOW's
// and the panel's send task's, and the 250 Hz sense task (priority 2)
// preempts it wherever it runs. Its stack is internal RAM (TLS needs about
// 7 KB of it), made for the GET and given back when it ends. The TLS record
// buffers go to PSRAM (src/wifi_ota.h, tlsCalloc).
//
// A text GET (the manifest) reads the whole body, at most kTextMax bytes,
// into text(). A streamed GET (the image) feeds a ring of kRingBytes in PSRAM
// that the loop drains with take(): when the loop is slow the task waits for
// room, so the download goes at the speed the flash takes it.
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

#include "hw/tls_roots.h"

namespace hw {

class NetJob {
 public:
  enum class State : uint8_t { Idle, Opening, Body, Done, Failed };

  static constexpr size_t kTextMax = 2048;              // a manifest is a few hundred bytes
  static constexpr size_t kRingBytes = 32 * 1024;
  static constexpr uint32_t kStackBytes = 12 * 1024;
  static constexpr uint16_t kConnectMs = 5000;
  static constexpr uint32_t kHandshakeS = 8;
  static constexpr uint16_t kHeadersMs = 6000;
  static constexpr uint32_t kStallMs = 20000;           // a body that sends nothing this long has failed

  // Start a GET of `url`: https:// with the pinned roots (tls_roots.h), or
  // http:// (only a cable's `DEBUG ota check http://...` asks for one).
  // False when one is still running or the task could not be made.
  bool start(const char* url, bool text) {
    if (busy()) return false;
    std::snprintf(url_, sizeof url_, "%s", url);
    text_ = text;
    textLen_ = 0;
    size_ = -1;
    got_ = 0;
    error_[0] = 0;
    cancel_ = false;
    openMs_ = 0;
    stackFree_ = 0;
    if (!text) {
      if (!ringStore_) ringStore_ = static_cast<uint8_t*>(heap_caps_malloc(kRingBytes + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!ringStore_) return fail("no PSRAM for the ring"), false;
      ring_ = xStreamBufferCreateStatic(kRingBytes, 1, ringStore_, &ringStatic_);
    }
    state_ = State::Opening;
    running_ = true;
    startedAt_ = millis();
    if (xTaskCreatePinnedToCore(&NetJob::run, "wifiget", kStackBytes, this, 1, nullptr, 1) != pdPASS) {
      running_ = false;
      return fail("no RAM for the task (heap %u)", unsigned(ESP.getFreeHeap())), false;
    }
    return true;
  }

  State state() const { return state_; }
  bool busy() const { return running_; }
  // The task stops at its next chance: a read, a wait for room, or the end of
  // the blocking call it is in (each bounded by the timeouts above).
  void cancel() { cancel_ = true; }
  uint32_t runningMs() const { return running_ ? millis() - startedAt_ : 0; }

  int size() const { return size_; }                    // Content-Length, -1 when none was sent
  const char* text() const { return text_buf_; }
  size_t textLen() const { return textLen_; }
  size_t received() const { return got_; }
  const char* error() const { return error_; }
  uint32_t openMs() const { return openMs_; }           // DNS, connect, TLS and headers
  uint32_t stackFree() const { return stackFree_; }     // the task's stack never used, at its end

  // The loop: up to `cap` streamed bytes that have arrived, without waiting.
  size_t take(uint8_t* out, size_t cap) { return ring_ ? xStreamBufferReceive(ring_, out, cap, 0) : 0; }
  size_t waiting() const { return ring_ ? xStreamBufferBytesAvailable(ring_) : 0; }
  // Once the task has ended: the ring emptied for the next GET.
  void reset() {
    if (busy()) return;
    if (ring_) xStreamBufferReset(ring_);
    state_ = State::Idle;
  }

 private:
  static void run(void* arg) {
    auto* self = static_cast<NetJob*>(arg);
    self->get();   // everything it made is gone when this returns
    self->stackFree_ = uxTaskGetStackHighWaterMark(nullptr);
    self->running_ = false;
    vTaskDelete(nullptr);
  }

  void get() {
    std::unique_ptr<WiFiClient> client;
    if (std::strncmp(url_, "https://", 8) == 0) {
      auto* secure = new WiFiClientSecure;
      secure->setCACert(kTlsRoots);
      secure->setHandshakeTimeout(kHandshakeS);
      client.reset(secure);
    } else {
      client.reset(new WiFiClient);
    }
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(kConnectMs);
    http.setTimeout(kHeadersMs);
    http.setUserAgent("blorbarium");
    const uint32_t t0 = millis();
    if (!http.begin(*client, url_)) return finish(fail("bad url"));
    const int code = http.GET();
    openMs_ = millis() - t0;
    if (cancel_) return finish(fail("cancelled"), http);
    if (code != HTTP_CODE_OK)
      return finish(code < 0 ? fail("%s", HTTPClient::errorToString(code).c_str()) : fail("http %d", code), http);
    size_ = http.getSize();
    WiFiClient* s = http.getStreamPtr();
    if (!s) return finish(fail("no stream"), http);
    if (text_) {
      if (size_ > int(kTextMax)) return finish(fail("%d bytes is not a manifest", size_), http);
      state_ = State::Body;
      const size_t want = size_ >= 0 ? size_t(size_) : kTextMax;
      uint32_t heard = millis();
      while (textLen_ < want && !cancel_) {
        const int n = s->available() > 0 ? s->read(reinterpret_cast<uint8_t*>(text_buf_) + textLen_, want - textLen_) : 0;
        if (n > 0) textLen_ += size_t(n), heard = millis();
        else if (!s->connected()) break;
        else if (millis() - heard > kStallMs) return finish(fail("the manifest stalled"), http);
        else vTaskDelay(pdMS_TO_TICKS(10));
      }
      text_buf_[textLen_] = 0;
      if (cancel_) return finish(fail("cancelled"), http);
      if (size_ >= 0 && textLen_ != size_t(size_)) return finish(fail("the manifest was cut at %u bytes", unsigned(textLen_)), http);
      return finish(State::Done, http);
    }
    if (size_ <= 0) return finish(fail("no length"), http);
    state_ = State::Body;
    uint8_t buf[1024];
    uint32_t heard = millis();
    while (got_ < size_t(size_) && !cancel_) {
      const int avail = s->available();
      if (avail > 0) {
        const int n = s->read(buf, std::min(sizeof buf, size_t(avail)));
        if (n <= 0) continue;
        for (size_t off = 0; off < size_t(n) && !cancel_;)   // wait for the loop to make room
          off += xStreamBufferSend(ring_, buf + off, size_t(n) - off, pdMS_TO_TICKS(50));
        got_ += size_t(n);
        heard = millis();
      } else if (!s->connected()) {
        break;
      } else if (millis() - heard > kStallMs) {
        return finish(fail("stalled at %u bytes", unsigned(got_)), http);
      } else {
        vTaskDelay(pdMS_TO_TICKS(5));
      }
    }
    if (cancel_) return finish(fail("cancelled"), http);
    if (got_ != size_t(size_)) return finish(fail("the connection closed at %u of %d bytes", unsigned(got_), size_), http);
    finish(State::Done, http);
  }

  void finish(State s) { state_ = s; }
  void finish(State s, HTTPClient& http) {
    http.end();
    state_ = s;
  }

  State fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(error_, sizeof error_, fmt, ap);
    va_end(ap);
    return State::Failed;
  }

  char url_[200] = "";
  bool text_ = true;
  char text_buf_[kTextMax + 1] = "";
  volatile size_t textLen_ = 0, got_ = 0;
  volatile int size_ = -1;
  volatile uint32_t openMs_ = 0, stackFree_ = 0, startedAt_ = 0;
  char error_[96] = "";
  std::atomic<bool> running_{false}, cancel_{false};
  std::atomic<State> state_{State::Idle};
  uint8_t* ringStore_ = nullptr;
  StaticStreamBuffer_t ringStatic_;
  StreamBufferHandle_t ring_ = nullptr;
};

}  // namespace hw
