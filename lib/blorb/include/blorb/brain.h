#pragma once
// The brain: a legible learner over (feature, action, drive).
//
//   W[f][a][d] = "when feature f is on and I do action a, drive d changes by this much"
//
// which is what C3 learned through its resp lobe, written down directly.
// Decide: score[a] = sum_d drive[d] * (-sum_f feat[f] * W[f][a][d]) - habit[a]
//                    + noise * (explore + arousal); argmax; held for minTicks.
// Learn:  W[f][a][d] += learnRate * startFeat[f] * trace * (observed[d] - predicted[d]).
// Reward is the observed fall of a pressing drive, so eating is good when
// hungry and worthless when full, and a shake can be fun when bored and awful
// when tired. Instincts are the same update fed from a gene while dreaming.
// What a life learned beyond its instincts becomes heirloom instinct genes
// at death (mutate.h).
#include <cstdint>
#include <vector>
#include "blorb/fixed.h"
#include "blorb/registry.h"

namespace blorb {

struct Temperament {               // decoded TemperamentGene
  Fx learnRate, forgetRate, explore, habituation;
  uint16_t dreamEveryTicks, dreamLenTicks;
  Decay trace;
};

struct Instinct { LocusId cue[3]; uint8_t cueCount; ActionId action; DriveId drive; Fx level; Fx strength; };

// One learned row, for the phone and for heirlooms.
struct Belief { LocusId feature; ActionId action; DriveId drive; Fx effect; Fx confidence; };

struct Decision { ActionId action; bool changed; };

class Brain {
 public:
  // Perceive, learn, decide. 5 Hz while awake. `features` follows FEATURES,
  // `drives` follows DRIVES.
  Decision think(const Fx features[kFeatureCount], const Fx drives[kDriveCount], Fx arousal,
                 const Temperament&, bool actionFinished, Rng&);

  // Asleep. Each call is one dream: it applies one queued instinct or replays
  // one episode. True if the dream had content. The caller paces dreams
  // (dreamEveryTicks) and shows them (dreamLenTicks).
  bool dream(const Temperament&, Rng&);
  // On waking from `dreams` dreams: every weight keeps (1 - forgetRate *
  // scale)^dreams of itself, so an unreinforced belief fades by the same
  // fraction each night whatever its size.
  void forget(const Temperament&, uint32_t dreams);
  void queueInstinct(const Instinct&);
  // At bedtime: each instinct is queued again at a share of its strength,
  // unless one for the same cues, action and drive is still waiting, so the
  // night's first dreams pull its cells back toward the genome.
  void refreshInstincts(const std::vector<Instinct>&);

  std::vector<Belief> strongestBeliefs(uint8_t n) const;   // sorted by |effect| * confidence
  // What this life built beyond `instincts`, which are rebuilt into a blank
  // table at full strength: cells whose weight grew away from that table
  // (|w| > |birth|), never cued on context, with confidence |w - birth| / 0.25
  // of at least `minConfidence`. Largest change first, at most n; effect is w.
  std::vector<Belief> lessons(const std::vector<Instinct>& instincts, Fx minConfidence, uint8_t n) const;
  Fx predict(LocusId feature, ActionId, DriveId) const;

  // Saves key weights by stable ids, so a firmware that adds a feature, an
  // action or a drive keeps every learned weight; new rows start at zero.
  struct Axes { std::vector<LocusId> features; std::vector<ActionId> actions; std::vector<DriveId> drives; };
  static Axes currentAxes();
  void loadWeights(const Axes& saved, const Q15* w);
  // One weight by stable ids (the prophecy twist). False if an id is on no axis.
  bool setWeight(LocusId feature, ActionId, DriveId, Fx effect);

  uint32_t hash() const;

 private:
  friend class Keepsake;
  struct Episode { Fx features[kFeatureCount]; ActionId action; Fx driveDelta[kDriveCount]; };
  Q15 w_[kFeatureCount][kActionCount][kDriveCount]{};
  Fx habit_[kActionCount]{};
  Fx startFeatures_[kFeatureCount]{};
  Fx startDrives_[kDriveCount]{};
  Fx trace_{};
  ActionId current_{};
  std::vector<Instinct> instinctQueue_;
  Episode episodes_[16]{};
  uint8_t episodeHead_ = 0;
  uint32_t heldTicks_ = UINT32_MAX;   // ticks the current action has run; starts saturated so a fresh brain decides at once
};

}  // namespace blorb
