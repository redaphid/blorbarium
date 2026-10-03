// learnsim: runs body-only lives under a scripted owner and prints what the
// brain learned as "M <key> ..." lines that score.py aggregates.
//
//   life <style> <seed> <days>
//
// Styles: rich rough doting quiet (plausible owners), trainer (double knock
// on every Call), punisher (shake on every Chase), cuetrainer / cuecontrol
// (a knock cue; a hop within 5 s earns a double knock, or nothing), sloppy
// (overfeeds, so pellets rot in the dish), neglect (never feeds).
// FEED=demand presses BOOT when hunger >= 0.25 and the dish has no fresh
// pellet; otherwise BOOT is pressed at 08:00, 13:00 and 19:00.
// GARDEN=<days> then hatches the run's final genome in a fresh dish under one
// fixed owner and seed, so lineages are compared on what they inherited.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <variant>
#include <vector>
#include "blorb/dish.h"
#include "links.h"
#include "mem_storage.h"
#include "traces.h"

using namespace blorb;
using namespace blorbtest;

constexpr size_t F = kFeatureCount, A = kActionCount, D = kDriveCount;
using Table = double[F][A][D];
constexpr uint32_t kTicksPerDay = 864000;

double v(Fx x) { return x.raw / 16777216.0; }

const char* locusName(LocusId id) {
  for (const LocusInfo& l : LOCI) if (l.id == id) return l.name;
  for (const StimInfo& s : STIMULI) if (locus::recent(s.id) == id) return s.name;
  return "?";
}
std::string featName(LocusId id) {
  std::string n = locusName(id);
  return id.v >= kRecentBase && id.v < 128 ? "recent_" + n : n;
}
// Measured here, not read from the engine, so the yardstick does not move with the change.
bool contextCue(LocusId id) {
  return id == locus::light || id == locus::day_sin || id == locus::day_cos || id == locus::age ||
         id == locus::marble_near || id == locus::asleep;
}
const char* actName(ActionId a) { for (auto& r : ACTIONS) if (r.id == a) return r.name; return "?"; }
const char* driveName(DriveId d) { for (auto& r : DRIVES) if (r.id == d) return r.name; return "?"; }
size_t actIndex(ActionId a) { for (size_t i = 0; i < A; ++i) if (ACTIONS[i].id == a) return i; return 0; }
int featIndex(LocusId id) { for (size_t f = 0; f < F; ++f) if (FEATURES[f] == id) return int(f); return -1; }
int driveIndex(DriveId id) { for (size_t d = 0; d < D; ++d) if (DRIVES[d].id == id) return int(d); return -1; }

void snap(const Brain& b, Table& t) {
  for (size_t f = 0; f < F; ++f)
    for (size_t a = 0; a < A; ++a)
      for (size_t d = 0; d < D; ++d) t[f][a][d] = v(b.predict(FEATURES[f], ACTIONS[a].id, DRIVES[d].id));
}

// What the genome alone would wire: every instinct expressed so far, dreamt into a blank brain.
void instinctBaseline(const Creature& c, Table& t) {
  Brain b;
  Rng rng = Rng::seeded(1);
  const Phenotype& p = c.phenotype();
  for (const Instinct& in : p.instincts) b.queueInstinct(in);
  for (size_t i = 0; i < p.instincts.size(); ++i) b.dream(p.temperament, rng);
  snap(b, t);
}

bool freshPellet(const Dish& dish, const Creature& c) {
  uint32_t rot = c.phenotype().habitat.rotTicks;
  for (const Pellet& p : dish.habitat().pellets)
    if (p.present && dish.tickCount() - p.droppedTick <= rot) return true;
  return false;
}

struct Owner {
  enum G { Rest, Knock, DKnock, Shake, Tilt, Cradle, Lid, Flip, Drop, Press };
  G g = Rest;
  uint32_t until = 0, start = 0;
  int tiltSign = 1;
  std::mt19937 rng;
  std::string style, feed;
  int lastMealHour = -1;
  int presses = 0;            // queued presses (sloppy feeds three at a time)
  bool wasNight = false;
  uint32_t counts[10] = {};
  uint32_t lastCueMs = 0;
  bool cueOpen = false;
  uint32_t cueDays[64] = {};

  bool cueStyle() const { return style == "cuetrainer" || style == "cuecontrol"; }

  BodySample sample(uint32_t ms) {
    BodySample s = still();
    uint32_t t = ms - start;
    switch (g) {
      case Knock: if (t == 0) s.tapCode = 1; break;
      case DKnock: if (t == 0) s.tapCode = 2; break;
      case Shake: s = jolt((t / 80) % 2 == 0); break;
      case Tilt: s.ax = int16_t(250 * tiltSign); s.az = 968; break;
      case Cradle: s = cradled(); break;
      case Lid: s.az = -1000; break;
      case Flip: s.az = -1000; break;
      case Drop: if (t < 160) { s.az = 50; } else if (t < 200) { s.az = 2400; } break;
      case Press: s.buttonDown = true; break;
      default: break;
    }
    return s;
  }
  void act(G ng, uint32_t ms, uint32_t dur) { g = ng; start = ms; until = ms + dur; ++counts[ng]; }

  void choose(uint32_t ms, const Dish& dish) {
    if (g != Rest) { g = Rest; until = ms + 1500; return; }
    double hour = v(dish.clock().dayFraction()) * 24;
    bool night = dish.clock().night();
    if (std::holds_alternative<Egg>(dish.occupant())) return act(Cradle, ms, 4000);
    const Creature* c = std::get_if<Creature>(&dish.occupant());
    if (!c) { until = ms + 30000; return; }
    if (night && !wasNight && !c->body().asleep) { wasNight = night; return act(Lid, ms, 10 * 60000); }
    wasNight = night;
    const double hunger = v(c->chemistry().drive(drive::hunger));
    const bool pellet = dish.habitat().nearestPellet(DishPos{}).has_value();
    const bool pantry = dish.habitat().pantry > 0;
    if (presses > 0 && pantry) { --presses; return act(Press, ms, 200); }
    if (style == "neglect") { until = ms + 60000; return; }
    if (feed == "demand") {
      if (hunger >= 0.25 && !freshPellet(dish, *c) && pantry) {
        if (style == "sloppy") presses = 2;
        return act(Press, ms, 200);
      }
    } else {
      for (int mh : {8, 13, 19}) {
        if (int(hour) == mh && lastMealHour != mh) {
          lastMealHour = mh;
          if (!pellet && pantry) return act(Press, ms, 200);
        }
      }
    }
    // Any style but neglect feeds a starving pet.
    if (hunger > 0.7 && !pellet && pantry) return act(Press, ms, 200);
    if (night || c->body().asleep) { until = ms + 60000; return; }
    std::uniform_real_distribution<double> u(0, 1);
    if (style == "quiet" || cueStyle()) {
      if (v(c->chemistry().drive(drive::need_touch)) >= 0.5) return act(Cradle, ms, 4000);
      if (cueStyle() && ms - lastCueMs >= 60000) {   // a cue knock about once a minute
        lastCueMs = ms;
        cueOpen = true;
        ++cueDays[std::min<uint32_t>(63, dish.tickCount() / kTicksPerDay)];
        return act(Knock, ms, 100);
      }
      until = ms + 10000;
      return;
    }
    double r = u(rng);
    double knock = 0, dk = 0, shake = 0, tilt = 0, cradle = 0, flip = 0, drop = 0;
    uint32_t gap = 90000;
    if (style == "rich" || style == "trainer" || style == "punisher" || style == "sloppy") {
      knock = .27; dk = .10; shake = .10; tilt = .25; cradle = .22; flip = .05; drop = .002;
    }
    if (style == "rough") { knock = .20; shake = .45; tilt = .20; cradle = .05; flip = .10; drop = .004; }
    if (style == "doting") { knock = .25; dk = .25; tilt = .10; cradle = .40; }
    double acc = 0;
    std::uniform_int_distribution<uint32_t> jitter(gap / 3, gap * 5 / 3);
    if (u(rng) > 0.25) { until = ms + jitter(rng); return; }   // most looks, the owner just watches
    if (r < (acc += knock)) return act(Knock, ms, 100);
    if (r < (acc += dk)) return act(DKnock, ms, 100);
    if (r < (acc += shake)) return act(Shake, ms, 1000);
    if (r < (acc += tilt)) { tiltSign = u(rng) < .5 ? 1 : -1; return act(Tilt, ms, 15000); }
    if (r < (acc += cradle)) return act(Cradle, ms, 15000);
    if (r < (acc += flip)) return act(Flip, ms, 1500);
    if (r < (acc += drop)) return act(Drop, ms, 400);
    until = ms + jitter(rng);
  }
};

// Per-run tallies. Days index from the run's start.
struct Tally {
  static constexpr int kDays = 64;
  uint32_t picks[kDays][A] = {};        // switch-ins while awake
  uint32_t ticks[kDays][A] = {};        // awake ticks per action
  uint32_t hourPicks[24][A] = {};       // second half of the run
  uint32_t hourEatNoFood[24] = {};
  uint32_t rot[kDays][2][2] = {};       // [day][nearest rotten][picked eat]
  uint32_t cues[kDays] = {}, cueHops[kDays] = {};
  uint32_t bites[kDays] = {}, rottenBites[kDays] = {};
  double driveSum[D] = {};
  uint64_t driveLow[D] = {}, awake = 0, painHigh = 0;
  double wakeHunger = 0;
  uint32_t wakes = 0;
  uint64_t stageAwake[4] = {}, stageHungryByFood[4] = {};   // hunger >= 0.4 with a fresh pellet in the dish
};

struct Run {
  Dish& dish;
  Owner& owner;
  Tally t;
  int days;
  std::string label;
  static Table w0, base;

  void report(const Creature& c, const char* tag) {
    static Table now;
    snap(c.brain(), now);
    instinctBaseline(c, base);
    std::vector<Belief> top = c.brain().strongestBeliefs(8);
    for (const Belief& b : top) {
      int f = featIndex(b.feature), a = int(actIndex(b.action)), d = driveIndex(b.drive);
      printf("M %s_belief %d %s %d %s %s %+.3f %+.3f %+.3f\n", tag, c.generation(), featName(b.feature).c_str(),
             int(contextCue(b.feature)), actName(b.action), driveName(b.drive), v(b.effect),
             f >= 0 && d >= 0 ? w0[f][a][d] : 0.0, f >= 0 && d >= 0 ? base[f][a][d] : 0.0);
    }
  }

  void loop() {
    ActionId lastAction{255};
    bool wasAsleep = false, sawClutch = false;
    int gen = -1;
    Owner::G pending = Owner::Rest;
    uint32_t pendingDur = 0;
    const uint64_t endMs = uint64_t(days) * 24 * 3600 * 1000;
    for (uint64_t ms64 = 0; ms64 < endMs; ms64 += kSampleMs) {
      const uint32_t ms = uint32_t(ms64);
      if (pending != Owner::Rest && owner.g == Owner::Rest) {
        owner.act(pending, ms, pendingDur);
        pending = Owner::Rest;
      }
      if (ms >= owner.until) owner.choose(ms, dish);
      dish.sample(owner.sample(ms), ms);
      const uint32_t before = dish.tickCount();
      static NullLink link;
      dish.tick(ms, link);
      if (dish.tickCount() == before) continue;
      const int day = std::min<int>(Tally::kDays - 1, int(dish.tickCount() / kTicksPerDay));
      if (const Clutch* k = std::get_if<Clutch>(&dish.occupant())) {
        if (!sawClutch) {
          sawClutch = true;
          printf("M death %d %.2f %d %d %zu\n", gen, dish.tickCount() / double(kTicksPerDay), int(k->cause),
                 int(k->reached), k->heirlooms.size());
          for (const Belief& b : k->heirlooms) {
            int f = featIndex(b.feature), a = int(actIndex(b.action)), d = driveIndex(b.drive);
            printf("M heir %d %s %d %s %s %+.3f %+.3f %+.3f\n", gen, featName(b.feature).c_str(),
                   int(contextCue(b.feature)), actName(b.action), driveName(b.drive), v(b.effect),
                   f >= 0 && d >= 0 ? w0[f][a][d] : 0.0, f >= 0 && d >= 0 ? base[f][a][d] : 0.0);
          }
        }
        continue;
      }
      sawClutch = false;
      const Creature* c = std::get_if<Creature>(&dish.occupant());
      if (!c) continue;
      if (c->generation() != gen) {
        gen = c->generation();
        snap(c->brain(), w0);
        printf("M hatch %d %.2f\n", gen, dish.tickCount() / double(kTicksPerDay));
      }
      // The baseline at death is the one in force just before it.
      if (dish.tickCount() % 6000 == 0) instinctBaseline(*c, base);
      const Chemistry& ch = c->chemistry();
      const bool awake = !c->body().asleep;
      static double lastInjury = 0;
      if (getenv("DIAG") && v(ch.chem[chem::injury.v]) - lastInjury > 0.03) {
        printf("injury %.4f +%.2f gesture=%d stims:", dish.tickCount() / double(kTicksPerDay),
               v(ch.chem[chem::injury.v]) - lastInjury, int(owner.g));
        for (const StimInfo& s : STIMULI)
          if (v(ch.locus[locus::recent(s.id).v]) > 0.2) printf(" %s", s.name);
        printf("\n");
      }
      lastInjury = v(ch.chem[chem::injury.v]);
      if (getenv("DIAGFROM") && dish.tickCount() >= atof(getenv("DIAGFROM")) * kTicksPerDay)
        printf("dying %.4f die=%.2f cause=%.3f injury=%.2f life=%.2f starvation=%.2f hunger=%.2f\n",
               dish.tickCount() / double(kTicksPerDay), v(ch.locus[locus::die.v]), v(ch.locus[locus::cause.v]),
               v(ch.chem[chem::injury.v]), v(ch.chem[chem::life.v]), v(ch.chem[34]), v(ch.drive(drive::hunger)));
      if (getenv("DIAG") && dish.tickCount() % 36000 == 0)
        printf("diag %.2f %s %-11s hunger=%.2f sleepy=%.2f bored=%.2f pain=%.2f injury=%.2f toxin=%.2f life=%.2f\n",
               dish.tickCount() / double(kTicksPerDay), awake ? "awake " : "asleep", actName(c->action()),
               v(ch.drive(drive::hunger)), v(ch.drive(drive::sleepiness)), v(ch.drive(drive::boredom)),
               v(ch.drive(drive::pain)), v(ch.chem[chem::injury.v]), v(ch.chem[chem::toxin.v]),
               v(ch.chem[chem::life.v]));
      // A recent locus reads 0.5 right after the tick that fired it, then halves.
      if (v(ch.locus[locus::recent(stim::fed).v]) > 0.4) {
        ++t.bites[day];
        if (v(ch.locus[locus::recent(stim::fed_bad).v]) > 0.4) ++t.rottenBites[day];
      }
      if (awake && wasAsleep) { t.wakeHunger += v(ch.drive(drive::hunger)); ++t.wakes; }
      wasAsleep = !awake;
      const size_t ai = actIndex(c->action());
      const int hour = int(v(dish.clock().dayFraction()) * 24) % 24;
      if (awake) {
        ++t.awake;
        ++t.ticks[day][ai];
        for (size_t d = 0; d < D; ++d) {
          double x = v(ch.drive(DRIVES[d].id));
          t.driveSum[d] += x;
          if (x < 0.02) ++t.driveLow[d];
        }
        if (v(ch.drive(drive::pain)) > 0.5) ++t.painHigh;
        const int stage = int(c->stage());
        ++t.stageAwake[stage];
        if (v(ch.drive(drive::hunger)) >= 0.4 && freshPellet(dish, *c)) ++t.stageHungryByFood[stage];
      }
      if (c->action() != lastAction) {
        lastAction = c->action();
        if (awake) {
          ++t.picks[day][ai];
          if (day >= days / 2) {
            ++t.hourPicks[hour][ai];
            if (c->action() == action::eat && !dish.habitat().nearestPellet(DishPos{})) ++t.hourEatNoFood[hour];
          }
          if (v(ch.locus[locus::food_near.v]) > 0) {
            bool rotten = v(ch.locus[locus::food_rotten.v]) > 0.5;
            ++t.rot[day][rotten][c->action() == action::eat];
          }
          if (owner.cueOpen && ms - owner.lastCueMs <= 5000 && c->action() == action::hop_circles) {
            owner.cueOpen = false;
            ++t.cueHops[day];
            if (owner.style == "cuetrainer") { pending = Owner::DKnock; pendingDur = 100; }
          }
          if (owner.style == "trainer" && c->action() == action::call) { pending = Owner::DKnock; pendingDur = 100; }
          if (owner.style == "punisher" && c->action() == action::chase) { pending = Owner::Shake; pendingDur = 1000; }
        }
      }
      if (owner.cueOpen && ms - owner.lastCueMs > 5000) owner.cueOpen = false;
    }
    for (int d = 0; d < Tally::kDays; ++d) t.cues[d] = owner.cueDays[d];
  }

  void summary() {
    printf("M awake %llu\n", (unsigned long long)t.awake);
    for (size_t d = 0; d < D; ++d)
      printf("M drive %s %.3f %.3f\n", DRIVES[d].name, t.driveSum[d] / std::max<uint64_t>(1, t.awake),
             double(t.driveLow[d]) / std::max<uint64_t>(1, t.awake));
    printf("M painhigh %.3f\n", double(t.painHigh) / std::max<uint64_t>(1, t.awake));
    printf("M wakehunger %.3f %u\n", t.wakes ? t.wakeHunger / t.wakes : 0.0, t.wakes);
    for (int s = 0; s < 4; ++s)
      printf("M starving_by_food %d %llu %llu\n", s, (unsigned long long)t.stageHungryByFood[s],
             (unsigned long long)t.stageAwake[s]);
    for (int day = 0; day < days && day < Tally::kDays; ++day) {
      printf("M picks %d", day);
      for (size_t a = 0; a < A; ++a) printf(" %u", t.picks[day][a]);
      printf("\nM ticks %d", day);
      for (size_t a = 0; a < A; ++a) printf(" %u", t.ticks[day][a]);
      printf("\nM rot %d %u %u %u %u\n", day, t.rot[day][0][0], t.rot[day][0][1], t.rot[day][1][0], t.rot[day][1][1]);
      printf("M cue %d %u %u\n", day, t.cues[day], t.cueHops[day]);
      printf("M bites %d %u %u\n", day, t.bites[day], t.rottenBites[day]);
    }
    for (int h = 0; h < 24; ++h) {
      printf("M hour %d", h);
      for (size_t a = 0; a < A; ++a) printf(" %u", t.hourPicks[h][a]);
      printf(" %u\n", t.hourEatNoFood[h]);
    }
    printf("M gestures");
    for (uint32_t n : owner.counts) printf(" %u", n);
    printf("\n");
  }
};
Table Run::w0, Run::base;

const Genome* finalGenome(const Dish& dish, std::optional<Genome>& hold) {
  if (const Creature* c = std::get_if<Creature>(&dish.occupant())) return &c->genome();
  if (const Egg* e = std::get_if<Egg>(&dish.occupant())) return &e->genome();
  if (const Clutch* k = std::get_if<Clutch>(&dish.occupant())) { hold = k->child(0).genome; return &*hold; }
  return nullptr;
}

int main(int argc, char** argv) {
  std::string style = argc > 1 ? argv[1] : "rich";
  uint32_t seed = argc > 2 ? uint32_t(atoi(argv[2])) : 7;
  int days = argc > 3 ? atoi(argv[3]) : 3;
  const char* feed = getenv("FEED");
  printf("M run %s %u %d %s\n", style.c_str(), seed, days, feed ? feed : "routine");

  MemStorage store;
  Dish dish(store, seed, 1);
  Owner owner;
  owner.style = style;
  owner.feed = feed ? feed : "routine";
  owner.rng.seed(seed * 7919u);
  Run run{dish, owner, {}, days, style};
  run.loop();
  run.summary();
  if (const Creature* c = std::get_if<Creature>(&dish.occupant())) {
    printf("M end %d %d\n", c->generation(), int(c->stage()));
    run.report(*c, "end");
    static Table now;
    snap(c->brain(), now);
    if (const char* path = getenv("WOUT"))
    if (FILE* out = fopen(path, "wb")) {
      fwrite(now, sizeof now, 1, out);
      fwrite(Run::w0, sizeof Run::w0, 1, out);
      fclose(out);
    }
  }
  int heirGenes = 0;
  std::optional<Genome> hold;
  const Genome* g = finalGenome(dish, hold);
  if (g)
    g->forEach([&](const GeneView& gv) {
      if (gv.header.type != 0x11 || !(gv.header.flags & GeneFlags::Heirloom)) return;
      ++heirGenes;
      printf("M heirgene %s %s %s %+.2f\n", gv.body[0] == 255 ? "-" : featName(LocusId{gv.body[0]}).c_str(),
             actName(ActionId{gv.body[3]}), driveName(DriveId{gv.body[4]}), (int(gv.body[5]) - 128) / 128.0);
    });
  printf("M heirgenes %d\n", heirGenes);

  if (const char* gd = getenv("GARDEN"); gd && g) {
    Genome founder = *g;
    MemStorage gstore;
    DishOptions opt;
    opt.founder = &founder;
    Dish garden(gstore, 4242, 2, opt);
    Owner go;
    go.style = "rich";
    go.feed = "demand";
    go.rng.seed(4242);
    Run gr{garden, go, {}, atoi(gd), "garden"};
    printf("M garden_begin\n");
    gr.loop();
    gr.summary();
    printf("M garden_end\n");
  }
  return 0;
}
