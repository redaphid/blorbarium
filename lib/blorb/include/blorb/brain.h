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
// The strongest rows become heirloom instinct genes at death (mutate.h).
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

  // Asleep. Each dream applies one queued instinct or replays one episode,
  // then decays W by forgetRate. True while a dream is showing.
  bool dream(const Temperament&, Rng&);
  void queueInstinct(const Instinct&);

  std::vector<Belief> strongestBeliefs(uint8_t n) const;   // sorted by |effect| * confidence
  Fx predict(LocusId feature, ActionId, DriveId) const;

  // Saves key weights by stable ids, so a firmware that adds a feature, an
  // action or a drive keeps every learned weight; new rows start at zero.
  struct Axes { std::vector<LocusId> features; std::vector<ActionId> actions; std::vector<DriveId> drives; };
  static Axes currentAxes();
  void loadWeights(const Axes& saved, const Q15* w);

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
  uint32_t dreamTick_ = 0;
};

}  // namespace blorb
