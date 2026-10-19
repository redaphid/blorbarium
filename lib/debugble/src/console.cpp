#include "debugble/console.h"

#include <cstdio>
#include <variant>
#include "blorb/registry.h"
#include "blorb/thoughts.h"
#include "debugble/jump.h"

namespace debugble {
namespace {

constexpr size_t kMaxLine = 200;
constexpr uint32_t kPlausibleWall = 1704067200;   // 2024-01-01: hw::SystemClock reads anything earlier as unset
constexpr size_t kMaxChems = 8;
const char* const kPhases[] = {"egg", "creature", "clutch"};
const char* const kStages[] = {"baby", "child", "adult", "elder"};
const char* const kReaches[] = {"look", "show", "write", "harm"};

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// The protocol's id rule (protocol.cpp takeId): no '#' is id 0, and "#" with
// 1 to 6 hex digits then a space or the end is that id.
std::optional<uint32_t> takeId(std::string_view& rest) {
  if (rest.empty() || rest[0] != '#') return 0u;
  uint32_t id = 0;
  size_t i = 1;
  for (; i < rest.size() && i <= 7 && hexValue(rest[i]) >= 0; ++i) id = id << 4 | uint32_t(hexValue(rest[i]));
  if (i == 1 || i > 7 || (i < rest.size() && rest[i] != ' ')) return std::nullopt;
  rest.remove_prefix(i);
  return id;
}

std::optional<unsigned> smallNumber(std::string_view w) {
  if (w.empty() || w.size() > 3) return std::nullopt;
  unsigned v = 0;
  for (char c : w) {
    if (c < '0' || c > '9') return std::nullopt;
    v = v * 10 + unsigned(c - '0');
  }
  return v;
}

// A registry row by its name or its numeric id, as twists take them.
template <class Row, size_t N>
const Row* rowNamed(const Row (&rows)[N], std::string_view w) {
  std::optional<unsigned> n = smallNumber(w);
  for (const Row& r : rows)
    if (n ? r.id.v == *n : w == r.name) return &r;
  return nullptr;
}

std::optional<blorb::ChemId> chemNamed(std::string_view w) {
  for (const blorb::DriveInfo& d : blorb::DRIVES)
    if (w == d.name) return blorb::driveChem(d.id);
  if (const blorb::ChemInfo* c = rowNamed(blorb::CHEMICALS, w)) return c->id;
  return std::nullopt;
}

// Any species this firmware has a row for.
std::optional<blorb::SpeciesId> speciesNamed(std::string_view w) {
  const blorb::SpeciesInfo* s = rowNamed(blorb::SPECIES, w);
  return s ? std::optional<blorb::SpeciesId>(s->id) : std::nullopt;
}

std::optional<blorb::Stage> stageNamed(std::string_view w) {
  if (w == "hatchling") return blorb::Stage::Baby;   // the serial verb's word for it
  for (uint8_t i = 0; i < blorb::kStageCount; ++i)
    if (w == kStages[i]) return blorb::Stage(i);
  return std::nullopt;
}

blorb::Creature* creatureOf(blorb::Dish& dish) { return std::get_if<blorb::Creature>(&dish.occupant()); }

Warp warpOf(Board& board) {
  return [&board](uint32_t ticks) { board.warp(ticks); };
}

const char* unlockName(Console::Call& c) {
  return c.console.unlock().isOpen(c.connection) ? "open" : c.console.unlock().asking() ? "asking" : "locked";
}

void jumpErr(Jump j, blorb::Reply& r) {
  switch (j) {
    case Jump::Done: return;
    case Jump::NoEgg: return r.err(409, "NOT_EGG");
    case Jump::NoCreature: return r.err(409, "NO_CREATURE");
    case Jump::PastStage: return r.err(409, "PAST_STAGE");
    case Jump::Died: return r.err(409, "DIED");
  }
}

void creatureLine(blorb::Dish& dish, blorb::Reply& r) {
  const blorb::Occupant& o = dish.occupant();
  const blorb::Creature* c = std::get_if<blorb::Creature>(&o);
  r.ok("phase=%s stage=%s age=%u", kPhases[o.index()], c ? kStages[uint8_t(c->stage())] : "-",
       unsigned(c ? c->stats().ageTicks : 0));
}

}  // namespace

void Console::handle(std::string_view line, uint32_t connection, uint32_t nowMs, blorb::Dish& dish, blorb::Link& out) {
  std::string_view rest = line;
  const std::optional<uint32_t> id = takeId(rest);
  blorb::Reply reply(out, id.value_or(0));
  if (line.size() > kMaxLine) return reply.err(413, "TOO_LONG");
  if (!id) return reply.err(400, "BAD_ID");
  blorb::Args args{rest};
  const std::optional<std::string_view> verb = args.word();
  if (!verb) return reply.err(400, "NO_VERB");
  for (const VerbInfo& v : VERBS) {
    if (*verb != v.verb) continue;
    if (v.reach >= Reach::Write) {
      if (dish.sealed()) return reply.err(409, "SEALED");
      if (board_.onTrial()) return reply.err(409, "TRIAL");
      if (board_.otaActive()) return reply.err(409, "OTA_ACTIVE");
    }
    if (v.reach == Reach::Harm && !unlock_.isOpen(connection)) return reply.err(423, "LOCKED");
    Call call{*this, board_, dish, connection, nowMs};
    return v.run(call, args, reply);
  }
  reply.err(404, "UNKNOWN_VERB");
}

void Console::tick(uint32_t nowMs, std::optional<uint32_t> live, blorb::Link& out) {
  unlock_.tick(nowMs);
  if (std::optional<uint32_t> o = unlock_.owner(); o && o != live) unlock_.lock();
  if (scale_.factor() > 1 && (scale_.connection() != live || board_.otaActive())) scale_.reset(nowMs);
  if (board_.otaActive()) unlock_.cancelQuestion();   // the button is the update's
  switch (unlock_.takeEvent()) {
    case Unlock::Event::Opened: out.writeLine("! DEBUG unlock=open"); break;
    case Unlock::Event::Lapsed: out.writeLine("! DEBUG unlock=lapsed"); break;
    case Unlock::Event::None: break;
  }
}

// ---- verbs ------------------------------------------------------------------------------

void verb_help(Console::Call&, blorb::Args& a, blorb::Reply& r) {
  if (!a.done()) return r.err(400, "BAD_ARGS");
  for (const VerbInfo& v : VERBS) r.line("%s %s %s", v.verb, kReaches[uint8_t(v.reach)], v.args);
  r.ok("%u", unsigned(sizeof VERBS / sizeof VERBS[0]));
}

void verb_status(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!a.done()) return r.err(400, "BAD_ARGS");
  const blorb::Occupant& o = c.dish.occupant();
  const blorb::Creature* cr = std::get_if<blorb::Creature>(&o);
  const std::optional<blorb::SpeciesId> look = c.board.looks();
  r.ok("unlock=%s scale=%u trial=%d ota=%d sealed=%d phase=%s stage=%s age=%u species=%s look=%s time=%s dropped=%u "
       "fight=%s last=%s",
       unlockName(c), unsigned(c.console.scale().factor()), int(c.board.onTrial()), int(c.board.otaActive()),
       int(c.dish.sealed()), kPhases[o.index()], cr ? kStages[uint8_t(cr->stage())] : "-",
       unsigned(cr ? cr->stats().ageTicks : 0), blorb::speciesInfo(c.dish.appearance().species).name,
       look ? blorb::speciesInfo(*look).name : "own", c.dish.timeKnown() ? "known" : "unknown",
       unsigned(c.console.scale().dropped()), c.dish.fightState(), c.dish.lastFightGate());
}

void verb_unlock(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!a.done()) return r.err(400, "BAD_ARGS");
  if (c.dish.sealed()) return r.err(409, "SEALED");
  if (c.board.onTrial()) return r.err(409, "TRIAL");
  if (c.board.otaActive()) return r.err(409, "OTA_ACTIVE");   // its question owns the button
  Unlock& u = c.console.unlock();
  if (u.isOpen(c.connection)) return r.ok("unlock=open");
  u.ask(c.now, c.connection);
  r.ok("unlock=asking ms=%u", unsigned(u.msLeft(c.now)));
}

void verb_lock(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!a.done()) return r.err(400, "BAD_ARGS");
  c.console.unlock().lock();
  r.ok("unlock=locked");
}

void verb_think(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<std::string_view> w = a.word();
  if (!w || !a.done()) return r.err(400, "BAD_ARGS");
  const blorb::ThoughtInfo* t = rowNamed(blorb::THOUGHTS, *w);
  if (!t) return r.err(416, "OUT_OF_RANGE");
  if (!creatureOf(c.dish)) return r.err(409, "NO_CREATURE");
  c.dish.think(t->id);
  r.ok("think=%s prophecy=%d", t->name, int(t->prophecy()));
}

void verb_fortune(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<std::string_view> w = a.word();
  if (!a.done()) return r.err(400, "BAD_ARGS");
  const blorb::ThoughtInfo* t = nullptr;
  if (w) {
    t = rowNamed(blorb::THOUGHTS, *w);
    if (!t || !t->prophecy()) return r.err(416, "OUT_OF_RANGE");
  } else {
    // Drawn by the tick, so two fortunes in a row are rarely the same.
    size_t count = 0;
    for (const blorb::ThoughtInfo& x : blorb::THOUGHTS) count += x.prophecy();
    size_t pick = count ? (c.dish.tickCount() * 2654435761u >> 7) % count : 0;
    for (const blorb::ThoughtInfo& x : blorb::THOUGHTS)
      if (x.prophecy() && pick-- == 0) t = &x;
  }
  if (!creatureOf(c.dish)) return r.err(409, "NO_CREATURE");
  if (!t) return r.err(416, "OUT_OF_RANGE");
  c.dish.think(t->id);
  r.ok("fortune=%s", t->name);
}

void verb_look(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<std::string_view> w = a.word();
  if (!w || !a.done()) return r.err(400, "BAD_ARGS");
  if (*w == "off") {
    c.board.look(std::nullopt);
    return r.ok("look=own");
  }
  const std::optional<blorb::SpeciesId> s = speciesNamed(*w);
  if (!s) return r.err(416, "OUT_OF_RANGE");
  c.board.look(*s);
  r.ok("look=%s", blorb::speciesInfo(*s).name);
}

void verb_fire(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<std::string_view> w = a.word();
  if (!w || !a.done()) return r.err(400, "BAD_ARGS");
  const blorb::StimInfo* s = rowNamed(blorb::STIMULI, *w);
  if (!s) return r.err(416, "OUT_OF_RANGE");
  c.dish.fire(s->id);
  r.ok("fire=%s", s->name);
}

void verb_wall(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  blorb::Args peek = a;
  if (peek.word() == std::optional<std::string_view>("clear")) {
    if (!peek.done()) return r.err(400, "BAD_ARGS");
    c.board.setClock(std::nullopt);
    c.dish.forgetWall();
    return r.ok("time=unknown");
  }
  const std::optional<uint32_t> unix = a.number(UINT32_MAX);
  const std::optional<int32_t> tz = a.done() ? std::optional<int32_t>(0) : a.signedNumber(-12 * 60, 14 * 60);
  if (!unix || !tz || !a.done()) return r.err(400, "BAD_ARGS");
  if (*unix < kPlausibleWall) return r.err(416, "OUT_OF_RANGE");
  // Forgotten first, so the new time is an anchor and not a gap he lives through.
  c.board.setClock(*unix);
  c.dish.forgetWall();
  c.dish.phoneTime().set(*unix);
  c.dish.checkWall();
  c.dish.clock().alignToWall(*unix, int16_t(*tz));
  r.ok("time=known wall=%u tz=%d night=%d", unsigned(*unix), int(*tz), int(c.dish.clock().night()));
}

void verb_scale(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<uint32_t> n = a.number(UINT16_MAX);
  if (!n || !a.done()) return r.err(400, "BAD_ARGS");
  if (*n < 1 || *n > TimeScale::kMax) return r.err(416, "OUT_OF_RANGE");
  c.console.scale().set(uint16_t(*n), c.now, c.connection);
  r.ok("scale=%u", unsigned(*n));
}

void verb_freehatch(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<std::string_view> w = a.word();
  const bool off = w == std::optional<std::string_view>("off");
  if (!a.done() || (w && !off && *w != "on")) return r.err(400, "BAD_ARGS");
  c.dish.setHatchFree(!off);
  r.ok("freehatch=%s", off ? "off" : "on");
}

void verb_hatch(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!a.done()) return r.err(400, "BAD_ARGS");
  const Jump j = hatch(c.dish, warpOf(c.board));
  if (j != Jump::Done) return jumpErr(j, r);
  creatureLine(c.dish, r);
}

void verb_stage(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<std::string_view> w = a.word();
  const std::optional<blorb::Stage> s = w ? stageNamed(*w) : std::nullopt;
  if (!s || !a.done()) return r.err(400, "BAD_ARGS");
  const Jump j = growTo(c.dish, *s, warpOf(c.board));
  if (j != Jump::Done) return jumpErr(j, r);
  creatureLine(c.dish, r);
}

void verb_warp(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<uint32_t> minutes = a.number(UINT16_MAX);
  if (!minutes || !a.done()) return r.err(400, "BAD_ARGS");
  if (*minutes < 1 || *minutes > Console::kMaxWarpMinutes) return r.err(416, "OUT_OF_RANGE");
  c.board.warp(*minutes * blorb::kTicksPerMinute);
  c.dish.flush();
  creatureLine(c.dish, r);
}

void verb_chem(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  struct Set { blorb::ChemId chem; blorb::Fx level; };
  Set sets[kMaxChems];
  size_t n = 0;
  // Every pair is read before one is applied, so a refused line changes nothing.
  while (std::optional<std::string_view> w = a.word()) {
    const std::optional<uint32_t> permille = a.number(1000);
    if (!permille || n == kMaxChems) return r.err(400, "BAD_ARGS");
    const std::optional<blorb::ChemId> chem = chemNamed(*w);
    if (!chem) return r.err(416, "OUT_OF_RANGE");
    sets[n++] = Set{*chem, blorb::Fx::ratio(int32_t(*permille), 1000)};
  }
  if (n == 0) return r.err(400, "BAD_ARGS");
  blorb::Creature* cr = creatureOf(c.dish);
  if (!cr) return r.err(409, "NO_CREATURE");
  for (size_t i = 0; i < n; ++i) cr->inject(sets[i].chem, sets[i].level);
  c.dish.markDirty();
  r.ok("%u", unsigned(n));
}

void verb_die(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<std::string_view> w = a.word();
  if (!a.done() || (w && *w != "eggs" && *w != "remains")) return r.err(400, "BAD_ARGS");
  const Jump j = die(c.dish, !w || *w == "eggs", warpOf(c.board));
  if (j != Jump::Done) return jumpErr(j, r);
  const blorb::Occupant& o = c.dish.occupant();
  const blorb::Clutch* k = std::get_if<blorb::Clutch>(&o);
  r.ok("phase=%s eggs=%u vigil=%u", kPhases[o.index()], unsigned(k ? k->count : 0), unsigned(k ? k->sinceDeath : 0));
}

void verb_species(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<std::string_view> w = a.word();
  if (!w || !a.done()) return r.err(400, "BAD_ARGS");
  const std::optional<blorb::SpeciesId> s = speciesNamed(*w);
  if (!s) return r.err(416, "OUT_OF_RANGE");
  if (!creatureOf(c.dish)) return r.err(409, "NO_CREATURE");
  // A line founded before species has no species gene: only a WIPE <species> refounds it.
  if (!c.dish.editGene(blorb::kSpeciesGeneUid, 0, s->v)) return r.err(404, "NO_SPECIES_GENE");
  c.dish.editGene(blorb::kPowerGeneUid, 0, blorb::speciesInfo(*s).power.v);   // pure-bred: the row's own power
  c.dish.flush();
  r.ok("species=%s", blorb::speciesInfo(*s).name);
}

void verb_wipe(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  const std::optional<std::string_view> w = a.word();
  if (!a.done()) return r.err(400, "BAD_ARGS");
  std::optional<blorb::SpeciesId> found;
  if (w && !(found = speciesNamed(*w))) return r.err(416, "OUT_OF_RANGE");
  if (!c.board.wipe(found)) return r.err(500, "WIPE_FAILED");
  r.ok("wiping found=%s", found ? blorb::speciesInfo(*found).name : "nest");
}

}  // namespace debugble

// ---- the device itself ------------------------------------------------------------------

namespace debugble {
namespace {

constexpr uint32_t kMaxIdleSeconds = 3600;
const char* const kDuelPhases[] = {"closed",    "lobby",    "greeting", "staking", "choosing", "waiting",
                                   "revealing", "fainting", "mercy",    "fallen",  "friends"};

void powerLine(const Board::PowerView& v, blorb::Reply& r) {
  r.unsaved().ok("power=%s why=%s idle_ms=%u left_ms=%u cpu=%u frames=%u changes=%u held=%d sleep=%s slept_ms=%u",
                 v.low ? "low" : "full", v.why, unsigned(v.idleMs), unsigned(v.msLeft), unsigned(v.cpuMhz),
                 unsigned(v.frames), unsigned(v.changes), int(v.held), v.sleep, unsigned(v.sleptMs));
}

// An optional number in [lo, hi], else `dflt`; nullopt when one is there but bad.
std::optional<uint32_t> optNumber(blorb::Args& a, uint32_t lo, uint32_t hi, uint32_t dflt) {
  if (a.done()) return dflt;
  const std::optional<uint32_t> n = a.number(hi);
  if (!n || *n < lo) return std::nullopt;
  return n;
}

}  // namespace

void verb_power(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!c.board.hasDevice()) return r.err(501, "NOT_ON_BOARD");
  const std::optional<std::string_view> w = a.word();
  Board::PowerAsk ask = Board::PowerAsk::Status;
  uint32_t idleMs = 0;
  if (w == std::optional<std::string_view>("low")) ask = Board::PowerAsk::Low;
  else if (w == std::optional<std::string_view>("wake")) ask = Board::PowerAsk::Wake;
  else if (w == std::optional<std::string_view>("idle")) {
    ask = Board::PowerAsk::Idle;
    blorb::Args peek = a;
    if (peek.word() == std::optional<std::string_view>("default")) {
      a = peek;
    } else {
      const std::optional<uint32_t> s = a.number(UINT16_MAX);
      if (!s) return r.err(400, "BAD_ARGS");
      if (*s < 1 || *s > kMaxIdleSeconds) return r.err(416, "OUT_OF_RANGE");
      idleMs = *s * 1000;
    }
  } else if (w && *w != "status") {
    return r.err(400, "BAD_ARGS");
  }
  if (!a.done()) return r.err(400, "BAD_ARGS");
  Board::PowerView v{};
  if (!c.board.power(ask, idleMs, v)) return r.err(409, "HELD_AWAKE");
  powerLine(v, r);
}

void verb_duel(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!a.done()) return r.err(400, "BAD_ARGS");
  const blorb::DuelView v = c.dish.duelView();
  r.ok("fight=%s active=%d phase=%s peers=%u ctx=%u hearts=%u-%u last=%s", c.dish.fightState(),
       int(c.dish.duelActive()), kDuelPhases[size_t(v.phase)], unsigned(v.peerCount), unsigned(c.dish.bootContext()),
       unsigned(v.mine.hearts), unsigned(v.theirs.hearts), c.dish.lastFightGate());
}

void verb_nap(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!a.done()) return r.err(400, "BAD_ARGS");
  blorb::Creature* cr = creatureOf(c.dish);
  if (!cr) return r.err(409, "NO_CREATURE");
  cr->force(blorb::action::sleep);
  c.dish.markDirty();
  r.ok("nap=1");
}

void verb_press(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!c.board.hasDevice()) return r.err(501, "NOT_ON_BOARD");
  const std::optional<uint32_t> ms = optNumber(a, 1, 10000, 150);   // the sim's !button
  if (!ms || !a.done()) return r.err(400, "BAD_ARGS");
  if (!c.board.press(*ms, false)) return r.err(409, "ASKING");
  r.ok("press=%u", unsigned(*ms));
}

void verb_double(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!c.board.hasDevice()) return r.err(501, "NOT_ON_BOARD");
  if (!a.done()) return r.err(400, "BAD_ARGS");
  if (!c.board.press(100, true)) return r.err(409, "ASKING");
  r.ok("double=1 ctx=%u", unsigned(c.dish.bootContext()));
}

void verb_hang(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!c.board.hasDevice()) return r.err(501, "NOT_ON_BOARD");
  const std::optional<uint32_t> ms = optNumber(a, 1, 30000, 2500);
  const std::optional<uint32_t> wobble = ms ? optNumber(a, 0, 1, 0) : std::nullopt;
  if (!wobble || !a.done()) return r.err(400, "BAD_ARGS");
  c.board.gesture(Board::Gesture::Hang, *ms, uint8_t(*wobble));
  r.ok("hang=%u wobble=%u", unsigned(*ms), unsigned(*wobble));
}

void verb_cradle(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!c.board.hasDevice()) return r.err(501, "NOT_ON_BOARD");
  const std::optional<uint32_t> ms = optNumber(a, 1, 30000, 5000);
  if (!ms || !a.done()) return r.err(400, "BAD_ARGS");
  c.board.gesture(Board::Gesture::Cradle, *ms, 0);
  r.ok("cradle=%u", unsigned(*ms));
}

void verb_lean(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!c.board.hasDevice()) return r.err(501, "NOT_ON_BOARD");
  const std::optional<uint32_t> aim = a.number(3);
  const std::optional<uint32_t> ms = aim ? optNumber(a, 1, 60000, 3000) : std::nullopt;
  if (!ms || !a.done()) return r.err(400, "BAD_ARGS");
  c.board.gesture(Board::Gesture::Lean, *ms, uint8_t(*aim));
  r.ok("lean=%u ms=%u", unsigned(*aim), unsigned(*ms));
}

void verb_net(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!c.board.hasDevice()) return r.err(501, "NOT_ON_BOARD");
  char report[160] = "";
  if (!c.board.net(a.rest, report, sizeof report)) return r.err(400, "BAD_ARGS");
  r.ok("%s", report);
}

void verb_reboot(Console::Call& c, blorb::Args& a, blorb::Reply& r) {
  if (!c.board.hasDevice()) return r.err(501, "NOT_ON_BOARD");
  const std::optional<std::string_view> w = a.word();
  if (!a.done() || (w && *w != "quiet")) return r.err(400, "BAD_ARGS");
  if (!c.board.reboot(bool(w))) return r.err(500, "REBOOT_FAILED");
  r.ok("rebooting quiet=%d", int(bool(w)));
}

}  // namespace debugble
