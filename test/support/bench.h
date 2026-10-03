#pragma once
// A stand-in for the Dish (unit 14) with the same tick order (DESIGN.md
// section 3) and the same occupant transitions, minus the keepsake and the
// protocol: detectors, pet clock, habitat, Egg -> Creature -> Clutch -> Egg,
// and the lineage records on the way. Tests that need a whole life use it
// until the Dish exists.
#include <utility>
#include "blorb/lineage.h"
#include "blorb/seams.h"

namespace blorbtest {

class Bench {
 public:
  Bench(blorb::Storage& store, uint32_t speciesSeed, uint64_t lineageId, const blorb::Genome& founder)
      : lineage_(blorb::Lineage::open(store, founder, lineageId, 0)),
        occ_(blorb::Egg(blorb::Offspring{founder, {}}, 0, 0)),
        rng_(blorb::Rng::seeded(speciesSeed)) {}

  void sample(const blorb::BodySample& s, uint32_t ms) { detectors_.sample(s, ms, pending_); }

  void tick(uint32_t ms, blorb::Link& link) {
    for (int n = 0; n < 10 && ms - lastTickMs_ >= blorb::kTickMs; ++n) {
      lastTickMs_ += blorb::kTickMs;
      runOneTick(link);
    }
  }

  const blorb::Occupant& occupant() const { return occ_; }
  const blorb::Habitat& habitat() const { return habitat_; }
  const blorb::PetClock& clock() const { return clock_; }
  blorb::Lineage& lineage() { return lineage_; }
  uint32_t tickCount() const { return tick_; }

 private:
  static bool fired(const blorb::SenseOut& s, blorb::StimId id) {
    for (uint8_t i = 0; i < s.stimCount; ++i) if (s.stimuli[i] == id) return true;
    return false;
  }

  void runOneTick(blorb::Link& link) {
    using namespace blorb;
    SenseOut out = pending_;
    pending_.clear();
    clock_.advance(detectors_.lid.lidded());
    detectors_.tick(TickContext{clock_, link.connected(), tick_}, out);
    for (uint8_t i = 0; i < out.lociCount; ++i) {
      if (out.loci[i].locus == locus::tilt_x) tiltX_ = out.loci[i].value;
      if (out.loci[i].locus == locus::tilt_y) tiltY_ = out.loci[i].value;
    }
    if (auto* c = std::get_if<Creature>(&occ_)) {
      const HabitatRules& rules = c->phenotype().habitat;
      if (fired(out, stim::button)) habitat_.dropPellet(rules, rng_, tick_, out);
      habitat_.step(rules, tiltX_, tiltY_, c->body().at, tick_, out);
      c->tick(out, habitat_, behaviours_, tick_);
      if (c->dead()) {
        lineage_.recordDeath(*c, lineage_.currentName(), tick_);
        Unlocks u = unlocksFor(lineage_.legacyFeats());
        occ_ = c->layClutch(u.clutchSize, u.wildBonus, tick_);
      }
    } else if (auto* e = std::get_if<Egg>(&occ_)) {
      if (e->tick(out)) {
        Creature born = Creature::hatch(*e, lineage_.legacyFeats(), tick_);
        habitat_ = Habitat{};
        habitat_.pantry = born.phenotype().habitat.pantrySize;
        occ_ = std::move(born);
      }
    } else {
      Clutch& k = std::get<Clutch>(occ_);
      k.derivePreviewStep();
      if (std::optional<uint8_t> i = k.tick(out)) {
        Egg egg(k.child(*i), k.generation, tick_);
        lineage_.recordBirth(egg, k, *i, tick_);
        occ_ = std::move(egg);
      }
    }
    ++tick_;
  }

  blorb::Lineage lineage_;
  blorb::Occupant occ_;
  blorb::Habitat habitat_;
  blorb::PetClock clock_;
  blorb::Detectors detectors_;
  blorb::Behaviours behaviours_;
  blorb::SenseOut pending_;
  blorb::Rng rng_;
  blorb::Fx tiltX_ = blorb::Fx::ratio(1, 2), tiltY_ = blorb::Fx::ratio(1, 2);
  uint32_t tick_ = 0, lastTickMs_ = 0;
};

}  // namespace blorbtest
