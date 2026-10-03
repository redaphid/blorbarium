#include "blorb/dish.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace blorb {
namespace {

constexpr int kMaxTicksPerCall = 10;
constexpr uint32_t kTicksPerSecond = 1000 / kTickMs;
constexpr Fx kLevel = Fx::ratio(1, 2);
constexpr uint8_t kDefaultBrightness = 200;

// The unpowered catch-up (DEVIATIONS.md 3): chemistry at the dry run's
// five-minute stride, at most 30 pet days of it per catch-up (about 8,600
// coarse steps, under a second on the device by the dry run's estimate).
constexpr uint32_t kCatchUpStride = 5 * kTicksPerMinute;
constexpr uint32_t kMaxCatchUpSteps = 30 * 24 * 12;
// A source within this of the anchor is clock drift, not an unpowered gap.
constexpr int64_t kDriftSeconds = 60;
// Once wall time is known, the sources are read again this often.
constexpr uint32_t kWallCheckTicks = kTicksPerMinute;

// A new life's marble starts on a seeded spot this far from the hatchling and
// no further than kMarbleStartMax from the centre, so it never sits under him.
constexpr Fx kMarbleClear = Fx::ratio(45, 100);
constexpr Fx kMarbleStartMax = Fx::ratio(70, 100);

DishPos marbleStart(Rng& rng, DishPos him) {
  for (;;) {
    DishPos p{kMarbleStartMax * (rng.unit() + rng.unit() - Fx::one()), kMarbleStartMax * (rng.unit() + rng.unit() - Fx::one())};
    Fx dx = p.x - him.x, dy = p.y - him.y;
    if (p.x * p.x + p.y * p.y <= kMarbleStartMax * kMarbleStartMax && dx * dx + dy * dy >= kMarbleClear * kMarbleClear)
      return p;
  }
}

// A newer firmware's keepsake must not be written, so the throwaway life it
// runs keeps its lineage nowhere.
struct NullStorage : Storage {
  std::optional<size_t> read(const char*, size_t, uint8_t*, size_t) override { return std::nullopt; }
  bool writeAtomic(const char*, const uint8_t*, size_t) override { return false; }
  bool append(const char*, const uint8_t*, size_t) override { return false; }
  bool rename(const char*, const char*) override { return false; }
  size_t size(const char*) override { return 0; }
};

Storage& nowhere() {
  static NullStorage none;
  return none;
}

bool fired(const SenseOut& s, StimId id) {
  for (uint8_t i = 0; i < s.stimCount; ++i) if (s.stimuli[i] == id) return true;
  return false;
}

void copyName(char (&to)[16], const char* from) {
  std::memset(to, 0, sizeof to);
  for (size_t i = 0; i + 1 < sizeof to && from[i]; ++i) to[i] = from[i];
}

}  // namespace

Dish::Dish(Storage& store, uint32_t speciesSeed, uint64_t lineageId, DishOptions options)
    : store_(store), keep_(store), rtc_(options.rtc) {
  const Genome founder = options.founder ? *options.founder : starterGenome(speciesSeed);
  live_.rng = Rng::seeded(speciesSeed);
  live_.settings.lineageId = lineageId;
  live_.settings.speciesSeed = speciesSeed;
  live_.settings.brightness = kDefaultBrightness;
  const SlotState loaded = keep_.load(live_);
  if (loaded == SlotState::NewerFormat) {
    boot_ = Boot::ReadOnlyNewer;
    readOnly_ = true;
    lineage_ = Lineage::open(nowhere(), founder, lineageId, 0);
    copyName(live_.settings.name, lineage_.currentName());
    live_.occupant = Egg(Offspring{founder, {}}, 0, 0);
    return;
  }
  lineage_ = Lineage::open(store, founder, lineageId, 0);
  if (loaded == SlotState::Resumed || loaded == SlotState::FellBack) {
    live_.seq = 0;
    tick_ = lastSaveTick_ = lastWallCheckTick_ = live_.clock.petTicks;
    boot_ = loaded == SlotState::FellBack ? Boot::FellBack : Boot::Resumed;
    return;
  }
  // No readable snapshot: the line goes on from the newest genome the log can rebuild.
  uint16_t generation = lineage_.currentGeneration();
  std::optional<Genome> latest = lineage_.genomeOf(generation);
  bool history = generation > 0 || lineage_.legacyFeats() != 0;
  boot_ = loaded == SlotState::Corrupt || history ? Boot::FromLineage : Boot::Fresh;
  copyName(live_.settings.name, lineage_.currentName());
  live_.occupant = latest ? Egg(Offspring{std::move(*latest), {}}, generation, 0) : Egg(Offspring{founder, {}}, 0, 0);
  save();
}

void Dish::sample(const BodySample& s, uint32_t nowMs) { detectors_.sample(s, nowMs, pending_); }

void Dish::tick(uint32_t nowMs, Link& link) {
  nowMs_ = nowMs;
  for (int n = 0; n < kMaxTicksPerCall && nowMs - lastTickMs_ >= kTickMs; ++n) {
    lastTickMs_ += kTickMs;
    runOneTick(link);
  }
  while (std::optional<std::string_view> line = link.readLine()) proto_.handle(*line, *this, link);
  proto_.pump(*this, link, tick_);
  bool connected = link.connected();
  if (wasConnected_ && !connected) {
    proto_.disconnected();
    eventDirty_ = true;
  }
  wasConnected_ = connected;
  if (!wallKnown_ || tick_ - lastWallCheckTick_ >= kWallCheckTicks) checkWall();
  if (!readOnly_ && SavePolicy::due(tick_, lastSaveTick_, eventDirty_)) save();
}

// DESIGN.md section 3, in this order and no other.
void Dish::runOneTick(Link& link) {
  SenseOut out = pending_;
  pending_.clear();
  live_.clock.advance(detectors_.lid.lidded());
  detectors_.tick(TickContext{live_.clock, link.connected(), tick_}, out);
  for (uint8_t i = 0; i < out.lociCount; ++i) {
    if (out.loci[i].locus == locus::tilt_x) tiltX_ = out.loci[i].value;
    if (out.loci[i].locus == locus::tilt_y) tiltY_ = out.loci[i].value;
  }
  if (Creature* c = std::get_if<Creature>(&live_.occupant)) {
    const HabitatRules& rules = c->phenotype().habitat;
    if (fired(out, stim::button)) live_.habitat.dropPellet(rules, live_.rng, tick_, out);
    live_.habitat.step(rules, tiltX_, tiltY_, c->body().at, tick_, out);
    Stage before = c->stage();
    c->tick(out, live_.habitat, behaviours_, tick_);
    if (c->stage() != before) eventDirty_ = true;
    if (c->dead()) onDeath(*c);
    else if (wallKnown_) thinker_.step(*c, live_.habitat, live_.clock, lineage_, tick_, fired(out, stim::shake));
  } else if (Egg* e = std::get_if<Egg>(&live_.occupant)) {
    if (e->tick(out)) onHatch(*e);
  } else {
    Clutch& k = std::get<Clutch>(live_.occupant);
    k.derivePreviewStep();
    if (std::optional<uint8_t> i = k.tick(out)) onPicked(k, *i);
  }
  ++tick_;
}

void Dish::onDeath(Creature& c) {
  lineage_.recordDeath(c, lineage_.currentName(), tick_);
  Unlocks u = unlocksFor(lineage_.legacyFeats());
  Clutch k = c.layClutch(u.clutchSize, u.wildBonus, tick_);
  live_.occupant = std::move(k);
  eventDirty_ = true;
}

void Dish::onPicked(Clutch& k, uint8_t i) {
  Egg egg(k.child(i), k.generation, tick_);
  lineage_.recordBirth(egg, k, i, tick_);
  live_.occupant = std::move(egg);
  eventDirty_ = true;
}

void Dish::onHatch(Egg& e) {
  const Egg egg = std::move(e);   // emplace ends the egg before it builds the creature
  const Creature& born = live_.occupant.emplace<Creature>(egg, lineage_.legacyFeats(), tick_);
  live_.habitat = Habitat{};
  live_.habitat.pantry = born.phenotype().habitat.pantrySize;
  live_.habitat.pantryTick = tick_;
  live_.habitat.marble.at = marbleStart(live_.rng, born.body().at);
  eventDirty_ = true;
}

bool Dish::pick(uint8_t egg) {
  Clutch* k = std::get_if<Clutch>(&live_.occupant);
  if (!k || egg >= k->count) return false;
  onPicked(*k, egg);
  return true;
}

void Dish::fire(StimId s) { pending_.fire(s); }

void Dish::think(ThoughtId id) {
  if (const Creature* c = std::get_if<Creature>(&live_.occupant))
    thinker_.force(id, *c, live_.habitat, live_.clock, lineage_, tick_);
}

bool Dish::editGene(GeneUid uid, uint8_t offset, uint8_t value) {
  Creature* c = std::get_if<Creature>(&live_.occupant);
  if (!c || !c->editGene(uid, offset, value)) return false;
  lineage_.recordEdit(c->generation(), c->genome());
  eventDirty_ = true;
  return true;
}

void Dish::rename(const char* name) {
  lineage_.rename(name);
  copyName(live_.settings.name, lineage_.currentName());
  eventDirty_ = true;
}

void Dish::flush() {
  if (!readOnly_) save();
}

void Dish::save() {
  if (!keep_.save(live_)) return;   // an event save stays urgent and retries on the next tick() call
  lastSaveTick_ = tick_;
  eventDirty_ = false;
}

namespace {
// Drawn at 25 fps but moved at 10 Hz, a fast marble would jump a sixth of the
// dish every fourth frame; it is drawn where its speed carries it since the
// tick. Only the picture moves: the habitat's state is untouched.
void projectMarble(Appearance& a, const Marble& m, uint32_t sinceTickMs) {
  const Fx t = Fx::ratio(int32_t(sinceTickMs < kTickMs ? sinceTickMs : kTickMs), int32_t(kTickMs));
  const DishPos ahead{m.at.x + m.vx * t, m.at.y + m.vy * t};
  if (ahead.x * ahead.x + ahead.y * ahead.y > Fx::ratio(92, 100) * Fx::ratio(92, 100)) return;
  for (uint8_t i = 0; i < a.itemCount; ++i)
    if (a.items[i].what == Appearance::Item::What::Marble) a.items[i].at = ahead;
}
}  // namespace

Appearance Dish::appearance() const {
  Appearance a = present(live_.occupant, live_.habitat, live_.clock, tick_);
  projectMarble(a, live_.habitat.marble, nowMs_ - lastTickMs_);
  a.timeUnknown = !wallKnown_;
  if (wallKnown_ && std::holds_alternative<Creature>(live_.occupant)) thinker_.show(a, tick_);
  return a;
}

// ---- wall time and the unpowered catch-up --------------------------------------------

std::optional<uint32_t> Dish::wallNow() const {
  if (!wallKnown_ || !live_.wall) return std::nullopt;
  return live_.wall->wallSeconds + (tick_ - live_.wall->tick) / kTicksPerSecond;
}

void Dish::checkWall() {
  lastWallCheckTick_ = tick_;
  std::optional<uint32_t> now;
  // Every source is read, so a phone reading behind a working RTC is spent, not kept for later.
  for (TimeSource* source : {rtc_, static_cast<TimeSource*>(&phone_)}) {
    if (!source) continue;
    std::optional<uint32_t> t = source->unixSeconds();
    if (!now) now = t;
  }
  if (!now) return;
  wallKnown_ = true;
  if (live_.wall) {
    int64_t powered = int64_t((tick_ - live_.wall->tick) / kTicksPerSecond);
    int64_t gap = int64_t(*now) - int64_t(live_.wall->wallSeconds) - powered;
    if (gap > -kDriftSeconds && gap < kDriftSeconds) return;
    // A source that runs backwards is believed from here on; only a forward gap is lived.
    if (gap > 0) catchUp(uint32_t(std::min<int64_t>(gap * kTicksPerSecond, UINT32_MAX)));
  }
  live_.wall = WallAnchor{*now, tick_};
  if (!readOnly_) save();
}

// Nobody is there: no gestures, no feeding, no brain. The chemistry runs
// coarse, stages express as their loci fire, the pantry refills, and a death
// takes the normal path. An egg incubates and hatches; a clutch's vigil runs
// to its end and the choice waits for the owner.
void Dish::catchUp(uint32_t ticks) {
  const uint64_t cap = uint64_t(kMaxCatchUpSteps) * kCatchUpStride;
  lastCatchUp_ = CatchUp{uint32_t(std::min<uint64_t>(ticks, cap)), uint32_t(ticks > cap ? ticks - cap : 0)};
  for (uint32_t left = lastCatchUp_.ticks; left > 0;) {
    uint32_t n = std::min(left, kCatchUpStride);
    if (Creature* c = std::get_if<Creature>(&live_.occupant)) {
      advanceClock(n);
      SenseOut senses;
      detectors_.day.tick(TickContext{live_.clock, false, tick_}, senses);
      live_.habitat.step(c->phenotype().habitat, kLevel, kLevel, c->body().at, tick_ + n - 1, senses);
      c->tickCoarse(senses, n, tick_);
      tick_ += n;
      if (c->dead()) onDeath(*c);
    } else if (Egg* e = std::get_if<Egg>(&live_.occupant)) {
      bool ready = false;
      uint32_t i = 0;
      while (i < n && !ready) {
        ready = e->tick(SenseOut{});
        ++i;
      }
      n = i;
      advanceClock(n);
      tick_ += n;
      if (ready) onHatch(*e);
    } else {
      Clutch& k = std::get<Clutch>(live_.occupant);
      n = left;
      if (k.sinceDeath < Clutch::kVigilTicks)
        k.sinceDeath = uint32_t(std::min<uint64_t>(Clutch::kVigilTicks, uint64_t(k.sinceDeath) + n));
      advanceClock(n);
      tick_ += n;
    }
    left -= n;
  }
}

// PetClock::advance n times, unlidded, without the loop: no entrainment, and
// the day's entrainment allowance resets if a pet midnight passed.
void Dish::advanceClock(uint32_t ticks) {
  Fx before = live_.clock.dayFraction();
  live_.clock.petTicks += ticks;
  live_.clock.darkRunTicks = 0;
  if (ticks >= PetClock::kDayTicks || live_.clock.dayFraction() < before) live_.clock.entrainedTicks = 0;
}

}  // namespace blorb
