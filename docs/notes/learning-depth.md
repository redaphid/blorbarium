# What Grungo can learn, and how deep it goes

Read-only investigation of `origin/engine` at `36ceffd`. Code read from a scratch clone (`scratchpad/learn-clone`). Behaviour measured by running the engine natively in WSL `survivor` with a body-only owner driver (`scratchpad/learnsim/life.cpp`, method at the end). Paths below are relative to `lib/blorb/`.

## Verdict

**Not deep today. In practice it learns almost nothing that lasts.** The table has 1,760 weights (20 situations, 11 actions, 8 drives), and the design reads well, but four things flatten it in real play:

1. **Rewards can't land.** Reward is the fall of a pressing drive. Boredom sits below 0.02 for **100%** of waking time under every owner style where I measured it (doting, quiet, trainer, cradle-trainer), because Foresee relieves boredom by 0.2 every time and he picks it about 2,000 times a day. An attentive owner pins loneliness and need-touch to zero the same way. A drive at zero cannot fall, so play, the marble, knocks and cuddles teach nothing.
2. **Instincts erode and nothing replaces them.** Over a natural life the birth instinct "food near, eat, hunger falls" goes from -0.56 to -0.02. Whole-table size falls to 5% to 30% of the newborn's. Three full simulated lives (rich, quiet and rough owners, about 8.5 pet days each, old age) **passed on zero heirlooms**. None of their beliefs came near the 0.137 effect the heirloom filter needs.
3. **Situations are mostly blind.** A stimulus is visible to the brain for about 0.3 s (`creature.cpp:236` halves recent loci every tick). Decisions come every 2 to 4 s, and no stimulus forces one while he's awake. So "just knocked", "just shaken" or "just fed" is rarely on when he decides.
4. **Choice is mostly noise and rotation.** Exploration noise reaches 0.167 and habituation adds 0.108 per pick (`brain.cpp:18-22`, `starter_genome.cpp:246`). Learned scores are a few hundredths. Across every run, switch-ins are spread almost evenly over the 11 actions (about 7,500 each in 4 days), with Sleep and punished actions the only outliers.

**One thing works well, and it's punishment.** An owner who shakes him whenever he starts chasing the marble cut Chase from about 7,400 picks to 263 and 311 (two seeds), a 96% drop. The chase row learned fear +0.42 and discomfort +0.34. Punishment works because the shake creates the drive it then lowers, so it never runs into the pinned-at-zero problem.

**Time, sequences, the owner's habits.** He can't anticipate meals. He sleeps from about 21:00 to 09:30, so the 08:00 meal always lands while he's asleep, and Eat picks are flat across the waking hours. He has no "A then B", because credit goes only to the features at the instant of a decision and the one action that followed. He has no model of the owner (`owner_near` needs a phone). Day phase is one sine/cosine pair plus `light`, so a time-of-day preference is representable but tiny in practice.

**Lineage personalities don't emerge from learning.** Learned changes look the same whatever the owner did (cosine 0.86 to 1.00 across 13 runs of 6 owner styles), because they're mostly the same seven instinct cells eroding. The only owner who produced a distinct, reproducible brain was the punisher (cosine 0.95 between seeds, about 0.3 to everyone else). Today, lineages diverge through mutation, not through what they learned.

## The registries

### Situations: the brain's 20 features

Built at `registry.h:157-173`: every locus with `situation = 1`, then the recent locus of every stimulus with `situation = 1`. Values are 0 to 1.

| # | Feature | Source | Reachable body-only? |
|---|---|---|---|
| 1 | `motion` | IMU deviation from 1 g, smoothed (`senses.cpp:184-188`) | yes |
| 2 | `held` | tilted more than 300 mg for 0.4 s (`senses.cpp:226-229`) | yes. A dish tilted more than about 17° counts as held |
| 3 | `upside_down` | face down past -600 mg (`senses.cpp:176-181`) | yes |
| 4 | `light` | 0 at pet night or while lidded (`senses.cpp:284-286`) | yes |
| 5 | `day_sin` | pet day phase (`senses.cpp:287-289`) | yes |
| 6 | `day_cos` | pet day phase | yes |
| 7 | `owner_near` | a phone is connected (`senses.cpp:295-299`) | **no (phone)** |
| 8 | `asleep` | `creature.cpp:109` | **dead.** The brain thinks only while awake (`creature.cpp:186-201`), so it is always 0 when learning |
| 9 | `age` | 1 - life (`creature.cpp:110`) | yes. Rises slowly, acts as a bias |
| 10 | `food_near` | nearness of the nearest pellet (`habitat.cpp:101`) | yes |
| 11 | `marble_near` | nearness of the marble (`habitat.cpp:103`) | yes. Almost always on, acts as a bias |
| 12 | `cradled` | held still and upright 3 s (`senses.cpp:231-240`) | yes |
| 13 | recent `knock` | tap engine single tap | yes, about 0.3 s per knock |
| 14 | recent `shake` | 4 jolts in 0.9 s | yes, about 0.3 s |
| 15 | recent `picked_up` | held turns on | yes, about 0.3 s |
| 16 | recent `cradle` | every 10 s while cradled | yes, about 0.3 s |
| 17 | recent `fed` | the Eat bite | **near-dead.** It fires mid-Eat and has decayed by the time Eat ends 15 ticks later. Seen 0 to 3 pet-minutes per 4 days |
| 18 | recent `marble_hit` | marble reaches him (`habitat.cpp:85-98`) | near-dead, rare |
| 19 | recent `owner_arrived` | phone connects | **no (phone)** |
| 20 | recent `petted` | phone STIM | **no (phone)** |

The brain senses some things but never sees them as situations. These are `tilt_x`, `tilt_y`, `warmth`, `eating`, **`food_rotten`** (`loci.def:24`, situation 0), `pantry`, `foreseeing` and `touch`, plus the stimuli `double_knock`, `put_down`, `dropped`, `flipped`, `righted`, `button`, `button_hold`, `lid_down`, `lid_up`, `pellet_dropped`, `fed_bad`, `dusk`, `dawn`, `owner_left`, the four `self_*`, `played` and `spoken_to`.

Body-only, that leaves **14 live features, 2 near-dead and 4 dead** (asleep and the three phone ones). That's 154 of 220 situation×action cells with any chance of signal.

### Actions (11), `defs/actions.def`

| Action | minTicks | What it does to drives by itself |
|---|---|---|
| rest | 20 | nothing |
| wander | 30 | nothing, but walking into the marble fires `marble_hit` |
| eat | 20 | walks to the nearest pellet and bites. `fed`: hunger -0.40, boredom -0.05 (`actions.cpp:150-151`, `starter_genome.cpp:193`). A rotten pellet adds `fed_bad`: toxin +0.4, discomfort +0.3 (`:194`). **With no pellet it ends at once** (`actions.cpp:143`) |
| sleep | 300 | runs while `sleep_gate` holds. Hours long |
| foresee | 40 | self-stim `self_foresaw`: boredom -0.20, every time (`actions.def:9`, `creature.cpp:147`, `starter_genome.cpp:199`) |
| call | 25 | self-stim `self_called` has **no stimulus gene**, so nothing happens |
| curl | 40 | walks to the rim. Nothing else |
| follow_tilt | 20 | walks downhill. Tilt also rolls the marble (`habitat.cpp:86-88`), so it can produce `marble_hit` |
| flee_tilt | 20 | walks uphill |
| chase | 15 | walks to the marble and noses it. `marble_hit`: boredom -0.30 (`starter_genome.cpp:195`) |
| hop_circles | 20 | bounces in circles. Nothing else |

### Drives (8), `defs/drives.def`, and their tonics (`starter_genome.cpp:131-139`)

| Drive | Half-life | Tonic level | What moves it body-only |
|---|---|---|---|
| hunger | 30 min | 0.15, plus a low-energy call | fed -0.40 |
| sleepiness | 2 h | 0.10, plus melatonin in the dark | button_hold +0.20, lid_down via melatonin |
| boredom | 1 h | 0.60 | knock -0.15, double_knock -0.25, marble_hit -0.30, self_foresaw -0.20, fed -0.05 |
| loneliness | 3 h | 0.50 | double_knock -0.10, cradle -0.20 |
| fear | 2 min | 0.05 | shake +0.30, flipped +0.15, knock +0.05, picked_up +0.05, cradle -0.20 |
| pain | 10 min | 0.02 | dropped +0.30, any injury ≥ 0.1 (`:153-154`) |
| discomfort | 5 min | 0.05 | shake +0.25, flipped +0.30, fed_bad +0.30 |
| need_touch | 3 h | 0.50 | picked_up -0.10, cradle -0.35 |

### Reflexes (2), `defs/reflexes.def`

`hop` (adrenaline to startle) and `flinch` (a knock's jolt). These are chemistry, not learning, and they override the action for 6 to 12 ticks.

### Starter instincts (9), `starter_genome.cpp:207-217`

| Cue | Action | Drive | Level | Stage |
|---|---|---|---|---|
| food_near | eat | hunger | -0.70 | baby |
| day_sin + day_cos | sleep | sleepiness | -0.70 | baby |
| recent shake + recent knock | curl | fear | -0.60 | baby |
| cradled | rest | need_touch | -0.50 | baby |
| day_sin | foresee | boredom | -0.45 | baby |
| marble_near | chase | boredom | -0.60 | child |
| owner_near | call | loneliness | -0.50 | adult (phone-only cue) |
| recent fed | hop_circles | boredom | -0.40 | adult (near-dead cue) |
| recent shake | foresee | fear | -0.50 | adult, gated on the `fifth_generation` feat |

## How learning works, exactly

**The model.** `W[f][a][d]` predicts how much drive `d` changes when action `a` runs while feature `f` is on (`brain.h:4-13`).

**Decide** (`brain.cpp:124-133`). `score[a] = -Σ_d drive[d] · Σ_f feat[f]·W[f][a][d] - habit[a] + uniform(0, 0.25·(explore + arousal))`, and the argmax wins. A predicted fall of a high drive scores well.

**Cadence.** He thinks at 5 Hz, but an action is held for its `minTicks` unless it finishes (`brain.cpp:108-110`). In practice he decides every 2 to 4 s. Sleep can last hours. A stimulus never interrupts a waking action. Only stimuli with the wake flag end a sleep (`creature.cpp:159-164`).

**Reward signal.** At each decision, `observed[d] = drives_now - drives_at_last_decision` (`brain.cpp:114`). It's the raw per-drive change over the previous action's run, not a scalar. Drives are chemicals clamped to 0..1, so a drive already at 0 can't produce a relief signal.

**Credit assignment** (`brain.cpp:57-69,115`). This is normalised LMS. Only the features as they were when the previous action **started** (`startFeatures_`), and only that one action, get credit. Each active feature moves in proportion to its own value divided by the squared norm of all features. The always-on features `light`, `day_sin`, `day_cos`, `age` and `marble_near` sit in nearly every update and absorb most of it. A 0.5 recent-knock beside them gets roughly 10%.

**"Eligibility trace."** It isn't one in the RL sense. It's a single scalar, `trace = 0.5^(heldTicks / traceHalfLife)` (`brain.cpp:105`), that discounts learning by how long the action ran. The starter's byte 20 is about 30 ticks, 3 s (`genes.cpp:202-208`). A 2 s action learns at 0.63 strength. A sleep learns at about 0 while awake. Earlier actions never get credit, so nothing spans two actions.

**Rates** (starter temperament `{140,16,170,110,8,3,20}`, `starter_genome.cpp:246`). Learn rate 0.55 × 0.5 = 0.27 per update. Explore 0.67. Habituation 0.43 × 0.25 = 0.108 per pick, fading 1/16 per think. The elder gene at `:247` slows learning and speeds forgetting.

**Dreams** (`brain.cpp:145-157`, `creature.cpp:186-194`). Asleep, he dreams once every 80 ticks (8 s). Each dream either applies one queued instinct or replays one of the **last 16 episodes** at full rate, with no trace discount. That's how a sleep episode eventually gets learned. Then every weight decays by `forgetRate/2048`, which is 3.1e-5 per dream. A 10-hour night is about 4,500 dreams, so about 13% of every weight fades each night. The 16 replayed episodes are whatever happened in the last minute or two before sleep, each replayed about 280 times.

**Instincts** (`genes.cpp:161-166`, `creature.cpp:87-92`, `brain.cpp:72-84`). An instinct gene is a cue set, an action, a drive, a level and a strength. It's queued once when its stage expresses, and applied as one LMS nudge toward the level, during the hatch burst (12 dreams, `EggGene{30,128,12}`) or the next sleep. It is never re-applied, so experience and forgetting wear it down.

**Heirlooms** (`clutch.cpp:50`, `mutate.cpp:265-302`). At death, `strongestBeliefs(heirloomMax)` ranks every non-zero weight by `|effect| × min(1, |effect|/0.25)`, which is effect² (`brain.cpp:161-176`). It keeps the top `heirloomMax = 3` with confidence ≥ `heirloomConf = 140/255 = 0.549`, which means **|effect| ≥ 0.137** (`MutationPolicyGene{…,3,140}`, `starter_genome.cpp:248`). Each survivor becomes a **single-cue, baby-stage instinct gene** flagged `Heirloom`, with level = effect and strength = confidence. A child already carrying an heirloom with the same cue, action and drive has it overwritten instead of duplicated. The ranking uses the absolute weight, not what was learned, so an unworn birth instinct qualifies as readily as a real lesson.

## What he can learn: 25 concrete associations

Status key:
- **sim** means verified in code and seen in the simulated lives.
- **code** means the mechanism is traceable in code but weak or inert in practice.
- **unreachable** means the table has the slot but nothing produces the signal today.

| # | In plain words | Status | Evidence |
|---|---|---|---|
| 1 | **Learns that chasing the marble gets him shaken, and stops chasing.** | **sim** | punisher: chase 263 and 311 picks vs about 7,400. Chase→fear +0.42, discomfort +0.34. Shake gene `starter_genome.cpp:186`, credit `brain.cpp:114-115` |
| 2 | Generalising 1: **learns to avoid whatever he was doing when shaken, flipped or dropped.** | **code** | punishment stimuli `starter_genome.cpp:186,190,191`. Any action can be the one blamed |
| 3 | Learns that eating when a pellet is near relieves hunger. It starts as an instinct, experience confirms it, then **it fades over life** from -0.56 to about -0.02. | **sim** | `actions.cpp:133-155`, `starter_genome.cpp:193,207`. 14-day runs |
| 4 | Learns that whatever he does while held or cradled calms him (fear -0.04 on chase, follow_tilt and flee_tilt while held). | **sim** | cradle every 10 s, fear -0.2 (`senses.cpp:235-239`, `starter_genome.cpp:192`) |
| 5 | Superstition: **"calling while held makes the fear go away"** (held→call→fear -0.19 to -0.25). | **sim** | rich gen 6, rough gen 0, first batch |
| 6 | Learns that being picked up mid-call is a little scary (call→fear +0.03 to +0.05). | **sim** | cradle-trainer runs. picked_up fear +0.05 (`starter_genome.cpp:189`) |
| 7 | Learns that a night's sleep ends hungry and hurting (day_cos→sleep→hunger +0.11, pain +0.09). This is a real lesson, from overnight starvation. | **sim** | quiet 4- and 14-day runs. Hunger reaches 0.99 overnight (diag trace) |
| 8 | Superstition: **"sleeping in daylight relieves hunger and pain"** (light→sleep→hunger -0.05, pain -0.03 to -0.06). The owner feeds him and pain fades during daytime naps. | **sim** | quiet and cradle-trainer runs |
| 9 | Heirloom superstitions from punished short lives: "in daylight, curling is frightening" (+0.22), "after a cuddle, calling is frightening" (+0.38), "in daylight, calling is uncomfortable" (+0.29), "near the marble, foreseeing is frightening" (+0.21). | **sim** | first batch, a hyperactive owner who dropped him about 10 times a day. These are what heirlooms look like when they do form |
| 10 | "Rotten food makes him sick." He eats about 4 rotten pellets per 4 days (all runs), but `food_rotten` isn't a situation, so it averages into "eating sometimes hurts" (food_near→eat→discomfort +0.10, gen-3 heirloom). | **unreachable** as a specific lesson | `loci.def:24` situation 0, `starter_genome.cpp:194` |
| 11 | Foreseeing relieves boredom. True every time, but boredom is already 0, so it is worth nothing after the first hour. | **code** | `starter_genome.cpp:199`. Boredom < 0.02 for 100% of waking time |
| 12 | Chasing the marble when it's near relieves boredom. | **code**, inert | `habitat.cpp:85-98`, `starter_genome.cpp:195,212`. Boredom pinned |
| 13 | A knock rewards whatever he was doing (owner as clicker). | **code**, fails | trainer double-knocks every Call. Call picks fell 16% to 36%, call weights stayed 0.000, loneliness pinned 100% |
| 14 | A cuddle rewards whatever he was doing. | **code**, fails | cradle-trainer: need_touch and loneliness pinned 100%, no call gain |
| 15 | After a shake, curling calms him. | instinct only | cue visible for 0.3 s (`creature.cpp:236`). Fear falls after any shake whatever he does. Erodes |
| 16 | "I foresaw the shake" (feat-gated). | instinct only | Foresee does nothing to fear (`starter_genome.cpp:216`). Erodes |
| 17 | Hops in circles after a meal. | **unreachable** | recent `fed` is gone before Eat's next decision (`actions.cpp:133-141`) |
| 18 | Calls the owner when they're near. | **unreachable** body-only | `owner_near` is phone-only (`senses.cpp:295-299`) |
| 19 | Upside down, anything he does calms him (fear and discomfort decay with 2- and 5-minute half-lives). | **code** | `upside_down` is a feature. Seen as upside_down→sleep/foresee weights in batch 1 |
| 20 | Tuck-in (BOOT hold) during X makes X feel worse. Sleepiness +0.2 is blamed on the running action. | **code**, spurious | `starter_genome.cpp:198` |
| 21 | Dropping hurts, and whatever he was doing gets the blame. About 6 drops in a day kill him (injury +0.15 each, death at 0.9). | **sim** | `starter_genome.cpp:162,190`. First and second batches died this way |
| 22 | Follows the tilt when it brings the marble. | **unreachable** as tilt-conditioned | tilt isn't a situation (`loci.def:10-11`). Boredom pinned |
| 23 | Hides when the dish is tilted, because shaking usually follows. | **unreachable** | no stimulus prediction anywhere. `DESIGN.md:153` claims it, the code doesn't do it |
| 24 | Waits for breakfast at the owner's usual hour. | **unreachable** | asleep at 08:00 every day. Eat picks flat by hour. Eat with no pellet ends at once |
| 25 | Face-down means bedtime. | chemistry, not learning | lid_down → melatonin +0.3 (`starter_genome.cpp:197`). Any face-down past 2 s counts, including a flip |

## Depth, in numbers

All lives are body-only, starter genome, `speciesSeed` 7, 8 or 9.

| Measure | Value |
|---|---|
| Live situation×action cells body-only | 154 of 220 |
| Cells whose weights changed at all while awake (4 days) | 83 to 156, nearly all through the bias features |
| Cells with a learned change > 0.05 (4 days) | 7 to 8. About 7 of them are the birth instincts eroding. Punisher 11 to 12 |
| Cells strong enough to be an heirloom at day 4 | 0 (1 for the punisher) |
| Heirlooms passed on by a full natural life (3 lives, about 8.5 days, old age) | **0** |
| Best belief at a natural death | \|effect\| 0.046, confidence 0.18, against 0.549 needed |
| Learned-delta similarity across owner styles | cosine 0.86 to 1.00 |
| Waking time with boredom < 0.02 | 100% under all 4 owner styles measured |
| Mean waking pain | 0.83 to 0.88. Pinned by daily injury (see below) |
| Punishment effect (shake on Chase) | Chase picks -96% |
| Reward effect (double knock or cradle on Call) | none, or negative |

**Associations across minutes.** No. The credit window is one action, 2 to 4 s, except sleep. The only memory longer than 0.3 s is the slow continuous loci (`held`, `cradled`, `upside_down`, `light`, day phase) and the 16-episode dream buffer.

## Side findings that distort learning

These are balance defects, not learning code. Each one makes the table look shallower than its design. They need tracked issues; I filed none, since this was read-only.

1. **A free boredom pump.** Foresee gives boredom -0.2 on every use (`starter_genome.cpp:199`). Boredom's tonic refills about 0.4 per hour, and Foresee removes about 17 per hour. Every boredom lesson is dead.
2. **Nightly starvation, then a rotten breakfast.** Hunger reaches about 0.99 overnight, because nothing wakes a hungry sleeper. Pinned hunger emits injury (`starter_genome.cpp:150-151`). A pellet pressed while he sleeps rots in 90 minutes (`MetabolismGene{4,8,90,…}`), and he eats it on waking. Injury ≥ 0.1 pins pain near 1.0 for most of the day (`:153-154`). The doting, quiet and trainer runs averaged 0.83 to 0.86 pain while awake.
3. **A flip longer than 2 s is a tuck-in** (`senses.cpp:25-26,263-265`). Each one adds melatonin +0.3, about 3 hours of sleep, and no waking learning.
4. **Drops are lethal at about 6 a day.** That's fine as a stake, but drops give no warning and no learnable cue, since `dropped` isn't a situation.

## Proposals for depth, ranked by fun per cost

Each one fits the registry pattern where possible. Before shipping, rerun `learnsim` to check that the trainer gains Call share, heirlooms appear, and decisions stop being uniform.

1. **Let rewards land. Cost: genome numbers only.** Shrink Foresee's boredom relief (`self_foresaw` -200 to about -40) and make boredom, loneliness and need-touch refill fast enough to matter between gestures. Fix the overnight hunger (a hunger→wakes stimulus or slower night hunger) so pain stops sitting at 1. This alone turns on clicker training (knock as reward), the marble as a toy, and cuddles as comfort. **Highest fun per cost.** Every reward lesson in the table is dead without it.
2. **Heirlooms of what this life learned. Cost: small, in `clutch.cpp` and `mutate.cpp`.** Rank beliefs by |w - w_birth|. The birth table is free to rebuild, because instincts are deterministic: replay the genome's instinct genes into a fresh `Brain` at clutch time. Drop the bias features (`light`, `age`, `day_*`, `marble_near`) as cues, or tag them as temperament heirlooms. Re-queue instinct genes nightly, so the table doesn't wear down to zero, which is what lets a lesson survive a full life. Today a full life passes on nothing.
3. **Stimuli that interrupt, plus a heritable memory span. Cost: one `stimuli.def` column, one line.** An `interrupts` flag makes knock, shake, cradle and marble_hit force a fresh decision. A temperament byte replaces the hard `>> 1` at `creature.cpp:236` with a half-life of a few seconds. That unlocks cue→trick training: a knock becomes a command and a double knock its reward. "After a shake, curl" becomes real. Memory span becomes a lineage trait.
4. **Two situation flags. Cost: trivial (`loci.def`).** Turn on `food_rotten`, and add a derived `tilted` locus. Lines that turn their nose up at old food, or hug the downhill rim, become heirloom-able.
5. **Dreams of the day's highlights. Cost: small (`brain.cpp` episode insert).** Keep the 16 episodes with the largest |drive change| instead of the last 16 before bed, and let the dream sprite show which. He dreams about the shake. This also stops bedtime from being burned in 280 times a night.
6. **Expectation table, shown by Foresee. Cost: medium.** A second small table `E[f][s]` (20 × situation stimuli, NLMS) predicts which stimulus follows a situation within N seconds. It writes `expect_<s>` loci that genes can wire to fear or appetite. Foresee's glow then shows what he expects. The seer theme becomes literal, and it unlocks hiding before a shake and perking up before the owner's usual knock.
7. **Owner routine: hour bins plus a Beg action. Cost: medium (6 locus rows, 1 action row and behaviour, 1 stimulus gene).** Replace the single day sine/cosine with six 4-hour bins. Begging gets `pellet_dropped` → a small hunger relief, so a beg in the hour the owner feeds pays off. Breakfast anticipation then emerges, and it can become an heirloom.
8. **Curiosity from surprise. Cost: small to medium.** The brain writes a `surprise` sense locus, Σ|observed - predicted|, and an emitter gene turns surprise into boredom relief. He then seeks out actions he can't yet predict, and the gain is a heritable "curious" gene.
9. **Real cross-action traces. Cost: medium.** Credit the last K (feature, action) episodes with λ^k when a drive moves. That gives A-then-B lessons and richer, funnier superstitions, such as the hop before the cuddle.
10. **Name the superstitions. Cost: presentation only, and it depends on 2.** Label beliefs cued on bias features or caused by owner gestures as superstitions in the lineage text, such as "gen 4 believes calling in daylight is dangerous".

## Method and reproduction

- Driver: `scratchpad/learnsim/life.cpp`. Build: `scratchpad/learnsim/build.sh` (g++ -O2 against `lib/blorb/src/*.cpp` and `test/support`). Batch: `runall2.sh`. Divergence: `diverge.py`. Outputs are in `~/.cache/learnsim/c_*.txt` and `~/.cache/learnsim/d/*.txt` inside WSL `survivor`.
- Usage: `life <style> <seed> <days>`. Styles are rich, rough, doting, quiet, trainer (double knock on every Call), punisher (shake on every Chase) and cradletrainer. `FEED=demand` presses BOOT when hunger ≥ 0.25 and the dish is empty. Without it, BOOT is pressed at 08:00, 13:00 and 19:00. `DIAG`, `DIAG2` and `DIAG3` print drive, melatonin and injury traces.
- Owner realism: a gesture on 25% of looks, a look every 30 to 150 s while he's awake. That's about 150 to 250 gestures per waking day, and a lid tuck at dusk.
- Caveats:
  - "Decisions" counts switch-ins, not re-picks of the same action.
  - "Touched" counts pet-minutes with any weight change. Batch 1 (hyperactive owner, 4 s flips) is used only for the superstition-heirloom examples, and the table says so.
  - The pinned-boredom and pinned-pain readings are the starter genome's, and mutated lines may differ.
