#pragma once
// Improv Wi-Fi over Bluetooth LE (https://www.improv-wifi.com/ble/), the
// de-facto standard a page or an app uses to hand a device a Wi-Fi network
// (ESPHome, WLED and Home Assistant speak it; it has a Web Bluetooth SDK).
// The packet codec and the state machine, free of NimBLE so the native suite
// drives them (test/test_wifi_ota). src/hw/improv_ble.h is the GATT service;
// src/wifi_ota.h joins the network. docs/wifi-ota.md, "Provisioning".
//
//   authorized --Wi-Fi settings--> provisioning --joined--> provisioned (result: the page's URL)
//                                               --failed--> authorized  (error: unable to connect)
//
// The badge needs no authorization step (the spec's state 1): see
// docs/wifi-ota.md, "Security", for why and what a stranger could do.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace wifiota::improv {

inline constexpr const char* kService = "00467768-6228-2272-4663-277478268000";
inline constexpr const char* kCurrentState = "00467768-6228-2272-4663-277478268001";   // read, notify
inline constexpr const char* kErrorState = "00467768-6228-2272-4663-277478268002";     // read, notify
inline constexpr const char* kRpcCommand = "00467768-6228-2272-4663-277478268003";     // write
inline constexpr const char* kRpcResult = "00467768-6228-2272-4663-277478268004";      // read, notify
inline constexpr const char* kCapabilities = "00467768-6228-2272-4663-277478268005";   // read
// Advertised as 16-bit service data: the state, the capabilities, four zero bytes.
inline constexpr uint16_t kServiceDataUuid = 0x4677;
inline constexpr uint8_t kCanIdentify = 0x01;   // capabilities bit 0

enum class State : uint8_t { AuthorizationRequired = 1, Authorized = 2, Provisioning = 3, Provisioned = 4 };
enum class Error : uint8_t { None = 0, InvalidRpc = 1, UnknownRpc = 2, UnableToConnect = 3, NotAuthorized = 4, Unknown = 0xff };
// 1 and 2 are the BLE spec's; 3 and 4 are the serial spec's numbers, which
// newer clients send over BLE too. A client that never sends them loses nothing.
enum class Command : uint8_t { WifiSettings = 1, Identify = 2, DeviceInfo = 3, ScanNetworks = 4 };

// Command, data length, data, checksum.
inline constexpr size_t kMaxPacket = 3 + 255;

// The low byte of the sum of every byte before it.
uint8_t checksum(const uint8_t* p, size_t n);

// An RPC result: the command, the data length, then each string as a length
// and its bytes, then the checksum. 0 when it does not fit `cap` or 255 bytes of data.
size_t encodeResult(Command, const std::string_view* strings, size_t count, uint8_t* out, size_t cap);

// What the Wi-Fi settings command carries. NUL-terminated copies.
struct Credentials {
  char ssid[33] = "";       // 1..32 bytes
  char password[65] = "";   // 0..64 bytes: empty is an open network
};
// The command's data: an SSID length and bytes, then a password length and bytes, nothing after.
std::optional<Credentials> parseCredentials(const uint8_t* data, size_t n);

// One network a scan heard.
struct Network {
  char ssid[33] = "";
  int8_t rssi = 0;
  bool secured = false;
};

// What the machine needs from the badge.
class Port {
 public:
  virtual ~Port() = default;
  virtual void showState(State) = 0;                        // the Current State characteristic: set it and notify
  virtual void showError(Error) = 0;                        // the Error State characteristic
  virtual void sendResult(const uint8_t* p, size_t n) = 0;  // the RPC Result characteristic
  virtual void identify() = 0;                              // show which badge this is
  virtual void join(const Credentials&) = 0;                // try them: answer joined() or couldNotJoin()
  virtual void scan() = 0;                                  // look for networks: answer scanned()
};

// Device info's four strings.
struct DeviceInfo {
  const char* firmware;
  const char* version;
  const char* chip;
  const char* name;
};

class Machine {
 public:
  static constexpr size_t kMaxNetworks = 12;   // a scan's results, strongest first, one SSID once

  Machine(Port& port, DeviceInfo info) : port_(port), info_(info) {}

  State state() const { return state_; }
  Error error() const { return error_; }

  // Bytes written to RPC Command. A packet may come in several writes; it
  // runs once its length is whole. A bad checksum or length is InvalidRpc.
  void write(const uint8_t* p, size_t n);
  // A new connection: half a packet from the last one is dropped.
  void reset() { have_ = 0; }
  // The join's outcome. `url` is where the page goes next (the result's one string).
  void joined(std::string_view url);
  void couldNotJoin();
  void scanned(const Network* networks, size_t n);

 private:
  void run(Command, const uint8_t* data, size_t n);
  void set(State s);
  void fail(Error e);
  void result(Command c, const std::string_view* strings, size_t count);

  Port& port_;
  DeviceInfo info_;
  State state_ = State::Authorized;
  Error error_ = Error::None;
  uint8_t buf_[kMaxPacket] = {};
  size_t have_ = 0;
};

}  // namespace wifiota::improv
