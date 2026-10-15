// The debug link's characteristic (docs/debug-ble.md): 6E400005 in the NUS
// service, lines in by write and lines out by notify. It has its own buffers,
// so a debug reply never lands in the middle of a protocol reply on TX. It
// rides NusLink's connection: one phone, and the same connection numbers, so
// an unlock belongs to the connection that asked for it.
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <optional>
#include <string_view>

#include "blorb/notify_queue.h"
#include "blorb/rx_lines.h"
#include "blorb/seams.h"
#include "hw/link_nus.h"

namespace hw {

class DebugChar : public blorb::Link, NimBLECharacteristicCallbacks {
 public:
  static constexpr const char* kUuid = "6E400005-B5A3-F393-E0A9-E50E24DCCA9E";
  static constexpr size_t kRxMax = 512, kTxMax = 2048;

  // Joins the service when `nus` begins, so it is in every build.
  explicit DebugChar(NusLink& nus) : nus_(nus) {
    nus.extend([this](NimBLEService* s) {
      c_ = s->createCharacteristic(kUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
      c_->setCallbacks(this);
    });
  }

  // The next whole line, tagged with the connection it came on.
  std::optional<blorb::RxLines<kRxMax>::Line> take() {
    pump();
    return rx_.line();
  }

  bool connected() override { return nus_.connected(); }
  std::optional<std::string_view> readLine() override {
    const std::optional<blorb::RxLines<kRxMax>::Line> l = take();
    return l ? std::optional<std::string_view>(l->text) : std::nullopt;
  }
  // As NusLink's: a line waits while the queue drains, and a stalled phone loses it.
  void writeLine(std::string_view s) override {
    const uint32_t since = millis();
    while (!q_.push(s)) {
      if (!pump() && millis() - since > kStallMs) return;
      delay(2);
    }
    pump();
  }

  // Sends what fits; true when the queue is empty.
  bool pump() {
    if (nus_.generation() != sentGen_ || !nus_.connected() || !c_) {
      sentGen_ = nus_.generation();
      q_.clear();
      return false;
    }
    if (q_.empty()) return true;
    const uint32_t now = millis();
    if (int32_t(now - retryAt_) < 0) return false;
    const size_t payload = nus_.mtu() > 3 ? nus_.mtu() - 3 : 20;
    const bool done = q_.drain(payload, [this](const uint8_t* p, size_t n) { return c_->notify(p, n); });
    if (!done) retryAt_ = now + kRetryMs;
    return done;
  }

 private:
  static constexpr uint32_t kRetryMs = 4, kStallMs = 3000;

  // The BLE task. NusLink's onConnect has already counted the connection, so
  // its number tags these bytes; a new one drops the last one's unread half line.
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const uint32_t gen = nus_.generation();
    if (gen != tagged_) {
      tagged_ = gen;
      rx_.connected(gen);
    }
    const NimBLEAttValue v = c->getValue();
    rx_.push(v.data(), v.size());
  }

  NusLink& nus_;
  NimBLECharacteristic* c_ = nullptr;
  blorb::RxLines<kRxMax> rx_;
  blorb::NotifyQueue<kTxMax> q_;
  uint32_t tagged_ = 0, sentGen_ = 0, retryAt_ = 0;
};

}  // namespace hw
