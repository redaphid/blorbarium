#include "wifiota/improv.h"

#include <cstring>

namespace wifiota::improv {

uint8_t checksum(const uint8_t* p, size_t n) {
  uint32_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += p[i];
  return uint8_t(sum);
}

size_t encodeResult(Command c, const std::string_view* strings, size_t count, uint8_t* out, size_t cap) {
  size_t data = 0;
  for (size_t i = 0; i < count; ++i) {
    if (strings[i].size() > 255) return 0;
    data += 1 + strings[i].size();
  }
  if (data > 255 || 3 + data > cap) return 0;
  out[0] = uint8_t(c);
  out[1] = uint8_t(data);
  size_t at = 2;
  for (size_t i = 0; i < count; ++i) {
    out[at++] = uint8_t(strings[i].size());
    std::memcpy(out + at, strings[i].data(), strings[i].size());
    at += strings[i].size();
  }
  out[at] = checksum(out, at);
  return at + 1;
}

std::optional<Credentials> parseCredentials(const uint8_t* data, size_t n) {
  Credentials c;
  if (n < 1) return std::nullopt;
  const size_t ssid = data[0];
  if (ssid < 1 || ssid > 32 || n < 1 + ssid + 1) return std::nullopt;
  const size_t pass = data[1 + ssid];
  if (pass > 64 || n != 1 + ssid + 1 + pass) return std::nullopt;
  std::memcpy(c.ssid, data + 1, ssid);
  c.ssid[ssid] = 0;
  std::memcpy(c.password, data + 2 + ssid, pass);
  c.password[pass] = 0;
  // A NUL inside would cut the name short when it is joined: not the network that was sent.
  if (std::strlen(c.ssid) != ssid || std::strlen(c.password) != pass) return std::nullopt;
  return c;
}

void Machine::write(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (have_ == sizeof buf_) return fail(Error::InvalidRpc), reset();
    buf_[have_++] = p[i];
  }
  if (have_ < 2) return;
  const size_t whole = 3 + size_t(buf_[1]);
  if (have_ < whole) return;   // the rest is in a later write
  const size_t got = have_;
  reset();
  if (got != whole || checksum(buf_, whole - 1) != buf_[whole - 1]) return fail(Error::InvalidRpc);
  run(Command(buf_[0]), buf_ + 2, buf_[1]);
}

void Machine::run(Command c, const uint8_t* data, size_t n) {
  switch (c) {
    case Command::WifiSettings: {
      if (state_ == State::Provisioning) return fail(Error::Unknown);   // one join at a time
      const std::optional<Credentials> creds = parseCredentials(data, n);
      if (!creds) return fail(Error::InvalidRpc);
      if (error_ != Error::None) port_.showError(error_ = Error::None);
      set(State::Provisioning);
      return port_.join(*creds);
    }
    case Command::DeviceInfo: {
      if (error_ != Error::None) port_.showError(error_ = Error::None);
      const std::string_view s[] = {info_.firmware, info_.version, info_.chip, info_.name};
      return result(c, s, 4);
    }
  }
  fail(Error::UnknownRpc);
}

void Machine::joined(std::string_view url) {
  if (state_ != State::Provisioning) return;
  set(State::Provisioned);
  result(Command::WifiSettings, &url, 1);
}

void Machine::couldNotJoin() {
  if (state_ != State::Provisioning) return;
  fail(Error::UnableToConnect);
  set(State::Authorized);
}

void Machine::set(State s) {
  state_ = s;
  port_.showState(s);
}

void Machine::fail(Error e) {
  error_ = e;
  port_.showError(e);
}

void Machine::result(Command c, const std::string_view* strings, size_t count) {
  uint8_t out[kMaxPacket];
  if (const size_t n = encodeResult(c, strings, count, out, sizeof out)) port_.sendResult(out, n);
}

}  // namespace wifiota::improv
