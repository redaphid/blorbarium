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

constexpr void playTurn(Outcome& o, const BattleCard* const (&card)[2], uint32_t seed, uint8_t t, const Move (&m)[2]) {
  TurnReport& r = o.turns[t];
  SideState(&s)[2] = o.side;
  uint32_t luck[2]{};
  Stance original[2]{};
  bool dozed[2]{};
  PowerId acting[2]{};
  for (uint8_t i = 0; i < 2; ++i) {
    r.move[i] = m[i];
    luck[i] = sideLuck(seed, t, card[i]->lineageId);
    dozed[i] = r.dozed[i] = s[i].dozing;
    s[i].dozing = false;
    original[i] = r.shown[i] = m[i].stance;
  }

  // 2 powers
  for (uint8_t i = 0; i < 2; ++i) {
    const PowerInfo& p = powerInfo(card[i]->power);
    if (dozed[i] || !m[i].power || s[i].spark < p.cost) continue;
    s[i].spark = uint8_t(s[i].spark - p.cost);
    acting[i] = r.fired[i] = p.id == power::gadget ? gadgetPick(luck[i]) : p.id;
    r.powered[i] = true;
  }
  const auto phase = [&](PowerPhase ph) {
    for (uint8_t i = 0; i < 2; ++i) {
      if (!r.powered[i]) continue;
      PowerCtx c{Side(i), acting[i], original, dozed, r.shown, r.hit, r.healed, s, luck[i]};
      kPowerHandlers[acting[i].v](ph, c);
    }
  };
  phase(PowerPhase::BeforeClash);

  // 3 clash. A dozing side has no stance: it loses to anyone awake.
  int8_t win = -1;
  if (dozed[0] != dozed[1]) win = dozed[0] ? 1 : 0;
  else if (!dozed[0] && beats(r.shown[0], r.shown[1])) win = 0;
  else if (!dozed[0] && beats(r.shown[1], r.shown[0])) win = 1;
  else {
    const bool bonk0 = s[0].bonkTurns > 0, bonk1 = s[1].bonkTurns > 0;
    if (bonk0 != bonk1) win = bonk0 ? 0 : 1;
  }
  r.clash = win < 0 ? Clash::Bonk : win == 0 ? Clash::AWins : Clash::BWins;
  const uint8_t lose = uint8_t(1 - win);

  // 4 damage. A dozing winner (both asleep, one dashing) deals nothing.
  const bool clashLands = win >= 0 && !dozed[win];
  if (clashLands) r.hit[lose] = 1;
  phase(PowerPhase::OnDamage);

  // 5 blocks
  if (clashLands) {
    const bool dazzleMiss = s[win].dazzledTurns > 0 && (draw(luck[win], Purpose::Dazzle) & 1) != 0;
    if (dazzleMiss || s[lose].hushTurns > 0) {
      --r.hit[lose];
      r.blocked[lose] = true;
    }
  }
  for (uint8_t i = 0; i < 2; ++i) {
    if (r.hit[i] > 0 && s[i].shell) {
      --r.hit[i];
      s[i].shell = false;
      r.blocked[i] = true;
    }
    s[i].hearts = r.hit[i] >= s[i].hearts ? 0 : uint8_t(s[i].hearts - r.hit[i]);
  }

  // 6 after: mend, spores, then sparks and the counters
  phase(PowerPhase::AfterDamage);
  for (uint8_t i = 0; i < 2; ++i) {
    uint8_t spark = s[i].spark;
    if (!dozed[i]) ++spark;
    if (win >= 0 && i == lose) ++spark;
    s[i].spark = spark > kMeterMax ? kMeterMax : spark;
    if (s[i].bonkTurns) --s[i].bonkTurns;
    if (s[i].dazzledTurns) --s[i].dazzledTurns;
    if (s[i].hushTurns) --s[i].hushTurns;
  }
  if (clashLands) {
    r.super[win] = beats(elementOf(*card[win]), elementOf(*card[lose]));
    r.zone[win] = r.shown[win] == card[win]->knack.best();
  }
}

constexpr Outcome resolveCore(const BattleCard& a, const BattleCard& b, uint32_t seed, Stakes stakes,
                              const MoveLog& movesA, const MoveLog& movesB) {
  Outcome o{};
  o.stakes = stakes;
  const BattleCard* const card[2] = {&a, &b};
  const MoveLog* const log[2] = {&movesA, &movesB};
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
    else if (out0 || out1) {
      o.ending = out1 ? Ending::AWon : Ending::BWon;
      if (rule(stakes).downs) o.downed[out1 ? 1 : 0] = true;
    }
  }
  if (!o.over() && o.resolved == DuelRules::kMaxTurns) {
    const uint8_t h0 = o.side[0].hearts, h1 = o.side[1].hearts;
    o.ending = h0 == h1 ? Ending::Draw : h0 > h1 ? Ending::AWon : Ending::BWon;
  }

  // The verdict: only the winner's log, only the slot right after the downing.
  if (o.downed[0] != o.downed[1]) {
    const MoveLog& winner = *log[o.downed[0] ? 1 : 0];
    if (winner.count() > o.resolved) o.verdict = winner.at(o.resolved).verdict;
  }
  return o;
}

// What hit me on one turn, as a scar names it: a zap's ring over the clash's
// mark, then a Pounce's claw, then the winner's element.
constexpr Scar::Kind hitKind(const TurnReport& r, uint8_t me, const BattleCard& theirs) {
  const uint8_t them = uint8_t(1 - me);
  if (r.powered[them] && r.fired[them] == power::zap) return Scar::Kind::Ring;
  if (r.shown[them] == Stance::Pounce) return Scar::Kind::Claw;
  switch (elementOf(theirs)) {
    case Element::Splash: return Scar::Kind::Ripple;
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
