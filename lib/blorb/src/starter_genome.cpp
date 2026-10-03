// Grungo, generation 0: skittish, curious, nocturnal, calm, a seer.
// Every rate below is a time on the half-life scale (see timeByte), so the
// genome reads as "Life halves in 3 days", not as bytes.
#include <cstdint>
#include "blorb/genes.h"
#include "blorb/registry.h"

namespace blorb {
namespace {

// Open question 1, decided by the user: sustained neglect is an illness that
// can end the life (cause Starved). false = neglect only shortens it.
constexpr bool kNeglectCanKill = true;

constexpr uint32_t kSecond = 1000 / kTickMs;
constexpr uint32_t kMinute = kTicksPerMinute;
constexpr uint32_t kHour = kTicksPerHour;
constexpr uint32_t kDay = 24 * kHour;

constexpr uint32_t kSemitoneQ16[12] = {65536, 69433, 73562, 77936, 82570, 87480,
                                       92682, 98193, 104032, 110218, 116772, 123715};

// The byte whose time on the half-life scale, 10 ticks * 2^((b-1)/12), is
// nearest `ticks`. Half-lives, emitter gains (time to fill 0 -> 1) and
// reaction rates (time to convert all of the limiting reagent) share it.
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

// A tonic emitter's fill time for an equilibrium of `permille` against a half-life.
constexpr uint8_t tonicByte(uint32_t halfLife, uint32_t permille) {
  return timeByte(uint64_t(halfLife) * 1443 / permille);
}

constexpr uint8_t unit(uint32_t permille) { return uint8_t((permille * 255 + 500) / 1000); }
// signedByte's inverse: -1000 .. +992 permille around 128.
constexpr uint8_t sgn(int32_t permille) { return uint8_t(128 + (permille * 128 + (permille < 0 ? -500 : 500)) / 1000); }

constexpr uint8_t kNone = 0;       // chem 0 = unused slot
constexpr uint8_t kNoCue = 255;
constexpr uint8_t kNoDrive = 255;

// Unnamed chemicals and gene-to-gene loci (free band 192..255).
constexpr uint8_t kJolt = 32;        // a knock's sting, gone in a second; drives flinch
constexpr uint8_t kStarvation = 34;  // integrates time spent starving
constexpr uint8_t kHungerCall = 192; // low energy, as a signal
constexpr uint8_t kStarving = 193;   // hunger pinned high
constexpr uint8_t kHurting = 194;    // injury, as a signal

// Emitter and receptor flags.
constexpr uint8_t kDigital = 1, kInvert = 2;

constexpr uint8_t kGene = GeneFlags::Mutable | GeneFlags::Dupable | GeneFlags::Delable;
constexpr uint8_t kVital = GeneFlags::Mutable;   // may drift, never deleted
constexpr uint8_t kLook = kGene | GeneFlags::OwnerEditable;
constexpr uint8_t kDormant = GeneFlags::Dormant;

// Death bands on the cause locus: DeathCause(level * 5), so 0.1 = OldAge,
// 0.3 = Starved, 0.5 = Injured.
constexpr uint8_t kCauseOldAge = sgn(100), kCauseStarved = sgn(300), kCauseInjured = sgn(500);

constexpr uint8_t d(DriveId id) { return driveChem(id).v; }

struct Author {
  GenomeBuilder b;
  Rng look;

  void chem(uint8_t c, uint32_t halfLife, uint8_t initial = 0, uint8_t flags = kGene) {
    b.append(ChemGene{c, halfLife ? timeByte(halfLife) : uint8_t(0), initial}, flags);
  }
  void react(uint8_t a, uint8_t qa, uint8_t b2, uint8_t qb, uint8_t c, uint8_t qc, uint32_t ticks,
             uint8_t flags = kGene) {
    b.append(ReactionGene{a, qa, b2, qb, c, qc, kNone, 0, timeByte(ticks)}, flags);
  }
  void emit(uint8_t locus, uint8_t c, uint8_t threshold, uint8_t gain, uint8_t flags, uint8_t geneFlags = kGene) {
    b.append(EmitterGene{locus, c, threshold, gain, flags, 0}, geneFlags);
  }
  void receive(uint8_t c, uint8_t locus, uint8_t threshold, uint8_t nominal, uint8_t gain, uint8_t flags,
               uint8_t geneFlags = kGene) {
    b.append(ReceptorGene{c, locus, threshold, nominal, gain, flags}, geneFlags);
  }
  void stimulus(StimId s, uint8_t flags, uint8_t c0, int a0, uint8_t c1 = kNone, int a1 = 0, uint8_t c2 = kNone,
                int a2 = 0, uint8_t geneFlags = kGene) {
    b.append(StimulusGene{s.v, flags, {c0, c1, c2}, {sgn(a0), sgn(a1), sgn(a2)}}, geneFlags);
  }
  void instinct(LocusId c0, LocusId c1, ActionId a, DriveId dr, int level, uint32_t strength,
                Stage stage = Stage::Baby, uint8_t featGate = 0) {
    b.append(InstinctGene{{c0.v, c1.v, kNoCue}, a.v, dr.v, sgn(level), unit(strength)}, kGene, stage, featGate);
  }
  void face(ExprId f, uint32_t weight, DriveId d0 = DriveId{kNoDrive}, int a0 = 0, DriveId d1 = DriveId{kNoDrive},
            int a1 = 0, DriveId d2 = DriveId{kNoDrive}, int a2 = 0) {
    b.append(ExpressionGene{f.v, unit(weight), {d0.v, d1.v, d2.v}, {sgn(a0), sgn(a1), sgn(a2)}}, kGene);
  }
  // speciesSeed moves a Look byte by at most `spread` either way.
  uint8_t nudge(uint8_t v, uint8_t spread) { return uint8_t(int(v) - spread + int(look.below(2u * spread + 1))); }
};

constexpr LocusId kNoLocus{kNoCue};

}  // namespace

Genome starterGenome(uint32_t speciesSeed) {
  Author g{GenomeBuilder{}, Rng::seeded(speciesSeed)};
  const uint8_t hunger = d(drive::hunger), sleepiness = d(drive::sleepiness), boredom = d(drive::boredom),
                loneliness = d(drive::loneliness), fear = d(drive::fear), pain = d(drive::pain),
                discomfort = d(drive::discomfort), needTouch = d(drive::need_touch);
  const uint8_t life = chem::life.v, injury = chem::injury.v, energy = chem::energy.v, food = chem::food.v,
                adrenaline = chem::adrenaline.v, toxin = chem::toxin.v, melatonin = chem::melatonin.v,
                vision = chem::vision.v;

  // ---- Life: seeded full, halves every 3 days; stages and old age read it ----
  g.chem(life, 3 * kDay, 255, kVital);
  g.receive(life, locus::become_child.v, 227, 255, 128, kDigital | kInvert, kVital);   // about day 0.5
  g.receive(life, locus::become_adult.v, 162, 255, 128, kDigital | kInvert, kVital);   // about day 2
  g.receive(life, locus::become_elder.v, 46, 255, 128, kDigital | kInvert, kVital);    // about day 7.5
  g.receive(life, locus::die.v, 26, 255, 128, kDigital | kInvert, kVital);             // about day 10
  g.receive(life, locus::cause.v, 26, kCauseOldAge, 128, kDigital | kInvert, kVital);

  // ---- drives: each has a half-life and tonic production on `always` ----------
  struct Tonic { uint8_t chem; uint32_t halfLife; uint32_t permille; uint8_t initial; };
  const Tonic tonics[] = {
      {hunger, 30 * kMinute, 150, unit(300)}, {sleepiness, 2 * kHour, 100, 0}, {boredom, 1 * kHour, 600, 0},
      {loneliness, 3 * kHour, 500, 0},        {fear, 2 * kMinute, 50, 0},       {pain, 10 * kMinute, 20, 0},
      {discomfort, 5 * kMinute, 50, 0},       {needTouch, 3 * kHour, 500, 0},
  };
  for (const Tonic& t : tonics) {
    g.chem(t.chem, t.halfLife, t.initial);
    g.emit(locus::always.v, t.chem, 0, tonicByte(t.halfLife, t.permille), kDigital);
  }

  // ---- food -> energy; low energy calls up hunger ------------------------------
  g.chem(food, 0);
  g.chem(energy, 4 * kHour, unit(800));
  g.react(food, 1, kNone, 0, energy, 1, 2 * kMinute, kVital);
  g.receive(energy, kHungerCall, unit(350), 128, 128 + 48, kInvert);   // 3 x the shortfall below 0.35
  g.emit(kHungerCall, hunger, 0, timeByte(20 * kMinute), 0);

  // ---- neglect: pinned hunger makes him ill, and illness speeds Life's fall ---
  g.chem(injury, 6 * kHour);
  g.receive(hunger, kStarving, unit(800), 255, 128, kDigital);
  g.emit(kStarving, injury, 0, timeByte(1 * kDay), kDigital);
  g.react(injury, 1, life, 1, injury, 1, 4 * kDay);   // injury catalyses ageing
  g.receive(injury, kHurting, unit(100), 128, 128 + 32, 0);
  g.emit(kHurting, pain, 0, timeByte(2 * kMinute), 0);
  if (kNeglectCanKill) {
    g.chem(kStarvation, 1 * kDay);
    g.emit(kStarving, kStarvation, 0, timeByte(2 * kDay), kDigital);
    g.receive(kStarvation, locus::die.v, unit(600), 255, 128, kDigital, kVital);
    g.receive(kStarvation, locus::cause.v, unit(600), kCauseStarved, 128, kDigital, kVital);
  }
  // Only sustained severe injury kills; neglect alone plateaus far below it.
  g.receive(injury, locus::die.v, unit(900), 255, 128, kDigital, kVital);
  g.receive(injury, locus::cause.v, unit(900), kCauseInjured, 128, kDigital, kVital);
  g.chem(toxin, 1 * kHour);
  g.react(toxin, 1, kNone, 0, injury, 1, 10 * kMinute);

  // ---- sleep: melatonin in the dark, late, so he is up for the first of the night
  g.chem(melatonin, 1 * kHour);
  g.emit(locus::light.v, melatonin, unit(300), timeByte(1 * kHour), kInvert);
  g.react(melatonin, 1, kNone, 0, sleepiness, 1, 30 * kMinute);
  g.receive(sleepiness, locus::sleep_gate.v, unit(350), sgn(500), 128 + 32, 0);

  // ---- skittish: a shake floods adrenaline, which falls slowly, so he hops once
  g.chem(adrenaline, 20 * kSecond);
  g.receive(adrenaline, locus::startle.v, unit(250), sgn(300), 128 + 16, 0);   // gain = hop height
  g.receive(adrenaline, locus::arousal.v, unit(100), 128, 128 + 16, 0);
  g.chem(kJolt, 1 * kSecond);
  g.receive(kJolt, locus::flinch.v, unit(200), 255, 128, kDigital);

  // ---- the seer: foreseeing raises vision, vision lights the glow -----------------
  g.chem(vision, 3 * kSecond);
  g.emit(locus::foreseeing.v, vision, unit(500), timeByte(1 * kSecond), kDigital);
  g.receive(vision, locus::glow.v, unit(50), sgn(400), 128 + 16, 0);

  // ---- what each stimulus does to him ---------------------------------------------
  g.stimulus(stim::shake, 0, adrenaline, 600, discomfort, 250, fear, 300);
  g.stimulus(stim::knock, 0, kJolt, 500, fear, 50, boredom, -150);
  g.stimulus(stim::double_knock, 0, boredom, -250, loneliness, -100);
  g.stimulus(stim::picked_up, 0, needTouch, -100, fear, 50);
  g.stimulus(stim::dropped, 0, injury, 150, adrenaline, 400, pain, 300);
  g.stimulus(stim::flipped, 0, discomfort, 300, fear, 150);
  g.stimulus(stim::cradle, 1, needTouch, -350, loneliness, -200, fear, -200);
  g.stimulus(stim::fed, 0, hunger, -400, boredom, -50);
  g.stimulus(stim::fed_bad, 0, toxin, 400, discomfort, 300);
  g.stimulus(stim::marble_hit, 0, boredom, -300);
  g.stimulus(stim::owner_arrived, 0, loneliness, -300);
  g.stimulus(stim::lid_down, 1, melatonin, 300);
  g.stimulus(stim::button_hold, 0, sleepiness, 200);
  g.stimulus(stim::self_foresaw, 0, boredom, -200);
  g.stimulus(stim::petted, 1, needTouch, -300, loneliness, -150);
  g.stimulus(stim::played, 0, boredom, -300);
  g.stimulus(stim::spoken_to, 0, loneliness, -200);
  // Dormant: he loves being shaken.
  g.stimulus(stim::shake, 0, boredom, -400, needTouch, -150, discomfort, -250, kGene | kDormant);

  // ---- instincts: a newborn eats, sleeps and curls before his first decision ----
  g.instinct(locus::food_near, kNoLocus, action::eat, drive::hunger, -700, 800);
  g.instinct(locus::day_sin, locus::day_cos, action::sleep, drive::sleepiness, -700, 650);
  g.instinct(locus::recent(stim::shake), locus::recent(stim::knock), action::curl, drive::fear, -600, 700);
  g.instinct(locus::cradled, kNoLocus, action::rest, drive::need_touch, -500, 500);
  g.instinct(locus::day_sin, kNoLocus, action::foresee, drive::boredom, -450, 500);
  g.instinct(locus::marble_near, kNoLocus, action::chase, drive::boredom, -600, 550, Stage::Child);
  g.instinct(locus::owner_near, kNoLocus, action::call, drive::loneliness, -500, 500, Stage::Adult);
  g.instinct(locus::recent(stim::fed), kNoLocus, action::hop_circles, drive::boredom, -400, 450, Stage::Adult);
  // Once the line has dreamt, he learns he foresaw the shake.
  g.instinct(locus::recent(stim::shake), kNoLocus, action::foresee, drive::fear, -500, 500, Stage::Adult,
             /*featGate=*/5);

  // ---- faces over the drive mix: weight is a base, amounts per drive ----------
  g.face(expr::neutral, 350);
  g.face(expr::happy, 550, drive::hunger, -500, drive::boredom, -500, drive::fear, -600);
  g.face(expr::alarmed, 0, drive::fear, 900, drive::discomfort, 500);
  g.face(expr::annoyed, 0, drive::discomfort, 700, drive::pain, 600);
  g.face(expr::croak, 250, drive::need_touch, -600, drive::loneliness, -400);
  g.face(expr::blep, 0, drive::hunger, 800);
  g.face(expr::foresee, 0, drive::boredom, 300);
  g.face(expr::sleepy, 0, drive::sleepiness, 700);
  g.face(expr::asleep, 0, drive::sleepiness, 950);
  g.face(expr::yawn, 0, drive::sleepiness, 500, drive::boredom, 200);

  // ---- look: olive skin, pale belly, brown cloak, red-brown eyes, teal glow ------
  g.b.append(PaletteGene{region::skin.v, g.nudge(128, 6), 128, 128, kNone, 0}, kLook);
  g.b.append(PaletteGene{region::belly.v, 128, 128, g.nudge(140, 6), kNone, 0}, kLook);
  g.b.append(PaletteGene{region::cloak.v, 128, 128, g.nudge(128, 10), kNone, 0}, kLook);
  g.b.append(PaletteGene{region::eye.v, 128, 128, 128, kNone, 0}, kLook);
  g.b.append(PaletteGene{region::glow.v, 128, 140, 160, vision, 255}, kLook);
  g.b.append(PaletteGene{region::cloak.v, 38, 110, 120, kNone, 0}, kLook | kDormant);   // a bluer cloak
  g.b.append(PaletteGene{region::glow.v, 128, 170, 235, vision, 255}, kLook | kDormant); // a brighter seer glow
  g.b.append(MarkGene{0, g.nudge(3, 1), 128, kNone}, kLook);                              // mottling density
  g.b.append(MarkGene{1, 2, 128, kNone}, kLook, Stage::Adult, /*featGate=*/2);           // longer stalks, after an elder
  g.b.append(SizeGene{128, 140, 140}, kGene);
  g.b.append(SizeGene{136, 128, 128}, kGene, Stage::Adult);

  // ---- mind and life ----------------------------------------------------------------
  g.b.append(TemperamentGene{140, 16, 170, 110, 8, 3, 20}, kGene);   // calm learner, curious
  g.b.append(TemperamentGene{90, 24, 110, 140, 6, 4, 24}, kGene, Stage::Elder);
  g.b.append(MutationPolicyGene{40, 6, 4, 20, 6, 10, 3, 140}, kVital);
  g.b.append(EggGene{30, 128, 12}, kVital);                               // a 30-minute egg
  g.b.append(MetabolismGene{4, 8, 90, unit(300)}, kVital);                // 4 pellets, one back every 2 h
  NoteGene note{};
  const char motto[] = "it is WRITTEN";
  for (size_t i = 0; i + 1 < sizeof motto; ++i) note.text[i] = uint8_t(motto[i]);
  g.b.append(note, GeneFlags::OwnerEditable);

  return *g.b.build();
}

}  // namespace blorb
