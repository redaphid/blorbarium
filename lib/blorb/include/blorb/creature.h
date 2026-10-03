#pragma once
// One life is a run. The dish holds exactly one Occupant:
//
//   Egg --hatch--> Creature --die--> Clutch (remains, then 1..3 eggs) --pick--> Egg
//
// One std::variant, so "dead and hatching" or "no pet at all" cannot exist.
// Every transition is a pure function of saved state plus a seed drawn and
// saved before it, so a crash at any point replays to the same egg.
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>
#include "blorb/actions.h"
#include "blorb/brain.h"
#include "blorb/chemistry.h"
#include "blorb/genes.h"
#include "blorb/genome.h"
#include "blorb/habitat.h"
#include "blorb/mutate.h"
#include "blorb/senses.h"

namespace blorb {

struct EggRules { uint32_t incubateTicks; Fx warmthBoost; uint8_t hatchBurstDreams; };

// What gene expression built. Grows at each stage; never shrinks in a life.
struct Phenotype {
  ChemRules chem;
  Temperament temperament;
  HabitatRules habitat;
  EggRules egg;
  struct Paint { RegionId region; Tint tint; ChemId bound; Fx gain; bool hasBound; };
  struct Mark { uint8_t layer, variant, tint; ChemId bound; bool hasBound; };
  struct Face { ExprId face; Fx weight; DriveId drive[3]; Fx amount[3]; };
  struct StimResponse { StimId stim; bool whenAsleep, wakes; ChemId chem[3]; Fx amount[3]; };
  struct Size { uint8_t base, growth, squash; };
  // A duplicated palette or mark gene appends here: a second spot, a brighter glow.
  std::vector<Paint> palette;
  std::vector<Mark> marks;
  std::vector<Face> faces;
  std::vector<StimResponse> stimuli;   // several genes for one stimulus all apply
  std::vector<Instinct> instincts;     // every instinct expressed so far
  Size size{};
  uint8_t expressedStages = 0;         // bitmask of Stage; makes expressStage idempotent
};

enum class DeathCause : uint8_t { OldAge, Starved, Injured, Poisoned, Unknown };

struct LifeStats {                     // feeds the lineage's Death entry and the feats
  uint32_t ageTicks = 0;
  Stage reached = Stage::Baby;
  uint16_t fed = 0, cradled = 0, played = 0, knocked = 0, shaken = 0, dropped = 0;
  uint16_t nights = 0, dreams = 0, foresights = 0, hops = 0;
  uint16_t actionTicks[kActionCount]{};
};

// The face shown, with hysteresis, and the one it is crossfading from.
struct FaceState { ExprId current{}, previous{}; Fx intensity{}; uint32_t sinceTick = 0; };

class Egg;
struct Clutch;

class Creature {
 public:
  // Express Baby, apply initial chemicals, run the hatch burst of dreams so
  // instincts are wired before the first decision. Rng seeded from the genome hash.
  static Creature hatch(const Egg&, uint32_t legacyFeats, uint32_t tick);

  // One 100 ms tick, in this order and no other (replays depend on it):
  //  1 sense loci from `senses` (detectors and habitat already wrote them)
  //  2 stimuli -> stimulus genes -> chemicals; recent loci = 1; whenAsleep gate; wakes
  //  3 chemistry.step
  //  4 lifecycle: stage loci express the next stage once; die locus; reflex
  //    loci rising past 0.5 start a reflex (strength = locus level)
  //  5 brain on even ticks: think while awake, dream while asleep
  //  6 behaviour step, unless a reflex owns the body; its stimuli land next tick
  //  7 face: expression genes over the drive mix, with hysteresis; stats; recent loci halve
  void tick(const SenseOut& senses, Habitat&, Behaviours&, uint32_t tick);

  bool dead() const;
  DeathCause cause() const;

  // Death. Captures heirloom beliefs and draws the clutch seeds from this
  // creature's rng. The Dish saves immediately after, so it runs once.
  Clutch layClutch(uint8_t clutchSize, Fx wildBonus, uint32_t tick);

  const Genome& genome() const { return genome_; }
  uint16_t generation() const { return generation_; }
  Stage stage() const { return stage_; }
  const Body& body() const { return body_; }
  const Chemistry& chemistry() const { return chem_; }
  const Brain& brain() const { return brain_; }
  const Phenotype& phenotype() const { return pheno_; }
  const LifeStats& stats() const { return stats_; }
  const FaceState& face() const { return face_; }
  ActionId action() const { return action_; }
  uint32_t hash() const;               // chem, loci, W, body, tick: the replay check

  void inject(ChemId, Fx);             // sim scripts and tests only; no wire verb reaches it
  // Sim scripts and tests only: starts that action at the next tick, as the
  // brain would, and holds it for its minTicks unless it finishes.
  void force(ActionId);
  void fire(StimId);                   // the STIM verb; the caller has checked source == Phone

 private:
  friend class Keepsake;               // the only other writer of private state
  Creature() = default;
  Genome genome_;                      // immutable this life: no non-const accessor
  uint16_t generation_ = 0;
  uint32_t legacyFeats_ = 0;
  Stage stage_ = Stage::Baby;
  Phenotype pheno_;
  Chemistry chem_;
  Brain brain_;
  Body body_;
  LifeStats stats_;
  FaceState face_;
  ActionId action_{};
  uint32_t actionStart_ = 0;           // in stats_.ageTicks
  bool actionDone_ = false;            // its behaviour finished and stopped; the brain picks next
  std::optional<ActionId> forced_;     // force(), applied at the next tick
  SenseOut pendingSelf_;               // self-stimuli fired this tick, applied next tick
  Rng rng_{};
};

class Egg {
 public:
  Egg(Offspring child, uint16_t generation, uint32_t laidTick);
  // Time incubates it; held or cradled adds warmthBoost; a knock wobbles it.
  // True once ready; the Dish then calls Creature::hatch.
  bool tick(const SenseOut&);
  const Genome& genome() const { return child_.genome; }
  const MutationDiff& diff() const { return child_.diff; }
  uint16_t generation() const { return generation_; }
  Fx progress() const;
  uint32_t hash() const;
 private:
  friend class Keepsake;
  Offspring child_;
  uint16_t generation_;
  uint32_t laidTick_;
  uint32_t incubated_ = 0;
  EggRules rules_;                     // decoded from the child's EggGene
};

// What the clutch screen shows per egg. Derived, never saved.
struct EggPreview { Tint skin, cloak, shell; uint8_t lookChanges, mindChanges; };

// After a death: the remains for the vigil, then 1..3 eggs. Seeds, not
// genomes: egg i is mutate(parent, policy, heirlooms, Rng::seeded(seeds[i])).
// With one egg there is no choice and it is laid when the vigil ends.
struct Clutch {
  static constexpr uint32_t kVigilTicks = 30 * kTicksPerMinute;
  static constexpr uint32_t kPickTimeoutTicks = 30 * kTicksPerMinute;   // then the cursor egg is picked
  Genome parent;
  uint16_t generation = 0;             // the children's generation
  std::vector<Belief> heirlooms;       // captured from the brain at death
  Fx wildBonus{};
  uint64_t seeds[kMaxClutch]{};
  uint8_t count = 1, cursor = 0;
  uint32_t sinceDeath = 0;
  DeathCause cause = DeathCause::Unknown;
  Stage reached = Stage::Baby;
  EggPreview previews[kMaxClutch]{};
  uint8_t previewed = 0;               // previews[0..previewed) are valid; derived, never saved

  Offspring child(uint8_t i) const;    // pure; runs mutate() with its viability retries
  // Body choice: Button or Knock moves the cursor, ButtonHold or DoubleKnock
  // picks; the timeout picks the cursor. Returns the chosen index once.
  std::optional<uint8_t> tick(const SenseOut&);
  // One egg per call (one mutate and dry run, about 30 ms estimated), called
  // once per tick during the vigil, so death never stalls a frame. Idempotent.
  void derivePreviewStep();
};

using Occupant = std::variant<Egg, Creature, Clutch>;

}  // namespace blorb
