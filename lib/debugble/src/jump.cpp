#include "debugble/jump.h"

#include <variant>
#include "blorb/registry.h"

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

Jump die(blorb::Dish& dish, bool eggs, const Warp& warp) {
  auto* c = std::get_if<blorb::Creature>(&dish.occupant());
  if (!c) return Jump::NoCreature;
  c->fall();
  warp(1);
  const auto* k = std::get_if<blorb::Clutch>(&dish.occupant());
  if (!k) return Jump::NoCreature;   // the next tick runs the death; nothing else can stand here
  while (eggs && (k->sinceDeath < blorb::Clutch::kVigilTicks || k->previewed < k->count)) {
    warp(100);
    k = std::get_if<blorb::Clutch>(&dish.occupant());
    if (!k) break;   // the pick's timeout outran the vigil: an egg is laid, which is still a valid dish
  }
  dish.flush();
  return Jump::Done;
}

std::optional<blorb::ChemId> chemNamed(std::string_view w) {
  for (const blorb::DriveInfo& d : blorb::DRIVES)
    if (w == d.name) return blorb::driveChem(d.id);
  // A chemical by its number, as twists take them: 1 to 3 digits.
  std::optional<unsigned> number;
  if (!w.empty() && w.size() <= 3 && w.find_first_not_of("0123456789") == std::string_view::npos) {
    number = 0;
    for (char ch : w) *number = *number * 10 + unsigned(ch - '0');
  }
  for (const blorb::ChemInfo& c : blorb::CHEMICALS)
    if (number ? c.id.v == *number : w == c.name) return c.id;
  return std::nullopt;
}

Jump setChems(blorb::Dish& dish, const ChemSet* sets, size_t n) {
  blorb::Creature* c = std::get_if<blorb::Creature>(&dish.occupant());
  if (!c) return Jump::NoCreature;
  for (size_t i = 0; i < n; ++i) c->inject(sets[i].chem, sets[i].level);
  dish.markDirty();
  return Jump::Done;
}

}  // namespace debugble
