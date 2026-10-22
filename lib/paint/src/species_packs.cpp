#include "blorb/species.h"
#include "paint/sprite_pack.h"

#include "fizz/fizz_pack.h"
#include "gilly/gilly_pack.h"
#include "grungo/grungo_pack.h"
#include "hushwing/hushwing_pack.h"
#include "mossby/mossby_pack.h"
#include "mothwick/mothwick_pack.h"
#include "mudge/mudge_pack.h"
#include "octopus/octopus_pack.h"
#include "puffkin/puffkin_pack.h"
#include "tinker/tinker_pack.h"

namespace paint {
namespace {

// Built from species.def, so row n is the pack named by row n's slug: a row
// with no `<slug>Pack()` (tools/sprite_pack.py --pet <slug>) does not compile.
using PackFn = const SpritePack& (*)();
constexpr PackFn kPacks[] = {
#define BLORB_SPECIES(id, name, el, st, pw, bold, noct, app, vc, coat, coatHue) &name##Pack,
#include "blorb/defs/species.def"
#undef BLORB_SPECIES
};
static_assert(blorb::countOf(kPacks) == blorb::kSpeciesCount, "a pack per species.def row");

}  // namespace

const SpritePack& packFor(blorb::SpeciesId s) {
  return s.v < blorb::countOf(kPacks) ? kPacks[s.v]() : grungoPack();
}

blorb::LookBands bandsFor(blorb::SpeciesId s) {
  const SpritePack& pack = packFor(s);
  blorb::LookBands bands{};
  for (uint8_t r = 0; r < blorb::kRegionCount; ++r) bands[r] = pack.band(blorb::RegionId{r});
  return bands;
}

}  // namespace paint
