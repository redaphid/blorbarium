#pragma once
// Whether a freshly updated image keeps itself (docs/ota.md "The trial").
// Pure, so the native suite can hold every case; the firmware gathers the
// facts and acts on the verdict with esp_ota_*.
#include <cstdint>
#include <optional>

namespace ota {

// Which pet the old image handed over. Not the occupant kind: a catch-up at
// boot can hatch the egg that was handed over, and that is still the same pet.
struct PetMark {
  uint16_t generation;
  uint32_t genome;
  bool operator==(const PetMark& o) const { return generation == o.generation && genome == o.genome; }
};

struct TrialFacts {
  bool pending;                       // ESP_OTA_IMG_PENDING_VERIFY
  bool panelUp;
  bool resumed;                       // the keepsake loaded as Boot::Resumed
  std::optional<PetMark> handedOver;  // written by the old image just before the restart
  PetMark now;
  uint32_t ranMs;                     // loop time since boot
};

enum class Verdict : uint8_t { NotOnTrial, Wait, Keep, RollBack };

constexpr uint32_t kTrialMs = 30000;

inline Verdict judge(const TrialFacts& f) {
  if (!f.pending) return Verdict::NotOnTrial;
  if (!f.panelUp) return Verdict::RollBack;
  if (f.handedOver && (!f.resumed || !(f.now == *f.handedOver))) return Verdict::RollBack;
  if (f.ranMs < kTrialMs) return Verdict::Wait;
  return Verdict::Keep;
}

}  // namespace ota
