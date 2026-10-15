#pragma once
// The press that opens the debug link's harmful commands to one connection
// (docs/debug-ble.md, "The unlock"). It is the update's question
// (ota/session.h): the screen asks, a press in its first 1.5 s was already
// under way and is no yes, and 30 s with no press lapses. A yes is for the
// connection that asked and no other, so a reconnect starts locked, and so
// does a stranger's phone that connects after the owner's.
#include <cstdint>
#include <optional>

namespace debugble {

class Unlock {
 public:
  static constexpr uint32_t kAskMs = 30000;
  static constexpr uint32_t kMinAskMs = 1500;
  enum class Event : uint8_t { None, Opened, Lapsed };

  // Ask the hand on the dish for `connection`. Asking again for the same one
  // keeps the question as it is, so a repeated UNLOCK cannot restart the
  // 1.5 s a press must wait.
  void ask(uint32_t now, uint32_t connection) {
    if (isOpen(connection) || (asking_ && asking_->connection == connection)) return;
    asking_ = Asking{connection, now};
  }
  // The dish's button, while the question shows. True: the press was the
  // question's, and the pet must not feel it.
  bool press(uint32_t now) {
    if (!asking_) return false;
    if (now - asking_->since >= kMinAskMs) {
      open_ = asking_->connection;
      asking_.reset();
      event_ = Event::Opened;
    }
    return true;
  }
  void tick(uint32_t now) {
    if (asking_ && now - asking_->since >= kAskMs) {
      asking_.reset();
      event_ = Event::Lapsed;
    }
  }
  void lock() {
    open_.reset();
    asking_.reset();
  }
  void cancelQuestion() { asking_.reset(); }

  bool isOpen(uint32_t connection) const { return open_ && *open_ == connection; }
  bool asking() const { return asking_.has_value(); }
  // The connection asking or answered for, if any.
  std::optional<uint32_t> owner() const { return asking_ ? std::optional<uint32_t>(asking_->connection) : open_; }
  uint32_t msLeft(uint32_t now) const {
    return asking_ && now - asking_->since < kAskMs ? kAskMs - (now - asking_->since) : 0;
  }
  // What happened since the last call, once.
  Event takeEvent() {
    const Event e = event_;
    event_ = Event::None;
    return e;
  }

 private:
  struct Asking { uint32_t connection, since; };
  std::optional<Asking> asking_;
  std::optional<uint32_t> open_;
  Event event_ = Event::None;
};

}  // namespace debugble
