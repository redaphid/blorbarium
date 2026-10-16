#include "blorb/duel_wire.h"

#include <cstdio>
#include <cstring>
#include "log_frames.h"

namespace blorb {
namespace {

enum class Kind : uint8_t { Beacon = 1, State = 2, Genome = 3 };

// Every field at a fixed width, little-endian, into one frame.
struct Out {
  uint8_t* b;
  size_t n = 0;
  void u8(uint8_t v) { b[n++] = v; }
  void u16(uint16_t v) { u8(uint8_t(v)), u8(uint8_t(v >> 8)); }
  void u32(uint32_t v) { u16(uint16_t(v)), u16(uint16_t(v >> 16)); }
  void u64(uint64_t v) { u32(uint32_t(v)), u32(uint32_t(v >> 32)); }
  void raw(const uint8_t* p, size_t len) { std::memcpy(b + n, p, len), n += len; }
};
using In = logframes::In;

// An enum byte past its last value is refused, not folded: the boundary says
// what it read or nothing.
template <class E>
bool enumIn(In& in, E last, E& out) {
  const uint8_t v = in.u8();
  out = E(v);
  return v <= uint8_t(last);
}

void putBattle(Out& o, BattleId b) { o.u32(b.seed), o.u8(b.rematch), o.u8(uint8_t(b.stakes)); }
bool getBattle(In& in, BattleId& b) {
  b.seed = in.u32(), b.rematch = in.u8();
  return enumIn(in, Stakes::Mortal, b.stakes);
}

void putCard(Out& o, const BattleCard& c, bool withLineage) {
  o.u8(c.version), o.u8(c.species.v), o.u8(c.power.v), o.u8(c.knack.order);
  o.u8(c.mood.nerve), o.u8(c.mood.pep), o.u8(c.mood.spirit), o.u8(c.mood.trust);
  o.u8(c.level), o.u8(c.prowess), o.u64(withLineage ? c.lineageId : 0);
  for (const Tint& t : c.look.regions) o.u8(uint8_t(t.hue)), o.u8(t.sat), o.u8(t.val);
  o.u8(c.look.scalePct), o.u8(uint8_t(c.look.stage)), o.u8(c.look.markCount);
  for (const CardLook::Mark& k : c.look.marks) o.u8(k.layer), o.u8(k.variant);
  o.u32(c.look.lifeSeed);
}
// Species and power stay as heard: a friend on another roster still shows
// greyed in the lobby, and speciesInfo()/powerInfo() fold an unknown id.
bool getCard(In& in, BattleCard& c) {
  c.version = in.u8(), c.species = SpeciesId{in.u8()}, c.power = PowerId{in.u8()}, c.knack.order = in.u8();
  c.mood.nerve = in.u8(), c.mood.pep = in.u8(), c.mood.spirit = in.u8(), c.mood.trust = in.u8();
  c.level = in.u8(), c.prowess = in.u8(), c.lineageId = in.u64();
  for (Tint& t : c.look.regions) t.hue = int8_t(in.u8()), t.sat = in.u8(), t.val = in.u8();
  c.look.scalePct = in.u8();
  bool ok = enumIn(in, Stage::Elder, c.look.stage);
  c.look.markCount = in.u8();
  for (CardLook::Mark& k : c.look.marks) k.layer = in.u8(), k.variant = in.u8();
  c.look.lifeSeed = in.u32();
  return ok && c.version == DuelRules::kCardVersion && c.knack.order < 6 && c.look.markCount <= 4;
}

void put(Out& o, const BeaconPacket& p) {
  o.u16(p.roster), o.u32(p.nonce), o.raw(p.wants.mac, 6), o.u16(p.seq);
  o.u8(uint8_t(p.decline)), o.u8(uint8_t(p.ceiling));
  putCard(o, p.card, false);
}
bool get(In& in, BeaconPacket& p) {
  p.roster = in.u16(), p.nonce = in.u32(), in.raw(p.wants.mac, 6), p.seq = in.u16();
  bool ok = enumIn(in, Decline::Unfair, p.decline);
  ok &= enumIn(in, Stakes::Mortal, p.ceiling);
  return getCard(in, p.card) && ok;
}

void put(Out& o, const StatePacket& p) {
  o.u16(p.roster), putBattle(o, p.battle), o.u32(p.nonce);
  o.u8(uint8_t(p.chosen)), o.u8(p.confirmed), o.u8(uint8_t(p.ceiling));
  for (Decline d : p.why) o.u8(uint8_t(d));
  for (uint8_t s : p.skull) o.u8(s);
  o.u8(p.hold);
  putCard(o, p.card, true);
  o.u8(p.moveCount), o.raw(p.moves, DuelRules::kLogSlots), o.u8(p.heard);
  o.u8(p.emoteSeq), o.u8(uint8_t(p.emote));
  o.u8(uint8_t(p.leaving | p.rematch << 1 | p.hugged << 2 | p.greeted << 3));
  o.u16(p.genomeBytes), o.u32(p.genomeHash);
}
bool get(In& in, StatePacket& p) {
  p.roster = in.u16();
  bool ok = getBattle(in, p.battle);
  p.nonce = in.u32();
  ok &= enumIn(in, Stakes::Mortal, p.chosen);
  const uint8_t confirmed = in.u8();
  ok &= confirmed <= 1;
  p.confirmed = confirmed;
  ok &= enumIn(in, Stakes::Mortal, p.ceiling);
  for (Decline& d : p.why) ok &= enumIn(in, Decline::Unfair, d);
  for (uint8_t& s : p.skull) s = in.u8();
  p.hold = in.u8();
  ok &= getCard(in, p.card);
  p.moveCount = in.u8(), in.raw(p.moves, DuelRules::kLogSlots), p.heard = in.u8();
  ok &= p.moveCount <= DuelRules::kLogSlots && p.heard <= DuelRules::kLogSlots;
  p.emoteSeq = in.u8();
  ok &= enumIn(in, Emote::Plead, p.emote);
  const uint8_t flags = in.u8();
  ok &= flags < 16;
  p.leaving = flags & 1, p.rematch = flags & 2, p.hugged = flags & 4, p.greeted = flags & 8;
  p.genomeBytes = in.u16(), p.genomeHash = in.u32();
  return ok && p.genomeBytes <= kMaxGenomeBytes;
}

void put(Out& o, const GenomePacket& p) {
  putBattle(o, p.battle), o.u64(p.have), o.u8(p.index), o.u8(p.len), o.raw(p.bytes, kGenomeChunk);
}
bool get(In& in, GenomePacket& p) {
  bool ok = getBattle(in, p.battle);
  p.have = in.u64(), p.index = in.u8(), p.len = in.u8();
  in.raw(p.bytes, kGenomeChunk);
  return ok && p.index < kMaxGenomeChunks && p.len > 0 && p.len <= kGenomeChunk &&
         size_t(p.index) * kGenomeChunk + p.len <= kMaxGenomeBytes;
}

constexpr Kind kindOf(const BeaconPacket&) { return Kind::Beacon; }
constexpr Kind kindOf(const StatePacket&) { return Kind::State; }
constexpr Kind kindOf(const GenomePacket&) { return Kind::Genome; }
constexpr size_t wireBytes(Kind k) {
  return k == Kind::Beacon ? kBeaconWireBytes : k == Kind::State ? kStateWireBytes : kGenomeWireBytes;
}

template <class P>
std::optional<Packet> body(In& in) {
  P p{};
  if (!get(in, p) || !in.ok) return std::nullopt;
  return Packet{p};
}

}  // namespace

size_t encode(const Packet& packet, uint8_t (&out)[kDatagramMax]) {
  Out o{out};
  o.u8(kWireMagic0), o.u8(kWireMagic1), o.u8(kWireVersion);
  std::visit([&](const auto& p) { o.u8(uint8_t(kindOf(p))), put(o, p); }, packet);
  o.u32(crc32(out, o.n));
  return o.n;
}

namespace {
// Why the envelope is refused, or nullptr when the body may be parsed.
const char* envelopeFault(const uint8_t* bytes, size_t len) {
  if (len < kFrameOverhead) return "short";
  if (bytes[0] != kWireMagic0 || bytes[1] != kWireMagic1) return "magic";
  if (bytes[2] != kWireVersion) return "version";
  const Kind kind = Kind(bytes[3]);
  if (kind < Kind::Beacon || kind > Kind::Genome) return "kind";
  if (len != wireBytes(kind)) return "short";
  In crc{bytes + len - 4, 4};
  if (crc.u32() != crc32(bytes, len - 4)) return "bad crc";
  return nullptr;
}
}  // namespace

std::optional<Packet> decode(const uint8_t* bytes, size_t len) {
  if (envelopeFault(bytes, len)) return std::nullopt;
  const Kind kind = Kind(bytes[3]);
  In in{bytes + 4, len - kFrameOverhead};
  switch (kind) {
    case Kind::Beacon: return body<BeaconPacket>(in);
    case Kind::State: return body<StatePacket>(in);
    case Kind::Genome: return body<GenomePacket>(in);
  }
  return std::nullopt;
}

WireText describe(const uint8_t* bytes, size_t len) {
  WireText t{};
  static constexpr const char* kTier[] = {"Spar", "Scrap", "Mortal"};
  const std::optional<Packet> p = decode(bytes, len);
  if (!p) {
    const char* why = envelopeFault(bytes, len);
    std::snprintf(t.s, sizeof t.s, "%s", why ? why : "bad body");
  } else if (const BeaconPacket* b = std::get_if<BeaconPacket>(&*p)) {
    std::snprintf(t.s, sizeof t.s, "beacon%s #%u", b->wants.none() ? "" : " claims", unsigned(b->seq));
  } else if (const StatePacket* st = std::get_if<StatePacket>(&*p)) {
    std::snprintf(t.s, sizeof t.s, "state t%u h%u %s%s%s", st->moveCount, st->heard, kTier[uint8_t(st->battle.stakes)],
                  st->leaving ? " bye" : "", st->hugged ? " hug" : "");
  } else {
    const GenomePacket& g = std::get<GenomePacket>(*p);
    uint8_t have = 0;
    for (uint64_t v = g.have; v; v &= v - 1) ++have;
    std::snprintf(t.s, sizeof t.s, "genome %u have %u", g.index, have);
  }
  return t;
}

}  // namespace blorb
