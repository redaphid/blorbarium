#include "ota/crypto.h"

#include <utility>

#if defined(ARDUINO_ARCH_ESP32)
#include <mbedtls/ecdsa.h>
#include <mbedtls/sha256.h>

namespace ota {

Sha256::Sha256() : ctx_(new mbedtls_sha256_context) {
  auto* c = static_cast<mbedtls_sha256_context*>(ctx_);
  mbedtls_sha256_init(c);
  mbedtls_sha256_starts_ret(c, 0);
}
Sha256::~Sha256() {
  if (!ctx_) return;
  mbedtls_sha256_free(static_cast<mbedtls_sha256_context*>(ctx_));
  delete static_cast<mbedtls_sha256_context*>(ctx_);
}
void Sha256::update(const uint8_t* p, size_t n) { mbedtls_sha256_update_ret(static_cast<mbedtls_sha256_context*>(ctx_), p, n); }
void Sha256::finish(uint8_t out[32]) { mbedtls_sha256_finish_ret(static_cast<mbedtls_sha256_context*>(ctx_), out); }

bool verifyP256(const uint8_t publicKey[65], const uint8_t* msg, size_t len, const uint8_t sig[64]) {
  uint8_t hash[32];
  if (mbedtls_sha256_ret(msg, len, hash, 0) != 0) return false;
  mbedtls_ecp_group grp;
  mbedtls_ecp_point q;
  mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point_init(&q);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  const bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                  mbedtls_ecp_point_read_binary(&grp, &q, publicKey, 65) == 0 &&
                  mbedtls_mpi_read_binary(&r, sig, 32) == 0 && mbedtls_mpi_read_binary(&s, sig + 32, 32) == 0 &&
                  mbedtls_ecdsa_verify(&grp, hash, sizeof hash, &q, &r, &s) == 0;
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&q);
  mbedtls_ecp_group_free(&grp);
  return ok;
}

}  // namespace ota

#else
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/params.h>

namespace ota {

Sha256::Sha256() : ctx_(EVP_MD_CTX_new()) { EVP_DigestInit_ex(static_cast<EVP_MD_CTX*>(ctx_), EVP_sha256(), nullptr); }
Sha256::~Sha256() { EVP_MD_CTX_free(static_cast<EVP_MD_CTX*>(ctx_)); }
void Sha256::update(const uint8_t* p, size_t n) { EVP_DigestUpdate(static_cast<EVP_MD_CTX*>(ctx_), p, n); }
void Sha256::finish(uint8_t out[32]) {
  unsigned n = 32;
  EVP_DigestFinal_ex(static_cast<EVP_MD_CTX*>(ctx_), out, &n);
}

bool verifyP256(const uint8_t publicKey[65], const uint8_t* msg, size_t len, const uint8_t sig[64]) {
  EVP_PKEY* key = nullptr;
  EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
  OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, const_cast<char*>("prime256v1"), 0),
      OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, const_cast<uint8_t*>(publicKey), 65),
      OSSL_PARAM_construct_end()};
  const bool loaded = kctx && EVP_PKEY_fromdata_init(kctx) == 1 &&
                      EVP_PKEY_fromdata(kctx, &key, EVP_PKEY_PUBLIC_KEY, params) == 1;
  EVP_PKEY_CTX_free(kctx);
  if (!loaded) return false;

  ECDSA_SIG* es = ECDSA_SIG_new();
  ECDSA_SIG_set0(es, BN_bin2bn(sig, 32, nullptr), BN_bin2bn(sig + 32, 32, nullptr));
  unsigned char* der = nullptr;
  const int derLen = i2d_ECDSA_SIG(es, &der);
  ECDSA_SIG_free(es);

  EVP_MD_CTX* md = EVP_MD_CTX_new();
  const bool ok = derLen > 0 && EVP_DigestVerifyInit(md, nullptr, EVP_sha256(), nullptr, key) == 1 &&
                  EVP_DigestVerify(md, der, size_t(derLen), msg, len) == 1;
  EVP_MD_CTX_free(md);
  OPENSSL_free(der);
  EVP_PKEY_free(key);
  return ok;
}

}  // namespace ota
#endif

namespace ota {
Sha256::Sha256(Sha256&& o) noexcept : ctx_(std::exchange(o.ctx_, nullptr)) {}
Sha256& Sha256::operator=(Sha256&& o) noexcept {
  std::swap(ctx_, o.ctx_);
  return *this;
}
}  // namespace ota
