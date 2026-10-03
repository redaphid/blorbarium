#pragma once
// Actions: what the creature does in the dish, and the reflexes that
// interrupt it. The brain picks an action; its behaviour moves the body, sets
// a pose, may bite food and may fire a self-stimulus, until it reports Done or
// the brain switches after minTicks. A reflex (registry REFLEXES) overrides
// the behaviour's motion while it runs, then hands the body back.
#include <cstdint>
#include <optional>
#include "blorb/fixed.h"
#include "blorb/habitat.h"
#include "blorb/registry.h"

namespace blorb {

struct ActiveReflex {
  ReflexId kind;
  uint16_t tick;       // ticks since it started
  uint16_t ticks;      // its length (REFLEXES row)
  Fx strength;         // the trigger locus level at the edge: hop height
};

// Where the creature is and how it holds itself now. Written by behaviours
// and reflexes, read by the presentation.
struct Body {
  DishPos at{};
  Fx facing{};                       // -1..1 = -pi..pi
  PoseId pose{};
  uint16_t poseTick = 0;
  bool asleep = false, dreaming = false;
  std::optional<ActiveReflex> reflex;
};

struct ActionCtx {
  Habitat& habitat;
  const HabitatRules& rules;
  Fx tiltX, tiltY;
  uint32_t tick;
  Rng& rng;
  SenseOut& out;          // self-stimuli and loci (asleep, eating, foreseeing), applied by the creature next tick
  Fx foodEaten{};
  bool ateRotten = false;
};

enum class Status : uint8_t { Running, Done };

namespace behave {
struct Base {
  void start(Body&, ActionCtx&) {}
  void stop(Body&, ActionCtx&) {}
};
struct Rest : Base { Status step(Body&, ActionCtx&); };                 // idle, breathe
struct Wander : Base { Status step(Body&, ActionCtx&); };               // random walk inside the rim
struct Eat : Base {                                                     // to the nearest pellet, bite(), chew; eating locus
  void start(Body&, ActionCtx&); Status step(Body&, ActionCtx&);
};
struct Sleep : Base {                                                   // asleep locus; Done when sleep_gate drops
  void start(Body&, ActionCtx&); Status step(Body&, ActionCtx&); void stop(Body&, ActionCtx&);
};
struct Foresee : Base {                                                 // foreseeing locus = 1; still, eyes up
  void start(Body&, ActionCtx&); Status step(Body&, ActionCtx&); void stop(Body&, ActionCtx&);
};
struct Call : Base { Status step(Body&, ActionCtx&); };                 // croak at the owner
struct Curl : Base { Status step(Body&, ActionCtx&); };                 // hood up, toward the rim
struct FollowTilt : Base { Status step(Body&, ActionCtx&); };           // downhill
struct FleeTilt : Base { Status step(Body&, ActionCtx&); };             // uphill
struct Chase : Base { Status step(Body&, ActionCtx&); };                // after the marble; MarbleHit on contact
struct HopCircles : Base { Status step(Body&, ActionCtx&); };           // happy hops
}  // namespace behave

// Every behaviour, generated from defs/actions.def and dispatched by stable
// id. Adding an action is a struct above plus one row.
struct Behaviours {
#define BLORB_ACTION(id, name, Type, minTicks, self) behave::Type name;
#include "blorb/defs/actions.def"
#undef BLORB_ACTION
  void start(ActionId a, Body& b, ActionCtx& c) {
    switch (a.v) {
#define BLORB_ACTION(id, name, Type, minTicks, self) case id: name.start(b, c); return;
#include "blorb/defs/actions.def"
#undef BLORB_ACTION
      default: return;
    }
  }
  Status step(ActionId a, Body& b, ActionCtx& c) {
    switch (a.v) {
#define BLORB_ACTION(id, name, Type, minTicks, self) case id: return name.step(b, c);
#include "blorb/defs/actions.def"
#undef BLORB_ACTION
      default: return Status::Done;
    }
  }
  void stop(ActionId a, Body& b, ActionCtx& c) {
    switch (a.v) {
#define BLORB_ACTION(id, name, Type, minTicks, self) case id: name.stop(b, c); return;
#include "blorb/defs/actions.def"
#undef BLORB_ACTION
      default: return;
    }
  }
};

}  // namespace blorb
