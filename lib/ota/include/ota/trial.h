#pragma once
// Whether a freshly updated image keeps itself (docs/ota.md "The trial").
// judge() is the verdict; Trial runs it against a TrialPort, which the
// firmware implements with esp_ota_* and nvs and the native suite in memory.
#include <cstdint>
#include <optional>
#include "ota/types.h"

namespace ota {

// Which pet the old image handed over. Not the occupant kind: a catch-up at
// boot can hatch the egg that was handed over, and that is still the same pet.
struct PetMark {
  uint16_t generation;
  uint32_t genome;
  bool operator==(const PetMark& o) const { return generation == o.generation && genome == o.genome; }
};

// What the old image records before it switches slots.
struct Handover {
  // Which pet, or none when the old image could not read one (a sealed or
  // newer-format keepsake): then any pet the new image resumes is the one,
  // which is how an update wakes a sealed pet.
  std::optional<PetMark> pet;
  Version to;     // the version it switched to: named in STATUS if that one rolls back
  Token token;    // the page whose update it was: only it may CONFIRM
};

struct TrialFacts {
  bool pending;                       // ESP_OTA_IMG_PENDING_VERIFY
  bool panelUp;                       // the panel driver's init said yes; it cannot see a dead screen
  bool resumed;                       // the keepsake loaded as Boot::Resumed
  bool handedOver;                    // the old image wrote its record before it switched slots
  std::optional<PetMark> pet;         // the pet it names, if it could read one
  PetMark now;
  uint32_t ranMs;                     // loop time since boot
  bool confirmed;                     // the page reached this image over Bluetooth (OTA CONFIRM)
};

enum class Verdict : uint8_t { NotOnTrial, Wait, Keep, RollBack };

constexpr uint32_t kTrialMs = 30000;            // the least an image runs before it keeps itself
constexpr uint32_t kConfirmWithinMs = 300000;   // no page by then: roll back, the safe way

inline Verdict judge(const TrialFacts& f) {
  if (!f.pending) return Verdict::NotOnTrial;
  // Every update writes the record before it switches, so a pending image
  // without one, or with another pet, is not the update that was sent.
  if (!f.panelUp || !f.resumed || !f.handedOver || (f.pet && !(f.now == *f.pet))) return Verdict::RollBack;
  if (f.confirmed && f.ranMs >= kTrialMs) return Verdict::Keep;
  if (!f.confirmed && f.ranMs >= kConfirmWithinMs) return Verdict::RollBack;
  return Verdict::Wait;
}

class TrialPort {
 public:
  virtual ~TrialPort() = default;
  virtual bool pending() = 0;                              // the running image is on trial
  virtual std::optional<Handover> handover() = 0;
  virtual void forgetHandover() = 0;
  virtual void noteRolledBack(std::optional<Version>) = 0; // what STATUS says as rolledback=
  virtual void keep() = 0;                                 // esp_ota_mark_app_valid_cancel_rollback
  virtual void rollBack() = 0;                             // restarts into the old image; returns only if it could not
  virtual void say(const char* line) = 0;                  // the serial log
};

class Trial {
 public:
  enum class Poll : uint8_t { Running, Kept, GaveUp };   // Kept and GaveUp each come once: the pet may save again

  void begin(TrialPort& port, bool panelUp, bool resumed, PetMark now) {
    port_ = &port;
    const std::optional<Handover> h = port.handover();
    facts_ = TrialFacts{port.pending(), panelUp, resumed, h.has_value(), h ? h->pet : std::nullopt, now, 0, false};
    if (h) token_ = h->token;
    if (!facts_.pending && h) {
      // A record and no trial: the image it named never kept itself.
      port.noteRolledBack(h->to);
      port.forgetHandover();
      port.say("[hw] ota: the last update rolled back");
    }
  }

  Poll poll(uint32_t ms) {
    if (!facts_.pending) return Poll::Running;
    facts_.ranMs = ms;
    switch (judge(facts_)) {
      case Verdict::Keep:
        port_->keep();
        port_->forgetHandover();
        port_->noteRolledBack(std::nullopt);
        facts_.pending = false;
        port_->say("[hw] ota trial: kept");
        return Poll::Kept;
      case Verdict::RollBack:
        port_->say(facts_.confirmed ? "[hw] ota trial: FAILED (checks), rolling back"
                                    : "[hw] ota trial: FAILED (checks or no page), rolling back");
        port_->rollBack();
        // Still here: the bootloader has nothing to go back to. Keep running
        // this image and let the pet live and save, rather than retry forever.
        facts_.pending = false;
        port_->say("[hw] ota trial: rollback FAILED, keeping this image");
        return Poll::GaveUp;
      default:
        return Poll::Running;
    }
  }

  Confirm confirm(const Token& t) {
    if (!facts_.pending) return Confirm::NotOnTrial;
    if (!token_ || *token_ != t) return Confirm::NotYours;
    if (!facts_.confirmed) port_->say("[hw] ota trial: a page reached it over Bluetooth");
    facts_.confirmed = true;
    return Confirm::Confirmed;
  }

  bool onTrial() const { return facts_.pending; }

 private:
  TrialPort* port_ = nullptr;
  TrialFacts facts_{};
  std::optional<Token> token_;
};

}  // namespace ota
