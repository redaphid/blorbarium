# Does Grungo learn from what happens to him? End-to-end, simulator only

Branch `e2e` (on `origin/engine` at `a1cb726`). Each scenario was run as a treatment arm and a control arm on 40 seeds. The control is the same timeline without the stimulus. Every number below is measured, unless it is labelled as inferred.

**The short answer.** Only a little, and almost nothing lasts the night. He clearly changes while a stimulus is going on, as a punishment or as a change in his body clock. After one night of sleep the brain is back where it started. One fact causes most of the failures. Nightly forgetting takes a fixed 0.14 off every learned weight, and no lesson he forms in a day is bigger than that.

## How it was measured

- **The lever.** `tools/e2e_learning.sh` runs `test/test_learning` (env `e2e`) in WSL `survivor`. Scenarios are data in `test/support/e2e/scenarios.h`. The runner `test/support/e2e/run.h` drives the real `Dish` through the board's seams. It feeds BodySamples at 50 Hz built from the sim's `SimHand` waveforms, calls `Dish::tick` on a board-style millis clock, reads wall time from a battery RTC, and handles an unplug by rebuilding the Dish from storage. It records only what the phone's `STATE` reply and the screen show.
- **Pairing.** Both arms share the seed, the care schedule and every jittered row. `Harness.ArmsDifferOnlyByTheStimulus` checks that the arms end bit-identical when there is no Treatment row.
- **Effect.** For each seed the effect is treatment minus control. The report gives the mean, a 95% bootstrap CI (10,000 resamples) and the share of seeds that moved in the predicted direction. **Yes** means the CI excludes 0 on the predicted side and at least 75% of seeds moved that way. **Weak** means the CI excludes 0 but fewer seeds moved that way. **No** means the CI includes 0. **Opposite** means the CI excludes 0 on the wrong side. These rules were fixed before any scenario ran.
- **Care, both arms.** He gets a pellet at 11:00, 13:30, 16:00, 18:30 and 21:00, the hours he is awake. A first run fed him from 07:00 every 3 h. He died at 34 h, every generation, cause Starved. A pellet dropped while he sleeps (about 23:00 to 10:00) rots in 90 min, he eats it rotten on waking, and pinned overnight hunger adds injury.

## Verdicts

| Scenario | Stimulus | Predicted (before running) | Measured, T vs C, T-C [95% CI], seeds as predicted | Clearly modified? |
|---|---|---|---|---|
| shaking | shaken about every 30 min while awake, days 1-3 | curls more after a probe shake on day 4; the hop does not change | curl rise after a probe shake 0.013 vs 0.009, +0.004 [-0.014, 0.022], 52%. Hops on 100% of probes in both arms. Fear peak 395 vs 395 | **no** |
| shaken_for_chasing | shaken each time he starts to chase the marble, days 1-3 | chases less while it goes on | day-3 chase share 0.0023 vs 0.0645, -0.062 [-0.063, -0.062], 100% | **yes** (strongest) |
| shaken_for_chasing_next_day | same owner, day 4 with no shaking | the lesson lasts the night | day-4 chase share 0.0642 vs 0.0643, -0.0001 [-0.0004, 0.0002], 52% | **no** |
| feeding_hour | fed at 11, 14, 17, 20 every day instead of at random hours | tries to eat before the usual 17:00 meal, reaches the pellet sooner | eat share with no pellet down, 16:30-17:00: 7.6 vs 7.6 per mille, +0.02 [-0.25, 0.28], 48%. Bite latency 146 vs 151 s, -5 [-73, 62] | **no** |
| marble_play | marble rolled at him for 2 min after he wakes bored, days 1-3 | chases a rolled marble more on day 4 | chase share 3 min after the roll 0.065 vs 0.065, -0.001 [-0.009, 0.008], 45% | **no** |
| rotten_pellets | the pellet waiting at waking is rotten instead of fresh, days 1-3 | waits longer before biting rot on day 5, takes fresh food as fast as the control on day 4 | minutes awake before biting rot 3.5 vs 3.8, -0.3 [-2.1, 1.5], 48%. Fresh: 3.3 vs 4.5, -1.2 [-3.1, 0.4] | **no** |
| comfort_holding | held in a palm for 30 s four times a day, days 1-3 | settles (rests) more when held on day 4 | rest-share rise while held -0.001 vs 0.002, -0.003 [-0.022, 0.016], 45%. need_touch before the hold 295 vs 299 (chemistry, every seed) | **no** |
| face_down_nights | face down 19:00-09:00, days 1-4 | falls asleep earlier on day 5 | first evening hour mostly asleep 24 vs 23, +1 [1, 1]. Asleep 19:00-23:00: 0.00 vs 0.05 | **no (opposite)** |
| face_down_evenings | face down 19:00-23:00, days 1-4 | falls asleep earlier on day 5 | first evening hour mostly asleep 21 vs 23, -2 [-2, -2], 100%. Asleep 19:00-23:00: 0.40 vs 0.05 | **yes** (body clock, not the brain) |
| neglect | no pellet at 11:00, 13:30, 16:00 on day 1 | goes for a dropped pellet sooner on days 2-3 | seconds to bite 104 vs 155, -50 [-68, -32], 82%. Asleep 00:00-07:00 the next night: 0% vs 100% (every seed). Injury +76 per mille | **yes**, by a side route (see below) |
| heirloom | parent shaken as in `shaking`, then 9 days unplugged (dies of old age in the gap), egg picked and hatched | the shaken parent's hatchling curls more after a shake | curl rise over 4 probes 0.027 vs -0.002, +0.029 [-0.011, 0.069], 60%. First probe +0.005 [-0.059, 0.072], 40% | **no** |

Each "no" scenario is a `knownFailing` test. It is skipped with these numbers. When the effect appears, the test fails with "clear knownFailing", so the mark has to be removed on purpose.

## Strongest and weakest

- **Strongest.** In shaken_for_chasing, punishment works immediately. Chase share fell from 6.4% of waking time to 0.2% on day 1 itself, on all 40 seeds. The shake creates the fear it then teaches him to avoid, so it does not need a drive that is already high. The survey found the same (96% drop).
- **Weakest.** In shaken_for_chasing_next_day, the same lesson is gone the next morning: 6.42% vs 6.43%. Heirloom, feeding_hour and marble_play are flat as well, with no seed-level trend.

## Root causes, traced in the code

1. **Nightly forgetting takes a fixed 0.14 off every weight, and it clears every lesson.** This one explains shaking, shaken_for_chasing_next_day, comfort_holding, rotten_pellets, feeding_hour and heirloom. `forget` rounds each decrement up (`lib/blorb/src/brain.cpp:94`). The rate is `forgetRate 16/255 × 1/2048` (`brain.cpp:16`, `starter_genome.cpp:246`), about 514 raw out of 2^24. So `ceil(|w| × 514 / 2^24)` is exactly 1 Q15 LSB (3.05e-5) for every weight below 0.996. He dreams every 8 s (`creature.cpp:186-191`, dreamEvery 8), about 4,500 times in a 10-hour night. Forgetting is therefore linear at about 0.137 per night per weight, not the proportional 13% the learning-depth survey estimated. Measured in `Diagnose.Forgetting`:
   - food_near→eat→hunger went from -0.404 to -0.250 over night 1. That is 5,066 dreams × 3.05e-5 = 0.155, and 0.155 was measured.
   - The curl-after-shake instinct (-0.21) was gone by 11:00 on day 1.
   - By day 4 every cradled→*→need_touch and food_near→eat→* weight read exactly 0 in both arms (`Diagnose.CradleAndRot`).

   The proposed fix is to scale forgetting with |w| without the round-up, or to forget in Fx before converting to Q15. It is not made here, by instruction.
2. **A stimulus is visible to the brain for about half a second.** This affects shaking and heirloom. Recent loci halve every tick (`creature.cpp:236`), and the brain reads features only when it starts an action (`brain.cpp:138`). Of 66 shakes per treatment run, 7 to 14 landed within 0.5 s of an action start (`Diagnose.Shaking`). At day 4, W[recent_shake][*][fear] was 0.000 for every action in both arms.
3. **The hop and the fear are chemistry, with no memory.** In shaking, he hops on 100% of probe shakes and fear peaks at 395 per mille in both arms. The shake gene (`starter_genome.cpp:186`) and the startle receptor (`:175`) respond the same way every time. Nothing in the genome habituates or sensitises them.
4. **Boredom is pinned at zero, so play cannot reward him.** In marble_play, waking boredom averages 2 per mille (`Harness.BaselineDay`). Foresee relieves boredom by 0.2 every time (`starter_genome.cpp:199`), and a drive at zero cannot fall, so marble_hit's -0.3 (`:195`) teaches nothing.
5. **Anticipating a meal is unreachable.** In feeding_hour, Eat with no pellet down ends at once with no outcome (`actions.cpp:143`). So "try to eat at 17:00" is never paid, and the only time features are the day sin/cos pair (`loci.def:16-17`), which is erased nightly by cause 1.
6. **He cannot see rot.** In rotten_pellets, `food_rotten` is not a brain feature (`loci.def:24`, situation 0), and neither is `fed_bad` (`stimuli.def:26`). A rotten bite also fires `fed` (`actions.cpp:151-152`), so it relieves his hunger. He wakes with hunger at 990 per mille (`Diagnose.RottenMorning`), and all of his eat weights are 0 at waking anyway (cause 1).
7. **Face-down nights shift his clock the wrong way.** Entrainment has a budget of one hour a pet day, reset at pet midnight (`senses.cpp:132`). A lid that runs past pet dawn spends the budget first, and morning dark delays the phase (`senses.cpp:133-134`, "stands still in the morning"). So a 19:00-09:00 lid moves his day 120 min later by day 4 (`Diagnose.NeglectAndClock`). A 19:00-23:00 lid moves it 84 min earlier. That half is the yes in face_down_evenings. Delaying the clock is arguably right for morning darkness. The owner's intent ("bed early") gets the opposite effect.
8. **No heirloom passes on.** At death both arms capture the same beliefs: light→sleep→pain/hunger and food_near→eat→hunger, with |effect| 0.05 to 0.10 (`Diagnose.Heirlooms`, `clutch.cpp:50`). `mutate.cpp:275` keeps only beliefs with confidence ≥ heirloomConf 0.549. Confidence is |effect| / 0.25 (`brain.cpp:161-176`), so the cut-off is |effect| ≥ 0.137, which is the same size as one night's forgetting. Zero heirlooms were inherited in either arm, so the hatchlings start equal. The measured +0.029 is within noise.
9. **Neglect is a yes, but not because he learned to want food.** The neglected grungo never slept the next night: 0% of 00:00-07:00 asleep against 100% for the control, on every seed. With no sleep there were no dreams, so there was no forgetting. W[food_near][eat][hunger] stayed at -0.27 overnight against -0.08 for the control (`Diagnose.NeglectAndClock`). So he bites 50 s sooner. Why he stayed awake is inferred, not traced. Hunger pinned at 1 most likely keeps the brain choosing Eat (which ends at once with no pellet) over Sleep.

## Contact sheets and videos

Every scenario has a contact sheet and an MP4. The sheet shows control above treatment at eight matched moments. The MP4 (H.264, yuv420p, encoded by imageio-ffmpeg's ffmpeg 7.0.2) shows control left and treatment right, captioned with the measured effect. All frames come from one representative seed, the seed whose difference is nearest the mean. They are drawn with `paint::draw` and the grungo pack, the call `src/main.cpp`'s loop makes.

- `scratchpad/e2e-sheets/<scenario>.png`
- `scratchpad/e2e-videos/<scenario>.mp4`
- `scratchpad/e2e/json/<scenario>.json` holds per-seed values for every metric.

## Rerun

```
MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/e2e_learning.sh            # 40 seeds, about 13 min
wsl -d survivor --exec env E2E_OUT=~/e2e E2E_SEEDS=20 bash tools/e2e_learning.sh test_learning -a --gtest_filter='Scenarios/*'
```
