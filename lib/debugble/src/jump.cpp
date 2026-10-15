#include "debugble/jump.h"

#include <variant>

namespace debugble {

using blorb::Fx;

namespace {

// Longer than any egg a genome can lay (EggGene's minutes are a byte), so a
// hatch that never comes is a bug, not a hang.
constexpr uint32_t kMaxIncubateTicks = 256 * blorb::kTicksPerMinute;

}  // namespace

Jump hatch(blorb::Dish& dish, const Warp& warp) {
  if (!std::holds_alternative<blorb::Egg>(dish.occupant())) return Jump::NoEgg;
  for (uint32_t t = 0; std::holds_alternative<blorb::Egg>(dish.occupant()) && t < kMaxIncubateTicks; t += 100) warp(100);
  dish.flush();
  return Jump::Done;
}

Jump growTo(blorb::Dish& dish, blorb::Stage stage, const Warp& warp) {
  if (std::holds_alternative<blorb::Egg>(dish.occupant())) hatch(dish, warp);
  auto* c = std::get_if<blorb::Creature>(&dish.occupant());
  if (!c) return Jump::NoCreature;
  if (c->stage() > stage) return Jump::PastStage;
  const Fx nudge = Fx::ratio(1, 50);
  while (c->stage() < stage) {
    const Fx life = c->chemistry().chem[blorb::chem::life.v];
    if (life <= nudge) return Jump::Died;
    c->inject(blorb::chem::life, life - nudge);
    warp(2);
    c = std::get_if<blorb::Creature>(&dish.occupant());
    if (!c) return Jump::Died;
  }
  for (const auto& d : blorb::DRIVES) c->inject(blorb::driveChem(d.id), Fx::zero());
  c->inject(blorb::chem::injury, Fx::zero());
  c->inject(blorb::chem::toxin, Fx::zero());
  c->inject(blorb::chem::adrenaline, Fx::zero());
  c->inject(blorb::chem::energy, Fx::one());
  warp(20);
  dish.flush();
  return Jump::Done;
}

}  // namespace debugble
