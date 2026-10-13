// The update's hardware half (docs/ota.md): the inactive app slot over
// esp_ota_*, image packets handed from the Bluetooth task to the loop, and the
// trial a new image runs before it keeps itself. The session itself is
// lib/ota, which the native suite drives.
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <nvs.h>

#include <atomic>
#include <cstring>
#include <optional>

#include "hw/link_nus.h"
#include "ota/session.h"
#include "ota/trial.h"

// The core marks every new image valid at boot unless this says otherwise
// (esp32-hal-misc.c). It is declared in C there, so it must be defined with C
// linkage here: a C++ definition is a different symbol and the core's weak
// `return false` wins silently. main.cpp is this header's one includer.
extern "C" bool verifyRollbackLater() { return true; }

namespace hw {

class EspSlot : public ota::Slot {
 public:
  uint32_t capacity() override {
    const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
    return p ? p->size : 0;
  }
  bool open(uint32_t) override {
    part_ = esp_ota_get_next_update_partition(nullptr);
    // Sequential writes erase each sector as the image reaches it; erasing the
    // whole 4 MB slot up front stalls the loop, and the pet, for seconds.
    open_ = part_ && esp_ota_begin(part_, OTA_WITH_SEQUENTIAL_WRITES, &handle_) == ESP_OK;
    return open_;
  }
  bool write(const uint8_t* p, size_t n) override { return open_ && esp_ota_write(handle_, p, n) == ESP_OK; }
  bool close() override {
    if (!open_) return false;
    open_ = false;
    return esp_ota_end(handle_) == ESP_OK;   // checks the image's segments and its own appended hash
  }
  bool activate() override { return part_ && esp_ota_set_boot_partition(part_) == ESP_OK; }
  void discard() override {
    if (open_) esp_ota_abort(handle_);
    open_ = false;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (esp_ota_get_boot_partition() != running) esp_ota_set_boot_partition(running);
  }

 private:
  const esp_partition_t* part_ = nullptr;
  esp_ota_handle_t handle_ = 0;
  bool open_ = false;
};

// Data writes arrive on the Bluetooth task and are written to flash on the
// loop. A full ring drops the write; the session then sees a gap and the page
// goes back, so nothing here has to be reliable. In PSRAM, so it costs the
// internal heap nothing.
class PacketRing {
 public:
  static constexpr size_t kSlots = 32;   // past the page's 6 KB window at 240 bytes a write
  static constexpr size_t kMax = 244;    // MTU 247 less the ATT header

  void begin() {
    slots_ = static_cast<Packet*>(heap_caps_malloc(sizeof(Packet) * kSlots, MALLOC_CAP_SPIRAM));
    if (!slots_) slots_ = static_cast<Packet*>(malloc(sizeof(Packet) * kSlots));
  }
  void push(const uint8_t* p, size_t n, ota::Peer peer) {
    const size_t head = head_.load(std::memory_order_relaxed), next = (head + 1) % kSlots;
    if (!slots_ || n > kMax || next == tail_.load(std::memory_order_acquire)) return;
    slots_[head].len = uint16_t(n);
    slots_[head].peer = peer;
    std::memcpy(slots_[head].bytes, p, n);
    head_.store(next, std::memory_order_release);
  }
  template <class F>
  void drain(F&& f) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    while (tail != head_.load(std::memory_order_acquire)) {
      f(slots_[tail].bytes, slots_[tail].len, slots_[tail].peer);
      tail = (tail + 1) % kSlots;
      tail_.store(tail, std::memory_order_release);
    }
  }

 private:
  struct Packet { uint16_t len; ota::Peer peer; uint8_t bytes[kMax]; };
  Packet* slots_ = nullptr;
  std::atomic<size_t> head_{0}, tail_{0};
};

struct OtaData : NimBLECharacteristicCallbacks {
  PacketRing& ring;
  NusLink& ble;
  OtaData(PacketRing& r, NusLink& b) : ring(r), ble(b) {}
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue v = c->getValue();
    ring.push(v.data(), v.size(), ble.generation());
  }
};

// The Bluetooth link with OTA lines taken out: the session answers those, the
// Dish's protocol everything else. Only the air has an update, so on the
// cable `OTA` stays an unknown verb.
class OtaLink : public blorb::Link {
 public:
  OtaLink(NusLink& ble, ota::Session& session) : ble_(ble), session_(session) {}
  bool connected() override { return ble_.connected(); }
  std::optional<std::string_view> readLine() override {
    while (std::optional<std::string_view> line = ble_.readLine())
      if (!session_.handle(*line, millis(), ble_.lineConnection())) return line;
    return std::nullopt;
  }
  void writeLine(std::string_view s) override { ble_.writeLine(s); }

 private:
  NusLink& ble_;
  ota::Session& session_;
};

// The trial's hold on the hardware (docs/ota.md "The trial"): esp_ota_* for
// the image's state, and the default nvs partition, never `pet`, for what the
// old image hands over. nvs_* directly, not Preferences: the core's
// Preferences logs an error past 0x0c through a NULL table entry.
class EspTrialPort : public ota::TrialPort {
 public:
  bool pending() override {
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
  }

  // Written by the old image, before it switches and again before it restarts.
  static bool write(const ota::Handover& h) {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READWRITE, &n) != ESP_OK) return false;
    const uint8_t to[3 * 2] = {uint8_t(h.to.major), uint8_t(h.to.major >> 8), uint8_t(h.to.minor),
                               uint8_t(h.to.minor >> 8), uint8_t(h.to.patch), uint8_t(h.to.patch >> 8)};
    const bool ok = nvs_set_u16(n, "gen", h.pet.generation) == ESP_OK && nvs_set_u32(n, "genome", h.pet.genome) == ESP_OK &&
                    nvs_set_blob(n, "to", to, sizeof to) == ESP_OK &&
                    nvs_set_blob(n, "tok", h.token.b, sizeof h.token.b) == ESP_OK && nvs_commit(n) == ESP_OK;
    nvs_close(n);
    return ok;
  }

  std::optional<ota::Handover> handover() override {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READONLY, &n) != ESP_OK) return std::nullopt;
    ota::Handover h{};
    uint8_t to[6];
    size_t toLen = sizeof to, tokLen = sizeof h.token.b;
    const bool ok = nvs_get_u16(n, "gen", &h.pet.generation) == ESP_OK && nvs_get_u32(n, "genome", &h.pet.genome) == ESP_OK &&
                    nvs_get_blob(n, "to", to, &toLen) == ESP_OK && toLen == sizeof to &&
                    nvs_get_blob(n, "tok", h.token.b, &tokLen) == ESP_OK && tokLen == sizeof h.token.b;
    nvs_close(n);
    if (!ok) return std::nullopt;
    h.to = ota::Version{uint16_t(to[0] | to[1] << 8), uint16_t(to[2] | to[3] << 8), uint16_t(to[4] | to[5] << 8)};
    return h;
  }

  void forgetHandover() override {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READWRITE, &n) != ESP_OK) return;
    for (const char* k : {"gen", "genome", "to", "tok"}) nvs_erase_key(n, k);
    nvs_commit(n);
    nvs_close(n);
  }

  std::optional<ota::Version> rolledBack() {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READONLY, &n) != ESP_OK) return std::nullopt;
    uint8_t v[6];
    size_t len = sizeof v;
    const bool ok = nvs_get_blob(n, "rb", v, &len) == ESP_OK && len == sizeof v;
    nvs_close(n);
    if (!ok) return std::nullopt;
    return ota::Version{uint16_t(v[0] | v[1] << 8), uint16_t(v[2] | v[3] << 8), uint16_t(v[4] | v[5] << 8)};
  }

  void noteRolledBack(std::optional<ota::Version> v) override {
    nvs_handle_t n;
    if (nvs_open(kNamespace, NVS_READWRITE, &n) != ESP_OK) return;
    if (v) {
      const uint8_t b[6] = {uint8_t(v->major), uint8_t(v->major >> 8), uint8_t(v->minor),
                            uint8_t(v->minor >> 8), uint8_t(v->patch), uint8_t(v->patch >> 8)};
      nvs_set_blob(n, "rb", b, sizeof b);
    } else {
      nvs_erase_key(n, "rb");
    }
    nvs_commit(n);
    nvs_close(n);
  }

  void keep() override { esp_ota_mark_app_valid_cancel_rollback(); }
  void rollBack() override {
    Serial.flush();
    const esp_err_t e = esp_ota_mark_app_invalid_rollback_and_reboot();   // returns only when it cannot
    Serial.printf("[hw] ota trial: esp_ota_mark_app_invalid_rollback_and_reboot: %s\n", esp_err_to_name(e));
  }
  void say(const char* line) override { Serial.println(line); }

 private:
  static constexpr const char* kNamespace = "ota";
};

// A hang never reaches a verdict, so while the image is on trial a timer
// restarts a loop that stops beating; a restart while still pending is the
// bootloader's rollback.
class TrialWatchdog {
 public:
  void start() {
    const esp_timer_create_args_t args{&TrialWatchdog::watch, this, ESP_TIMER_TASK, "ota-trial", false};
    if (esp_timer_create(&args, &timer_) == ESP_OK) esp_timer_start_periodic(timer_, 1000 * 1000);
  }
  void beat(uint32_t ms) { beat_ = ms; }
  void stop() {
    if (timer_) esp_timer_stop(timer_);
  }

 private:
  static constexpr uint32_t kHangMs = 20000;
  static void watch(void* self) {
    if (millis() - static_cast<TrialWatchdog*>(self)->beat_ > kHangMs) esp_restart();
  }
  esp_timer_handle_t timer_ = nullptr;
  volatile uint32_t beat_ = 0;
};

}  // namespace hw
