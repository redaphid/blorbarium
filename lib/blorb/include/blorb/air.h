#pragma once
// The device-to-device radio seam (docs/design/BATTLES.md, "Radio"), a fourth
// seam beside Storage, TimeSource and Link. Datagrams, not lines: a duel
// packet is one ESP-NOW frame. Firmware implements it over ESP-NOW
// (src/hw/radio_espnow.h), the simulator over a socket (sim/serial_air.h),
// tests over FakeAirs that drop, double, reorder and delay on a seeded schedule.
//
// The consent rule is in the contract: a closed Air hears nothing and sends
// nothing, and only the local owner's gesture opens it (DuelSession::open
// takes a LocalConsent only the Dish can make). No packet, phone verb or twist
// can open it, so a duel cannot reach a device whose owner did not ask.
#include <cstddef>
#include <cstdint>
#include <optional>

namespace blorb {

struct PeerAddr {
  uint8_t mac[6]{};
  friend bool operator==(const PeerAddr& a, const PeerAddr& b) {
    for (int i = 0; i < 6; ++i) if (a.mac[i] != b.mac[i]) return false;
    return true;
  }
  friend bool operator!=(const PeerAddr& a, const PeerAddr& b) { return !(a == b); }
  friend bool operator<(const PeerAddr& a, const PeerAddr& b) {   // side A is the lower address
    for (int i = 0; i < 6; ++i) if (a.mac[i] != b.mac[i]) return a.mac[i] < b.mac[i];
    return false;
  }
  bool none() const { return *this == PeerAddr{}; }
};

// ESP-NOW v1's payload cap. The S3's Arduino core here is 2.0.17 on IDF 4.4
// (measured: framework-arduinoespressif32 3.20017), which has no v2 frames.
inline constexpr size_t kDatagramMax = 250;

struct Datagram {
  PeerAddr from{};
  int8_t rssi = 0;        // dBm as the radio heard it: the lobby's nearness gate
  uint8_t len = 0;
  uint8_t bytes[kDatagramMax]{};
};

class Air {
 public:
  virtual ~Air() = default;
  // Radio on, on the fixed duel channel. False while the radio is OTA's (an
  // update session, or a firmware image still on trial): the pet shakes his
  // head and the duel never starts.
  virtual bool open() = 0;
  virtual void close() = 0;                                      // idempotent; frees the Wi-Fi heap
  virtual bool isOpen() const = 0;
  virtual PeerAddr self() const = 0;
  virtual void broadcast(const uint8_t*, size_t) = 0;            // the lobby beacon
  virtual void send(const PeerAddr&, const uint8_t*, size_t) = 0; // battle state, unicast (link-layer ack and retry)
  virtual std::optional<Datagram> receive() = 0;                 // oldest first; nothing while closed
};

}  // namespace blorb
