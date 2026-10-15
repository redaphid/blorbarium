# The first hours of a fresh pet, with no phone and no wall time

Branch `rel-play` (from `release-0.7` at a1b7cca). The owner, 2026-10-15: "I'll be testing one on the plane. I want the gameplay to be interesting during that time". The board runs on a phone's power with no internet and no battery, so after a replug the clock is unknown. The owner sits and handles the board gently. The kids also play in cars.

## How it was measured

`test/test_first_hours` drives the real `Dish` the way `src/main.cpp` does: BodySamples at 50 Hz, `askForTime` off, a NullLink (no phone) and an RTC that never knows the time. Each pet is founded from `starterGenomeOf(species, seed)`, which is what a pinned or chosen gift device hatches. There are two owners:

- **nobody**: the board lies still on a tray table for six hours.
- **seated**: every 3 to 10 minutes the owner does one gentle thing. That is a tilt of 9 to 16 degrees for 4 to 10 s (it rolls the marble but stays under "held"), a pick-up for 5 to 20 s, 1 to 3 single taps, or a BOOT press. The owner presses BOOT when the rim hint says feed, and about one glance in ten anyway. The owner holds the egg now and then and mostly lets him sleep. There are no shakes, drops or face-down tucks.

Every run logs what the screen shows: each line said, face changes (asleep is the asleep face, as `paint::faceFor` draws it), hops, sleep and wake with sleepiness and the gate, bites, and the drives every 10 minutes.

```
# the sweep: 9 species x 4 seeds x 2 owners x 6 h, about 90 s; timelines to OUT
MSYS_NO_PATHCONV=1 wsl -d survivor --exec env FIRST_HOURS_SWEEP=1 FIRST_HOURS_SEEDS=4 \
  FIRST_HOURS_OUT=/mnt/c/somewhere bash tools/wsl_test.sh test_first_hours
# FIRST_HOURS_HOURS=30 for longer runs; FIRST_HOURS_FOUNDING_HOUR=0 for the old clock;
# FIRST_HOURS_GIFT=1 founds starterGenome() (the Gift recipe) instead
```

"Before" is `release-0.7` with only the driver added. "After" is this branch. Both runs used the same driver, seeds 0x5EED0000 + 7919k for k = 0..3.

## What was flat and what was harsh

1. **The first seven hours of every new life were night.** A fresh founding starts its pet clock at `petTicks` 0, which is pet midnight, and pet night is 21:00 to 07:00. A phone's TIME would snap the clock to the wall, but this board never gets one. Every species makes melatonin in the dark. "Nocturnal" only slows it (a 1 h fill against 15 min). Day species hatched at 00:30, fell asleep 44 min later and slept 4.8 of the 6 hours. Asleep, they said "ZZZ... LUNCH... ZZZ" every 15 minutes and nothing else. Night-sleepers stayed up for 2.2 h, then slept 3.3 h.
2. **Moved to a morning, he was awake but flat.** Founded at 09:00 (clock fix alone, old genome), every species stayed awake all six hours. Left alone he wore one face for six hours: "happy" scored 0.444 against neutral's 0.349 + 0.1 hysteresis, short by 0.005. No drive passed the care hint's 0.5. Hunger settled at its 0.15 tonic, because a hatchling starts with 0.8 energy and energy halves only every 4 h. So no feed hint, no "SNACK?" and no reason to press BOOT. He never napped, because daylight sleepiness sits at its 0.1 tonic, under the 0.35 gate. He said 17 lines an hour but only 8 distinct ones. The top line ran 29 times on average and 63 times for mudge. That line was "BRRRUP. I SEE... DAMP." (row 24): it is priority 8, an urgent interjection that skips the quiet gap, with a 300 s cooldown, and he foresees all the time, because foreseeing relieves the boredom the boredom tonic keeps making. Mossby the snail says it too.
3. **Naps need a gap between falling asleep and waking.** Sleep runs while `sleep_gate` is at least 0.5, which is sleepiness at least 0.35. The brain picks actions nearly at random when drives are low, every 2 to 4 s, so any sleepiness hovering at the gate gave 120 to 280 "naps" of under 5 minutes in six hours.
4. **Harsh, but outside the first six hours and on the old firmware too.** At 25 to 30 h, a seated owner's random BOOT presses while he sleeps leave pellets that rot (90 min). He wakes starving and eats three rotten ones in two minutes, and the toxin kills him: seated mossby and tinker died of injury at hour 28 to 30 on both builds. This was not changed (see "Not changed").

## What changed

| Where | What | Old -> new | Why |
|---|---|---|---|
| `DishOptions::foundingHour`, `kUnaskedFoundingHour` (dish.h, dish.cpp), set in main.cpp beside `askForTime = false` | the pet hour a fresh founding's clock starts at | 0 -> 9 on a board that never asks for the time; 0 elsewhere | Hatches him into a pet day with 12 h before pet night. It is saved in the snapshot's `phaseOffsetTicks`, so a replug resumes it and never re-applies it. A phone's TIME still snaps it to the wall. The sim and tests keep 0 (the sim still asks for the time), so no sim golden moved. |
| `kSleepOnset` (creature.cpp) | a sleep the brain chooses starts only with `sleep_gate` at least this | none -> 0.75 (sleepiness about 0.475). It still runs until the gate is under 0.5 (sleepiness 0.35). | The gap between them is the nap. Forced sleeps (tests, sim scripts) are exempt. It applies to every pet, including the Gift genome; see "Effect on pets alive today". |
| `authorStarter(..., Recipe)` (genes.h, starter_genome.cpp) | the recipe has two sets of numbers | new `Recipe::Gift` and `Recipe::FirstHours` | `starterGenome()` is pinned byte for byte to the gift device's grungo d30f00d1 (test_species `Gift.*`, test_founding), and it is what an unpicked nest founds. Gift keeps those bytes. `starterGenomeOf(species)` gets FirstHours. Same genes, same uids, so a friend egg's cross still pairs them. |
| FirstHours: sleepiness tonic | emitter locus, rate, half-life, start | `always`, 0.10 equilibrium, 2 h half-life, starts 0 -> `asleep` inverted (made only while awake), 0.52 equilibrium (0.58 for a species that sleeps by day), 90 min half-life, starts 0.25 | Sleepiness becomes pressure that builds while he is awake and drains while he sleeps, so a pet day has naps. Day species nap about 40 min after every 2.3 h awake, and night-sleepers after every 1.8 h (30 h runs). That is species character from the existing `nocturnal` column ("sleeps by day"). |
| FirstHours: energy at hatching | `chem(energy, 4 h, initial)` | 0.80 -> 0.45 | Hunger reaches the feed hint (0.5) about 3 h after hatching instead of after 6 h, so the feed hint, a BOOT press, the bite and "BRRRUP. CRUNCH." all fall inside a flight. Left alone he is starving (0.8 or more) from about 4.5 h, with injury 0.05 by 6 h, which one BOOT press fixes. |
| FirstHours: happy face | `face(happy, weight, ...)` | 0.55 -> 0.60 | A content pet now clears the 0.1 hysteresis over neutral and shows happy, then blep as hunger rises. |
| FirstHours: egg | `EggGene.incubateMinutes` | 30 -> 15 | A fresh egg hatches in 15 minutes, 10 if held. The founding nest's 60 s timeout comes first. |
| thoughts.def row 24 `see_damp` | cooldown | 300 s -> 900 s | It was a quarter of everything he said. Now it is at most 4 an hour. The thoughts agent was told, and no text was touched. |

Tests changed on purpose:
- `Starter.CarriesEveryGeneKindAndExpressesEachAtSomeStage`: the egg is 15 minutes, not 30.
- `Gift.GrungosStarterIsTheOldStarterPlusOnlyTheSpeciesGenes` became `...RetunedPlusTheSpeciesGenes`. The Gift recipe is still `starterGenome()` byte for byte, with the same uids, types and lengths, and exactly 5 genes retuned.
- `Replay.TwentyFourHoursOfTheSameRoutineGiveTheCommittedHash`: dca5e44a -> 8a7f68fc, from `kSleepOnset`. The device computes it with the same code.

New guards are in `test_first_hours`:
- `ANoPhoneBoardFoundsIntoMorningAndAReplugKeepsIt`
- `AFreshPetPlaysThroughHisFirstSixHours`: grungo, mossby, mudge and hushwing, both owners. It checks no death, a hatch within 16 min, at least 10 lines an hour, an awake gap of at most 20 min, at least 7 distinct lines, no line more than a third of all lines, at least 1 nap, at most 2.5 h asleep, no sleep over 60 min, at least 3 face changes, injury under 0.1, and for seated owners at least 2 bites and no starving. On the old clock (`FIRST_HOURS_FOUNDING_HOUR=0`) it fails at 4.8 to 5.3 h asleep.

## Before -> after, per species and owner (mean of 4 seeds; gaps, sleeps, hunger, starving and injury are the worst seed)

"visible" counts lines, face changes, hops, sleep and wake edges, and bites. "longest awake gap" is the longest stretch awake with no line. "Before" is low there only because he was awake for under an hour.

| species | owner | hatch min | visible/h | lines/h | distinct lines | top line x | longest awake gap min | h asleep | naps | longest sleep min | first sleep after hatch min | face changes | bites | hunger max | starving min | injury max | deaths |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| grungo | nobody | 30 → 15 | 11.8 → 16.1 | 10.5 → 14.2 | 13 → 20 | 10 → 9 | 6 → 15 | 3.3 → 1.3 | 1.8 → 2.0 | 197 → 40 | 132 → 154 | 2 → 5 | 0.0 → 0.0 | 0.30 → 1.00 | 0 → 91 | 0.00 → 0.06 | 0 → 0 |
| grungo | seated | 29 → 15 | 16.6 → 23.3 | 12.3 → 13.1 | 21 → 23 | 10 → 8 | 6 → 14 | 3.3 → 1.3 | 1.2 → 2.5 | 199 → 41 | 133 → 67 | 13 → 34 | 2.0 → 3.5 | 0.30 → 0.39 | 0 → 0 | 0.00 → 0.00 | 0 → 0 |
| mossby | nobody | 30 → 15 | 6.9 → 15.8 | 6.2 → 14.6 | 11 → 19 | 8 → 10 | 6 → 15 | 4.8 → 0.7 | 1.0 → 1.0 | 286 → 40 | 45 → 192 | 1 → 3 | 0.0 → 0.0 | 0.30 → 1.00 | 0 → 84 | 0.00 → 0.05 | 0 → 0 |
| mossby | seated | 29 → 15 | 9.5 → 27.2 | 7.4 → 14.8 | 16 → 24 | 9 → 10 | 6 → 14 | 4.8 → 0.8 | 1.0 → 1.5 | 287 → 40 | 44 → 161 | 6 → 42 | 0.2 → 4.5 | 0.30 → 0.30 | 0 → 0 | 0.00 → 0.00 | 0 → 0 |
| puffkin | nobody | 30 → 15 | 6.9 → 16.2 | 6.2 → 15.0 | 12 → 20 | 10 → 10 | 6 → 14 | 4.8 → 0.7 | 1.0 → 1.0 | 286 → 40 | 44 → 192 | 1 → 3 | 0.0 → 0.0 | 0.30 → 1.00 | 0 → 95 | 0.00 → 0.06 | 0 → 0 |
| puffkin | seated | 29 → 15 | 9.5 → 24.5 | 7.3 → 14.1 | 16 → 22 | 8 → 10 | 5 → 15 | 4.8 → 1.0 | 1.2 → 2.0 | 287 → 40 | 40 → 137 | 6 → 34 | 0.2 → 4.0 | 0.30 → 0.30 | 0 → 0 | 0.00 → 0.00 | 0 → 0 |
| gilly | nobody | 30 → 15 | 6.9 → 16.1 | 6.2 → 14.9 | 12 → 20 | 10 → 9 | 6 → 15 | 4.8 → 0.7 | 1.0 → 1.0 | 286 → 40 | 44 → 192 | 1 → 3 | 0.0 → 0.0 | 0.30 → 1.00 | 0 → 91 | 0.00 → 0.06 | 0 → 0 |
| gilly | seated | 29 → 15 | 9.5 → 27.9 | 7.4 → 16.1 | 15 → 25 | 9 → 10 | 5 → 14 | 4.8 → 0.7 | 1.0 → 1.0 | 288 → 40 | 44 → 192 | 6 → 40 | 0.2 → 5.2 | 0.30 → 0.30 | 0 → 0 | 0.00 → 0.00 | 0 → 0 |
| mothwick | nobody | 30 → 15 | 11.6 → 16.1 | 10.5 → 14.2 | 14 → 20 | 11 → 9 | 7 → 15 | 3.3 → 1.4 | 1.5 → 2.0 | 197 → 40 | 132 → 155 | 2 → 5 | 0.0 → 0.0 | 0.30 → 1.00 | 0 → 87 | 0.00 → 0.05 | 0 → 0 |
| mothwick | seated | 29 → 15 | 16.8 → 24.0 | 12.2 → 13.7 | 21 → 24 | 10 → 8 | 6 → 14 | 3.3 → 1.2 | 1.5 → 2.2 | 199 → 40 | 132 → 154 | 14 → 33 | 2.0 → 5.2 | 0.30 → 0.30 | 0 → 0 | 0.00 → 0.00 | 0 → 0 |
| mudge | nobody | 30 → 15 | 6.9 → 16.6 | 6.2 → 15.4 | 5 → 7 | 19 → 21 | 6 → 14 | 4.8 → 0.7 | 1.0 → 1.0 | 286 → 40 | 44 → 192 | 1 → 3 | 0.0 → 0.0 | 0.30 → 1.00 | 0 → 102 | 0.00 → 0.06 | 0 → 0 |
| mudge | seated | 29 → 15 | 9.5 → 27.0 | 7.4 → 16.1 | 8 → 10 | 20 → 22 | 6 → 14 | 4.8 → 0.7 | 1.0 → 1.5 | 287 → 40 | 44 → 145 | 6 → 36 | 0.2 → 5.2 | 0.30 → 0.30 | 0 → 0 | 0.00 → 0.00 | 0 → 0 |
| fizz | nobody | 30 → 15 | 6.9 → 16.1 | 6.2 → 14.9 | 11 → 18 | 10 → 10 | 5 → 15 | 4.8 → 0.7 | 1.0 → 1.0 | 286 → 40 | 44 → 192 | 1 → 3 | 0.0 → 0.0 | 0.30 → 1.00 | 0 → 97 | 0.00 → 0.06 | 0 → 0 |
| fizz | seated | 29 → 15 | 9.8 → 26.7 | 7.4 → 15.8 | 15 → 25 | 9 → 10 | 5 → 15 | 4.8 → 0.7 | 1.5 → 1.2 | 287 → 40 | 33 → 191 | 6 → 36 | 0.2 → 5.0 | 0.30 → 0.30 | 0 → 0 | 0.00 → 0.00 | 0 → 0 |
| hushwing | nobody | 30 → 15 | 11.9 → 16.0 | 10.5 → 14.1 | 13 → 18 | 10 → 10 | 6 → 15 | 3.3 → 1.3 | 2.0 → 2.0 | 197 → 40 | 132 → 154 | 3 → 5 | 0.0 → 0.0 | 0.30 → 1.00 | 0 → 87 | 0.00 → 0.05 | 0 → 0 |
| hushwing | seated | 29 → 15 | 17.1 → 23.7 | 12.5 → 13.4 | 20 → 23 | 12 → 11 | 6 → 14 | 3.3 → 1.3 | 1.8 → 2.8 | 199 → 41 | 122 → 127 | 14 → 33 | 2.0 → 4.8 | 0.30 → 0.52 | 0 → 0 | 0.00 → 0.00 | 0 → 0 |
| tinker | nobody | 30 → 15 | 11.6 → 16.2 | 10.5 → 14.4 | 14 → 18 | 12 → 9 | 6 → 16 | 3.3 → 1.3 | 1.5 → 2.0 | 197 → 40 | 132 → 154 | 2 → 5 | 0.0 → 0.0 | 0.30 → 1.00 | 0 → 91 | 0.00 → 0.06 | 0 → 0 |
| tinker | seated | 29 → 15 | 17.1 → 22.8 | 12.4 → 13.7 | 21 → 24 | 10 → 8 | 6 → 15 | 3.3 → 1.3 | 1.8 → 2.0 | 199 → 40 | 108 → 154 | 14 → 30 | 2.0 → 4.8 | 0.30 → 0.30 | 0 → 0 | 0.00 → 0.00 | 0 → 0 |

The clock fix alone, before the genome and line changes (same 72 runs, old genome founded at 09:00):
- nobody: 17.4 lines/h, 8 distinct, top line 29 times (63 for mudge), 0.3 face changes, 0 h asleep, hunger never above 0.30.
- seated: 23.6 lines/h, 21 distinct, top line 30 times (64 for mudge), 0 h asleep.

The genome changes and the see_damp cooldown took the top line from 29 to 9-10 (21 for mudge), more than doubled the distinct lines a lone pet says, and added naps, hunger and faces. They cost thought rate (17.4 to about 14.5 an hour when alone) and lengthened the longest awake silence from 7 to 15 minutes.

Over 30 h (one seed, all species), nothing died after the change. Day species sleep 21:05 to 09:58 with 3 day naps of about 40 min. Night-sleepers sleep 21:17 to 08:05 with 4 naps. The seated owner's rotten pellets still push injury up to 0.85 by hour 30, against 0.67 to 0.90 before.

## Effect on pets alive today

A pet already on a board keeps its genome and its clock: the founding hour and the FirstHours numbers reach only fresh foundings, so a board still in the pet-night hours of an old founding stays there until its pet morning. Two changes do reach existing pets:
- `kSleepOnset`: on the Gift genome over 30 h, bedtime moves about 65 min later (night 1 02:43 -> 03:48, night 2 22:49 -> 23:55) and wake time is unchanged (09:47). The zero-length doze-and-wake flickers go too.
- The see_damp cooldown.

## Not changed, and why

- **No foraging or self-feeding.** With nobody there, food never appears, and he is hungry by the time someone looks again. One BOOT press fixes it (the pantry is full). A dish that feeds itself would make neglect free, and the owner decided neglect matters (genes.h `kNeglectCanKill`).
- **Rotten pellets killing at 25 to 30 h** is the same on the old firmware and outside the first hours. Rot is designed (e2e-learning.md). If kids pressing BOOT at night in the car turns out to kill pets, the cheap lever is the starter's `rotMinutes` (90) or `fed_bad`'s toxin (0.4).
- **The awake gap of up to 15 minutes** is set by the thoughts table: with all drives low only no_hat, marble_knows, science and the drive rows can fire. The thoughts agent's offline lines are the fix. The terse voice (mudge) has 7 lines, and its top line is about a quarter of what it says.
- **What "nocturnal" means** (the species.def column says "sleeps by day", but nights are still melatonin-driven for every species) belongs to the species work. FirstHours only uses the column to raise day sleep pressure.
- senses.cpp and thoughts.cpp were not touched.

## Evidence

- Native suite: before (release-0.7 + driver) 727 cases, 726 passed, 1 skipped. After: see the commit message.
- Sim film goldens: 22 of 22 frames match, nothing re-blessed (the sim founds `starterGenome()`, the Gift recipe, and asks for the time).
