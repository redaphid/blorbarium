#include "blorb/ble_name.h"

#include <variant>
#include "blorb/dish.h"
#include "blorb/founding.h"
#include "blorb/species.h"

namespace blorb {

const char* advertisedName(Dish& dish, const uint8_t mac[6], char (&out)[blename::kMax + 1]) {
  const Genome* pet = nullptr;
  if (!dish.sealed()) {
    const Occupant& o = dish.occupant();
    if (const Creature* c = std::get_if<Creature>(&o)) pet = &c->genome();
    else if (const Egg* e = std::get_if<Egg>(&o)) pet = &e->genome();
    else pet = &std::get<Clutch>(o).parent;
  }
  const char* species = speciesInfo(advertisedSpecies(pet, mac)).name;
  return blename::make(dish.sealed() ? nullptr : dish.lineage().currentName(), species, out);
}

}  // namespace blorb
