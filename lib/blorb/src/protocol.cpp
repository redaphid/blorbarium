#include "blorb/protocol.h"

#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <type_traits>
#include "blorb/dish.h"
#include "blorb/genes.h"
#include "blorb/seams.h"

namespace blorb {
namespace {

constexpr size_t kMaxLine = 200;
constexpr const char* kFirmware = "0.3.0";
constexpr size_t kChunkBytes = 120;          // 160 base64 characters: a "+ <offset> <b64>" line fits the cap
constexpr uint32_t kStateEventTicks = 20;    // ! STATE every 2 s
constexpr size_t kDefaultBeliefs = 8, kMaxBeliefs = 32;

const char* const kPhases[] = {"egg", "creature", "clutch"};
const char* const kStages[] = {"baby", "child", "adult", "elder"};
const char* const kCauses[] = {"old_age", "starved", "injured", "poisoned", "unknown"};
const char* const kDirs[] = {"sense", "act", "free"};
const char* const kSources[] = {"body", "world", "self", "phone"};
const char* const kClasses[] = {"body", "mind", "look", "life"};
const char kRules[] = {'A', 'N', 'F', 'X'};   // MutRule: Any, Nudge, Flag, Fixed

// Event bits, in SUB's vocabulary.
enum Event : uint32_t { kState = 1, kStage = 2, kDied = 4, kClutch = 8, kPicked = 16, kHatched = 32 };
struct EventName { const char* name; uint32_t bit; };
constexpr EventName kEvents[] = {{"state", kState},   {"stage", kStage},   {"died", kDied},
                                 {"clutch", kClutch}, {"picked", kPicked}, {"hatched", kHatched}};

// "#<id> <kind>[ <payload>]", cut at the line cap.
void emit(Link& link, uint32_t id, const char* kind, const char* fmt, va_list ap) {
  char payload[kMaxLine + 1];
  std::vsnprintf(payload, sizeof payload, fmt, ap);
  char line[kMaxLine + 1];
  int n = std::snprintf(line, sizeof line, "#%x %s%s%s", unsigned(id), kind, payload[0] ? " " : "", payload);
  link.writeLine(std::string_view(line, n < 0 ? 0 : std::min<size_t>(size_t(n), kMaxLine)));
}

// One line built in pieces, cut at the cap.
struct Line {
  char b[kMaxLine + 1] = {};
  size_t n = 0;
  void add(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int k = std::vsnprintf(b + n, sizeof b - n, fmt, ap);
    va_end(ap);
    if (k > 0) n = std::min(sizeof b - 1, n + size_t(k));
  }
};

int permille(Fx v) { return int((int64_t(v.raw) * 1000 + (v.raw >= 0 ? Fx::kOne / 2 : -Fx::kOne / 2)) / Fx::kOne); }

template <class Row, size_t N, class IdT>
const char* nameOf(const Row (&rows)[N], IdT id) {
  for (const Row& r : rows) if (r.id == id) return r.name;
  return "?";
}

const char* stageName(Stage s) { return kStages[uint8_t(s) & 3]; }
const char* causeName(DeathCause c) { return kCauses[std::min<uint8_t>(uint8_t(c), 4)]; }

// A brain feature: a situation locus by name, or recent_<stim> for a recent-stimulus locus.
void featureName(Line& l, LocusId f) {
  if (f.v >= kRecentBase && f.v < kRecentBase + 64) l.add("recent_%s", nameOf(STIMULI, StimId{uint8_t(f.v - kRecentBase)}));
  else l.add("%s", nameOf(LOCI, f));
}

const Genome& genomeOf(const Occupant& o) {
  return std::visit([](const auto& x) -> const Genome& {
    if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Clutch>) return x.parent;
    else return x.genome();
  }, o);
}

uint16_t generationOf(const Occupant& o) {
  return std::visit([](const auto& x) -> uint16_t {
    if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Clutch>) return x.generation;
    else return x.generation();
  }, o);
}

bool eggsShown(const Occupant& o) {
  const Clutch* k = std::get_if<Clutch>(&o);
  return k && k->sinceDeath >= Clutch::kVigilTicks;
}

const char* kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64(const uint8_t* in, size_t n, char* out) {
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < n ? uint32_t(in[i + 1]) << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = kBase64[v >> 18 & 63];
    out[o++] = kBase64[v >> 12 & 63];
    out[o++] = i + 1 < n ? kBase64[v >> 6 & 63] : '=';
    out[o++] = i + 2 < n ? kBase64[v & 63] : '=';
  }
  out[o] = 0;
  return o;
}

void stateLine(Line& l, const Dish& dish) {
  const Occupant& o = dish.occupant();
  l.add("phase=%s gen=%u", kPhases[o.index()], unsigned(generationOf(o)));
  if (const Creature* c = std::get_if<Creature>(&o)) {
    l.add(" stage=%s age=%u action=%s face=%s asleep=%d", stageName(c->stage()), unsigned(c->stats().ageTicks),
          nameOf(ACTIONS, c->action()), nameOf(EXPRESSIONS, c->face().current), int(c->body().asleep));
  } else if (const Egg* e = std::get_if<Egg>(&o)) {
    l.add(" progress=%d", permille(e->progress()));
  } else {
    const Clutch& k = std::get<Clutch>(o);
    l.add(" eggs=%u cursor=%u vigil=%u cause=%s", unsigned(k.count), unsigned(k.cursor), unsigned(k.sinceDeath),
          causeName(k.cause));
  }
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "#<1..6 hex digits> " or no id at all (id 0). nullopt for a malformed id.
std::optional<uint32_t> takeId(std::string_view& rest) {
  if (rest.empty() || rest[0] != '#') return 0u;
  uint32_t id = 0;
  size_t i = 1;
  for (; i < rest.size() && i <= 7 && hexValue(rest[i]) >= 0; ++i) id = id << 4 | uint32_t(hexValue(rest[i]));
  if (i == 1 || i > 7 || (i < rest.size() && rest[i] != ' ')) return std::nullopt;
  rest.remove_prefix(i);
  return id;
}

}  // namespace

// ---- framing -------------------------------------------------------------------------

std::optional<std::string_view> Args::word() {
  size_t from = rest.find_first_not_of(' ');
  if (from == std::string_view::npos) {
    rest = {};
    return std::nullopt;
  }
  rest.remove_prefix(from);
  size_t end = std::min(rest.find(' '), rest.size());
  std::string_view w = rest.substr(0, end);
  rest.remove_prefix(end);
  return w;
}

std::optional<uint32_t> Args::number(uint32_t max) {
  std::optional<std::string_view> w = word();
  if (!w || w->empty() || w->size() > 10) return std::nullopt;
  uint64_t v = 0;
  for (char c : *w) {
    if (c < '0' || c > '9') return std::nullopt;
    v = v * 10 + uint64_t(c - '0');
  }
  if (v > max) return std::nullopt;
  return uint32_t(v);
}

std::optional<int32_t> Args::signedNumber(int32_t min, int32_t max) {
  std::optional<std::string_view> w = word();
  if (!w || w->empty()) return std::nullopt;
  bool negative = (*w)[0] == '-';
  std::string_view digits = negative ? w->substr(1) : *w;
  if (digits.empty() || digits.size() > 10) return std::nullopt;
  int64_t v = 0;
  for (char c : digits) {
    if (c < '0' || c > '9') return std::nullopt;
    v = v * 10 + (c - '0');
  }
  if (negative) v = -v;
  if (v < min || v > max) return std::nullopt;
  return int32_t(v);
}

bool Args::done() { return rest.find_first_not_of(' ') == std::string_view::npos; }

Reply::Reply(Link& link, uint32_t id) : link_(link), id_(id) {}

Reply& Reply::line(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(link_, id_, "+", fmt, ap);
  va_end(ap);
  return *this;
}

void Reply::chunks(const uint8_t* data, size_t len) {
  char b64[kChunkBytes / 3 * 4 + 1];
  for (size_t at = 0; at < len; at += kChunkBytes) {
    base64(data + at, std::min(kChunkBytes, len - at), b64);
    line("%u %s", unsigned(at), b64);
  }
  ok("%u %08x", unsigned(len), unsigned(crc32(data, len)));
}

void Reply::ok(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  emit(link_, id_, "OK", fmt, ap);
  va_end(ap);
  ok_ = true;
}

void Reply::err(uint16_t code, const char* text) {
  char line[kMaxLine + 1];
  int n = std::snprintf(line, sizeof line, "#%x ERR %u %s", unsigned(id_), unsigned(code), text);
  link_.writeLine(std::string_view(line, n < 0 ? 0 : std::min<size_t>(size_t(n), kMaxLine)));
}

void Describe::field(const char* name, int value) { out.line("%s=%d", name, value); }
void Describe::field(const char* name, const char* value) { out.line("%s=%s", name, value); }

void Protocol::handle(std::string_view line, Dish& dish, Link& link) {
  std::string_view rest = line;
  std::optional<uint32_t> id = takeId(rest);
  Reply reply(link, id.value_or(0));
  if (line.size() > kMaxLine) return reply.err(413, "TOO_LONG");
  if (!id) return reply.err(400, "BAD_ID");
  Args args{rest};
  std::optional<std::string_view> verb = args.word();
  if (!verb) return reply.err(400, "NO_VERB");
  for (const CommandInfo& cmd : COMMANDS) {
    if (*verb != cmd.verb) continue;
    size_t from = args.rest.find_first_not_of(' ');
    Request req{*id, *verb, from == std::string_view::npos ? std::string_view{} : args.rest.substr(from)};
    cmd.run(dish, req, reply);
    if ((cmd.flags & kMutating) && reply.succeeded()) dish.markDirty();
    return;
  }
  reply.err(404, "UNKNOWN_VERB");
}

void Protocol::pump(Dish& dish, Link& link, uint32_t tick) {
  const Occupant& o = dish.occupant();
  uint8_t kind = uint8_t(o.index());
  const Creature* c = std::get_if<Creature>(&o);
  uint8_t stage = c ? uint8_t(c->stage()) : 255;
  bool eggs = eggsShown(o);
  auto event = [&](uint32_t bit, const char* fmt, auto... args) {
    if (!(subMask_ & bit)) return;
    char line[kMaxLine + 1];
    int n = std::snprintf(line, sizeof line, fmt, args...);
    link.writeLine(std::string_view(line, n < 0 ? 0 : std::min<size_t>(size_t(n), kMaxLine)));
  };
  if (seenKind_ != 255 && subMask_) {
    unsigned gen = generationOf(o);
    if (kind != seenKind_ && std::holds_alternative<Clutch>(o))
      event(kDied, "! DIED gen=%u cause=%s", gen - 1, causeName(std::get<Clutch>(o).cause));
    if (kind != seenKind_ && std::holds_alternative<Egg>(o)) event(kPicked, "! PICKED gen=%u", gen);
    if (kind != seenKind_ && c) event(kHatched, "! HATCHED gen=%u", gen);
    if (kind == seenKind_ && c && stage != seenStage_) event(kStage, "! STAGE stage=%s", stageName(c->stage()));
    if (eggs && !seenEggs_) event(kClutch, "! CLUTCH eggs=%u", unsigned(std::get<Clutch>(o).count));
    if (tick - lastStateTick_ >= kStateEventTicks) {
      lastStateTick_ = tick;
      Line l;
      stateLine(l, dish);
      event(kState, "! STATE %s", l.b);
    }
  }
  seenKind_ = kind;
  seenStage_ = stage;
  seenEggs_ = eggs;
}

void Protocol::disconnected() { subMask_ = 0; }

// ---- verbs ------------------------------------------------------------------------------

void cmd_hello(Dish& dish, const Request&, Reply& r) {
  const Occupant& o = dish.occupant();
  r.ok("fw=%s fmt=%u lineage=%012llx gen=%u name=%s phase=%s feats=0x%02x", kFirmware, unsigned(kFormatVersion),
       (unsigned long long)(dish.settings().lineageId & 0xFFFFFFFFFFFFull), unsigned(generationOf(o)),
       dish.settings().name, kPhases[o.index()], unsigned(dish.lineage().legacyFeats()));
}

void cmd_schema(Dish&, const Request&, Reply& r) {
  unsigned rows = 0;
  auto row = [&](const char* fmt, auto... args) {
    r.line(fmt, args...);
    ++rows;
  };
  for (const ChemInfo& x : CHEMICALS) row("chem %u %s %04x %d", unsigned(x.id.v), x.name, unsigned(x.rgb565), int(x.show));
  for (const DriveInfo& x : DRIVES) row("drive %u %s", unsigned(x.id.v), x.name);
  for (const LocusInfo& x : LOCI) row("locus %u %s %s %d", unsigned(x.id.v), x.name, kDirs[uint8_t(x.dir)], int(x.situation));
  for (const StimInfo& x : STIMULI)
    row("stim %u %s %s %d", unsigned(x.id.v), x.name, kSources[uint8_t(x.source)], int(x.situation));
  for (const ActionInfo& x : ACTIONS)
    row("action %u %s %u %s", unsigned(x.id.v), x.name, unsigned(x.minTicks),
        x.selfStim == stim::none ? "none" : nameOf(STIMULI, x.selfStim));
  for (const PoseInfo& x : POSES) row("pose %u %s", unsigned(x.id.v), x.name);
  for (const ExprInfo& x : EXPRESSIONS) row("expr %u %s", unsigned(x.id.v), x.name);
  for (const RegionInfo& x : REGIONS) row("region %u %s", unsigned(x.id.v), x.name);
  for (const ReflexInfo& x : REFLEXES)
    row("reflex %u %s %s %u %s %s", unsigned(x.id.v), x.name, nameOf(LOCI, x.trigger), unsigned(x.ticks),
        nameOf(EXPRESSIONS, x.face), nameOf(POSES, x.pose));
  for (const CareInfo& x : CARES)
    row("care %u %s %s %s", unsigned(x.id.v), x.name, nameOf(DRIVES, x.drive), nameOf(STIMULI, x.gesture));
  for (const FeatInfo& x : FEATS) row("feat %u %s", unsigned(x.bit), x.name);
  for (const GeneTypeInfo& x : GENE_TYPES) {
    char rules[256];
    for (uint8_t i = 0; i < x.bodyLen; ++i) rules[i] = kRules[uint8_t(x.rules[i]) & 3];
    rules[x.bodyLen] = 0;
    row("gene %u %s %u %s %s", unsigned(x.type), x.name, unsigned(x.bodyLen), kClasses[uint8_t(x.cls)], rules);
  }
#define BLORB_SENSE(member, Type) row("sense %s", #member);
#include "blorb/defs/senses.def"
#undef BLORB_SENSE
  for (const CommandInfo& x : COMMANDS) row("cmd %s %u", x.verb, unsigned(x.flags));
  for (const TwistInfo& x : TWISTS) row("twist %s %s", x.name, x.args);
  r.ok("%u", rows);
}

void cmd_state(Dish& dish, const Request&, Reply& r) {
  Line state;
  stateLine(state, dish);
  r.line("%s", state.b);
  if (const Creature* c = std::get_if<Creature>(&dish.occupant())) {
    Line drives;
    drives.add("drives");
    for (const DriveInfo& d : DRIVES) drives.add(" %s=%d", d.name, permille(c->chemistry().drive(d.id)));
    r.line("%s", drives.b);
    const Chemistry& chem = c->chemistry();
    r.line("body life=%d injury=%d glow=%d dreaming=%d", permille(chem.chem[chem::life.v]),
           permille(chem.chem[chem::injury.v]), permille(chem.locus[locus::glow.v]), int(c->body().dreaming));
  }
  unsigned pellets = 0;
  for (const Pellet& p : dish.habitat().pellets) pellets += p.present;
  r.line("dish pantry=%u pellets=%u night=%d time=%s", unsigned(dish.habitat().pantry), pellets,
         int(dish.clock().night()), dish.timeKnown() ? "known" : "unknown");
  r.ok();
}

void cmd_chem(Dish& dish, const Request&, Reply& r) {
  const Creature* c = std::get_if<Creature>(&dish.occupant());
  if (!c) return r.err(409, "NO_CREATURE");
  unsigned n = 0;
  const Chemistry& chem = c->chemistry();
  for (uint16_t i = 1; i < kChemSlots; ++i) {
    if (chem.chem[i] == Fx::zero()) continue;
    const char* name = i >= kDriveBase && i < kDriveBase + kDriveCount ? DRIVES[i - kDriveBase].name
                                                                       : nameOf(CHEMICALS, ChemId{uint8_t(i)});
    r.line("chem=%u name=%s level=%d", unsigned(i), name, permille(chem.chem[i]));
    ++n;
  }
  for (uint16_t i = 0; i < kLocusSlots; ++i) {
    if (chem.locus[i] == Fx::zero()) continue;
    Line l;
    featureName(l, LocusId{uint8_t(i)});
    r.line("locus=%u name=%s level=%d", unsigned(i), l.b, permille(chem.locus[i]));
    ++n;
  }
  r.ok("%u", n);
}

void cmd_genome(Dish& dish, const Request&, Reply& r) {
  const Genome& g = genomeOf(dish.occupant());
  r.chunks(g.bytes().data(), g.bytes().size());
}

void cmd_gene(Dish& dish, const Request& req, Reply& r) {
  Args a{req.args};
  std::optional<uint32_t> uid = a.number(UINT16_MAX);
  if (!uid || !a.done()) return r.err(400, "BAD_ARGS");
  std::optional<GeneView> v = genomeOf(dish.occupant()).find(GeneUid{uint16_t(*uid)});
  if (!v) return r.err(404, "NO_GENE");
  const GeneTypeInfo* info = geneType(v->header.type);
  r.line("uid=%u kind=%s len=%u flags=0x%02x stage=%s gate=%u weight=%u", unsigned(*uid), info ? info->name : "unknown",
         unsigned(v->header.len), unsigned(v->header.flags), stageName(v->header.stage), unsigned(v->header.featGate),
         unsigned(v->header.mutWeight));
  if (info) {
    Describe d{r};
    info->describe(*v, d);
  } else {
    for (size_t at = 0; at < v->header.len; at += 64) {
      Line l;
      for (size_t i = at; i < std::min<size_t>(v->header.len, at + 64); ++i) l.add("%02x", unsigned(v->body[i]));
      r.line("body=%s", l.b);
    }
  }
  r.ok();
}

void cmd_brain(Dish& dish, const Request& req, Reply& r) {
  Args a{req.args};
  std::optional<uint32_t> n = a.done() ? std::optional<uint32_t>(kDefaultBeliefs) : a.number(kMaxBeliefs);
  if (!n || !a.done()) return r.err(400, "BAD_ARGS");
  const Creature* c = std::get_if<Creature>(&dish.occupant());
  if (!c) return r.err(409, "NO_CREATURE");
  std::vector<Belief> beliefs = c->brain().strongestBeliefs(uint8_t(*n));
  for (const Belief& b : beliefs) {
    Line l;
    l.add("feature=");
    featureName(l, b.feature);
    l.add(" action=%s drive=%s effect=%d conf=%d", nameOf(ACTIONS, b.action), nameOf(DRIVES, b.drive),
          permille(b.effect), permille(b.confidence));
    r.line("%s", l.b);
  }
  r.ok("%u", unsigned(beliefs.size()));
}

void cmd_lineage(Dish& dish, const Request&, Reply& r) {
  unsigned n = 0;
  dish.lineage().forEach([&](const LineageEntry& e) {
    ++n;
    if (auto* f = std::get_if<Founding>(&e))
      r.line("founding id=%012llx species=%s", (unsigned long long)(f->lineageId & 0xFFFFFFFFFFFFull), f->species);
    else if (auto* b = std::get_if<Birth>(&e))
      r.line("birth gen=%u chosen=%u of=%u ops=%u", unsigned(b->generation), unsigned(b->chosen),
             unsigned(b->clutchSize), unsigned(b->diff.ops.size()));
    else if (auto* c = std::get_if<Checkpoint>(&e))
      r.line("checkpoint gen=%u", unsigned(c->generation));
    else if (auto* d = std::get_if<Death>(&e))
      r.line("death gen=%u cause=%s age=%u feats=0x%02x name=%s", unsigned(d->generation), causeName(d->cause),
             unsigned(d->stats.ageTicks), unsigned(d->feats), d->name);
    else if (auto* x = std::get_if<Rename>(&e))
      r.line("rename gen=%u name=%s", unsigned(x->generation), x->name);
  });
  r.ok("%u", n);
}

void cmd_ancestor(Dish& dish, const Request& req, Reply& r) {
  Args a{req.args};
  std::optional<uint32_t> gen = a.number(UINT16_MAX);
  if (!gen || !a.done()) return r.err(400, "BAD_ARGS");
  std::optional<Genome> g = dish.lineage().genomeOf(uint16_t(*gen));
  if (!g) return r.err(404, "NO_GENOME");
  r.chunks(g->bytes().data(), g->bytes().size());
}

void cmd_diff(Dish& dish, const Request& req, Reply& r) {
  Args a{req.args};
  std::optional<uint32_t> gen = a.number(UINT16_MAX);
  if (!gen || !a.done()) return r.err(400, "BAD_ARGS");
  if (*gen == 0) return r.err(416, "NO_PARENT");
  std::optional<Genome> parent = dish.lineage().genomeOf(uint16_t(*gen - 1));
  std::optional<MutationDiff> diff = dish.lineage().diffOf(uint16_t(*gen));
  if (!parent || !diff) return r.err(404, "NO_DIFF");
  Describe d{r};
  describeDiff(*parent, uint16_t(*gen - 1), *diff, d);
  r.ok("%u", unsigned(diff->ops.size()));
}

void cmd_portrait(Dish& dish, const Request& req, Reply& r) {
  Args a{req.args};
  std::optional<uint32_t> gen = a.number(UINT16_MAX);
  std::optional<uint32_t> stage = a.done() ? std::optional<uint32_t>(uint32_t(Stage::Adult)) : a.number(kStageCount - 1);
  if (!gen || !stage || !a.done()) return r.err(400, "BAD_ARGS");
  std::optional<Genome> g = dish.lineage().genomeOf(uint16_t(*gen));
  if (!g) return r.err(404, "NO_GENOME");
  Appearance p = portrait(*g, Stage(*stage), uint16_t(*gen), dish.lineage().legacyFeats());
  r.line("stage=%s scale=%u seed=%08x marks=%u", stageName(p.stage), unsigned(p.scalePct), unsigned(p.lifeSeed),
         unsigned(p.markCount));
  for (const RegionInfo& region : REGIONS) {
    const Tint& t = p.regions[region.id.v];
    r.line("region=%s hue=%d sat=%u val=%u", region.name, int(t.hue), unsigned(t.sat), unsigned(t.val));
  }
  for (uint8_t i = 0; i < p.markCount; ++i)
    r.line("mark layer=%u variant=%u hue=%d", unsigned(p.marks[i].layer), unsigned(p.marks[i].variant),
           int(p.marks[i].tint.hue));
  r.ok();
}

void cmd_clutch(Dish& dish, const Request&, Reply& r) {
  const Clutch* k = std::get_if<Clutch>(&dish.occupant());
  if (!k) return r.err(409, "NO_CLUTCH");
  for (uint8_t i = 0; i < k->previewed; ++i) {
    const EggPreview& e = k->previews[i];
    r.line("egg=%u skin=%d,%u,%u cloak=%d,%u,%u shell=%d,%u,%u look=%u mind=%u", unsigned(i), int(e.skin.hue),
           unsigned(e.skin.sat), unsigned(e.skin.val), int(e.cloak.hue), unsigned(e.cloak.sat), unsigned(e.cloak.val),
           int(e.shell.hue), unsigned(e.shell.sat), unsigned(e.shell.val), unsigned(e.lookChanges),
           unsigned(e.mindChanges));
  }
  r.ok("eggs=%u cursor=%u previewed=%u", unsigned(k->count), unsigned(k->cursor), unsigned(k->previewed));
}

void cmd_snapshot(Dish& dish, const Request&, Reply& r) {
  std::vector<uint8_t> blob = Keepsake::encode(dish.live());
  r.chunks(blob.data(), blob.size());
}

void cmd_twist(Dish& dish, const Request& req, Reply& r) {
  Args a{req.args};
  std::optional<std::string_view> kind = a.word();
  if (!kind) return r.err(400, "NO_TWIST");
  for (const TwistInfo& t : TWISTS) {
    if (*kind != t.name) continue;
    switch (t.apply(dish, a)) {
      case TwistStatus::Applied: return r.ok("%s", t.name);
      case TwistStatus::Malformed: return r.err(400, "BAD_ARGS");
      case TwistStatus::OutOfRange: return r.err(416, "OUT_OF_RANGE");
      case TwistStatus::NotNow: return r.err(409, "NOT_NOW");
      case TwistStatus::BodyOnly: return r.err(403, "BODY_ONLY");
    }
  }
  r.err(404, "UNKNOWN_TWIST");
}

void cmd_time(Dish& dish, const Request& req, Reply& r) {
  Args a{req.args};
  std::optional<uint32_t> unix = a.number(UINT32_MAX);
  std::optional<int32_t> tz = a.done() ? std::optional<int32_t>(0) : a.signedNumber(-12 * 60, 14 * 60);
  if (!unix || !tz || !a.done()) return r.err(400, "BAD_ARGS");
  dish.phoneTime().set(*unix);
  dish.checkWall();
  // After any catch-up the pet tick stands for this wall time, so the day snaps here.
  dish.clock().alignToWall(*unix, int16_t(*tz));
  r.ok("caught_up=%u", unsigned(dish.lastCatchUp().ticks));
}

void cmd_sub(Dish& dish, const Request& req, Reply& r) {
  Args a{req.args};
  uint32_t mask = 0;
  if (a.done()) {
    for (const EventName& e : kEvents) mask |= e.bit;
  }
  while (std::optional<std::string_view> w = a.word()) {
    if (*w == "none") continue;
    const EventName* e = std::find_if(std::begin(kEvents), std::end(kEvents), [&](const EventName& x) { return *w == x.name; });
    if (e == std::end(kEvents)) return r.err(400, "BAD_EVENT");
    mask |= e->bit;
  }
  dish.protocol().subscribe(mask);
  r.ok("events=0x%02x", unsigned(mask));
}

void cmd_hash(Dish& dish, const Request&, Reply& r) {
  r.ok("hash=%08x tick=%u", unsigned(dish.hash()), unsigned(dish.tickCount()));
}

}  // namespace blorb
