// The update's hardware half (docs/ota.md): the inactive app slot over
// esp_ota_*, image packets handed from the Bluetooth task to the loop, and the
// trial a new image runs before it keeps itself. The session itself is
// lib/ota, which the native suite drives.
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>

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
      if (!session_.handle(*line, millis(), ble_.generation())) return line;
    return std::nullopt;
  }
  void writeLine(std::string_view s) override { ble_.writeLine(s); }

 private:
  NusLink& ble_;
  ota::Session& session_;
};

// The trial (docs/ota.md "The trial"). The old image hands over which pet it
// saved, in the default nvs partition, never `pet`; the new one keeps itself
// only once it has the same pet back, a page has reached it over Bluetooth,
// and it has run 30 s.
class Trial {
 public:
  static bool handOver(const ota::PetMark& m) {
    Preferences p;
    if (!p.begin(kNamespace, false)) return false;
    const bool ok = p.putUShort("gen", m.generation) && p.putUInt("genome", m.genome);
    p.end();
    return ok;
  }

  // Before the Dish is built: an image on trial holds its saves.
  static bool pending() {
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
  }

  void begin(bool panelUp, bool resumed, const ota::PetMark& now) {
    const bool pending = Trial::pending();
    facts_ = ota::TrialFacts{pending, panelUp, resumed, handedOver(), now, 0, false};
    if (!pending) {
      if (facts_.handedOver) forget();   // a session that ended in a rollback
      return;
    }
    Serial.printf("[hw] ota trial: on trial, panel=%d resumed=%d handover=%s\n", panelUp, resumed,
                  facts_.handedOver ? (*facts_.handedOver == now ? "same pet" : "ANOTHER PET") : "none");
    // A hang never reaches a verdict, so a timer restarts a loop that stops
    // beating; a restart while still pending is the bootloader's rollback.
    const esp_timer_create_args_t args{&Trial::watch, this, ESP_TIMER_TASK, "ota-trial", false};
    if (esp_timer_create(&args, &watchdog_) == ESP_OK) esp_timer_start_periodic(watchdog_, 1000 * 1000);
  }

  void confirm() {
    if (facts_.pending && !facts_.confirmed) Serial.println("[hw] ota trial: a page reached it over Bluetooth");
    facts_.confirmed = true;
  }

  // True once, when the image keeps itself: the Dish may save again.
  bool poll(uint32_t ms) {
    if (!facts_.pending) return false;
    beat_ = ms;
    facts_.ranMs = ms;
    switch (ota::judge(facts_)) {
      case ota::Verdict::Keep:
        esp_ota_mark_app_valid_cancel_rollback();
        forget();
        esp_timer_stop(watchdog_);
        facts_.pending = false;
        Serial.println("[hw] ota trial: kept");
        return true;
      case ota::Verdict::RollBack:
        Serial.printf("[hw] ota trial: FAILED (%s), rolling back\n", facts_.confirmed ? "checks" : "checks or no page");
        Serial.flush();
        esp_ota_mark_app_invalid_rollback_and_reboot();
        return false;
      default:
        return false;
    }
  }

  bool onTrial() const { return facts_.pending; }

 private:
  static constexpr const char* kNamespace = "ota";
  static constexpr uint32_t kHangMs = 20000;

  static std::optional<ota::PetMark> handedOver() {
    Preferences p;
    if (!p.begin(kNamespace, true)) return std::nullopt;
    std::optional<ota::PetMark> m;
    if (p.isKey("genome")) m = ota::PetMark{p.getUShort("gen"), p.getUInt("genome")};
    p.end();
    return m;
  }
  static void forget() {
    Preferences p;
    if (!p.begin(kNamespace, false)) return;
    p.clear();
    p.end();
  }
  static void watch(void* self) {
    if (millis() - static_cast<Trial*>(self)->beat_ > kHangMs) esp_restart();
  }

  ota::TrialFacts facts_{};
  esp_timer_handle_t watchdog_ = nullptr;
  volatile uint32_t beat_ = 0;
};

}  // namespace hw
