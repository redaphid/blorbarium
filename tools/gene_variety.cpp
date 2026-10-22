// Do the genes show? (#60) A native harness over the real engine and renderer:
//
//   gene_variety behaviour <out> [hours] [species,...|all] [threads]
//       founds each species' pet, then the same pet with one gene byte pushed
//       to each end of its range (every other byte held), plus genomes with every
//       axis rolled at random, and lives each through the Dish for `hours` of a
//       kid's day (a phone's TIME at 07:00, a look every 4 to 25 minutes, a shake
//       now and then, nobody at night). One CSV row per run: behaviour.csv.
//   gene_variety looks <out>
//       the same adult with each look byte swept, and a grid of random founders,
//       as 240x240 PPM cells plus looks.csv (what each cell is) for
//       tools/gene_variety_sheets.py to lay out with labels.
//   gene_variety lineage <out> [generations] [lines] [hours] [threads]
//       generations through the REAL egg path: the parent hatched as a Creature,
//       Creature::layClutch (3 eggs), Clutch::child (mutate() with its viability
//       retries), one egg picked at random or as the one that looks most
//       different from its parent. lineage.csv (drift per generation),
//       kinship.csv (siblings against strangers), lineage_behaviour.csv
//       (generations 0, 5, 10 and 20 lived as above) and the strip's cells.
//
// Built by `pio run -e genevariety` (through ~/Worktrees/blorbarium/pio).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "blorb/dish.h"   // first, so PlatformIO's dependency finder links lib/blorb
#include "blorb/appearance.h"
#include "blorb/duel.h"
#include "blorb/duel_card.h"
#include "blorb/founding.h"
#include "blorb/genes.h"
#include "blorb/mutate.h"
#include "blorb/species.h"
#include "links.h"
#include "mem_storage.h"
#include "paint/sprite_pack.h"
#include "time_sources.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

namespace {

// ---- the genome ------------------------------------------------------------------

using Match = bool (*)(const GeneView&);

// The first awake gene of `type` at `stage` that `m` accepts.
std::optional<GeneView> findGene(const Genome& g, uint8_t type, Match m, Stage stage = Stage::Baby, bool dormant = false) {
  std::optional<GeneView> out;
  g.forEach([&](const GeneView& v) {
    if (out || v.header.type != type || v.header.stage != stage || v.header.featGate) return;
    if (bool(v.header.flags & GeneFlags::Dormant) != dormant) return;
    if (m(v)) out = v;
  });
  return out;
}

Genome withByte(const Genome& g, GeneUid uid, uint8_t at, uint8_t value) {
  return *GenomeBuilder::from(g).setByte(uid, at, value).build();
}

constexpr uint8_t kPalette = GeneKindOf<PaletteGene>::value, kMark = GeneKindOf<MarkGene>::value,
                  kSize = GeneKindOf<SizeGene>::value, kEmitter = GeneKindOf<EmitterGene>::value,
                  kReceptor = GeneKindOf<ReceptorGene>::value, kStimulus = GeneKindOf<StimulusGene>::value,
                  kInstinct = GeneKindOf<InstinctGene>::value, kExpression = GeneKindOf<ExpressionGene>::value,
                  kTemperament = GeneKindOf<TemperamentGene>::value, kKnack = GeneKindOf<KnackGene>::value,
                  kEggKind = GeneKindOf<EggGene>::value, kMetabolism = GeneKindOf<MetabolismGene>::value,
                  kChemKind = GeneKindOf<ChemGene>::value, kPolicy = GeneKindOf<MutationPolicyGene>::value;

uint8_t dc(DriveId d) { return driveChem(d).v; }

// ---- behaviour axes ---------------------------------------------------------------

// One gene byte and the behaviour its name says it moves. lo and hi are the
// trait's low and high ends (bytes, or offsets from the pet's own byte), so
// "high" always means more of what the name says, whichever way the byte runs.
struct Axis {
  const char* name;
  const char* gene;
  uint8_t type;
  Match match;
  uint8_t offset;
  int lo, hi;
  bool relative;
  const char* metric;    // the metric the name implies, which should rise from lo to hi
  Stage stage = Stage::Baby;
};

#define M(expr) [](const GeneView& v) -> bool { return expr; }
const std::vector<Axis>& axes() {
  static const std::vector<Axis> a = {
      // the species row's bytes
      {"bold", "receptor adrenaline->startle gain", kReceptor, M(v.body[0] == chem::adrenaline.v && v.body[1] == locus::startle.v), 4, 128, 160, false, "hop_height"},
      {"appetite", "emitter always->hunger fill", kEmitter, M(v.body[0] == locus::always.v && v.body[1] == dc(drive::hunger)), 3, 24, -24, true, "hunger_mean"},
      {"sleep_pressure", "emitter awake->sleepiness fill", kEmitter, M(v.body[0] == locus::asleep.v && v.body[1] == dc(drive::sleepiness)), 3, 24, -24, true, "asleep_day_pct"},
      {"night_melatonin", "emitter dark->melatonin fill", kEmitter, M(v.body[0] == locus::light.v && v.body[1] == chem::melatonin.v), 3, 24, -24, true, "asleep_night_pct"},
      {"sleep_gate", "receptor sleepiness->sleep_gate threshold", kReceptor, M(v.body[0] == dc(drive::sleepiness) && v.body[1] == locus::sleep_gate.v), 2, 60, -60, true, "sleep_min"},
      // drive tonics
      {"boredom", "emitter always->boredom fill", kEmitter, M(v.body[0] == locus::always.v && v.body[1] == dc(drive::boredom)), 3, 24, -24, true, "boredom_mean"},
      {"loneliness", "emitter always->loneliness fill", kEmitter, M(v.body[0] == locus::always.v && v.body[1] == dc(drive::loneliness)), 3, 24, -24, true, "lonely_mean"},
      {"need_touch", "emitter always->need_touch fill", kEmitter, M(v.body[0] == locus::always.v && v.body[1] == dc(drive::need_touch)), 3, 24, -24, true, "touch_mean"},
      {"fear_tonic", "emitter always->fear fill", kEmitter, M(v.body[0] == locus::always.v && v.body[1] == dc(drive::fear)), 3, 24, -24, true, "fear_mean"},
      // stimuli
      {"shake_fear", "stimulus shake: fear amount", kStimulus, M(v.body[0] == stim::shake.v), 7, 128, 255, false, "fear_max"},
      {"knock_fear", "stimulus knock: fear amount", kStimulus, M(v.body[0] == stim::knock.v), 6, 128, 255, false, "curl_pct"},
      {"cradle_comfort", "stimulus cradle: need_touch relief", kStimulus, M(v.body[0] == stim::cradle.v), 5, 128, 0, false, "touch_mean_neg"},
      // temperament
      {"explore", "temperament explore", kTemperament, M(true), 2, 0, 255, false, "idle_nose"},
      {"habituation", "temperament habituation", kTemperament, M(true), 3, 0, 255, false, "diversity"},
      {"learn_rate", "temperament learnRate", kTemperament, M(true), 0, 0, 255, false, "beliefs"},
      {"forget_rate", "temperament forgetRate", kTemperament, M(true), 1, 0, 255, false, "beliefs_neg"},
      {"dream_every", "temperament dreamEvery (x1 s)", kTemperament, M(true), 4, 60, 1, false, "dreams"},
      {"dream_len", "temperament dreamLen (x1 s)", kTemperament, M(true), 5, 0, 60, false, "dreaming_pct"},
      {"trace", "temperament traceHalfLife", kTemperament, M(true), 6, 1, 120, false, "beliefs"},
      // instincts and faces
      {"eat_instinct", "instinct food_near->eat strength", kInstinct, M(v.body[3] == action::eat.v), 6, 0, 255, false, "eat_pct"},
      {"curl_instinct", "instinct shake/knock->curl strength", kInstinct, M(v.body[3] == action::curl.v), 6, 0, 255, false, "curl_pct"},
      {"foresee_instinct", "instinct day->foresee strength", kInstinct, M(v.body[3] == action::foresee.v), 6, 0, 255, false, "foresee_pct"},
      {"happy_face", "expression happy weight", kExpression, M(v.body[0] == expr::happy.v), 1, 0, 255, false, "face_happy"},
      {"croak_face", "expression croak weight", kExpression, M(v.body[0] == expr::croak.v), 1, 0, 255, false, "face_croak"},
      // the duel
      {"knack_pounce", "knack pounce", kKnack, M(true), 0, 0, 255, false, "duel_pounce"},
      {"knack_guard", "knack guard", kKnack, M(true), 1, 0, 255, false, "duel_guard"},
      // life
      {"lifespan", "chem life half-life", kChemKind, M(v.body[0] == chem::life.v), 1, -12, 12, true, "child_h"},
      {"incubation", "egg incubateMinutes", kEggKind, M(true), 0, 5, 60, false, "hatch_min"},
      {"hatch_dreams", "egg hatchBurstDreams", kEggKind, M(true), 2, 0, 60, false, "beliefs"},
      {"pantry", "metabolism pantrySize", kMetabolism, M(true), 0, 1, 8, false, "feeds"},
      {"refill", "metabolism refillHours (slower)", kMetabolism, M(true), 1, 1, 24, false, "hunger_mean"},
      {"bite", "metabolism biteSize", kMetabolism, M(true), 3, 20, 255, false, "energy_mean"},
  };
  return a;
}
#undef M

Genome setAxis(const Genome& g, const Axis& ax, int which /*0 lo, 1 hi*/, uint8_t* wrote = nullptr) {
  std::optional<GeneView> v = findGene(g, ax.type, ax.match, ax.stage);
  if (!v) return g;
  int value = which ? ax.hi : ax.lo;
  if (ax.relative) value += v->body[ax.offset];
  const uint8_t b = uint8_t(std::clamp(value, ax.type == kEmitter || ax.type == kChemKind ? 1 : 0, 255));
  if (wrote) *wrote = b;
  return withByte(g, v->header.uid, ax.offset, b);
}

// Every axis at a random point of its range.
Genome rollAxes(const Genome& g, Rng& rng) {
  Genome out = g;
  for (const Axis& ax : axes()) {
    std::optional<GeneView> v = findGene(out, ax.type, ax.match, ax.stage);
    if (!v) continue;
    int lo = ax.lo, hi = ax.hi;
    if (ax.relative) lo += v->body[ax.offset], hi += v->body[ax.offset];
    if (lo > hi) std::swap(lo, hi);
    const int b = std::clamp(lo + int(rng.below(uint32_t(hi - lo + 1))), ax.type == kEmitter ? 1 : 0, 255);
    out = withByte(out, v->header.uid, ax.offset, uint8_t(b));
  }
  return out;
}

// ---- a kid's day ---------------------------------------------------------------------

constexpr uint32_t kMinuteMs = 60000;
constexpr uint32_t kHourMs = 60 * kMinuteMs;
constexpr uint32_t kStartHour = 7;
constexpr uint32_t kStartUnix = 1792540800u + kStartHour * 3600u;

uint32_t wallHour(uint32_t ms) { return (kStartHour + ms / kHourMs) % 24; }
bool kidAwake(uint32_t ms) { const uint32_t h = wallHour(ms); return h >= 7 && h < 21; }

// test_idle_day's kid, with a shake now and then: on the desk from 07:00 to
// 21:00 with a look every 4 to 25 minutes (a knock, a knock knock for a pellet
// when the rim asks for one, a pick-up, a tilt, a shake), nobody at night.
class Kid {
 public:
  explicit Kid(uint64_t seed) : rng_(Rng::seeded(seed ^ 0x51EE9ull)) {}
  uint32_t feeds = 0, shakes = 0;
  BodySample next(uint32_t ms, const Dish& dish, const Appearance& a) {
    if (!kidAwake(ms)) {
      g_ = G::Rest, until_ = ms;
      return still();
    }
    if (ms >= until_) choose(ms, dish, a);
    BodySample s = still();
    switch (g_) {
      case G::Knock:
        if (!sent_) s.tapCode = knockCode_, sent_ = true;
        break;
      case G::Hold: s = cradled(); break;
      case G::Tilt: s.ax = tiltX_, s.az = 975; break;
      case G::Shake: s = jolt(((ms - start_) / 80) % 2 == 0); break;
      case G::Rest: break;
    }
    return s;
  }

 private:
  enum class G : uint8_t { Rest, Knock, Hold, Tilt, Shake };
  void act(G g, uint32_t ms, uint32_t forMs) { g_ = g, until_ = ms + forMs, start_ = ms, sent_ = false; }
  uint32_t between(uint32_t lo, uint32_t hi) { return lo + rng_.below(hi - lo + 1); }
  void choose(uint32_t ms, const Dish& dish, const Appearance& a) {
    if (g_ != G::Rest) return act(G::Rest, ms, 1500);
    if (!looked_) {
      looked_ = true;
      return act(G::Rest, ms, between(4, 25) * kMinuteMs);
    }
    looked_ = false;
    const bool pelletDown = dish.habitat().nearestPellet(DishPos{}).has_value();
    if (a.hasHint && a.hint == care::feed && !pelletDown && dish.habitat().pantry > 0) {
      ++feeds, knockCode_ = 2;
      return act(G::Knock, ms, 300);
    }
    switch (rng_.below(8)) {
      case 0: case 1: knockCode_ = 1; return act(G::Knock, ms, 300);
      case 2: case 3: return act(G::Hold, ms, between(10, 40) * 1000);
      case 4: tiltX_ = int16_t(rng_.below(2) ? 220 : -220); return act(G::Tilt, ms, between(4, 10) * 1000);
      case 5: ++shakes; return act(G::Shake, ms, 1200);
      default: return act(G::Rest, ms, 1000);
    }
  }
  Rng rng_;
  G g_ = G::Rest;
  uint32_t until_ = 0, start_ = 0;
  bool looked_ = false, sent_ = false;
  uint8_t knockCode_ = 1;
  int16_t tiltX_ = 0;
};

double fx(Fx v) { return double(v.raw) / double(Fx::kOne); }

struct Run {
  std::map<std::string, double> m;                 // the metrics
  std::map<std::string, uint32_t> doing, idles, faces;
  double asleepMin[96]{};
  double hunger[96]{};
};

const char* familyName(IdleFamily f) {
  switch (f) {
    case IdleFamily::Romp: return "romp";
    case IdleFamily::Nose: return "nose";
    case IdleFamily::Show: return "show";
    case IdleFamily::Muse: return "muse";
    case IdleFamily::Night: return "night";
  }
  return "?";
}

// The pet's duel lean with a full meter, over many turns' noise: its stance
// mix and how often it would wind up the heavy special.
void duelLean(const Creature& c, uint32_t salt, double& pounce, double& guard, double& trick, double& heavy, double& n) {
  const BattleCard mine = cardOf(c, 1, 1, 0);
  BattleCard theirs = mine;
  theirs.mood = Mood{};
  Outcome o;
  o.side[0].spark = kMeterMax;
  for (uint32_t s = 0; s < 24; ++s) {
    const Lean l = petChoice(DuelSkill{}, o, Side::A, mine, theirs, 0, salt * 977u + s);
    pounce += l.stance == Stance::Pounce, guard += l.stance == Stance::Guard, trick += l.stance == Stance::Trick;
    heavy += l.special == Windup::Heavy;
    n += 1;
  }
}

Run live(const Genome& founder, uint64_t kidSeed, double hours) {
  Run r;
  MemStorage store;
  NullLink link;
  FakeRtc rtc;
  uint32_t ms = 0;
  rtc.poweredMs = &ms;
  DishOptions opt;
  opt.rtc = &rtc;
  opt.founder = &founder;
  const uint32_t seed = founder.hash();
  auto dish = std::make_unique<Dish>(store, seed, seed, opt);
  dish->phoneTime().set(kStartUnix);
  dish->checkWall();
  dish->clock().alignToWall(kStartUnix, 0);
  dish->setZone(0);
  Kid kid(kidSeed);
  Appearance a = dish->appearance();
  uint32_t lastTick = 0, lastHourDuel = UINT32_MAX;
  bool wasAsleep = false, wasThinking = false, hatched = false, child = false, wasHop = false;
  double hopHeight = 0, hopCount = 0;
  double awakeTicks = 0, dayTicks = 0, dayAsleep = 0, nightTicks = 0, nightAsleep = 0, asleepTicks = 0, dreamingTicks = 0;
  double hungerSum = 0, fearSum = 0, boredSum = 0, lonelySum = 0, touchSum = 0, sleepySum = 0, energySum = 0, fearMax = 0;
  double naps = 0, thoughts = 0, prophecies = 0, rotSeen = 0, liveTicks = 0;
  double dp = 0, dg = 0, dt = 0, dh = 0, dn = 0;
  double sleepMin = 0;
  bool died = false;
  const uint32_t endMs = uint32_t(hours * kHourMs);
  for (; ms < endMs; ms += kSampleMs) {
    dish->sample(kid.next(ms, *dish, a), ms);
    dish->tick(ms, link);
    if (dish->tickCount() == lastTick) continue;
    lastTick = dish->tickCount();
    a = dish->appearance();
    if (std::holds_alternative<Clutch>(dish->occupant())) died = true;
    const Creature* c = std::get_if<Creature>(&dish->occupant());
    if (!c) continue;
    if (!hatched) hatched = true, r.m["hatch_min"] = double(ms) / kMinuteMs;
    if (!child && c->stage() >= Stage::Child) child = true, r.m["child_h"] = double(ms) / kHourMs;
    liveTicks += 1;
    const uint32_t h = std::min<uint32_t>(95, ms / kHourMs);
    const bool asleep = c->body().asleep;
    const Chemistry& ch = c->chemistry();
    if (asleep) r.asleepMin[h] += 1.0 / (60 * 10), asleepTicks += 1, sleepMin += 1.0 / 600;
    if (asleep && c->body().dreaming) dreamingTicks += 1;
    if (asleep && !wasAsleep && kidAwake(ms)) naps += 1;
    wasAsleep = asleep;
    r.hunger[h] += fx(ch.drive(drive::hunger)) / 36000.0;
    if (kidAwake(ms)) dayTicks += 1, dayAsleep += asleep;
    else nightTicks += 1, nightAsleep += asleep;
    if (a.thinking && !wasThinking) {
      thoughts += 1;
      if (a.prophecy) prophecies += 1;
    }
    wasThinking = a.thinking;
    const bool hop = a.reflexActive && a.reflex == reflex::hop;
    if (hop && !wasHop) hopHeight += fx(a.reflexStrength), hopCount += 1;
    wasHop = hop;
    for (uint8_t i = 0; i < a.itemCount; ++i) rotSeen += a.items[i].what == Appearance::Item::What::RottenPellet;
    const double fear = fx(ch.drive(drive::fear));
    fearMax = std::max(fearMax, fear);
    if (!asleep) {
      awakeTicks += 1;
      hungerSum += fx(ch.drive(drive::hunger));
      fearSum += fear;
      boredSum += fx(ch.drive(drive::boredom));
      lonelySum += fx(ch.drive(drive::loneliness));
      touchSum += fx(ch.drive(drive::need_touch));
      sleepySum += fx(ch.drive(drive::sleepiness));
      energySum += fx(ch.chem[chem::energy.v]);
      r.doing[c->doing()]++;
      r.faces[EXPRESSIONS[a.expression.v % countOf(EXPRESSIONS)].name]++;
      if (c->playing() && c->idle().ticks == 1) r.idles[idleName(c->idle().now)]++;
      if (h != lastHourDuel && kidAwake(ms)) lastHourDuel = h, duelLean(*c, h, dp, dg, dt, dh, dn);
    }
  }
  const Creature* c = std::get_if<Creature>(&dish->occupant());
  auto& m = r.m;
  auto per = [](double a, double b) { return b > 0 ? a / b : 0.0; };
  m["awake_day_pct"] = 100 * per(dayTicks - dayAsleep, dayTicks);
  m["asleep_day_pct"] = 100 * per(dayAsleep, dayTicks);
  m["asleep_night_pct"] = 100 * per(nightAsleep, nightTicks);
  m["sleep_min"] = sleepMin;
  m["naps"] = naps;
  m["dreaming_pct"] = 100 * per(dreamingTicks, asleepTicks);
  m["hunger_mean"] = per(hungerSum, awakeTicks);
  m["fear_mean"] = per(fearSum, awakeTicks);
  m["fear_max"] = fearMax;
  m["boredom_mean"] = per(boredSum, awakeTicks);
  m["lonely_mean"] = per(lonelySum, awakeTicks);
  m["touch_mean"] = per(touchSum, awakeTicks);
  m["touch_mean_neg"] = -m["touch_mean"];
  m["sleepy_mean"] = per(sleepySum, awakeTicks);
  m["energy_mean"] = per(energySum, awakeTicks);
  m["feeds"] = kid.feeds;
  m["hop_height"] = per(hopHeight, hopCount);
  m["thoughts"] = thoughts;
  m["prophecies"] = prophecies;
  m["rot_seen_min"] = rotSeen / 600;
  m["died"] = died;
  m["duel_pounce"] = 100 * per(dp, dn);
  m["duel_guard"] = 100 * per(dg, dn);
  m["duel_trick"] = 100 * per(dt, dn);
  m["duel_heavy"] = 100 * per(dh, dn);
  // Bedtime: the first evening hour (from 17:00) he sleeps 30 minutes of; wake:
  // the first morning hour after it he is up for 30.
  int bed = -1, wake = -1;
  for (int i = 0; i < 96 && i < int(hours); ++i) {
    const uint32_t wh = (kStartHour + i) % 24;
    if (bed < 0 && i >= 10 && (wh >= 17 || wh < 5) && r.asleepMin[i] >= 30) bed = i;
    if (bed >= 0 && wake < 0 && i > bed && wh >= 4 && wh < 12 && r.asleepMin[i] < 30) wake = i;
  }
  m["bedtime_h"] = bed < 0 ? -1.0 : double((kStartHour + uint32_t(bed)) % 24);
  m["wake_h"] = wake < 0 ? -1.0 : double((kStartHour + uint32_t(wake)) % 24);
  if (c) {
    m["hops"] = c->stats().hops;
    m["fed"] = c->stats().fed;
    m["dreams"] = c->stats().dreams;
    double beliefs = 0;
    for (const Belief& b : c->brain().strongestBeliefs(8)) beliefs += std::fabs(fx(b.effect)) * fx(b.confidence);
    m["beliefs"] = beliefs;
    m["beliefs_neg"] = -beliefs;
    m["stage"] = double(uint8_t(c->stage()));
    const Traits& t = c->phenotype().traits;
    m["trait_appetite"] = t.appetite, m["trait_sleepiness"] = t.sleepiness, m["trait_nightOwl"] = t.nightOwl;
    m["trait_playfulness"] = t.playfulness, m["trait_affection"] = t.affection, m["trait_nerves"] = t.nerves;
    m["trait_boldness"] = t.boldness, m["trait_curiosity"] = t.curiosity, m["trait_cheer"] = t.cheer;
  }
  if (!m.count("child_h")) m["child_h"] = hours + 1;
  double total = 0, ent = 0;
  for (auto& [k, n] : r.doing) total += n;
  for (auto& [k, n] : r.doing) {
    const double p = n / total;
    ent -= p * std::log2(p);
  }
  m["kinds"] = double(r.doing.size());
  m["diversity"] = ent;
  auto pct = [&](const char* name) { return r.doing.count(name) ? 100.0 * r.doing.at(name) / total : 0.0; };
  m["eat_pct"] = pct("eat");
  m["curl_pct"] = pct("curl");
  m["foresee_pct"] = pct("foresee");
  m["call_pct"] = pct("call");
  m["rest_pct"] = pct("rest");
  m["wander_pct"] = pct("wander");
  double play = 0;
  for (auto& [k, n] : r.doing)
    for (const IdleInfo& i : IDLES) if (k == i.name) play += n;
  m["play_pct"] = 100 * per(play, total);
  double faceTotal = 0;
  for (auto& [k, n] : r.faces) faceTotal += n;
  for (const ExprInfo& e : EXPRESSIONS)
    m[std::string("face_") + e.name] = r.faces.count(e.name) ? 100.0 * r.faces.at(e.name) / faceTotal : 0.0;
  double fam[5]{}, idleTotal = 0;
  for (auto& [k, n] : r.idles)
    for (const IdleInfo& i : IDLES)
      if (k == i.name) fam[uint8_t(i.family)] += n, idleTotal += n;
  for (int f = 0; f < 5; ++f) m[std::string("idle_") + familyName(IdleFamily(f))] = 100 * per(fam[f], idleTotal);
  return r;
}

std::string topOf(const std::map<std::string, uint32_t>& counts, int n) {
  std::vector<std::pair<uint32_t, std::string>> v;
  for (auto& [k, c] : counts) v.push_back({c, k});
  std::sort(v.rbegin(), v.rend());
  std::string s;
  for (int i = 0; i < n && i < int(v.size()); ++i) s += (i ? " " : "") + v[i].second;
  return s;
}

const std::vector<std::string>& metricNames() {
  static const std::vector<std::string> names = {
      "hatch_min", "child_h", "stage", "died", "awake_day_pct", "asleep_day_pct", "asleep_night_pct", "sleep_min", "naps",
      "bedtime_h", "wake_h", "dreams", "dreaming_pct", "hunger_mean", "energy_mean", "feeds", "fed", "rot_seen_min",
      "fear_mean", "fear_max", "hops", "hop_height", "boredom_mean", "lonely_mean", "touch_mean", "sleepy_mean", "kinds", "diversity",
      "play_pct", "rest_pct", "wander_pct", "eat_pct", "curl_pct", "foresee_pct", "call_pct", "idle_romp", "idle_nose",
      "idle_show", "idle_muse", "idle_night", "face_neutral", "face_happy", "face_alarmed", "face_annoyed", "face_croak",
      "face_blep", "face_sleepy", "thoughts", "prophecies", "beliefs", "duel_pounce", "duel_guard", "duel_trick",
      "duel_heavy", "trait_appetite", "trait_sleepiness", "trait_nightOwl", "trait_playfulness",
      "trait_affection", "trait_nerves", "trait_boldness", "trait_curiosity", "trait_cheer"};
  return names;
}

void header(FILE* f, const char* lead) {
  std::fprintf(f, "%s", lead);
  for (const std::string& n : metricNames()) std::fprintf(f, ",%s", n.c_str());
  std::fprintf(f, ",top_idles,hunger_curve,sleep_curve\n");
}

void row(FILE* f, const std::string& lead, const Run& r, double hours) {
  std::fprintf(f, "%s", lead.c_str());
  for (const std::string& n : metricNames()) std::fprintf(f, ",%.3f", r.m.count(n) ? r.m.at(n) : 0.0);
  std::fprintf(f, ",%s,", topOf(r.idles, 3).c_str());
  for (int h = 0; h < int(hours) && h < 96; ++h) std::fprintf(f, "%s%.2f", h ? " " : "", r.hunger[h]);
  std::fprintf(f, ",");
  for (int h = 0; h < int(hours) && h < 96; ++h) std::fprintf(f, "%s%.0f", h ? " " : "", r.asleepMin[h]);
  std::fprintf(f, "\n");
}

// Runs jobs on `threads` threads, each writing its own row under a lock.
void parallel(size_t n, int threads, const std::function<void(size_t)>& job) {
  std::atomic<size_t> next{0};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t)
    pool.emplace_back([&] {
      for (size_t i; (i = next++) < n;) job(i);
    });
  for (auto& t : pool) t.join();
}

uint32_t boardSeed(uint32_t n) {
  const uint8_t mac[6] = {0xd4, 0x05, 0x92, uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
  return fnv1a(mac, sizeof mac);
}

Genome founderOf(SpeciesId s, uint32_t n) { return foundingGenome(s, boardSeed(n), paint::bandsFor(s)); }

std::vector<SpeciesId> speciesArg(const char* arg) {
  std::vector<SpeciesId> out;
  for (const SpeciesInfo& s : SPECIES)
    if (!arg || !std::strcmp(arg, "all") || std::strstr(arg, s.name)) out.push_back(s.id);
  return out;
}

int behaviour(const std::string& dir, double hours, const char* which, int threads) {
  struct Job { SpeciesId sp; std::string label, axis; int level; uint64_t kid; Genome g; uint8_t byte; };
  std::vector<Job> jobs;
  constexpr int kBaseSeeds = 4, kRandom = 6;
  for (SpeciesId sp : speciesArg(which)) {
    const Genome base = founderOf(sp, 0x771c00);
    for (int k = 0; k < kBaseSeeds; ++k) jobs.push_back({sp, "base", "base", -1, 1000u + k, base, 0});
    Rng rng = Rng::seeded(0xA11A5ull + sp.v);
    for (int k = 0; k < kRandom; ++k) jobs.push_back({sp, "random", "random", k, 1000u, rollAxes(base, rng), 0});
    for (const Axis& ax : axes())
      for (int lv = 0; lv < 2; ++lv) {
        uint8_t b = 0;
        Genome g = setAxis(base, ax, lv, &b);
        jobs.push_back({sp, "axis", ax.name, lv, 1000u, std::move(g), b});
      }
  }
  if (FILE* a = std::fopen((dir + "/axes.csv").c_str(), "w")) {
    std::fprintf(a, "axis,gene,metric,lo,hi,relative\n");
    for (const Axis& ax : axes()) std::fprintf(a, "%s,%s,%s,%d,%d,%d\n", ax.name, ax.gene, ax.metric, ax.lo, ax.hi, ax.relative);
    std::fclose(a);
  }
  FILE* f = std::fopen((dir + "/behaviour.csv").c_str(), "w");
  if (!f) return 1;
  header(f, "species,kind,axis,level,byte,kid");
  std::mutex lock;
  std::atomic<int> done{0};
  parallel(jobs.size(), threads, [&](size_t i) {
    const Job& j = jobs[i];
    const Run r = live(j.g, j.kid, hours);
    char lead[160];
    std::snprintf(lead, sizeof lead, "%s,%s,%s,%d,%u,%llu", speciesInfo(j.sp).name, j.label.c_str(), j.axis.c_str(), j.level,
                  unsigned(j.byte), (unsigned long long)j.kid);
    std::lock_guard<std::mutex> g(lock);
    row(f, lead, r, hours);
    std::fflush(f);
    std::fprintf(stderr, "\r%d/%zu", ++done, jobs.size());
  });
  std::fclose(f);
  std::fprintf(stderr, "\n");
  return 0;
}

// ---- looks -----------------------------------------------------------------------------

bool writePpm(const paint::Canvas240& cv, const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n240 240\n255\n");
  for (int i = 0; i < 240 * 240; ++i) {
    const uint16_t p = cv.px[i];
    const uint8_t px[3] = {uint8_t(((p >> 11) & 31) * 255 / 31), uint8_t(((p >> 5) & 63) * 255 / 63), uint8_t((p & 31) * 255 / 31)};
    std::fwrite(px, 1, 3, f);
  }
  return std::fclose(f) == 0;
}

Appearance adultLook(const Genome& g, bool glow = false) {
  Appearance a = portrait(g, Stage::Adult, 1, 0);
  // One life seed for every portrait, so a blink or the mottle's layout (both
  // drawn from it) never counts as a difference between two pets: only genes do.
  a.lifeSeed = 0x5EED;
  if (glow) a.foreseeing = true, a.glow = Fx::ratio(8, 10), a.expression = a.previous = expr::foresee;
  return a;
}

void render(const Appearance& a, paint::Canvas240& cv) { paint::draw(a, paint::packFor(a.species), cv, 0); }

// How different two renders of one species look to a kid: the mean colour
// distance (RGB, 0..100) over the pixels either shows the pet on. `mask` is
// the species' body, worked out once from renders with every region pushed.
struct Body { std::vector<uint8_t> mask; size_t n = 0; };
double rgbDist(uint16_t p, uint16_t q) {
  const int dr = int((p >> 11) & 31) * 8 - int((q >> 11) & 31) * 8, dg = int((p >> 5) & 63) * 4 - int((q >> 5) & 63) * 4,
            db = int(p & 31) * 8 - int(q & 31) * 8;
  return std::sqrt(double(dr * dr + dg * dg + db * db)) / 441.7 * 100;
}
double lookDistance(const paint::Canvas240& a, const paint::Canvas240& b, const Body& body) {
  double s = 0;
  for (int i = 0; i < 240 * 240; ++i)
    if (body.mask[i]) s += rgbDist(a.px[i], b.px[i]);
  return body.n ? s / body.n : 0;
}

Body bodyOf(SpeciesId sp) {
  Body b;
  b.mask.assign(240 * 240, 0);
  auto cv0 = std::make_unique<paint::Canvas240>(), cv1 = std::make_unique<paint::Canvas240>();
  const Genome g = founderOf(sp, 0x771c00);
  render(adultLook(g), *cv0);
  for (uint8_t r : {region::skin.v, region::belly.v, region::cloak.v, region::eye.v})
    for (uint8_t at : {uint8_t(1), uint8_t(3)})
      for (uint8_t v : {uint8_t(0), uint8_t(255)}) {
        Genome h = g;
        g.forEach([&](const GeneView& x) {
          if (x.header.type == kPalette && x.body[0] % kRegionCount == r && !(x.header.flags & GeneFlags::Dormant))
            h = withByte(h, x.header.uid, at, v);
        });
        render(adultLook(h), *cv1);
        for (int i = 0; i < 240 * 240; ++i) b.mask[i] |= cv0->px[i] != cv1->px[i];
      }
  for (uint8_t m : b.mask) b.n += m;
  return b;
}

struct LookAxis {
  const char* name;
  uint8_t type;
  uint8_t region;    // palette: region; mark: layer; size: 0
  Stage stage;
  uint8_t offset;
  std::vector<int> values;
  bool glow = false;
  bool wake = false;   // the dormant gene of this kind and region, woken
};

std::vector<LookAxis> lookAxes() {
  const std::vector<int> full = {0, 80, 128, 176, 255};
  return {
      {"skin hue", kPalette, region::skin.v, Stage::Baby, 1, full},
      {"skin sat", kPalette, region::skin.v, Stage::Baby, 2, full},
      {"skin val", kPalette, region::skin.v, Stage::Baby, 3, full},
      {"belly hue", kPalette, region::belly.v, Stage::Baby, 1, full},
      {"belly val", kPalette, region::belly.v, Stage::Baby, 3, full},
      {"cloak hue", kPalette, region::cloak.v, Stage::Baby, 1, full},
      {"cloak sat", kPalette, region::cloak.v, Stage::Baby, 2, full},
      {"cloak val", kPalette, region::cloak.v, Stage::Baby, 3, full},
      {"eye hue", kPalette, region::eye.v, Stage::Baby, 1, full},
      {"eye val", kPalette, region::eye.v, Stage::Baby, 3, full},
      {"glow hue", kPalette, region::glow.v, Stage::Baby, 1, full, true},
      {"glow val", kPalette, region::glow.v, Stage::Baby, 3, full, true},
      {"mottle density", kMark, 0, Stage::Baby, 1, {0, 3, 8, 24, 64}},
      {"mottle hue", kMark, 0, Stage::Baby, 2, full},
      {"size (adult)", kSize, 0, Stage::Adult, 0, {0, 64, 128, 192, 255}},
      {"size (baby)", kSize, 0, Stage::Baby, 0, {0, 128, 255}},
      {"size growth", kSize, 0, Stage::Baby, 1, {0, 255}},
      {"cloak: dormant blue woken", kPalette, region::cloak.v, Stage::Baby, 0, {0}, false, true},
      {"glow: dormant bright woken", kPalette, region::glow.v, Stage::Baby, 0, {0}, true, true},
  };
}

// The gene a look axis moves: the LAST awake one for its kind and region (the later gene wins).
std::optional<GeneView> lookGene(const Genome& g, const LookAxis& ax) {
  std::optional<GeneView> out;
  g.forEach([&](const GeneView& v) {
    if (v.header.type != ax.type || v.header.stage != ax.stage || v.header.featGate) return;
    if (bool(v.header.flags & GeneFlags::Dormant) != ax.wake) return;
    if (ax.type == kPalette && v.body[0] % kRegionCount != ax.region) return;
    if (ax.type == kMark && v.body[0] != ax.region) return;
    if (!ax.wake || !out) out = v;
  });
  return out;
}

int looks(const std::string& dir) {
  FILE* f = std::fopen((dir + "/looks.csv").c_str(), "w");
  if (!f) return 1;
  std::fprintf(f, "sheet,row,col,label,value,file,dist_from_founder\n");
  auto cv = std::make_unique<paint::Canvas240>(), ref = std::make_unique<paint::Canvas240>();
  for (const SpeciesInfo& s : SPECIES) {
    const Body body = bodyOf(s.id);
    const Genome g = founderOf(s.id, 0x771c00);
    const std::vector<LookAxis> axs = lookAxes();
    for (size_t r = 0; r < axs.size(); ++r) {
      const LookAxis& ax = axs[r];
      std::optional<GeneView> v = lookGene(g, ax);
      render(adultLook(g, ax.glow), *ref);
      for (size_t c = 0; c < ax.values.size(); ++c) {
        Genome h = g;
        char label[64];
        if (!v) {
          std::snprintf(label, sizeof label, "no gene");
        } else if (ax.wake) {
          h = *GenomeBuilder::from(g).setFlags(v->header.uid, uint8_t(v->header.flags & ~GeneFlags::Dormant)).build();
          std::snprintf(label, sizeof label, "woken");
        } else {
          h = withByte(g, v->header.uid, ax.offset, uint8_t(ax.values[c]));
          std::snprintf(label, sizeof label, "%d", ax.values[c]);
        }
        render(adultLook(h, ax.glow), *cv);
        char file[96];
        std::snprintf(file, sizeof file, "cells/%s_sweep_%02zu_%zu.ppm", s.name, r, c);
        writePpm(*cv, dir + "/" + file);
        std::fprintf(f, "%s_sweep,%zu,%zu,%s,%s,%s,%.2f\n", s.name, r, c, ax.name, label, file, lookDistance(*cv, *ref, body));
      }
    }
    // A grid of random founders: 4 x 6 boards of this species.
    for (int i = 0; i < 24; ++i) {
      const Genome h = founderOf(s.id, 0x880000 + uint32_t(i) * 7919u);
      render(adultLook(h), *cv);
      char file[96];
      std::snprintf(file, sizeof file, "cells/%s_grid_%02d.ppm", s.name, i);
      writePpm(*cv, dir + "/" + file);
      std::fprintf(f, "%s_grid,%d,%d,board %d,,%s,%.2f\n", s.name, i / 6, i % 6, i, file, 0.0);
    }
    std::fprintf(stderr, "%s looks done\n", s.name);
  }
  std::fclose(f);
  return 0;
}

// ---- generations --------------------------------------------------------------------

// The real egg path for one generation: the parent hatched as a Creature, its
// clutch laid as at a death, and each egg made by Clutch::child.
struct Clutched { std::vector<Offspring> eggs; };
Clutched clutchOf(const Genome& parent, uint16_t generation, uint8_t size) {
  Egg egg(Offspring{parent, {}}, generation, 0);
  auto c = std::make_unique<Creature>(egg, 0, 0);
  Clutch k = c->layClutch(size, Fx::zero(), 0);
  Clutched out;
  for (uint8_t i = 0; i < k.count; ++i) out.eggs.push_back(k.child(i));
  return out;
}

struct OpCount { uint32_t point = 0, wild = 0, dup = 0, del = 0, wake = 0, sleep = 0, heir = 0, look = 0, mind = 0; };
OpCount countOps(const Offspring& o, const Genome& parent) {
  OpCount n;
  for (const MutationOp& op : o.diff.ops) {
    GeneUid uid{};
    std::visit([&](const auto& x) {
      using T = std::decay_t<decltype(x)>;
      if constexpr (std::is_same_v<T, MutPoint>) ++n.point, n.wild += x.wild, uid = x.gene;
      if constexpr (std::is_same_v<T, MutDup>) ++n.dup, uid = x.gene;
      if constexpr (std::is_same_v<T, MutDel>) ++n.del, uid = x.gene;
      if constexpr (std::is_same_v<T, MutWake>) ++n.wake, uid = x.gene;
      if constexpr (std::is_same_v<T, MutSleep>) ++n.sleep, uid = x.gene;
      if constexpr (std::is_same_v<T, MutHeirloom>) ++n.heir, uid = x.after;
    }, op);
    std::optional<GeneView> v = o.genome.find(uid);
    if (!v) v = parent.find(uid);
    const GeneTypeInfo* info = v ? geneType(v->header.type) : nullptr;
    if (info && info->cls == GeneClass::Look) ++n.look;
    if (info && info->cls == GeneClass::Mind) ++n.mind;
  }
  return n;
}

// Body bytes that differ at shared uids, plus every gene only one of them has.
uint32_t byteDiff(const Genome& a, const Genome& b) {
  uint32_t n = 0;
  a.forEach([&](const GeneView& v) {
    std::optional<GeneView> w = b.find(v.header.uid);
    if (!w || w->header.len != v.header.len) { n += v.header.len; return; }
    for (uint8_t i = 0; i < v.header.len; ++i) n += v.body[i] != w->body[i];
    n += (v.header.flags & GeneFlags::Dormant) != (w->header.flags & GeneFlags::Dormant);
  });
  b.forEach([&](const GeneView& v) { if (!a.find(v.header.uid)) n += v.header.len; });
  return n;
}

int lineage(const std::string& dir, int generations, int lines, double hours, int threads) {
  FILE* lf = std::fopen((dir + "/lineage.csv").c_str(), "w");
  FILE* kf = std::fopen((dir + "/kinship.csv").c_str(), "w");
  FILE* sf = std::fopen((dir + "/strip.csv").c_str(), "w");
  if (!lf || !kf || !sf) return 1;
  std::fprintf(lf, "species,line,pick,gen,genes,bytes_from_founder,look_from_founder,look_from_parent,sib_look,"
                   "point,wild,dup,del,wake,sleep,heir,look_ops,mind_ops,skin_hue,skin_val,size,mottle\n");
  std::fprintf(kf, "species,pair,gen,look\n");
  std::fprintf(sf, "species,line,pick,gen,file\n");
  auto cv = std::make_unique<paint::Canvas240>(), cw = std::make_unique<paint::Canvas240>(),
       cf = std::make_unique<paint::Canvas240>();
  struct Lived { SpeciesId sp; int line; std::string pick; int gen; Genome g; };
  std::vector<Lived> toLive;
  const int strips[] = {0, 5, 10, 20, 40};
  for (const SpeciesInfo& s : SPECIES) {
    const Body body = bodyOf(s.id);
    // Strangers: founders of this species from other boards.
    std::vector<std::unique_ptr<paint::Canvas240>> strangers;
    for (int i = 0; i < 8; ++i) {
      strangers.push_back(std::make_unique<paint::Canvas240>());
      render(adultLook(founderOf(s.id, 0x990000 + uint32_t(i) * 104729u)), *strangers.back());
    }
    for (size_t i = 0; i < strangers.size(); ++i)
      for (size_t j = i + 1; j < strangers.size(); ++j)
        std::fprintf(kf, "%s,strangers,0,%.2f\n", s.name, lookDistance(*strangers[i], *strangers[j], body));
    std::vector<std::vector<std::unique_ptr<paint::Canvas240>>> lastGen;   // per line, the gen-20 pet (for cross-line strangers)
    for (const char* pick : {"random", "different"}) {
      std::vector<std::unique_ptr<paint::Canvas240>> ends;
      for (int line = 0; line < lines; ++line) {
        const Genome founder = founderOf(s.id, 0x660000 + uint32_t(line) * 31337u);
        render(adultLook(founder), *cf);
        Genome g = founder;
        Rng pickRng = Rng::seeded(0x9100ull * (line + 1) + s.id.v);
        auto stripCell = [&](int gen) {
          char file[96];
          std::snprintf(file, sizeof file, "cells/%s_line%d_%s_gen%02d.ppm", s.name, line, pick, gen);
          render(adultLook(g), *cv);
          writePpm(*cv, dir + "/" + file);
          std::fprintf(sf, "%s,%d,%s,%d,%s\n", s.name, line, pick, gen, file);
        };
        stripCell(0);
        if (!std::strcmp(pick, "random") && line < 2) toLive.push_back({s.id, line, pick, 0, g});
        for (int gen = 1; gen <= generations; ++gen) {
          const Clutched k = clutchOf(g, uint16_t(gen - 1), 3);
          std::vector<std::unique_ptr<paint::Canvas240>> eggs;
          for (const Offspring& o : k.eggs) {
            eggs.push_back(std::make_unique<paint::Canvas240>());
            render(adultLook(o.genome), *eggs.back());
          }
          render(adultLook(g), *cw);   // the parent
          double sib = 0, sibN = 0;
          for (size_t i = 0; i < eggs.size(); ++i)
            for (size_t j = i + 1; j < eggs.size(); ++j) {
              const double d = lookDistance(*eggs[i], *eggs[j], body);
              sib += d, sibN += 1;
              std::fprintf(kf, "%s,siblings,%d,%.2f\n", s.name, gen, d);
            }
          for (size_t i = 0; i < eggs.size(); ++i)
            std::fprintf(kf, "%s,parent_child,%d,%.2f\n", s.name, gen, lookDistance(*eggs[i], *cw, body));
          size_t chosen = 0;
          if (!std::strcmp(pick, "random")) {
            chosen = pickRng.below(uint32_t(eggs.size()));
          } else {
            double best = -1;
            for (size_t i = 0; i < eggs.size(); ++i) {
              const double d = lookDistance(*eggs[i], *cw, body);
              if (d > best) best = d, chosen = i;
            }
          }
          const Offspring& o = k.eggs[chosen];
          const OpCount n = countOps(o, g);
          const double fromParent = lookDistance(*eggs[chosen], *cw, body);
          g = o.genome;
          const double fromFounder = lookDistance(*eggs[chosen], *cf, body);
          const Appearance a = adultLook(g);
          int mottle = 0;
          for (uint8_t i = 0; i < a.markCount; ++i) if (a.marks[i].layer == 0) mottle = a.marks[i].variant;
          std::fprintf(lf, "%s,%d,%s,%d,%zu,%u,%.2f,%.2f,%.2f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%u,%d\n", s.name, line, pick, gen,
                       g.geneCount(), byteDiff(founder, g), fromFounder, fromParent, sibN ? sib / sibN : 0.0, n.point, n.wild,
                       n.dup, n.del, n.wake, n.sleep, n.heir, n.look, n.mind, int(a.regions[region::skin.v].hue),
                       int(a.regions[region::skin.v].val), unsigned(a.scalePct), mottle);
          if (std::find(std::begin(strips), std::end(strips), gen) != std::end(strips)) stripCell(gen);
          if (!std::strcmp(pick, "random") && line < 2 && (gen == 5 || gen == 10 || gen == 20)) toLive.push_back({s.id, line, pick, gen, g});
        }
        ends.push_back(std::make_unique<paint::Canvas240>());
        render(adultLook(g), *ends.back());
        std::fflush(lf), std::fflush(kf);
      }
      for (size_t i = 0; i < ends.size(); ++i)
        for (size_t j = i + 1; j < ends.size(); ++j)
          std::fprintf(kf, "%s,lines_at_end_%s,%d,%.2f\n", s.name, pick, generations, lookDistance(*ends[i], *ends[j], body));
    }
    std::fprintf(stderr, "%s lineage done\n", s.name);
  }
  std::fclose(lf), std::fclose(kf), std::fclose(sf);
  if (hours <= 0) return 0;
  FILE* bf = std::fopen((dir + "/lineage_behaviour.csv").c_str(), "w");
  header(bf, "species,line,pick,gen");
  std::mutex lock;
  std::atomic<int> done{0};
  parallel(toLive.size(), threads, [&](size_t i) {
    const Lived& l = toLive[i];
    const Run r = live(l.g, 1000, hours);
    char lead[96];
    std::snprintf(lead, sizeof lead, "%s,%d,%s,%d", speciesInfo(l.sp).name, l.line, l.pick.c_str(), l.gen);
    std::lock_guard<std::mutex> g(lock);
    row(bf, lead, r, hours);
    std::fflush(bf);
    std::fprintf(stderr, "\r%d/%zu", ++done, toLive.size());
  });
  std::fclose(bf);
  return 0;
}

// One line again, eggs picked at random as `lineage` picks them, printing every
// op that is not a quiet point mutation: what a line lost, slept, woke or copied.
std::string geneText(const GeneView& v) {
  const GeneTypeInfo* info = geneType(v.header.type);
  std::string s = std::string(info ? info->name : "?") + "#" + std::to_string(v.header.uid.v) + "[";
  for (uint8_t i = 0; i < v.header.len && i < 9; ++i) s += (i ? " " : "") + std::to_string(v.body[i]);
  return s + "]";
}

int explain(const char* name, int line, int generations) {
  SpeciesId sp = kGrungo;
  for (const SpeciesInfo& s : SPECIES) if (!std::strcmp(s.name, name)) sp = s.id;
  Genome g = founderOf(sp, 0x660000 + uint32_t(line) * 31337u);
  Rng pickRng = Rng::seeded(0x9100ull * (line + 1) + sp.v);
  for (int gen = 1; gen <= generations; ++gen) {
    const Clutched k = clutchOf(g, uint16_t(gen - 1), 3);
    const Offspring& o = k.eggs[pickRng.below(uint32_t(k.eggs.size()))];
    for (const MutationOp& op : o.diff.ops) {
      std::visit([&](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        auto at = [&](GeneUid u) { std::optional<GeneView> v = g.find(u); return v ? geneText(*v) : std::string("?"); };
        if constexpr (std::is_same_v<T, MutDel>) std::printf("gen %d  deleted %s\n", gen, at(x.gene).c_str());
        if constexpr (std::is_same_v<T, MutSleep>) std::printf("gen %d  slept   %s\n", gen, at(x.gene).c_str());
        if constexpr (std::is_same_v<T, MutWake>) std::printf("gen %d  woke    %s\n", gen, at(x.gene).c_str());
        if constexpr (std::is_same_v<T, MutDup>) std::printf("gen %d  copied  %s\n", gen, at(x.gene).c_str());
        if constexpr (std::is_same_v<T, MutPoint>)
          if (x.wild) std::printf("gen %d  wild    %s byte %u: %u -> %u\n", gen, at(x.gene).c_str(), x.offset, x.from, x.to);
      }, op);
    }
    g = o.genome;
  }
  return 0;
}

// How big each species can grow before the rim cuts him: the farthest pixel
// of him from the panel's centre, at each adult size byte. His pixels are the
// ones a coat change moves (skin, belly, cloak pushed both ways), at the size.
int clip() {
  auto a = std::make_unique<paint::Canvas240>(), b = std::make_unique<paint::Canvas240>();
  std::printf("species,size_byte,scale_pct,reach_px\n");
  for (const SpeciesInfo& s : SPECIES) {
    const Genome g = founderOf(s.id, 0x771c00);
    for (int base = 128; base <= 255; base += 8) {
      Genome sized = g;
      sized.forEach([&](const GeneView& v) {
        if (v.header.type == kSize && v.header.stage == Stage::Adult) sized = withByte(sized, v.header.uid, 0, uint8_t(base));
      });
      double reach = 0;
      for (uint8_t r : {region::skin.v, region::belly.v, region::cloak.v}) {
        Genome lo = sized, hi = sized;
        sized.forEach([&](const GeneView& v) {
          if (v.header.type == kPalette && v.body[0] % kRegionCount == r && !(v.header.flags & GeneFlags::Dormant))
            lo = withByte(lo, v.header.uid, 3, 0), hi = withByte(hi, v.header.uid, 3, 255);
        });
        render(adultLook(lo), *a);
        render(adultLook(hi), *b);
        for (int i = 0; i < 240 * 240; ++i)
          if (a->px[i] != b->px[i]) reach = std::max(reach, std::hypot(i % 240 - 119.5, i / 240 - 119.5));
      }
      std::printf("%s,%d,%u,%.1f\n", s.name, base, unsigned(adultLook(sized).scalePct), reach);
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && !std::strcmp(argv[1], "clip")) return clip();
  if (argc >= 3 && !std::strcmp(argv[1], "explain")) return explain(argv[2], argc > 3 ? std::atoi(argv[3]) : 0, argc > 4 ? std::atoi(argv[4]) : 10);
  if (argc < 3) {
    std::fprintf(stderr, "usage: gene_variety behaviour|looks|lineage <out> [...]\n");
    return 2;
  }
  const std::string mode = argv[1], dir = argv[2];
  std::system(("mkdir -p '" + dir + "/cells'").c_str());
  auto arg = [&](int i, double d) { return argc > i ? std::atof(argv[i]) : d; };
  if (mode == "behaviour") return behaviour(dir, arg(3, 24), argc > 4 ? argv[4] : "all", int(arg(5, 4)));
  if (mode == "looks") return looks(dir);
  if (mode == "lineage") return lineage(dir, int(arg(3, 20)), int(arg(4, 3)), arg(5, 24), int(arg(6, 4)));
  if (mode == "time") {   // one run, to see what a pet-day costs
    const Run r = live(founderOf(SpeciesId{2}, 0x771c00), 1000, arg(3, 24));
    std::printf("awake %.1f%% night asleep %.1f%% hops %.0f feeds %.0f\n", r.m.at("awake_day_pct"), r.m.at("asleep_night_pct"),
                r.m.count("hops") ? r.m.at("hops") : 0.0, r.m.at("feeds"));
    return 0;
  }
  return 2;
}
