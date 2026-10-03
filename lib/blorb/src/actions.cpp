#include "blorb/actions.h"

namespace blorb {
namespace {

// Behaviours keep no state of their own: Behaviours lives in the Dish and is
// never saved, so everything a behaviour needs is in Body or the habitat.
constexpr Fx kWalk = Fx::ratio(15, 1000);     // per tick: across the dish in about 13 s
constexpr Fx kAmble = Fx::ratio(6, 1000);
constexpr Fx kRoam = Fx::ratio(85, 100);      // he stays this far in, short of the rim
constexpr Fx kNearRim = Fx::ratio(78, 100);   // Curl stops here
constexpr Fx kWanderTurn = Fx::ratio(1, 10);  // most a wander heading changes per tick, in half turns
constexpr Fx kHopTurn = Fx::ratio(1, 16);
constexpr uint16_t kChewTicks = 15;            // he holds the bite this long, so eating can be seen
constexpr Fx kNoseRange = Fx::ratio(12, 100);  // the marble this close, he noses it on ...
constexpr Fx kNoseSpeed = Fx::ratio(3, 100);   // ... at twice his walk: it rolls about half the dish
constexpr Fx kTiltDeadZone = Fx::ratio(3, 100);   // the habitat's: a dish on a desk is level
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
Fx absFx(Fx v) { return v < Fx::zero() ? -v : v; }

// Facing is in half turns, -1..1 = -pi..pi.
Fx wrapTurn(Fx a) {
  int64_t two = int64_t(2) * Fx::kOne;
  int64_t r = ((int64_t(a.raw) + Fx::kOne) % two + two) % two - Fx::kOne;
  return Fx{int32_t(r)};
}

// sin(pi x) by Bhaskara I's approximation, error under 0.002.
Fx sinTurn(Fx x) {
  x = wrapTurn(x);
  bool negative = x < Fx::zero();
  x = absFx(x);
  Fx p = x * (Fx::one() - x);
  Fx s = fxDiv(p * Fx{16 * Fx::kOne}, Fx{5 * Fx::kOne} - p * Fx{4 * Fx::kOne});
  return negative ? -s : s;
}
Fx cosTurn(Fx x) { return sinTurn(x + kHalf); }

// atan2(dy, dx) / pi, error under 0.002 half turns.
Fx heading(Fx dx, Fx dy) {
  Fx ax = absFx(dx), ay = absFx(dy);
  Fx z = ax >= ay ? fxDiv(ay, ax) : fxDiv(ax, ay);
  Fx a = z * (Fx::ratio(1, 4) + Fx::ratio(869, 10000) * (Fx::one() - z));
  if (ay > ax) a = kHalf - a;
  if (dx < Fx::zero()) a = Fx::one() - a;
  return dy < Fx::zero() ? -a : a;
}

// True if it pulled him back from the rim.
bool keepInside(DishPos& p) {
  Fx r2 = p.x * p.x + p.y * p.y;
  if (r2 <= kRoam * kRoam) return false;
  Fx r = fxSqrt(r2);
  p = DishPos{fxDiv(p.x, r) * kRoam, fxDiv(p.y, r) * kRoam};
  return true;
}

// Walks toward `to` at `speed`, facing it. True once there.
bool walkTo(Body& b, DishPos to, Fx speed) {
  Fx dx = to.x - b.at.x, dy = to.y - b.at.y;
  Fx dist = fxSqrt(dx * dx + dy * dy);
  if (dist == Fx::zero()) return true;
  b.facing = heading(dx, dy);
  if (dist <= speed) {
    b.at = to;
  } else {
    b.at = DishPos{b.at.x + fxDiv(dx, dist) * speed, b.at.y + fxDiv(dy, dist) * speed};
  }
  keepInside(b.at);
  return dist <= speed;
}

// Walks along his facing; turns him round at the rim.
void walkOn(Body& b, Fx speed) {
  b.at = DishPos{b.at.x + cosTurn(b.facing) * speed, b.at.y + sinTurn(b.facing) * speed};
  if (keepInside(b.at)) b.facing = wrapTurn(b.facing + Fx::one());
}

Fx downhill(Fx tilt) {
  Fx d = kHalf - tilt;
  return absFx(d) > kTiltDeadZone ? d : Fx::zero();
}

// Tilt above 0.5 raises the + edge, so downhill is toward -x (notes.md).
Status walkWithTilt(Body& b, ActionCtx& c, bool uphill, PoseId moving) {
  Fx dx = downhill(c.tiltX), dy = downhill(c.tiltY);
  if (dx == Fx::zero() && dy == Fx::zero()) {
    b.pose = pose::idle;
    return Status::Running;
  }
  if (uphill) dx = -dx, dy = -dy;
  b.pose = moving;
  walkTo(b, DishPos{b.at.x + dx, b.at.y + dy}, kWalk);
  return Status::Running;
}

}  // namespace

namespace behave {

Status Rest::step(Body& b, ActionCtx&) {
  b.pose = pose::idle;
  return Status::Running;
}

Status Wander::step(Body& b, ActionCtx& c) {
  b.pose = pose::walk;
  b.facing = wrapTurn(b.facing + (c.rng.unit() - kHalf) * (kWanderTurn + kWanderTurn));
  walkOn(b, kAmble);
  return Status::Running;
}

// A fresh Eat walks to food; one decided again mid-chew finishes the mouthful.
void Eat::start(Body& b, ActionCtx&) {
  if (b.pose != pose::eat || b.poseTick + 1u >= kChewTicks) b.pose = pose::walk;
}

// To the nearest pellet and bite it, then chew for kChewTicks with the eating
// locus up; done after the chew, or when there is nothing to eat. The chew
// is timed by the eat pose's poseTick, so the behaviour keeps no state.
Status Eat::step(Body& b, ActionCtx& c) {
  if (b.pose == pose::eat) {
    if (b.poseTick + 1u >= kChewTicks) return Status::Done;
    c.out.set(locus::eating, Fx::one());
    return Status::Running;
  }
  std::optional<DishPos> pellet = c.habitat.nearestPellet(b.at);
  if (!pellet) return Status::Done;
  b.pose = pose::walk;
  walkTo(b, *pellet, kWalk);
  std::optional<Habitat::Bite> bite = c.habitat.bite(c.rules, b.at, c.tick);
  if (!bite) return Status::Running;
  b.pose = pose::eat;
  c.foodEaten += bite->food;
  c.ateRotten = c.ateRotten || bite->rotten;
  c.out.fire(stim::fed);   // a rotten pellet still fills him, so he stops at one
  if (bite->rotten) c.out.fire(stim::fed_bad);
  c.out.set(locus::eating, Fx::one());
  return Status::Running;
}

// The creature ends a sleep (sleep_gate falls, or a stimulus that wakes).
void Sleep::start(Body& b, ActionCtx&) {
  b.asleep = true;
  b.pose = pose::sleep;
}
Status Sleep::step(Body& b, ActionCtx&) {
  b.pose = pose::sleep;
  return Status::Running;
}
void Sleep::stop(Body& b, ActionCtx& c) {
  b.asleep = false;
  b.dreaming = false;
  c.out.fire(stim::woke);
}

void Foresee::start(Body& b, ActionCtx&) { b.pose = pose::foresee; }
Status Foresee::step(Body& b, ActionCtx& c) {
  b.pose = pose::foresee;
  c.out.set(locus::foreseeing, Fx::one());
  return Status::Running;
}
void Foresee::stop(Body&, ActionCtx&) {}

Status Call::step(Body& b, ActionCtx&) {
  b.pose = pose::call;
  return Status::Running;
}

Status Curl::step(Body& b, ActionCtx&) {
  b.pose = pose::curl;
  Fx r = fxSqrt(b.at.x * b.at.x + b.at.y * b.at.y);
  if (r >= kNearRim) return Status::Running;
  if (r == Fx::zero()) walkOn(b, kAmble);
  else walkTo(b, DishPos{fxDiv(b.at.x, r) * kNearRim, fxDiv(b.at.y, r) * kNearRim}, kAmble);
  return Status::Running;
}

Status FollowTilt::step(Body& b, ActionCtx& c) { return walkWithTilt(b, c, false, pose::walk); }
Status FleeTilt::step(Body& b, ActionCtx& c) { return walkWithTilt(b, c, true, pose::flee); }

// Reaching the marble he noses it on the way he is walking, so the chase goes
// on and he never comes to stand on it.
Status Chase::step(Body& b, ActionCtx& c) {
  b.pose = pose::walk;
  Marble& m = c.habitat.marble;
  walkTo(b, m.at, kWalk);
  Fx dx = m.at.x - b.at.x, dy = m.at.y - b.at.y;
  if (dx * dx + dy * dy <= kNoseRange * kNoseRange) {
    m.vx = cosTurn(b.facing) * kNoseSpeed;
    m.vy = sinTurn(b.facing) * kNoseSpeed;
  }
  return Status::Running;
}

Status HopCircles::step(Body& b, ActionCtx&) {
  b.pose = pose::bounce;
  b.facing = wrapTurn(b.facing + kHopTurn);
  walkOn(b, kAmble);
  return Status::Running;
}

}  // namespace behave
}  // namespace blorb
