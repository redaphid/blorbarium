#pragma once
// The duel screen from a live duel: the session's DuelView (what the kid's
// device knows right now) and my Appearance, as the DuelScene drawDuel()
// paints. Pure, so the firmware, the simulator and the films draw a duel the
// same way, and a test can hold a view to a scene.
#include "blorb/appearance.h"
#include "blorb/duel_session.h"
#include "paint/duel.h"

namespace paint {

// The friend as the card carries him: no genome, only his look and species.
blorb::Appearance lookOf(const blorb::BattleCard&);

// The rim hint as boot.h's BootHint gives it.
RimHint rimOf(const blorb::BootHint&);

// Whether this phase has a duel scene. The lobby has none yet: the dish
// stays on screen with the friend hint until a friend is matched.
bool hasScene(blorb::DuelPhase);

DuelScene sceneOf(const blorb::DuelView&, const blorb::Appearance& me);

}  // namespace paint
