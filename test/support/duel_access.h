#pragma once
// Tests and the simulator's duel bot stand in for the Dish: they need a
// LocalConsent, which only DishDuel makes in the firmware.
#include "blorb/duel_session.h"

namespace blorb {

struct DuelTestAccess {
  static LocalConsent consent() { return LocalConsent{}; }
};

}  // namespace blorb
