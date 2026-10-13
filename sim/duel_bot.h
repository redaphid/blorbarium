#pragma once
// A kid at a duel, for films: BOOT twice to open, then each turn a think, a
// tilt toward one of the three rim stances and a knock to lock it in, with
// now and then a shake (the pet foretells), a double knock (a cheer) or BOOT
// (the power). Seeded and on the script's clock, so a film replays.
//
// It plays blind on a fixed turn length, because nothing yet says when a turn
// opens. Once the Dish has a duel view, the bot should wait for the turn to
// open and lock before its deadline instead (see docs/hardware-testing.md).
#include <cstdint>
#include <cstdio>
#include <deque>
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

  void planTurn() {
    const uint32_t t = turnAt_;
    turnAt_ += turnMs_;
    const uint32_t think = turnMs_ / 7 + roll(turnMs_ * 3 / 7);   // a kid takes a moment, and never the whole turn
    const StanceTilt& s = kStances[roll(3)];
    if (roll(100) < 15) plan_.push_back({t + think / 2, "!shake 900", "shakes: what will they do?"});
    if (roll(100) < 20) plan_.push_back({t + think - 400, "!button", "BOOT: power!"});
    char tilt[32];
    snprintf(tilt, sizeof(tilt), "!tilt %d %d", s.x, s.y);
    plan_.push_back({t + think, tilt, s.name});
    plan_.push_back({t + think + 400 + roll(600), "!knock", "knock: locked"});
    plan_.push_back({t + think + 1600, "!tilt 0 0", ""});
    if (roll(100) < 15) plan_.push_back({t + turnMs_ - 1200, "!dtap", "double knock: cheer"});
  }

  uint32_t rng_;
  uint32_t turnAt_, turnMs_;
  std::deque<Move> plan_;
};

}  // namespace duelbot
