#pragma once
// v2 FINAL SKETCH. The oracle: grungo's model of what happens next.
//
// The brain (brain.h) learns what his actions do to his drives. The oracle
// learns what the world does next, from what is going on and what he is
// doing. It learns from whether things happen, never from a drive falling, so
// it keeps learning while he is content.
//
//   E[cue][omen] = P(omen fires within the horizon | this cue is on)
//
// Learning: every beat a snapshot of the 12 strongest cues enters a ring.
// When a snapshot is horizon beats old, each omen's target is 1 if it fired
// inside that window and 0 if not, and E moves toward it by NLMS. A window
// label is 0 or 1, never a per-tick hazard, so a rare omen does not underflow
// Q15. Forgetting is one proportional multiply at pet dawn.
//
// Readers, and nothing else computes an expectation:
//   expect_<topic> loci   brain features; genes turn them into brace, appetite, dread
//   surprise / let_down   genes decide if surprise delights or frightens
//   Outlook               anticipation (the bell becomes a clicker) and planning (begging, superstition)
//   prophesy()            what Foresee shows and what the 8-ball says, then judged kept or broken
//   the day map           what usually happens in each quarter hour: routine
//   lore                  the strongest learned beliefs, laid at death
//
// Pure over its inputs and its own state; integer only; one step per tick.
#include <cstdint>
#include <vector>
#include "blorb/fixed.h"
#include "blorb/genes_v2.h"
#include "blorb/registry.h"
#include "blorb/v2_registry.h"

namespace blorb {

// Cues: every brain feature except the expect band (its own output), then
// the oracle-only cues (cues.def), then a one-hot of the action he is doing,
// then a half-strength one-hot of the action before it. Derived, never listed.
constexpr size_t countOracleFeatures() {
  size_t n = 0;
  for (LocusId l : FEATURES) n += isExpectLocus(l) ? 0 : 1;
  return n;
}
inline constexpr size_t kOracleFeatureCount = countOracleFeatures() + countOf(ORACLE_CUES);
inline constexpr size_t kCueCount = kOracleFeatureCount + 2 * kActionCount;
inline constexpr size_t kRingBeats = 16;      // the longest horizon a genome can ask for
inline constexpr size_t kSnapshotCues = 12;   // a beat keeps its 12 strongest cues
inline constexpr size_t kDreamMemories = 8;   // the day's most surprising moments
inline constexpr size_t kDayBins = 96;        // quarter hours of the pet day
inline constexpr size_t kClaimLog = 8;        // the last prophecies and how they ended
static_assert(kCueCount <= 255, "a snapshot names its cues by a byte");

// Decoded from SeerGene, OracleGene and PaceGene, once per stage.
struct SeerTraits {
  uint16_t beatTicks;
  uint8_t horizonBeats;           // <= kRingBeats
  Fx learnRate, dawnForget, forethought, curiosity, wishfulness, bar, routineRate;
  Fx oracleChance;                // OracleGene.chance: a shake gives a prophecy instead of a hop
  Fx topicWeight[kTopicCount];    // OracleGene.topics: what this line likes to talk about
};

// What each omen does to each drive, read from his stimulus genes for the
// omen's stimulus. Derived from the phenotype, never learned, never saved.
struct OmenEffects { Fx delta[kOmenCount][kDriveCount]; };

// What a prophecy claims, so the board can check it.
struct Claim {
  enum class Kind : uint8_t { Omen, DriveAbove };
  Kind kind = Kind::Omen;
  OmenId omen{};                  // Omen: this omen fires inside the window
  DriveId drive{};                // DriveAbove: this drive is above `level` at the window's opening
  Fx level{};
};
enum class Source : uint8_t { Foresee, Shake, Phone };

// One committed prophecy. Saved, so a reboot inside the window still judges it.
struct Prophecy {
  Claim claim{};
  Source source = Source::Foresee;
  TopicId topic{};
  Fx confidence{};
  uint32_t madeTick = 0, opensTick = 0, closesTick = 0;   // the phone may open a window tomorrow
  bool grounded = false;          // false: nothing cleared his bar, an improv line, never judged
  bool active = false;
};

// The kept-or-broken log the phone reads (PROPHECIES) and the lineage totals.
struct ClaimRecord { Claim claim; Source source; Fx confidence; uint32_t madeTick; bool kept; };

// What he shows above his head.
struct Vision {
  enum class Kind : uint8_t { None, Foresee, Prophecy, Kept, Broken, Dream };
  Kind kind = Kind::None;
  OmenId omen{};
  Fx confidence{};                // the pictogram's opacity
  Fx windowLeft{};                // the sand ring, 1 -> 0 toward the deadline
  uint16_t minutes = 0;           // "+ 9 MIN" under the pictogram; 0 = soon
};

struct OracleOut {
  Fx expect[kTopicCount]{};       // grounded topics -> expect_<topic> loci
  Fx surprise{}, letDown{};
  StimId fire[4]{};               // prophecy_kept, prophecy_broken, self_surprised, self_let_down
  uint8_t fireCount = 0;
  Vision vision{};
};

// Fed to Brain::think at each decision. With forethought 0 and no omens it
// is all zeros, and the brain behaves exactly as today (the regression test).
struct Outlook {
  Fx actionBias[kActionCount]{};  // forethought x sum_omen E[token a][omen] x value(omen) + curiosity x uncertainty(a)
  Fx anticipated[kDriveCount]{};  // sum_omen p(omen) x effect(omen, drive)
};

// The brain's credit for a change in anticipation over an action: only the
// part that strengthened counts. A promise made is felt when it is made; a
// promise kept is felt through the drive itself, so eating an expected
// pellet is not discounted for having been expected (candidate A's rule; B's
// symmetric form discounted it).
constexpr Fx strengthening(Fx before, Fx after) {
  if (after >= Fx::zero()) return fxMax(Fx::zero(), after - fxMax(before, Fx::zero()));
  return fxMin(Fx::zero(), after - fxMin(before, Fx::zero()));
}
static_assert(strengthening(Fx::zero(), Fx::ratio(-32, 100)) == Fx::ratio(-32, 100), "the bell's promise is credited");
static_assert(strengthening(Fx::ratio(-32, 100), Fx::zero()) == Fx::zero(), "a kept promise is not charged back");
static_assert(strengthening(Fx::ratio(-5, 10), Fx::ratio(-3, 10)) == Fx::zero(), "a fading promise is not charged");
static_assert(strengthening(Fx::ratio(1, 10), Fx::ratio(3, 10)) == Fx::ratio(2, 10), "growing dread is credited");

// The 8-ball's odds on a shake: the gene's chance, closed by oracle_gate
// after shakes close together (oracle_tired). Asleep he always hops.
constexpr Fx shakeOdds(Fx oracleChance, Fx oracleGate, bool asleep) {
  return asleep ? Fx::zero() : oracleChance * clamp01(oracleGate);
}

class Oracle {
 public:
  // Every tick, awake or asleep. `fired` is this tick's omens (routed stimuli
  // mapped through OMENS). On a beat it closes the oldest window (learn),
  // snapshots `cues` and predicts. Every tick it scores surprise and judges
  // the active prophecy against `fired` or `drives`.
  OracleOut step(const Fx cues[kCueCount], OmenMask fired, const Fx drives[kDriveCount], uint16_t dayBin,
                 uint32_t tick, const SeerTraits&);

  // Foresee or the 8-ball. score(omen) = topicWeight x (p + wishfulness x value+)
  // over the near predictions and the day map's next two bins. Under `bar`
  // the result is ungrounded: an improv topic drawn by the weights, no claim.
  // Draws from `rng` the same number of times either way. While a prophecy is
  // active it returns that one unchanged, so a double trigger is a no-op.
  Prophecy prophesy(const SeerTraits&, const OmenEffects&, const Fx drives[kDriveCount], uint16_t dayBin,
                    uint32_t tick, Source, Rng&);
  // The phone's prophecy twist, already parsed and range-checked in
  // twists.cpp. It replaces an active one; false if its window is closed.
  bool accept(const Prophecy&, uint32_t tick);

  void outlook(const SeerTraits&, const OmenEffects&, const Fx drives[kDriveCount], Outlook&) const;

  // Asleep: replay one of the day's most surprising moments into E and say
  // what he dreams of. False when there is none.
  bool dream(const SeerTraits&, Rng&, Vision& shown);
  // At pet dawn: proportional forgetting and the day map closes the day.
  // A saved day index makes a second call for the same day a no-op, so the
  // unpowered catch-up can call it freely.
  void dawn(const SeerTraits&, uint32_t dayIndex);

  // Lore. The beliefs that moved most from `birth` (a fresh oracle seeded
  // with this genome's lore), bias cues excluded: Omen and Routine kinds.
  struct Belief { LoreKind kind; uint8_t a, b; Fx p, learned; };
  std::vector<Belief> strongestLearned(const Oracle& birth, uint8_t n) const;
  void seed(const LoreGene&);     // one NLMS nudge, like an instinct

  Fx predicted(OmenId) const;
  const Prophecy& prophecy() const { return prophecy_; }
  uint8_t claimLog(const ClaimRecord*& out) const { out = log_; return logCount_; }
  uint16_t made() const { return made_; }
  uint16_t kept() const { return kept_; }
  uint32_t hash() const;

 private:
  friend class Keepsake;
  struct Snapshot { uint32_t tick; uint8_t n; uint8_t cue[kSnapshotCues]; uint8_t level[kSnapshotCues]; };
  struct Memory { Snapshot at; OmenId omen; Fx surprise; };

  // Saved.
  Q15 e_[kCueCount][kOmenCount]{};
  uint8_t routine_[kDayBins][kTopicCount]{};  // 0..255: how often a topic happens in this quarter hour
  uint16_t todaySeen_[kDayBins]{};            // topic bits seen today, closed into routine_ at dawn
  uint32_t lastDawnDay_ = 0;
  Prophecy prophecy_{};
  ClaimRecord log_[kClaimLog]{};
  uint8_t logCount_ = 0, logHead_ = 0;
  uint16_t made_ = 0, kept_ = 0;               // this life; the Death entry records them
  Memory memories_[kDreamMemories]{};
  uint8_t memoryCount_ = 0;

  // Not saved: a reboot drops at most one horizon of windows in flight.
  Snapshot ring_[kRingBeats]{};
  uint8_t ringHead_ = 0, ringFill_ = 0;
  uint16_t firedBeats_[kOmenCount]{};          // bit i = fired i beats ago
  Fx p_[kOmenCount]{};
  Fx surprise_{}, letDown_{};
  uint32_t lastBeatTick_ = 0;
};

}  // namespace blorb
