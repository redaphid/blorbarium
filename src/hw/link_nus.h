// The phone's link: Nordic UART Service over NimBLE-Arduino 2.x, from
// claude-notification-screen's ble.h without its owner token. Newline-framed
// protocol lines both ways; a reply is split at the negotiated MTU.
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstdio>
#include <optional>
#include <string_view>

#include "blorb/seams.h"

namespace hw {

class NusLink : public blorb::Link {
 public:
  static constexpr const char* kService = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
  static constexpr const char* kRx = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";   // the phone writes
  static constexpr const char* kTx = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";   // the badge notifies

  // Name "blorb-xxxx" from the MAC's last two bytes.
  const char* begin(const uint8_t mac[6]) {
    std::snprintf(name_, sizeof name_, "blorb-%02x%02x", mac[4], mac[5]);
    NimBLEDevice::init(name_);
    NimBLEDevice::setMTU(247);
    NimBLEDevice::setPower(21);
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(&serverCb_);
    server->advertiseOnDisconnect(true);
    NimBLEService* nus = server->createService(kService);
    nus->createCharacteristic(kRx, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)->setCallbacks(&rxCb_);
    tx_ = nus->createCharacteristic(kTx, NIMBLE_PROPERTY::NOTIFY);
    nus->start();
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(kService);
    adv->enableScanResponse(true);
    adv->setName(name_);
    adv->setMinInterval(160);
    adv->setMaxInterval(240);
    adv->start();
    return name_;
  }

  bool connected() override { return up_; }

  std::optional<std::string_view> readLine() override {
    poll();
    while (tail_ != head_) {
      const char c = rx_[tail_];
      tail_ = (tail_ + 1) % kRxMax;
      if (c == '\r') continue;
      if (c != '\n') {
        if (len_ < sizeof line_) line_[len_++] = c;
        continue;
      }
      const size_t n = len_;
      len_ = 0;
      return std::string_view(line_, n);
    }
    return std::nullopt;
  }

  void writeLine(std::string_view s) override {
    notify(s.data(), s.size());
    notify("\n", 1);
  }

 private:
  static constexpr size_t kRxMax = 2048;
  // 30 to 50 ms interval and a 6 s supervision timeout (docs/bluetooth-link.md in the sibling).
  static constexpr uint16_t kConnMin = 24, kConnMax = 40, kTimeout10ms = 600;
  static constexpr uint32_t kParamsAfterMs = 2000;

  void notify(const char* s, size_t n) {
    if (!up_ || !tx_) return;
    const size_t chunk = mtu_ > 3 ? mtu_ - 3 : 20;
    while (n) {
      const size_t len = n < chunk ? n : chunk;
      tx_->notify(reinterpret_cast<const uint8_t*>(s), len);
      s += len;
      n -= len;
    }
  }

  // Advertising restarts if it ever stops, and the interval is asked for once per connection.
  void poll() {
    const uint32_t now = millis();
    if (now - lastPoll_ < 250) return;
    lastPoll_ = now;
    NimBLEServer* server = NimBLEDevice::getServer();
    if (!server) return;
    if (!server->getConnectedCount()) {
      up_ = false;
      if (!NimBLEDevice::getAdvertising()->isAdvertising()) NimBLEDevice::getAdvertising()->start();
      return;
    }
    if (up_ && !paramsAsked_ && now - connAt_ > kParamsAfterMs) {
      paramsAsked_ = true;
      if (itvl_ < kConnMin || itvl_ > kConnMax || timeout_ < kTimeout10ms)
        server->updateConnParams(conn_, kConnMin, kConnMax, 0, kTimeout10ms);
    }
  }

  struct RxCb : NimBLECharacteristicCallbacks {
    NusLink* link;
    explicit RxCb(NusLink* l) : link(l) {}
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
      const NimBLEAttValue v = c->getValue();
      for (size_t i = 0; i < v.size(); i++) {
        const size_t next = (link->head_ + 1) % kRxMax;
        if (next == link->tail_) return;   // full: the rest is dropped, so the line reads as garbled
        link->rx_[link->head_] = char(v.data()[i]);
        link->head_ = next;
      }
    }
  };
  struct ServerCb : NimBLEServerCallbacks {
    NusLink* link;
    explicit ServerCb(NusLink* l) : link(l) {}
    void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
      link->conn_ = info.getConnHandle();
      link->connAt_ = millis();
      link->mtu_ = info.getMTU();
      link->itvl_ = info.getConnInterval();
      link->timeout_ = info.getConnTimeout();
      link->paramsAsked_ = false;
      link->up_ = true;
    }
    void onConnParamsUpdate(NimBLEConnInfo& info) override {
      link->itvl_ = info.getConnInterval();
      link->timeout_ = info.getConnTimeout();
    }
    void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { link->mtu_ = mtu; }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
      link->up_ = false;
      link->mtu_ = 23;
    }
  };

  char name_[16] = "";
  NimBLECharacteristic* tx_ = nullptr;
  RxCb rxCb_{this};
  ServerCb serverCb_{this};
  char rx_[kRxMax];
  volatile size_t head_ = 0, tail_ = 0;   // head_ written by the BLE task, tail_ by the loop
  char line_[256];                        // past the protocol's 200, so an overlong line still reads as overlong
  size_t len_ = 0;
  volatile bool up_ = false, paramsAsked_ = false;
  volatile uint16_t mtu_ = 23, conn_ = 0, itvl_ = 0, timeout_ = 0;
  volatile uint32_t connAt_ = 0;
  uint32_t lastPoll_ = 0;
};

// The Dish takes one Link. This one reads the cable first, then the air, and
// answers on whichever the last line came from: the protocol replies within
// handle(), straight after the read, so a reply always goes back to its asker.
class TwoLinks : public blorb::Link {
 public:
  TwoLinks(blorb::Link& serial, blorb::Link& air) : serial_(serial), air_(air) {}
  bool connected() override { return serial_.connected() || air_.connected(); }
  std::optional<std::string_view> readLine() override {
    for (blorb::Link* l : {&serial_, &air_})
      if (std::optional<std::string_view> line = l->readLine()) {
        last_ = l;
        return line;
      }
    return std::nullopt;
  }
  void writeLine(std::string_view s) override { last_->writeLine(s); }

 private:
  blorb::Link& serial_;
  blorb::Link& air_;
  blorb::Link* last_ = &serial_;
};

}  // namespace hw
