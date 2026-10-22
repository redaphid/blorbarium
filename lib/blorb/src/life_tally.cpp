#include "blorb/life_tally.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include "blorb/controls.h"
#include "blorb/dish.h"
#include "blorb/duel_book.h"
#include "blorb/founding.h"
#include "log_frames.h"

namespace blorb {
namespace {

// life.stats, all little-endian:
//   "BLLS", version u8 (1), seq u32, sections, crc32 of everything before it.
//   A section: tag u8, len u16, then len bytes.
// The life section: spells u8, lineageId u64, generation u16, this life's
// counters, an ancestor count u8 (0..2) and each ancestor as generation u16
// then its counters. Counters: a slot count u8, then that many u32 by slot
// (life_stats.def). A reader takes the slots it knows, zeroes the ones the
// file lacks and keeps the ones past its own (LifeCounters::extra), so slots
// are only ever added. The analytics section is analytics.cpp's.
constexpr uint8_t kMagic[4] = {'B', 'L', 'L', 'S'};
constexpr size_t kHeadLen = sizeof kMagic + 1 + 4, kCrcLen = 4;
constexpr uint8_t kMaxAncestors = 2;
constexpr size_t kMaxFile = 16384;   // three lives and sixteen buckets need a few KB; a file past this is not ours

// The spells' gates. Starving is the "starving" line's (thoughts.cpp); it
// counts again only once he has fallen back under the "hungry" line's. Sick
// is the "sick" line's toxin; hurt the "hurt" line's injury.
constexpr Fx kStarvingOn = Fx::ratio(8, 10), kStarvingOff = Fx::ratio(1, 2);
constexpr Fx kSickOn = Fx::ratio(3, 10), kSickOff = Fx::ratio(15, 100);
constexpr Fx kHurtOn = Fx::ratio(4, 10), kHurtOff = Fx::ratio(2, 10);
constexpr Fx kHeld = Fx::ratio(1, 2);

uint32_t sat(uint64_t v) { return v > UINT32_MAX ? UINT32_MAX : uint32_t(v); }

void putLife(logframes::Out& o, const LifeCounters& c) {
  const size_t slots = std::min<size_t>(kLifeStatCount + c.extra.size(), 255);
  o.u8(uint8_t(slots));
  for (size_t i = 0; i < slots; ++i) o.u32(i < kLifeStatCount ? c.v[i] : c.extra[i - kLifeStatCount]);
}

LifeCounters getLife(logframes::In& in) {
  LifeCounters c;
  const uint8_t slots = in.u8();
  for (uint8_t i = 0; i < slots && in.ok; ++i) {
    const uint32_t v = in.u32();
    if (i < kLifeStatCount) c.v[i] = v;
    else if (c.extra.size() < LifeTally::kMaxExtra) c.extra.push_back(v);
  }
  return c;
}

const char* const kCauseNames[] = {"old_age", "starved", "injured", "poisoned", "unknown"};
const char* const kPhaseNames[] = {"egg", "creature", "clutch"};
const char* const kStageNames[] = {"baby", "child", "adult", "elder"};

// One JSON document, built in place.
struct Json {
  std::string s;
  void add(const char* fmt, ...) {
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n > 0) s.append(b, std::min<size_t>(size_t(n), sizeof b - 1));
  }
  // A string the pet's own name may hold: quotes, backslashes and controls escaped.
  void str(const char* key, const char* v) {
    add("\"%s\":\"", key);
    for (const char* p = v; *p; ++p) {
      if (*p == '"' || *p == '\\') s += '\\', s += *p;
      else if (uint8_t(*p) < 0x20) add("\\u%04x", unsigned(uint8_t(*p)));
      else s += *p;
    }
    s += '"';
  }
  void life(const LifeCounters& c) {
    s += '{';
    for (uint8_t i = 0; i < kLifeStatCount; ++i) {
      if (i) s += ',';
      const StatInfo& st = LIFE_STATS[i];
      if (st.unit == StatUnit::Ticks) add("\"%s_s\":%u", st.name, unsigned(c.v[i] / 10));
      else if (st.unit == StatUnit::Code && c.v[i] == 0) add("\"%s\":null", st.name);
      else if (st.unit == StatUnit::Code) add("\"%s\":\"%s\"", st.name, lifeStatCauseName(c.v[i]));
      else add("\"%s\":%u", st.name, unsigned(c.v[i]));
    }
    s += '}';
  }
};

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64(const std::vector<uint8_t>& in, std::string& out) {
  for (size_t i = 0; i < in.size(); i += 3) {
    const size_t n = in.size();
    const uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < n ? uint32_t(in[i + 1]) << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out += kB64[v >> 18 & 63];
    out += kB64[v >> 12 & 63];
    out += i + 1 < n ? kB64[v >> 6 & 63] : '=';
    out += i + 2 < n ? kB64[v & 63] : '=';
  }
}

bool body(const SenseOut& s, StimId id) {
  for (uint8_t i = 0; i < s.stimCount; ++i)
    if (s.stimuli[i] == id) return !(s.remote & duelStimBit(id));
  return false;
}

}  // namespace

bool LifeCounters::operator==(const LifeCounters& o) const {
  return std::equal(std::begin(v), std::end(v), std::begin(o.v)) && extra == o.extra;
}

// ---- the life section ----------------------------------------------------------------------

std::vector<uint8_t> LifeTally::encode() const {
  logframes::Out o;
  o.u8(spells);
  o.u64(lineageId);
  o.u16(generation);
  putLife(o, life);
  const uint8_t n = uint8_t(std::min<size_t>(ancestors.size(), kMaxAncestors));
  o.u8(n);
  for (uint8_t i = 0; i < n; ++i) {
    o.u16(ancestors[i].generation);
    putLife(o, ancestors[i].life);
  }
  return o.b;
}

std::optional<LifeTally> LifeTally::decode(const uint8_t* b, size_t n) {
  logframes::In in{b, n};
  LifeTally t;
  t.spells = in.u8();
  t.lineageId = in.u64();
  t.generation = in.u16();
  t.life = getLife(in);
  const uint8_t ancestors = in.u8();
  if (ancestors > kMaxAncestors) return std::nullopt;
  for (uint8_t i = 0; i < ancestors && in.ok; ++i) {
    AncestorLife a;
    a.generation = in.u16();
    a.life = getLife(in);
    t.ancestors.push_back(std::move(a));
  }
  if (!in.ok || in.at != n) return std::nullopt;
  return t;
}

LifeTally LifeTally::resume(const std::optional<LifeTally>& stored, uint64_t lineageId, uint16_t generation) {
  if (stored && stored->lineageId == lineageId) {
    if (stored->generation == generation) return *stored;
    if (uint16_t(stored->generation + 1) == generation) return stored->laid(generation);
  }
  LifeTally t;
  t.lineageId = lineageId;
  t.generation = generation;
  return t;
}

LifeTally LifeTally::laid(uint16_t childGeneration) const {
  LifeTally child;
  child.lineageId = lineageId;
  child.generation = childGeneration;
  child.ancestors.push_back(AncestorLife{generation, life});
  if (!ancestors.empty()) child.ancestors.push_back(ancestors[0]);
  return child;
}

// ---- the file ------------------------------------------------------------------------------

std::vector<uint8_t> Stats::encode() const {
  logframes::Out o;
  o.raw(kMagic, sizeof kMagic);
  o.u8(kVersion);
  o.u32(seq);
  auto section = [&](uint8_t tag, const std::vector<uint8_t>& bytes) {
    o.u8(tag);
    o.u16(uint16_t(bytes.size()));
    o.raw(bytes.data(), bytes.size());
  };
  section(kLife, life.encode());
  section(kAnalytics, analytics.encode());
  for (const Section& s : unknown) section(s.tag, s.bytes);
  o.u32(crc32(o.b.data(), o.b.size()));
  return o.b;
}

std::optional<Stats::Loaded> Stats::decode(const uint8_t* b, size_t n) {
  if (n < kHeadLen + kCrcLen || std::memcmp(b, kMagic, sizeof kMagic) != 0) return std::nullopt;
  uint32_t crc = 0;
  for (size_t i = 0; i < kCrcLen; ++i) crc |= uint32_t(b[n - kCrcLen + i]) << (8 * i);
  if (crc != crc32(b, n - kCrcLen)) return std::nullopt;
  // A newer version may change the layout, not just add sections: not ours to read.
  if (b[sizeof kMagic] != kVersion) return std::nullopt;
  logframes::In in{b + sizeof kMagic + 1, n - sizeof kMagic - 1 - kCrcLen};
  Loaded l;
  l.seq = in.u32();
  while (in.ok && in.at < in.n) {
    const uint8_t tag = in.u8();
    const uint16_t len = in.u16();
    if (!in.ok || len > in.n - in.at) return std::nullopt;
    const uint8_t* p = in.p + in.at;
    if (tag == kLife) {
      if (!(l.life = LifeTally::decode(p, len))) return std::nullopt;
    } else if (tag == kAnalytics) {
      Analytics a;
      if (!a.decode(p, len)) return std::nullopt;
      l.analytics = std::move(a);
    } else {
      l.unknown.push_back(Section{tag, std::vector<uint8_t>(p, p + len)});
    }
    in.at += len;
  }
  if (!in.ok) return std::nullopt;
  return l;
}

std::optional<Stats::Loaded> Stats::load(Storage& s) {
  const size_t size = s.size(kFile);
  if (size == 0 || size > kMaxFile) return std::nullopt;
  std::vector<uint8_t> b(size);
  const std::optional<size_t> n = s.read(kFile, 0, b.data(), size);
  if (!n || *n != size) return std::nullopt;
  return decode(b.data(), size);
}

bool Stats::save(Storage& s) {
  ++seq;
  const std::vector<uint8_t> b = encode();
  if (s.writeAtomic(kFile, b.data(), b.size())) return true;
  --seq;
  analytics.add(anx::stats_save_failed);
  return false;
}

void Stats::resume(std::optional<Loaded> stored, uint64_t lineageId, uint16_t generation) {
  life = LifeTally::resume(stored ? stored->life : std::nullopt, lineageId, generation);
  if (!stored) return;
  seq = stored->seq;   // the server keeps the highest: never go back
  if (stored->analytics) analytics = std::move(*stored->analytics);   // the badge's, whoever lives in it
  unknown = std::move(stored->unknown);
}

void Stats::laid(uint16_t childGeneration) { life = life.laid(childGeneration); }

// ---- counting ----------------------------------------------------------------------------

void Stats::add(uint8_t slot, uint32_t n) { life.life.v[slot] = sat(uint64_t(life.life.v[slot]) + n); }

// A spell: counted once as it starts, timed while it lasts, over once `off`.
void Stats::spell(LifeTally::Spell which, bool on, bool off, uint8_t count, uint8_t ticks, uint32_t n) {
  uint8_t& spells = life.spells;
  if (!(spells & which) && on) {
    spells |= which;
    add(count);
  } else if ((spells & which) && off) {
    spells &= uint8_t(~which);
  }
  if (spells & which) add(ticks, n);
}

void Stats::gestures(const SenseOut& s, bool sideHolding, int8_t aim) {
  struct Row { StimId stim; uint8_t life, anx; };
  static constexpr Row kRows[] = {
      {stim::button, stat::presses, anx::presses},
      {stim::double_knock, stat::knock_knocks, anx::knock_knocks},
      {stim::knock, stat::knocks, anx::knocks},
      {stim::button_double, stat::double_presses, anx::double_presses},
      {stim::button_hold, stat::holds, anx::holds},
      {stim::hang, stat::hangs, anx::hangs},
      {stim::shake, stat::shakes, anx::shakes},
      {stim::cradle, stat::cradles, anx::cradles},
      {stim::owner_arrived, stat::phone_visits, anx::phone_visits},
  };
  if (s.stimCount) {
    for (const Row& r : kRows)
      if (body(s, r.stim)) add(r.life), analytics.add(r.anx);
    if (body(s, stim::picked_up)) add(stat::pickups);
    if (body(s, stim::dropped)) add(stat::drops);
    if (body(s, stim::lid_down)) add(stat::face_downs);
    if (sideHolding && body(s, stim::button_hold)) add(stat::side_holds), analytics.add(anx::side_holds);
  }
  // A tilt: from level into a sector. The aim's own hysteresis keeps a
  // trembling hand from counting twice.
  uint8_t& spells = life.spells;
  if (aim >= 0 && !(spells & LifeTally::kAimed)) add(stat::tilts), analytics.add(anx::tilts);
  spells = aim >= 0 ? uint8_t(spells | LifeTally::kAimed) : uint8_t(spells & ~LifeTally::kAimed);
}

void Stats::fed(bool dropped, bool asleep, Fx hunger) {
  constexpr uint8_t kNone = 255;
  if (dropped) add(stat::pellets), analytics.add(anx::pellets);
  const uint8_t refused = !dropped ? stat::refused_empty : asleep ? stat::refused_asleep
                          : hunger < FeedShow::kFull ? stat::refused_full : kNone;
  if (refused != kNone) add(refused), analytics.add(anx::feed_refused);
}

void Stats::bitten(bool rotten) {
  add(stat::bites);
  if (rotten) add(stat::rotten_bites);
}

void Stats::lived(const Creature& c, uint32_t ticks, bool present) {
  const Chemistry& chem = c.chemistry();
  if (present && chem.locus[locus::held.v] >= kHeld) add(stat::held, ticks);
  const Fx hunger = chem.drive(drive::hunger), toxin = chem.chem[chem::toxin.v], injury = chem.chem[chem::injury.v];
  spell(LifeTally::kStarving, hunger >= kStarvingOn, hunger < kStarvingOff, stat::starving_spells, stat::starving, ticks);
  spell(LifeTally::kSick, toxin >= kSickOn, toxin < kSickOff, stat::sick_spells, stat::sick, ticks);
  spell(LifeTally::kHurt, injury >= kHurtOn, injury < kHurtOff, stat::hurt_spells, stat::hurt, ticks);
  if (present) spell(LifeTally::kAsleep, c.body().asleep, !c.body().asleep, stat::naps, stat::asleep, ticks);
  const uint32_t age = c.stats().ageTicks;
  life.life.v[stat::age] = age;
  // A stage's age, the first tick he is seen in it (a catch-up's stride late at most).
  const uint8_t at[] = {stat::child_at, stat::adult_at, stat::elder_at};
  for (uint8_t s = uint8_t(Stage::Child); s <= uint8_t(c.stage()) && s <= uint8_t(Stage::Elder); ++s)
    if (life.life.v[at[s - 1]] == 0) life.life.v[at[s - 1]] = std::max<uint32_t>(age, 1);
}

void Stats::fightOpened() {
  add(stat::fights_opened);
  analytics.add(anx::fight_opens);
}

void Stats::turn(const TurnReport& t) {
  analytics.add(anx::turns);
  if (t.move[0].byPet) analytics.add(anx::turns_by_pet);
  if (!t.dozed[0] && !t.bracing[0]) {
    const uint8_t stance[] = {anx::pounces, anx::guards, anx::tricks};
    analytics.add(stance[std::min<uint8_t>(uint8_t(t.shown[0]), 2)]);
  }
  if (t.special[0] == TurnReport::Special::Committed) analytics.add(anx::specials);
  if (t.special[0] == TurnReport::Special::Landed) analytics.add(anx::specials_landed);
}

void Stats::met(const Meeting& m, uint32_t battleTicks) {
  add(stat::battles);
  analytics.add(anx::battles);
  const bool won = (m.ending == Ending::AWon && m.me == Side::A) || (m.ending == Ending::BWon && m.me == Side::B);
  if (m.ending == Ending::Draw || m.ending == Ending::Ongoing) add(stat::drawn), analytics.add(anx::drawn);
  else if (won) add(stat::won), analytics.add(anx::won);
  else add(stat::lost), analytics.add(anx::lost);
  if (m.fizzled) add(stat::fizzled), analytics.add(anx::fizzled);
  if (m.battle.stakes == Stakes::Mortal) add(stat::mortals), analytics.add(anx::mortals);
  else if (m.battle.stakes == Stakes::Scrap) add(stat::scraps), analytics.add(anx::scraps);
  else add(stat::spars), analytics.add(anx::spars);
  if (m.iSpared) add(stat::spared_one);
  if (m.wasSpared) add(stat::was_spared);
  if (m.iSlew) add(stat::slew);
  if (m.iFell) add(stat::fell);
  if (m.fled) add(stat::fled);
  analytics.hist(hist::battle_turns, m.turns);
  analytics.hist(hist::battle_s, battleTicks / 10);
}

void Stats::duelEnded(uint8_t why) {
  switch (EndReason(why)) {
    case EndReason::LidDown: return analytics.add(anx::end_lid);
    case EndReason::Dropped: return analytics.add(anx::end_dropped);
    case EndReason::WanderedOff: return analytics.add(anx::end_wandered);
    case EndReason::LobbyTimeout: return analytics.add(anx::end_lobby_timeout);
    case EndReason::Conflict: return analytics.add(anx::end_conflict);
    case EndReason::Fled: return analytics.add(anx::end_fled);
    default: return;
  }
}

void Stats::away(uint32_t seconds, bool hurt) {
  add(stat::away, sat(uint64_t(seconds) * 10));
  if (hurt) add(stat::long_absences);
}

void Stats::died(DeathCause cause, uint32_t ageTicks) {
  life.life.v[stat::age] = ageTicks;
  life.life.v[stat::death] = uint32_t(cause) + 1;
  life.spells &= uint8_t(LifeTally::kAimed);   // no spell outlives him
}

// ---- what the phone and the server read --------------------------------------------------

const char* lifeStatCauseName(uint32_t code) {
  if (code == 0) return "";
  return kCauseNames[std::min<uint32_t>(code - 1, 4)];
}

std::string lifeStatsJson(const Dish& dish, const StatsIdentity& id) {
  const Stats& st = dish.stats();
  const LifeTally& t = st.life;
  const Occupant& o = dish.occupant();
  const Genome& g = std::holds_alternative<Clutch>(o) ? std::get<Clutch>(o).parent
                    : std::holds_alternative<Egg>(o) ? std::get<Egg>(o).genome()
                                                     : std::get<Creature>(o).genome();
  Json j;
  j.add("{\"schema\":\"blorb.life-stats\",\"v\":%u,\"fw\":\"%s\"", unsigned(Stats::kVersion), kFirmwareVersion);
  if (id.mac) j.add(",\"mac\":\"%s\"", id.mac);
  j.add(",\"lineage\":\"%012llx\",\"gen\":%u,\"seq\":%u,", (unsigned long long)(t.lineageId & 0xFFFFFFFFFFFFull),
        unsigned(t.generation), unsigned(st.seq));
  j.str("name", dish.settings().name);
  j.add(",\"species\":\"%s\",\"phase\":\"%s\"", speciesInfo(speciesOf(g)).name, kPhaseNames[o.index()]);
  if (const Creature* c = std::get_if<Creature>(&o)) j.add(",\"stage\":\"%s\"", kStageNames[uint8_t(c->stage()) & 3]);
  j.add(",\"genome_fnv\":\"%08x\"", unsigned(g.hash()));
  if (id.withGenome) {
    j.s += ",\"genome\":\"";
    base64(g.bytes(), j.s);
    j.s += '"';
  }
  j.s += ",\"life\":";
  j.life(t.life);
  const char* const kKin[] = {"parent", "grandparent"};
  for (size_t i = 0; i < t.ancestors.size() && i < 2; ++i) {
    j.add(",\"%s\":{\"gen\":%u,\"life\":", kKin[i], unsigned(t.ancestors[i].generation));
    j.life(t.ancestors[i].life);
    j.s += '}';
  }
  j.s += ',';
  analyticsJson(st.analytics, j.s);
  j.s += '}';
  return j.s;
}

}  // namespace blorb
