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

// A newer firmware's keepsake must not be written, so the throwaway life it
// runs keeps its lineage nowhere.
struct NullStorage : Storage {
  std::optional<size_t> read(const char*, uint8_t*, size_t) override { return std::nullopt; }
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
    : store_(store), keep_(store), occ_(Clutch{}), rtc_(options.rtc), rng_(Rng::seeded(speciesSeed)) {
  const Genome founder = options.founder ? *options.founder : starterGenome(speciesSeed);
  settings_.lineageId = lineageId;
  settings_.speciesSeed = speciesSeed;
  settings_.brightness = kDefaultBrightness;
  Loaded loaded = keep_.load();
  if (loaded.state == SlotState::NewerFormat) {
    boot_ = Boot::ReadOnlyNewer;
    readOnly_ = true;
    lineage_ = Lineage::open(nowhere(), founder, lineageId, 0);
    copyName(settings_.name, lineage_.currentName());
    occ_ = Egg(Offspring{founder, {}}, 0, 0);
    return;
  }
  lineage_ = Lineage::open(store, founder, lineageId, 0);
  if (loaded.snapshot) {
    adopt(std::move(*loaded.snapshot));
    boot_ = loaded.state == SlotState::FellBack ? Boot::FellBack : Boot::Resumed;
    return;
  }
  // No readable snapshot: the line goes on from the newest genome the log can rebuild.
  uint16_t generation = lineage_.currentGeneration();
  std::optional<Genome> latest = lineage_.genomeOf(generation);
  bool history = generation > 0 || lineage_.legacyFeats() != 0;
  boot_ = loaded.state == SlotState::Corrupt || history ? Boot::FromLineage : Boot::Fresh;
  copyName(settings_.name, lineage_.currentName());
  occ_ = latest ? Egg(Offspring{std::move(*latest), {}}, generation, 0) : Egg(Offspring{founder, {}}, 0, 0);
  save();
}

void Dish::adopt(Snapshot&& s) {
  clock_ = s.clock;
  occ_ = std::move(s.occupant);
  habitat_ = s.habitat;
  settings_ = s.settings;
  rng_ = s.rng;
  wall_ = s.wall;
  unknown_ = std::move(s.unknown);
  tick_ = clock_.petTicks;
  lastSaveTick_ = tick_;
  lastWallCheckTick_ = tick_;
}

void Dish::sample(const BodySample& s, uint32_t nowMs) { detectors_.sample(s, nowMs, pending_); }

void Dish::tick(uint32_t nowMs, Link& link) {
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
  clock_.advance(detectors_.lid.lidded());
  detectors_.tick(TickContext{clock_, link.connected(), tick_}, out);
  for (uint8_t i = 0; i < out.lociCount; ++i) {
    if (out.loci[i].locus == locus::tilt_x) tiltX_ = out.loci[i].value;
    if (out.loci[i].locus == locus::tilt_y) tiltY_ = out.loci[i].value;
  }
  if (Creature* c = std::get_if<Creature>(&occ_)) {
    const HabitatRules& rules = c->phenotype().habitat;
    if (fired(out, stim::button)) habitat_.dropPellet(rules, rng_, tick_, out);
    habitat_.step(rules, tiltX_, tiltY_, c->body().at, tick_, out);
    Stage before = c->stage();
    c->tick(out, habitat_, behaviours_, tick_);
    if (c->stage() != before) eventDirty_ = true;
    if (c->dead()) onDeath(*c);
  } else if (Egg* e = std::get_if<Egg>(&occ_)) {
    if (e->tick(out)) onHatch(*e);
  } else {
    Clutch& k = std::get<Clutch>(occ_);
    k.derivePreviewStep();
    if (std::optional<uint8_t> i = k.tick(out)) onPicked(k, *i);
  }
  ++tick_;
}

void Dish::onDeath(Creature& c) {
  lineage_.recordDeath(c, lineage_.currentName(), tick_);
  Unlocks u = unlocksFor(lineage_.legacyFeats());
  Clutch k = c.layClutch(u.clutchSize, u.wildBonus, tick_);
  occ_ = std::move(k);
  eventDirty_ = true;
}

void Dish::onPicked(Clutch& k, uint8_t i) {
  Egg egg(k.child(i), k.generation, tick_);
  lineage_.recordBirth(egg, k, i, tick_);
  occ_ = std::move(egg);
  eventDirty_ = true;
}

void Dish::onHatch(Egg& e) {
  Creature born = Creature::hatch(e, lineage_.legacyFeats(), tick_);
  habitat_ = Habitat{};
  habitat_.pantry = born.phenotype().habitat.pantrySize;
  habitat_.pantryTick = tick_;
  occ_ = std::move(born);
  eventDirty_ = true;
}

bool Dish::pick(uint8_t egg) {
  Clutch* k = std::get_if<Clutch>(&occ_);
  if (!k || egg >= k->count) return false;
  onPicked(*k, egg);
  return true;
}

void Dish::fire(StimId s) { pending_.fire(s); }

bool Dish::editGene(GeneUid uid, uint8_t offset, uint8_t value) {
  Creature* c = std::get_if<Creature>(&occ_);
  if (!c || !c->editGene(uid, offset, value)) return false;
  lineage_.recordEdit(c->generation(), c->genome());
  eventDirty_ = true;
  return true;
}

void Dish::rename(const char* name) {
  lineage_.rename(name);
  copyName(settings_.name, lineage_.currentName());
  eventDirty_ = true;
}

void Dish::flush() {
  if (!readOnly_) save();
}

void Dish::save() {
  if (keep_.save(snapshot())) lastSaveTick_ = tick_;
  eventDirty_ = false;
}

Snapshot Dish::snapshot() const {
  return Snapshot{0, clock_, occ_, habitat_, settings_, rng_, wall_, unknown_};
}

uint32_t Dish::hash() const { return snapshot().hash(); }

Appearance Dish::appearance() const {
  Appearance a = present(occ_, habitat_, clock_, tick_);
  a.timeUnknown = !wallKnown_;
  return a;
}

// ---- wall time and the unpowered catch-up --------------------------------------------

std::optional<uint32_t> Dish::wallNow() const {
  if (!wallKnown_ || !wall_) return std::nullopt;
  return wall_->wallSeconds + (tick_ - wall_->tick) / kTicksPerSecond;
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
  if (wall_) {
    int64_t powered = int64_t((tick_ - wall_->tick) / kTicksPerSecond);
    int64_t gap = int64_t(*now) - int64_t(wall_->wallSeconds) - powered;
    if (gap > -kDriftSeconds && gap < kDriftSeconds) return;
    // A source that runs backwards is believed from here on; only a forward gap is lived.
    if (gap > 0) catchUp(uint32_t(std::min<int64_t>(gap * kTicksPerSecond, UINT32_MAX)));
  }
  wall_ = WallAnchor{*now, tick_};
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
    if (Creature* c = std::get_if<Creature>(&occ_)) {
      advanceClock(n);
      SenseOut senses;
      detectors_.day.tick(TickContext{clock_, false, tick_}, senses);
      habitat_.step(c->phenotype().habitat, kLevel, kLevel, c->body().at, tick_ + n - 1, senses);
      c->tickCoarse(senses, n, tick_);
      tick_ += n;
      if (c->dead()) onDeath(*c);
    } else if (Egg* e = std::get_if<Egg>(&occ_)) {
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
      Clutch& k = std::get<Clutch>(occ_);
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
  Fx before = clock_.dayFraction();
  clock_.petTicks += ticks;
  clock_.darkRunTicks = 0;
  if (ticks >= PetClock::kDayTicks || clock_.dayFraction() < before) clock_.entrainedTicks = 0;
}

}  // namespace blorb
