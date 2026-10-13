#pragma once
// SKETCH (docs/design/BATTLES.md) of src/hw/radio_lease.h + src/hw/radio_espnow.h. Types
// and the calls each method makes, no Arduino include, so it compiles here.
//
// One 2.4 GHz radio, three wants: BLE (always, the phone's window), ESP-NOW
// (only while a duel is open), and the BLE OTA (DESIGN unit 22, a sibling's).
// BLE and Wi-Fi share the radio by the IDF's software coexistence, which this
// core has on (measured: sdkconfig CONFIG_SW_COEXIST_ENABLE 1 for esp32s3).
// What must never share it is a duel and an OTA: the lease below is the one
// place that decides, and both the duel's Air and the OTA ask it.
#include <atomic>
#include <cstdint>
#include "blorb/air.h"

namespace hw {

enum class RadioUse : uint8_t { Idle, Duel, Ota };

// One compare-and-swap (graft from candidate B): the duel asks from the loop
// task, and the OTA receiver may ask from the BLE host task, so the lease is
// an atomic rather than a convention about which task writes. The OTA owner
// (DESIGN unit 22, a sibling) adopts this header; its BEGIN asks for Ota and
// its end, abort or stall releases it.
class RadioLease {
 public:
  // Idle -> Duel or Idle -> Ota. Refused when held, and Duel is also refused
  // while the running image is still on trial (ESP_OTA_IMG_PENDING_VERIFY):
  // a trial image writes nothing new and risks nothing it does not have to.
  bool acquire(RadioUse);              // compare_exchange Idle -> use
  void release(RadioUse);              // compare_exchange use -> Idle: only the holder releases
  RadioUse holder() const { return holder_.load(); }
  void setTrialImage(bool onTrial) { trial_.store(onTrial); }
 private:
  std::atomic<RadioUse> holder_{RadioUse::Idle};
  std::atomic<bool> trial_{false};
};

// The OTA side of the interlock (for unit 22's owner): its BEGIN handler
// replies `OTA err busy:duel` when lease.acquire(RadioUse::Ota) is false, and
// the website says "finish your battle first". Its end, abort or stall
// releases the lease.

// ESP-NOW behind blorb::Air. Wi-Fi exists only between open() and close(),
// so a pet that never duels never pays its heap.
class EspNowAir : public blorb::Air {
 public:
  static constexpr int8_t kTxQuarterDbm = 40;   // 10 dBm, in the IDF's 0.25 dBm units
  static constexpr uint8_t kChannel = 1;   // fixed; no AP is ever joined. Same as the sibling badge's mirror (its frames fail our magic).
  explicit EspNowAir(RadioLease& lease) : lease_(lease) {}

  // lease.acquire(Duel); WiFi.mode(WIFI_STA); WiFi.disconnect();
  // esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE);
  // esp_wifi_set_max_tx_power(kTxQuarterDbm) (graft from C: a room, not a street;
  // BLE keeps its own +21 dBm); esp_now_init();
  // add the broadcast peer; esp_now_register_recv_cb(onRecv). On any failure:
  // undo, release, false.
  bool open() override;
  // esp_now_deinit(); WiFi.mode(WIFI_OFF) (esp_wifi_stop + deinit: the heap
  // comes back); lease.release(Duel). Idempotent.
  void close() override;
  bool isOpen() const override { return open_; }
  blorb::PeerAddr self() const override { return self_; }
  void broadcast(const uint8_t*, size_t) override;                    // esp_now_send(ff:ff:ff:ff:ff:ff, ...)
  // Adds the peer on first use (unencrypted; at most a handful), then esp_now_send.
  void send(const blorb::PeerAddr&, const uint8_t*, size_t) override;
  std::optional<blorb::Datagram> receive() override;                  // pops the ring

 private:
  // IDF 4.4's callback: (const uint8_t* mac, const uint8_t* data, int len),
  // on the Wi-Fi task; RSSI comes from a promiscuous-mode rx hook
  // (wifi_promiscuous_pkt_t rx_ctrl.rssi) matched by source MAC, because 4.4's
  // ESP-NOW callback carries none (unit D12 confirms on the board). It copies
  // into a fixed ring of 8 datagrams (2 KB) and drops when full; the
  // state-based protocol makes a drop a delay.
  static void onRecv(const uint8_t* mac, const uint8_t* data, int len);
  RadioLease& lease_;
  blorb::PeerAddr self_{};
  bool open_ = false;
};

}  // namespace hw
