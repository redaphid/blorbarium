#pragma once
// SKETCH (docs/design/BATTLES.md). The duel book: friends, results, this
// life's duel XP, the kid's prowess and the pet's duel Skill, as one append-only CRC'd log on petfs
// (`duels.log`), folded at boot like lineage.log.
//
// Why a file and not a keepsake chunk: it changes no save format at all. The
// keepsake (snap.a/b) and lineage.log are untouched; an older firmware simply
// never opens duels.log, and a rollback after a duel loses nothing it knew.
// It outlives deaths (a friend made by great-grandma stays in the book) and
// survives a lost keepsake, as lineage.log does.
//
// Everything in RAM is derived by folding the log, so nothing is synced:
//   level      from the XP of entries whose generation is the current one
//   skill      the last Skill frame of the current generation (none: zeros)
//   prowess    the kid's running performance (all lives: it is the kid's, not the pet's)
#include <cstdint>
#include <optional>
#include "blorb/duel.h"
#include "blorb/seams.h"

namespace blorb {

// A battle as one device saw it. Written once, when the friends screen opens.
struct Meeting {
  BattleCard friendCard{};
  Side me = Side::A;
  Ending ending = Ending::Draw;
  uint8_t turns = 0;
  BattleId battle{};            // the two books name the same battle (and the same rematch)
  uint32_t outcomeHash = 0;     // Outcome::hash(): the two books can be compared from the phone
  bool fizzled = false;         // the friend wandered off, or a lid ended it: a draw, half XP
  // How well my kid played, 0..255: the share of turns my kid picked (not the
  // pet) that won or bonked, weighted toward later turns. Feeds prowess only.
  uint8_t performance = 128;
  uint8_t manualTurns = 0;      // a battle the pet played alone moves prowess not at all
  DuelSkill skillAfter{};
};

struct FriendSummary {
  uint64_t lineageId = 0;
  SpeciesId species{};
  CardLook look{};
  uint16_t met = 0, won = 0, lost = 0, drawn = 0;
  uint8_t daysMet = 0;          // distinct pet days with a duel: the friendship's depth, shown as hearts in the book
  uint16_t firstGeneration = 0;
  uint32_t lastAt = 0;          // pet ticks
};

// XP: a win 3, a loss 2 (losing still grows you), a draw 2, a fizzle 1.
// level = 1 + floor(sqrt(xp / 2)), capped: 2 at 2 XP, 4 at 18, 8 at 98, 12 at 242.
constexpr uint8_t levelFromXp(uint16_t xp) {
  uint8_t l = 1;
  while (l < DuelRules::kMaxLevel && uint32_t(l) * l * 2 <= xp) ++l;
  return l;
}
static_assert(levelFromXp(0) == 1 && levelFromXp(2) == 2 && levelFromXp(242) == 12 && levelFromXp(60000) == 12,
              "level curve");

class DuelBook {
 public:
  static constexpr uint8_t kProwessGainShift = 3;   // prowess += (performance - prowess) / 8 per battle
  static constexpr uint32_t kByteCap = 64 * 1024;   // compaction keeps one summary per friend

  static DuelBook open(Storage&);   // scan once, truncate a torn tail

  // Idempotent by battle id: a repeat after a crash is a no-op.
  void record(const Meeting&, uint16_t generation, uint32_t at, uint16_t petDay);

  uint16_t xp(uint16_t generation) const;
  uint8_t level(uint16_t generation) const { return levelFromXp(xp(generation)); }
  const DuelSkill& skill() const { return skill_; }     // current generation's
  uint8_t prowess() const { return prowess_; }
  uint8_t lossesInARow() const { return lossStreak_; }
  uint16_t friendCount() const { return friends_; }

  // Streaming reads for the phone (FRIENDS) and the home screen's rim faces.
  template <class F> void forEachFriend(F&& f) const;   // f(const FriendSummary&), most recent first
  std::optional<FriendSummary> friendOf(uint64_t lineageId) const;

 private:
  Storage* store_ = nullptr;
  uint16_t generation_ = 0, xp_ = 0, friends_ = 0;
  uint8_t prowess_ = 128, lossStreak_ = 0;
  DuelSkill skill_{};
};

}  // namespace blorb
