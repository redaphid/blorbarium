// v2 FINAL SKETCH: the call sites in DESIGN.md section 2, as code that
// compiles against origin/engine's headers with this folder's defs in front.
// `-DSIZES` builds a main that prints the new structures' sizes.
#include <cstdio>
#include "blorb/games.h"
#include "blorb/genes_v2.h"
#include "blorb/oracle.h"
#include "blorb/world.h"

namespace blorb {

// ---- 1. Creature::tick, the lines v2 adds ----------------------------------
struct CreatureV2Excerpt {
  Oracle oracle;
  SeerTraits seer;
  OmenEffects effects;            // from the phenotype's stimulus genes, once per stage
  Fx locus[256], drives[kDriveCount];
  ActionId action, previous;
  bool asleep;
  Vision vision;

  OmenMask omensIn(const SenseOut& routed) const {
    OmenMask m = 0;
    for (uint8_t i = 0; i < routed.stimCount; ++i)
      for (const OmenInfo& o : OMENS)
        if (o.stim == routed.stimuli[i]) m |= OmenMask(1u << o.id.v);
    return m;
  }

  void cuesInto(Fx cues[kCueCount]) const {
    size_t i = 0;
    for (LocusId l : FEATURES)
      if (!isExpectLocus(l)) cues[i++] = locus[l.v];
    for (LocusId l : ORACLE_CUES) cues[i++] = locus[l.v];
    for (size_t a = 0; a < kActionCount; ++a) cues[i++] = ACTIONS[a].id == action ? Fx::one() : Fx::zero();
    for (size_t a = 0; a < kActionCount; ++a) cues[i++] = ACTIONS[a].id == previous ? Fx::ratio(1, 2) : Fx::zero();
  }

  // Step 2: a shake lands. One draw whatever the outcome, so replays never fork.
  // A prophecy raises the trance locus, whose reflex owns the body and so
  // pre-empts the hop. Otherwise the shake's chemistry runs as always, and a
  // brace (made by expect_shaking, eating adrenaline) is what stops the hop.
  void onShake(Rng& rng, uint16_t dayBin, uint32_t tick) {
    const bool speaks = rng.chance(shakeOdds(seer.oracleChance, locus[locus::oracle_gate.v], asleep));
    if (!speaks) return;
    oracle.prophesy(seer, effects, drives, dayBin, tick, Source::Shake, rng);
    locus[locus::trance.v] = Fx::one();
  }

  // Step 4b, after chemistry and before the brain: the oracle looks ahead.
  void oracleStep(const SenseOut& routed, uint16_t dayBin, uint32_t tick, SenseOut& selfOut) {
    Fx cues[kCueCount];
    cuesInto(cues);
    const OracleOut o = oracle.step(cues, omensIn(routed), drives, dayBin, tick, seer);
    for (const TopicInfo& t : TOPICS)
      if (t.grounded) locus[t.expect.v] = o.expect[t.id.v];
    locus[locus::surprise.v] = o.surprise;
    locus[locus::let_down.v] = o.letDown;
    for (uint8_t i = 0; i < o.fireCount; ++i) selfOut.fire(o.fire[i]);
    if (o.vision.kind != Vision::Kind::None) vision = o.vision;
  }

  // Step 5: the brain decides with an outlook (Brain::think gains one argument).
  void decide(Outlook& out) const { oracle.outlook(seer, effects, drives, out); }
};

// ---- 2. Brain::learn: the observed change it credits to the last action ----
inline Fx observedChange(Fx driveAtStart, Fx driveNow, Fx anticipatedAtStart, Fx anticipatedNow) {
  return (driveNow - driveAtStart) + strengthening(anticipatedAtStart, anticipatedNow);
}

// ---- 3. Dish::tick: BOOT, routed by mode; the hold always feeds ------------
inline void boot(Mode mode, StimId raw, World& world, const WorldRules& rules, uint32_t tick, SenseOut& routed) {
  if (raw == stim::button_hold) {
    if (mode != Mode::Clutch && mode != Mode::Egg) world.dropPellet(rules, tick, routed);
    return;
  }
  const StimId meaning = route(mode, raw);
  if (meaning != stim::none) routed.fire(meaning);
}

// ---- 4. A game's turn: its rules and its context, nothing more -------------
inline void keeperTurn(GameCtx& c, uint8_t headingSector, uint8_t tiltDirection, uint8_t enteredThird) {
  const uint8_t context = uint8_t(headingSector * 4 + tiltDirection);
  const uint8_t guard = chooseMove(c.skill, context, 3, c.talent, c.rng);
  const bool saved = guard == enteredThird;
  learnMove(c.skill, context, guard, saved, c.talent);
  c.out.fire(saved ? stim::game_point : stim::game_lost);
}

// ---- 5. twists.cpp: the phone's prophecy, from 512 futures ("tomorrow, 2pm: a hungry frog")
inline bool twistProphecy(Oracle& o, uint32_t now, uint32_t tomorrow2pm, Fx confidence) {
  Prophecy p;
  p.claim.kind = Claim::Kind::DriveAbove;
  p.claim.drive = drive::hunger;
  p.claim.level = Fx::ratio(6, 10);
  p.source = Source::Phone;
  p.topic = topic::food;
  p.confidence = confidence;
  p.madeTick = now;
  p.opensTick = tomorrow2pm;
  p.closesTick = tomorrow2pm + 600;
  p.grounded = true;
  p.active = true;
  return o.accept(p, now);
}

// ---- 6. Creature::layClutch: lore from what this life learned --------------
inline uint8_t layLore(const Oracle& life, const Oracle& birth, uint8_t slots, uint16_t generation, LoreGene* out) {
  uint8_t n = 0;
  for (const Oracle::Belief& b : life.strongestLearned(birth, slots))
    out[n++] = LoreGene{uint8_t(b.kind), b.a, b.b, 0, uint8_t(b.p.raw >> 16), uint8_t(generation & 0xFF),
                        uint8_t(generation >> 8), 0};
  return n;
}

// ---- 7. starter_genome.cpp additions (genes, not code) ---------------------
// g.b.append(OracleGene{64, voice::mystic, {150, 110, 90, 160, 40, 40, 40, 120, 90, 140, 130}}, kGene);  // 1 in 4
// g.b.append(SeerGene{5, 11, 90, 26, 110, 140, 40, 90, 30}, kGene);
// g.emit(locus::expect_shaking.v, brace, unit(450), timeByte(4 * kSecond), 0);       // he braces for a shake he foresees
// g.react(brace, 1, adrenaline, 1, kNone, 0, 1 * kSecond);                           // brace eats adrenaline: no hop
// g.stimulus(stim::shake, 0, oracle_tired, 200);                                     // five in a minute close the oracle
// g.receive(oracle_tired, locus::oracle_gate.v, unit(-255), 255, 0, 0);              // ... for about an hour
// g.stimulus(stim::prophecy_kept, 0, boredom, -350, fondness, 60);                   // being right is his favourite thing
// g.stimulus(stim::prophecy_broken, 0, discomfort, 80);
// g.stimulus(stim::bell, 0, kNone, 0);                                               // the bell is born meaningless
// g.emit(locus::surprise.v, zest, unit(120), timeByte(2 * kDay), 0);                 // a neophile line
// g.emit(locus::expect_food.v, hunger, unit(60), timeByte(10 * kMinute), 0);          // appetite: he goes to the bell

}  // namespace blorb

#ifdef SIZES
int main() {
  using namespace blorb;
  std::printf("features=%zu actions=%zu drives=%zu oracleFeatures=%zu cues=%zu omens=%zu topics=%zu games=%zu\n",
              kFeatureCount, kActionCount, kDriveCount, kOracleFeatureCount, kCueCount, kOmenCount, kTopicCount,
              kGameCount);
  std::printf("brain W today=%zu v2=%zu\n", size_t(20 * 11 * 8 * 2), sizeof(Q15) * kFeatureCount * kActionCount * kDriveCount);
  std::printf("Oracle=%zu (E %zu, day map %zu)\n", sizeof(Oracle), sizeof(Q15) * kCueCount * kOmenCount,
              kDayBins * kTopicCount);
  std::printf("World=%zu Swarm=%zu Bond=%zu Games=%zu Skill=%zu\n", sizeof(World), sizeof(Swarm), sizeof(Bond),
              sizeof(Games), sizeof(Skill));
  std::printf("OmenEffects=%zu Outlook=%zu SeerTraits=%zu Prophecy=%zu LoreGene=%zu PartGene=%zu OracleGene=%zu\n",
              sizeof(OmenEffects), sizeof(Outlook), sizeof(SeerTraits), sizeof(Prophecy), sizeof(LoreGene),
              sizeof(PartGene), sizeof(OracleGene));
  std::printf("route(Live, button)=%u route(Live, button_double)=%u route(Game, button)=%u\n",
              route(Mode::Live, stim::button).v, route(Mode::Live, stim::button_double).v,
              route(Mode::Game, stim::button).v);
  return 0;
}
#endif
