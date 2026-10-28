#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "blorb/appearance.h"
#include "blorb/species.h"
#include "paint/sprite_pack.h"
#include "grungo_pack.h"

using blorb::Appearance;
using blorb::Fx;
using blorb::SpeciesId;
using blorb::Tint;
using paint::Canvas240;

namespace {

constexpr int kPx = 240 * 240;

std::unique_ptr<Canvas240> render(const Appearance& a) {
  auto cv = std::make_unique<Canvas240>();
  paint::draw(a, paint::packFor(a.species), *cv, 0);
  return cv;
}

Appearance adult(SpeciesId s) {
  Appearance a;
  a.species = s;
  a.stage = blorb::Stage::Adult;
  return a;
}

// Every frame a pack draws: the body under every face, the power face (its
// glow pixels, with no halo over them), asleep, the egg and the remains. Each
// face twice, 20 ticks apart, since a blink can stand in for it at one of them.
std::vector<std::pair<std::string, Appearance>> scenes(SpeciesId s) {
  std::vector<std::pair<std::string, Appearance>> out;
  for (const blorb::ExprInfo& e : blorb::EXPRESSIONS)
    for (uint16_t tick : {0, 20}) {
      Appearance a = adult(s);
      a.expression = a.previous = e.id;
      a.intensity = Fx::one();
      a.exprTicks = 100;
      a.poseTick = tick;
      a.foreseeing = e.id == blorb::expr::foresee;   // glow 0: the face, but no halo lighting the pixels round it
      a.asleep = e.id == blorb::expr::asleep;
      out.push_back({e.name, a});
    }
  Appearance egg = adult(s);
  egg.kind = Appearance::Kind::Egg;
  out.push_back({"egg", egg});
  Appearance remains = adult(s);
  remains.kind = Appearance::Kind::Remains;
  out.push_back({"remains", remains});
  return out;
}

// The furthest tint the region's band allows, so a gene at its limit is what moves it.
Tint strongest(const paint::SpritePack& pack, uint8_t region) {
  const paint::RegionBand b = pack.band(blorb::RegionId{region});
  return Tint{int8_t(-b.hueMin > b.hueMax ? b.hueMin : b.hueMax), b.satMax, b.valMax};
}

// Pixels a region's tint must move, summed over every scene, for its gene to
// show. Every species uses all seven slots; a slot drawn only in specks (a few
// px in one face) or not at all is a gene that does nothing for that species.
constexpr int kVisibleRegionPx = 200;

class EverySpecies : public ::testing::TestWithParam<uint8_t> {};

// The colour genes reach every species: each of the seven regions moves a
// visible part on screen under its own tint, and no pixel moves under two
// regions'.
TEST_P(EverySpecies, EachRegionRecoloursOnItsOwn) {
  const SpeciesId s{GetParam()};
  const paint::SpritePack& pack = paint::packFor(s);
  int moved[blorb::kRegionCount] = {};
  for (const auto& [name, plain] : scenes(s)) {
    const auto before = render(plain);
    std::vector<uint8_t> owner(kPx, 0xFF);
    for (uint8_t r = 0; r < blorb::kRegionCount; ++r) {
      Appearance tinted = plain;
      tinted.regions[r] = strongest(pack, r);
      const auto after = render(tinted);
      for (int i = 0; i < kPx; ++i) {
        if (before->px[i] == after->px[i]) continue;
        ++moved[r];
        ASSERT_EQ(owner[i], 0xFF) << blorb::speciesInfo(s).name << " " << name << ": pixel " << i
                                  << " moved under region " << int(owner[i]) << " and region " << int(r);
        owner[i] = r;
      }
    }
  }
  for (const blorb::RegionInfo& r : blorb::REGIONS)
    EXPECT_GE(moved[r.id.v], kVisibleRegionPx) << blorb::speciesInfo(s).name << "'s " << r.name << " moves only "
                                               << moved[r.id.v] << " px: specks or nothing, not a part";
}

std::vector<uint8_t> rosterIds() {
  std::vector<uint8_t> ids;
  for (const blorb::SpeciesInfo& r : blorb::SPECIES) ids.push_back(r.id.v);
  return ids;
}

INSTANTIATE_TEST_SUITE_P(Roster, EverySpecies, ::testing::ValuesIn(rosterIds()),
                         [](const ::testing::TestParamInfo<uint8_t>& p) {
                           return std::string(blorb::speciesInfo(SpeciesId{p.param}).name);
                         });

TEST(PackFor, EverySpeciesDrawsItsOwnBody) {
  const auto grungo = render(adult(blorb::kGrungo));
  std::set<const paint::SpritePack*> packs;
  for (const blorb::SpeciesInfo& r : blorb::SPECIES) {
    packs.insert(&paint::packFor(r.id));
    if (r.id == blorb::kGrungo) continue;
    const auto cv = render(adult(r.id));
    int differ = 0;
    for (int i = 0; i < kPx; ++i) differ += cv->px[i] != grungo->px[i];
    EXPECT_GT(differ, kPx / 20) << r.name << " draws too much like grungo";
  }
  EXPECT_EQ(packs.size(), blorb::kSpeciesCount) << "two species share a pack";
}

// A species this firmware has no row for (an egg from a newer one) is a frog,
// still in its own genes' colours: never a blank.
TEST(PackFor, AnUnknownSpeciesIsGrungoInItsOwnColours) {
  for (uint8_t id : {uint8_t(blorb::kSpeciesCount), uint8_t(200), uint8_t(255)}) {
    const SpeciesId s{id};
    EXPECT_EQ(&paint::packFor(s), &paint::packFor(blorb::kGrungo)) << int(id);
    Appearance tinted = adult(s);
    tinted.regions[blorb::region::skin.v] = Tint{30, 160, 140};
    const auto plain = render(adult(s)), recoloured = render(tinted);
    int differ = 0;
    for (int i = 0; i < kPx; ++i) differ += plain->px[i] != recoloured->px[i];
    EXPECT_GT(differ, kPx / 50) << int(id) << ": the fallback frog ignores the skin gene";
  }
}

// ---- the species the genes say, with no DEBUG pin --------------------------------

bool sameCanvas(const Canvas240& x, const Canvas240& y) { return std::equal(x.px, x.px + kPx, y.px); }

std::unique_ptr<Canvas240> renderWith(const Appearance& a, const paint::SpritePack& pack) {
  auto cv = std::make_unique<Canvas240>();
  paint::draw(a, pack, *cv, 0);
  return cv;
}

// A pet founded from a species' starter is that species everywhere it is
// drawn: as an egg, hatched, at every stage of the family tree, as remains
// and as the clutch it leaves.
TEST(FromTheGenes, EveryStarterIsItsSpeciesEverywhereItIsDrawn) {
  const blorb::Habitat h;
  const blorb::PetClock clock;
  for (const blorb::SpeciesInfo& r : blorb::SPECIES) {
    const blorb::Genome g = blorb::starterGenomeOf(r.id, 7);
    const blorb::Occupant egg{std::in_place_type<blorb::Egg>, blorb::Offspring{g, {}}, uint16_t(0), 0u};
    EXPECT_EQ(present(egg, h, clock, 0).species, r.id) << r.name << "'s egg";
    const blorb::Occupant hatched{std::in_place_type<blorb::Creature>, std::get<blorb::Egg>(egg), 0u, 0u};
    EXPECT_EQ(present(hatched, h, clock, 0).species, r.id) << r.name;
    for (uint8_t st = 0; st < blorb::kStageCount; ++st)
      EXPECT_EQ(portrait(g, blorb::Stage(st), 0, 0).species, r.id) << r.name << "'s portrait at stage " << int(st);
    blorb::Occupant gone{std::in_place_type<blorb::Clutch>};
    std::get<blorb::Clutch>(gone).parent = g;
    EXPECT_EQ(present(gone, h, clock, 0).species, r.id) << r.name << "'s remains";
    std::get<blorb::Clutch>(gone).sinceDeath = blorb::Clutch::kVigilTicks;
    EXPECT_EQ(present(gone, h, clock, 0).species, r.id) << r.name << "'s clutch";
  }
}

TEST(FromTheGenes, EveryStarterDrawsItsOwnArtNotTheFrog) {
  for (const blorb::SpeciesInfo& r : blorb::SPECIES) {
    const Appearance a = portrait(blorb::starterGenomeOf(r.id, 7), blorb::Stage::Adult, 0, 0);
    const auto drawn = render(a), own = renderWith(a, paint::packFor(r.id)), frog = renderWith(a, grungoPack());
    EXPECT_TRUE(sameCanvas(*drawn, *own)) << r.name << " is not drawn from its own pack";
    if (r.id != blorb::kGrungo) EXPECT_FALSE(sameCanvas(*drawn, *frog)) << r.name << " is drawn as the frog";
  }
}

// starterGenome() has no species gene, so it draws exactly as it did when
// grungoPack() was the only pack.
TEST(FromTheGenes, TheStarterDrawsAsGrungoByteForByte) {
  const blorb::Genome starter = blorb::starterGenome(15048);
  const blorb::Habitat h;
  const blorb::PetClock clock;
  const blorb::Occupant egg{std::in_place_type<blorb::Egg>, blorb::Offspring{starter, {}}, uint16_t(0), 0u};
  const blorb::Occupant hatched{std::in_place_type<blorb::Creature>, std::get<blorb::Egg>(egg), 0u, 0u};
  std::vector<Appearance> shown = {present(egg, h, clock, 0), present(hatched, h, clock, 0)};
  for (uint8_t st = 0; st < blorb::kStageCount; ++st) shown.push_back(portrait(starter, blorb::Stage(st), 0, 0));
  for (const Appearance& a : shown) {
    EXPECT_EQ(a.species, blorb::kGrungo);
    EXPECT_TRUE(sameCanvas(*render(a), *renderWith(a, grungoPack())));
  }
}

// ---- an egg shaken open ------------------------------------------------------------

Appearance eggOf(SpeciesId s, double progress, double shaken, uint32_t liveMs = 0) {
  Appearance a = portrait(blorb::starterGenomeOf(s, 7), blorb::Stage::Baby, 0, 0);
  a.kind = Appearance::Kind::Egg;
  a.eggProgress = Fx::ratio(int32_t(progress * 1000), 1000);
  a.eggShaken = Fx::ratio(int32_t(shaken * 1000), 1000);
  a.lively = true;
  a.liveMs = liveMs;
  return a;
}

int differs(const Canvas240& x, const Canvas240& y) {
  int n = 0;
  for (int i = 0; i < kPx; ++i) n += x.px[i] != y.px[i];
  return n;
}

// Every species' egg cracks as it is shaken: each step toward hatching more
// of it broken than the step before, from a hairline to the shell coming
// away, and a hand shaking it moves it.
TEST(ShakenOpen, EveryEggCracksMoreAsItIsShaken) {
  for (const blorb::SpeciesInfo& r : blorb::SPECIES) {
    const auto whole = render(eggOf(r.id, 0, 0));
    int last = 0;
    for (double p : {0.12, 0.3, 0.55, 0.8, 0.97}) {
      const int broken = differs(*whole, *render(eggOf(r.id, p, 0)));
      EXPECT_GT(broken, last + 15) << r.name << " at " << p << ": no more cracked than before";
      last = broken;
    }
    EXPECT_GT(differs(*render(eggOf(r.id, 0.3, 0, 0)), *render(eggOf(r.id, 0.3, 1, 0))), 200)
        << r.name << ": shaking it does not move it";
  }
}

// While the shell comes away, a hard shake knocks chips off it, on their own beat.
TEST(ShakenOpen, ChipsJumpOffTheShellWhileItIsShaken) {
  const SpeciesId s = blorb::kGrungo;
  const auto a = render(eggOf(s, 0.9, 1, 0)), b = render(eggOf(s, 0.9, 1, 140));   // the jiggle back where it was
  const auto later = render(eggOf(s, 0.9, 1, 280));
  EXPECT_GT(differs(*a, *later), 0);
  EXPECT_GT(differs(*b, *later), 0);
}

// He hatches out of it: the shell flies off him and is gone.
TEST(ShakenOpen, TheShellFliesOffAsHeHatches) {
  for (const blorb::SpeciesInfo& r : blorb::SPECIES) {
    Appearance born = portrait(blorb::starterGenomeOf(r.id, 7), blorb::Stage::Baby, 0, 0);
    const auto plain = render(born);
    born.hatchedMs = 200;
    EXPECT_GT(differs(*plain, *render(born)), 40) << r.name;
    born.hatchedMs = Appearance::kHatchBurstMs;
    EXPECT_EQ(differs(*plain, *render(born)), 0) << r.name;
  }
}

void writePpm(const std::string& path, const Canvas240& cv) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  std::fprintf(f, "P6 240 240 255\n");
  for (int i = 0; i < kPx; ++i) {
    const uint16_t p = cv.px[i];
    const uint8_t rgb[3] = {uint8_t((p >> 11 & 31) << 3), uint8_t((p >> 5 & 63) << 2), uint8_t((p & 31) << 3)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
}

// PAINT_DUMP=<dir>: each species' egg along the way to hatching, shaken and
// still, and the hatch, to look at.
TEST(ShakenOpen, DumpWhenAsked) {
  const char* dir = std::getenv("PAINT_DUMP");
  if (!dir) GTEST_SKIP() << "set PAINT_DUMP to a directory to write previews";
  for (const blorb::SpeciesInfo& r : blorb::SPECIES) {
    for (int p : {0, 12, 25, 40, 55, 70, 85, 97})
      writePpm(std::string(dir) + "/egg_" + r.name + "_" + std::to_string(p) + ".ppm", *render(eggOf(r.id, p / 100.0, 0)));
    writePpm(std::string(dir) + "/egg_" + r.name + "_90_shaken.ppm", *render(eggOf(r.id, 0.9, 1, 100)));
    Appearance born = portrait(blorb::starterGenomeOf(r.id, 7), blorb::Stage::Baby, 0, 0);
    for (int ms : {100, 400, 800}) {
      born.hatchedMs = uint16_t(ms);
      writePpm(std::string(dir) + "/hatch_" + r.name + "_" + std::to_string(ms) + ".ppm", *render(born));
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
