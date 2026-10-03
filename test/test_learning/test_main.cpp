#include <gtest/gtest.h>

#include <cstdio>

#include "blorb/dish.h"   // named here so PlatformIO's dependency finder links lib/blorb and lib/paint
#include "paint/sprite_pack.h"
#include "e2e/check.h"
#include "e2e/scenarios.h"

using namespace e2e;

// The contract the comparisons rest on: the arms differ only by Treatment rows.
TEST(Harness, ArmsDifferOnlyByTheStimulus) {
  std::vector<Row> rows = {daily(Who::Both, 0, 0, 11 * kH, press(), 30 * kM), once(Who::Both, 12 * kH, shake())};
  EXPECT_EQ(run(rows, 14 * kH, Arm::Control, 3).finalHash, run(rows, 14 * kH, Arm::Treatment, 3).finalHash);
  rows.push_back(once(Who::Treatment, 13 * kH, shake()));
  EXPECT_NE(run(rows, 14 * kH, Arm::Control, 3).finalHash, run(rows, 14 * kH, Arm::Treatment, 3).finalHash);
  EXPECT_EQ(run(rows, 14 * kH, Arm::Treatment, 3).finalHash, run(rows, 14 * kH, Arm::Treatment, 3).finalHash);
}

// What a plainly cared-for grungo does, hour by hour: the backdrop every
// scenario's probe is read against. Prints; asserts only that he lives.
TEST(Harness, BaselineDay) {
  std::vector<Row> rows;
  for (uint64_t at : {11 * kH, 13 * kH + 30 * kM, 16 * kH, 18 * kH + 30 * kM, 21 * kH}) rows.push_back(daily(Who::Both, 0, 4, at, press()));
  for (int d = 1; d < 5; ++d) rows.push_back(once(Who::Both, d * kD + 11 * kH, shake()));
  for (int d = 1; d < 5; ++d) rows.push_back(once(Who::Both, d * kD + 11 * kH - kS, watch(40 * kS)));
  RunLog log = run(rows, 5 * kD, Arm::Control, 1);
  ASSERT_EQ(log.hatches.size(), 1u) << "fed only while he is awake, he lives five days";
  EXPECT_TRUE(log.deaths.empty());
  std::printf("hour  sleep  rest wand  eat slep fore call curl foll flee chas hopc   hunger bored need_t\n");
  for (int h = 0; h < 24; ++h) {
    uint64_t all = 0, asleep = 0, acts[kActionCount]{};
    int64_t drv[kDriveCount]{};
    for (int d = 1; d < 5; ++d) {
      all += log.creatureTicks[d][h];
      asleep += log.asleepTicks[d][h];
      for (size_t a = 0; a < kActionCount; ++a) acts[a] += log.actionTicks[d][h][a];
      for (size_t k = 0; k < kDriveCount; ++k) drv[k] += log.driveSum[d][h][k];
    }
    std::printf("%4d  %5.2f ", h, double(asleep) / all);
    for (size_t a = 0; a < kActionCount; ++a) std::printf(" %4.2f", double(acts[a]) / all);
    std::printf("   %6.0f %5.0f %6.0f\n", double(drv[0]) / all, double(drv[2]) / all, double(drv[7]) / all);
  }
  for (const Window& w : log.windows) {
    std::printf("shake at day %llu:", (unsigned long long)(w.at / kD));
    for (size_t i = 0; i < w.ticks.size(); i += 10)
      std::printf(" %s%s", detail::actionName(w.ticks[i].action), w.ticks[i].hop ? "^" : "");
    std::printf("  fear peak");
    int16_t peak = 0;
    for (const Tick& t : w.ticks) peak = std::max(peak, t.drive[4]);
    std::printf(" %d\n", peak);
  }
}

// ---- diagnosis -------------------------------------------------------------------
// Not measurements: these read the brain through Peek to trace why a scenario
// shows no effect, and print what they find for e2e-learning.md.

const blorb::Creature* creatureIn(const blorb::Dish& d) { return std::get_if<blorb::Creature>(&d.occupant()); }

double weight(const blorb::Creature& c, blorb::LocusId f, blorb::ActionId a, blorb::DriveId d) {
  return double(c.brain().predict(f, a, d).raw) / blorb::Fx::kOne;
}

// How often a shake is still a feature when he starts an action, and what he believes about it.
TEST(Diagnose, Shaking) {
  const Scenario s = sc::shaking();
  for (Arm arm : {Arm::Control, Arm::Treatment})
    for (uint32_t seed = 1; seed <= 3; ++seed) {
      uint32_t shakes = 0, starts = 0, startsAfterShake = 0, lastShaken = 0;
      uint8_t lastAction = 255;
      double w[blorb::kActionCount]{};
      run(s.rows, 4 * kD + 11 * kH, arm, seed, nullptr, [&](const blorb::Dish& d, uint64_t wall) {
        const blorb::Creature* c = creatureIn(d);
        if (!c) return;
        if (c->stats().shaken != lastShaken) ++shakes, lastShaken = c->stats().shaken;
        if (c->action().v != lastAction) {
          ++starts;
          // The recent locus halves at the end of every tick: above 1/32 here, the shake was within 5 ticks.
          if (c->chemistry().locus[blorb::locus::recent(blorb::stim::shake).v] > blorb::Fx::ratio(1, 32)) ++startsAfterShake;
          lastAction = c->action().v;
        }
        if (wall + blorb::kSampleMs >= 4 * kD + 11 * kH)
          for (size_t a = 0; a < blorb::kActionCount; ++a)
            w[a] = weight(*c, blorb::locus::recent(blorb::stim::shake), blorb::ACTIONS[a].id, blorb::drive::fear);
      });
      std::printf("%s seed %u: %u shakes, %u action starts, %u of them within 0.5 s of a shake; "
                  "W[recent_shake][a][fear] curl %+.3f rest %+.3f wander %+.3f foresee %+.3f eat %+.3f\n",
                  arm == Arm::Treatment ? "T" : "C", seed, shakes, starts, startsAfterShake, w[6], w[0], w[1], w[4], w[2]);
    }
}

// How big his strongest belief is across nights, and how many dreams each night holds.
TEST(Diagnose, Forgetting) {
  std::vector<Row> rows;
  sc::care(rows, Who::Both, 0, 3);
  const uint64_t marks[] = {35 * kM, 2 * kH, 22 * kH, kD + 11 * kH, kD + 22 * kH, 2 * kD + 11 * kH, 2 * kD + 22 * kH, 3 * kD + 11 * kH};
  size_t next = 0;
  run(rows, 3 * kD + 11 * kH + kS, Arm::Control, 1, nullptr, [&](const blorb::Dish& d, uint64_t wall) {
    if (next >= std::size(marks) || wall < marks[next]) return;
    ++next;
    const blorb::Creature* c = creatureIn(d);
    if (!c) return;
    const std::vector<blorb::Belief> top = c->brain().strongestBeliefs(200);
    double strongest = top.empty() ? 0 : std::fabs(double(top[0].effect.raw) / blorb::Fx::kOne);
    std::printf("day %llu %02llu:%02llu  dreams so far %u  nonzero weights %zu  strongest |W| %.4f  "
                "W[food_near][eat][hunger] %+.4f  W[recent_shake][curl][fear] %+.4f\n",
                (unsigned long long)(wall / kD), (unsigned long long)(wall % kD / kH), (unsigned long long)(wall % kH / kM),
                unsigned(c->stats().dreams), top.size(), strongest,
                weight(*c, blorb::locus::food_near, blorb::action::eat, blorb::drive::hunger),
                weight(*c, blorb::locus::recent(blorb::stim::shake), blorb::action::curl, blorb::drive::fear));
  });
}

// The rotten morning of day 5: what state he wakes in, what he believes about
// eating then, and what he does until he bites.
TEST(Diagnose, RottenMorning) {
  const Scenario s = sc::rotten();
  for (uint32_t seed = 1; seed <= 4; ++seed)
    for (Arm arm : {Arm::Control, Arm::Treatment}) {
      bool seen = false;
      double w[blorb::kDriveCount]{};
      RunLog log = run(s.rows, s.endAt, arm, seed, nullptr, [&](const blorb::Dish& d, uint64_t wall) {
        const blorb::Creature* c = creatureIn(d);
        if (seen || wall < 5 * kD + 9 * kH || !c || c->body().asleep) return;
        seen = true;
        for (size_t k = 0; k < blorb::kDriveCount; ++k)
          w[k] = weight(*c, blorb::locus::food_near, blorb::action::eat, blorb::DRIVES[k].id);
      });
      const Window& win = log.windows[1];
      size_t woke = 0;
      while (woke < win.ticks.size() && (win.ticks[woke].asleep || !sc::alive(win.ticks[woke]))) ++woke;
      if (woke >= win.ticks.size()) {
        std::printf("%s seed %u never woke\n", arm == Arm::Treatment ? "T" : "C", seed);
        continue;
      }
      const Tick& t = win.ticks[woke];
      std::printf("%s seed %u wakes %+.1f min: hunger %d pain %d discomfort %d injury %d pellets %u; "
                  "W[food_near][eat] hunger %+.3f discomfort %+.3f pain %+.3f\n   ",
                  arm == Arm::Treatment ? "T" : "C", seed, double(woke) / 600, t.drive[0], t.drive[5], t.drive[6],
                  t.injury, t.pellets, w[0], w[6], w[5]);
      size_t bite = woke;
      while (bite < win.ticks.size() && win.ticks[bite].mouth != blorb::Mouthful::RottenPellet) ++bite;
      for (size_t i = woke; i < bite && i < woke + 6000; i += 300) std::printf(" %s", detail::actionName(win.ticks[i].action));
      std::printf("  -> bites rot after %.1f min (eat share before: %.3f)\n", double(bite - woke) / 600,
                  sc::share(win, double(woke) / 10, double(bite) / 10, [](const Tick& x) { return x.is(sc::eat); }));
    }
}

// Neglect: is the faster bite a lesson (the eat weight) or his state? And the
// face-down owners: where does each leave his pet clock?
TEST(Diagnose, NeglectAndClock) {
  const Scenario neg = sc::neglect(), nights = sc::faceDown(), evenings = sc::faceDownEvenings();
  for (uint32_t seed = 1; seed <= 3; ++seed)
    for (Arm arm : {Arm::Control, Arm::Treatment}) {
      double w[3]{};
      int k = 0;
      const uint64_t at[] = {1 * kD + 22 * kH, 2 * kD + 11 * kH, 3 * kD + 11 * kH};
      run(neg.rows, 3 * kD + 11 * kH + kS, arm, seed, nullptr, [&](const blorb::Dish& d, uint64_t wall) {
        if (k < 3 && wall >= at[k])
          if (const blorb::Creature* c = creatureIn(d)) w[k++] = weight(*c, blorb::locus::food_near, blorb::action::eat, blorb::drive::hunger);
      });
      std::printf("neglect %s seed %u W[food_near][eat][hunger] day1 22:00 %+.3f day2 11:00 %+.3f day3 11:00 %+.3f\n",
                  arm == Arm::Treatment ? "T" : "C", seed, w[0], w[1], w[2]);
    }
  for (const Scenario* s : {&nights, &evenings}) {
    std::printf("%s phase offset (pet minutes ahead of the wall) at 11:00:", s->name);
    int day = 1;
    run(s->rows, 5 * kD + 11 * kH + kS, Arm::Treatment, 1, nullptr, [&](const blorb::Dish& d, uint64_t wall) {
      if (day <= 5 && wall >= uint64_t(day) * kD + 11 * kH) {
        int32_t off = d.clock().phaseOffsetTicks;
        if (off > int32_t(blorb::PetClock::kDayTicks / 2)) off -= int32_t(blorb::PetClock::kDayTicks);
        std::printf(" day %d %+d", day, off / int32_t(blorb::kTicksPerMinute));
        ++day;
      }
    });
    std::printf("\n");
  }
}

// The beliefs the parent hands on: Clutch::heirlooms, captured at death.
TEST(Diagnose, Heirlooms) {
  const Scenario s = sc::heirloom();
  for (Arm arm : {Arm::Control, Arm::Treatment})
    for (uint32_t seed = 1; seed <= 3; ++seed) {
      std::string line;
      bool done = false;
      run(s.rows, 14 * kD + 11 * kH + 2 * kM, arm, seed, nullptr, [&](const blorb::Dish& d, uint64_t) {
        const auto* k = std::get_if<blorb::Clutch>(&d.occupant());
        if (!k || done) return;
        done = true;
        for (const blorb::Belief& b : k->heirlooms) {
          char buf[96];
          std::snprintf(buf, sizeof buf, " [locus %u, %s, drive %u, %+.2f]", unsigned(b.feature.v),
                        detail::actionName(b.action.v), unsigned(b.drive.v), double(b.effect.raw) / blorb::Fx::kOne);
          line += buf;
        }
      });
      std::printf("%s seed %u heirlooms:%s\n", arm == Arm::Treatment ? "T" : "C", seed, line.c_str());
    }
}

// What a held grungo learns about each action while cradled, and what the rotten-fed one learned about eating.
TEST(Diagnose, CradleAndRot) {
  const Scenario hold = sc::holding(), rot = sc::rotten();
  for (Arm arm : {Arm::Control, Arm::Treatment})
    for (uint32_t seed = 1; seed <= 2; ++seed) {
      double cradled[blorb::kActionCount]{}, eat[blorb::kDriveCount]{};
      run(hold.rows, 4 * kD + 11 * kH, arm, seed, nullptr, [&](const blorb::Dish& d, uint64_t wall) {
        if (wall + blorb::kSampleMs < 4 * kD + 11 * kH) return;
        if (const blorb::Creature* c = creatureIn(d))
          for (size_t a = 0; a < blorb::kActionCount; ++a)
            cradled[a] = weight(*c, blorb::locus::cradled, blorb::ACTIONS[a].id, blorb::drive::need_touch);
      });
      run(rot.rows, 4 * kD + 9 * kH, arm, seed, nullptr, [&](const blorb::Dish& d, uint64_t wall) {
        if (wall + blorb::kSampleMs < 4 * kD + 9 * kH) return;
        if (const blorb::Creature* c = creatureIn(d))
          for (size_t k = 0; k < blorb::kDriveCount; ++k)
            eat[k] = weight(*c, blorb::locus::food_near, blorb::action::eat, blorb::DRIVES[k].id);
      });
      std::printf("%s seed %u W[cradled][a][need_touch]:", arm == Arm::Treatment ? "T" : "C", seed);
      for (size_t a = 0; a < blorb::kActionCount; ++a) std::printf(" %s %+.3f", blorb::ACTIONS[a].name, cradled[a]);
      std::printf("\n%s seed %u W[food_near][eat][d]:", arm == Arm::Treatment ? "T" : "C", seed);
      for (size_t k = 0; k < blorb::kDriveCount; ++k) std::printf(" %s %+.3f", blorb::DRIVES[k].name, eat[k]);
      std::printf("\n");
    }
}

// Each scenario: both arms over E2E_SEEDS seeds (default 40). A scenario the
// engine shows asserts its effect. One it does not show yet (knownFailing)
// is skipped with its numbers, and fails the day the effect appears, so the
// mark is removed when the engine improves.
class Learning : public ::testing::TestWithParam<size_t> {};

TEST_P(Learning, TreatmentChangesBehaviour) {
  static const std::vector<Scenario> scenarios = sc::all();
  const Scenario& s = scenarios[GetParam()];
  const Result r = evaluate(s, seedsFromEnv(40));
  const std::string numbers = std::string(s.name) + " (" + s.stimulus + ")\n    " + summary(r);
  std::printf("%s", numbers.c_str());
  if (const char* out = std::getenv("E2E_OUT")) write(r, out);
  const bool shown = r.effects[0].verdict == Verdict::Yes;
  if (s.knownFailing) {
    EXPECT_FALSE(shown) << "the engine now shows this effect: clear knownFailing\n" << numbers;
    if (!shown) GTEST_SKIP() << "known failing, measured: " << numbers;
  } else {
    EXPECT_TRUE(shown) << numbers;
  }
}

INSTANTIATE_TEST_SUITE_P(Scenarios, Learning, ::testing::Range<size_t>(0, sc::all().size()),
                         [](const ::testing::TestParamInfo<size_t>& i) { return std::string(sc::all()[i.param].name); });

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
