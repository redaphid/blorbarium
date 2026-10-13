#pragma once
// SKETCH (docs/design/BATTLES.md, "Radio") of src/hw/radio_espnow.h. Types and
// the calls each method makes, no Arduino include, so it compiles here.
//
// One 2.4 GHz radio, three wants: BLE (always, the phone's window), ESP-NOW
// (only while a duel is open), and the BLE OTA (branch `ota`, docs/ota.md
// "Other users of the radio"). BLE and Wi-Fi share the radio by the IDF's
// software coexistence, which this core has on (measured:
// CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE 1 in esp32s3/qio_qspi/include/sdkconfig.h).
// What must never share it is a duel and an OTA. That interlock is OTA's own,
// and duels bind to it rather than adding a second one:
//
//   a duel may open        only while !ota::Session::active() && !hw::Trial::onTrial()
//   an update may start    only while !DishOtaHost::busy(), and busy() returns true
//                          while EspNowAir::isOpen(); the update then answers BEGIN
//                          `ERR 409 BUSY` and holds its restart until it is false
//
// No lock: the OTA session, the trial and busy() all run on the loop task, and
// so does every EspNowAir call (the Dish ticks on the loop task). The BLE task
// only pushes image packets into OTA's ring.
#include <cstdint>
#include <optional>
#include "blorb/air.h"

namespace hw {

// The two facts a duel needs from OTA. src/main.cpp implements it over the
// real objects: otaActive() is `updater.active()` (ota::Session), onTrial() is
// `trial.onTrial()` (hw::Trial). A trial image never opens a duel, so it never
// writes a duel book entry, a wound or a friend egg that a rollback would meet.
struct RadioGate {
  virtual ~RadioGate() = default;
  virtual bool otaActive() const = 0;
  virtual bool onTrial() const = 0;
};

// ESP-NOW behind blorb::Air. Wi-Fi exists only between open() and close(),
// so a pet that never duels never pays its heap.
class EspNowAir : public blorb::Air {
 public:
  static constexpr int8_t kTxQuarterDbm = 40;   // 10 dBm, in the IDF's 0.25 dBm units (a room, not a street)
  static constexpr uint8_t kChannel = 1;        // fixed; no AP is ever joined
  explicit EspNowAir(const RadioGate& gate) : gate_(gate) {}

  // Refused (false) while gate.otaActive() or gate.onTrial(): the pet shakes
  // his head. Otherwise WiFi.mode(WIFI_STA); WiFi.disconnect();
  // esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
  // esp_wifi_set_max_tx_power(kTxQuarterDbm) (BLE keeps its own +21 dBm);
  // esp_now_init(); add the broadcast peer; esp_now_register_recv_cb(onRecv);
  // the promiscuous rx hook for RSSI. On any failure: undo, false.
  bool open() override;
  // esp_now_deinit(); WiFi.mode(WIFI_OFF) (esp_wifi_stop + deinit: the heap
  // comes back). Idempotent. From here DishOtaHost::busy() is false again.
  void close() override;
  bool isOpen() const override { return open_; }
  blorb::PeerAddr self() const override { return self_; }
  void broadcast(const uint8_t*, size_t) override;                    // esp_now_send(ff:ff:ff:ff:ff:ff, ...)
  void send(const blorb::PeerAddr&, const uint8_t*, size_t) override; // adds the peer on first use (unencrypted)
  std::optional<blorb::Datagram> receive() override;                  // pops the ring

 private:
  // IDF 4.4's callback: (const uint8_t* mac, const uint8_t* data, int len),
  // on the Wi-Fi task; RSSI comes from a promiscuous-mode rx hook
  // (wifi_promiscuous_pkt_t rx_ctrl.rssi) matched by source MAC, because 4.4's
  // ESP-NOW callback carries none (unit D12 confirms on the board). It copies
  // into a fixed ring of 8 datagrams (2 KB) and drops when full; the
  // state-based protocol makes a drop a delay.
  static void onRecv(const uint8_t* mac, const uint8_t* data, int len);
  const RadioGate& gate_;
  blorb::PeerAddr self_{};
  bool open_ = false;
};

// For src/main.cpp (branch `ota`): DishOtaHost::busy() gains `|| espNowAir.isOpen()`.

}  // namespace hw
