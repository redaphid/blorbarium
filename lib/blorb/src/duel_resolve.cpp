// resolve(): the whole battle, replayed from turn 0 on every call. The turn
// order is duel.h's; this file is that list in code. The core is constexpr so
// tools/duel_determinism.sh can have the S3 compiler evaluate the golden
// battles and static_assert the hashes the host test pins.
#include "blorb/duel.h"
#include "duel_powers.cpp"

namespace blorb {
namespace duel_detail {

// What one turn's luck is drawn for. Each use hashes the side's luck with its
// purpose, so gadget's pick and a dazzle miss in one turn are independent.
enum class Purpose : uint8_t { Gadget = 1, Dazzle = 2, Scar = 3, Mind = 4 };

// FNV's low bits barely mix (an odd multiplier keeps bit 0), and a dazzle
// miss is one bit, so every draw goes through murmur's finaliser.
constexpr uint32_t mix(uint32_t h) {
  h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
  return h;
}
constexpr uint32_t sideLuck(uint32_t seed, uint8_t turn, uint64_t lineage) {
  return fnvWord(fnvWord(fnvByte(fnvWord(2166136261u, seed), turn), uint32_t(lineage)), uint32_t(lineage >> 32));
}
constexpr uint32_t draw(uint32_t luck, Purpose p) { return mix(fnvByte(luck, uint8_t(p))); }

// One of the other eight powers, evenly.
constexpr PowerId gadgetPick(uint32_t luck) {
  uint8_t pick = uint8_t(draw(luck, Purpose::Gadget) % (kPowerCount - 1));
  for (const PowerInfo& p : POWERS)
    if (p.id != power::gadget && pick-- == 0) return p.id;
  return power::foresee;
}

constexpr Element elementOf(const BattleCard& c) { return speciesInfo(c.species).element; }

using Special = TurnReport::Special;

// Step 1 for one side: what it plays this turn, and whether its special lands.
// Asleep while winding up, a special keeps winding; asleep on its landing
// turn, it is lost.
constexpr bool takeStance(TurnReport& r, SideState& s, const BattleCard& card, uint8_t i, uint8_t t, const Move& m) {
  r.shown[i] = m.stance;
  if (s.queued != Windup::None) {
    if (t < s.landsOn) r.bracing[i] = !r.dozed[i];
    else if (r.dozed[i]) s.queued = Windup::None;
    else return true;
    return false;
  }
  if (m.windup == Windup::None || r.dozed[i]) return false;
  const uint8_t cost = powerInfo(card.power).cost;
  const uint8_t lands = uint8_t(t + uint8_t(m.windup));
  if (s.spark < cost || lands >= DuelRules::kMaxTurns) return false;
  s.spark = uint8_t(s.spark - cost);
  s.queued = m.windup;
  s.landsOn = lands;
  r.bracing[i] = true;
  r.special[i] = Special::Committed;
  return false;
}

// Step 2 for one landing side. Reads only what neither landing writes.
constexpr void land(TurnReport& r, SideState (&s)[2], const BattleCard& card, uint8_t i, uint32_t luck, PowerId& acting) {
  const uint8_t o = uint8_t(1 - i);
  const PowerId p = powerInfo(card.power).id;
  const PowerId fires = p == power::gadget ? gadgetPick(luck) : p;
  const uint8_t hearts = uint8_t(1 + SpecialRules::extra(s[i].queued));
  const bool countered = !r.dozed[o] && !r.bracing[o] && beats(r.shown[o], r.shown[i]);
  if (countered && seesTheRiposte(fires)) {
    r.special[i] = Special::Whiffed;
  } else if (countered) {
    r.hit[i] = uint8_t(r.hit[i] + hearts);
    r.special[i] = Special::Riposted;
  } else if (s[o].hushTurns > 0) {
    r.special[i] = Special::Whiffed;
  } else {
    r.hit[o] = uint8_t(r.hit[o] + hearts);
    r.special[i] = Special::Landed;
    r.powered[i] = true;
    acting = r.fired[i] = fires;
  }
}

// Step 3: a side strikes in a clash only awake and not bracing; a sleeper
// loses to any striker, and a bracer only to a Trick.
constexpr bool strikes(const TurnReport& r, uint8_t i) { return !r.dozed[i] && !r.bracing[i]; }
constexpr bool winsClash(const TurnReport& r, uint8_t a, uint8_t b) {
  if (!strikes(r, a)) return false;
  if (r.dozed[b]) return true;
  if (r.bracing[b]) return r.shown[a] == Stance::Trick;
  return beats(r.shown[a], r.shown[b]);
}

constexpr void clash(TurnReport& r, SideState (&s)[2], const BattleCard* const (&card)[2], const uint32_t (&luck)[2]) {
  int8_t win = -1;
  if (winsClash(r, 0, 1)) win = 0;
  else if (winsClash(r, 1, 0)) win = 1;
  else if ((s[0].bonkTurns > 0) != (s[1].bonkTurns > 0)) win = s[0].bonkTurns > 0 ? 0 : 1;
  r.clash = win < 0 ? Clash::Bonk : win == 0 ? Clash::AWins : Clash::BWins;
  if (win < 0 || !strikes(r, uint8_t(win))) return;
  const uint8_t w = uint8_t(win), l = uint8_t(1 - win);
  const bool dazzleMiss = s[w].dazzledTurns > 0 && (draw(luck[w], Purpose::Dazzle) & 1) != 0;
  if (dazzleMiss || s[l].hushTurns > 0) r.blocked[l] = true;
  else ++r.hit[l];
  if (r.bracing[l]) {
    s[l].queued = s[l].queued == Windup::Heavy ? Windup::Quick : Windup::None;
    r.special[l] = Special::Staggered;
  }
  r.super[w] = beats(elementOf(*card[w]), elementOf(*card[l]));
  r.zone[w] = r.shown[w] == card[w]->knack.best();
}

constexpr void playTurn(Outcome& o, const BattleCard* const (&card)[2], uint32_t seed, uint8_t t, const Move (&m)[2]) {
  TurnReport& r = o.turns[t];
  SideState(&s)[2] = o.side;
  uint32_t luck[2]{};
  bool landing[2]{};
  PowerId acting[2]{};
  for (uint8_t i = 0; i < 2; ++i) {
    r.move[i] = m[i];
    luck[i] = sideLuck(seed, t, card[i]->lineageId);
    r.dozed[i] = s[i].dozing;
    s[i].dozing = false;
    landing[i] = takeStance(r, s[i], *card[i], i, t, m[i]);
  }

  // 2 landings, or 3 the clash
  if (landing[0] || landing[1]) {
    for (uint8_t i = 0; i < 2; ++i)
      if (landing[i]) land(r, s, *card[i], i, luck[i], acting[i]);
    for (uint8_t i = 0; i < 2; ++i)
      if (landing[i]) s[i].queued = Windup::None;
  } else {
    clash(r, s, card, luck);
  }

  // 4 the fired powers, around the shell
  const auto phase = [&](PowerPhase ph) {
    for (uint8_t i = 0; i < 2; ++i) {
      if (!r.powered[i]) continue;
      PowerCtx c{Side(i), acting[i], r.hit, r.healed, s, luck[i]};
      kPowerHandlers[acting[i].v](ph, c);
    }
  };
  phase(PowerPhase::OnDamage);
  for (uint8_t i = 0; i < 2; ++i) {
    if (r.hit[i] > 0 && s[i].shell) {
      --r.hit[i];
      s[i].shell = false;
      r.blocked[i] = true;
    }
    s[i].hearts = r.hit[i] >= s[i].hearts ? 0 : uint8_t(s[i].hearts - r.hit[i]);
  }
  phase(PowerPhase::AfterDamage);

  // 5 sparks and the counters
  for (uint8_t i = 0; i < 2; ++i) {
    uint8_t spark = s[i].spark;
    if (!r.dozed[i]) ++spark;
    if (r.hit[i] > 0 && r.hit[1 - i] == 0) ++spark;
    s[i].spark = spark > kMeterMax ? kMeterMax : spark;
    if (s[i].bonkTurns) --s[i].bonkTurns;
    if (s[i].dazzledTurns) --s[i].dazzledTurns;
    if (s[i].hushTurns) --s[i].hushTurns;
  }
}

constexpr Outcome resolveCore(const BattleCard& a, const BattleCard& b, uint32_t seed, Stakes stakes,
                              const MoveLog& movesA, const MoveLog& movesB) {
  Outcome o{};
  o.stakes = stakes;
  const BattleCard* const card[2] = {&a, &b};
  for (uint8_t i = 0; i < 2; ++i) {
    SideState& s = o.side[i];
    s.hearts = s.maxHearts = uint8_t(DuelRules::kHearts + Assist::hearts(card[i]->prowess, card[1 - i]->prowess));
    if (beats(elementOf(*card[i]), elementOf(*card[1 - i]))) s.spark = DuelRules::kElementHeadStart;
  }

  const uint8_t known = movesA.count() < movesB.count() ? movesA.count() : movesB.count();
  for (uint8_t t = 0; t < DuelRules::kMaxTurns && t < known && !o.over(); ++t) {
    const Move m[2] = {movesA.at(t), movesB.at(t)};
    playTurn(o, card, seed, t, m);
    o.resolved = uint8_t(t + 1);
    const bool out0 = o.side[0].hearts == 0, out1 = o.side[1].hearts == 0;
    if (out0 && out1) o.ending = Ending::Draw;
    else if (out0 || out1) o.ending = out1 ? Ending::AWon : Ending::BWon;
  }
  if (!o.over() && o.resolved == DuelRules::kMaxTurns) {
    const uint8_t h0 = o.side[0].hearts, h1 = o.side[1].hearts;
    o.ending = h0 == h1 ? Ending::Draw : h0 > h1 ? Ending::AWon : Ending::BWon;
  }
  return o;
}

// What hit me on one turn, as a scar names it: a zap's bite over the strike's
// mark, then a Pounce's claw, then the striker's element. A riposte is struck
// by the friend's stance like any other hit.
constexpr Scar::Kind hitKind(const TurnReport& r, uint8_t me, const BattleCard& theirs) {
  const uint8_t them = uint8_t(1 - me);
  if (r.powered[them] && r.fired[them] == power::zap) return Scar::Kind::Bite;
  if (r.shown[them] == Stance::Pounce) return Scar::Kind::Claw;
  switch (elementOf(theirs)) {
    case Element::Splash: return Scar::Kind::Stitches;
    case Element::Spark: return Scar::Kind::Scorch;
    case Element::Sprout: return Scar::Kind::Notch;
  }
  return Scar::Kind::Claw;
}

}  // namespace duel_detail

Outcome resolve(const BattleCard& a, const BattleCard& b, uint32_t seed, Stakes stakes, const MoveLog& movesA,
                const MoveLog& movesB) {
  return duel_detail::resolveCore(a, b, seed, stakes, movesA, movesB);
}

void turnHits(const Outcome& o, Side me, uint32_t seed, const BattleCard& mine, const BattleCard& theirs,
              TurnHit* out) {
  using namespace duel_detail;
  const uint8_t i = uint8_t(me);
  for (uint8_t t = 0; t < o.resolved; ++t) {
    const TurnReport& r = o.turns[t];
    out[t] = TurnHit{r.hit[i], hitKind(r, i, theirs), draw(sideLuck(seed, t, mine.lineageId), Purpose::Scar)};
  }
}

}  // namespace blorb
