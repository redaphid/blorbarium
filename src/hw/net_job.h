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
// Internal RAM is the scarce kind (a duel's radio takes about 37 KB of it):
// everything here but a few flags lives in PSRAM, made at the first GET and
// kept. The ring is a plain single-writer, single-reader one (not FreeRTOS's
// stream buffer, whose code sits in IRAM).
//
// A text GET (the manifest) reads the whole body, at most kTextMax bytes,
// into text(). A streamed GET (the image) feeds a ring of kRingBytes that the
// loop drains with take(): when the loop is slow the task waits for room, so
// the download goes at the speed the flash takes it.
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <optional>

#include "hw/tls_roots.h"
#include "rdebug/remote.h"
#include "wifiota/net_clock.h"

namespace hw {

// The least internal RAM seen free in a window, sampled: ESP.getMinFreeHeap()
// is the boot's, so it cannot say which window, or which part of it, dipped.
// `tls` at every mbedtls allocation (wififw::tlsCalloc: the handshake), `io`
// at every write and read of a GET's body (the POST going out, the reply
// coming in), `loop` once a loop pass. src/wifi_ota.h resets them as a
// window opens and logs them as it closes. A diagnostic: a race between the
// tasks only loses a sample.
struct NetLow {
  volatile uint32_t tls = UINT32_MAX, io = UINT32_MAX, loop = UINT32_MAX;
  void reset() { tls = io = loop = UINT32_MAX; }
  static void sample(volatile uint32_t& low) {
    const uint32_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (f < low) low = f;
  }
};
inline NetLow netLow;

// An HTTPS client whose writes go to TLS at most rdebug::kWriteChunk bytes at
// a time, each straight from the caller's buffer (PSRAM): HTTPClient hands a
// POST's whole body to one write(), and an 8.8 KB one failed on the badge at
// RSSI -77 ("Failed to send chunk!") with internal RAM down to 9.9 KB. A
// piece that fails is tried once more 100 ms later.
//
// Its TLS state in PSRAM too: the core makes its sslclient_context (2,176 B:
// the SSL context and config, the DRBG, the entropy pool, two certificate
// holders) with new, and malloc keeps anything under 4 KB in internal RAM,
// which a provision's check-in took down to 3.9 KB (0.7.35). A plain C
// struct: the core's delete frees it wherever it is. No PSRAM: the core's stays.
class ChunkedTls : public WiFiClientSecure {
 public:
  ChunkedTls() {
    auto* ps = static_cast<sslclient_context*>(heap_caps_malloc(sizeof(sslclient_context), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!ps) return;
    ssl_init(ps);
    ps->socket = sslclient->socket;
    ps->handshake_timeout = sslclient->handshake_timeout;
    delete sslclient;
    sslclient = ps;
  }
  size_t write(const uint8_t* p, size_t n) override {
    return rdebug::writeChunked(
        p, n, rdebug::kWriteChunk,
        [this](const uint8_t* q, size_t k) {
          pacedMs += rdebug::pace([] { return uint32_t(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)); },
                                  [](uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); });
          const size_t took = WiFiClientSecure::write(q, k);
          NetLow::sample(netLow.io);
          return took;
        },
        [] { vTaskDelay(pdMS_TO_TICKS(100)); });
  }
  static inline volatile uint32_t pacedMs = 0;   // rdebug::pace's waits, all bodies since boot
};

class NetJob {
 public:
  enum class State : uint8_t { Idle, Opening, Body, Done, Failed };

  static constexpr size_t kTextMax = 16 * 1024;         // a manifest is a few hundred bytes, a pet's line pack (#59) some KB
  static constexpr size_t kRingBytes = 32 * 1024;
  // The task's stack, internal RAM while it runs (FreeRTOS on this framework
  // refuses a PSRAM stack): a TLS handshake through the P-384 chain needs some
  // KB of it, a plain HTTP GET (a cable's `DEBUG ota check http://...`) less.
  // The TLS one is `DEBUG wifi stack <KB>` (RAM only) to find the least that
  // holds: every GET logs the bytes its task never used.
  // 7 KB: #27's board run used 5,256 B of 10 KB for the manifest's handshake
  // through the P-384 chain; the image's GET uses less (its reads decrypt, and
  // the hash and the flash writes are the loop's).
  static constexpr uint32_t kStackTlsDefault = 7 * 1024;
  static constexpr uint32_t kStackHttpBytes = 6 * 1024;
  static inline uint32_t stackTlsBytes = kStackTlsDefault;
  static constexpr uint16_t kConnectMs = 5000;
  static constexpr uint32_t kHandshakeS = 20;   // 4.5 to 8.7 s seen at RSSI -75: 8 cut good ones off
  static constexpr uint16_t kHeadersMs = 6000;
  static constexpr uint32_t kStallMs = 20000;           // a body that sends nothing this long has failed

  // Start a GET of `url`: https:// with the pinned roots (tls_roots.h), or
  // http:// (only a cable's `DEBUG ota check http://...` asks for one).
  // False when one is still running, or PSRAM or the task could not be had.
  // `post`: a JSON body to POST instead (a text GET's reply, the pet's line pack, #59).
  // `borrowLen` > 0: `post` is that many bytes the caller keeps alive (in
  // PSRAM) until busy() is false, sent as it is rather than copied: the
  // remote debug report, which may carry a screenshot (src/remote_debug.h).
  bool start(const char* url, bool text, const char* post = nullptr, size_t borrowLen = 0) {
    if (busy()) return false;
    reap();
    if (!m_) {
      void* p = heap_caps_malloc(sizeof(Mem), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!p) return false;
      m_ = new (p) Mem;
    }
    std::snprintf(m_->url, sizeof m_->url, "%s", url);
    std::snprintf(m_->post, sizeof m_->post, "%s", post && !borrowLen ? post : "");
    body_ = borrowLen ? post : m_->post;
    bodyLen_ = borrowLen ? borrowLen : std::strlen(m_->post);
    text_ = text;
    textLen_ = 0;
    size_ = -1;
    got_ = 0;
    m_->error[0] = 0;
    m_->text[0] = 0;
    cancel_ = false;
    openMs_ = 0;
    stackFree_ = 0;
    head_ = tail_ = 0;
    clockAt_ = 0;
    state_ = State::Opening;
    running_ = true;
    startedAt_ = millis();
    stackBytes_ = std::strncmp(url, "https://", 8) == 0 ? stackTlsBytes : kStackHttpBytes;
    if (xTaskCreatePinnedToCore(&NetJob::run, "wifiget", stackBytes_, this, 1, &task_, 1) != pdPASS) {
      task_ = nullptr;
      running_ = false;
      state_ = fail("no RAM for the task (heap %u)", unsigned(ESP.getFreeHeap()));
      return false;
    }
    return true;
  }

  State state() const { return state_; }
  bool busy() const { return running_; }
  // The task stops at its next chance: a read, a wait for room, or the end of
  // the blocking call it is in (each bounded by the timeouts above).
  void cancel() { cancel_ = true; }

  int size() const { return size_; }                    // Content-Length, -1 when none was sent
  const char* text() const { return m_ ? m_->text : ""; }
  size_t textLen() const { return textLen_; }
  size_t received() const { return got_; }
  const char* error() const { return m_ ? m_->error : "no PSRAM"; }
  uint32_t openMs() const { return openMs_; }           // DNS, connect, TLS and headers
  uint32_t stackFree() const { return stackFree_; }     // the task's stack never used, at its end
  uint32_t stackBytes() const { return stackBytes_; }
  // Internal heap and its largest block just after the handshake and headers.
  uint32_t heapAfterOpen() const { return heapAfterOpen_; }
  uint32_t bigAfterOpen() const { return bigAfterOpen_; }

  // The wall time the last HTTPS reply carried (its Date, and the site's zone
  // when it sent X-Blorb-Tz), whatever its status: a 202 is as sure of the
  // time as a 200. Plain http is never believed (anything on the way could
  // say anything). Taken once, after the task has ended, aged by the time
  // since the headers came.
  struct Clock {
    uint32_t unixSeconds;
    std::optional<int16_t> tzMinutes;
  };
  std::optional<Clock> takeClock(uint32_t nowMs) {
    if (busy() || !clockAt_) return std::nullopt;
    const Clock c{clockUnix_ + (nowMs - clockAt_) / 1000, hasTz_ ? std::optional<int16_t>(tz_) : std::nullopt};
    clockAt_ = 0;
    return c;
  }

  // The loop: up to `cap` streamed bytes that have arrived, without waiting.
  size_t take(uint8_t* out, size_t cap) {
    const uint32_t head = head_, tail = tail_;
    size_t n = std::min(cap, size_t(head - tail));
    std::atomic_thread_fence(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i) out[i] = m_->ring[(tail + i) % kRingBytes];
    std::atomic_thread_fence(std::memory_order_release);
    tail_ = tail + uint32_t(n);
    return n;
  }
  size_t waiting() const { return size_t(head_ - tail_); }
  // Once the task has ended: ready for the next GET.
  void reset() {
    if (busy()) return;
    reap();
    head_ = tail_ = 0;
    state_ = State::Idle;
  }

 private:
  struct Mem {
    char url[200];
    char post[2 * 8192 + 512];   // a genome (at most 8 KB) as hex, and the rest of the pet-lines body
    char text[kTextMax + 1];
    char error[96];
    uint8_t chunk[1024];   // off the task's stack
    uint8_t ring[kRingBytes];
  };

  static void run(void* arg) {
    auto* self = static_cast<NetJob*>(arg);
    self->get();   // everything it made is gone when this returns
    self->stackFree_ = uxTaskGetStackHighWaterMark(nullptr);
    self->running_ = false;
    // It waits to be deleted by the loop (reap) rather than deleting itself: a
    // task that deletes itself keeps its stack until the idle task gets round
    // to it, and a second GET started at once (the pet's lines straight after
    // the manifest) then ran its TLS handshake with the first's 7 KB still
    // held, down to 2.3 KB of internal RAM left.
    vTaskSuspend(nullptr);
  }

  // The ended task, deleted from the loop: its stack is free when this returns
  // (it is suspended, or ready on the loop's core, so never running now).
  void reap() {
    if (task_ && !running_) vTaskDelete(task_), task_ = nullptr;
  }

  // The task: `n` bytes into the ring, waiting while it is full.
  void put(const uint8_t* p, size_t n) {
    while (n && !cancel_) {
      const uint32_t head = head_, tail = tail_;
      const size_t room = kRingBytes - size_t(head - tail);
      if (!room) {
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      const size_t k = std::min(room, n);
      std::atomic_thread_fence(std::memory_order_acquire);   // the loop is done with that room
      for (size_t i = 0; i < k; ++i) m_->ring[(head + i) % kRingBytes] = p[i];
      std::atomic_thread_fence(std::memory_order_release);
      head_ = head + uint32_t(k);   // published after the bytes
      p += k, n -= k;
    }
  }

  // HTTPClient's "connection refused" is any open that failed: a lookup, a
  // TCP connect or a TLS handshake. Which one, by asking the resolver again.
  State failConnect(uint32_t t0) {
    char host[96] = "";
    const char* h = std::strstr(m_->url, "://");
    h = h ? h + 3 : m_->url;
    size_t n = 0;
    while (h[n] && h[n] != '/' && h[n] != ':' && n + 1 < sizeof host) host[n] = h[n], ++n;
    host[n] = 0;
    const uint32_t failedAfter = millis() - t0;
    IPAddress ip;
    if (!WiFi.hostByName(host, ip)) return fail("connection refused after %u ms: dns, %s did not resolve", unsigned(failedAfter), host);
    return fail("connection refused after %u ms: dns ok now (%s), so the tcp connect or the tls handshake", unsigned(failedAfter),
                ip.toString().c_str());
  }

  void get() {
    std::unique_ptr<WiFiClient> client;
    if (std::strncmp(m_->url, "https://", 8) == 0) {
      auto* secure = new ChunkedTls;
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
    if (!http.begin(*client, m_->url)) return finish(fail("bad url"));
    if (bodyLen_) http.addHeader("content-type", "application/json");
    const bool secure = std::strncmp(m_->url, "https://", 8) == 0;
    static const char* kClockHeaders[] = {"Date", wifiota::kTzHeader};
    if (secure) http.collectHeaders(kClockHeaders, 2);   // two short Strings
    const int code = bodyLen_ ? http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body_)), bodyLen_) : http.GET();
    openMs_ = millis() - t0;
    heapAfterOpen_ = ESP.getFreeHeap();
    bigAfterOpen_ = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (secure && code > 0) heardClock(http);
    if (cancel_) return finish(fail("cancelled"), http);
    if (code == HTTPC_ERROR_CONNECTION_REFUSED) return finish(failConnect(t0), http);
    if (code != HTTP_CODE_OK)
      return finish(code < 0 ? fail("%s", HTTPClient::errorToString(code).c_str()) : fail("http %d", code), http);
    size_ = http.getSize();
    WiFiClient* s = http.getStreamPtr();
    if (!s) return finish(fail("no stream"), http);
    if (text_) {
      if (size_ > int(kTextMax)) return finish(fail("%d bytes is too long", int(size_)), http);
      state_ = State::Body;
      const size_t want = size_ >= 0 ? size_t(size_) : kTextMax;
      uint32_t heard = millis();
      size_t len = 0;
      while (len < want && !cancel_) {
        const int n = s->available() > 0 ? s->read(reinterpret_cast<uint8_t*>(m_->text) + len, want - len) : 0;
        NetLow::sample(netLow.io);
        if (n > 0) len += size_t(n), heard = millis();
        else if (!s->connected()) break;
        else if (millis() - heard > kStallMs) return finish(fail("the text stalled"), http);
        else vTaskDelay(pdMS_TO_TICKS(10));
      }
      m_->text[len] = 0;
      textLen_ = len;
      if (cancel_) return finish(fail("cancelled"), http);
      if (size_ >= 0 && len != size_t(size_)) return finish(fail("the text was cut at %u bytes", unsigned(len)), http);
      return finish(State::Done, http);
    }
    if (size_ <= 0) return finish(fail("no length"), http);
    state_ = State::Body;
    uint8_t* buf = m_->chunk;
    uint32_t heard = millis();
    while (got_ < size_t(size_) && !cancel_) {
      const int avail = s->available();
      if (avail > 0) {
        const int n = s->read(buf, std::min(sizeof m_->chunk, size_t(avail)));
        NetLow::sample(netLow.io);
        if (n <= 0) continue;
        put(buf, size_t(n));
        got_ = got_ + size_t(n);
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
    if (got_ != size_t(size_)) return finish(fail("the connection closed at %u of %d bytes", unsigned(got_), int(size_)), http);
    finish(State::Done, http);
  }

  void heardClock(HTTPClient& http) {
    const std::optional<uint32_t> t = wifiota::parseHttpDate(http.header("Date").c_str());
    if (!t) return;
    const std::optional<int16_t> tz = wifiota::parseTzMinutes(http.header(wifiota::kTzHeader).c_str());
    clockUnix_ = *t;
    hasTz_ = tz.has_value();
    tz_ = tz.value_or(0);
    clockAt_ = std::max<uint32_t>(millis(), 1);   // 0 means none
  }

  void finish(State s) { state_ = s; }
  void finish(State s, HTTPClient& http) {
    http.end();
    state_ = s;
  }

  State fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(m_->error, sizeof m_->error, fmt, ap);
    va_end(ap);
    return State::Failed;
  }

  Mem* m_ = nullptr;
  const char* body_ = nullptr;   // m_->post, or the caller's (borrowLen)
  size_t bodyLen_ = 0;
  bool text_ = true;
  // Each written by one side and read by the other: plain words, never a
  // compare-and-swap.
  volatile bool running_ = false, cancel_ = false;
  TaskHandle_t task_ = nullptr;   // the GET's task, from start until reap
  volatile State state_ = State::Idle;
  volatile uint32_t head_ = 0, tail_ = 0;
  volatile size_t textLen_ = 0, got_ = 0;
  volatile int size_ = -1;
  volatile uint32_t openMs_ = 0, stackFree_ = 0, startedAt_ = 0;
  uint32_t stackBytes_ = 0;
  volatile uint32_t heapAfterOpen_ = 0, bigAfterOpen_ = 0;
  // Written by the task before it ends, read by the loop only after (takeClock).
  volatile uint32_t clockUnix_ = 0, clockAt_ = 0;
  volatile int16_t tz_ = 0;
  volatile bool hasTz_ = false;
};

}  // namespace hw
