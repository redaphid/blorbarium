// The nine signature powers (defs/powers.def), one handler each. A handler
// runs only for a side whose power fired this turn, once per phase, and acts
// in the phases it cares about. What lasts past the turn goes into SideState,
// and the turn's own steps read it (duel_resolve.cpp), so a two-turn hush is
// the core's to honour on the second turn, not the handler's.
//
// Both sides' handlers in one phase must commute: each reads `original`,
// never what the other just wrote, and writes only its own fields or adds.
//
// Everything here is constexpr, and duel_resolve.cpp includes this file, so
// the S3 compiler can evaluate whole battles (tools/duel_determinism.sh).
// Compiled on its own it defines nothing that links.
#include "blorb/duel.h"

namespace blorb {
namespace duel_detail {

constexpr uint8_t mine(const PowerCtx& c) { return uint8_t(c.me); }
constexpr uint8_t theirs(const PowerCtx& c) { return uint8_t(other(c.me)); }

// He SAW your move: his stance becomes the one that beats it. A dozing friend
// has no move to see.
constexpr void power_foresee(PowerPhase ph, PowerCtx& c) {
  if (ph == PowerPhase::BeforeClash && !c.dozed[theirs(c)]) c.shown[mine(c)] = counterOf(c.original[theirs(c)]);
}

// Up from this turn until a hit uses it.
constexpr void power_shell(PowerPhase ph, PowerCtx& c) {
  if (ph == PowerPhase::BeforeClash) c.side[mine(c)].shell = true;
}

constexpr void power_spores(PowerPhase ph, PowerCtx& c) {
  if (ph == PowerPhase::AfterDamage) c.side[theirs(c)].dozing = true;
}

// After damage, so a heart lost this turn can grow back, even from zero.
constexpr void power_mend(PowerPhase ph, PowerCtx& c) {
  SideState& s = c.side[mine(c)];
  if (ph == PowerPhase::AfterDamage && s.hearts < s.maxHearts) {
    ++s.hearts;
    ++c.healed[mine(c)];
  }
}

constexpr void power_lantern(PowerPhase ph, PowerCtx& c) {
  if (ph != PowerPhase::BeforeClash) return;
  SideState& s = c.side[mine(c)];
  if (s.bonkTurns < 1) s.bonkTurns = 1;
  c.side[theirs(c)].dazzledTurns = 2;
}

constexpr void power_dash(PowerPhase ph, PowerCtx& c) {
  if (ph == PowerPhase::BeforeClash) c.side[mine(c)].bonkTurns = 2;
}

constexpr void power_zap(PowerPhase ph, PowerCtx& c) {
  if (ph == PowerPhase::OnDamage) ++c.hit[theirs(c)];
}

constexpr void power_hush(PowerPhase ph, PowerCtx& c) {
  if (ph == PowerPhase::BeforeClash) c.side[mine(c)].hushTurns = 2;
}

// Never called: the core swaps gadget for the power it draws before any phase.
constexpr void power_gadget(PowerPhase, PowerCtx&) {}

using PowerFn = void (*)(PowerPhase, PowerCtx&);
inline constexpr PowerFn kPowerHandlers[] = {
#define BLORB_POWER(id, name, cost, glyph) &power_##name,
#include "blorb/defs/powers.def"
#undef BLORB_POWER
};
static_assert(countOf(kPowerHandlers) == kPowerCount, "one handler per powers.def row");

}  // namespace duel_detail
}  // namespace blorb
