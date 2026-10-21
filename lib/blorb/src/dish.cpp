#include "blorb/dish.h"

#include "blorb/controls.h"

#include <algorithm>
#include "blorb/founding.h"
#include "blorb/friend_egg.h"
#include <cstring>
#include <utility>

namespace blorb {
namespace {

constexpr int kMaxTicksPerCall = 10;
constexpr uint32_t kTicksPerSecond = 1000 / kTickMs;
constexpr Fx kLevel = Fx::ratio(1, 2);
constexpr uint8_t kDefaultBrightness = 200;
// A press for the eyes (a peek, a flower) or a refused duel shows this long.
constexpr uint32_t kEchoTicks = 3 * 1000 / kTickMs;
constexpr uint32_t kNoFightTicks = 5 * 1000 / kTickMs;   // a refused fight's reason stays up this long

// The unpowered catch-up (DEVIATIONS.md 3): chemistry at the dry run's
// five-minute stride, at most 12 hours of it per catch-up. The boards have no
// battery, so the only clock after a replug is a phone's TIME: an uncapped gap
// would make connecting a phone, not neglect, the thing that kills him. Twelve
// hours is a night in a drawer; a pet already near death still dies inside it.
constexpr uint32_t kCatchUpStride = 5 * kTicksPerMinute;
constexpr uint32_t kMaxCatchUpSteps = 12 * 12;
// The cap must not make abandonment free: a gap this long also lands as harm
// the catch-up's twelve hours cannot show.
constexpr int64_t kLongAbsenceSeconds = 21 * 24 * 3600;
// The glow a welcomed phone leaves, the size of the phone's prophecy treat.
constexpr Fx kWelcomeVision = Fx::ratio(1, 2);

// He wakes starving (hunger above the kStarving locus, energy low enough to
// keep calling it up) and hurt; feeding saves him, neglect carries on from
// here. wound() stops short of lethal. Twelve hours unfed often leaves him
// worse than these levels already, and the harm must never ease him.
void longAbsence(Creature& c) {
  const Chemistry& now = c.chemistry();
  c.inject(driveChem(drive::hunger), fxMax(now.drive(drive::hunger), Fx::ratio(9, 10)));
  c.inject(chem::energy, fxMin(now.chem[chem::energy.v], Fx::ratio(2, 10)));
  c.wound(Fx::ratio(1, 2));
}

// A source within this of the anchor is clock drift, not an unpowered gap.
constexpr int64_t kDriftSeconds = 60;
// Once wall time is known, the sources are read again this often.
constexpr uint32_t kWallCheckTicks = kTicksPerMinute;

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

namespace {
constexpr const char* kAdoptedFile = "adopted.flag";   // beside the keepsake, never in the save or its hash
}

Dish::Dish(Storage& store, uint32_t speciesSeed, uint64_t lineageId, DishOptions options)
    : store_(store), keep_(store), rtc_(options.rtc), air_(options.air), hatchNeedsPhone_(options.hatchNeedsPhone) {
  const Genome founder = options.founder ? *options.founder : starterGenome(speciesSeed);
  live_.rng = Rng::seeded(speciesSeed);
  live_.settings.lineageId = lineageId;
  live_.settings.speciesSeed = speciesSeed;
  live_.settings.brightness = kDefaultBrightness;
  // A sealed board does not even load: a load quarantines a torn slot, and that is a write.
  const SlotState loaded = options.sealed ? SlotState::Empty : keep_.load(live_);
  if (options.sealed || loaded == SlotState::NewerFormat) {
    boot_ = options.sealed ? Boot::Sealed : Boot::ReadOnlyNewer;
    readOnly_ = true;
    lineage_ = Lineage::open(nowhere(), founder, lineageId, 0);
    copyName(live_.settings.name, lineage_.currentName());
    live_.occupant = Egg(Offspring{founder, {}}, 0, 0);
    return;
  }
  lineage_ = Lineage::open(store, founder, lineageId, 0);
  duel_.attach(store);
  taught_ = Taught::load(store);
  adopted_ = store.size(kAdoptedFile) > 0;
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
  live_.clock.phaseOffsetTicks = int32_t(uint32_t(options.foundingHour % 24) * kTicksPerHour);
  copyName(live_.settings.name, lineage_.currentName());
  live_.occupant = latest ? Egg(Offspring{std::move(*latest), {}}, generation, 0) : Egg(Offspring{founder, {}}, 0, 0);
  save();
}

void Dish::sample(const BodySample& s, uint32_t nowMs) {
  const uint8_t before = pending_.stimCount;
  detectors_.sample(s, nowMs, pending_);
  // Each press, double and hold the detector fires, as it fires: the press it
  // ends was measured from BOOT's raw level by the samples before this one.
  lastFired_ = 0;
  if (int32_t(nowMs - nowMs_) > 0) nowMs_ = nowMs;
  if (s.knockWaiting && !knockWaitWas_) knockAim_ = detectors_.aim.aim();   // the first knock: what it aims at
  knockWaitWas_ = s.knockWaiting;
  for (uint8_t i = before; i < pending_.stimCount; ++i) {
    const StimId f = pending_.stimuli[i];
    if (f.v < 64) lastFired_ |= uint64_t(1) << f.v;
    const char* what = f == stim::button ? "press" : f == stim::button_double ? "double" : nullptr;
    // The twins (controls.h) say which gesture spoke, not BOOT.
    if (f == stim::button && s.tapCode == 2 && !s.buttonDown && !cues_.buttonDown()) diag_.say("gesture knock_knock=press");
    else if (f == stim::button_hold && !cues_.buttonDown() && detectors_.side.holding())
      diag_.say("gesture side=hold ms=%u", unsigned(kSideHoldMs));
    else if (f == stim::button_hold) diag_.say("boot hold ms=%u", unsigned(kButtonHoldMs));
    else if (what && cues_.lastGapMs() == UINT32_MAX) diag_.say("boot %s ms=%u gap=none", what, unsigned(cues_.lastPressMs()));
    else if (what) diag_.say("boot %s ms=%u gap=%u", what, unsigned(cues_.lastPressMs()), unsigned(cues_.lastGapMs()));
  }
  cues_.sample(s, detectors_.side.holding(), nowMs);
  feed_.sample(s, (lastFired_ >> stim::button.v) & 1, nowMs);
}

void Dish::tick(uint32_t nowMs, Link& link) {
  if (int32_t(nowMs - nowMs_) > 0) nowMs_ = nowMs;
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
  if (sealed()) {   // no life to run: the pet is the one on the flash
    ++tick_;
    return;
  }
  live_.clock.advance(detectors_.lid.lidded());
  detectors_.tick(TickContext{live_.clock, link.connected(), tick_}, out);
  for (uint8_t i = 0; i < out.lociCount; ++i) {
    if (out.loci[i].locus == locus::tilt_x) tiltX_ = out.loci[i].value;
    if (out.loci[i].locus == locus::tilt_y) tiltY_ = out.loci[i].value;
  }
  routeBoot(bootContext(), out);
  const uint16_t petDay = uint16_t(live_.clock.petTicks / PetClock::kDayTicks);
  const bool side = kSideHoldHolds && detectors_.side.holding();
  // The duel reads the aim (AimDetector) as the tilt at its sector's middle,
  // so DuelInput::from finds the same sector; a knock locks the aim it was
  // made at, not where the hand drifted while it was decided.
  const bool knocked = fired(out, stim::knock) || fired(out, stim::double_knock);
  const int8_t aim = knocked && knockAim_ >= 0 ? knockAim_ : detectors_.aim.aim();
  constexpr Fx kMid = Fx::ratio(1, 2), kLean = Fx::ratio(1, 4);
  const Fx tiltX = aim == 0 ? kMid + kLean : aim == 1 ? kMid - kLean : kMid;
  const Fx tiltY = aim == 2 ? kMid - kLean : aim == 3 ? kMid + kLean : kMid;
  if (!histPrimed_) {
    histPrimed_ = true;
    for (Fx* t : tiltHist_) t[0] = tiltX, t[1] = tiltY;
  }
  if (side && !sideWas_) {
    const Fx* before = tiltHist_[(tick_ - kAimBeforeTurn) % kAimHistory];
    sideTilt_[0] = before[0], sideTilt_[1] = before[1];
  }
  sideWas_ = side;
  tiltHist_[tick_ % kAimHistory][0] = tiltX, tiltHist_[tick_ % kAimHistory][1] = tiltY;
  const Fx aimX = side ? sideTilt_[0] : tiltX, aimY = side ? sideTilt_[1] : tiltY;
  if (duel_.step(live_.occupant, lineage_, air_, out, aimX, aimY, cues_.holding(), live_.settings.lineageId, petDay, tick_)
          .save)
    save();
  if (Creature* c = std::get_if<Creature>(&live_.occupant)) {
    const HabitatRules& rules = c->phenotype().habitat;
    if (fired(out, stim::button)) {
      const bool dropped = live_.habitat.dropPellet(rules, live_.rng, tick_, out);
      feed_.pressed(lastTickMs_, dropped, tick_, c->body().asleep, c->chemistry().drive(drive::hunger));
    }
    // Face down is bedtime, always (owner, 2026-10-19): the lid puts him to
    // sleep whatever his genes make of it, unless a duel holds him.
    if (fired(out, stim::lid_down) && !(out.remote & duelStimBit(stim::lid_down)) && !c->body().asleep &&
        !duel_.active())
      c->force(action::sleep);
    c->keepAsleep(detectors_.lid.lidded() && !duel_.active());
    live_.habitat.step(rules, tiltX_, tiltY_, c->body().at, tick_, out);
    Stage before = c->stage();
    const Mouthful mouth = c->body().mouth;
    const Fx hunger = c->chemistry().drive(drive::hunger);
    c->tick(out, live_.habitat, behaviours_, tick_);
    if (mouth == Mouthful::Nothing && c->body().mouth != Mouthful::Nothing)   // a bite, for the eyes
      feed_.ate(lastTickMs_, c->body().mouth == Mouthful::RottenPellet, hunger);
    if (c->stage() != before) eventDirty_ = true;
    if (c->dead()) onDeath(*c);
    else thinker_.step(*c, live_.habitat, live_.clock, lineage_, tick_, fired(out, stim::shake), fromPhone());
  } else if (Egg* e = std::get_if<Egg>(&live_.occupant)) {
    if (!hatchWaits() && e->tick(out)) onHatch(*e);
  } else {
    Clutch& k = std::get<Clutch>(live_.occupant);
    if (k.previewed < k.count) {
      const std::optional<FriendEgg> egg = k.friendSlot ? FriendEggStore::load(store_) : std::nullopt;
      k.derivePreviewStep(egg ? &*egg : nullptr);
    }
    if (std::optional<uint8_t> i = k.tick(out)) onPicked(k, *i);
  }
  ++tick_;
}

const char* Dish::fightState() const {
  if (!duel_.active()) return duel_.pending() ? "waiting" : "off";
  return duel_.searching() ? "searching" : "paired";
}

BootContext Dish::bootContext() const {
  if (sealed()) return BootContext::Sealed;
  const DuelView v = duel_.active() ? duel_.view(tick_) : DuelView{};
  return blorb::bootContext(live_.occupant, duel_.active() ? &v : nullptr);
}

// kBoot's rows outside a duel (inside one, the session routes the button).
// A double press where it means nothing is a press again, so tapping through a
// clutch is not slowed; OpenDuel is DishDuel's. A press for the eyes only is
// remembered for the screen.
void Dish::routeBoot(BootContext ctx, SenseOut& out) {
  if (duel_.active()) return;
  const BootMeaning& m = bootMeaning(ctx);
  if (m.doublePress == BootAct::Nothing && fired(out, stim::button_double)) {
    const bool remote = out.remote & duelStimBit(stim::button_double);
    // A kid asking for a fight where there is no pet to fight is told why.
    if (!remote && !sealed()) {
      if (ctx == BootContext::Egg) duel_.refuse(NoFight::Egg, tick_);
      else if (ctx == BootContext::FoundingNest || ctx == BootContext::ClutchPick) duel_.refuse(NoFight::Nest, tick_);
      else if (ctx == BootContext::Vigil) duel_.refuse(NoFight::Remains, tick_);
    }
    uint8_t kept = 0;
    for (uint8_t i = 0; i < out.stimCount; ++i)
      if (out.stimuli[i] != stim::button_double) out.stimuli[kept++] = out.stimuli[i];
    out.stimCount = kept;
    out.remote &= ~duelStimBit(stim::button_double);
    if (remote) out.fireRemote(stim::button);
    else out.fire(stim::button);
  }
  // A hang is the double press's twin, never a press: it is not turned into
  // a pellet here, and where a fight is refused it is refused in words, like
  // a double press. Where a double press means OpenDuel (Awake, Asleep) a
  // body hang stays in `out` for DishDuel to open with; everywhere else, and
  // whenever a phone fired it, it leaves `out` here.
  if (fired(out, stim::hang)) {
    const bool remote = (out.remote & duelStimBit(stim::hang)) != 0;
    if (!(m.doublePress == BootAct::OpenDuel && !remote)) {
      if (!remote && !sealed()) {
        if (ctx == BootContext::Egg) duel_.refuse(NoFight::Egg, tick_, nullptr, true);
        else if (ctx == BootContext::FoundingNest || ctx == BootContext::ClutchPick)
          duel_.refuse(NoFight::Nest, tick_, nullptr, true);
        else if (ctx == BootContext::Vigil) duel_.refuse(NoFight::Remains, tick_, nullptr, true);
      }
      uint8_t kept = 0;
      for (uint8_t i = 0; i < out.stimCount; ++i)
        if (out.stimuli[i] != stim::hang) out.stimuli[kept++] = out.stimuli[i];
      out.stimCount = kept;
      out.remote &= ~duelStimBit(stim::hang);
    }
  }
  if ((m.press == BootAct::PeekEgg || m.press == BootAct::LayFlower) && fired(out, stim::button)) {
    echo_ = m.press;
    echoTick_ = tick_;
  }
}

void Dish::onDeath(Creature& c) {
  lineage_.recordDeath(c, lineage_.currentName(), tick_, duel_.book().featsThisLife(c.generation()));
  Unlocks u = unlocksFor(lineage_.legacyFeats());
  Clutch k = c.layClutch(u.clutchSize, u.wildBonus, tick_);
  // A hug's egg waits in friend.egg for this clutch (friend_egg.h).
  if (std::optional<FriendEgg> egg = FriendEggStore::load(store_)) joinClutch(k, egg->record.battleSeed);
  live_.occupant = std::move(k);
  eventDirty_ = true;
}

// friend.egg is consumed before the Birth is written, so a power cut at any
// step can lose a friend egg but never hatch it twice. A consume refused (an
// image on trial writes nothing) leaves the clutch waiting for the pick.
bool Dish::onPicked(Clutch& k, uint8_t i) {
  const std::optional<FriendEgg> friendEgg = k.friendSlot ? FriendEggStore::load(store_) : std::nullopt;
  Egg egg(childOf(k, i, friendEgg ? &*friendEgg : nullptr), k.generation, tick_);
  if (friendEgg && !FriendEggStore::consume(store_)) return false;
  lineage_.recordBirth(egg, k, i, tick_);
  live_.occupant = std::move(egg);
  eventDirty_ = true;
  return true;
}

void Dish::onHatch(Egg& e) {
  const Egg egg = std::move(e);   // emplace ends the egg before it builds the creature
  const Creature& born = live_.occupant.emplace<Creature>(egg, lineage_.legacyFeats(), tick_);
  live_.habitat = Habitat{};
  live_.habitat.pantry = born.phenotype().habitat.pantrySize;
  live_.habitat.pantryTick = tick_;
  eventDirty_ = true;
}

bool Dish::pick(uint8_t egg) {
  Clutch* k = std::get_if<Clutch>(&live_.occupant);
  if (!k || egg >= k->count) return false;
  return onPicked(*k, egg);
}

void Dish::fire(StimId s) { pending_.fireRemote(s); }

void Dish::think(ThoughtId id) {
  if (const Creature* c = std::get_if<Creature>(&live_.occupant))
    thinker_.force(id, *c, live_.habitat, live_.clock, lineage_, tick_, fromPhone());
}

bool Dish::say(std::string_view line) {
  const Creature* c = std::get_if<Creature>(&live_.occupant);
  if (c) thinker_.say(line, *c, lineage_, tick_);
  return c != nullptr;
}

bool Dish::teach(std::string_view line) {
  const Taught next = taught_.with(line);
  if (readOnly_ || !next.save(store_)) return false;
  taught_ = next;
  say(line);
  return true;
}

bool Dish::unteach() {
  if (readOnly_ || !Taught{}.save(store_)) return false;
  taught_ = Taught{};
  return true;
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

bool Dish::adopt() {
  if (adopted_) return true;
  if (readOnly_ || !store_.writable()) return false;
  static const uint8_t kMark[] = {'A', 'D', 'O', 'P'};
  if (!store_.writeAtomic(kAdoptedFile, kMark, sizeof kMark)) return false;
  adopted_ = true;
  return true;
}

Dish::Adoption Dish::adoptSpecies(SpeciesId species, const LookBands& bands) {
  if (readOnly_) return Adoption::NotNeeded;
  Creature* c = std::get_if<Creature>(&live_.occupant);
  if (!c) {
    const Genome& g = std::holds_alternative<Egg>(live_.occupant) ? std::get<Egg>(live_.occupant).genome()
                                                                  : std::get<Clutch>(live_.occupant).parent;
    return predatesSpecies(g) ? Adoption::NotYet : Adoption::NotNeeded;
  }
  if (!predatesSpecies(c->genome())) return Adoption::NotNeeded;
  if (!store_.writable() || duel_.active()) return Adoption::NotYet;   // a duel plays a copy of his genome
  if (!blorb::adoptSpecies(*c, species, live_.settings.speciesSeed, bands)) return Adoption::NotNeeded;
  lineage_.recordEdit(c->generation(), c->genome());
  char was[16], now[16];
  speciesLineName(kGrungo, was);
  speciesLineName(species, now);
  if (std::strcmp(lineage_.currentName(), was) == 0) rename(now);
  // A rerun after a power cut may find the Rename on the log and the old name in the save.
  copyName(live_.settings.name, lineage_.currentName());
  eventDirty_ = true;
  save();
  return Adoption::Adopted;
}

Dish::Adoption Dish::retuneFirstHours(SpeciesId species) {
  if (readOnly_) return Adoption::NotNeeded;
  Creature* c = std::get_if<Creature>(&live_.occupant);
  if (!c) return Adoption::NotYet;
  if (!store_.writable() || duel_.active()) return Adoption::NotYet;
  if (!blorb::retuneFirstHours(*c, species, live_.settings.speciesSeed)) return Adoption::NotNeeded;
  lineage_.recordEdit(c->generation(), c->genome());
  eventDirty_ = true;
  save();
  return Adoption::Adopted;
}

// Read-only (a newer firmware's keepsake) has nothing of its own to save, and the newer save is safe as it is.
bool Dish::flush() { return readOnly_ || save(); }

bool Dish::save() {
  if (!keep_.save(live_)) return false;   // an event save stays urgent and retries on the next tick() call
  lastSaveTick_ = tick_;
  eventDirty_ = false;
  return true;
}

Appearance Dish::appearance() const {
  if (sealed()) {
    // The founder asleep: the keepsake's own genome is on the flash, unread.
    Appearance a = portrait(std::get<Egg>(live_.occupant).genome(), Stage::Adult, 0, 0);
    a.sealed = a.asleep = true;
    a.pose = pose::sleep;
    a.expression = a.previous = expr::asleep;
    a.intensity = Fx::one();
    a.boot = bootHint(BootContext::Sealed, nullptr);
    return a;
  }
  Appearance a = present(live_.occupant, live_.habitat, live_.clock, tick_);
  // The hang's charge as of the last sample, not the last tick: the rim fills
  // with the hand, a frame after it, not up to 100 ms later. Whoever is in
  // the dish: an egg or his remains refuse the fight in words, but the hand
  // that asked sees it felt first.
  a.hangCharge = clamp01(detectors_.hang.charge());
  const BootContext ctx = bootContext();
  const DuelView v = duel_.active() ? duelView() : DuelView{};   // the aim as of the last sample
  a.boot = bootHint(ctx, duel_.active() ? &v : nullptr);
  cues_.dress(a.boot, bootMeaning(ctx), a.hangCharge);
  if ((ctx == BootContext::Awake || ctx == BootContext::Asleep || ctx == BootContext::Lobby) && live_.habitat.pantry == 0)
    a.boot.greyed = true;
  a.dueling = duel_.active();
  if (a.dueling && (v.phase == DuelPhase::Lobby || v.phase == DuelPhase::Greeting)) {
    a.lobby = true;
    a.lobbyConnecting = v.phase == DuelPhase::Greeting;
    a.lobbyFriends = v.heardCount;
    a.lobbyTicksLeft = v.fightTicksLeft;
    a.lobbyDecline = v.myDecline;
  }
  feedView(a);
  if (echo_ != BootAct::Nothing && tick_ - echoTick_ < kEchoTicks) a.echo = echo_, a.echoTicks = uint16_t(tick_ - echoTick_);
  const DishDuel::Refusal& r = duel_.refusal();
  if (r.any && tick_ - r.tick < kEchoTicks) a.declined = r.why, a.refused = true, a.refusedTicks = uint16_t(tick_ - r.tick);
  // Why no fight, in words: for kNoFightTicks, or for as long as fight mode waits on an update.
  if (!a.dueling && r.any && r.kind != NoFight::None && (duel_.pending() || tick_ - r.tick < kNoFightTicks))
    a.noFight = r.kind, a.noFightTicks = uint16_t(std::min<uint32_t>(tick_ - r.tick, 0xFFFF));
  a.restTicksLeft = uint16_t(std::min<uint32_t>(duel_.restTicksLeft(tick_), 0xFFFF));
  const DuelBook& book = duel_.book();
  book.forEachTrophy([&](const Trophy& t) {
    if (a.trophyCount < Appearance::kRimTrophies) a.trophies[a.trophyCount++] = t.fallen;
  });
  const uint16_t gen = std::holds_alternative<Creature>(live_.occupant) ? std::get<Creature>(live_.occupant).generation()
                                                                         : lineage_.currentGeneration();
  a.vendetta = book.vendetta(gen).has_value();
  book.forEachFriend([&](const FriendSummary& f) { a.owesDebt = a.owesDebt || f.weOwe > 0; });
  if (std::holds_alternative<Creature>(live_.occupant)) thinker_.show(a, tick_);
  return a;
}

// The aim the screen shows: what the next tick will lock. While turned onto
// a side, the session's own (the aim from before the turn); while one knock
// waits for its second, the aim it was made at; else the aim as of the last
// sample (AimDetector), not the last tick.
int8_t Dish::liveAim() const {
  if (kSideHoldHolds && detectors_.side.holding()) return duel_.view(tick_).aim;
  if (knockWaitWas_ && knockAim_ >= 0) return knockAim_;
  return detectors_.aim.aim();
}

DuelView Dish::duelView() const {
  DuelView v = duel_.view(tick_);
  if (duel_.active()) v.aim = liveAim();
  return v;
}

// Feeding as the eyes see it: FeedShow's ages, and what the habitat and he
// say now. Reads only; nothing here reaches him.
void Dish::feedView(Appearance& a) const {
  a.feed = feed_.view(nowMs_);
  const Creature* c = std::get_if<Creature>(&live_.occupant);
  if (!c) return;
  const HabitatRules& rules = c->phenotype().habitat;
  a.feed.fullness = Fx::one() - clamp01(c->chemistry().drive(drive::hunger));
  a.feed.pantryMax = rules.pantrySize;
  const Habitat& h = live_.habitat;
  if (h.pantry < rules.pantrySize && rules.refillTicks)
    a.feed.refill = Fx::ratio(int32_t(std::min<uint32_t>(tick_ - h.pantryTick, rules.refillTicks)), int32_t(rules.refillTicks));
  // The pellet in flight is the newest, while it is still news: its index
  // among the items, which present() lists in the pantry's slot order.
  uint32_t dropped = 0;
  if (!feed_.dropTick(nowMs_, dropped)) return;
  int8_t item = 0;
  for (const Pellet& p : h.pellets) {
    if (!p.present) continue;
    if (p.droppedTick == dropped) {
      a.feed.flying = item;
      break;
    }
    ++item;
  }
}

// ---- wall time and the unpowered catch-up --------------------------------------------

std::optional<uint32_t> Dish::wallNow() const {
  if (!wallKnown_ || !live_.wall) return std::nullopt;
  return live_.wall->wallSeconds + (tick_ - live_.wall->tick) / kTicksPerSecond;
}

std::optional<uint32_t> Dish::localNow() const {
  const std::optional<uint32_t> wall = wallNow();
  if (!wall || !zone_) return std::nullopt;
  return uint32_t(int64_t(*wall) + int64_t(*zone_) * 60);
}

FromPhone Dish::fromPhone() const { return FromPhone{phoneAwayTicks(), localNow(), lineage_.named(), &taught_}; }

uint32_t Dish::phoneAwayTicks() const {
  uint32_t since = live_.wall ? live_.wall->tick : 0;
  if (lastPhoneTick_) since = std::max(since, *lastPhoneTick_);   // a catch-up re-anchors after the phone spoke
  return tick_ - since;
}

void Dish::checkWall() {
  lastWallCheckTick_ = tick_;
  std::optional<uint32_t> now;
  bool welcome = false;
  // Every source is read, so a phone reading behind a working RTC is spent, not kept for later.
  for (TimeSource* source : {rtc_, static_cast<TimeSource*>(&phone_)}) {
    if (!source) continue;
    std::optional<uint32_t> t = source->unixSeconds();
    if (t && source == &phone_) {
      welcome = phoneAwayTicks() >= Thinker::kMissPhoneTicks;
      lastPhoneTick_ = tick_;
    }
    if (!now) now = t;
  }
  if (!now) return;
  wallKnown_ = true;
  // Each reading reports its own gap, so a phone reconnecting is not told of the last one again.
  lastCatchUp_ = CatchUp{};
  if (live_.wall) {
    int64_t powered = int64_t((tick_ - live_.wall->tick) / kTicksPerSecond);
    int64_t gap = int64_t(*now) - int64_t(live_.wall->wallSeconds) - powered;
    if (gap > -kDriftSeconds && gap < kDriftSeconds) {
      if (welcome) welcomePhone();
      return;
    }
    // A source that runs backwards is believed from here on; only a forward gap is lived.
    if (gap > 0) {
      catchUp(uint32_t(std::min<int64_t>(gap * kTicksPerSecond, UINT32_MAX)));
      lastCatchUp_.awaySeconds = uint32_t(std::min<int64_t>(gap, UINT32_MAX));
      // Before the anchor moves and the save, so the harm lands once, in the same save.
      Creature* c = std::get_if<Creature>(&live_.occupant);
      if (c && gap >= kLongAbsenceSeconds) {
        longAbsence(*c);
        lastCatchUp_.hurt = true;
      }
    }
  }
  live_.wall = WallAnchor{*now, tick_};
  if (!readOnly_) save();
  if (welcome) welcomePhone();
}

void Dish::forgetWall() {
  wallKnown_ = false;
  live_.wall.reset();
  lastCatchUp_ = CatchUp{};
  eventDirty_ = true;
}

// A phone came back after he had been asking for it: the visit is worth
// making. He glows as the phone's treat makes him, the stimulus eases his
// loneliness through his own genes, and he says so, sorer if it was weeks.
void Dish::welcomePhone() {
  Creature* c = std::get_if<Creature>(&live_.occupant);
  if (!c) return;
  c->inject(chem::vision, c->chemistry().chem[chem::vision.v] + kWelcomeVision);
  fire(stim::spoken_to);
  thinker_.say(lastCatchUp_.hurt ? "YOU LEFT ME. I AM HUNGRY." : "THE GLOWING ONE RETURNS!", *c, lineage_, tick_);
}

// Nobody is there: no gestures, no feeding, no brain. The chemistry runs
// coarse, stages express as their loci fire, the pantry refills, and a death
// takes the normal path. An egg incubates and hatches; a clutch's vigil runs
// to its end and the choice waits for the owner.
void Dish::catchUp(uint32_t ticks) {
  if (sealed()) return;
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
