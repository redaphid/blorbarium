#pragma once
// The duel book (docs/design/BATTLES.md): friends, results, each life's duel
// XP, the kid's prowess and the pet's duel Skill, as one append-only CRC'd log
// on petfs (`duels.log`), folded at open like lineage.log.
//
// Why a file and not a keepsake chunk: it changes no save format. The
// keepsake (snap.a/b) and lineage.log are untouched; an older firmware never
// opens duels.log, and a rollback after a duel loses nothing it knew. It
// outlives deaths (a friend made by great-grandma stays in the book) and
// survives a lost keepsake, as lineage.log does.
//
// Everything in RAM is the fold of the log, so nothing is synced:
//   level, skill   from the entries of the generation asked about
//   prowess        the kid's running performance (all lives: it is the kid's, not the pet's)
//
// 0.7.0 also kept a dark ledger of duel deaths (Mortal falls, slayings, spared
// lives, trophies, vendettas). No duel kills now, so none of it is kept; its
// entries and bytes in an older book are read past (duel_book.cpp).
#include <cstdint>
#include <optional>
#include <vector>
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
  bool fizzled = false;         // the friend wandered off, or a lid ended it: a draw, less XP
  // How well my kid played, 0..255: the share of turns my kid picked (not the
  // pet) that won or bonked, weighted toward later turns. Feeds prowess only.
  uint8_t performance = 128;
  uint8_t manualTurns = 0;      // a battle the pet played alone moves prowess not at all
  DuelSkill skillAfter{};
  bool fled = false;
  uint8_t scarsTaken = 0;
};

struct FriendSummary {
  uint64_t lineageId = 0;
  SpeciesId species{};
  CardLook look{};
  uint16_t met = 0, won = 0, lost = 0, drawn = 0;
  uint8_t daysMet = 0;           // distinct pet days with a duel: the friendship's depth, shown as hearts
  uint16_t firstGeneration = 0;
  uint32_t lastAt = 0;           // pet ticks
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

// The fold of duels.log: what the book knows. A plain value, so a snapshot
// of it can be written back as entries (compaction) and compared in tests.
struct BookState {
  struct Friend {
    FriendSummary s{};
    uint16_t lastDay = 0;        // the pet day of the newest duel, for daysMet
    uint16_t forKeepsDay = 0;    // the pet day of the newest Scrap
    uint8_t forKeepsThatDay = 0;
  };

  uint16_t generation = 0;       // the newest life with a duel in the book
  uint16_t xp = 0;               // that life's
  DuelSkill skill{};             // that life's newest
  uint8_t prowess = 128, lossStreak = 0;
  uint32_t manualTurns = 0;
  std::vector<Friend> friends;      // most recent last
};

class DuelBook {
 public:
  static constexpr uint8_t kProwessGainShift = 3;   // prowess += (performance - prowess) / 8 per battle
  static constexpr uint32_t kByteCap = 64 * 1024;   // past it, compaction keeps one summary per friend
  static constexpr uint8_t kKeepRecent = 8;         // Meetings kept whole through compaction, for idempotence

  static DuelBook open(Storage&);   // scan once, truncate a torn tail

  // Idempotent by battle id: a repeat after a crash is a no-op.
  void record(const Meeting&, uint16_t generation, uint32_t at, uint16_t petDay);
  // A cradle while he wants one after a run of losses: the run is over
  // (duel_card.h, Decline::WantsACuddle). Nothing is written without a run.
  void recordCuddle(uint32_t at);

  uint16_t xp(uint16_t generation) const { return generation == state_.generation ? state_.xp : 0; }
  uint8_t level(uint16_t generation) const { return levelFromXp(xp(generation)); }
  // The life's newest Skill, or zeros if it has not dueled.
  DuelSkill skill(uint16_t generation) const;
  uint8_t prowess() const { return state_.prowess; }
  uint32_t manualTurns() const { return state_.manualTurns; }   // every turn this device's kid picked, all lives
  uint8_t lossesInARow() const { return state_.lossStreak; }
  uint16_t friendCount() const { return uint16_t(state_.friends.size()); }

  // The two lineages' record, for the head-to-head gates.
  struct HeadToHead { uint16_t duels = 0, myWins = 0, theirWins = 0; };
  HeadToHead headToHead(uint64_t friendLineage) const;
  // Scraps with this lineage on this pet day (kForKeepsPerPairPerDay).
  uint8_t forKeepsOn(uint64_t friendLineage, uint16_t petDay) const;

  // Streaming reads for the phone (FRIENDS) and the home screen's rim faces.
  template <class F> void forEachFriend(F&& f) const {   // f(const FriendSummary&), most recent first
    for (auto it = state_.friends.rbegin(); it != state_.friends.rend(); ++it) f(it->s);
  }
  std::optional<FriendSummary> friendOf(uint64_t lineageId) const;

  const BookState& state() const { return state_; }

 private:
  bool append(const std::vector<uint8_t>& frames);   // all or nothing, then compactIfNeeded
  void compactIfNeeded();
  Storage* store_ = nullptr;
  BookState state_{};
};

}  // namespace blorb
