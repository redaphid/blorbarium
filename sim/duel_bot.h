#pragma once
// A kid at a duel, for films: BOOT twice to open, then each turn a think, a
// tilt toward one of the three rim stances, and a lock: mostly a knock, now
// and then BOOT for a quick special or a 1.2 s hold for a heavy one, after
// which the pet braces through its wind-up turns and the kid waits. Now and
// then a shake (the pet foretells) or a double knock (a cheer). Seeded and on
// the script's clock, so a film replays.
//
// It plays blind on a fixed turn length, because nothing yet says when a turn
// opens or that the friend is bracing. Once the Dish has a duel view, the sim
// should set friendBracing from it, and the bot should wait for each turn to
// open (a side must not lock turn t+1 before turn t resolves) instead of
// keeping time (sim/README.md, "The kid").
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iterator>
#include <string>

namespace duelbot {

// Which rim each stance is, in mg of tilt. Until D9's DuelInput::from fixes
// the real mapping these are three rims a third of a turn apart.
struct StanceTilt {
  const char* name;
  int x, y;
};
inline constexpr StanceTilt kStances[3] = {{"pounce", 0, -600}, {"guard", -520, 300}, {"trick", 520, 300}};

struct Move {
  uint32_t at;
  std::string line;   // a sim hand line, `!knock`
  const char* why;    // what a caption calls it
};

class KidBot {
 public:
  KidBot(uint32_t seed, uint32_t startMs, uint32_t turnMs) : rng_(seed * 2654435761u + 1), turnAt_(startMs + 1500), turnMs_(turnMs) {
    plan_.push_back({startMs, "!button", "BOOT: duel?"});
    plan_.push_back({startMs + 300, "!button", "BOOT again"});
  }

  // The friend's pet is winding up a special: a Trick now staggers it.
  bool friendBracing = false;

  // The next move due by `now`, or false.
  bool next(uint32_t now, Move& out) {
    if (plan_.empty()) planTurn();
    if (plan_.front().at > now) return false;
    out = plan_.front();
    plan_.pop_front();
    return true;
  }

 private:
  uint32_t roll(uint32_t n) {   // xorshift32
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_ % n;
  }

  void put(uint32_t at, std::string line, const char* why) {
    auto it = plan_.end();
    while (it != plan_.begin() && std::prev(it)->at > at) --it;
    plan_.insert(it, Move{at, std::move(line), why});
  }

  void planTurn() {
    const uint32_t t = turnAt_;
    turnAt_ += turnMs_;
    if (windup_) {
      windup_--;
      put(t + turnMs_ / 4, "", "waits: the special winds up");
      return;
    }
    const uint32_t think = turnMs_ / 7 + roll(turnMs_ * 3 / 7);   // a kid takes a moment, and never the whole turn
    const bool punish = friendBracing && roll(100) < 70;
    const StanceTilt& s = kStances[punish ? 2 : roll(3)];
    if (roll(100) < 15) put(t + think / 2, "!shake 900", "shakes: what will they do?");
    char tilt[32];
    snprintf(tilt, sizeof(tilt), "!tilt %d %d", s.x, s.y);
    put(t + think, tilt, punish ? "trick: stagger it!" : s.name);
    const uint32_t lock = t + think + 400 + roll(600);
    const uint32_t how = roll(100);
    if (how < 20) {
      put(lock, "!button", "BOOT: quick special");
      windup_ = 1;
    } else if (how < 32) {
      put(lock, "!button 1300", "BOOT held: heavy special");
      windup_ = 2;
    } else {
      put(lock, "!knock", "knock: locked");
    }
    put(lock + 1500, "!tilt 0 0", "");
    if (roll(100) < 15) put(t + turnMs_ - 600, "!dtap", "double knock: cheer");
  }

  uint32_t rng_;
  uint32_t turnAt_, turnMs_;
  uint32_t windup_ = 0;   // turns still bracing after a special
  std::deque<Move> plan_;
};

}  // namespace duelbot
