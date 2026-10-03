#include "blorb/habitat.h"

namespace blorb {
namespace {

constexpr Fx kDropRadius = Fx::ratio(6, 10);
constexpr Fx kRimRadius = Fx::ratio(92, 100);       // nothing rolls past this
constexpr Fx kBiteRange = Fx::ratio(12, 100);
constexpr Fx kMarbleHitRange = Fx::ratio(15, 100);
constexpr Fx kSenseRange = Fx::one();                // *_near is 1 on top of it, 0 this far away or more
constexpr Fx kLevelDeadZone = Fx::ratio(3, 100);     // about 3 degrees: a dish on a desk stays put
constexpr Fx kPelletSlide = Fx::ratio(6, 100);       // per tick per unit of tilt; pellets do not coast
constexpr Fx kMarbleAccel = Fx::ratio(1, 100);       // per tick per unit of tilt
constexpr Fx kMarbleFriction = Fx::ratio(1, 16);     // velocity lost per tick
constexpr Fx kRimBounce = Fx::ratio(1, 2);           // share of outward speed returned inward
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

int nearestIndex(const Pellet (&pellets)[Habitat::kMaxPellets], DishPos from) {
  int best = -1;
  Fx bestD2{};
  for (int i = 0; i < Habitat::kMaxPellets; ++i) {
    if (!pellets[i].present) continue;
    Fx d2 = dist2(pellets[i].at, from);
    if (best < 0 || d2 < bestD2) { best = i; bestD2 = d2; }
  }
  return best;
}

}  // namespace

void Habitat::step(const HabitatRules& rules, Fx tiltX, Fx tiltY, DishPos creature, uint32_t tick, SenseOut& out) {
  if (pantry >= rules.pantrySize) {
    pantry = rules.pantrySize;
    pantryTick = tick;
  } else if (tick - pantryTick >= rules.refillTicks) {
    ++pantry;
    pantryTick = tick;
  }

  Fx ax = downhill(tiltX), ay = downhill(tiltY);
  DishPos normal{};
  for (Pellet& p : pellets) {
    if (!p.present) continue;
    p.at = DishPos{p.at.x + ax * kPelletSlide, p.at.y + ay * kPelletSlide};
    keepInside(p.at, normal);
  }

  bool wasClear = dist2(marble.at, creature) > kMarbleHitRange * kMarbleHitRange;
  marble.vx += ax * kMarbleAccel - marble.vx * kMarbleFriction;
  marble.vy += ay * kMarbleAccel - marble.vy * kMarbleFriction;
  marble.at = DishPos{marble.at.x + marble.vx, marble.at.y + marble.vy};
  if (keepInside(marble.at, normal)) {
    Fx outward = marble.vx * normal.x + marble.vy * normal.y;
    if (outward > Fx::zero()) {
      Fx cancel = outward + outward * kRimBounce;
      marble.vx -= normal.x * cancel;
      marble.vy -= normal.y * cancel;
    }
  }
  Fx marbleD2 = dist2(marble.at, creature);
  if (wasClear && marbleD2 <= kMarbleHitRange * kMarbleHitRange) out.fire(stim::marble_hit);

  int near = nearestIndex(pellets, creature);
  out.set(locus::food_near, near < 0 ? Fx::zero() : nearness(dist2(pellets[near].at, creature)));
  out.set(locus::food_rotten, near >= 0 && rotten(pellets[near], rules, tick) ? Fx::one() : Fx::zero());
  out.set(locus::marble_near, nearness(marbleD2));
  out.set(locus::pantry, Fx::ratio(pantry, rules.pantrySize));
}

// A full dish replaces its oldest pellet, so rotten food can never lock the pantry out.
bool Habitat::dropPellet(const HabitatRules&, Rng& rng, uint32_t tick, SenseOut& out) {
  if (pantry == 0) return false;
  int slot = 0;
  for (int i = 0; i < kMaxPellets; ++i) {
    if (!pellets[i].present) { slot = i; break; }
    if (tick - pellets[i].droppedTick > tick - pellets[slot].droppedTick) slot = i;
  }
  DishPos at{};
  do {
    at = DishPos{kDropRadius * (rng.unit() + rng.unit() - Fx::one()), kDropRadius * (rng.unit() + rng.unit() - Fx::one())};
  } while (at.x * at.x + at.y * at.y > kDropRadius * kDropRadius);
  pellets[slot] = Pellet{at, tick, true};
  --pantry;
  out.fire(stim::pellet_dropped);
  return true;
}

std::optional<Habitat::Bite> Habitat::bite(const HabitatRules& rules, DishPos creature, uint32_t tick) {
  int i = nearestIndex(pellets, creature);
  if (i < 0 || dist2(pellets[i].at, creature) > kBiteRange * kBiteRange) return std::nullopt;
  pellets[i].present = false;
  return Bite{rules.biteSize, rotten(pellets[i], rules, tick)};
}

std::optional<DishPos> Habitat::nearestPellet(DishPos from) const {
  int i = nearestIndex(pellets, from);
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
  mix(uint32_t(marble.at.x.raw));
  mix(uint32_t(marble.at.y.raw));
  mix(uint32_t(marble.vx.raw));
  mix(uint32_t(marble.vy.raw));
  mix(pantry);
  mix(pantryTick);
  return h;
}

}  // namespace blorb
