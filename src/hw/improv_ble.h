// Improv Wi-Fi's GATT service (wifiota/improv.h, docs/wifi-ota.md
// "Provisioning"), beside the UART service on the same NimBLE server and the
// same one phone. Writes arrive on the Bluetooth task and are run on the loop,
// tagged with NusLink's connection number so a new phone never finishes the
// last one's half packet. Reading or subscribing counts as speaking, so a
// page that only provisions is not dropped as a squatter while its keeper
// types the password.
//
// Advertising: the UART service UUID and the flags take 21 of the
// advertisement's 31 bytes; Improv's service data (state, capabilities and
// four zeros under 16-bit UUID 0x4677) takes the last 10. Its 128-bit UUID
// goes in the scan response after the name, only if there is room (a name of
// up to 11 characters; every species and blorb-xxxx fit), so it can never
// push the name out. The advertised state is the boot's, Authorized: it is
// not rewritten while the host task may be advertising.
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <atomic>
#include <cstring>
#include <functional>

#include "hw/link_nus.h"
#include "wifiota/improv.h"

namespace hw {

class ImprovBle : public wifiota::improv::Port {
 public:
  ImprovBle(NusLink& nus, wifiota::improv::DeviceInfo info) : nus_(nus), machine_(*this, info) {
    nus.serve([this](NimBLEServer* server, NimBLEAdvertising* adv) { build(server, adv); });
  }

  // What the badge does with the network a phone sent (src/wifi_ota.h).
  std::function<void(const wifiota::improv::Credentials&)> onJoin;

  wifiota::improv::Machine& machine() { return machine_; }

  // Every loop: the RPC bytes written since the last pass.
  void poll() {
    if (heard_.exchange(false)) nus_.spoke();
    uint8_t bytes[kRxMax];
    size_t n = 0;
    uint32_t gen = 0;
    portENTER_CRITICAL(&mux_);
    n = rxLen_;
    gen = rxGen_;
    std::memcpy(bytes, rx_, n);
    rxLen_ = 0;
    portEXIT_CRITICAL(&mux_);
    if (gen != seenGen_) seenGen_ = gen, machine_.reset();
    if (n) machine_.write(bytes, n);
  }

  // Port: the characteristics.
  void showState(wifiota::improv::State s) override { set(state_, uint8_t(s)); }
  void showError(wifiota::improv::Error e) override { set(error_, uint8_t(e)); }
  void sendResult(const uint8_t* p, size_t n) override {
    if (!result_) return;
    result_->setValue(p, n);
    result_->notify();
  }
  void join(const wifiota::improv::Credentials& c) override {
    if (onJoin) onJoin(c);
  }

 private:
  static constexpr size_t kRxMax = wifiota::improv::kMaxPacket;

  void build(NimBLEServer* server, NimBLEAdvertising* adv) {
    namespace im = wifiota::improv;
    NimBLEService* s = server->createService(im::kService);
    state_ = s->createCharacteristic(im::kCurrentState, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    error_ = s->createCharacteristic(im::kErrorState, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic* rpc = s->createCharacteristic(im::kRpcCommand, NIMBLE_PROPERTY::WRITE);
    result_ = s->createCharacteristic(im::kRpcResult, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic* caps = s->createCharacteristic(im::kCapabilities, NIMBLE_PROPERTY::READ);
    for (NimBLECharacteristic* c : {state_, error_, rpc, result_, caps}) c->setCallbacks(&callbacks_);
    const uint8_t state = uint8_t(machine_.state()), none = 0, can = im::kCapabilityBits;
    state_->setValue(&state, 1);
    error_->setValue(&none, 1);
    caps->setValue(&can, 1);
    const uint8_t data[6] = {state, can, 0, 0, 0, 0};
    const bool inAdv = adv->setServiceData(NimBLEUUID(im::kServiceDataUuid), data, sizeof data);
    const bool inScan = adv->addServiceUUID(im::kService);
    Serial.printf("[wifi] improv service up: service data %s, uuid in scan response %s\n", inAdv ? "advertised" : "NO ROOM",
                  inScan ? "yes" : "no room");
  }

  void set(NimBLECharacteristic* c, uint8_t v) {
    if (!c) return;
    c->setValue(&v, 1);
    c->notify();
  }

  struct Callbacks : NimBLECharacteristicCallbacks {
    ImprovBle* self;
    explicit Callbacks(ImprovBle* s) : self(s) {}
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
      self->heard_ = true;
      const NimBLEAttValue v = c->getValue();
      const uint32_t gen = self->nus_.generation();
      portENTER_CRITICAL(&self->mux_);
      if (gen != self->rxGen_) self->rxLen_ = 0, self->rxGen_ = gen;
      const size_t n = std::min(size_t(v.size()), kRxMax - self->rxLen_);   // past a whole packet is junk: the machine says so
      std::memcpy(self->rx_ + self->rxLen_, v.data(), n);
      self->rxLen_ += n;
      portEXIT_CRITICAL(&self->mux_);
    }
    void onRead(NimBLECharacteristic*, NimBLEConnInfo&) override { self->heard_ = true; }
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t) override { self->heard_ = true; }
  };

  NusLink& nus_;
  wifiota::improv::Machine machine_;
  Callbacks callbacks_{this};
  NimBLECharacteristic *state_ = nullptr, *error_ = nullptr, *result_ = nullptr;
  std::atomic<bool> heard_{false};
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  uint8_t rx_[kRxMax] = {};
  size_t rxLen_ = 0;
  uint32_t rxGen_ = 0, seenGen_ = 0;
};

}  // namespace hw
