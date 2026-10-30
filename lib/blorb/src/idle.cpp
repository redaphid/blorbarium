#include "blorb/idle.h"

namespace blorb {
namespace {

constexpr uint32_t kTicksPerSecond = 1000 / kTickMs;

// Each species' favourites, 0 (never) to 9, in idles.def's order, from its
// personality (personalities/*.txt):
//   explore chase_tail dance juggle dig stretch look_around peek sing
//   shadow_box sniff tidy wave roll_over daydream stargaze toy sneeze
//   discover boo hide hiccup zoomies |
//   wash sunbathe fireflies exercise yawn tummy beg nod_off nurse queasy
//   mope beam tumble reminisce  (#61)
constexpr uint8_t kAffinity[][kIdleCount] = {
    {3, 1, 1, 2, 2, 4, 6, 3, 5, 2, 4, 2, 1, 1, 7, 8, 2, 1, 4, 2, 6, 5, 1, 3, 3, 6, 1, 4, 5, 3, 4, 4, 4, 5, 3, 3, 7},   // grungo: a hooded seer: muses, looks about, croaks, hides in his hood
    {4, 0, 1, 1, 2, 5, 4, 4, 4, 0, 6, 8, 3, 1, 7, 4, 2, 1, 6, 1, 7, 2, 0, 3, 9, 2, 1, 6, 4, 3, 6, 4, 4, 3, 4, 2, 8},   // mossby: no rush: tidies his house, sniffs, dreams, into his shell
    {3, 6, 8, 5, 2, 3, 3, 6, 5, 3, 3, 1, 6, 8, 2, 2, 6, 9, 4, 6, 3, 6, 7, 5, 5, 7, 4, 4, 6, 6, 5, 5, 5, 4, 9, 9, 4},   // puffkin: bounces, rolls, sneezes sparkles, zooms
    {3, 4, 7, 3, 2, 4, 3, 7, 6, 2, 3, 3, 9, 5, 4, 3, 5, 1, 4, 5, 3, 4, 5, 6, 3, 4, 3, 3, 4, 7, 4, 4, 4, 5, 7, 5, 4},   // gilly: waves, dances, comes to say hi
    {5, 2, 4, 1, 0, 4, 5, 4, 9, 1, 2, 6, 3, 1, 8, 9, 2, 1, 5, 2, 4, 2, 2, 6, 1, 9, 1, 5, 4, 3, 5, 4, 4, 5, 4, 3, 7},   // mothwick: hums, dreams, the stars
    {9, 8, 4, 3, 5, 6, 3, 3, 1, 7, 3, 1, 3, 4, 1, 1, 7, 1, 3, 3, 1, 2, 9, 2, 4, 6, 9, 3, 6, 4, 3, 5, 4, 3, 5, 6, 3},   // mudge: laps of the dish, warm-ups, racing his tail
    {4, 3, 6, 7, 7, 2, 5, 7, 2, 4, 4, 4, 3, 3, 3, 4, 6, 1, 7, 5, 3, 3, 6, 3, 5, 7, 5, 3, 5, 5, 4, 4, 5, 3, 6, 6, 4},   // fizz: juggles shinies, digs a hoard, shows off, finds things
    {2, 0, 1, 1, 1, 5, 9, 6, 2, 2, 2, 3, 1, 0, 6, 9, 1, 1, 5, 4, 2, 1, 0, 6, 1, 5, 1, 7, 4, 2, 7, 4, 4, 4, 2, 2, 8},   // hushwing: an owl's look about, a stare, the stars, a silent boo
    {4, 3, 3, 7, 8, 3, 5, 4, 2, 3, 7, 7, 3, 3, 4, 4, 8, 1, 8, 6, 5, 2, 4, 2, 3, 5, 3, 3, 5, 5, 4, 5, 4, 3, 4, 5, 5},   // tinker: digs for scrap, finds things, mischief
    {3, 2, 6, 8, 2, 3, 4, 6, 5, 9, 2, 2, 5, 3, 5, 6, 4, 1, 4, 7, 6, 2, 2, 5, 3, 5, 6, 4, 5, 6, 4, 5, 5, 6, 6, 5, 6},   // octopus: eight arms juggle, the duel rehearsed, theatre
    {7, 6, 2, 4, 3, 5, 8, 8, 2, 8, 6, 1, 2, 6, 2, 4, 9, 2, 7, 8, 7, 2, 9, 5, 7, 7, 6, 3, 5, 9, 2, 3, 2, 3, 6, 6, 2},   // puck: zoomies, ballies, ambushes, begs, never naps
};
static_assert(countOf(kAffinity) >= kSpeciesCount, "a row of favourites per species.def row");

// Of 1000: how often a rest or wander he picks becomes play.
constexpr uint16_t kLively[] = {350, 250, 600, 500, 400, 650, 550, 250, 500, 450, 650};
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
    case IdleFamily::Feel: p = 0; break;   // feels(): only what presses
  }
  return p < 100 && f != IdleFamily::Night && f != IdleFamily::Feel ? 100 : p;
}

// The part of his day each leans to (#61), in tenths (10 = as ever), in
// DayPart's order: Waking, Morning, Midday, Afternoon, Evening, Night.
constexpr uint8_t kWhen[][kDayPartCount] = {
    { 4, 10, 13, 11,  8,  6},   // explore
    { 3, 10, 13, 11,  8,  5},   // chase_tail
    { 3, 11, 12, 11,  9,  5},   // dance
    { 3,  9, 12, 13,  9,  5},   // juggle
    { 2, 10, 14, 11,  8,  4},   // dig
    {30, 18,  8,  9, 10,  6},   // stretch: on waking
    { 8, 10, 10, 10, 11, 12},   // look_around
    { 6, 13, 10, 10, 10,  8},   // peek: good morning
    { 6, 20,  9,  9, 14,  8},   // sing: a dawn song, a lullaby
    { 2, 12, 12, 11,  7,  4},   // shadow_box
    { 4, 11, 11, 12,  9,  7},   // sniff
    { 4, 18, 10,  9,  8,  4},   // tidy: the morning's chores
    { 8, 15, 10, 10, 10,  6},   // wave
    { 3, 10, 12, 12,  9,  5},   // roll_over
    { 6,  7,  9, 18, 13, 10},   // daydream: the afternoon's
    {10, 10, 10, 10, 10, 10},   // stargaze (only in the dark, its family)
    { 3, 10, 14, 12,  8,  5},   // toy
    {12, 10, 10, 10, 10, 10},   // sneeze
    { 4, 10, 12, 15,  9,  6},   // discover
    { 3, 10, 10, 12, 12, 10},   // boo
    { 5,  8, 10, 10, 12, 12},   // hide
    {10, 10, 10, 10, 10, 10},   // hiccup
    { 2, 10, 15, 11, 10,  6},   // zoomies
    {25, 25,  4,  4, 15,  6},   // wash: on waking and of a morning, again at evening
    { 0,  0, 30, 20,  0,  0},   // sunbathe: the middle of the day (never in the dark)
    { 0,  0,  0,  0, 30, 25},   // fireflies: dusk and the dark
    { 6, 30,  4,  0,  0,  0},   // exercise: mornings
    {35,  8,  4,  8, 25, 15},   // yawn: just up, or winding down
    {10, 10, 10, 10, 10, 10},   // tummy
    {10, 10, 10, 10, 10, 10},   // beg
    {10, 10, 10, 10, 10, 10},   // nod_off
    {10, 10, 10, 10, 10, 10},   // nurse
    {10, 10, 10, 10, 10, 10},   // queasy
    { 6, 10, 10, 10, 12, 10},   // mope
    { 8, 12, 12, 10, 10,  6},   // beam
    { 4, 10, 12, 12,  8,  4},   // tumble
    { 4,  6,  8, 14, 18, 10},   // reminisce: an old one's afternoon and evening
};
static_assert(countOf(kWhen) == kIdleCount, "a row of kWhen per idles.def row");

// His age (#61), in tenths, by family (Romp, Nose, Show, Muse; Night and Feel
// as ever), in Stage's order: a baby romps, an elder muses.
constexpr uint8_t kAge[4][4] = {
    {14, 11, 12,  7},   // baby
    {13, 11, 11,  8},   // child
    {10, 10, 10, 10},   // adult
    { 5, 10,  9, 14},   // elder
};

// Past `mark`, how far, in permille of the rest of the way to 1.
int32_t past(Fx v, Fx mark) {
  if (v <= mark) return 0;
  return int32_t((int64_t(v.raw - mark.raw) * 1000) / (Fx::kOne - mark.raw));
}
int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

// The rest of what moves one act's weight (#61), in permille (1000 = as
// ever): how he feels for the few that show it, his age, his genes.
int32_t lean(IdleId id, const IdleMood& m) {
  const IdleInfo& row = IDLES[id.v];
  int32_t w = 1000;
  const uint8_t stage = uint8_t(m.stage) & 3;
  if (row.family <= IdleFamily::Muse) w = w * kAge[stage][uint8_t(row.family)] / 10;
  // His size gene: a big one romps less and muses more, a small one the other way.
  const int32_t bulk = int32_t(m.bulk) - 128;
  if (row.family == IdleFamily::Romp) w = w * (384 - bulk) / 384;
  if (row.family == IdleFamily::Muse) w = w * (384 + bulk) / 384;
  // His temperament's dreams: a long dreamer muses more.
  if (row.family == IdleFamily::Muse) w = w * (120 + clampi(m.dreamLenTicks, 0, 240)) / 150;
  // What his genes make him (traits.h; 100 is one doubling of a gene's rate):
  // a playful one romps more, an affectionate one shows off for the kid and
  // begs more, a sleepy one muses more, a night owl takes to the evening and
  // the dark, a cheerful one beams, a nervous one hides and looks about, a
  // bold one boos, boxes and zooms. Curiosity is explore's, in familyPull.
  const Traits& t = m.traits;
  auto by = [](int32_t v, int32_t trait, int32_t per) { return v * clampi(1000 + trait * 1000 / per, 400, 2500) / 1000; };
  if (row.family == IdleFamily::Romp) w = by(w, t.playfulness, 200);
  if (row.family == IdleFamily::Show || id == idles::beg) w = by(w, t.affection, 200);
  if (row.family == IdleFamily::Muse) w = by(w, t.sleepiness, 200);
  if ((m.part == DayPart::Evening || m.part == DayPart::Night) &&
      (row.family == IdleFamily::Night || id == idles::fireflies || id == idles::sing))
    w = by(w, t.nightOwl, 200);
  if (id == idles::beam) w = by(w, t.cheer, 150);
  if (id == idles::hide || id == idles::look_around) w = by(w, t.nerves - t.boldness / 2, 200);
  if (id == idles::boo || id == idles::shadow_box || id == idles::zoomies) w = by(w, t.boldness - t.nerves / 2, 200);
  if (id == idles::sunbathe && m.dark) return 0;
  if (id == idles::tumble && m.stage >= Stage::Adult) return 0;     // a little one's
  if (id == idles::reminisce && m.stage != Stage::Elder) return 0;  // an old one's
  if (id == idles::zoomies && m.stage == Stage::Elder) w = w * 3 / 10;
  if (id == idles::mope) {   // lonely or bored a lot
    const Fx most = fxMax(fxMax(m.loneliness, m.needTouch), m.boredom);
    const int32_t by = past(most, Fx::ratio(55, 100));
    if (!by) return 0;
    w = w * (500 + 4 * by) / 1000;
  }
  if (id == idles::beam) {   // all's well
    const int32_t by = past(m.content, Fx::ratio(6, 10));
    if (!by) return 0;
    w = w * (300 + 2 * by) / 1000;
  }
  if (id == idles::yawn && m.part != DayPart::Waking && m.part != DayPart::Evening && m.sleepiness < Fx::ratio(3, 10))
    w /= 6;
  return w;
}

constexpr const char* kPartNames[kDayPartCount] = {"waking", "morning", "midday", "afternoon", "evening", "night"};

// The wall's hours, as the day loci hold them (senses.cpp DayDetector), 1/1000s.
constexpr int16_t kHourSin[24] = {0,    259,  500,  707,  866,  966,  1000, 966,  866,  707,  500,  259,
                                  0,    -259, -500, -707, -866, -966, -1000, -966, -866, -707, -500, -259};

}  // namespace

Fx idleLiveliness(const IdleMood& m) {
  const uint16_t base = kLively[m.species.v < kSpeciesCount ? m.species.v : 0];
  // A playful one (traits.h) plays the more often, a sleepy one the less.
  const int32_t genes = clampi(int32_t(m.traits.playfulness) - m.traits.sleepiness / 2, -300, 300);
  return clamp01(Fx::ratio(clampi(int32_t(base) + genes / 2, 50, 950), 1000) + m.boredom * Fx::ratio(3, 10));
}

const char* dayPartName(DayPart p) { return uint8_t(p) < kDayPartCount ? kPartNames[uint8_t(p)] : "?"; }

DayPart dayPartOf(const DayCues& c) {
  constexpr uint32_t kHour = 60u * kTicksPerMinute;
  if (c.dark) return DayPart::Night;
  if (c.upTicks < 10 * kTicksPerMinute) return DayPart::Waking;
  if (c.hour >= 0) {
    if (c.hour >= 5 && c.hour < 10) return DayPart::Morning;
    if (c.hour >= 10 && c.hour < 14) return DayPart::Midday;
    if (c.hour >= 14 && c.hour < 18) return DayPart::Afternoon;
    if (c.hour >= 18 && c.hour < 22) return DayPart::Evening;
    return DayPart::Night;
  }
  // No clock: his own day, from when he woke. Up a long while and sleepy is his evening.
  if (c.dayTicks >= 3 * kHour && c.sleepiness >= Fx::ratio(35, 100)) return DayPart::Evening;
  if (c.dayTicks < 2 * kHour) return DayPart::Morning;
  if (c.dayTicks < 5 * kHour) return DayPart::Midday;
  if (c.dayTicks < 9 * kHour) return DayPart::Afternoon;
  return DayPart::Evening;
}

int8_t hourOfDayLoci(Fx daySin, Fx dayCos) {
  // Back from 0..1 to -1000..1000; the middle of both (no clock) is no hour.
  const int32_t s = int32_t((int64_t(daySin.raw) * 2000) >> Fx::kFrac) - 1000;
  const int32_t c = int32_t((int64_t(dayCos.raw) * 2000) >> Fx::kFrac) - 1000;
  if (s * s + c * c < 250000) return -1;
  int8_t best = 0;
  int64_t bestDot = INT64_MIN;
  for (int8_t h = 0; h < 24; ++h) {
    const int64_t dot = int64_t(s) * kHourSin[h] + int64_t(c) * kHourSin[(h + 6) % 24];
    if (dot > bestDot) bestDot = dot, best = h;
  }
  return best;
}

int32_t feels(IdleId id, const IdleMood& m) {
  const Fx hurt = fxMax(fxMax(m.pain, m.discomfort), m.injury);
  switch (id.v) {
    case idles::tummy.v: return m.hunger >= Fx::ratio(5, 10) ? 300 + 4 * past(m.hunger, Fx::ratio(5, 10)) : 0;
    case idles::beg.v:
      return m.hunger >= Fx::ratio(5, 10) ? (200 + 3 * past(m.hunger, Fx::ratio(5, 10))) *
                                                (1000 + permille(fxMax(m.loneliness, m.needTouch))) / 1000
                                          : 0;
    case idles::nod_off.v: return m.sleepiness >= Fx::ratio(4, 10) ? 400 + 4 * past(m.sleepiness, Fx::ratio(4, 10)) : 0;
    case idles::yawn.v: return m.sleepiness >= Fx::ratio(35, 100) ? 300 + 2 * past(m.sleepiness, Fx::ratio(35, 100)) : 0;
    case idles::nurse.v: return hurt >= Fx::ratio(25, 100) ? 400 + 3 * past(hurt, Fx::ratio(25, 100)) : 0;
    case idles::queasy.v: return m.toxin >= Fx::ratio(5, 100) ? 500 + 3 * permille(m.toxin) : 0;
    default: return 0;
  }
}

uint8_t idlePace(const IdleMood& m) {
  int32_t p = 100;
  static constexpr uint8_t kStagePace[4] = {115, 110, 100, 80};
  p = p * kStagePace[uint8_t(m.stage) & 3] / 100;
  p = p * (512 - (int32_t(m.bulk) - 128)) / 512;   // a big one slower, a small one quicker
  p = p * clampi(1000 + m.traits.playfulness / 2 - m.traits.sleepiness / 2, 700, 1300) / 1000;   // playful brisk, sleepy slow
  p = p * (1000 - 7 * past(m.sleepiness, Fx::ratio(3, 10)) / 10) / 1000;
  p = p * (1000 - 5 * past(fxMax(fxMax(m.pain, m.discomfort), m.injury), Fx::ratio(2, 10)) / 10) / 1000;
  p = p * (1000 - permille(m.toxin) / 2) / 1000;
  if (m.hunger >= Fx::ratio(6, 10)) p = p * 9 / 10;
  p = p * (1000 + past(m.content, Fx::ratio(6, 10)) / 8) / 1000;
  return uint8_t(clampi(p, 55, 140));
}

IdleId pickIdle(const IdleState& s, const IdleMood& m, Rng& rng) {
  const uint8_t* fav = kAffinity[m.species.v < kSpeciesCount ? m.species.v : 0];
  const IdleFamily lastFamily = s.last.v < kIdleCount ? IDLES[s.last.v].family : IdleFamily::Night;
  // His temperament's habituation: one who tires of a thing sooner waits longer
  // to do it again and leaves a family he just played the more.
  const int32_t hab = permille(m.habituation);
  const uint32_t coolPct = uint32_t(clampi(57 + hab / 10, 40, 160));
  const int32_t samePct = clampi(100 - 115 * hab / 1000, 10, 100);
  // What he feels most, of the acts that show it: the more he feels it, the
  // less he plays at anything else.
  int32_t feelMost = 0;
  for (size_t i = 0; i < kIdleCount; ++i) feelMost = feels(IDLES[i].id, m) > feelMost ? feels(IDLES[i].id, m) : feelMost;
  const int32_t damp = 1000 - clampi(feelMost, 0, 800);
  uint32_t weight[kIdleCount]{}, total = 0;
  for (size_t i = 0; i < kIdleCount; ++i) {
    const IdleInfo& row = IDLES[i];
    if (row.id == s.last || !fav[i]) continue;
    // A greeting: something for the kid (a Show idle, a dance), whatever its cooldown.
    if (m.greeting && row.family != IdleFamily::Show && row.id != idles::dance) continue;
    const int32_t feel = feels(row.id, m);
    // Pressed, only what shows how he feels; a Feel act only while he feels it.
    if ((m.pressed || row.family == IdleFamily::Feel) && !feel) continue;
    const uint8_t when = kWhen[i][uint8_t(m.part)];
    if (!when) continue;   // not at this part of his day
    const int32_t lent = lean(row.id, m);
    if (!lent) continue;
    const int32_t pull = m.pressed || row.family == IdleFamily::Feel ? feel : familyPull(row.family, m);
    // How keen he is on it now, permille: its part of the day, his age and
    // genes, his drives, how he feels. A keen act comes round again sooner,
    // so the day, the feeling and the genes show even when he plays all the
    // time and every act takes its turn.
    const int32_t keen = row.family == IdleFamily::Feel || feel
                             ? 1000 + 2 * feel
                             : int32_t(int64_t(when) * 100 * lent / 1000 * pull / 700);
    const uint32_t cooldown =
        uint32_t(uint64_t(row.cooldownS) * kTicksPerSecond * coolPct / 100 * 1000 / uint32_t(clampi(keen, 350, 4000)));
    if (!m.greeting && s.startedAt[i] && m.age + 1 - s.startedAt[i] < cooldown) continue;
    // His own taste: each favourite 0.75x to 1.25x his species', from his genome.
    const uint32_t mine = 75 + mix(m.taste, uint32_t(i)) % 51;
    uint32_t w = uint32_t(fav[i]) * mine * uint32_t(pull) / 100;
    w = uint32_t(uint64_t(w) * when / 10);
    w = uint32_t(uint64_t(w) * uint32_t(lent) / 1000);
    if (row.family != IdleFamily::Feel && !feel) w = uint32_t(uint64_t(w) * uint32_t(damp) / 1000);
    if (row.family == lastFamily) w = w * uint32_t(samePct) / 100;   // a romp, then something else
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

void beginIdle(IdleState& s, IdleId id, DishPos at, uint32_t age, Rng& rng, uint8_t pace) {
  const IdleInfo* row = idleInfo(id);
  if (!row) return;
  const uint32_t base = uint32_t(row->seconds) * kTicksPerSecond;
  s.now = id;
  s.ticks = 0;
  s.pace = pace ? pace : 100;
  // A slow pace plays the act the longer, so it shows all of itself.
  s.length = uint16_t(base * (80 + rng.below(51)) / s.pace);
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
  if (id == idles::yawn) return at < 20 ? expr::sleepy : at < 75 ? expr::yawn : expr::neutral;          // the great yawn
  if (id == idles::nod_off) return at % 33 < 26 ? expr::sleepy : expr::alarmed;                         // drooping; up with a start
  if (id == idles::wash) return at < 85 ? expr::sleepy : expr::happy;                                  // eyes shut while he scrubs
  if (id == idles::tumble) return at < 30 ? expr::happy : at < 55 ? expr::alarmed : expr::happy;       // whoops, and up
  if (id == idles::exercise && at >= 85) return expr::happy;                                            // done!
  if (id == idles::sunbathe && at >= 15 && at < 90) return expr::sleepy;                                // basking, eyes half shut
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
    case IdleFamily::Feel: break;   // it shows how he feels; it mends nothing
  }
  if (id == idles::stretch) out[n++] = {drive::sleepiness, per(-20)};
  return n;
}

}  // namespace blorb
