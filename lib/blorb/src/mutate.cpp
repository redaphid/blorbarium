#include "blorb/mutate.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include "blorb/creature.h"
#include "blorb/genes.h"

namespace blorb {
namespace {

// The forced Look change moves a visible byte at least this far (of 255).
constexpr uint8_t kMinVisibleDelta = 24;
constexpr uint8_t kNudgeMax = 8;
constexpr uint8_t kFlagBits = 3;          // the widest flags byte a known gene uses
constexpr uint8_t kDefaultWeight = 64;    // mutWeight at which a gene mutates at the policy's rate
constexpr int kAttempts = 1 + 8;          // the roll, then 8 retries from the same stream
// The starter's own policy, used when a genome carries no expressed policy gene.
constexpr MutationPolicyGene kFallbackPolicy{40, 6, 4, 20, 6, 10, 3, 140};

constexpr uint32_t kDryRunTicks = 48 * kTicksPerHour;
constexpr uint32_t kDryRunStride = 5 * kTicksPerMinute;
constexpr uint32_t kDawnHour = 7, kDuskHour = 21;   // the pet clock's day
constexpr Fx kHalf = Fx::ratio(1, 2);
constexpr Fx kPinned = Fx::ratio(99, 100);

// Bytes whose change shows on the creature, per Look kind: tint hue, sat and
// val; mark density and tint; body size.
struct Visible { uint8_t type; uint8_t offsets[3]; uint8_t count; };
constexpr Visible kVisible[] = {
    {GeneKindOf<PaletteGene>::value, {1, 2, 3}, 3},
    {GeneKindOf<MarkGene>::value, {1, 2, 0}, 2},
    {GeneKindOf<SizeGene>::value, {0, 0, 0}, 1},
};

const Visible* visibleOf(uint8_t type) {
  for (const Visible& v : kVisible) if (v.type == type) return &v;
  return nullptr;
}

// A gene of a kind this firmware knows, with that kind's length; only those express.
const GeneTypeInfo* knownKind(const GeneHeader& h) {
  const GeneTypeInfo* info = geneType(h.type);
  return info && h.len == info->bodyLen ? info : nullptr;
}

Fx scaled(Fx rate, uint8_t weight) { return clamp01(Fx::sat(int64_t(rate.raw) * weight / kDefaultWeight)); }
Fx perGene(uint8_t b) { return Fx{Fx::unitByte(b).raw / 4}; }

GeneUid uidOf(const MutationOp& op) {
  return std::visit([](const auto& o) -> GeneUid {
    using T = std::decay_t<decltype(o)>;
    if constexpr (std::is_same_v<T, MutHeirloom>) return o.after;
    else return o.gene;
  }, op);
}

std::optional<GeneHeader> headerOf(const std::vector<uint8_t>& gene) {
  if (gene.size() < kGeneHeaderLen || gene.size() != kGeneHeaderLen + gene[1]) return std::nullopt;
  return GeneHeader{gene[0], gene[1], gene[2], Stage(gene[3]), gene[4], gene[5], GeneUid{uint16_t(gene[6] | gene[7] << 8)}};
}

// One op, checked against the genome it lands on: the uid must exist, a
// point's `from` must be the byte there, a new uid must be free.
std::optional<Genome> step(const Genome& g, const MutationOp& op) {
  std::optional<GeneView> v = g.find(uidOf(op));
  if (!v) return std::nullopt;
  GenomeBuilder b = GenomeBuilder::from(g);
  if (auto* p = std::get_if<MutPoint>(&op)) {
    if (p->offset >= v->header.len || v->body[p->offset] != p->from) return std::nullopt;
    b.setByte(p->gene, p->offset, p->to);
  } else if (auto* d = std::get_if<MutDup>(&op)) {
    if (g.find(d->copy)) return std::nullopt;
    GeneHeader h = v->header;
    h.uid = d->copy;
    b.insertAfter(d->gene, h, v->body);
  } else if (std::holds_alternative<MutDel>(op)) {
    b.erase(v->header.uid);
  } else if (std::holds_alternative<MutWake>(op)) {
    b.setFlags(v->header.uid, uint8_t(v->header.flags & ~GeneFlags::Dormant));
  } else if (std::holds_alternative<MutSleep>(op)) {
    b.setFlags(v->header.uid, uint8_t(v->header.flags | GeneFlags::Dormant));
  } else {
    const MutHeirloom& hl = std::get<MutHeirloom>(op);
    std::optional<GeneHeader> h = headerOf(hl.gene);
    if (!h || g.find(h->uid)) return std::nullopt;
    b.insertAfter(hl.after, *h, hl.gene.data() + kGeneHeaderLen);
  }
  return b.build();
}

// The child in progress: every op is applied the moment it is recorded,
// through the same step() apply() replays, so the diff and the child agree.
struct Draft {
  Genome cur;
  MutationDiff diff;
  GeneUid nextUid;

  bool add(const MutationOp& op) {
    std::optional<Genome> next = step(cur, op);
    if (!next) return false;
    cur = std::move(*next);
    diff.ops.push_back(op);
    return true;
  }
  GeneUid freshUid() {
    GeneUid u = nextUid;
    nextUid.v = uint16_t(nextUid.v + 1);
    return u;
  }
  bool fits(size_t extra) const { return cur.bytes().size() + extra <= kMaxGenomeBytes; }
};

uint8_t otherByte(uint8_t from, Rng& rng) { return uint8_t(from + 1 + rng.below(255)); }

uint8_t nudged(uint8_t from, uint8_t lo, uint8_t hi, Rng& rng) {
  int by = int(lo + rng.below(uint32_t(hi - lo) + 1));
  int up = from + by, down = from - by;
  if (rng.below(2) == 0) return uint8_t(up <= 255 ? up : down);
  return uint8_t(down >= 0 ? down : up);
}

// One point mutation of a known gene, by its kind's rules. nullopt if every byte is Fixed.
std::optional<MutPoint> pointOf(const GeneView& v, const GeneTypeInfo& info, Fx wild, Rng& rng) {
  uint8_t open[255];
  uint8_t n = 0;
  for (uint8_t i = 0; i < info.bodyLen; ++i) if (info.rules[i] != MutRule::Fixed) open[n++] = i;
  if (n == 0) return std::nullopt;
  uint8_t at = open[rng.below(n)], from = v.body[at];
  MutPoint p{v.header.uid, at, from, from, false};
  switch (info.rules[at]) {
    case MutRule::Any: p.to = otherByte(from, rng); break;
    case MutRule::Flag: p.to = uint8_t(from ^ (1u << rng.below(kFlagBits))); break;
    default:
      p.wild = rng.chance(wild);
      p.to = p.wild ? otherByte(from, rng) : nudged(from, 1, kNudgeMax, rng);
      break;
  }
  return p;
}

GeneClass classIn(const Genome& a, const Genome& b, GeneUid uid) {
  std::optional<GeneView> v = a.find(uid);
  if (!v) v = b.find(uid);
  const GeneTypeInfo* info = v ? geneType(v->header.type) : nullptr;
  return info ? info->cls : GeneClass::Body;
}

bool visibleChange(const MutationOp& op, const Genome& parent, const Genome& child, uint8_t minDelta) {
  if (classIn(child, parent, uidOf(op)) != GeneClass::Look) return false;
  if (std::holds_alternative<MutWake>(op)) return true;
  auto* p = std::get_if<MutPoint>(&op);
  return p && std::abs(int(p->to) - int(p->from)) >= minDelta;
}

bool mindChange(const MutationOp& op, const Genome& parent, const Genome& child) {
  return std::holds_alternative<MutHeirloom>(op) || classIn(child, parent, uidOf(op)) == GeneClass::Mind;
}

// Genes eligible for a forced change: shown now (Baby, ungated, awake) and free to drift.
template <class Pred>
std::vector<GeneView> candidates(const Genome& g, Pred pred) {
  std::vector<GeneView> out;
  g.forEach([&](const GeneView& v) {
    const GeneTypeInfo* info = knownKind(v.header);
    if (info && (v.header.flags & GeneFlags::Mutable) && !(v.header.flags & GeneFlags::Dormant) &&
        v.header.featGate == 0 && v.header.stage == Stage::Baby && pred(v, *info))
      out.push_back(v);
  });
  return out;
}

void forceLook(Draft& d, const Genome& parent, const MutationPolicy& pol, Rng& rng) {
  for (const MutationOp& op : d.diff.ops) if (visibleChange(op, parent, d.cur, pol.minVisibleDelta)) return;
  auto looks = candidates(d.cur, [](const GeneView& v, const GeneTypeInfo& i) {
    return i.cls == GeneClass::Look && visibleOf(v.header.type);
  });
  if (looks.empty()) return;
  const GeneView& v = looks[rng.below(uint32_t(looks.size()))];
  const Visible& vis = *visibleOf(v.header.type);
  uint8_t at = vis.offsets[rng.below(vis.count)], from = v.body[at];
  uint8_t delta = uint8_t(std::min<int>(pol.minVisibleDelta, 255));
  d.add(MutPoint{v.header.uid, at, from, nudged(from, delta, uint8_t(std::min(255, delta + 16)), rng), false});
}

void forceMind(Draft& d, const Genome& parent, const MutationPolicy& pol, Rng& rng) {
  for (const MutationOp& op : d.diff.ops) if (mindChange(op, parent, d.cur)) return;
  auto minds = candidates(d.cur, [](const GeneView&, const GeneTypeInfo& i) { return i.cls == GeneClass::Mind; });
  if (minds.empty()) return;
  const GeneView& v = minds[rng.below(uint32_t(minds.size()))];
  if (std::optional<MutPoint> p = pointOf(v, *knownKind(v.header), pol.wild, rng)) d.add(*p);
}

// Each parent gene gets at most one of: wake, delete, sleep, point, duplicate.
void randomPass(Draft& d, const Genome& parent, const MutationPolicy& pol, Rng& rng) {
  parent.forEach([&](const GeneView& pv) {
    std::optional<GeneView> v = d.cur.find(pv.header.uid);
    if (!v) return;
    const GeneHeader& h = v->header;
    const GeneTypeInfo* info = knownKind(h);
    if ((h.flags & GeneFlags::Dormant) && rng.chance(pol.wakePerDormant)) { d.add(MutWake{h.uid}); return; }
    if (h.flags & GeneFlags::Delable) {
      if (rng.chance(pol.delPerGene)) { d.add(MutDel{h.uid}); return; }
      if (!(h.flags & GeneFlags::Dormant) && rng.chance(pol.sleepPerGene)) { d.add(MutSleep{h.uid}); return; }
    }
    if (info && (h.flags & GeneFlags::Mutable) && rng.chance(scaled(pol.pointPerGene, h.mutWeight))) {
      if (std::optional<MutPoint> p = pointOf(*v, *info, pol.wild, rng)) d.add(*p);
      return;
    }
    if ((h.flags & GeneFlags::Dupable) && rng.chance(pol.dupPerGene) && d.fits(kGeneHeaderLen + h.len))
      d.add(MutDup{h.uid, d.freshUid()});
  });
}

uint8_t levelByte(Fx effect) { return uint8_t(std::clamp<int32_t>(128 + (effect.raw >> (Fx::kFrac - 7)), 0, 255)); }
uint8_t unitOf(Fx v) { return uint8_t((int64_t(clamp01(v).raw) * 255 + Fx::kOne / 2) >> Fx::kFrac); }

// A belief the parent held firmly becomes an instinct gene the child is born
// with. One already inherited for the same cue, action and drive is updated.
void addHeirlooms(Draft& d, const MutationPolicy& pol, const std::vector<Belief>& beliefs) {
  constexpr uint8_t kInstinct = GeneKindOf<InstinctGene>::value;
  uint8_t taken = 0;
  for (const Belief& b : beliefs) {
    if (taken >= pol.heirloomMax) break;
    if (b.confidence < pol.heirloomMinConfidence) continue;
    ++taken;
    InstinctGene body{{b.feature.v, 255, 255}, b.action.v, b.drive.v, levelByte(b.effect), unitOf(b.confidence)};
    std::optional<GeneView> same, last;
    d.cur.forEach([&](const GeneView& v) {
      if (v.header.type != kInstinct || !knownKind(v.header)) return;
      last = v;
      if ((v.header.flags & GeneFlags::Heirloom) && v.body[0] == body.cue[0] && v.body[3] == body.action &&
          v.body[4] == body.drive)
        same = v;
    });
    if (same) {
      GeneUid uid = same->header.uid;
      const uint8_t level = same->body[5], strength = same->body[6];
      if (level != body.level) d.add(MutPoint{uid, 5, level, body.level, false});
      if (strength != body.strength) d.add(MutPoint{uid, 6, strength, body.strength, false});
      continue;
    }
    if (!d.fits(kGeneHeaderLen + sizeof body)) continue;
    GeneUid uid = d.freshUid();
    constexpr uint8_t flags = GeneFlags::Mutable | GeneFlags::Dupable | GeneFlags::Delable | GeneFlags::Heirloom;
    std::vector<uint8_t> gene = {kInstinct, uint8_t(sizeof body), flags, uint8_t(Stage::Baby), 0, kDefaultWeight,
                                 uint8_t(uid.v), uint8_t(uid.v >> 8)};
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&body);
    gene.insert(gene.end(), raw, raw + sizeof body);
    GeneUid after = last ? last->header.uid : d.cur.gene(uint16_t(d.cur.geneCount() - 1)).header.uid;
    d.add(MutHeirloom{after, std::move(gene)});
  }
}

Draft startFrom(const Genome& parent) { return Draft{parent, {}, parent.nextUid()}; }

// ---- the dry run --------------------------------------------------------------------

void expressInto(const Genome& g, Stage s, Phenotype& p, Chemistry& c) {
  size_t from = p.chem.seeds.size();
  expressStage(g, s, 0, p);
  for (size_t i = from; i < p.chem.seeds.size(); ++i) c.chem[p.chem.seeds[i].chem.v] = clamp01(p.chem.seeds[i].level);
}

bool feedsEnergy(const Reaction& r) {
  bool eats = (r.a == chem::food && r.qa) || (r.b == chem::food && r.qb);
  bool makes = (r.c == chem::energy && r.qc) || (r.d == chem::energy && r.qd);
  return eats && makes && r.rate > Fx::zero();
}

}  // namespace

MutationPolicy policyOf(const Genome& g, Fx wildBonus) {
  MutationPolicyGene m = kFallbackPolicy;
  bool found = false;
  g.forEach([&](const GeneView& v) {
    if (found || v.header.type != GeneKindOf<MutationPolicyGene>::value || !knownKind(v.header) ||
        (v.header.flags & GeneFlags::Dormant))
      return;
    std::memcpy(&m, v.body, sizeof m);
    found = true;
  });
  return MutationPolicy{perGene(m.point), perGene(m.dup),  perGene(m.del),
                        Fx::unitByte(m.wake), perGene(m.sleep), clamp01(Fx::unitByte(m.wild) + wildBonus),
                        m.heirloomMax,        Fx::unitByte(m.heirloomConf), kMinVisibleDelta};
}

Offspring mutate(const Genome& parent, const MutationPolicy& pol, const std::vector<Belief>& heirlooms, Rng& rng) {
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    Draft d = startFrom(parent);
    randomPass(d, parent, pol, rng);
    addHeirlooms(d, pol, heirlooms);
    forceLook(d, parent, pol, rng);
    forceMind(d, parent, pol, rng);
    if (viability(d.cur).ok) return Offspring{std::move(d.cur), std::move(d.diff)};
  }
  Draft d = startFrom(parent);
  forceLook(d, parent, pol, rng);
  forceMind(d, parent, pol, rng);
  return Offspring{std::move(d.cur), std::move(d.diff)};
}

std::optional<Genome> apply(const Genome& parent, const MutationDiff& diff) {
  Genome g = parent;
  for (const MutationOp& op : diff.ops) {
    std::optional<Genome> next = step(g, op);
    if (!next) return std::nullopt;
    g = std::move(*next);
  }
  return g;
}

Viability viability(const Genome& g) {
  if (g.bytes().size() > kMaxGenomeBytes) return {false, "over the size cap"};
  Phenotype p{};
  Chemistry c;
  expressInto(g, Stage::Baby, p, c);
  if (c.chem[chem::life.v] == Fx::zero()) return {false, "no life chemical seeded"};
  if (std::none_of(p.chem.reactions.begin(), p.chem.reactions.end(), feedsEnergy))
    return {false, "no food -> energy reaction"};
  if (p.habitat.pantrySize == 0) return {false, "no metabolism gene"};
  bool looks = false;
  g.forEach([&](const GeneView& v) {
    const GeneTypeInfo* info = knownKind(v.header);
    looks = looks || (info && info->cls == GeneClass::Look && expressedAt(v.header, Stage::Baby, 0));
  });
  if (!looks) return {false, "no Look gene expressed"};

  // 48 pet-hours, a pellet's worth of food each refill, and nothing else.
  constexpr struct { Stage from; LocusId trigger; Stage to; } kStages[] = {
      {Stage::Baby, locus::become_child, Stage::Child},
      {Stage::Child, locus::become_adult, Stage::Adult},
      {Stage::Adult, locus::become_elder, Stage::Elder},
  };
  Stage stage = Stage::Baby;
  const uint32_t refill = p.habitat.refillTicks;
  for (uint32_t tick = 0; tick < kDryRunTicks; tick += kDryRunStride) {
    uint32_t meals = (tick + kDryRunStride + refill - 1) / refill - (tick + refill - 1) / refill;
    for (uint32_t i = 0; i < meals; ++i) c.add(chem::food, p.habitat.biteSize);
    uint32_t hour = (tick / kTicksPerHour) % 24;
    c.set(locus::always, Fx::one());
    c.set(locus::light, hour >= kDawnHour && hour < kDuskHour ? Fx::one() : Fx::zero());
    c.stepCoarse(p.chem, kDryRunStride, tick);
    for (const auto& s : kStages)
      if (stage == s.from && c.locus[s.trigger.v] >= kHalf) {
        expressInto(g, s.to, p, c);
        stage = s.to;
        break;
      }
    if (c.locus[locus::die.v] >= kHalf) return {false, "dies in the dry run"};
  }
  bool pinned = true;
  for (const DriveInfo& d : DRIVES) pinned = pinned && c.drive(d.id) >= kPinned;
  if (pinned) return {false, "every drive pinned at 1"};
  return {true, nullptr};
}

}  // namespace blorb
