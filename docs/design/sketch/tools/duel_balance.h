#pragma once
// SKETCH (docs/design/BATTLES.md) of tools/duel_balance.cpp: the lever that tunes the
// wheel. It plays every species pair (81, mirrors included) thousands of
// times through the real resolve() with scripted players and prints a 9 x 9
// table. Run by hand while tuning powers.def costs, and as a test with the
// acceptance bounds below, so a row change that tips the wheel fails CI.
#include <cstdint>
#include "blorb/duel.h"

namespace blorbtool {

enum class Player : uint8_t {
  Random,    // a six-year-old mashing: uniform stances, powers whenever full
  Habit,     // repeats a stance 60% of the time: readable
  Reader,    // counters the opponent's most frequent stance so far: a parent
  Pet,       // petChoice() with a fresh Skill: the autopilot
};

struct PairResult { blorb::SpeciesId a, b; uint16_t aWins, bWins, draws; uint16_t meanTurnsX10; };

struct BalanceReport {
  PairResult pairs[blorb::kSpeciesCount * blorb::kSpeciesCount];
  uint16_t worstPairPermille;       // the furthest any pair sits from 500
  uint16_t worstSpeciesPermille;    // the furthest any species' overall rate sits from 500
  uint16_t medianTurnsX10;
};

// Acceptance, from what tools/roster_probe.py measured on these rules bound
// to the roster (Random vs Random, 81 pairs x 1,500, 2026-10-13): species
// overall 450..546 permille, worst pair 672 (hushwing over mudge), median 12
// turns. The gate: every species within 430..570, every pair within 300..700,
// median 8 to 14 turns. Reader vs Habit measured 923 with no assist and 801
// with two assist hearts: the gate is "skill matters" (>= 700 at assist 0).
// Reader vs a kid who follows the pet's lean half the time measured 491: the
// gate is 400..600. A tighter pair bound is unit D4's tuning target, not a
// promise of this design.
BalanceReport tournament(uint32_t battlesPerPair, Player a, Player b, uint32_t seed);

}  // namespace blorbtool
