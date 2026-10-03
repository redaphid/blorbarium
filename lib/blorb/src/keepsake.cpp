#include "blorb/keepsake.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <type_traits>
#include "blorb/seams.h"

namespace blorb {
namespace {

constexpr char kMagic[4] = {'B', 'L', 'R', 'B'};
// magic 4, format u16, seq u32, payload len u32, crc32 u32
constexpr size_t kHeaderLen = 18;
constexpr size_t kCrcAt = 14;
constexpr size_t kChunkHeaderLen = 4;
constexpr uint8_t kChunkVersion = 1;
constexpr const char* kSlots[2] = {"snap.a", "snap.b"};

enum class Kind : uint8_t { Egg = 0, Creature = 1, Clutch = 2 };

// The keepsake's own byte codec. It is deliberately not shared with the
// lineage log: the two formats evolve on their own schedules.
struct Out {
  std::vector<uint8_t> b;
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v) { u8(uint8_t(v)), u8(uint8_t(v >> 8)); }
  void u32(uint32_t v) { u16(uint16_t(v)), u16(uint16_t(v >> 16)); }
  void u64(uint64_t v) { u32(uint32_t(v)), u32(uint32_t(v >> 32)); }
  void fx(Fx v) { u32(uint32_t(v.raw)); }
  void raw(const void* p, size_t n) { b.insert(b.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n); }
  void blob(const std::vector<uint8_t>& v) { u16(uint16_t(v.size())), raw(v.data(), v.size()); }
};

// Reads past the end yield zeros and clear `ok`, so a short chunk is one
// check. Bytes left over are a newer chunk version's additions, ignored.
struct In {
  const uint8_t* p;
  size_t n, at = 0;
  bool ok = true;
  uint8_t u8() {
    if (at >= n) { ok = false; return 0; }
    return p[at++];
  }
  uint16_t u16() { uint16_t lo = u8(); return uint16_t(lo | u8() << 8); }
  uint32_t u32() { uint32_t lo = u16(); return lo | uint32_t(u16()) << 16; }
  uint64_t u64() { uint64_t lo = u32(); return lo | uint64_t(u32()) << 32; }
  Fx fx() { return Fx{int32_t(u32())}; }
  void raw(void* out, size_t len) {
    if (n - at < len) { ok = false; std::memset(out, 0, len); at = n; return; }
    std::memcpy(out, p + at, len);
    at += len;
  }
  std::vector<uint8_t> blob() {
    std::vector<uint8_t> v(u16());
    raw(v.data(), v.size());
    return v;
  }
};

void putLE32(uint8_t* at, uint32_t v) {
  for (int i = 0; i < 4; ++i) at[i] = uint8_t(v >> (8 * i));
}
uint32_t getLE32(const uint8_t* at) {
  return uint32_t(at[0]) | uint32_t(at[1]) << 8 | uint32_t(at[2]) << 16 | uint32_t(at[3]) << 24;
}

// CRC over the whole blob with its own field zeroed, so the seq is covered too.
uint32_t blobCrc(const uint8_t* data, size_t len) {
  if (len < kHeaderLen) return 0;
  std::vector<uint8_t> copy(data, data + len);
  putLE32(&copy[kCrcAt], 0);
  return crc32(copy.data(), copy.size());
}

void chunk(Out& o, Chunk tag, const Out& body) {
  o.u8(uint8_t(tag)), o.u8(kChunkVersion), o.u16(uint16_t(body.b.size()));
  o.raw(body.b.data(), body.b.size());
}

// ---- pieces with public fields ----------------------------------------------------

void putClock(Out& o, const PetClock& c) {
  o.u32(c.petTicks), o.u32(uint32_t(c.phaseOffsetTicks)), o.u32(c.darkRunTicks), o.u32(c.entrainedTicks);
}
PetClock getClock(In& in) {
  PetClock c;
  c.petTicks = in.u32();
  c.phaseOffsetTicks = int32_t(in.u32());
  c.darkRunTicks = in.u32();
  c.entrainedTicks = in.u32();
  return c;
}

void putRng(Out& o, const Rng& r) { for (uint32_t w : r.s) o.u32(w); }
Rng getRng(In& in) {
  Rng r{};
  for (uint32_t& w : r.s) w = in.u32();
  return r;
}

void putHabitat(Out& o, const Habitat& h) {
  o.u8(Habitat::kMaxPellets);
  for (const Pellet& p : h.pellets) o.fx(p.at.x), o.fx(p.at.y), o.u32(p.droppedTick), o.u8(p.present);
  o.fx(h.marble.at.x), o.fx(h.marble.at.y), o.fx(h.marble.vx), o.fx(h.marble.vy);
  o.u8(h.pantry), o.u32(h.pantryTick);
}
Habitat getHabitat(In& in) {
  Habitat h;
  uint8_t n = in.u8();
  for (uint8_t i = 0; i < n; ++i) {
    Pellet p{{in.fx(), in.fx()}, 0, false};
    p.droppedTick = in.u32();
    p.present = in.u8() != 0;
    if (i < Habitat::kMaxPellets) h.pellets[i] = p;
  }
  h.marble.at.x = in.fx(), h.marble.at.y = in.fx(), h.marble.vx = in.fx(), h.marble.vy = in.fx();
  h.pantry = in.u8();
  h.pantryTick = in.u32();
  return h;
}

void putSettings(Out& o, const Settings& s) {
  o.raw(s.name, sizeof s.name), o.u64(s.lineageId), o.u32(s.speciesSeed), o.u8(s.brightness);
}
Settings getSettings(In& in) {
  Settings s{};
  in.raw(s.name, sizeof s.name);
  s.name[sizeof s.name - 1] = 0;
  s.lineageId = in.u64();
  s.speciesSeed = in.u32();
  s.brightness = in.u8();
  return s;
}

void putSenseOut(Out& o, const SenseOut& s) {
  o.u8(s.lociCount);
  for (uint8_t i = 0; i < s.lociCount; ++i) o.u8(s.loci[i].locus.v), o.fx(s.loci[i].value);
  o.u8(s.stimCount);
  for (uint8_t i = 0; i < s.stimCount; ++i) o.u8(s.stimuli[i].v);
}
SenseOut getSenseOut(In& in) {
  SenseOut s;
  uint8_t loci = in.u8();
  for (uint8_t i = 0; i < loci; ++i) {
    LocusId l{in.u8()};
    s.set(l, in.fx());
  }
  uint8_t stims = in.u8();
  for (uint8_t i = 0; i < stims; ++i) s.fire(StimId{in.u8()});
  return s;
}

void putStats(Out& o, const LifeStats& s) {
  o.u32(s.ageTicks), o.u8(uint8_t(s.reached));
  for (uint16_t v : {s.fed, s.cradled, s.played, s.knocked, s.shaken, s.dropped, s.nights, s.dreams, s.foresights, s.hops})
    o.u16(v);
  o.u8(uint8_t(kActionCount));
  for (size_t a = 0; a < kActionCount; ++a) o.u8(ACTIONS[a].id.v), o.u16(s.actionTicks[a]);
}
LifeStats getStats(In& in) {
  LifeStats s;
  s.ageTicks = in.u32();
  s.reached = Stage(in.u8() & 3);
  for (uint16_t* v : {&s.fed, &s.cradled, &s.played, &s.knocked, &s.shaken, &s.dropped, &s.nights, &s.dreams,
                      &s.foresights, &s.hops})
    *v = in.u16();
  uint8_t n = in.u8();
  for (uint8_t i = 0; i < n; ++i) {
    ActionId id{in.u8()};
    uint16_t t = in.u16();
    for (size_t a = 0; a < kActionCount; ++a) if (ACTIONS[a].id == id) s.actionTicks[a] = t;
  }
  return s;
}

void putBody(Out& o, const Body& b) {
  o.fx(b.at.x), o.fx(b.at.y), o.fx(b.facing), o.u8(b.pose.v), o.u16(b.poseTick), o.u8(b.asleep), o.u8(b.dreaming);
  o.u8(b.reflex.has_value());
  ActiveReflex r = b.reflex.value_or(ActiveReflex{});
  o.u8(r.kind.v), o.u16(r.tick), o.u16(r.ticks), o.fx(r.strength);
}
Body getBody(In& in) {
  Body b;
  b.at = DishPos{in.fx(), in.fx()};
  b.facing = in.fx();
  b.pose = PoseId{in.u8()};
  b.poseTick = in.u16();
  b.asleep = in.u8() != 0;
  b.dreaming = in.u8() != 0;
  bool reflex = in.u8() != 0;
  ActiveReflex r{ReflexId{in.u8()}, 0, 0, {}};
  r.tick = in.u16();
  r.ticks = in.u16();
  r.strength = in.fx();
  if (reflex) b.reflex = r;
  return b;
}

void putDiff(Out& o, const MutationDiff& d) {
  o.u16(uint16_t(d.ops.size()));
  for (const MutationOp& op : d.ops) {
    o.u8(uint8_t(op.index()));
    std::visit([&o](const auto& x) {
      using T = std::decay_t<decltype(x)>;
      if constexpr (std::is_same_v<T, MutPoint>) o.u16(x.gene.v), o.u8(x.offset), o.u8(x.from), o.u8(x.to), o.u8(x.wild);
      else if constexpr (std::is_same_v<T, MutDup>) o.u16(x.gene.v), o.u16(x.copy.v);
      else if constexpr (std::is_same_v<T, MutHeirloom>) o.u16(x.after.v), o.blob(x.gene);
      else o.u16(x.gene.v);
    }, op);
  }
}
MutationDiff getDiff(In& in) {
  MutationDiff d;
  uint16_t n = in.u16();
  for (uint16_t i = 0; i < n && in.ok; ++i) {
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
      default: in.ok = false; break;
    }
  }
  return d;
}

void putBelief(Out& o, const Belief& b) {
  o.u8(b.feature.v), o.u8(b.action.v), o.u8(b.drive.v), o.fx(b.effect), o.fx(b.confidence);
}
Belief getBelief(In& in) {
  Belief b{LocusId{in.u8()}, ActionId{in.u8()}, DriveId{in.u8()}, {}, {}};
  b.effect = in.fx();
  b.confidence = in.fx();
  return b;
}

void putInstinct(Out& o, const Instinct& i) {
  for (LocusId c : i.cue) o.u8(c.v);
  o.u8(i.cueCount), o.u8(i.action.v), o.u8(i.drive.v), o.fx(i.level), o.fx(i.strength);
}
Instinct getInstinct(In& in) {
  Instinct i{};
  for (LocusId& c : i.cue) c = LocusId{in.u8()};
  i.cueCount = in.u8();
  i.action = ActionId{in.u8()};
  i.drive = DriveId{in.u8()};
  i.level = in.fx();
  i.strength = in.fx();
  return i;
}

uint32_t clutchHash(const Clutch& k) {
  uint32_t h = fnv1a(nullptr, 0);
  auto mix = [&h](uint32_t v) { h = fnv1a(&v, sizeof v, h); };
  mix(k.parent.hash());
  mix(k.generation);
  for (const Belief& b : k.heirlooms) {
    mix(uint32_t(b.feature.v) | uint32_t(b.action.v) << 8 | uint32_t(b.drive.v) << 16);
    mix(uint32_t(b.effect.raw));
    mix(uint32_t(b.confidence.raw));
  }
  mix(uint32_t(k.wildBonus.raw));
  for (uint64_t s : k.seeds) mix(uint32_t(s)), mix(uint32_t(s >> 32));
  mix(uint32_t(k.count) | uint32_t(k.cursor) << 8 | uint32_t(k.cause) << 16 | uint32_t(k.reached) << 24);
  mix(k.sinceDeath);
  return h;
}

}  // namespace

// ---- private state of the occupant ------------------------------------------------------

struct Keepsake::Codec {
  struct Chunks {   // the known chunks of one blob, by tag
    std::optional<std::vector<uint8_t>> bytes[16];
    std::vector<RawChunk> unknown;
  };

  static void putBrain(Out& o, const Brain& b) {
    Brain::Axes axes = Brain::currentAxes();
    o.u8(uint8_t(axes.features.size()));
    for (LocusId f : axes.features) o.u8(f.v);
    o.u8(uint8_t(axes.actions.size()));
    for (ActionId a : axes.actions) o.u8(a.v);
    o.u8(uint8_t(axes.drives.size()));
    for (DriveId d : axes.drives) o.u8(d.v);
    for (const auto& byAction : b.w_)
      for (const auto& byDrive : byAction)
        for (Q15 q : byDrive) o.u16(uint16_t(q.v));
    for (Fx v : b.habit_) o.fx(v);
    for (Fx v : b.startFeatures_) o.fx(v);
    for (Fx v : b.startDrives_) o.fx(v);
    o.fx(b.trace_), o.u8(b.current_.v), o.u32(b.heldTicks_), o.u8(b.episodeHead_);
    o.u8(uint8_t(std::size(b.episodes_)));
    for (const Brain::Episode& e : b.episodes_) {
      for (Fx v : e.features) o.fx(v);
      o.u8(e.action.v);
      for (Fx v : e.driveDelta) o.fx(v);
    }
    o.u16(uint16_t(b.instinctQueue_.size()));
    for (const Instinct& i : b.instinctQueue_) putInstinct(o, i);
  }

  // Weights remap by stable id. Habit, the decision's start state and the
  // episodes are positional, so they load only when the saved axes match.
  // `b` is a fresh brain.
  static void getBrain(In& in, Brain& b) {
    Brain::Axes saved;
    for (uint8_t i = 0, n = in.u8(); i < n; ++i) saved.features.push_back(LocusId{in.u8()});
    for (uint8_t i = 0, n = in.u8(); i < n; ++i) saved.actions.push_back(ActionId{in.u8()});
    for (uint8_t i = 0, n = in.u8(); i < n; ++i) saved.drives.push_back(DriveId{in.u8()});
    const size_t nf = saved.features.size(), na = saved.actions.size(), nd = saved.drives.size();
    std::vector<Q15> w(nf * na * nd);
    for (Q15& q : w) q.v = int16_t(in.u16());
    b.loadWeights(saved, w.data());
    Brain::Axes now = Brain::currentAxes();
    bool same = saved.features.size() == now.features.size() && saved.actions.size() == now.actions.size() &&
                saved.drives.size() == now.drives.size() &&
                std::equal(saved.features.begin(), saved.features.end(), now.features.begin()) &&
                std::equal(saved.actions.begin(), saved.actions.end(), now.actions.begin()) &&
                std::equal(saved.drives.begin(), saved.drives.end(), now.drives.begin());
    std::vector<Fx> habit(na), startF(nf), startD(nd);
    for (Fx& v : habit) v = in.fx();
    for (Fx& v : startF) v = in.fx();
    for (Fx& v : startD) v = in.fx();
    b.trace_ = in.fx();
    b.current_ = ActionId{in.u8()};
    b.heldTicks_ = in.u32();
    uint8_t head = in.u8();
    uint8_t episodes = in.u8();
    for (uint8_t i = 0; i < episodes; ++i) {
      Brain::Episode e{};
      std::vector<Fx> f(nf), d(nd);
      for (Fx& v : f) v = in.fx();
      e.action = ActionId{in.u8()};
      for (Fx& v : d) v = in.fx();
      if (!same || i >= std::size(b.episodes_)) continue;
      std::copy(f.begin(), f.end(), e.features);
      std::copy(d.begin(), d.end(), e.driveDelta);
      b.episodes_[i] = e;
    }
    if (same) {
      std::copy(habit.begin(), habit.end(), b.habit_);
      std::copy(startF.begin(), startF.end(), b.startFeatures_);
      std::copy(startD.begin(), startD.end(), b.startDrives_);
      b.episodeHead_ = uint8_t(head % std::size(b.episodes_));
    }
    for (uint16_t i = 0, n = in.u16(); i < n && in.ok; ++i) b.instinctQueue_.push_back(getInstinct(in));
  }

  static void putOccupant(Out& o, Out& genome, Out& chemistry, Out& brain, Out& body, Out& stats,
                          const Occupant& occ) {
    if (const Egg* e = std::get_if<Egg>(&occ)) {
      o.u8(uint8_t(Kind::Egg)), o.u16(e->generation_), o.u32(e->laidTick_), o.u32(e->incubated_);
      putDiff(o, e->child_.diff);
      genome.raw(e->genome().bytes().data(), e->genome().bytes().size());
    } else if (const Creature* c = std::get_if<Creature>(&occ)) {
      o.u8(uint8_t(Kind::Creature)), o.u16(c->generation_), o.u32(c->legacyFeats_), o.u8(uint8_t(c->stage_));
      o.u8(c->action_.v), o.u32(c->actionStart_), o.u8(c->actionDone_);
      o.u8(c->forced_.has_value()), o.u8(c->forced_.value_or(ActionId{}).v);
      o.u8(c->face_.current.v), o.u8(c->face_.previous.v), o.fx(c->face_.intensity), o.u32(c->face_.sinceTick);
      putSenseOut(o, c->pendingSelf_);
      putRng(o, c->rng_);
      genome.raw(c->genome_.bytes().data(), c->genome_.bytes().size());
      for (Fx v : c->chem_.chem) chemistry.fx(v);
      for (Fx v : c->chem_.locus) chemistry.fx(v);
      putBrain(brain, c->brain_);
      putBody(body, c->body_);
      putStats(stats, c->stats_);
    } else {
      const Clutch& k = std::get<Clutch>(occ);
      o.u8(uint8_t(Kind::Clutch)), o.u16(k.generation), o.fx(k.wildBonus);
      for (uint64_t s : k.seeds) o.u64(s);
      o.u8(k.count), o.u8(k.cursor), o.u32(k.sinceDeath), o.u8(uint8_t(k.cause)), o.u8(uint8_t(k.reached));
      o.u8(uint8_t(k.heirlooms.size()));
      for (const Belief& b : k.heirlooms) putBelief(o, b);
      genome.raw(k.parent.bytes().data(), k.parent.bytes().size());
    }
  }

  // Builds the occupant in place in `out`.
  static bool getOccupant(const Chunks& c, Occupant& out) {
    auto view = [&](Chunk tag) -> std::optional<In> {
      const auto& b = c.bytes[uint8_t(tag)];
      if (!b) return std::nullopt;
      return In{b->data(), b->size()};
    };
    std::optional<In> occ = view(Chunk::Occupant), gen = view(Chunk::Genome);
    if (!occ || !gen) return false;
    std::optional<Genome> genome = Genome::parse(gen->p, gen->n);
    if (!genome) return false;
    In& in = *occ;
    switch (Kind(in.u8())) {
      case Kind::Egg: {
        uint16_t generation = in.u16();
        uint32_t laid = in.u32(), incubated = in.u32();
        MutationDiff diff = getDiff(in);
        if (!in.ok) return false;
        Egg& e = out.emplace<Egg>(Offspring{std::move(*genome), std::move(diff)}, generation, laid);
        e.incubated_ = incubated;
        return true;
      }
      case Kind::Creature: {
        std::optional<In> chem = view(Chunk::Chemistry), brain = view(Chunk::Brain), body = view(Chunk::Body),
                          stats = view(Chunk::Stats);
        if (!chem || !brain || !body || !stats) return false;
        Creature& c = out.emplace<Creature>(Creature::Blank{});
        c.genome_ = std::move(*genome);
        c.generation_ = in.u16();
        c.legacyFeats_ = in.u32();
        c.stage_ = Stage(in.u8() & 3);
        c.action_ = ActionId{in.u8()};
        c.actionStart_ = in.u32();
        c.actionDone_ = in.u8() != 0;
        bool forced = in.u8() != 0;
        ActionId forcedId{in.u8()};
        if (forced) c.forced_ = forcedId;
        c.face_.current = ExprId{in.u8()};
        c.face_.previous = ExprId{in.u8()};
        c.face_.intensity = in.fx();
        c.face_.sinceTick = in.u32();
        c.pendingSelf_ = getSenseOut(in);
        c.rng_ = getRng(in);
        for (Fx& v : c.chem_.chem) v = chem->fx();
        for (Fx& v : c.chem_.locus) v = chem->fx();
        getBrain(*brain, c.brain_);
        c.body_ = getBody(*body);
        c.stats_ = getStats(*stats);
        if (!in.ok || !chem->ok || !brain->ok || !body->ok || !stats->ok) return false;
        // Derived, never saved: the phenotype is re-expressed stage by stage.
        // Chemical levels and the instinct queue are saved, so nothing is reseeded.
        for (uint8_t s = 0; s <= uint8_t(c.stage_); ++s) expressStage(c.genome_, Stage(s), c.legacyFeats_, c.pheno_);
        return true;
      }
      case Kind::Clutch: {
        Clutch& k = out.emplace<Clutch>();
        k.parent = std::move(*genome);
        k.generation = in.u16();
        k.wildBonus = in.fx();
        for (uint64_t& s : k.seeds) s = in.u64();
        k.count = uint8_t(std::clamp<int>(in.u8(), 1, kMaxClutch));
        k.cursor = uint8_t(in.u8() % k.count);
        k.sinceDeath = in.u32();
        k.cause = DeathCause(std::min<uint8_t>(in.u8(), uint8_t(DeathCause::Unknown)));
        k.reached = Stage(in.u8() & 3);
        for (uint8_t i = 0, n = in.u8(); i < n && in.ok; ++i) k.heirlooms.push_back(getBelief(in));
        return in.ok;
      }
    }
    return false;
  }

  static std::vector<uint8_t> encode(const Snapshot& s, uint32_t seq) {
    Out occ, genome, chemistry, brain, body, stats, clock, habitat, rng, settings, wall;
    putOccupant(occ, genome, chemistry, brain, body, stats, s.occupant);
    putClock(clock, s.clock);
    putHabitat(habitat, s.habitat);
    putRng(rng, s.rng);
    putSettings(settings, s.settings);
    Out payload;
    chunk(payload, Chunk::Clock, clock);
    chunk(payload, Chunk::Occupant, occ);
    chunk(payload, Chunk::Genome, genome);
    if (std::holds_alternative<Creature>(s.occupant)) {
      chunk(payload, Chunk::Chemistry, chemistry);
      chunk(payload, Chunk::Brain, brain);
      chunk(payload, Chunk::Body, body);
      chunk(payload, Chunk::Stats, stats);
    }
    chunk(payload, Chunk::Habitat, habitat);
    chunk(payload, Chunk::Rng, rng);
    chunk(payload, Chunk::Settings, settings);
    if (s.wall) {
      wall.u32(s.wall->wallSeconds), wall.u32(s.wall->tick);
      chunk(payload, Chunk::Wall, wall);
    }
    for (const RawChunk& r : s.unknown) {
      payload.u8(r.tag), payload.u8(r.ver), payload.u16(uint16_t(r.bytes.size()));
      payload.raw(r.bytes.data(), r.bytes.size());
    }
    Out o;
    o.raw(kMagic, sizeof kMagic), o.u16(kFormatVersion), o.u32(seq), o.u32(uint32_t(payload.b.size())), o.u32(0);
    o.raw(payload.b.data(), payload.b.size());
    putLE32(&o.b[kCrcAt], blobCrc(o.b.data(), o.b.size()));
    return std::move(o.b);
  }

  // The occupant goes last, so a blob that fails before it leaves `into` whole.
  static bool decode(const uint8_t* data, size_t len, Snapshot& into) {
    if (len < kHeaderLen || std::memcmp(data, kMagic, sizeof kMagic) != 0) return false;
    uint16_t format = uint16_t(data[4] | data[5] << 8);
    uint32_t payloadLen = getLE32(data + 10);
    if (format != kFormatVersion || payloadLen != len - kHeaderLen) return false;
    if (getLE32(data + kCrcAt) != blobCrc(data, len)) return false;
    Chunks c;
    for (size_t at = kHeaderLen; at < len;) {
      if (len - at < kChunkHeaderLen) return false;
      uint8_t tag = data[at], ver = data[at + 1];
      size_t n = size_t(data[at + 2]) | size_t(data[at + 3]) << 8;
      at += kChunkHeaderLen;
      if (len - at < n) return false;
      std::vector<uint8_t> bytes(data + at, data + at + n);
      at += n;
      bool known = tag >= uint8_t(Chunk::Clock) && tag <= uint8_t(Chunk::Wall);
      if (known) c.bytes[tag] = std::move(bytes);
      else c.unknown.push_back(RawChunk{tag, ver, std::move(bytes)});
    }
    for (Chunk required : {Chunk::Clock, Chunk::Habitat, Chunk::Rng, Chunk::Settings})
      if (!c.bytes[uint8_t(required)]) return false;
    auto in = [&](Chunk tag) { return In{c.bytes[uint8_t(tag)]->data(), c.bytes[uint8_t(tag)]->size()}; };
    In clockIn = in(Chunk::Clock), habitatIn = in(Chunk::Habitat), rngIn = in(Chunk::Rng), settingsIn = in(Chunk::Settings);
    const PetClock clock = getClock(clockIn);
    const Habitat habitat = getHabitat(habitatIn);
    const Rng rng = getRng(rngIn);
    const Settings settings = getSettings(settingsIn);
    std::optional<WallAnchor> wall;
    if (c.bytes[uint8_t(Chunk::Wall)]) {
      In wallIn = in(Chunk::Wall);
      WallAnchor a{wallIn.u32(), wallIn.u32()};
      if (!wallIn.ok) return false;
      wall = a;
    }
    if (!clockIn.ok || !habitatIn.ok || !rngIn.ok || !settingsIn.ok) return false;
    if (!getOccupant(c, into.occupant)) return false;
    into.seq = getLE32(data + 6);
    into.clock = clock;
    into.habitat = habitat;
    into.settings = settings;
    into.rng = rng;
    into.wall = wall;
    into.unknown = std::move(c.unknown);
    return true;
  }
};

// ---- the public face ----------------------------------------------------------------------

uint32_t Snapshot::hash() const {
  uint32_t h = fnv1a(nullptr, 0);
  auto mix = [&h](uint32_t v) { h = fnv1a(&v, sizeof v, h); };
  mix(uint32_t(occupant.index()));
  std::visit([&](const auto& o) {
    if constexpr (std::is_same_v<std::decay_t<decltype(o)>, Clutch>) mix(clutchHash(o));
    else mix(o.hash());
  }, occupant);
  mix(habitat.hash());
  mix(clock.petTicks), mix(uint32_t(clock.phaseOffsetTicks)), mix(clock.darkRunTicks), mix(clock.entrainedTicks);
  h = fnv1a(settings.name, sizeof settings.name, h);
  mix(uint32_t(settings.lineageId)), mix(uint32_t(settings.lineageId >> 32)), mix(settings.speciesSeed);
  mix(settings.brightness);
  for (uint32_t w : rng.s) mix(w);
  mix(wall ? 1u : 0u);
  if (wall) mix(wall->wallSeconds), mix(wall->tick);
  return h;
}

Keepsake::Keepsake(Storage& store) : store_(store) {}

std::vector<uint8_t> Keepsake::encode(const Snapshot& s) { return Codec::encode(s, s.seq); }

bool Keepsake::decode(const uint8_t* data, size_t len, Snapshot& into) {
  return data && Codec::decode(data, len, into);
}

// Version 1 is the only format; each later one adds one step here and a fixture.
bool Keepsake::migrate(std::vector<uint8_t>&, uint16_t fromVersion) { return fromVersion == kFormatVersion; }

SlotState Keepsake::load(Snapshot& into) {
  struct Slot { bool present = false, newer = false, valid = false; std::vector<uint8_t> blob; };
  Slot slots[2];
  for (int i = 0; i < 2; ++i) {
    uint8_t probe;
    if (!store_.read(kSlots[i], &probe, 0)) continue;
    Slot& s = slots[i];
    s.present = true;
    s.blob.resize(store_.size(kSlots[i]));
    std::optional<size_t> n = store_.read(kSlots[i], s.blob.data(), s.blob.size());
    s.blob.resize(n ? std::min(*n, s.blob.size()) : 0);
    bool magic = s.blob.size() >= kHeaderLen && std::memcmp(s.blob.data(), kMagic, sizeof kMagic) == 0;
    uint16_t format = magic ? uint16_t(s.blob[4] | s.blob[5] << 8) : 0;
    s.newer = magic && format > kFormatVersion;
    if (magic && format < kFormatVersion && !migrate(s.blob, format)) s.blob.clear();
  }
  // A newer firmware's keepsake is left exactly as it is, both slots.
  for (const Slot& s : slots)
    if (s.newer) return SlotState::NewerFormat;

  // Decoded one at a time into `into`, the preferred slot last (the higher
  // header seq, slot a on a tie), so when it is valid it is the one left there.
  auto seqOf = [](const Slot& s) { return s.blob.size() >= kHeaderLen ? getLE32(s.blob.data() + 6) : 0u; };
  const int last = seqOf(slots[1]) > seqOf(slots[0]) ? 1 : 0;
  int best = -1;
  for (int i : {1 - last, last}) {
    if (!slots[i].present) continue;
    slots[i].valid = decode(slots[i].blob.data(), slots[i].blob.size(), into);
    if (slots[i].valid) best = i;
  }
  // The preferred slot failed after the other decoded: `into` holds its debris.
  if (best >= 0 && best != last && slots[last].present) decode(slots[best].blob.data(), slots[best].blob.size(), into);

  bool anyCorrupt = false;
  for (int i = 0; i < 2; ++i) {
    if (!slots[i].present || slots[i].valid) continue;
    anyCorrupt = true;
    char to[32];
    uint8_t probe;
    for (unsigned k = 0;; ++k) {
      std::snprintf(to, sizeof to, "rescue/%s.%u", kSlots[i], k);
      if (!store_.read(to, &probe, 0)) break;
    }
    store_.rename(kSlots[i], to);
  }
  if (best < 0) return anyCorrupt ? SlotState::Corrupt : SlotState::Empty;
  lastSeq_ = into.seq;
  nextIsA_ = best == 1;
  return anyCorrupt ? SlotState::FellBack : SlotState::Resumed;
}

bool Keepsake::save(const Snapshot& s) {
  uint32_t seq = lastSeq_ + 1;
  std::vector<uint8_t> blob = Codec::encode(s, seq);
  if (!store_.writeAtomic(nextIsA_ ? kSlots[0] : kSlots[1], blob.data(), blob.size())) return false;
  lastSeq_ = seq;
  nextIsA_ = !nextIsA_;
  return true;
}

bool SavePolicy::due(uint32_t tick, uint32_t lastSaveTick, bool eventDirty) {
  return eventDirty || tick - lastSaveTick >= kIntervalTicks;
}

}  // namespace blorb
