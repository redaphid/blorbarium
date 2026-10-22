#include "blorb/idle.h"

namespace blorb {
namespace {

constexpr uint32_t kTicksPerSecond = 1000 / kTickMs;

// Each species' favourites, 0 (never) to 9, in idles.def's order, from its
// personality (personalities/*.txt):
//   explore chase_tail dance juggle dig stretch look_around peek sing
//   shadow_box sniff tidy wave roll_over daydream stargaze toy sneeze
//   discover boo hide hiccup zoomies
constexpr uint8_t kAffinity[][kIdleCount] = {
    {3, 1, 1, 2, 2, 4, 6, 3, 5, 2, 4, 2, 1, 1, 7, 8, 2, 1, 4, 2, 6, 5, 1},   // grungo: a hooded seer: muses, looks about, croaks, hides in his hood
    {4, 0, 1, 1, 2, 5, 4, 4, 4, 0, 6, 8, 3, 1, 7, 4, 2, 1, 6, 1, 7, 2, 0},   // mossby: no rush: tidies his house, sniffs, dreams, into his shell
    {3, 6, 8, 5, 2, 3, 3, 6, 5, 3, 3, 1, 6, 8, 2, 2, 6, 9, 4, 6, 3, 6, 7},   // puffkin: bounces, rolls, sneezes sparkles, zooms
    {3, 4, 7, 3, 2, 4, 3, 7, 6, 2, 3, 3, 9, 5, 4, 3, 5, 1, 4, 5, 3, 4, 5},   // gilly: waves, dances, comes to say hi
    {5, 2, 4, 1, 0, 4, 5, 4, 9, 1, 2, 6, 3, 1, 8, 9, 2, 1, 5, 2, 4, 2, 2},   // mothwick: hums, dreams, the stars
    {9, 8, 4, 3, 5, 6, 3, 3, 1, 7, 3, 1, 3, 4, 1, 1, 7, 1, 3, 3, 1, 2, 9},   // mudge: laps of the dish, warm-ups, racing his tail
    {4, 3, 6, 7, 7, 2, 5, 7, 2, 4, 4, 4, 3, 3, 3, 4, 6, 1, 7, 5, 3, 3, 6},   // fizz: juggles shinies, digs a hoard, shows off, finds things
    {2, 0, 1, 1, 1, 5, 9, 6, 2, 2, 2, 3, 1, 0, 6, 9, 1, 1, 5, 4, 2, 1, 0},   // hushwing: an owl's look about, a stare, the stars, a silent boo
    {4, 3, 3, 7, 8, 3, 5, 4, 2, 3, 7, 7, 3, 3, 4, 4, 8, 1, 8, 6, 5, 2, 4},   // tinker: digs for scrap, finds things, mischief
    {3, 2, 6, 8, 2, 3, 4, 6, 5, 9, 2, 2, 5, 3, 5, 6, 4, 1, 4, 7, 6, 2, 2},   // octopus: eight arms juggle, the duel rehearsed, theatre
};
static_assert(countOf(kAffinity) >= kSpeciesCount, "a row of favourites per species.def row");

// Of 1000: how often a rest or wander he picks becomes play.
constexpr uint16_t kLively[] = {350, 250, 600, 500, 400, 650, 550, 250, 500, 450};
static_assert(countOf(kLively) >= kSpeciesCount, "a liveliness per species.def row");

uint32_t mix(uint32_t a, uint32_t b) {
  uint32_t h = a ^ (b * 0x9E3779B1u);
  h ^= h >> 16, h *= 0x85EBCA6Bu, h ^= h >> 13, h *= 0xC2B2AE35u, h ^= h >> 16;
  return h;
}

int32_t permille(Fx v) { return int32_t((int64_t(v.raw) * 1000) >> Fx::kFrac); }

// How much his drives call for a family now, in permille.
int32_t familyPull(IdleFamily f, const IdleMood& m) {
  int32_t p = 0;
  switch (f) {
    case IdleFamily::Romp: p = 600 + 14 * permille(m.boredom) / 10 - 8 * permille(m.sleepiness) / 10; break;
    case IdleFamily::Nose: p = 600 + 9 * permille(m.explore) / 10 + 6 * permille(m.boredom) / 10; break;
    case IdleFamily::Show: p = 500 + 15 * permille(m.loneliness) / 10 + 8 * permille(m.needTouch) / 10; break;
    case IdleFamily::Muse: p = 700 + 9 * permille(m.sleepiness) / 10; break;
    case IdleFamily::Night: p = m.dark ? 1500 : 0; break;
  }
  return p < 100 && f != IdleFamily::Night ? 100 : p;
}

}  // namespace

Fx idleLiveliness(const IdleMood& m) {
  const uint16_t base = kLively[m.species.v < kSpeciesCount ? m.species.v : 0];
  return clamp01(Fx::ratio(base, 1000) + m.boredom * Fx::ratio(3, 10));
}

IdleId pickIdle(const IdleState& s, const IdleMood& m, Rng& rng) {
  const uint8_t* fav = kAffinity[m.species.v < kSpeciesCount ? m.species.v : 0];
  const IdleFamily lastFamily = s.last.v < kIdleCount ? IDLES[s.last.v].family : IdleFamily::Night;
  uint32_t weight[kIdleCount]{}, total = 0;
  for (size_t i = 0; i < kIdleCount; ++i) {
    const IdleInfo& row = IDLES[i];
    if (row.id == s.last || !fav[i]) continue;
    // A greeting: something for the kid (a Show idle, a dance), whatever its cooldown.
    if (m.greeting && row.family != IdleFamily::Show && row.id != idles::dance) continue;
    const uint32_t cooldown = uint32_t(row.cooldownS) * kTicksPerSecond;
    if (!m.greeting && s.startedAt[i] && m.age + 1 - s.startedAt[i] < cooldown) continue;
    // His own taste: each favourite 0.75x to 1.25x his species', from his genome.
    const uint32_t mine = 75 + mix(m.genomeHash, uint32_t(i)) % 51;
    uint32_t w = uint32_t(fav[i]) * mine * uint32_t(familyPull(row.family, m)) / 100;
    if (row.family == lastFamily) w /= 2;   // a romp, then something else
    weight[i] = w;
    total += w;
  }
  if (!total) return idles::none;
  uint32_t r = rng.below(total);
  for (size_t i = 0; i < kIdleCount; ++i) {
    if (r < weight[i]) return IDLES[i].id;
    r -= weight[i];
  }
  return idles::none;
}

void beginIdle(IdleState& s, IdleId id, DishPos at, uint32_t age, Rng& rng) {
  const IdleInfo* row = idleInfo(id);
  if (!row) return;
  const uint32_t base = uint32_t(row->seconds) * kTicksPerSecond;
  s.now = id;
  s.ticks = 0;
  s.length = uint16_t(base * (80 + rng.below(51)) / 100);
  s.last = id;
  s.anchor = at;
  s.startedAt[id.v] = age + 1;
}

ExprId idleFace(IdleId id, uint16_t ticks, uint16_t length) {
  const IdleInfo* row = idleInfo(id);
  if (!row) return expr::neutral;
  const uint32_t at = length ? uint32_t(ticks) * 100 / length : 0;   // percent through it
  if (id == idles::stretch) return at < 55 ? expr::happy : expr::yawn;
  if (id == idles::sneeze) return at < 45 ? expr::sleepy : at < 65 ? expr::alarmed : expr::happy;
  if (id == idles::look_around && at >= 45 && at < 60) return expr::alarmed;   // what was that?
  if (id == idles::daydream) return at < 20 ? expr::neutral : expr::happy;
  if (id == idles::dig && at >= 80) return expr::happy;                          // found something
  if (id == idles::discover) return at < 25 ? expr::neutral : at < 45 ? expr::alarmed : expr::happy;   // !, then delight
  if (id == idles::boo) return at < 55 ? expr::blep : at < 75 ? expr::alarmed : expr::happy;          // sneaking, BOO, giggling
  if (id == idles::hide) return at < 70 ? expr::sleepy : expr::happy;                                 // eyes squeezed shut, then out
  if (id == idles::hiccup) return at % 34 < 12 ? expr::alarmed : expr::neutral;                         // hic, and again
  return row->face;
}

// A whole act's worth, spread over its ticks. A romp eases boredom most and
// tires him a little (so a nap follows play); showing off for the kid eases
// loneliness; nosing about and musing a little boredom; a stretch wakes him.
uint8_t idleEffects(IdleId id, uint16_t length, IdleEffect (&out)[3]) {
  const IdleInfo* row = idleInfo(id);
  if (!row || !length) return 0;
  auto per = [length](int32_t permilleWhole) { return Fx{int32_t((int64_t(Fx::kOne) * permilleWhole / 1000) / length)}; };
  uint8_t n = 0;
  switch (row->family) {
    case IdleFamily::Romp:
      out[n++] = {drive::boredom, per(-180)};
      out[n++] = {drive::sleepiness, per(3)};
      break;
    case IdleFamily::Nose: out[n++] = {drive::boredom, per(-120)}; break;
    case IdleFamily::Show:
      out[n++] = {drive::loneliness, per(-120)};
      out[n++] = {drive::boredom, per(-60)};
      break;
    case IdleFamily::Muse:
    case IdleFamily::Night: out[n++] = {drive::boredom, per(-60)}; break;
  }
  if (id == idles::stretch) out[n++] = {drive::sleepiness, per(-20)};
  return n;
}

}  // namespace blorb
