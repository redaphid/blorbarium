#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "blorb/appearance.h"
#include "blorb/species.h"
#include "paint/sprite_pack.h"

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

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
