#include "blorb/habitat.h"

namespace blorb {
namespace {

constexpr Fx kDropRadius = Fx::ratio(6, 10);
constexpr Fx kRimRadius = Fx::ratio(92, 100);       // nothing rolls past this
constexpr Fx kBiteRange = Fx::ratio(12, 100);
constexpr Fx kSenseRange = Fx::one();                // *_near is 1 on top of it, 0 this far away or more
constexpr Fx kLevelDeadZone = Fx::ratio(3, 100);     // about 3 degrees: a dish on a desk stays put
constexpr Fx kPelletSlide = Fx::ratio(6, 100);       // per tick per unit of tilt; pellets do not coast
constexpr Fx kHalf = Fx::ratio(1, 2);

uint64_t isqrt64(uint64_t v) {
  uint64_t r = 0, bit = uint64_t(1) << 62;
  while (bit > v) bit >>= 2;
  for (; bit != 0; bit >>= 2) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else { r >>= 1; }
  }
  return r;
}

Fx fxSqrt(Fx v) { return v.raw <= 0 ? Fx::zero() : Fx{int32_t(isqrt64(uint64_t(v.raw) * Fx::kOne))}; }
Fx fxDiv(Fx a, Fx b) { return Fx::sat(int64_t(a.raw) * Fx::kOne / b.raw); }

Fx dist2(DishPos a, DishPos b) {
  Fx dx = a.x - b.x, dy = a.y - b.y;
  return dx * dx + dy * dy;
}

// Downhill push from a tilt locus (0.5 = level); tilt above 0.5 raises the + edge.
Fx downhill(Fx tilt) {
  Fx d = kHalf - tilt;
  return (d > kLevelDeadZone || d < -kLevelDeadZone) ? d : Fx::zero();
}

// Pulls a point back to the rim radius; returns the outward unit normal if it did.
bool keepInside(DishPos& p, DishPos& normal) {
  Fx r2 = p.x * p.x + p.y * p.y;
  if (r2 <= kRimRadius * kRimRadius) return false;
  Fx r = fxSqrt(r2);
  normal = DishPos{fxDiv(p.x, r), fxDiv(p.y, r)};
  p = DishPos{normal.x * kRimRadius, normal.y * kRimRadius};
  return true;
}

Fx nearness(Fx d2) { return clamp01(Fx::one() - fxDiv(fxSqrt(d2), kSenseRange)); }

bool rotten(const Pellet& p, const HabitatRules& rules, uint32_t tick) { return tick - p.droppedTick > rules.rotTicks; }

// Which pellets count: every one, only fresh ones, or only rotten ones.
enum class Kind : uint8_t { Any, Fresh, Rotten };

int nearestIndex(const Pellet (&pellets)[Habitat::kMaxPellets], DishPos from, Kind kind = Kind::Any,
                 const HabitatRules* rules = nullptr, uint32_t tick = 0) {
  int best = -1;
  Fx bestD2{};
  for (int i = 0; i < Habitat::kMaxPellets; ++i) {
    if (!pellets[i].present) continue;
    if (kind != Kind::Any && rotten(pellets[i], *rules, tick) != (kind == Kind::Rotten)) continue;
    Fx d2 = dist2(pellets[i].at, from);
    if (best < 0 || d2 < bestD2) { best = i; bestD2 = d2; }
  }
  return best;
}

}  // namespace

void Habitat::step(const HabitatRules& rules, Fx tiltX, Fx tiltY, DishPos creature, uint32_t tick, SenseOut& out,
                   bool rotIsFood) {
  // One back each refillTicks; a coarse step (the unpowered catch-up) brings
  // back every one it covered. A full pantry holds its clock at now, so the
  // next one is a whole refill after the first drop.
  while (pantry < rules.pantrySize && tick - pantryTick >= rules.refillTicks) {
    ++pantry;
    pantryTick += rules.refillTicks;
  }
  if (pantry >= rules.pantrySize) {
    pantry = rules.pantrySize;
    pantryTick = tick;
  }

  Fx ax = downhill(tiltX), ay = downhill(tiltY);
  DishPos normal{};
  for (Pellet& p : pellets) {
    if (!p.present) continue;
    p.at = DishPos{p.at.x + ax * kPelletSlide, p.at.y + ay * kPelletSlide};
    keepInside(p.at, normal);
  }

  int near = nearestIndex(pellets, creature);
  int food = nearestIndex(pellets, creature, rotIsFood ? Kind::Any : Kind::Fresh, &rules, tick);
  out.set(locus::food_near, food < 0 ? Fx::zero() : nearness(dist2(pellets[food].at, creature)));
  out.set(locus::food_rotten, near >= 0 && rotten(pellets[near], rules, tick) ? Fx::one() : Fx::zero());
  out.set(locus::pantry, Fx::ratio(pantry, rules.pantrySize));
}

namespace {
// An empty slot, or the oldest pellet's: a full dish gives that one up.
int freeSlot(const Pellet (&pellets)[Habitat::kMaxPellets], uint32_t tick) {
  int slot = 0;
  for (int i = 0; i < Habitat::kMaxPellets; ++i) {
    if (!pellets[i].present) return i;
    if (tick - pellets[i].droppedTick > tick - pellets[slot].droppedTick) slot = i;
  }
  return slot;
}
}  // namespace

// A full dish replaces its oldest pellet, so rotten food can never lock the pantry out.
bool Habitat::dropPellet(const HabitatRules&, Rng& rng, uint32_t tick, SenseOut& out) {
  if (pantry == 0) return false;
  const int slot = freeSlot(pellets, tick);
  DishPos at{};
  do {
    at = DishPos{kDropRadius * (rng.unit() + rng.unit() - Fx::one()), kDropRadius * (rng.unit() + rng.unit() - Fx::one())};
  } while (at.x * at.x + at.y * at.y > kDropRadius * kDropRadius);
  pellets[slot] = Pellet{at, tick, true};
  --pantry;
  out.fire(stim::pellet_dropped);
  return true;
}

void Habitat::placeFound(DishPos at, uint32_t tick) {
  DishPos normal{};
  keepInside(at, normal);
  pellets[freeSlot(pellets, tick)] = Pellet{at, tick, true};
}

uint8_t Habitat::give(const HabitatRules& rules, uint8_t n) {
  const uint8_t room = pantry >= rules.pantrySize ? 0 : uint8_t(rules.pantrySize - pantry);
  const uint8_t got = n < room ? n : room;
  pantry = uint8_t(pantry + got);
  return got;
}

uint8_t Habitat::take(uint8_t n) {
  const uint8_t lost = n < pantry ? n : pantry;
  pantry = uint8_t(pantry - lost);
  return lost;
}

uint32_t Habitat::refillLeft(const HabitatRules& rules, uint32_t tick) const {
  if (pantry >= rules.pantrySize) return 0;
  const uint32_t since = tick - pantryTick;
  return since >= rules.refillTicks ? 1 : rules.refillTicks - since;
}

std::optional<Habitat::Bite> Habitat::bite(const HabitatRules& rules, DishPos creature, uint32_t tick, bool rotIsFood) {
  int i = nearestIndex(pellets, creature, rotIsFood ? Kind::Any : Kind::Fresh, &rules, tick);
  if (i < 0 || dist2(pellets[i].at, creature) > kBiteRange * kBiteRange) return std::nullopt;
  pellets[i].present = false;
  return Bite{rules.biteSize, rotten(pellets[i], rules, tick)};
}

std::optional<DishPos> Habitat::nearestPellet(DishPos from) const {
  int i = nearestIndex(pellets, from);
  if (i < 0) return std::nullopt;
  return pellets[i].at;
}

std::optional<DishPos> Habitat::nearestFood(const HabitatRules& rules, DishPos from, uint32_t tick, bool rotIsFood) const {
  int i = nearestIndex(pellets, from, rotIsFood ? Kind::Any : Kind::Fresh, &rules, tick);
  if (i < 0) return std::nullopt;
  return pellets[i].at;
}

std::optional<DishPos> Habitat::nearestRotten(const HabitatRules& rules, DishPos from, uint32_t tick) const {
  int i = nearestIndex(pellets, from, Kind::Rotten, &rules, tick);
  if (i < 0) return std::nullopt;
  return pellets[i].at;
}

uint32_t Habitat::hash() const {
  uint32_t h = fnv1a(nullptr, 0);
  auto mix = [&h](uint32_t v) { h = fnv1a(&v, sizeof v, h); };
  for (const Pellet& p : pellets) {
    mix(uint32_t(p.at.x.raw));
    mix(uint32_t(p.at.y.raw));
    mix(p.droppedTick);
    mix(p.present ? 1u : 0u);
  }
  mix(pantry);
  mix(pantryTick);
  return h;
}

}  // namespace blorb
