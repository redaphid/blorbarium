// The phone's link: Nordic UART Service over NimBLE-Arduino 2.x, from
// claude-notification-screen's ble.h without its owner token. Newline-framed
// protocol lines both ways; a reply is split at the negotiated MTU.
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "blorb/notify_queue.h"
#include "blorb/rx_lines.h"
#include "blorb/seams.h"
#include "blorb/squatters.h"

// One phone at a time, enforced in onConnect, never by shrinking NimBLE: its
// host indexes per-connection arrays by the controller's raw handle with no
// bounds check (ble_gap.c slave_conn[MAX + 1]), and on the S3 the setting also
// sizes the controller's activities. At 1, a handle of 2 wrote past them, the
// host lost track of which connections it had announced, and a dropped link
// left a stale peer that stopped advertising until a power cycle.
static_assert(CONFIG_BT_NIMBLE_MAX_CONNECTIONS >= 3, "NimBLE's per-handle arrays need the default 3");

namespace hw {

class NusLink : public blorb::Link {
 public:
  static constexpr const char* kService = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
  static constexpr const char* kRx = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";   // the phone writes
  static constexpr const char* kTx = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";   // the badge notifies
  static constexpr const char* kOtaData = "6E400004-B5A3-F393-E0A9-E50E24DCCA9E";   // update images (docs/ota.md)

  // Name "blorb-xxxx" from the MAC's last two bytes. `otaData` takes the
  // update's image writes, when the firmware has updates.
  const char* begin(const uint8_t mac[6], NimBLECharacteristicCallbacks* otaData = nullptr) {
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
    if (otaData) nus->createCharacteristic(kOtaData, NIMBLE_PROPERTY::WRITE_NR)->setCallbacks(otaData);
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
  // While this is true (an update in progress) a quiet connection is never dropped.
  void keepWhile(std::function<bool()> busy) { keep_ = std::move(busy); }
  uint16_t mtu() const { return mtu_; }
  uint32_t generation() const { return gen_; }   // which connection: a reconnect is a new number

  std::optional<std::string_view> readLine() override {
    pump();
    poll();
    const std::optional<blorb::RxLines<kRxMax>::Line> line = rx_.line();
    if (!line) return std::nullopt;
    lineConnection_ = line->connection;
    if (line->connection == seenGen_) {
      if (!firstLogged_) {
        firstLogged_ = true;
        Serial.printf("[hw] ble first line: %.40s\n", std::string(line->text).c_str());
      }
      if (blorb::speaksBlorb(line->text)) squatters_.spoke();
    }
    return line->text;
  }
  // The connection the last line came on, tagged when its bytes arrived.
  uint32_t lineConnection() const { return lineConnection_; }

  // Queued, sent by pump(). A line too big for what is left waits here while
  // the queue drains, so a genome past the queue's size still goes out whole.
  void writeLine(std::string_view s) override {
    const uint32_t since = millis();
    while (!tx_q_.push(s)) {
      if (!pump() && millis() - since > kStallMs) return;   // a stalled phone: give up this line
      delay(2);
    }
  }

  // The reply that just finished going out, once, so the firmware can report it.
  struct Drained {
    size_t bytes;
    uint32_t chunks, retries;
  };
  std::optional<Drained> takeDrained() {
    if (!reported_ && tx_q_.empty() && tx_q_.burst()) {
      reported_ = true;
      return Drained{tx_q_.burst(), tx_q_.chunks(), tx_q_.retries()};
    }
    return std::nullopt;
  }

 private:
  static constexpr size_t kRxMax = 2048;
  // 30 to 50 ms interval and a 6 s supervision timeout (docs/bluetooth-link.md in the sibling).
  static constexpr uint16_t kConnMin = 24, kConnMax = 40, kTimeout10ms = 600;
  static constexpr uint32_t kParamsAfterMs = 2000;
  static constexpr uint32_t kRetryMs = 4, kStallMs = 3000;
  // Holds a whole GENOME of today's size (about 1.5 KB) without blocking the loop.
  static constexpr size_t kTxMax = 2048;

  // notify() is false when NimBLE refuses the chunk (BLE_HS_ENOMEM, EBUSY):
  // it stays queued and is tried again kRetryMs later. onStatus is no help
  // here, since for a notification it fires inside notify() with the same rc.
  // A new connection drops whatever the last one left unsent.
  bool pump() {
    if (gen_ != sentGen_ || !up_) {
      sentGen_ = gen_;
      tx_q_.clear();
      return false;
    }
    if (tx_q_.empty()) return true;
    reported_ = false;
    const uint32_t now = millis();
    if (int32_t(now - retryAt_) < 0) return false;
    const size_t payload = mtu_ > 3 ? mtu_ - 3 : 20;
    const bool done = tx_q_.drain(payload, [this](const uint8_t* p, size_t n) { return tx_->notify(p, n); });
    if (!done) retryAt_ = now + kRetryMs;
    return done;
  }

  // Advertising restarts if it ever stops, and the interval is asked for once per connection.
  void poll() {
    const uint32_t now = millis();
    if (now - lastPoll_ < 250) return;
    lastPoll_ = now;
    NimBLEServer* server = NimBLEDevice::getServer();
    if (!server) return;
    if (gen_ != seenGen_) {   // a new connection: say who, and refuse an address that squatted
      seenGen_ = gen_;
      firstLogged_ = false;
      const blorb::BleAddr a = peer_;
      Serial.printf("[hw] ble conn h=%u addr=%02x:%02x:%02x:%02x:%02x:%02x type=%s\n", unsigned(conn_), a.b[5], a.b[4],
                    a.b[3], a.b[2], a.b[1], a.b[0], a.type & 1 ? "random" : "public");
      const blorb::Squatters::Act act = squatters_.connected(a, now);
      if (act == blorb::Squatters::Act::Refuse) Serial.println("[hw] ble refused: it squatted; this address waits a minute");
      if (act != blorb::Squatters::Act::None) server->disconnect(conn_);
    }
    if (lastDrop_ != loggedDrop_) {
      loggedDrop_ = lastDrop_;
      Serial.printf("[hw] ble drop h=%u reason=0x%x\n", unsigned(loggedDrop_ >> 16), unsigned(loggedDrop_ & 0xffff));
    }
    // Live by the host's own table, not NimBLE-Arduino's peer list: a peer the
    // host dropped without a DISCONNECT event must not keep the dish invisible.
    const bool live = liveConnections(server) > 0;
    if (!live) {
      up_ = false;
      if (!NimBLEDevice::getAdvertising()->isAdvertising()) {
        if (server->getConnectedCount()) Serial.println("[hw] ble: a stale peer, advertising again");
        NimBLEDevice::getAdvertising()->start();
      }
    }
    // A board that goes silent says so.
    const bool advertising = NimBLEDevice::getAdvertising()->isAdvertising();
    if (advertising != advLogged_) Serial.printf("[hw] ble adv %s\n", (advLogged_ = advertising) ? "on" : "off");
    if (!live) return;
    if (squatters_.poll(now, keep_ && keep_()) == blorb::Squatters::Act::Evict) {
      Serial.printf("[hw] ble drop h=%u reason=idle: nothing the protocol knows in %u s\n", unsigned(conn_),
                    unsigned(blorb::Squatters::kSpeakWithinMs / 1000));
      server->disconnect(conn_);
      return;
    }
    if (up_ && !paramsAsked_ && now - connAt_ > kParamsAfterMs) {
      paramsAsked_ = true;
      if (itvl_ < kConnMin || itvl_ > kConnMax || timeout_ < kTimeout10ms)
        server->updateConnParams(conn_, kConnMin, kConnMax, 0, kTimeout10ms);
    }
  }

  static size_t liveConnections(NimBLEServer* server) {
    size_t n = 0;
    ble_gap_conn_desc desc;
    for (uint16_t h : server->getPeerDevices())
      if (ble_gap_conn_find(h, &desc) == 0) ++n;
    return n;
  }

  struct RxCb : NimBLECharacteristicCallbacks {
    NusLink* link;
    explicit RxCb(NusLink* l) : link(l) {}
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
      const NimBLEAttValue v = c->getValue();
      link->rx_.push(v.data(), v.size());
    }
  };
  struct ServerCb : NimBLEServerCallbacks {
    NusLink* link;
    explicit ServerCb(NusLink* l) : link(l) {}
    void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
      if (liveConnections(server) > 1) {   // a second phone: the first keeps the dish
        server->disconnect(info.getConnHandle());
        return;
      }
      link->conn_ = info.getConnHandle();
      const NimBLEAddress addr = info.getAddress();
      std::memcpy(link->peer_.b, addr.getVal(), sizeof link->peer_.b);
      link->peer_.type = addr.getType();
      link->connAt_ = millis();
      link->mtu_ = info.getMTU();
      link->itvl_ = info.getConnInterval();
      link->timeout_ = info.getConnTimeout();
      link->paramsAsked_ = false;
      link->gen_ = link->gen_ + 1;
      link->rx_.connected(link->gen_);
      link->up_ = true;
    }
    void onConnParamsUpdate(NimBLEConnInfo& info) override {
      link->itvl_ = info.getConnInterval();
      link->timeout_ = info.getConnTimeout();
    }
    void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { link->mtu_ = mtu; }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
      link->lastDrop_ = uint32_t(info.getConnHandle()) << 16 | uint16_t(reason);
      link->up_ = false;
      link->mtu_ = 23;
    }
  };

  char name_[16] = "";
  NimBLECharacteristic* tx_ = nullptr;
  RxCb rxCb_{this};
  ServerCb serverCb_{this};
  blorb::RxLines<kRxMax> rx_;   // written by the BLE task, read by the loop
  uint32_t lineConnection_ = 0;
  volatile uint32_t lastDrop_ = 0;   // written by the BLE task, logged by poll()
  uint32_t loggedDrop_ = 0;
  blorb::BleAddr peer_;               // written by the BLE task before gen_ moves
  uint32_t seenGen_ = 0;              // the connection poll() last announced
  bool firstLogged_ = false;
  blorb::Squatters squatters_;        // the loop's only
  std::function<bool()> keep_;
  bool advLogged_ = false;
  volatile bool up_ = false, paramsAsked_ = false;
  volatile uint16_t mtu_ = 23, conn_ = 0, itvl_ = 0, timeout_ = 0;
  volatile uint32_t connAt_ = 0;
  uint32_t lastPoll_ = 0;
  blorb::NotifyQueue<kTxMax> tx_q_;
  volatile uint32_t gen_ = 0;   // bumped by the BLE task per connection
  uint32_t sentGen_ = 0, retryAt_ = 0;
  bool reported_ = true;
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
