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
  bool panelUp;                       // the panel driver's init said yes; it cannot see a dead screen
  bool resumed;                       // the keepsake loaded as Boot::Resumed
  std::optional<PetMark> handedOver;  // written by the old image before it switched slots
  PetMark now;
  uint32_t ranMs;                     // loop time since boot
  bool confirmed;                     // a page reached this image over Bluetooth (OTA CONFIRM)
};

enum class Verdict : uint8_t { NotOnTrial, Wait, Keep, RollBack };

constexpr uint32_t kTrialMs = 30000;            // the least an image runs before it keeps itself
constexpr uint32_t kConfirmWithinMs = 300000;   // no page by then: roll back, the safe way

inline Verdict judge(const TrialFacts& f) {
  if (!f.pending) return Verdict::NotOnTrial;
  // Every update writes the record before it switches, so a pending image
  // without one, or with another pet, is not the update that was sent.
  if (!f.panelUp || !f.resumed || !f.handedOver || !(f.now == *f.handedOver)) return Verdict::RollBack;
  if (f.confirmed && f.ranMs >= kTrialMs) return Verdict::Keep;
  if (!f.confirmed && f.ranMs >= kConfirmWithinMs) return Verdict::RollBack;
  return Verdict::Wait;
}

}  // namespace ota
