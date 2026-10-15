// The duel radio (blorb/air.h) over ESP-NOW, beside NimBLE's phone link.
// Wi-Fi comes up in STA mode on a fixed channel, joined to nothing, only while
// a duel is open; close() takes it down again and gives its heap back.
//
// Callbacks run on the Wi-Fi task and only copy into a FreeRTOS queue; the
// loop task drains it in receive(), so nothing in the radio's context blocks
// or touches the Dish.
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstring>

#include "blorb/air.h"

namespace hw {

class EspNowAir : public blorb::Air {
 public:
  static constexpr uint8_t kChannel = 1;
  // A lobby beacon every 300 ms from up to four friends plus a State burst is
  // a handful of frames between loop passes; past this a frame is dropped and
  // counted, as the air drops them anyway.
  static constexpr UBaseType_t kQueueDepth = 12;
  // 10 dBm (quarter-dBm units): arm's length is the game, not the room.
  static constexpr int8_t kTxQuarterDbm = 40;

  bool open() override {
    if (open_) return true;
    if (!queue_ && !(queue_ = xQueueCreate(kQueueDepth, sizeof(blorb::Datagram)))) return fail("queue");
    xQueueReset(queue_);
    const uint32_t before = ESP.getFreeHeap();
    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_STA)) return fail("wifi");
    // ESP-NOW's receive callback on IDF 4.4 carries no RSSI, and the lobby
    // gates on it, so the frame's own rx_ctrl is read in promiscuous mode
    // (management frames only: ESP-NOW rides vendor action frames).
    wifi_promiscuous_filter_t filter{WIFI_PROMIS_FILTER_MASK_MGMT};
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&onSniff);
    esp_wifi_set_promiscuous(true);
    if (esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) return fail("channel");
    esp_wifi_set_max_tx_power(kTxQuarterDbm);
    if (esp_now_init() != ESP_OK) return fail("esp_now_init");
    self_ = this;
    esp_now_register_recv_cb(&onReceive);
    addPeer(kBroadcast);
    esp_wifi_get_mac(WIFI_IF_STA, me_.mac);
    open_ = true;
    rx_ = tx_ = dropped_ = sendErrors_ = 0;
    Serial.printf("[hw] air open ch=%u heap %u -> %u minheap=%u big=%u\n", kChannel, unsigned(before),
                  unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    return true;
  }

  void close() override {
    if (!open_) return;
    open_ = false;
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    self_ = nullptr;
    Serial.printf("[hw] air close rx=%u tx=%u dropped=%u send_errors=%u heap=%u\n", unsigned(rx_), unsigned(tx_),
                  unsigned(dropped_), unsigned(sendErrors_), unsigned(ESP.getFreeHeap()));
  }

  bool isOpen() const override { return open_; }
  blorb::PeerAddr self() const override { return me_; }

  void broadcast(const uint8_t* p, size_t n) override { transmit(kBroadcast, p, n); }

  void send(const blorb::PeerAddr& to, const uint8_t* p, size_t n) override {
    if (open_ && !esp_now_is_peer_exist(to.mac)) addPeer(to);
    transmit(to, p, n);
  }

  std::optional<blorb::Datagram> receive() override {
    blorb::Datagram d;
    if (!open_ || xQueueReceive(queue_, &d, 0) != pdTRUE) return std::nullopt;
    ++rx_;
    return d;
  }

 private:
  static constexpr blorb::PeerAddr kBroadcast{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

  bool fail(const char* what) {
    Serial.printf("[hw] air open FAILED at %s heap=%u\n", what, unsigned(ESP.getFreeHeap()));
    esp_now_deinit();
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  static void addPeer(const blorb::PeerAddr& a) {
    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, a.mac, 6);
    peer.channel = kChannel;
    peer.ifidx = WIFI_IF_STA;
    esp_now_add_peer(&peer);   // ESP-NOW holds 20; a duel names one friend at a time
  }

  void transmit(const blorb::PeerAddr& to, const uint8_t* p, size_t n) {
    if (!open_ || n > blorb::kDatagramMax) return;
    if (esp_now_send(to.mac, p, n) == ESP_OK) ++tx_;
    else ++sendErrors_;
  }

  // The sniffer sees each frame just before ESP-NOW hands it over, on the
  // same task, so the last sender's RSSI is the one to pair with it.
  static void onSniff(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
    if (pkt->rx_ctrl.sig_len < 16) return;
    std::memcpy(sniffedFrom_, pkt->payload + 10, 6);   // 802.11 addr2: the transmitter
    sniffedRssi_ = int8_t(pkt->rx_ctrl.rssi);
  }

  static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    EspNowAir* self = self_;
    if (!self || len <= 0 || len > int(blorb::kDatagramMax)) return;
    blorb::Datagram d;
    std::memcpy(d.from.mac, mac, 6);
    d.rssi = std::memcmp(sniffedFrom_, mac, 6) == 0 ? sniffedRssi_ : -127;
    d.len = uint8_t(len);
    std::memcpy(d.bytes, data, size_t(len));
    if (xQueueSend(self->queue_, &d, 0) != pdTRUE) ++self->dropped_;
  }

  static inline EspNowAir* volatile self_ = nullptr;
  static inline uint8_t sniffedFrom_[6]{};
  static inline volatile int8_t sniffedRssi_ = -127;

  QueueHandle_t queue_ = nullptr;
  blorb::PeerAddr me_{};
  bool open_ = false;
  uint32_t rx_ = 0, tx_ = 0, sendErrors_ = 0;
  volatile uint32_t dropped_ = 0;
};

}  // namespace hw
