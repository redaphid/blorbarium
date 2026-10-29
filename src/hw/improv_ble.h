// Improv Wi-Fi's GATT service (wifiota/improv.h, docs/wifi-ota.md
// "Provisioning"), beside the UART service on the same NimBLE server and the
// same one phone. Writes arrive on the Bluetooth task and are run on the loop,
// tagged with NusLink's connection number so a new phone never finishes the
// last one's half packet. Reading or subscribing counts as speaking, so a
// page that only provisions is not dropped as a squatter while its keeper
// types the password; only a read of the state or a write asks for a hand's
// yes (wifiota::improv::Opener: a subscription alone never does).
//
// Advertising: the flags, the UART service UUID and the badge's durable id
// (blorb/ble_id.h, for the NFC links) fill the advertisement's 31 bytes, so
// Improv's service data (state, capabilities and four zeros under 16-bit UUID
// 0x4677) is left out; it goes in only if a smaller advertisement ever leaves
// room. Its 128-bit UUID goes in the scan response after the name, only if
// there is room (a name of up to 11 characters; every species and blorb-xxxx
// fit), so it can never push the name out; the scan response is built whole
// here (NimBLE's getScanData() hands back the advertisement instead). Improv clients that filter on that
// UUID (improv-wifi.com) still find the badge; Home Assistant's matcher wants
// the service data too and does not. The website opens Improv after it
// connects by the UART service, so it needs neither. The service data's
// state would be the boot's, Authorization Required: it is not rewritten
// while the host task may be advertising.
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

  // Once, after a phone read the state or wrote: it wants to provision, so
  // the badge asks for a hand's yes (wifiota/consent.h).
  bool takeOpened() { return opened_.exchange(false); }
  // A provision joined: the phone that sent it is not asked again (Opener::provisioned).
  void provisioned() { opener_.provisioned(nus_.generation()); }

  // Every loop: the touches and the RPC bytes since the last pass.
  void poll() {
    if (const uint8_t t = touches_.exchange(0)) {
      nus_.spoke();
      for (uint8_t k = 0; k < 4; ++k)
        if (t >> k & 1 && opener_.touched(wifiota::improv::Touch(k), nus_.generation())) opened_ = true;
    }
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
    const uint32_t heap0 = ESP.getFreeHeap();
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
    // Only if it fits: the badge's id fills the advertisement (blorb/ble_id.h),
    // so today it never does, and nothing is logged for a refusal.
    const bool inAdv = adv->getAdvertisementData().getPayload().size() + 2 + 2 + sizeof data <= 31 &&
                       adv->setServiceData(NimBLEUUID(im::kServiceDataUuid), data, sizeof data);
    // The scan response, built here whole: the name, then the UUID if it fits
    // beside it (2 + name + 2 + 16 <= 31: a name of up to 11 characters).
    // Not from adv->getScanData(): NimBLE-Arduino's returns the
    // ADVERTISEMENT (m_advData), already full with the badge's id, so the UUID
    // never fit and every badge to 0.7.33 left it out, and improv-wifi.com
    // could not find them (owner, 2026-10-29).
    const char* name = nus_.name();
    NimBLEAdvertisementData scan;
    scan.setName(name);
    const bool fits = scan.getPayload().size() + 2 + 16 <= 31;
    if (fits) scan.addServiceUUID(NimBLEUUID(im::kService));
    const bool inScan = adv->setScanResponseData(scan) && fits;
    Serial.printf("[wifi] improv service up: service data %s; scan response %u B: name %u B, uuid %s; heap %u -> %u (before the server starts)\n",
                  inAdv ? "advertised" : "left out (the badge's id fills the advertisement)", unsigned(scan.getPayload().size()),
                  unsigned(std::strlen(name)), inScan ? "yes" : "left out (a name over 11 characters)", unsigned(heap0),
                  unsigned(ESP.getFreeHeap()));
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
      self->touch(wifiota::improv::Touch::Write);
      const NimBLEAttValue v = c->getValue();
      const uint32_t gen = self->nus_.generation();
      portENTER_CRITICAL(&self->mux_);
      if (gen != self->rxGen_) self->rxLen_ = 0, self->rxGen_ = gen;
      const size_t n = std::min(size_t(v.size()), kRxMax - self->rxLen_);   // past a whole packet is junk: the machine says so
      std::memcpy(self->rx_ + self->rxLen_, v.data(), n);
      self->rxLen_ += n;
      portEXIT_CRITICAL(&self->mux_);
    }
    void onRead(NimBLECharacteristic* c, NimBLEConnInfo&) override {
      self->touch(c == self->state_ ? wifiota::improv::Touch::ReadState : wifiota::improv::Touch::ReadOther);
    }
    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t) override { self->touch(wifiota::improv::Touch::Subscribe); }
  };
  void touch(wifiota::improv::Touch t) { touches_ |= uint8_t(1u << uint8_t(t)); }   // the Bluetooth task

  NusLink& nus_;
  wifiota::improv::Machine machine_;
  Callbacks callbacks_{this};
  NimBLECharacteristic *state_ = nullptr, *error_ = nullptr, *result_ = nullptr;
  wifiota::improv::Opener opener_;   // the loop's only
  std::atomic<uint8_t> touches_{0};   // a bit per Touch
  std::atomic<bool> opened_{false};
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  uint8_t rx_[kRxMax] = {};
  size_t rxLen_ = 0;
  uint32_t rxGen_ = 0, seenGen_ = 0;
};

}  // namespace hw
