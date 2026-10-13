#pragma once
// The two primitives an update needs. mbedtls on the ESP32 (it ships in the
// Arduino core), OpenSSL in the native suite, so the tests check real
// signatures rather than a stand-in.
#include <cstddef>
#include <cstdint>

namespace ota {

class Sha256 {
 public:
  Sha256();
  ~Sha256();
  Sha256(Sha256&&) noexcept;
  Sha256& operator=(Sha256&&) noexcept;
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;
  void update(const uint8_t*, size_t);
  void finish(uint8_t out[32]);
 private:
  void* ctx_;
};

// ECDSA P-256 over SHA-256(msg). publicKey is the uncompressed point
// (0x04 || x || y), sig is raw r || s.
bool verifyP256(const uint8_t publicKey[65], const uint8_t* msg, size_t len, const uint8_t sig[64]);

}  // namespace ota
