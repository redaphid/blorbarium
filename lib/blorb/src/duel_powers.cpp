// The nine signature powers (defs/powers.def), one handler each. A power is a
// queued special (duel.h): a handler runs only on the turn its owner's special
// LANDS, once per phase, and acts in the phases it cares about. What lasts
// past that turn goes into SideState, and later turns' own steps read it, so
// a hush whiffing the friend's next special is the core's to honour.
//
// The counters are set on the landing turn and tick down at its end, so "2"
// covers the turn after the landing; a landing turn has no clash for them to
// change. Both sides' handlers in one phase must commute: each writes only its
// own fields, or adds.
//
// Everything here is constexpr, and duel_resolve.cpp includes this file, so
// the S3 compiler can evaluate whole battles (tools/duel_determinism.sh).
// Compiled on its own it defines nothing that links.
#include "blorb/duel.h"

namespace blorb {
namespace duel_detail {

constexpr uint8_t mine(const PowerCtx& c) { return uint8_t(c.me); }
constexpr uint8_t theirs(const PowerCtx& c) { return uint8_t(other(c.me)); }

// He SAW the counter coming: a Foresee landing that the friend's stance beats
// whiffs instead of being riposted. special_probe.py measured the two other
// readings and rejected both (always the counter: grungo 0.667; plain: 0.429).
constexpr bool seesTheRiposte(PowerId p) { return p == power::foresee; }
constexpr void power_foresee(PowerPhase, PowerCtx&) {}

// Up from the landing until a hit uses it.
constexpr void power_shell(PowerPhase ph, PowerCtx& c) {
  if (ph == PowerPhase::OnDamage) c.side[mine(c)].shell = true;
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
  if (ph == PowerPhase::OnDamage) c.side[theirs(c)].dazzledTurns = 2;
}

constexpr void power_dash(PowerPhase ph, PowerCtx& c) {
  if (ph == PowerPhase::OnDamage) c.side[mine(c)].bonkTurns = 2;
}

constexpr void power_zap(PowerPhase ph, PowerCtx& c) {
  if (ph == PowerPhase::OnDamage) ++c.hit[theirs(c)];
}

constexpr void power_hush(PowerPhase ph, PowerCtx& c) {
  if (ph == PowerPhase::OnDamage) c.side[mine(c)].hushTurns = 2;
}

constexpr void power_blackbell(PowerPhase ph, PowerCtx& c) { power_hush(ph, c); }

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
