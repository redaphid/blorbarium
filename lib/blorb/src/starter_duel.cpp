#include "blorb/starter_duel.h"

#include "blorb/genes.h"
#include "blorb/registry.h"

namespace blorb {
namespace {

constexpr uint32_t kSecond = 1000 / kTickMs;
constexpr uint32_t kMinute = kTicksPerMinute;
constexpr uint32_t kDay = 24 * kTicksPerHour;

// The half-life scale and the signed-amount byte, as starter_genome.cpp has them.
constexpr uint32_t kSemitoneQ16[12] = {65536, 69433, 73562, 77936, 82570, 87480,
                                       92682, 98193, 104032, 110218, 116772, 123715};
constexpr uint8_t timeByte(uint64_t ticks) {
  uint8_t best = 1;
  uint64_t bestErr = UINT64_MAX;
  for (uint32_t b = 1; b <= 255; ++b) {
    uint64_t t = (uint64_t(10) * kSemitoneQ16[(b - 1) % 12]) << ((b - 1) / 12);
    uint64_t want = ticks << 16;
    uint64_t err = t > want ? t - want : want - t;
    if (err < bestErr) bestErr = err, best = uint8_t(b);
  }
  return best;
}
static_assert(timeByte(10) == 1 && timeByte(20) == 13 && timeByte(3 * kDay) == 217, "the half-life scale");
constexpr uint8_t unit(uint32_t permille) { return uint8_t((permille * 255 + 500) / 1000); }
constexpr uint8_t sgn(int32_t permille) { return uint8_t(128 + (permille * 128 + (permille < 0 ? -500 : 500)) / 1000); }

constexpr uint8_t kGene = GeneFlags::Mutable | GeneFlags::Dupable | GeneFlags::Delable;
constexpr uint8_t kWhenAsleep = 1;   // StimulusGene flags
constexpr uint8_t kDigital = 1;      // ReceptorGene flags

constexpr uint8_t d(DriveId id) { return driveChem(id).v; }
constexpr uint8_t kPain = d(drive::pain), kFear = d(drive::fear), kBoredom = d(drive::boredom),
                  kLoneliness = d(drive::loneliness), kNeedTouch = d(drive::need_touch),
                  kSleepiness = d(drive::sleepiness);

constexpr DuelGene gene(uint16_t n, uint8_t type, uint8_t len, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3 = 0,
                        uint8_t b4 = 0, uint8_t b5 = 0, uint8_t b6 = 0, uint8_t b7 = 0) {
  return DuelGene{GeneHeader{type, len, kGene, Stage::Baby, 0, 64, GeneUid{uint16_t(kFirstDuelGeneUid.v + n)}},
                  {b0, b1, b2, b3, b4, b5, b6, b7}};
}
constexpr DuelGene halfLife(uint16_t n, ChemId c, uint32_t ticks) {
  return gene(n, GeneKindOf<ChemGene>::value, sizeof(ChemGene), c.v, timeByte(ticks), 0);
}
constexpr DuelGene feels(uint16_t n, StimId s, uint8_t flags, uint8_t c0, int a0, uint8_t c1 = 0, int a1 = 0,
                         uint8_t c2 = 0, int a2 = 0) {
  return gene(n, GeneKindOf<StimulusGene>::value, sizeof(StimulusGene), s.v, flags, c0, c1, c2, sgn(a0), sgn(a1),
              sgn(a2));
}

}  // namespace

// Amounts in permille. A Spar's filter drops the pain, mettle and dread
// below; the genes do not know the stakes.
const DuelGene kDuelGenes[] = {
    halfLife(0, chem::daze, 90 * kSecond),   // the swoon is a nap of 3 to 4 minutes
    halfLife(1, chem::pluck, 5 * kMinute),
    halfLife(2, chem::mettle, 6 * kDay),
    halfLife(3, chem::dread, 6 * kDay),
    // Daze holds the sleep gate open while it is above 0.2: the nap after a faint.
    gene(4, GeneKindOf<ReceptorGene>::value, sizeof(ReceptorGene), chem::daze.v, locus::sleep_gate.v, unit(200),
         sgn(600), 128, kDigital),
    feels(5, stim::duel_bonked, 0, chem::adrenaline.v, 200, kFear, 50, kPain, 100),
    feels(6, stim::duel_landed, 0, chem::pluck.v, 300, kBoredom, -200),
    feels(7, stim::duel_won, 0, chem::pluck.v, 500, kBoredom, -400, kLoneliness, -200),
    feels(8, stim::duel_won, 0, chem::mettle.v, 100),
    feels(9, stim::duel_fainted, 0, chem::daze.v, 992, kFear, -300, chem::dread.v, 100),
    // The end of a duel lands on a pet still napping from his faint.
    feels(10, stim::duel_befriended, kWhenAsleep, kLoneliness, -500, kNeedTouch, -200, kBoredom, -300),
    feels(11, stim::duel_befriended, kWhenAsleep, kSleepiness, 150),
    // "Fondness" has no drive of its own; being spared eases loneliness.
    feels(12, stim::duel_spared, kWhenAsleep, kFear, -300, kLoneliness, -200, chem::dread.v, 200),
    feels(13, stim::duel_slew, 0, chem::pluck.v, 500, chem::dread.v, 100),
    feels(14, stim::duel_mourned, 0, kLoneliness, 300, chem::dread.v, 50),
};
const size_t kDuelGeneCount = sizeof(kDuelGenes) / sizeof(kDuelGenes[0]);

GenomeBuilder& appendDuelGenes(GenomeBuilder& b) {
  for (const DuelGene& g : kDuelGenes) b.append(g.header, g.body);
  return b;
}

}  // namespace blorb
