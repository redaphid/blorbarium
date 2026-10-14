#pragma once
// A whole duel in the simulator, ahead of the Dish: D8's DuelSession on the
// sim's radio, played by a scripted kid who reads the view as a kid reads the
// screen. It stands in for D9's DishDuel until the Dish takes the radio, so
// nothing it does reaches the pet: wounds, a fall, the book and the friend
// egg are logged, not applied. When the Dish duels, the kid's moves become
// hand lines (a tilt, a knock, BOOT) instead of a DuelInput.
//
// Everything the film needs is a stderr line:
//   duel <ms> phase <name>
//   duel <ms> state <my hearts> <max> <their hearts> <max> <my sparks> <theirs> <my special> <theirs> <stakes>
//                   <my hold> <theirs>   (0..255: the Mortal confirm, or in Mercy the Finish)
//   duel <ms> turn <n> <my stance> <theirs> <hearts I lost> <they lost> <my special's fate> <theirs> <super>
//   duel <ms> downed | fall | egg | closed
//   duel <ms> met <outcome hash> <turns> <won|lost|draw>
//   kid <ms> <what the kid did>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "blorb/duel_session.h"
#include "blorb/species.h"
#include "duel_access.h"

namespace duelbot {

// How this kid plays: `scrap:7:finish:hug` is a Scrap, seed 7, finishing a
// downed friend and hugging after.
struct Style {
  blorb::Stakes tier = blorb::Stakes::Scrap;
  uint32_t seed = 1;
  bool finish = false, hug = false;
  uint16_t queuePermille = 300;   // turns with the meter full on which the kid queues a special
};

inline const char* parseStyle(const char* spec, Style& s) {
  char tier[8] = "", a[8] = "", b[8] = "";
  unsigned seed = 1;
  if (sscanf(spec, "%7[a-z]:%u:%7[a-z]:%7[a-z]", tier, &seed, a, b) < 1)
    return "want <spar|scrap|mortal>[:seed[:finish|spare[:hug]]]";
  if (!strcmp(tier, "spar")) s.tier = blorb::Stakes::Spar;
  else if (!strcmp(tier, "scrap")) s.tier = blorb::Stakes::Scrap;
  else if (!strcmp(tier, "mortal")) s.tier = blorb::Stakes::Mortal;
  else return "the tier is spar, scrap or mortal";
  s.seed = seed;
  s.finish = !strcmp(a, "finish");
  s.hug = !strcmp(a, "hug") || !strcmp(b, "hug");
  return nullptr;
}

class BenchDuel {
 public:
  BenchDuel(blorb::SpeciesId species, const blorb::PeerAddr& self, Style style)
      : style_(style),
        genome_(blorb::starterGenomeOf(species, style.seed)),
        rng_(blorb::Rng::seeded(style.seed * 31 + self.mac[5])) {
    setup_.card.species = species;
    setup_.card.power = blorb::speciesInfo(species).power;
    setup_.card.knack.order = uint8_t(self.mac[5] % 6);
    for (uint8_t b : self.mac) setup_.card.lineageId = setup_.card.lineageId << 8 | b;
    setup_.genome = &genome_;
    setup_.ceiling.top = blorb::Stakes::Mortal;
    setup_.skull[1] = 60, setup_.skull[2] = 140;
    setup_.manualTurns = 100;   // a kid who has played enough for Mortal
    setup_.nonce = style.seed * 2654435761u + self.mac[5];
  }

  bool open(blorb::Air& air, uint32_t tick) { return session_.open(blorb::DuelTestAccess::consent(), air, setup_, tick); }
  bool active() const { return session_.active(); }

  // One 10 Hz tick, at script time `ms`.
  void tick(blorb::Air& air, uint32_t tick, uint32_t ms) {
    const blorb::DuelView v = session_.view(tick);
    say(v, ms);
    const blorb::DuelEffects fx = session_.tick(air, kid(v, ms), tick);
    if (fx.downed) fprintf(stderr, "duel %u downed\n", ms);
    if (fx.fall) fprintf(stderr, "duel %u fall\n", ms);
    if (fx.met) {
      const bool won = (fx.met->ending == blorb::Ending::AWon) == (fx.met->me == blorb::Side::A);
      fprintf(stderr, "duel %u met %08x %u %s\n", ms, unsigned(fx.met->outcomeHash), unsigned(fx.met->turns),
              fx.met->ending == blorb::Ending::Draw ? "draw" : won ? "won" : "lost");
    }
    if (fx.egg) fprintf(stderr, "duel %u egg\n", ms);
    if (fx.radioClosed) fprintf(stderr, "duel %u closed\n", ms);
  }

 private:
  static const char* stance(blorb::Stance s) {
    return s == blorb::Stance::Pounce ? "pounce" : s == blorb::Stance::Guard ? "guard" : "trick";
  }

  void say(const blorb::DuelView& v, uint32_t ms) {
    static const char* const kPhase[] = {"closed",   "lobby",    "greeting", "staking", "choosing", "waiting",
                                         "revealing", "fainting", "mercy",    "fallen",  "friends"};
    static const char* const kStakes[] = {"spar", "scrap", "mortal"};
    static const char* const kWind[] = {"-", "quick", "heavy"};
    static const char* const kFate[] = {"-", "committed", "landed", "riposted", "whiffed", "staggered"};
    if (v.phase != phase_) {
      phase_ = v.phase;
      const size_t p = size_t(v.phase);
      fprintf(stderr, "duel %u phase %s\n", ms, p < sizeof(kPhase) / sizeof(*kPhase) ? kPhase[p] : "other");
    }
    char state[112];
    snprintf(state, sizeof(state), "%u %u %u %u %u %u %s %s %s %u %u", v.mine.hearts, v.mine.maxHearts, v.theirs.hearts,
             v.theirs.maxHearts, v.mine.spark, v.theirs.spark, kWind[uint8_t(v.mine.queued)],
             kWind[uint8_t(v.theirs.queued)], kStakes[uint8_t(v.agreed)], v.confirmHold, v.theirHold);
    if (state_ != state) {
      state_ = state;
      fprintf(stderr, "duel %u state %s\n", ms, state);
    }
    if (v.hasLast && v.lastTurn != lastTurn_) {
      lastTurn_ = v.lastTurn;
      const blorb::TurnReport& t = v.last;   // index 0 is always me
      auto shown = [&](int i) { return t.dozed[i] ? "doze" : t.bracing[i] ? "brace" : stance(t.shown[i]); };
      fprintf(stderr, "duel %u turn %u %s %s %u %u %s %s %s\n", ms, unsigned(v.lastTurn), shown(0), shown(1), t.hit[0],
              t.hit[1], kFate[uint8_t(t.special[0])], kFate[uint8_t(t.special[1])], t.super[0] || t.super[1] ? "super" : "-");
    }
  }

  static void act(uint32_t ms, const char* what) { fprintf(stderr, "kid %u %s\n", ms, what); }

  // Once per phase: a hold starts on whatever tick the kid first sees the phase.
  void actOnce(uint32_t ms, const blorb::DuelView& v, const char* what) {
    if (actedIn_ == v.phase) return;
    actedIn_ = v.phase;
    act(ms, what);
  }

  // D8's scripted kid (test_duel_session, Device::kid), naming each move, and
  // aiming Trick at a friend who is winding up a special, to stagger it.
  blorb::DuelInput kid(const blorb::DuelView& v, uint32_t ms) {
    using blorb::DuelPhase;
    blorb::DuelInput in;
    switch (v.phase) {
      case DuelPhase::Lobby:
        if (v.peerCount > 0) {
          in.aim = 0;
          in.knock = v.phaseTicks % 5 == 0;
          if (in.knock && !picked_) act(ms, "picks the friend on the rim");
          picked_ |= in.knock;
        }
        break;
      case DuelPhase::Staking: {
        const auto tier = blorb::Stakes(std::min(uint8_t(style_.tier), uint8_t(v.ceiling[0])));
        in.aim = int8_t(tier);
        if (v.phaseTicks >= 15) {
          if (tier == blorb::Stakes::Mortal) in.bootDown = true;
          else if (tier == blorb::Stakes::Scrap) in.press = v.phaseTicks % 4 == 3;
          else in.knock = v.phaseTicks % 4 == 3;
        }
        if (v.phaseTicks == 15)
          act(ms, tier == blorb::Stakes::Mortal ? "holds BOOT: mortal" : tier == blorb::Stakes::Scrap ? "BOOT: scrap" : "knock: spar");
        break;
      }
      case DuelPhase::Choosing:
        if (v.phaseTicks == 0 || planAt_ < 0) {
          planAt_ = int16_t(12 + rng_.below(45));   // a kid takes a moment, and never runs the clock out
          punish_ = v.theirs.queued != blorb::Windup::None && rng_.below(100) < 60;
          planAim_ = punish_ ? int8_t(blorb::Stance::Trick) : int8_t(rng_.below(3));
          planHow_ = rng_.below(1000) < style_.queuePermille ? uint8_t(1 + rng_.below(2)) : 0;
        }
        if (v.phaseTicks == planAt_) {
          in.aim = planAim_;
          if (planHow_ == 0 || !v.specialReady) {
            in.knock = true;
            act(ms, punish_ ? "trick, knock: stagger it!" : planAim_ == 0 ? "pounce, knock" : planAim_ == 1 ? "guard, knock" : "trick, knock");
          } else if (planHow_ == 1) {
            in.press = true;
            act(ms, "BOOT: quick special");
          } else {
            in.hold = in.bootDown = true;
            act(ms, "BOOT held: heavy special");
          }
          planAt_ = -1;
        }
        break;
      case DuelPhase::Mercy:
        if (v.iAmDown) {
          in.press = v.phaseTicks == 5;
        } else if (style_.finish) {
          in.bootDown = true;
          actOnce(ms, v, "holds BOOT: finish");
        } else if (v.phaseTicks == 10) {
          in.knock = true;
          act(ms, "knock: spare");
        }
        break;
      case DuelPhase::Friends:
        if (style_.hug) {
          in.bootDown = true;
          actOnce(ms, v, "holds BOOT: hug");
        }
        break;
      default:
        break;
    }
    return in;
  }

  Style style_;
  blorb::Genome genome_;
  blorb::DuelSetup setup_;
  blorb::DuelSession session_;
  blorb::Rng rng_;
  blorb::DuelPhase phase_ = blorb::DuelPhase::Closed;
  std::string state_;
  uint8_t lastTurn_ = 0xff;
  int16_t planAt_ = -1;
  int8_t planAim_ = 0;
  uint8_t planHow_ = 0;
  bool punish_ = false, picked_ = false;
  blorb::DuelPhase actedIn_ = blorb::DuelPhase::Closed;
};

}  // namespace duelbot
