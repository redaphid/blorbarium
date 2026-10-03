#include "blorb/thoughts.h"

#include <variant>
#include "blorb/appearance.h"
#include "blorb/genes.h"
#include "blorb/lineage.h"

namespace blorb {
namespace {

constexpr Fx kHigh = Fx::ratio(6, 10);
constexpr Fx kHalf = Fx::ratio(1, 2);
constexpr Fx kLow = Fx::ratio(3, 10);
constexpr Fx kLearned = Fx::ratio(1, 10);   // a weight this far from zero is a lesson he holds
constexpr int kRecentTicks = 8;             // a stimulus counts as just now for this long
constexpr uint64_t kOracleSalt = 0x0AC1E5ull;

Fx level(const Observed& o, DriveId d) { return o.c.chemistry().drive(d); }
Fx sense(const Observed& o, LocusId l) { return o.c.chemistry().locus[l.v]; }
// Recent loci are 1 at the stimulus and halve every tick.
bool justNow(const Observed& o, StimId s) { return sense(o, locus::recent(s)).raw >= (Fx::kOne >> kRecentTicks); }
bool awake(const Observed& o) { return !o.c.body().asleep; }
bool doing(const Observed& o, ActionId a) { return o.c.action() == a; }
// Tick order that survives the counter wrapping.
bool before(uint32_t a, uint32_t b) { return int32_t(a - b) < 0; }

bool pelletIn(const Observed& o, bool rotten) {
  const uint32_t rot = o.c.phenotype().habitat.rotTicks;
  for (const Pellet& p : o.habitat.pellets)
    if (p.present && (o.tick - p.droppedTick > rot) == rotten) return true;
  return false;
}
bool anyPellet(const Observed& o) { return pelletIn(o, false) || pelletIn(o, true); }

// Whether, for some action, he has learned that this feature raises that drive.
bool learnedRaises(const Observed& o, LocusId feature, DriveId d, Fx by) {
  for (const ActionInfo& a : ACTIONS)
    if (o.c.brain().predict(feature, a.id, d) >= by) return true;
  return false;
}
bool dreadsShaking(const Observed& o) { return learnedRaises(o, locus::recent(stim::shake), drive::fear, kLearned); }

// The first heirloom the lineage can date, optionally only lessons about fear.
bool datedHeirloom(const Observed& o, bool fearOnly, uint16_t& num) {
  for (uint8_t i = 0; i < o.heirlooms.count; ++i) {
    const Heirloom& h = o.heirlooms.at[i];
    if (h.learnedBy == kUnknownGeneration) continue;
    if (fearOnly && !(h.drive == drive::fear && h.level > Fx::zero())) continue;
    num = h.learnedBy;
    return true;
  }
  return false;
}

Rng seededFor(uint32_t life, uint32_t tick, uint64_t salt = 0) { return Rng::seeded(((uint64_t(life) << 32) | tick) ^ salt); }

}  // namespace

// ---- conditions: one per `when` in thoughts.def -------------------------------------
namespace when {

bool hungry(const Observed& o, uint16_t&) { return level(o, drive::hunger) >= kHalf; }
bool starving(const Observed& o, uint16_t&) { return level(o, drive::hunger) >= Fx::ratio(8, 10); }
bool hoarding(const Observed& o, uint16_t&) { return awake(o) && pelletIn(o, false) && level(o, drive::hunger) < kLow; }
bool chewing(const Observed& o, uint16_t&) { return o.c.body().mouth == Mouthful::Pellet; }
bool rotten_bite(const Observed& o, uint16_t&) { return o.c.body().mouth == Mouthful::RottenPellet; }
bool rotten_near(const Observed& o, uint16_t&) { return awake(o) && pelletIn(o, true); }
bool sick(const Observed& o, uint16_t&) { return o.c.chemistry().chem[chem::toxin.v] >= kLow; }
bool hurt(const Observed& o, uint16_t&) { return o.c.chemistry().chem[chem::injury.v] >= Fx::ratio(4, 10); }
bool knocked(const Observed& o, uint16_t&) { return awake(o) && justNow(o, stim::knock); }
bool shaken(const Observed& o, uint16_t&) { return o.c.body().reflex && o.c.body().reflex->kind == reflex::hop; }
bool dreads_shake(const Observed& o, uint16_t&) { return awake(o) && sense(o, locus::held) >= kHalf && dreadsShaking(o); }
bool dreads(const Observed& o, uint16_t&) { return dreadsShaking(o); }
bool scared(const Observed& o, uint16_t&) { return level(o, drive::fear) >= kHalf; }
bool ancestor_fear(const Observed& o, uint16_t& num) { return level(o, drive::fear) >= kLow && datedHeirloom(o, true, num); }
bool heirloom(const Observed& o, uint16_t& num) { return awake(o) && doing(o, action::rest) && datedHeirloom(o, false, num); }
bool foreseen(const Observed& o, uint16_t& num) { return datedHeirloom(o, false, num); }
bool bored(const Observed& o, uint16_t&) { return awake(o) && level(o, drive::boredom) >= kHigh; }
bool chasing(const Observed& o, uint16_t&) { return doing(o, action::chase); }
bool held(const Observed& o, uint16_t&) { return sense(o, locus::held) >= kHalf && sense(o, locus::cradled) < kHalf; }
bool cradled(const Observed& o, uint16_t&) { return sense(o, locus::cradled) >= kHalf; }
bool upside_down(const Observed& o, uint16_t&) { return awake(o) && sense(o, locus::upside_down) >= kHalf; }
bool drowsy(const Observed& o, uint16_t&) { return awake(o) && level(o, drive::sleepiness) >= kHigh; }
bool dreaming(const Observed& o, uint16_t&) { return o.c.body().asleep && o.c.body().dreaming; }
bool foreseeing(const Observed& o, uint16_t&) { return doing(o, action::foresee); }
bool sees_pellet(const Observed& o, uint16_t&) { return doing(o, action::foresee) && anyPellet(o); }
bool sees_phone(const Observed& o, uint16_t&) { return doing(o, action::foresee) && sense(o, locus::owner_near) >= kHalf; }
bool sees_famine(const Observed& o, uint16_t&) {
  return doing(o, action::foresee) && !anyPellet(o) && o.habitat.pantry == 0;
}
bool phone_near(const Observed& o, uint16_t&) { return sense(o, locus::owner_near) >= kHalf; }
bool missing_you(const Observed& o, uint16_t&) { return level(o, drive::loneliness) >= kHalf; }
bool elder(const Observed& o, uint16_t&) { return awake(o) && o.c.stage() == Stage::Elder; }
bool old(const Observed& o, uint16_t&) { return o.c.stage() == Stage::Elder; }
bool night(const Observed& o, uint16_t&) { return awake(o) && o.clock.night(); }
bool dark(const Observed& o, uint16_t&) { return o.clock.night(); }
bool light(const Observed& o, uint16_t&) { return !o.clock.night(); }
bool lonely(const Observed& o, uint16_t&) { return awake(o) && level(o, drive::loneliness) >= kHigh; }
bool learned_food(const Observed& o, uint16_t&) {
  return awake(o) && level(o, drive::hunger) >= kLow &&
         o.c.brain().predict(locus::food_near, action::eat, drive::hunger) <= -kLearned;
}
bool always(const Observed& o, uint16_t& num) {
  num = o.c.generation();
  return awake(o);
}
bool anytime(const Observed& o, uint16_t& num) {
  num = o.c.generation();
  return true;
}

}  // namespace when

const ThoughtInfo* thoughtInfo(ThoughtId id) {
  for (const ThoughtInfo& t : THOUGHTS)
    if (t.id == id) return &t;
  return nullptr;
}

size_t thoughtLine(ThoughtId id, uint16_t num, VoiceId voice, uint8_t pick, char* out) {
  const ThoughtInfo* t = thoughtInfo(id);
  const char* text = t ? t->text : "";
  uint8_t own = 0;
  for (const VoicedLine& l : VOICED_LINES) own += l.thought == id && l.voice == voice;
  if (own) {
    uint8_t want = uint8_t(pick % own);
    for (const VoicedLine& l : VOICED_LINES)
      if (l.thought == id && l.voice == voice && want-- == 0) { text = l.text; break; }
  }
  const VoiceInfo* v = nullptr;
  for (const VoiceInfo& row : VOICES) if (row.id == voice) v = &row;
  const FrameInfo* f = nullptr;
  if (v && !own) {
    uint8_t frames = 0;
    for (const FrameInfo& row : FRAMES) frames += row.voice == voice;
    uint8_t want = uint8_t(pick % frames);
    for (const FrameInfo& row : FRAMES)
      if (row.voice == voice && want-- == 0) { f = &row; break; }
  }
  size_t n = 0;
  auto put = [&](char c) { if (n + 1 < kThoughtLineCap) out[n++] = c; };
  if (f) for (const char* s = f->prefix; *s; ++s) put(*s);
  const size_t body = n;
  for (const char* s = text; *s; ++s) {
      if (*s != '#') { put(*s); continue; }
      char digits[5];
      int k = 0;
      uint16_t rest = num;
      do { digits[k++] = char('0' + rest % 10); rest = uint16_t(rest / 10); } while (rest);
      while (k) put(digits[--k]);
  }
  // A terse voice stops at the first sentence's end (an ellipsis is not one).
  if (v && v->firstOnly && !own)
    for (size_t i = body; i + 1 < n; ++i)
      if ((out[i] == '.' || out[i] == '!' || out[i] == '?') && out[i + 1] == ' ' && (i == body || out[i - 1] != '.')) {
        n = i + 1;
        break;
      }
  // A suffix's own leading stop gives way to the line's ("PELLET!" + ". MINE." -> "PELLET! MINE.").
  if (f) {
    const char* s = f->suffix;
    if (n > body && (out[n - 1] == '.' || out[n - 1] == '!' || out[n - 1] == '?'))
      while (*s == '.' || *s == ',') ++s;
    for (; *s; ++s) put(*s);
  }
  out[n] = 0;
  return n;
}

Heirlooms heirloomsOf(const Genome& g, const Lineage& lineage) {
  constexpr uint8_t kInstinct = GeneKindOf<InstinctGene>::value;
  Heirlooms out;
  GeneUid uids[std::size(out.at)]{};
  g.forEach([&](const GeneView& v) {
    const GeneHeader& h = v.header;
    if (!(h.flags & GeneFlags::Heirloom) || (h.flags & GeneFlags::Dormant) || h.type != kInstinct ||
        h.len < sizeof(InstinctGene) || out.count >= std::size(out.at))
      return;
    uids[out.count] = h.uid;
    out.at[out.count++] = Heirloom{LocusId{v.body[0]}, ActionId{v.body[3]}, DriveId{v.body[4]},
                                   Fx::signedByte(v.body[5]), kUnknownGeneration};
  });
  if (!out.count) return out;
  // The birth that added a gene names the life before it as the learner; the
  // oldest such birth is where the lesson began.
  lineage.forEach([&](const LineageEntry& e) {
    const Birth* b = std::get_if<Birth>(&e);
    if (!b || b->generation == 0) return;
    for (const MutationOp& op : b->diff.ops) {
      const MutHeirloom* m = std::get_if<MutHeirloom>(&op);
      if (!m) continue;
      std::optional<Genome> one = Genome::parse(m->gene.data(), m->gene.size());
      if (!one || one->geneCount() != 1) continue;
      for (uint8_t i = 0; i < out.count; ++i)
        if (uids[i] == one->gene(0).header.uid && out.at[i].learnedBy == kUnknownGeneration)
          out.at[i].learnedBy = uint16_t(b->generation - 1);
    }
  });
  return out;
}

std::optional<ThoughtPick> chooseProphecy(const Observed& o, const uint32_t* readyAt, Rng& rng) {
  const Phenotype::Oracle& oracle = o.c.phenotype().oracle;
  uint32_t weights[kThoughtCount]{};
  uint16_t nums[kThoughtCount]{};
  uint32_t total = 0;
  for (size_t i = 0; i < kThoughtCount; ++i) {
    const ThoughtInfo& t = THOUGHTS[i];
    if (!t.prophecy() || (readyAt && before(o.tick, readyAt[i]))) continue;
    if (!t.when(o, nums[i])) continue;
    weights[i] = uint32_t(oracle.topics[t.topic.v % kTopicCount]) * (1u + t.priority);
    total += weights[i];
  }
  if (total == 0) return std::nullopt;
  uint32_t at = rng.below(total);
  for (size_t i = 0; i < kThoughtCount; ++i) {
    if (at < weights[i]) return ThoughtPick{THOUGHTS[i].id, nums[i]};
    at -= weights[i];
  }
  return std::nullopt;
}

// ---- the Thinker ------------------------------------------------------------------

void Thinker::meet(const Creature& c, const Lineage& lineage, uint32_t tick) {
  life_ = c.genome().hash();
  lifeGeneration_ = c.generation();
  heirlooms_ = heirloomsOf(c.genome(), lineage);
  for (uint32_t& r : readyAt_) r = tick;
  quietUntil_ = tick + kQuietTicks;
  now_.reset();
}

void Thinker::start(const Creature& c, ThoughtPick pick, uint32_t tick, Rng& rng) {
  const ThoughtInfo* info = thoughtInfo(pick.id);
  Shown s{pick.id, info->prophecy(), tick, 0, {}};
  const size_t len = thoughtLine(pick.id, pick.num, c.phenotype().oracle.voice, uint8_t(rng.below(256)), s.line);
  s.ticks = uint16_t(kThoughtLeadTicks + len * kThoughtTicksPerChar);
  now_ = s;
  readyAt_[info - THOUGHTS] = tick + uint32_t(info->cooldownS) * (1000 / kTickMs);
}

void Thinker::step(const Creature& c, const Habitat& habitat, const PetClock& clock, const Lineage& lineage,
                   uint32_t tick, bool shook) {
  if (c.genome().hash() != life_ || c.generation() != lifeGeneration_) meet(c, lineage, tick);
  if (c.stats().ageTicks < kWarmUpTicks) return;
  const Observed o{c, habitat, clock, heirlooms_, tick};
  // Every shake foretells, and a new shake restarts the line rather than
  // queueing behind it. With every row on cooldown he repeats one.
  if (shook) {
    Rng rng = seededFor(life_, tick, kOracleSalt);
    std::optional<ThoughtPick> p = chooseProphecy(o, readyAt_, rng);
    if (!p) p = chooseProphecy(o, nullptr, rng);
    if (p) return start(c, *p, tick, rng);
  }
  if (now_) {
    if (tick - now_->since < now_->ticks) return;
    // An urgent line or a prophecy is an interjection: the gap it cut short
    // still runs, so it cannot crowd out what he was waiting to say.
    const bool interjection = now_->prophecy || thoughtInfo(now_->id)->priority >= kUrgentThought;
    now_.reset();
    if (!interjection) quietUntil_ = tick + kQuietTicks + seededFor(life_, tick).below(kQuietJitterTicks);
    return;
  }
  uint8_t top[kThoughtCount];
  uint16_t nums[kThoughtCount]{};
  size_t n = 0;
  for (size_t i = 0; i < kThoughtCount; ++i) {
    const ThoughtInfo& t = THOUGHTS[i];
    if (t.prophecy() || before(tick, readyAt_[i]) || (n && t.priority < THOUGHTS[top[0]].priority)) continue;
    uint16_t num = 0;
    if (!t.when(o, num)) continue;
    if (n && t.priority > THOUGHTS[top[0]].priority) n = 0;
    nums[n] = num;
    top[n++] = uint8_t(i);
  }
  if (!n || (before(tick, quietUntil_) && THOUGHTS[top[0]].priority < kUrgentThought)) return;
  Rng rng = seededFor(life_, tick);
  const uint32_t pick = rng.below(uint32_t(n));
  start(c, ThoughtPick{THOUGHTS[top[pick]].id, nums[pick]}, tick, rng);
}

void Thinker::force(ThoughtId id, const Creature& c, const Habitat& habitat, const PetClock& clock,
                    const Lineage& lineage, uint32_t tick) {
  const ThoughtInfo* info = thoughtInfo(id);
  if (!info) return;
  if (c.genome().hash() != life_ || c.generation() != lifeGeneration_) meet(c, lineage, tick);
  uint16_t num = 0;
  info->when(Observed{c, habitat, clock, heirlooms_, tick}, num);
  Rng rng = seededFor(life_, tick);
  start(c, ThoughtPick{id, num}, tick, rng);
}

void Thinker::show(Appearance& a, uint32_t tick) const {
  if (!now_ || tick - now_->since >= now_->ticks) return;
  a.thinking = true;
  a.prophecy = now_->prophecy;
  a.thought = now_->id;
  std::copy(std::begin(now_->line), std::end(now_->line), a.line);
  a.thoughtPhase = Fx::ratio(int32_t(tick - now_->since), now_->ticks);
  if (!now_->prophecy) return;
  // The seer at work: the foresee face and glow, and no hop for the shake that asked.
  const uint16_t since = uint16_t(std::min<uint32_t>(UINT16_MAX, tick - now_->since));
  if (a.expression != expr::foresee) a.previous = a.expression;
  a.expression = expr::foresee;
  a.intensity = Fx::one();
  a.exprTicks = since;
  a.foreseeing = true;
  a.glow = fxMax(a.glow, kForeseeGlowFloor);
  if (a.reflexActive && a.reflex == reflex::hop) a.reflexActive = false;
}

}  // namespace blorb
