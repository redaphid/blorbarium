#pragma once
// The pet's storage, read through and written not at all while held. The Dish
// of an image on trial runs on it, so the keepsake and lineage the old image
// can read stay exactly as it left them until the new one keeps itself
// (docs/ota.md "The trial"). A write refused here is one the Dish already
// treats as failed: a keepsake save retries, a lineage entry is dropped.
#include "blorb/seams.h"

namespace ota {

class HeldStorage : public blorb::Storage {
 public:
  explicit HeldStorage(blorb::Storage& inner) : inner_(inner) {}
  void hold(bool held) { held_ = held; }
  bool held() const { return held_; }

  std::optional<size_t> read(const char* name, size_t offset, uint8_t* buf, size_t cap) override {
    return inner_.read(name, offset, buf, cap);
  }
  size_t size(const char* name) override { return inner_.size(name); }
  bool writeAtomic(const char* name, const uint8_t* data, size_t len) override {
    return !held_ && inner_.writeAtomic(name, data, len);
  }
  bool append(const char* name, const uint8_t* data, size_t len) override {
    return !held_ && inner_.append(name, data, len);
  }
  bool rename(const char* from, const char* to) override { return !held_ && inner_.rename(from, to); }

 private:
  blorb::Storage& inner_;
  bool held_ = false;
};

}  // namespace ota
