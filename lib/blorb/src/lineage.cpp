#include "blorb/lineage.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include "blorb/genes.h"
#include "blorb/protocol.h"
#include "blorb/seams.h"
#include "log_frames.h"

namespace blorb {
namespace {

constexpr const char* kLog = "lineage.log";
constexpr const char* kTmp = "lineage.tmp";   // a rewrite in progress; renamed over kLog when whole
constexpr uint32_t kDay = 24 * kTicksPerHour;
constexpr uint32_t kWellFedMealsPerDay = 4;
constexpr uint16_t kDreamerDreams = 2000;
constexpr uint16_t kFifthGeneration = 5;
constexpr Fx kWildPerFeat = Fx::ratio(2, 100);

// Each entry: [kind u8][len u16 LE][payload][crc32 LE of kind, len and payload].
enum class Kind : uint8_t { Founding = 1, Birth = 2, Checkpoint = 3, Death = 4, Rename = 5 };

// ---- byte codec -----------------------------------------------------------------------

using logframes::In;
using logframes::Out;

void putGenome(Out& o, const Genome& g) { o.blob(g.bytes()); }
std::optional<Genome> getGenome(In& in) {
  std::vector<uint8_t> v = in.blob();
  return in.ok ? Genome::parse(v.data(), v.size()) : std::nullopt;
}

void putDiff(Out& o, const MutationDiff& d) {
  o.u16(uint16_t(d.ops.size()));
  for (const MutationOp& op : d.ops) {
    o.u8(uint8_t(op.index()));
    if (auto* p = std::get_if<MutPoint>(&op)) {
      o.u16(p->gene.v), o.u8(p->offset), o.u8(p->from), o.u8(p->to), o.u8(p->wild);
    } else if (auto* u = std::get_if<MutDup>(&op)) {
      o.u16(u->gene.v), o.u16(u->copy.v);
    } else if (auto* h = std::get_if<MutHeirloom>(&op)) {
      o.u16(h->after.v), o.blob(h->gene);
    } else {
      o.u16(std::visit([](const auto& x) -> uint16_t {
        if constexpr (std::is_same_v<std::decay_t<decltype(x)>, MutHeirloom>) return x.after.v;
        else return x.gene.v;
      }, op));
    }
  }
}

std::optional<MutationDiff> getDiff(In& in) {
  MutationDiff d;
  uint16_t count = in.u16();
  for (uint16_t i = 0; i < count && in.ok; ++i) {
    uint8_t tag = in.u8();
    GeneUid uid{in.u16()};
    switch (tag) {
      case 0: {
        MutPoint p{uid, in.u8(), 0, 0, false};
        p.from = in.u8(), p.to = in.u8(), p.wild = in.u8() != 0;
        d.ops.push_back(p);
        break;
      }
      case 1: d.ops.push_back(MutDup{uid, GeneUid{in.u16()}}); break;
      case 2: d.ops.push_back(MutDel{uid}); break;
      case 3: d.ops.push_back(MutWake{uid}); break;
      case 4: d.ops.push_back(MutSleep{uid}); break;
      case 5: d.ops.push_back(MutHeirloom{uid, in.blob()}); break;
      default: return std::nullopt;
    }
  }
  if (!in.ok) return std::nullopt;
  return d;
}

void putStats(Out& o, const LifeStats& s) {
  o.u32(s.ageTicks), o.u8(uint8_t(s.reached));
  for (uint16_t v : {s.fed, s.cradled, s.played, s.knocked, s.shaken, s.dropped, s.nights, s.dreams, s.foresights, s.hops})
    o.u16(v);
  o.u8(uint8_t(kActionCount));
  for (uint16_t t : s.actionTicks) o.u16(t);
}

LifeStats getStats(In& in) {
  LifeStats s;
  s.ageTicks = in.u32();
  s.reached = Stage(in.u8());
  for (uint16_t* v : {&s.fed, &s.cradled, &s.played, &s.knocked, &s.shaken, &s.dropped, &s.nights, &s.dreams,
                      &s.foresights, &s.hops})
    *v = in.u16();
  uint8_t actions = in.u8();
  for (uint8_t a = 0; a < actions; ++a) {
    uint16_t t = in.u16();
    if (a < kActionCount) s.actionTicks[a] = t;
  }
  return s;
}

void putName(Out& o, const char (&name)[16]) { o.raw(name, sizeof name); }
void getName(In& in, char (&name)[16]) {
  in.raw(name, sizeof name);
  name[sizeof name - 1] = 0;
}
void copyName(char (&to)[16], const char* from) {
  std::memset(to, 0, sizeof to);
  if (from) std::strncpy(to, from, sizeof to - 1);
}

// The founding pick's species names the line until a rename: the slug,
// capitalised. A genome with no species gene reads as grungo, so an ordinary
// pet's Founding entry stays "Grungo", byte for byte.
void copySpeciesName(char (&to)[16], const Genome& starter) {
  Phenotype p;
  expressStage(starter, Stage::Baby, 0, p);
  copyName(to, speciesInfo(p.species.body).name);
  to[0] = char(std::toupper(static_cast<unsigned char>(to[0])));
}

std::vector<uint8_t> frame(Kind k, const Out& payload) { return logframes::frame(uint8_t(k), payload); }

std::vector<uint8_t> encode(const LineageEntry& e) {
  Out o;
  if (auto* f = std::get_if<Founding>(&e)) {
    o.u64(f->lineageId), o.u32(f->at), putName(o, f->species), putGenome(o, f->genome);
    return frame(Kind::Founding, o);
  }
  if (auto* b = std::get_if<Birth>(&e)) {
    o.u16(b->generation), o.u32(b->at), o.u8(b->chosen), o.u8(b->clutchSize);
    for (uint64_t s : b->seeds) o.u64(s);
    o.u8(b->mutateVersion), putDiff(o, b->diff);
    return frame(Kind::Birth, o);
  }
  if (auto* c = std::get_if<Checkpoint>(&e)) {
    o.u16(c->generation), putGenome(o, c->genome);
    return frame(Kind::Checkpoint, o);
  }
  if (auto* d = std::get_if<Death>(&e)) {
    o.u16(d->generation), o.u32(d->at), o.u8(uint8_t(d->cause)), putStats(o, d->stats), o.u32(d->feats);
    putName(o, d->name);
    return frame(Kind::Death, o);
  }
  const Rename& r = std::get<Rename>(e);
  o.u16(r.generation), putName(o, r.name);
  return frame(Kind::Rename, o);
}

// nullopt for a kind this firmware does not know (carried, skipped) or a bad payload.
std::optional<LineageEntry> decode(Kind k, In in) {
  switch (k) {
    case Kind::Founding: {
      Founding f{in.u64(), in.u32(), Genome{}, {}};
      getName(in, f.species);
      std::optional<Genome> g = getGenome(in);
      if (!g) return std::nullopt;
      f.genome = std::move(*g);
      return f;
    }
    case Kind::Birth: {
      Birth b{in.u16(), in.u32(), {}, in.u8(), in.u8(), {}, 0};
      for (uint64_t& s : b.seeds) s = in.u64();
      b.mutateVersion = in.u8();
      std::optional<MutationDiff> d = getDiff(in);
      if (!d) return std::nullopt;
      b.diff = std::move(*d);
      return b;
    }
    case Kind::Checkpoint: {
      uint16_t gen = in.u16();
      std::optional<Genome> g = getGenome(in);
      if (!g) return std::nullopt;
      return Checkpoint{gen, std::move(*g)};
    }
    case Kind::Death: {
      Death d{in.u16(), in.u32(), DeathCause(in.u8()), {}, 0, {}};
      d.stats = getStats(in);
      d.feats = in.u32();
      getName(in, d.name);
      if (!in.ok) return std::nullopt;
      return d;
    }
    case Kind::Rename: {
      Rename r{in.u16(), {}};
      getName(in, r.name);
      if (!in.ok) return std::nullopt;
      return r;
    }
  }
  return std::nullopt;
}

constexpr logframes::LogFile kFile{kLog, kTmp};

// f(kind, payload, frame bytes, offset) per valid frame; returns the valid prefix length.
template <class F>
size_t walk(Storage& s, size_t limit, F&& f) {
  return logframes::walk(s, kFile, limit, [&](uint8_t k, In in, const std::vector<uint8_t>& frame, size_t at) {
    f(Kind(k), in, frame, at);
  });
}

template <class F>
void entries(Storage& s, F&& f) {
  walk(s, SIZE_MAX, [&](Kind k, In in, const std::vector<uint8_t>&, size_t) {
    if (std::optional<LineageEntry> e = decode(k, in)) f(*e);
  });
}

template <class Keep>
bool rewrite(Storage& s, size_t limit, Keep&& keep) {
  return logframes::rewrite(s, kFile, limit, [&](uint8_t k, In in, size_t at) { return keep(Kind(k), in, at); });
}

bool appendWhole(Storage& s, const std::vector<uint8_t>& bytes) { return logframes::appendWhole(s, kFile, bytes); }

int popcount(uint32_t v) {
  int n = 0;
  for (; v; v &= v - 1) ++n;
  return n;
}

}  // namespace

// ---- the log ----------------------------------------------------------------------------

Lineage Lineage::open(Storage& store, const Genome& starter, uint64_t lineageId, uint32_t at) {
  Lineage l;
  l.store_ = &store;
  l.id_ = lineageId;
  copySpeciesName(l.name_, starter);
  size_t valid = walk(store, SIZE_MAX, [&](Kind k, In in, const std::vector<uint8_t>&, size_t) {
    std::optional<LineageEntry> e = decode(k, in);
    if (!e) return;
    if (auto* f = std::get_if<Founding>(&*e)) l.id_ = f->lineageId, copyName(l.name_, f->species);
    if (auto* b = std::get_if<Birth>(&*e)) l.generation_ = std::max(l.generation_, b->generation);
    if (auto* d = std::get_if<Death>(&*e)) l.legacyFeats_ |= d->feats;
    if (auto* r = std::get_if<Rename>(&*e)) copyName(l.name_, r->name), l.named_ = true;
  });
  logframes::truncateTorn(store, kFile, valid);
  if (valid == 0) {
    Founding f{lineageId, at, starter, {}};
    copySpeciesName(f.species, starter);
    appendWhole(store, encode(f));
  }
  return l;
}

void Lineage::recordBirth(const Egg& egg, const Clutch& clutch, uint8_t chosen, uint32_t at) {
  uint16_t gen = egg.generation();
  if (gen <= generation_) return;
  Birth b{gen, at, egg.diff(), chosen, clutch.count, {}, kMutateVersion};
  std::copy(std::begin(clutch.seeds), std::end(clutch.seeds), b.seeds);
  std::vector<uint8_t> bytes = encode(b);
  if (gen % kCheckpointEvery == 0) {
    std::vector<uint8_t> cp = encode(Checkpoint{gen, egg.genome()});
    bytes.insert(bytes.end(), cp.begin(), cp.end());
  }
  if (append(bytes)) generation_ = gen;
}

void Lineage::recordDeath(const Creature& c, const char* name, uint32_t at, uint32_t duelFeats) {
  bool recorded = false;
  entries(*store_, [&](const LineageEntry& e) {
    if (auto* d = std::get_if<Death>(&e)) recorded = recorded || d->generation == c.generation();
  });
  if (recorded) return;
  Death d{c.generation(), at, c.cause(), c.stats(), featsOf(c.stats(), c.generation()) | duelFeats, {}};
  copyName(d.name, name);
  if (append(encode(d))) legacyFeats_ |= d.feats;
}

void Lineage::rename(const char* name) {
  Rename r{generation_, {}};
  copyName(r.name, name);
  if (std::strcmp(r.name, name_) == 0) return;
  if (append(encode(r))) copyName(name_, r.name), named_ = true;
}

void Lineage::recordEdit(uint16_t generation, const Genome& g) { append(encode(Checkpoint{generation, g})); }

bool Lineage::append(const std::vector<uint8_t>& bytes) {
  if (!appendWhole(*store_, bytes)) return false;
  compactIfNeeded();
  return true;
}

void Lineage::visit(void (*f)(const LineageEntry&, void*), void* ctx) const {
  entries(*store_, [&](const LineageEntry& e) { f(e, ctx); });
}

// One pass, holding one genome: the newest Checkpoint at or before
// `generation` (the last of a generation wins: an owner edit), then each Birth
// after it replayed as it streams past. Births follow their parent's
// Checkpoint in the log, so a later Checkpoint only ever moves the base forward.
std::optional<Genome> Lineage::genomeOf(uint16_t generation) const {
  std::optional<Genome> g;
  uint16_t at = 0;   // the generation g holds
  bool broken = false;
  entries(*store_, [&](const LineageEntry& e) {
    if (auto* f = std::get_if<Founding>(&e)) {
      if (!g) g = f->genome, at = 0;
    } else if (auto* c = std::get_if<Checkpoint>(&e)) {
      if (c->generation <= generation && (!g || c->generation >= at)) g = c->genome, at = c->generation, broken = false;
    } else if (auto* b = std::get_if<Birth>(&e)) {
      if (!g || broken || b->generation != at + 1 || b->generation > generation) return;
      std::optional<Genome> next = apply(*g, b->diff);
      if (next) g = std::move(*next), at = b->generation;
      else broken = true;
    }
  });
  if (!g || broken || at != generation) return std::nullopt;
  return g;
}

std::optional<MutationDiff> Lineage::diffOf(uint16_t generation) const {
  std::optional<MutationDiff> out;
  entries(*store_, [&](const LineageEntry& e) {
    if (auto* b = std::get_if<Birth>(&e)) if (!out && b->generation == generation) out = b->diff;
  });
  return out;
}

// Keeps the founding, every Death, the newest Rename of each generation, and
// what rebuilds the last kKeepDetailGenerations generations: the newest
// Checkpoint at or before the cutoff, the newest Checkpoint of each generation
// after it (an owner edit supersedes the ones before it), and every Birth
// after it. Two streaming passes; RAM holds one frame and a short index.
void Lineage::compactIfNeeded() {
  if (store_->size(kLog) <= kLineageByteCap) return;
  const uint16_t cutoff = generation_ > kKeepDetailGenerations ? uint16_t(generation_ - kKeepDetailGenerations) : 0;
  uint16_t base = 0;
  struct Newest { Kind kind; uint16_t generation; size_t at; };
  std::vector<Newest> newest;
  walk(*store_, SIZE_MAX, [&](Kind k, In in, const std::vector<uint8_t>&, size_t at) {
    if (k != Kind::Checkpoint && k != Kind::Rename) return;
    const uint16_t gen = in.u16();
    if (k == Kind::Checkpoint && gen <= cutoff) base = std::max(base, gen);
    auto it = std::find_if(newest.begin(), newest.end(), [&](const Newest& n) { return n.kind == k && n.generation == gen; });
    if (it == newest.end()) newest.push_back(Newest{k, gen, at});
    else it->at = at;
  });
  auto isNewest = [&](Kind k, uint16_t gen, size_t at) {
    return std::any_of(newest.begin(), newest.end(), [&](const Newest& n) { return n.kind == k && n.generation == gen && n.at == at; });
  };
  rewrite(*store_, SIZE_MAX, [&](Kind k, In in, size_t at) {
    switch (k) {
      case Kind::Birth: return in.u16() > base;
      case Kind::Checkpoint: {
        const uint16_t gen = in.u16();
        return gen >= base && isNewest(k, gen, at);
      }
      case Kind::Rename: return isNewest(k, in.u16(), at);
      default: return true;
    }
  });
}

// ---- feats and unlocks ----------------------------------------------------------------

bool feat_reached_adult(const LifeStats& s, uint16_t) { return s.reached >= Stage::Adult; }
bool feat_reached_elder(const LifeStats& s, uint16_t) { return s.reached >= Stage::Elder; }
bool feat_lived_a_week(const LifeStats& s, uint16_t) { return s.ageTicks >= 7 * kDay; }
bool feat_well_fed(const LifeStats& s, uint16_t) {
  uint32_t days = s.ageTicks / kDay;
  return days >= 1 && s.fed >= kWellFedMealsPerDay * days;
}
bool feat_dreamer(const LifeStats& s, uint16_t) { return s.dreams >= kDreamerDreams; }
bool feat_fifth_generation(const LifeStats&, uint16_t generation) { return generation >= kFifthGeneration; }

uint32_t featsOf(const LifeStats& s, uint16_t generation) {
  uint32_t feats = 0;
#define BLORB_FEAT(bit, name) if (feat_##name(s, generation)) feats |= feat::name;
#include "blorb/defs/feats.def"
#undef BLORB_FEAT
  return feats;
}

Unlocks unlocksFor(uint32_t legacyFeats) {
  uint8_t size = uint8_t(1 + ((legacyFeats & feat::reached_adult) != 0) + ((legacyFeats & feat::reached_elder) != 0));
  return Unlocks{std::min<uint8_t>(size, kMaxClutch), Fx{kWildPerFeat.raw * popcount(legacyFeats)}};
}

// ---- describing a diff ----------------------------------------------------------------

void describeDiff(const Genome& parent, uint16_t parentGeneration, const MutationDiff& diff, Describe& d) {
  static const char* const kOps[] = {"point", "dup", "del", "wake", "sleep", "heirloom"};
  Genome g = parent;
  for (const MutationOp& op : diff.ops) {
    GeneUid uid = std::visit([](const auto& x) -> GeneUid {
      if constexpr (std::is_same_v<std::decay_t<decltype(x)>, MutHeirloom>) return x.after;
      else return x.gene;
    }, op);
    d.field(kOps[op.index()], uid.v);
    std::optional<GeneView> v = g.find(uid);
    const GeneTypeInfo* info = v ? geneType(v->header.type) : nullptr;
    if (info) d.field("kind", info->name);
    if (auto* p = std::get_if<MutPoint>(&op)) {
      d.field("byte", p->offset);
      d.field("from", p->from);
      d.field("to", p->to);
      if (p->wild) d.field("wild", 1);
    }
    if (auto* h = std::get_if<MutHeirloom>(&op)) {
      if (std::optional<Genome> one = Genome::parse(h->gene.data(), h->gene.size()); one && one->geneCount() == 1)
        if (const GeneTypeInfo* hi = geneType(one->gene(0).header.type)) hi->describe(one->gene(0), d);
      d.field("learned", parentGeneration);
    }
    std::optional<Genome> next = apply(g, MutationDiff{{op}});
    if (!next) return;
    g = std::move(*next);
  }
}

}  // namespace blorb
