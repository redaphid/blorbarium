#include "blorb/duel_card.h"

#include <algorithm>
#include <variant>

namespace blorb {
namespace {

constexpr Fx kHalf = Fx::ratio(1, 2);
// Where a fed, rested starter's energy sits (measured 0.76 to 0.79 over his
// first 15 minutes), so a pet at rest is not restless.
constexpr Fx kEnergyAtRest = Fx::ratio(3, 4);

uint8_t moodByte(Fx centred) {
  return uint8_t(std::clamp<int64_t>(128 + ((int64_t(centred.raw) * 128) >> Fx::kFrac), 0, 255));
}

uint8_t clampByte(int v) { return uint8_t(std::clamp(v, 0, 255)); }
int scaled(Fx level, int steps) { return int((int64_t(clamp01(level).raw) * steps + Fx::kOne / 2) >> Fx::kFrac); }

// appearance.cpp's look() and scaleFor(), which are private to it: the same
// region tints (the later gene wins, chemical-bound value, hue_shift), the
// same size and the first four marks. test_duel_card holds this equal to
// present() for every starter, so the friend sees him as his own screen does.
constexpr uint8_t kHatchlingScalePct = 55;
constexpr int kSizeLocusPct = 10;
constexpr int kHueShiftSteps = 64;
constexpr int kBoundValSteps = 127;

CardLook lookOf(const Creature& c) {
  const Phenotype& p = c.phenotype();
  const Chemistry& chem = c.chemistry();
  CardLook l;
  l.stage = c.stage();
  l.lifeSeed = c.genome().hash();
  if (c.stage() == Stage::Baby || c.stage() == Stage::Child) l.scalePct = kHatchlingScalePct;
  else if (c.stage() == Stage::Adult)
    l.scalePct = clampByte(100 * (128 + int(p.size.base ? p.size.base : 128)) / 256 +
                           scaled(chem.locus[locus::size.v], kSizeLocusPct));
  bool shellGene = false;
  for (const Phenotype::Paint& paint : p.palette) {
    Tint t = paint.tint;
    if (paint.hasBound) t.val = clampByte(t.val + scaled(chem.chem[paint.bound.v] * paint.gain, kBoundValSteps));
    l.regions[paint.region.v % kRegionCount] = t;
    shellGene = shellGene || paint.region.v % kRegionCount == region::shell.v;
  }
  if (!shellGene) l.regions[region::shell.v] = l.regions[region::skin.v];
  const int shift = scaled(chem.locus[locus::hue_shift.v], kHueShiftSteps);
  for (Tint& t : l.regions) t.hue = int8_t(((int(t.hue) + shift + 128) & 255) - 128);
  for (const Phenotype::Mark& m : p.marks) {
    if (l.markCount >= std::size(l.marks)) break;
    l.marks[l.markCount++] = CardLook::Mark{m.layer, m.variant};
  }
  return l;
}

}  // namespace

Mood cardMood(const Chemistry& c) {
  auto d = [&](DriveId id) { return c.drive(id); };
  Mood m;
  m.nerve = moodByte(c.chem[chem::mettle.v] - c.chem[chem::dread.v] - d(drive::fear));
  m.pep = moodByte(c.chem[chem::energy.v] - kEnergyAtRest + d(drive::boredom));
  m.spirit = moodByte(-(d(drive::sleepiness) + d(drive::pain)));
  m.trust = moodByte(kHalf - d(drive::loneliness) - d(drive::need_touch));
  return m;
}

BattleCard cardOf(const Creature& c, uint64_t lineageId, uint8_t level, uint8_t prowess) {
  BattleCard card;
  card.species = c.phenotype().species.body;
  card.power = c.phenotype().species.power;
  card.knack = Knack{c.phenotype().species.knackOrder};
  card.mood = cardMood(c.chemistry());
  card.level = level;
  card.prowess = prowess;
  card.lineageId = lineageId;
  card.look = lookOf(c);
  return card;
}

Decline readiness(const Occupant& o, uint8_t lossesInARow, uint32_t ticksSinceLastDuel, bool faintedLast,
                  const ReadinessRules& rules) {
  const Creature* c = std::get_if<Creature>(&o);
  if (!c || c->dead()) return Decline::NotACreature;
  if (c->body().asleep) return Decline::Asleep;
  if (rules.sleepinessBlocks && c->chemistry().drive(drive::sleepiness) >= kDeclineSleepy) return Decline::Sleepy;
  if (rules.hungerBlocks && c->chemistry().drive(drive::hunger) >= kDeclineHungry) return Decline::Hungry;
  if (ticksSinceLastDuel < (faintedLast ? kRestAfterFaintTicks : kRestTicks)) return Decline::Resting;
  if (lossesInARow >= kLossesBeforeACuddle) return Decline::WantsACuddle;
  return Decline::None;
}

Ceiling ceilingFor(const Creature& c, Stakes deviceCeiling) {
  Ceiling out;
  const bool wounded = c.chemistry().chem[chem::injury.v] >= kWoundedInjury;
  bool open = true;
  for (Stakes s : {Stakes::Spar, Stakes::Scrap, Stakes::Mortal}) {
    const StakesRule& r = rule(s);
    Decline& why = out.why[uint8_t(s)];
    if (uint8_t(c.stage()) < uint8_t(r.youngestStage)) why = Decline::TooYoung;
    else if (r.injuryCapPerDuel > Fx::zero() && wounded) why = Decline::Wounded;
    else if (r.injuryCapPerDuel > Fx::zero() && forecast(c, s).skull == 255) why = Decline::Fragile;
    else if (uint8_t(s) > uint8_t(deviceCeiling)) why = Decline::DeviceCeiling;
    open = open && why == Decline::None;
    if (open) out.top = s;
  }
  return out;
}

}  // namespace blorb
