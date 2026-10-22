#pragma once
// Life stats (#38, docs/life-stats.md): a few dozen counters of what one pet's
// life did, from the kid's hands (knocks, holds, hangs, time held) to his
// meals, spells of hunger and sickness, naps, duels, prophecies and death,
// plus the same record of his parent and grandparent, carried into the egg.
// The server reads it (STATS json, or the Wi-Fi upload) as an input to the
// personality and diary it writes for him (#37, #39, #40). The badge's own
// analytics (#50, analytics.h) share its file, its saves and its uploads.
//
// Bookkeeping only. Nothing in the engine reads a counter back, so no
// decision, save, hash or replay moves with it: it lives in its own file on
// petfs (life.stats, beside taught.lines), never in the keepsake, its hash or
// lineage.log. The Dish counts in RAM as things happen and writes the file
// only when it saves the keepsake anyway, at most every kSaveEveryTicks
// unless a life event (an egg laid, a hatch, a stage, a death), a flush or
// the firmware (going dark) asks: a crash or power cut loses at most that
// much counting. A wipe erases petfs, and the record with it.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "blorb/analytics.h"
#include "blorb/creature.h"
#include "blorb/seams.h"
#include "blorb/senses.h"

namespace blorb {

struct Meeting;      // duel_book.h
struct TurnReport;   // duel.h
class Dish;          // dish.h

enum class StatUnit : uint8_t { Count, Ticks, Code };
struct StatInfo { uint8_t slot; const char* name; StatUnit unit; };

inline constexpr StatInfo LIFE_STATS[] = {
#define BLORB_LIFE_STAT(slot, name, unit) {slot, #name, StatUnit::unit},
#include "blorb/defs/life_stats.def"
#undef BLORB_LIFE_STAT
};
inline constexpr uint8_t kLifeStatCount = uint8_t(sizeof LIFE_STATS / sizeof LIFE_STATS[0]);

namespace stat {
enum : uint8_t {
#define BLORB_LIFE_STAT(slot, name, unit) name = slot,
#include "blorb/defs/life_stats.def"
#undef BLORB_LIFE_STAT
};
}  // namespace stat

constexpr bool lifeStatSlotsInOrder() {
  for (uint8_t i = 0; i < kLifeStatCount; ++i)
    if (LIFE_STATS[i].slot != i) return false;
  return true;
}
static_assert(lifeStatSlotsInOrder(), "life_stats.def: each row's slot is its position, from 0, with no gaps");

// One life's counters, by slot. `extra` holds the slots a newer firmware
// wrote that this one does not know, so a rollback writes them back.
struct LifeCounters {
  uint32_t v[kLifeStatCount]{};
  std::vector<uint32_t> extra;
  uint32_t operator[](uint8_t slot) const { return v[slot]; }
  bool operator==(const LifeCounters& o) const;
};

// A parent's or grandparent's life, as the egg carried it.
struct AncestorLife {
  uint16_t generation = 0;
  LifeCounters life;
};

// One pet's life: whose it is, its counters and its ancestors'. Plain data;
// Stats (below) counts into it.
struct LifeTally {
  static constexpr uint8_t kMaxExtra = 64;   // unknown slots carried from a newer firmware

  uint64_t lineageId = 0;
  uint16_t generation = 0;    // the life counted: the egg's or creature's; a clutch's is its dead parent's
  LifeCounters life;
  // Index 0 the parent, 1 the grandparent; absent for a founding life.
  std::vector<AncestorLife> ancestors;
  // Spells under way, so a reboot mid-spell does not count it twice. Saved.
  enum Spell : uint8_t { kStarving = 1, kSick = 2, kHurt = 4, kAsleep = 8, kAimed = 16 };
  uint8_t spells = 0;

  // The file's life section (Stats).
  std::vector<uint8_t> encode() const;
  static std::optional<LifeTally> decode(const uint8_t*, size_t);

  // The record a boot keeps, given what the flash held: this life's, or the
  // next one's if the egg was laid and its record never landed, or a fresh one.
  static LifeTally resume(const std::optional<LifeTally>& stored, uint64_t lineageId, uint16_t generation);
  // An egg was laid: the child's empty record, carrying this life (and its
  // parent) as the child's parent (and grandparent).
  LifeTally laid(uint16_t childGeneration) const;
};

// life.stats on petfs: "BLLS", a version byte, the write count (u32), then
// sections, each [tag u8][len u16 LE][bytes], then a crc32 of all before it.
// The life (#38) and the analytics (#50) are sections; a section this
// firmware does not know is carried and written back as it was.
class Stats {
 public:
  static constexpr const char* kFile = "life.stats";
  static constexpr uint8_t kVersion = 1;
  static constexpr uint32_t kSaveEveryTicks = 30 * kTicksPerMinute;
  enum Tag : uint8_t { kLife = 1, kAnalytics = 2 };
  struct Section { uint8_t tag; std::vector<uint8_t> bytes; };

  uint32_t seq = 0;            // writes so far, across lives: the server keeps the highest
  LifeTally life;
  Analytics analytics;
  std::vector<Section> unknown;

  // ---- the file ----
  struct Loaded {
    uint32_t seq = 0;
    std::optional<LifeTally> life;
    std::optional<Analytics> analytics;
    std::vector<Section> unknown;
  };
  static std::optional<Loaded> load(Storage&);   // nullopt: missing, torn, or not ours
  static std::optional<Loaded> decode(const uint8_t*, size_t);
  std::vector<uint8_t> encode() const;
  bool save(Storage&);        // seq + 1 on a write that landed
  // Boot: what the flash held, made this life's (LifeTally::resume).
  void resume(std::optional<Loaded>, uint64_t lineageId, uint16_t generation);
  void laid(uint16_t childGeneration);   // an egg was laid: the life becomes the child's (LifeTally::laid)

  // ---- counting: the Dish's calls, each where the event happens ----
  // Every tick, after the detectors: the body's stimuli (a phone's never
  // count) and the aim, whoever is in the dish. `sideHolding`: a hold now is
  // the side's, not BOOT's.
  void gestures(const SenseOut&, bool sideHolding, int8_t aim);
  // A press reached a living pet: whether a pellet dropped, and how he was
  // (FeedShow's refusals, feed_show.h).
  void fed(bool dropped, bool asleep, Fx hunger);
  void bitten(bool rotten);   // not bit(): Arduino.h defines it as a macro
  // He lived `ticks` more (1 a tick, or a catch-up's stride): time held,
  // asleep, starving, sick, hurt, his age and the stages he has reached.
  // `present`: false for a catch-up, which nobody held and he did not nap through.
  void lived(const Creature&, uint32_t ticks, bool present = true);
  void foretold() { add(stat::prophecies); }   // a shake's foretelling landed
  void fightOpened();
  void fightRefused() { analytics.add(anx::fight_refused); }
  // A turn resolved, as my screen shows it (DuelView::last, mirrored: index 0 is me).
  void turn(const TurnReport&);
  void met(const Meeting&, uint32_t battleTicks);
  void duelEnded(uint8_t endReason);   // EndReason (duel_session.h)
  void fainted() { add(stat::fainted); }
  void away(uint32_t seconds, bool hurt);
  void died(DeathCause, uint32_t ageTicks);

 private:
  void add(uint8_t slot, uint32_t n = 1);
  void spell(LifeTally::Spell, bool on, bool off, uint8_t count, uint8_t ticks, uint32_t n);
};

// ---- what the phone and the server read (docs/life-stats.md) ----
const char* lifeStatCauseName(uint32_t deathCode);   // "starved", or "" while alive
// The whole record as one JSON object: identity (lineage, generation, name,
// species, phase, stage, genome hash, firmware), this life, the ancestors',
// and the analytics. `mac`: the badge's MAC as "d4:05:...", for the Wi-Fi
// upload (the phone knows which badge it reads); `withGenome`: the genome
// itself, base64, for a server that has never seen it.
struct StatsIdentity { const char* mac = nullptr; bool withGenome = false; };
std::string lifeStatsJson(const Dish&, const StatsIdentity& = {});

}  // namespace blorb
