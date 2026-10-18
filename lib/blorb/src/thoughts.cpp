#include "blorb/thoughts.h"

#include <cstring>
#include <variant>
#include "blorb/appearance.h"
#include "blorb/genes.h"
#include "blorb/lineage.h"
#include "blorb/taught.h"

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

// The owner's local clock, only once a phone's TIME has given it this boot:
// a wall-clock line never guesses.
constexpr uint32_t kSecondsPerDay = 86400;
std::optional<uint32_t> hourNow(const Observed& o) {
  if (!o.phone.localNow) return std::nullopt;
  return *o.phone.localNow % kSecondsPerDay / 3600;
}
// 0 is Sunday; 1 January 1970 was a Thursday.
std::optional<uint32_t> weekdayNow(const Observed& o) {
  if (!o.phone.localNow) return std::nullopt;
  return (*o.phone.localNow / kSecondsPerDay + 4) % 7;
}

// His name as the marquee draws it: a rename's '_' is a space.
size_t nameInFont(const char* name, char (&out)[kMaxNameText + 1]) {
  char spaced[kMaxNameText + 1]{};
  for (size_t i = 0; i < kMaxNameText && name[i]; ++i) spaced[i] = name[i] == '_' ? ' ' : name[i];
  return toFont(spaced, out, kMaxNameText);
}

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
bool dozing(const Observed& o, uint16_t&) { return o.c.body().asleep; }
bool young(const Observed& o, uint16_t&) { return o.c.stats().ageTicks < PetClock::kDayTicks; }   // his first day: when the teaching lines run
bool foreseeing(const Observed& o, uint16_t&) { return doing(o, action::foresee); }
bool sees_pellet(const Observed& o, uint16_t&) { return doing(o, action::foresee) && anyPellet(o); }
bool sees_phone(const Observed& o, uint16_t&) { return doing(o, action::foresee) && sense(o, locus::owner_near) >= kHalf; }
bool sees_famine(const Observed& o, uint16_t&) {
  return doing(o, action::foresee) && !anyPellet(o) && o.habitat.pantry == 0;
}
bool phone_near(const Observed& o, uint16_t&) { return sense(o, locus::owner_near) >= kHalf; }
bool missing_you(const Observed& o, uint16_t&) { return level(o, drive::loneliness) >= kHalf; }
// No battery: the phone is his only clock, so after half a day of play without
// one he asks for it, rarely, in his own voice rather than a nagging banner.
bool phone_long_gone(const Observed& o, uint16_t&) { return awake(o) && o.phone.awayTicks >= Thinker::kMissPhoneTicks; }
// What a phone adds. Each holds only once the phone has given it.
bool wall_hour(const Observed& o, uint16_t& num) {
  const std::optional<uint32_t> h = hourNow(o);
  if (!h) return false;
  num = uint16_t(*h % 12 ? *h % 12 : 12);
  return awake(o);
}
bool wall_morning(const Observed& o, uint16_t&) {
  const std::optional<uint32_t> h = hourNow(o);
  return awake(o) && h && *h >= 6 && *h < 10;
}
bool wall_late(const Observed& o, uint16_t&) {
  const std::optional<uint32_t> h = hourNow(o);
  return awake(o) && h && *h < 5;
}
bool wall_monday(const Observed& o, uint16_t&) { return awake(o) && weekdayNow(o) == 1u; }
bool wall_weekend(const Observed& o, uint16_t&) {
  const std::optional<uint32_t> d = weekdayNow(o);
  return awake(o) && d && (*d == 0 || *d == 6);
}
bool wall_friday(const Observed& o, uint16_t&) { return weekdayNow(o) == 5u; }
bool named_awake(const Observed& o, uint16_t&) { return awake(o) && o.phone.named; }
bool named(const Observed& o, uint16_t&) { return o.phone.named; }
bool taught(const Observed& o, uint16_t&) { return awake(o) && o.phone.taught && o.phone.taught->count > 0; }
bool elder(const Observed& o, uint16_t&) { return awake(o) && o.c.stage() == Stage::Elder; }
bool old(const Observed& o, uint16_t&) { return o.c.stage() == Stage::Elder; }
bool night(const Observed& o, uint16_t&) { return awake(o) && o.clock.night(); }
bool daytime(const Observed& o, uint16_t&) { return awake(o) && !o.clock.night(); }
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

size_t thoughtLine(ThoughtId id, uint16_t num, VoiceId voice, uint8_t pick, char* out, SpeciesId species,
                   const Words& words) {
  const ThoughtInfo* t = thoughtInfo(id);
  const char* text = t ? t->text : "";
  uint8_t mine = 0;
  for (const SpeciesLine& l : SPECIES_LINES) mine += l.thought == id && l.species == species;
  if (mine) {
    uint8_t want = uint8_t(pick % mine);
    for (const SpeciesLine& l : SPECIES_LINES)
      if (l.thought == id && l.species == species && want-- == 0) { text = l.text; break; }
    pick = uint8_t(pick / mine);   // so the frame is not tied to which line was drawn
  }
  uint8_t own = 0;
  if (!mine)
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
      if (*s == kNameMark || *s == kTaughtMark) {
        for (char c : *s == kNameMark ? words.name : words.taught) put(c);
        continue;
      }
      if (*s != kNumberMark) { put(*s); continue; }
      char digits[5];
      int k = 0;
      uint16_t rest = num;
      do { digits[k++] = char('0' + rest % 10); rest = uint16_t(rest / 10); } while (rest);
      while (k) put(digits[--k]);
  }
  // A terse voice stops at the first sentence's end (an ellipsis is not one),
  // but says what the owner taught him whole.
  if (v && v->firstOnly && !own && !std::strchr(text, kTaughtMark))
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

size_t toFont(std::string_view in, char* out, size_t cap) {
  size_t n = 0;
  for (char c : in) {
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (!inFont(c) || (c == ' ' && n == 0)) continue;
    if (n == cap) break;
    out[n++] = c;
  }
  while (n && out[n - 1] == ' ') --n;
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

// The ball's half of a foretelling: short, decisive, in the font, and done.
// Half of all foretellings (but never a day's first -- that one is a fortune)
// are one of these instead of an omen: the oracle still picks which omen
// would have carried it, and that row takes the cooldown. The ball says yes
// more than no, and sometimes refuses to say: 45 / 30 / 25.
struct BallAnswer { const char* text; uint8_t kind; };   // kind: 0 yes, 1 no, 2 not yet
constexpr BallAnswer kVerdicts[] = {
    {"YES.", 0}, {"YES. DEFINITELY.", 0}, {"SIGNS POINT TO YES.", 0}, {"COUNT ON IT.", 0},
    {"NO.", 1}, {"NO WAY.", 1}, {"MY ANSWER IS NO.", 1}, {"DO NOT COUNT ON IT.", 1},
    {"MAYBE.", 2}, {"CANNOT SAY YET.", 2}, {"THE MIST IS THICK.", 2}, {"ASK AGAIN LATER.", 2},
};
const char* drawVerdict(Rng& rng) {
  const uint32_t roll = rng.below(100);
  const uint8_t kind = roll < 45 ? 0 : roll < 75 ? 1 : 2;
  const char* pool[4];
  uint8_t n = 0;
  for (const BallAnswer& v : kVerdicts)
    if (v.kind == kind) pool[n++] = v.text;
  return pool[rng.below(n)];
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

void Thinker::start(const Creature& c, const Lineage& lineage, const FromPhone& phone, ThoughtPick pick, uint32_t tick,
                    Rng& rng, const char* verdict) {
  const ThoughtInfo* info = thoughtInfo(pick.id);
  Shown s{info->prophecy() ? Spoken::prophecy : Spoken::thought, pick.id, tick, 0, {}};
  if (verdict) {   // the ball's text stands in for the omen's; the row still takes its cooldown
    const size_t len = textLen(verdict);
    for (size_t i = 0; i < len; ++i) s.line[i] = verdict[i];
    s.ticks = passTicks(len);
    now_ = s;
    readyAt_[info - THOUGHTS] = tick + uint32_t(info->cooldownS) * (1000 / kTickMs);
    return;
  }
  const uint8_t frame = uint8_t(rng.below(256));
  char name[kMaxNameText + 1]{};
  Words words{std::string_view(name, nameInFont(lineage.currentName(), name)), {}};
  if (phone.taught && phone.taught->count) words.taught = phone.taught->line(uint8_t(rng.below(phone.taught->count)));
  const size_t len = thoughtLine(pick.id, pick.num, c.phenotype().oracle.voice, frame, s.line,
                                 c.phenotype().species.body, words);
  s.ticks = passTicks(len);
  now_ = s;
  readyAt_[info - THOUGHTS] = tick + uint32_t(info->cooldownS) * (1000 / kTickMs);
}

void Thinker::step(const Creature& c, const Habitat& habitat, const PetClock& clock, const Lineage& lineage,
                   uint32_t tick, bool shook, const FromPhone& phone) {
  if (c.genome().hash() != life_ || c.generation() != lifeGeneration_) meet(c, lineage, tick);
  if (c.stats().ageTicks < kWarmUpTicks) return;
  const Observed o{c, habitat, clock, heirlooms_, tick, phone};
  // A foretelling lands when its trance ends. In the trance he is silent:
  // the line he was saying is already cut and no thought starts. The phone's
  // said line is the one thing that dissolves a trance instead.
  if (trance_ && !shook) {
    if (now_ && now_->kind == Spoken::said) {
      trance_.reset();
    } else if (before(tick, trance_->until)) {
      return;
    } else {
      const Trance t = *trance_;
      trance_.reset();
      Rng rng = seededFor(life_, tick, kOracleSalt);
      return start(c, lineage, phone, t.pick, tick, rng, t.verdict);
    }
  }
  // Every shake foretells, after a trance: he goes quiet and swirly first,
  // like the ball filling with ink, and the answer lands whole. A new shake
  // restarts the trance rather than queueing behind it. With every row on
  // cooldown he repeats one. The phone's said line is the one thing a shake
  // does not cut short.
  if (shook && (!now_ || now_->kind != Spoken::said)) {
    Rng rng = seededFor(life_, tick, kOracleSalt);
    std::optional<ThoughtPick> p = chooseProphecy(o, readyAt_, rng);
    if (!p) p = chooseProphecy(o, nullptr, rng);
    // The day's first foretelling is a fortune, never a bare verdict; the
    // rest are the ball's coin flip between a verdict and an omen.
    const uint32_t day = clock.petTicks / PetClock::kDayTicks;
    const bool fortune = day != fortuneDay_;
    const bool verdict = !fortune && rng.below(2) == 0;
    if (p || verdict) {
      if (!p)   // a verdict still needs a row to carry its cooldown
        for (const ThoughtInfo& t : THOUGHTS)
          if (t.prophecy()) { p = ThoughtPick{t.id, 0}; break; }
      if (p) {
        if (fortune) fortuneDay_ = day;
        now_.reset();
        trance_ = Trance{tick + kTranceTicks, *p, verdict ? drawVerdict(rng) : nullptr};
        return;
      }
    }
  }
  if (now_) {
    if (tick - now_->since < now_->ticks) return;
    // An urgent line, a prophecy or the phone's line is an interjection: the
    // gap it cut short still runs, so it cannot crowd out what he was waiting to say.
    const bool interjection = now_->kind != Spoken::thought || thoughtInfo(now_->id)->priority >= kUrgentThought;
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
  start(c, lineage, phone, ThoughtPick{THOUGHTS[top[pick]].id, nums[pick]}, tick, rng);
}

void Thinker::force(ThoughtId id, const Creature& c, const Habitat& habitat, const PetClock& clock,
                    const Lineage& lineage, uint32_t tick, const FromPhone& phone) {
  const ThoughtInfo* info = thoughtInfo(id);
  if (!info || (startsWith(info->whenName, "taught") && !(phone.taught && phone.taught->count))) return;
  if (c.genome().hash() != life_ || c.generation() != lifeGeneration_) meet(c, lineage, tick);
  uint16_t num = 0;
  info->when(Observed{c, habitat, clock, heirlooms_, tick, phone}, num);
  Rng rng = seededFor(life_, tick);
  trance_.reset();
  start(c, lineage, phone, ThoughtPick{id, num}, tick, rng);
}

void Thinker::say(std::string_view line, const Creature& c, const Lineage& lineage, uint32_t tick) {
  if (c.genome().hash() != life_ || c.generation() != lifeGeneration_) meet(c, lineage, tick);
  const size_t n = std::min(line.size(), kMaxSaidText);
  trance_.reset();
  Shown s{Spoken::said, ThoughtId{}, tick, passTicks(n), {}};
  std::copy(line.begin(), line.begin() + n, s.line);
  now_ = s;
}

void Thinker::show(Appearance& a, uint32_t tick) const {
  if (trance_ && !now_) {   // in the trance: the foresee face, and no words yet
    a.prophecy = true;
    if (a.expression != expr::foresee) a.previous = a.expression;
    a.expression = expr::foresee;
    a.intensity = Fx::one();
    a.exprTicks = 0;
    return;
  }
  if (!now_ || tick - now_->since >= now_->ticks) return;
  a.thinking = true;
  a.prophecy = now_->kind == Spoken::prophecy;
  a.thought = now_->id;
  std::copy(std::begin(now_->line), std::end(now_->line), a.line);
  a.thoughtPhase = Fx::ratio(int32_t(tick - now_->since), now_->ticks);
  if (now_->kind == Spoken::thought) return;
  const ExprId face = now_->kind == Spoken::said ? expr::croak : expr::foresee;
  const uint16_t since = uint16_t(std::min<uint32_t>(UINT16_MAX, tick - now_->since));
  if (a.expression != face) a.previous = a.expression;
  a.expression = face;
  a.intensity = Fx::one();
  a.exprTicks = since;
  if (now_->kind == Spoken::said) return;
  // The seer at work: the foresee glow, and no hop for the shake that asked.
  a.foreseeing = true;
  a.glow = fxMax(a.glow, kForeseeGlowFloor);
  if (a.reflexActive && a.reflex == reflex::hop) a.reflexActive = false;
}

}  // namespace blorb
