#include "blorb/traits.h"

#include <algorithm>
#include "blorb/genes.h"

namespace blorb {
namespace {

constexpr int kDoubling = 100;      // a trait's step for twice the rate
constexpr int kBytesPerDoubling = 12;   // the half-life scale: 2^(1/12) a byte

int16_t clampTrait(int v) { return int16_t(std::clamp<int>(v, -Traits::kMax, Traits::kMax)); }

// One rate made by one or more genes: the fastest gene's byte, and how many
// genes add to it. Two equal genes are twice the rate, so +100; more add the
// doublings they make (3 is 1.58 doublings, 4 is 2).
struct Rate {
  int fastest = 256;   // the lowest byte (0 = no rate), 256 = no gene at all
  int copies = 0;
  void add(uint8_t b) {
    if (b == 0) return;   // a zero gain makes nothing
    fastest = std::min<int>(fastest, b);
    ++copies;
  }
  // Against the recipe's byte: positive when it fills faster.
  int16_t lean(uint8_t norm) const {
    if (copies == 0) return int16_t(-Traits::kMax);
    static constexpr int kExtra[] = {0, 0, 100, 158, 200};
    return clampTrait((int(norm) - fastest) * kDoubling / kBytesPerDoubling + kExtra[std::min(copies, 4)]);
  }
};

}  // namespace

Traits traitsOf(const Genome& g, uint8_t stages, uint32_t legacyFeats) {
  const uint8_t always = locus::always.v, hunger = driveChem(drive::hunger).v,
                sleepiness = driveChem(drive::sleepiness).v, boredom = driveChem(drive::boredom).v,
                loneliness = driveChem(drive::loneliness).v, touch = driveChem(drive::need_touch).v,
                fear = driveChem(drive::fear).v;
  Rate hungerFill, sleepyFill, melatoninFill, boredFill, lonelyFill, touchFill, fearFill;
  int startleGain = -1, shakeFear = -1, explore = -1, happy = -1;
  g.forEach([&](const GeneView& v) {
    const GeneTypeInfo* info = geneType(v.header.type);
    if (!info || v.header.len != info->bodyLen) return;
    bool on = false;
    for (uint8_t s = 0; s < kStageCount; ++s)
      on = on || ((stages >> s & 1) && expressedAt(v.header, Stage(s), legacyFeats));
    if (!on) return;
    const uint8_t* b = v.body;
    if (v.header.type == GeneKindOf<EmitterGene>::value) {
      const bool tonic = b[0] == always && (b[4] & 3) == 1;   // digital, not inverted: a steady fill
      if (tonic && b[1] == hunger) hungerFill.add(b[3]);
      if (tonic && b[1] == boredom) boredFill.add(b[3]);
      if (tonic && b[1] == loneliness) lonelyFill.add(b[3]);
      if (tonic && b[1] == touch) touchFill.add(b[3]);
      if (tonic && b[1] == fear) fearFill.add(b[3]);
      if (b[0] == locus::asleep.v && b[1] == sleepiness && (b[4] & 2)) sleepyFill.add(b[3]);
      if (b[0] == locus::light.v && b[1] == chem::melatonin.v && (b[4] & 2)) melatoninFill.add(b[3]);
    } else if (v.header.type == GeneKindOf<ReceptorGene>::value) {
      if (b[0] == chem::adrenaline.v && b[1] == locus::startle.v && startleGain < 0) startleGain = b[4];
    } else if (v.header.type == GeneKindOf<StimulusGene>::value) {
      // The first shake gene's fear: the one every pet carries awake.
      for (int k = 0; k < 3; ++k)
        if (b[0] == stim::shake.v && b[2 + k] == fear && shakeFear < 0) shakeFear = b[5 + k];
    } else if (v.header.type == GeneKindOf<TemperamentGene>::value) {
      explore = b[2];   // the later stage's temperament replaces the earlier, as expression does
    } else if (v.header.type == GeneKindOf<ExpressionGene>::value) {
      if (b[0] == expr::happy.v) happy = std::max<int>(happy, b[1]);
    }
  });
  const RecipeNorms& n = kRecipeNorms;
  Traits t;
  t.appetite = hungerFill.lean(n.hungerFill);
  t.sleepiness = sleepyFill.lean(n.sleepinessFill);
  t.nightOwl = clampTrait(-melatoninFill.lean(n.melatoninFill));
  t.playfulness = boredFill.lean(n.boredomFill);
  t.affection = clampTrait((lonelyFill.lean(n.lonelinessFill) + touchFill.lean(n.touchFill)) / 2);
  // A shake's fright counts as a doubling per doubling of its amount (signedByte: 128 = none).
  const int fright = shakeFear < 0 ? 0 : (shakeFear - int(n.shakeFear)) * kDoubling / std::max(1, int(n.shakeFear) - 128);
  t.nerves = clampTrait((fearFill.lean(n.fearFill) + std::max(-kDoubling * 2, fright)) / 2);
  // Gain bytes 128 (0, never hops) to 160 (x2): the species rows' 0 .. 255.
  t.boldness = startleGain < 0 ? int16_t(-kDoubling) : clampTrait((startleGain - int(n.startleGain)) * kDoubling / 16);
  t.curiosity = explore < 0 ? int16_t(-2 * kDoubling) : clampTrait((explore - int(n.explore)) * kDoubling / (255 - n.explore));
  t.cheer = happy < 0 ? int16_t(-kDoubling) : clampTrait((happy - int(n.happyWeight)) * kDoubling / (255 - n.happyWeight));
  return t;
}

}  // namespace blorb
