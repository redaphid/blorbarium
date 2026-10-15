#include "blorb/duel_book.h"

#include <algorithm>
#include "log_frames.h"

namespace blorb {
namespace {

using logframes::In;
using logframes::Out;

constexpr logframes::LogFile kFile{"duels.log", "duels.tmp"};

// Met is what happened; the rest are a compaction's snapshot of the fold,
// written at the head of the log in this order. Downed, Trophy and Vendetta
// were 0.7.0's dark ledger (a Mortal downing, a slaying's token, a fallen
// line's vendetta): an older book may still hold them, and they are read
// past. Their numbers are never reused.
enum class Kind : uint8_t { Met = 1, Downed = 2, Self = 3, Friend = 4, Trophy = 5, Vendetta = 6, Cuddled = 7 };

// ---- the fold -------------------------------------------------------------------------

struct Dated { uint16_t generation; uint32_t at; uint16_t petDay; };

BookState::Friend& friendFor(BookState& st, uint64_t lineage, uint16_t generation) {
  auto it = std::find_if(st.friends.begin(), st.friends.end(), [&](const BookState::Friend& f) { return f.s.lineageId == lineage; });
  BookState::Friend f{};
  if (it != st.friends.end()) {
    f = *it;
    st.friends.erase(it);
  } else {
    f.s.lineageId = lineage;
    f.s.firstGeneration = generation;
  }
  st.friends.push_back(f);   // most recent last
  return st.friends.back();
}

template <class T> void sat(T& v) { if (v != T(~T(0))) ++v; }

void applyMet(BookState& st, const Meeting& m, Dated when) {
  const bool won = !m.fizzled && (m.ending == (m.me == Side::A ? Ending::AWon : Ending::BWon));
  const bool lost = !m.fizzled && (m.ending == (m.me == Side::A ? Ending::BWon : Ending::AWon));
  const bool forKeeps = m.battle.stakes != Stakes::Spar;

  if (when.generation > st.generation) st.generation = when.generation, st.xp = 0, st.skill = DuelSkill{};
  if (when.generation == st.generation) {
    st.xp = uint16_t(std::min<uint32_t>(0xFFFF, st.xp + (m.fizzled ? 1u : won ? 3u : 2u)));
    st.skill = m.skillAfter;
  }
  if (m.manualTurns > 0) st.prowess = uint8_t(st.prowess + (int(m.performance) - int(st.prowess)) / (1 << DuelBook::kProwessGainShift));
  st.manualTurns += m.manualTurns;
  if (lost) sat(st.lossStreak);
  else st.lossStreak = 0;

  const uint64_t them = m.friendCard.lineageId;
  BookState::Friend& f = friendFor(st, them, when.generation);
  if (f.s.met == 0 || f.lastDay != when.petDay) sat(f.s.daysMet);
  f.lastDay = when.petDay;
  sat(f.s.met);
  if (won) sat(f.s.won);
  else if (lost) sat(f.s.lost);
  else sat(f.s.drawn);
  f.s.species = m.friendCard.species;
  f.s.look = m.friendCard.look;
  f.s.lastAt = when.at;
  if (forKeeps) {
    if (f.forKeepsDay != when.petDay) f.forKeepsDay = when.petDay, f.forKeepsThatDay = 0;
    sat(f.forKeepsThatDay);
  }
}

// ---- byte codec -----------------------------------------------------------------------

void putBattle(Out& o, BattleId b) { o.u32(b.seed), o.u8(b.rematch), o.u8(uint8_t(b.stakes)); }
BattleId getBattle(In& in) {
  BattleId b;
  b.seed = in.u32(), b.rematch = in.u8();
  stakesFromByte(in.u8(), b.stakes);
  return b;
}

void putLook(Out& o, const CardLook& l) {
  o.u8(uint8_t(kRegionCount));
  for (const Tint& t : l.regions) o.u8(uint8_t(t.hue)), o.u8(t.sat), o.u8(t.val);
  o.u8(l.scalePct), o.u8(uint8_t(l.stage)), o.u8(l.markCount);
  for (const CardLook::Mark& k : l.marks) o.u8(k.layer), o.u8(k.variant);
  o.u32(l.lifeSeed);
}
CardLook getLook(In& in) {
  CardLook l;
  const uint8_t regions = in.u8();
  for (uint8_t r = 0; r < regions; ++r) {
    Tint t{int8_t(in.u8()), in.u8(), 0};
    t.val = in.u8();
    if (r < kRegionCount) l.regions[r] = t;
  }
  l.scalePct = in.u8(), l.stage = Stage(in.u8() % kStageCount), l.markCount = std::min<uint8_t>(in.u8(), 4);
  for (CardLook::Mark& k : l.marks) k.layer = in.u8(), k.variant = in.u8();
  l.lifeSeed = in.u32();
  return l;
}

void putCard(Out& o, const BattleCard& c) {
  o.u8(c.version), o.u8(c.species.v), o.u8(c.power.v), o.u8(c.knack.order);
  o.u8(c.mood.nerve), o.u8(c.mood.pep), o.u8(c.mood.spirit), o.u8(c.mood.trust);
  o.u8(c.level), o.u8(c.prowess), o.u64(c.lineageId);
  putLook(o, c.look);
}
BattleCard getCard(In& in) {
  BattleCard c;
  c.version = in.u8(), c.species = SpeciesId{in.u8()}, c.power = PowerId{in.u8()}, c.knack.order = in.u8();
  c.mood.nerve = in.u8(), c.mood.pep = in.u8(), c.mood.spirit = in.u8(), c.mood.trust = in.u8();
  c.level = in.u8(), c.prowess = in.u8(), c.lineageId = in.u64();
  c.look = getLook(in);
  return c;
}

void putSkill(Out& o, const DuelSkill& s) {
  for (const auto& ctx : s.win)
    for (Q15 q : ctx) o.u16(uint16_t(q.v));
  o.u16(s.battles);
}
DuelSkill getSkill(In& in) {
  DuelSkill s;
  for (auto& ctx : s.win)
    for (Q15& q : ctx) q.v = int16_t(in.u16());
  s.battles = in.u16();
  return s;
}

std::vector<uint8_t> encodeMet(const Meeting& m, Dated when) {
  Out o;
  o.u16(when.generation), o.u32(when.at), o.u16(when.petDay);
  putBattle(o, m.battle), o.u32(m.outcomeHash);
  putCard(o, m.friendCard);
  o.u8(uint8_t(m.me)), o.u8(uint8_t(m.ending)), o.u8(m.turns), o.u8(m.fizzled), o.u8(m.performance), o.u8(m.manualTurns);
  o.u8(0);                      // 0.7.0's verdict
  o.u8(uint8_t(m.fled << 4));   // bits 0..3 were 0.7.0's fell, slew, spared and was spared
  o.u8(m.scarsTaken);
  putSkill(o, m.skillAfter);
  return logframes::frame(uint8_t(Kind::Met), o);
}

struct MetEntry { Meeting m; Dated when; };
std::optional<MetEntry> decodeMet(In in) {
  MetEntry e{};
  e.when.generation = in.u16(), e.when.at = in.u32(), e.when.petDay = in.u16();
  Meeting& m = e.m;
  m.battle = getBattle(in), m.outcomeHash = in.u32();
  m.friendCard = getCard(in);
  m.me = Side(in.u8() & 1), m.ending = Ending(in.u8() % 4), m.turns = in.u8(), m.fizzled = in.u8() != 0;
  m.performance = in.u8(), m.manualTurns = in.u8();
  in.u8();   // 0.7.0's verdict
  m.fled = in.u8() & 16;
  m.scarsTaken = in.u8();
  m.skillAfter = getSkill(in);
  if (!in.ok) return std::nullopt;
  return e;
}

// The fold as entries: replaying them gives back the same BookState.
std::vector<uint8_t> snapshot(const BookState& st) {
  std::vector<uint8_t> bytes;
  auto add = [&](Kind k, const Out& o) {
    std::vector<uint8_t> f = logframes::frame(uint8_t(k), o);
    bytes.insert(bytes.end(), f.begin(), f.end());
  };
  {
    Out o;
    o.u16(st.generation), o.u16(st.xp), o.u32(0), putSkill(o, st.skill);   // the 0 was 0.7.0's feats
    o.u8(st.prowess), o.u8(st.lossStreak), o.u32(st.manualTurns);
    add(Kind::Self, o);
  }
  for (const BookState::Friend& f : st.friends) {
    Out o;
    const FriendSummary& s = f.s;
    o.u64(s.lineageId), o.u8(s.species.v), putLook(o, s.look);
    o.u16(s.met), o.u16(s.won), o.u16(s.lost), o.u16(s.drawn), o.u8(s.daysMet);
    o.u32(0);   // 0.7.0's ledger: fell to, slew, owes us, we owe
    o.u16(s.firstGeneration), o.u32(s.lastAt);
    o.u16(f.lastDay), o.u16(f.forKeepsDay), o.u8(f.forKeepsThatDay);
    add(Kind::Friend, o);
  }
  return bytes;
}

// One entry into the fold. A kind this firmware does not know is skipped,
// so a newer book still opens here.
void apply(BookState& st, uint8_t kind, In in) {
  switch (Kind(kind)) {
    case Kind::Met:
      if (std::optional<MetEntry> e = decodeMet(in)) applyMet(st, e->m, e->when);
      return;
    case Kind::Downed:
    case Kind::Trophy:
    case Kind::Vendetta:
      return;
    case Kind::Self: {
      BookState s = st;
      s.generation = in.u16(), s.xp = in.u16();
      in.u32();   // 0.7.0's feats
      s.skill = getSkill(in);
      s.prowess = in.u8(), s.lossStreak = in.u8(), s.manualTurns = in.u32();
      if (in.ok) st = std::move(s);
      return;
    }
    case Kind::Friend: {
      BookState::Friend f{};
      FriendSummary& s = f.s;
      s.lineageId = in.u64(), s.species = SpeciesId{in.u8()}, s.look = getLook(in);
      s.met = in.u16(), s.won = in.u16(), s.lost = in.u16(), s.drawn = in.u16(), s.daysMet = in.u8();
      in.u32();   // 0.7.0's ledger
      s.firstGeneration = in.u16(), s.lastAt = in.u32();
      f.lastDay = in.u16(), f.forKeepsDay = in.u16(), f.forKeepsThatDay = in.u8();
      if (in.ok) friendFor(st, s.lineageId, s.firstGeneration) = f;
      return;
    }
    case Kind::Cuddled:
      in.u32();
      if (in.ok) st.lossStreak = 0;
      return;
  }
}

// The fold of the log's first `limit` bytes; the valid prefix length in `valid`.
BookState fold(Storage& s, size_t limit, size_t* valid = nullptr) {
  BookState st;
  size_t v = logframes::walk(s, kFile, limit, [&](uint8_t k, In in, const std::vector<uint8_t>&, size_t) { apply(st, k, in); });
  if (valid) *valid = v;
  return st;
}

// Whether the log already holds an entry of this kind for this battle.
bool logged(Storage& s, Kind kind, BattleId b) {
  bool found = false;
  logframes::walk(s, kFile, SIZE_MAX, [&](uint8_t k, In in, const std::vector<uint8_t>&, size_t) {
    if (!found && k == uint8_t(kind)) {
      if (kind == Kind::Met) in.u16(), in.u32(), in.u16();
      found = getBattle(in) == b && in.ok;
    }
  });
  return found;
}

}  // namespace

DuelBook DuelBook::open(Storage& store) {
  DuelBook b;
  b.store_ = &store;
  size_t valid = 0;
  b.state_ = fold(store, SIZE_MAX, &valid);
  logframes::truncateTorn(store, kFile, valid);
  return b;
}

void DuelBook::record(const Meeting& m, uint16_t generation, uint32_t at, uint16_t petDay) {
  if (logged(*store_, Kind::Met, m.battle)) return;
  const Dated when{generation, at, petDay};
  if (append(encodeMet(m, when))) applyMet(state_, m, when);
}

void DuelBook::recordCuddle(uint32_t at) {
  if (state_.lossStreak == 0) return;
  Out o;
  o.u32(at);
  if (append(logframes::frame(uint8_t(Kind::Cuddled), o))) state_.lossStreak = 0;
}

DuelSkill DuelBook::skill(uint16_t generation) const {
  return generation == state_.generation ? state_.skill : DuelSkill{};
}

DuelBook::HeadToHead DuelBook::headToHead(uint64_t friendLineage) const {
  std::optional<FriendSummary> f = friendOf(friendLineage);
  if (!f) return {};
  return HeadToHead{f->met, f->won, f->lost};
}

uint8_t DuelBook::forKeepsOn(uint64_t friendLineage, uint16_t petDay) const {
  for (const BookState::Friend& f : state_.friends)
    if (f.s.lineageId == friendLineage) return f.forKeepsDay == petDay ? f.forKeepsThatDay : 0;
  return 0;
}

std::optional<FriendSummary> DuelBook::friendOf(uint64_t lineageId) const {
  for (const BookState::Friend& f : state_.friends)
    if (f.s.lineageId == lineageId) return f.s;
  return std::nullopt;
}

bool DuelBook::append(const std::vector<uint8_t>& bytes) {
  if (!logframes::appendWhole(*store_, kFile, bytes)) return false;
  compactIfNeeded();
  return true;
}

// Keeps the newest kKeepRecent Meetings whole, so a repeat of a recent battle
// is still a no-op, and replaces everything before them with the snapshot of
// their fold: one Friend entry per friend. Replaying the result gives the same
// book, which is what makes it safe to cut.
void DuelBook::compactIfNeeded() {
  if (store_->size(kFile.log) <= kByteCap) return;
  std::vector<size_t> mets;
  logframes::walk(*store_, kFile, SIZE_MAX, [&](uint8_t k, In, const std::vector<uint8_t>&, size_t at) {
    if (k == uint8_t(Kind::Met)) mets.push_back(at);
  });
  if (mets.size() <= kKeepRecent) return;
  const size_t cut = mets[mets.size() - kKeepRecent];
  logframes::rewrite(*store_, kFile, SIZE_MAX, [&](uint8_t, In, size_t at) { return at >= cut; },
                     snapshot(fold(*store_, cut)));
}

}  // namespace blorb
