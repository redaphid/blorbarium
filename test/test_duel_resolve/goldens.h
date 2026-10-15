#pragma once
// Golden battles: fixed cards, seed, stakes and logs, and the Outcome::hash
// each must give. test_duel_resolve pins them on the host, and
// tools/duel_determinism.sh has the host compiler and the ESP32-S3 compiler
// each evaluate the same battles in static_asserts, so both devices of a pair
// are held to one answer.
#include "blorb/duel.h"

namespace duelgolden {
using namespace blorb;

// 'P' 'G' 'T' lock a stance. A special is 'q' (quick) or 'h' (heavy) for the
// commit, '_' for a heavy's placeholder, then lower case 'p' 'g' 't' for the
// landing entry. '.' is a turn the pet slept through.
constexpr MoveLog logOf(const char* s) {
  MoveLog l{};
  for (; *s; ++s) {
    Move m{};
    const char c = *s;
    m.stance = (c == 'G' || c == 'g') ? Stance::Guard : (c == 'T' || c == 't') ? Stance::Trick : Stance::Pounce;
    m.power = c == 'p' || c == 'g' || c == 't';
    m.byPet = c == '.';
    m.windup = c == 'q' ? Windup::Quick : c == 'h' ? Windup::Heavy : Windup::None;
    l.append(m);
  }
  return l;
}

constexpr BattleCard cardOf(SpeciesId s, uint64_t lineage, uint8_t prowess = 128) {
  BattleCard c{};
  c.species = s;
  c.power = speciesInfo(s).power;
  c.knack = Knack{knackOrderOf(knackFor(speciesInfo(s).style))};
  c.lineageId = lineage;
  c.prowess = prowess;
  return c;
}

namespace sp {
inline constexpr SpeciesId grungo{0}, mossby{1}, puffkin{2}, gilly{3}, mothwick{4}, mudge{5}, fizz{6}, hushwing{7},
    tinker{8};
}

struct Golden {
  const char* name;
  BattleCard a, b;
  uint32_t seed;
  Stakes stakes;
  const char* movesA;
  const char* movesB;
  uint32_t hash;
};

inline constexpr Golden kGoldens[] = {
    {"a spar, grungo v fizz", cardOf(sp::grungo, 0x1D0001), cardOf(sp::fizz, 0x2D0002), 0xD4D40001u, Stakes::Spar,
     "TPGqtPGh_pTGP", "PGTPqgTTGPh_t", 0xc371ec08u},
    {"a scrap with assist, tinker v gilly", cardOf(sp::tinker, 0xA, 200), cardOf(sp::gilly, 0xB, 100), 0xD4D40002u,
     Stakes::Scrap, "PTGPTqpGTh_gPT", "GTPqgPTGPTPGT", 0xb01a435au},
    {"specials on both sides, puffkin v mothwick", cardOf(sp::puffkin, 0x5EED), cardOf(sp::mothwick, 0x0DD),
     0xD4D40003u, Stakes::Spar, "PPPPPqpTGh_tPG", "PPPqtGPh_gTqpP", 0x1ab43fc2u},
    {"a riposte and a stagger, mudge v gilly", cardOf(sp::mudge, 0x9), cardOf(sp::gilly, 0xA), 0xD4D40006u,
     Stakes::Scrap, "PPPqpPPh_pTqg", "PPPGGPTPGPPTG", 0x88df67a4u},
};

// The first golden whose hash differs, or -1. Its own function so the S3
// check can call it in a static_assert through resolveCore.
template <class Resolve>
constexpr int firstBroken(Resolve resolveFn) {
  int i = 0;
  for (const Golden& g : kGoldens) {
    if (resolveFn(g.a, g.b, g.seed, g.stakes, logOf(g.movesA), logOf(g.movesB)).hash() != g.hash) return i;
    ++i;
  }
  return -1;
}

}  // namespace duelgolden
