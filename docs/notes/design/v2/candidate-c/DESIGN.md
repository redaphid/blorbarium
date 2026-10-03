# Blorbarium alife v2, candidate C: "The Almanac"

**Headline.** Grungo is a seer, so make that literal. One small predictor, the **Almanac** (what he expects to happen next, learned from what really happens), feeds ten things that were separate wishes: the vision he sees when he foresees, the shake-oracle that speaks in his heritable voice, curiosity, clicker tricks, attachment, taste, and the lineage's culture. Beside it sit a **Journal** (sleep consolidates the day's salient lessons), plastic **Traits** with sensitive periods, and a **Nest** (non-genetic inheritance: marks and lore that fade, drift and, if they keep working, harden into instincts). The body gets a clean split: **the dish moves the world** (a real 50 Hz physics marble driven by the raw accelerometer and gyro, tip-to-pour feeding) and **BOOT speaks to him** (click, call, lullaby, status card). Five minigames he plays and learns from sit on top.

Files in this folder: `DESIGN.md` (this), `sketch/` (headers and `usage_check.cpp`, compiled with the engine's own flags, `syntax_check.sh` reruns it), `ref/` (a `git archive` of `origin/engine` at `a1cb726`, read-only grounding).

## Contents

1. Problem and what I assume has landed
2. Usage (caller's view)
3. Shape (data, modules, tick order, registries, stations)
4. Inputs redesigned (marble, BOOT, feeding, gesture table)
5. The alife axes (ten cards)
6. Minigames
7. Proof (e2e scenarios per axis)
8. Phased build plan
9. Budgets
10. Red-flag screen, tradeoffs, alternatives, synthesis, reconciliation, open questions, next step

---

## 1. Problem

The engine on `origin/engine` is a sound skeleton with a shallow mind. Measured, in `learning-depth.md` and `e2e-learning.md`: only 3 of 11 controlled experiments show learning, nothing lasts the night (forgetting is 0.137 per night per weight), a stimulus is visible for about half a second, credit spans one action, and he has no model of anything that has not happened yet. Foresee, his signature, shows only a glow. The owner has one real verb (BOOT drops a pellet), the marble is a smoothed tilt that rolls at 10 Hz, and there is nothing to play.

Constraints I honor, read from the code and `user-asks.md`: integer Q8.24 and replayable bit for bit (WASM-clean, no floats); everything extends by a `.def` row plus a handler; the brain is `W[feature][action][drive]` with normalised LMS, a 16-episode dream buffer and `Q15` storage keyed by stable ids; body-only play is complete and the phone only brings special treats and twists (ask 4, 15); the board is the single writer (DEVIATIONS 4); keepsake chunks are TLV with unknown tags carried; 327,680 B of internal RAM with 32 to 62 KB free by DESIGN section 8; the character is Grungo (third person, lowercase, "brrrup", "eep", "it is WRITTEN").

**Assumed landed from the depth pass** (named so I do not redesign them): boredom and need rebalance so rewards can land, forgetting scaled by magnitude instead of a fixed 1 LSB, heirlooms ranked by learned change `|w - w_birth|`, `food_rotten` and a `tilted` situation, an `interrupts` stimulus flag, and one time-based lesson. Two further things I depend on and call out as units if the pass did not deliver them: a heritable memory span for the recent loci (unit 0.2), and a `wakes` flag on the shake gene (E12 uses it).

**What this design adds that depth does not.** A model of the world (so foresee, curiosity, bracing, begging and the oracle exist), credit across actions (so tricks exist), a place where sleep makes learning stick (so lessons last), a second inheritance channel that is allowed to be wrong (so culture and superstition exist), and an input and play surface worth a grown-up's time.

## 2. Usage (caller's view)

### 2.1 The firmware loop (nearly unchanged, still five calls)

```cpp
void loop() {
  uint32_t now = millis();
  if (now - lastSample >= blorb::kSampleMs) {
    dish.sample(readImu6(), now);          // v2: accel and gyro. BodySample gains gx, gy, gz.
    lastSample = now;
  }
  dish.tick(now, nus);
  if (now - lastFrame >= 40) { paint::draw(dish.appearance(), grungoPack(), canvas); pushToPanel(canvas); lastFrame = now; }
}
```

`Appearance` gains fixed-size fields only: `vision` (up to 3 chips), `thought` (a bounded string for the marquee), `hopper[3]`, `card`, `invite`, `shade` (the elder's ghost at hatch), `dreamGlyph`. `present()` stays pure, so frames stay byte-identical.

### 2.2 A day with him, as the owner lives it (body only)

- 08:10 You lift the green rim sector and tip the dish toward you. Moss rolls out of the hopper across the dish toward him. He is asleep and it will rot, so the care hint (a funnel with a clock) tells you he wakes at 10:00.
- 10:05 He is up. He walks to the centre, eyes glow teal, and three chips float above his head in an arc: a pellet (full ring), a palm (half ring), a moon with a small clock (quarter ring). That is his Almanac, honest. A minute later a pellet really drops and the first chip turns gold.
- 12:30 You tap BOOT once while he hops. A tick sparkle runs round the rim. You tip a pellet out two seconds later. After three days of this a tap alone makes him trot to the centre, face you and bob with a pellet in his thought bubble.
- 16:00 You shake him. Half the time he hops with an "eep". This time his eyes glow, he stands still and the marquee reads: `brrrup. a hand comes. before dusk.` At 17:40 you cradle him and the hand chip bursts gold. His second life will say it a little differently.
- 20:00 He proposes a game (a small glyph above him: a marble in a pocket). You tap to accept. You tilt, he flanks the marble and noses it, it clacks into the lit pocket.
- 21:30 You lay him face down and hold BOOT for a second. He sleeps, dreams (the bubble shows a lightning bolt, the day's shake), and lessons from the day are consolidated.

### 2.3 A host test (the clicker; `pio test -e native`)

```cpp
TEST(Clicker, PairingMakesAClickPayOnItsOwn) {
  Rig r(/*seed=*/7);                                   // Dish + ScriptedOwner + MemStorage
  r.hatchAndGrow(Stage::Child);
  for (int i = 0; i < 40; ++i) { r.owner.click(); r.run(2 * kS); r.owner.pour(bin::moss); r.run(40 * kS); }
  r.owner.hungry();                                    // let hunger rise, no pellets
  r.owner.click();
  r.run(8 * kS);
  EXPECT_GT(r.share(action::beg, 0, 8 * kS), 0.5);     // he asks for the pellet the click promised
  EXPECT_GT(r.dish.appearance().vision.chip(stim::pellet_dropped).fill, 0.5);
}
```

### 2.4 An e2e row, in the style of `e2e-learning.md` (data, not code)

```cpp
Scenario s{"clicker_pairing", "click then pellet 2 s later, 10 a day, days 1-2",
           "a click alone makes him come to the centre and beg within 8 s on day 3 (control: yoked random click times)", ...};
s.rows.push_back(daily(Who::Treatment, 1, 2, 11 * kH, click_then_pour(2 * kS), /*times=*/10));
s.rows.push_back(yoked(Who::Control, of(Who::Treatment)));       // same gestures, times independent of him
```

### 2.5 The phone, read-only verbs (the wire, additive to `commands.def`)

```
> #2a VISION
< #2a + near pellet_dropped 0.71 | far contact 0.44 | near cradle 0.18
< #2a OK topics=food,hand slots=3 floor=0.30 hit=14/20
> #2b LORE
< #2b + gen3 trick "after click, hop_circles lowers hunger" age=2 strength=0.61
< #2b + gen2 aversion "murk, tummy burbles" age=1 strength=0.40
> #2c PROPHECIES 3
< #2c + t=16:02 "brrrup. a hand comes. before dusk." about=cradle conf=0.72 KEPT
< #2c + t=11:20 "the bubbles are muddy. ask again." CLOUDY
```

New verbs are `VISION`, `ALMANAC`, `JOURNAL`, `TRICKS`, `LORE`, `TRAITS`, `PROPHECIES`. One new twist, `prime <stim> <cue> <permille>`, lets the phone's many-futures simulation (ask 11) seed a real prior into the Almanac. `pour`, `pellet_dropped`, `button` and `fed` join the `403 BODY_ONLY` refusal list.

## 3. Shape

### 3.1 The data, first

Five new state structs, each with one writer. Sizes are measured with `sketch/sizes.cpp` (x86-64, at the engine's current 20 features) and rescaled by arithmetic to the 40-feature budget in section 9.

| Struct | Holds | Single writer | Measured at F=20 |
|---|---|---|---|
| `Almanac` | `E[cue][target]` Q15, two rings of cue snapshots (1 Hz over 32 s, 1/60 Hz over 32 min), time-since per target, expectation and error EMAs | `Almanac::observe` | 2,188 B |
| `Journal` + `ProtectedCells` | the 32 most salient lessons, one protected bit per brain cell | `Journal::dreamStep` and `note` | 322 + 220 B |
| `Traits` | 5 genetic bases and 5 drifts (int8) | `experience()` once a second | 10 B |
| `Nest` | epigenetic `Marks` and up to 3 `LoreItem`s, laid with an egg | `layNest` at death, `retell` per generation | 38 B |
| `DishWorld` | 8 `Piece`s, the 3-bin `Hopper` | `DishWorld::sample` at 50 Hz, `pour` | 276 B |

Plus `GameLog` per game (136 B: plays, appeal, skill, a 16x4 `Q15` skill table) and the existing `Brain`, which grows only through the registries and is capped by a compile-time budget (section 9).

### 3.2 Modules and interface depth

```
lib/blorb/include/blorb/
  almanac.h  oracle.h  world.h  hand.h  games.h  memory.h   NEW, in sketch/
  defs/pieces.def foods.def games.def traits.def topics.def  NEW
  habitat.h/.cpp                                            DELETED in unit 1.1 (callers migrated, no shim)
  defs/*.def (stimuli, actions, loci, drives, chemicals, reflexes, gene_kinds, commands, twists)  +rows
```

- `Almanac` has four calls that matter (`observe`, `expect`, `vision`, `scale`) and hides the NLMS update, two snapshot rings, per-target horizons, error EMAs and the progress signal. Nothing else computes an expectation. This is the deep module of the design.
- `DishWorld` is two calls to the Dish (`sample`, `pour`) and hides rolling physics, rim and pair collisions, resting-piece sleep, the hopper and food choice. It replaces `Habitat`, so the marble, pellets and flies are one pool with one set of rules (migrate callers, then delete the old API, per migrate-callers-then-delete-legacy-apis).
- `Journal`, `Traits`, `Nest` each expose a `note`/`experience`/`layNest` and a reader. Their policies (what is salient, when a window closes, when lore assimilates) are hidden and genetic.
- A game is a struct with `start/step/state/over/stop` and a row. It cannot write a drive. Every effect on him is a stimulus his genes interpret, which is what keeps games heritable.

### 3.3 Tick order (additions in bold; the rest is DESIGN section 3 unchanged)

**50 Hz `sample`.** Detectors run (existing plus `ClickDetector`, `PourDetector`, `SpinDetector`), then **`DishWorld::sample` integrates every piece on the raw accelerometer and gyro** and fires `clack`, `marble_hit` and game outcomes into the pending `SenseOut`.

**10 Hz `tick`.**
1. Clock, tick-rate detectors, **hopper refill**.
2. Habitat becomes **world**: a `pour` stimulus tips a bin (fires `pellet_dropped`), **the active game steps**.
3. The creature ticks. Its step 2 (stimuli through genes) gains two hooks. **A `shake` rolls the oracle first** (`prophesies()`); on a prophecy the shake's chemical response runs at a quarter strength so he is rattled but does not hop, and the `trance` reflex starts. **Each stimulus amount is scaled by `Almanac::scale()`**, which reads the expectation as it stood before this tick's `observe`. Then **`Almanac::observe`** (events and cues in, expectation loci and `surprise`/`progress` out, anticipation paid, claims resolved), chemistry, lifecycle, brain (features now include expectation, previous-action, trait and relationship loci) or **`Journal::dreamStep` while asleep**, behaviour through the **station walker**, face.
4. **Once per second**, `experience()` moves trait drift, the Journal notes completed episodes, contact gaps update attachment.

Determinism. Every new quantity is `Fx` or an integer, every random draw comes from the creature's or the dish's saved stream in a fixed order, and the oracle draws one number whether or not it prophesies, so replays do not fork on the branch. Each module lands with a deliberate re-bless of the 24-hour replay hash (section 8).

### 3.4 Registry rows (what "one row plus a handler" now buys)

| Registry | New rows | Handler |
|---|---|---|
| `stimuli.def` | Body `click`14, `call`15, `pour`24, `spun`25. World `clack`26, `game_hit`27, `game_miss`28, `game_start`29, `contact`30, `reunion`41. Self `eureka`36, `prophesied`37, `prophecy_kept`38, `prophecy_broken`39, `nauseated`40, `dreamed`42. Id 8 `button` is retired and never fired. All ids stay under 64. | stimulus genes |
| `stimuli.def` new columns | `horizon` (seconds, 0 = not foreseeable), `scale` (Near or Far), `topic` bit, `phrase` index. 13 targets (see below). | none, the Almanac iterates the table |
| `actions.def` | `beg`11, `play`12, `attend`13, plus a new column `station` (Home, Pellet, Piece, Rim, Roam, Game) | one behaviour struct each |
| `drives.def` | `curiosity`8 | genes |
| `chemicals.def` | `bond`25, `wary`26, `hope`27, `trance`28 | genes |
| `loci.def` | `expect_<s>` 200 to 215 (derived from the target order), `prev_<a>` 216 to 231 (derived from actions flagged `chain`), `trait_<t>` 232 to 239, `game_on`240, `invited`241, `gap`242, `trance`141 (Act), `daypart_0..5` (Sense). Deleted: dead `asleep` situation. | detectors, Almanac, creature |
| `reflexes.def` | `trance` (30 ticks, face foresee, pose foresee) | draw.cpp |
| `gene_kinds.def` | `foresight`0x14, `appraisal`0x15, `plasticity`0x16, `taste`0x17, `aptitude`0x18, `oracle`0x19, `voice`0x1A, `culture`0x1B (all class Mind), `dreamer`0x1C (Mind), `lifehistory`0x33 (Life) | `express_/describe_/rules_` trio |
| `pieces.def foods.def games.def traits.def topics.def` | in `sketch/defs/` | piece physics constants, game structs |
| `commands.def`, `twists.def` | section 2.5 | `cmd_*`, `twist_prime` |

**The 13 foreseeable stimuli.** Near ring (1 Hz, 32 s): `knock` 6 s, `shake` 10 s, `picked_up` 10 s, `cradle` 20 s, `click` 8 s, `clack` 3 s, `marble_hit` 4 s, `lid_up` 12 s, `game_hit` 10 s. Far ring (1/60 Hz, 32 min): `pellet_dropped` 30 min, `dusk` 30 min, `contact` 60 min (clamped to the ring), `nauseated` 60 min (clamped). Topics: food (`pellet_dropped`), hand (`cradle`, `picked_up`, `click`, `contact`), danger (`shake`), play (`knock`, `marble_hit`, `clack`, `game_hit`), night (`dusk`, `lid_up`), self (`nauseated`).

Day phase. The single sine/cosine pair becomes six 4-hour `daypart` bins, because the Far ring cannot predict "supper at 17:00" from one harmonic.

### 3.5 Stations: where he is when he acts (user input 1)

He rests at the centre and crosses the face to act. This becomes a column, not scattered code.

| `station` | Meaning | Actions |
|---|---|---|
| Home | within 0.12 of the centre, facing the viewer | rest, foresee, call, beg, attend, prophesy (trance in place, vision anchored to the top arc) |
| Pellet | the pellet he chose (taste-weighted) | eat |
| Piece | the marble, a fly, or the flank point of the game | chase, play |
| Rim | the nearest rim point | curl, flee_tilt |
| Roam | an excursion within `bold`-set radius | wander, follow_tilt |
| Game | the game's own pose points | play (per move) |

One `StationWalker` (in `actions.cpp`) walks to the action's station at walk speed and returns him Home at amble when the action ends, so Rest means "back at the centre". Traits change how far and how fast, never where Rest is: bold ventures to 0.8 and returns briskly, shy ventures to 0.4 and bolts home hooded. Test `Stations.RestIsHome`: at least 90% of Rest ticks lie within 0.12 of the centre once 5 s have passed since the last action.

## 4. Inputs redesigned

### 4.1 The marble as a real IMU-driven physics toy

What is wrong today: `Habitat::step` reads smoothed tilt at 10 Hz (an EMA of 1/16 per 20 ms sample, then a 3 degree dead zone), so a flick does nothing, a spin does nothing, and the marble only ever eases downhill.

The v2 rule uses the **raw** 50 Hz accelerometer. A free piece in the dish frame accelerates by `-(ax, ay) x g_s` (the specific force reads gravity's plane component plus the dish's own acceleration), so a flick east sloshes the marble west, a slow tilt rolls it downhill, and a vertical dish drops every piece to the bottom rim. The gyro adds a rotating frame: centrifugal `omega^2 r` outward and Coriolis `-2 omega x v`, scaled by `kSpinGain`. Spin the dish and the marble is flung to the rim and orbits for a second or two.

| Constant | Value | Why |
|---|---|---|
| `g_s` | 6.25 radii/s^2 per g (0.0025 per sample^2) | 0.5 g (30 degrees) rolls centre to rim in 0.8 s, watchable on a 1.28 inch screen |
| rolling friction | marble 1.2%, pellet 25%, fly 0.8% per sample | the marble coasts, pellets stop where they land |
| restitution | marble 0.7, pellet 0.1 | the marble clacks off the rim and is a predictable event for the Almanac |
| `kSpinGain` | 0.25 | a 360 dps spin is about 40 radii/s^2 raw, which would pin everything to the rim in 100 ms |
| piece cap | 8 | 28 pair tests per sample, under 0.1% of a core |

He is a heavy kinematic circle. Pieces bounce off him and he moves them only by nosing (his facing x a speed). Everything is `Fx` with integer square root and a fixed index order for collisions, so it replays bit for bit. Resting pieces sleep and cost nothing.

Perception: new loci `marble_speed`, `marble_incoming` (heading toward him) and the stimuli `clack` and `marble_hit`. Because the marble is now chaotic but learnable, it is the first good source of learning progress (axis 3).

### 4.2 BOOT becomes "the Hand": he is spoken to, not fed

The split that keeps the table conflict-free: **the dish moves the world, BOOT speaks to him**. BOOT never feeds. It never changes meaning mid-game either; games read `click` events as the owner's voice.

| Press | Fires | Meaning for him |
|---|---|---|
| tap, released under 350 ms | `click` | the marker: "good, that". Fires on release, so latency is the press length (about 100 ms), close enough for clicker training |
| second tap within 400 ms of the first release | `call` (instead of a second `click`) | his name. He answers with the `attend` action and may be rewarded for coming |
| hold 600 to 1200 ms, then release | none (UI) | the **status card**: age, generation, 5 trait petals, attachment glyph, trick count, lore count, for 3 s |
| hold reaching 1200 ms | `button_hold` (existing) | lullaby: sleepiness up. In the clutch screen it picks the egg |

In the clutch screen `click` moves the cursor (it replaced `button`). The tap is also the reply to his game invitation. `button` (id 8) is retired, never fired.

### 4.3 Feeding moves to the dish: tip to pour

Three bins of food sit around the rim (the hopper, drawn as coloured pips). **You lift the sector you want and tip the dish: that bin pours onto the dish and the pellets roll toward him under the same physics as the marble.** One gesture, no menu, and the direction picks the flavor.

Gesture definition (`PourDetector`): the dish starts level (under 12 degrees for 300 ms), then tips at least 30 degrees with a tilt rate of at least 60 dps, and holds 350 ms. The bin is the rim sector that was highest. One pellet per pour, at most one per 1.2 s while held. Cradle (held still for 3 s) and pour (tipped with motion) cannot coexist by definition. An accidental tip while carrying can spill a pellet. That is a physical consequence an owner learns, and a rotten pellet teaches too (the care hint shows a funnel with a clock the first times).

Bins refill on the pet clock (moss fast, grub slow, murk follows the world's seeded "season"). Pantry pips on the rim now show three stocks. Everything the old `button` did to the habitat is now `pour`: still body-only, still refused to the phone.

### 4.4 The complete gesture table

| Gesture | Signal | Fires | Disambiguator against the others | Meaning |
|---|---|---|---|---|
| knock | tap engine single | `knock` | rate limit 400 ms | attention, light play, clicker's weaker marker |
| double knock | tap engine double | `double_knock` | a double is never two singles | praise; picks an egg |
| shake | 4 jolts of at least 0.55 g in 900 ms | `shake` | jolts need linear acceleration; a pour has none | startle, or the oracle |
| tilt | slow, continuous | tilt loci, no stimulus | below 30 degrees or without tip rate | rolls the marble and pellets |
| pour | level, then tip at 60 dps to 30 degrees, held 350 ms | `pour` | needs level start and motion; cradle needs stillness | feeds the bin you lifted |
| spin | gyro z at least 300 dps for 250 ms, then under 120 dps | `spun` | gyro only; shake needs accelerations | dizzy or delighted by genes; flings the marble |
| pick up / put down | in-plane over 0.3 g for 400 ms | `picked_up`, `put_down` | refines, does not exclude, pour | existing |
| cradle | held, still, face up for 3 s | `cradle` | stillness vs pour's motion | comfort |
| flip face down 2 s | `az` under -0.8 g | `lid_down`, `lid_up` | existing hysteresis | tuck-in, and the peekaboo game's frame |
| free fall then impact | existing | `dropped` | existing | pain |
| BOOT tap, double, card hold, lullaby hold | section 4.2 | `click`, `call`, none, `button_hold` | press length windows | the owner's voice |

**The conflict lint (encode the lesson in structure).** `GestureCorpus` is a set of recorded 50 Hz traces, one per row above plus the hard cases (a lift off a desk, a cradle at 25 degrees, a flick that is not a shake, a slow carry through a doorway, a spin that is not a shake, a tap on a face-down dish). Each trace lists the exact stimuli it may fire. The suite fails if any detector change fires an extra or missing stimulus on any trace, and the existing rule "a 10 Hz sampling of a shake is not detected" stays. Two gestures that share a signal region fail in CI, not in a hand.

## 5. The alife axes

Each card is what develops, the mechanism, how it shows on screen, how it is inherited, and how the owner influences it. The e2e for each is in section 7 under the same number.

### 5.1 Foresight: expectation, shown as the vision he sees (user input 2)

**What develops.** A model of what follows what, learned from experience, not scripted. "Tilt, then shake." "Click, then pellet." "Dusk, then dark." "Five-ish o'clock, then supper."

**Mechanism.** The Almanac `E[cue][target]` is a normalised-LMS linear predictor of "this stimulus arrives within its horizon". Cues are the brain's feature vector (one source of truth). A ring of cue snapshots gives the delayed target: each second, for each target, `y = (time since it last fired) < its horizon`, and the snapshot from one horizon ago is credited. Two rings (near, 1 Hz over 32 s; far, once a minute over 32 min) give both "a knock follows a tilt" and "a meal follows 17:00". `expect_<s>` loci publish the prediction. Competition between cues (NLMS) gives blocking and overshadowing, and also the spurious cues of 5.10.

What expectation does, all through existing genes:
- **Appraisal gene** (`appraisal`): a stimulus amount is scaled by `1 + surpriseGain(1-p) + expectedGain p`. The starter genome braces for shakes (expectedGain -0.5, surpriseGain +0.5). A predicted shake lands as a quarter of the adrenaline, below the startle threshold: he does not hop.
- **Anticipation** (a field of the same gene): a share of a stimulus's effect is paid when its expectation rises. That is Pavlov. It is how a click becomes a reward (5.4) and how a cue becomes dread.
- **Foresee action is no longer a boredom pump.** While it runs, Almanac learn rate doubles and braced appraisal applies at 1.5 times for one horizon (attention). It costs energy. Its boredom relief drops to the depth pass's small value.

**What he sees (on screen).** During Foresee he walks home, his eyes glow (the existing halo), and a **vision** opens above his head on an arc between 11 and 1 o'clock. The vision is the Almanac's top 1 to 3 expectations that clear his floor (default 0.30) and his topic mask, strongest first. Each chip is a 16x16 one-bit glyph (pellet, palm, bolt, ring, moon, spiral) with a **ring that fills to the confidence**. A small clock face on the chip means the Far ring ("later today"). Nothing is invented: if nothing clears the floor he sees a slow spiral and the face shows `foresee` with a questioning tilt. When the event arrives inside its horizon the chip bursts gold and he shows `happy` with a short "it is WRITTEN". When the window closes first the chip goes to grey smoke. A five-pip seer streak on the rim counts recent hits. After an event he did **not** foresee, with the heritable `hindsight` chance he strikes a "knew it" pose and says "...grungo knew that would happen" (the SOUL.md joke). The phone's log marks that claim as unpredicted, so the owner can see the boast and the truth separately.

**Heredity.** `foresight` gene: learn rate, floor, slots 1 to 3, topic mask (which of the six topics he attends to), hindsight. Rules: slots and rate nudge, topic bits are flags (a point mutation flips a topic, dormant bits wake and sleep like any dormant gene), hindsight nudges. Two lineages see different futures: a food-and-hand line in a regular home is a good seer, a danger-and-play line is a nervous one. Appraisal genes (who braces, who is startled harder by surprise) mutate and duplicate like stimulus genes.

**Owner influence.** Routine feeds the Far ring, so a regular home makes a better seer. A **tell** before a shake (a double knock, always) teaches bracing. A random home gives a muddy vision and a startle-prone frog.

### 5.2 The oracle and his voice (user inputs 3 and 4)

**What develops.** A heritable voice that tells the future out loud, grounded in what he really expects, in his own words.

**Mechanism.** On `shake` the creature rolls `prophesies(oracle, vision, surprise, rng)` against `chance + vibe x vision`, with a 20 s cooldown. Heads, he hops as today. Tails, the `trance` reflex runs: three seconds still, glow at its floor, the chips pop open in sequence, and the marquee scrolls the composed text (the `thoughts` branch's marquee; my contract is `Appearance::thought`, at most 56 characters, one pass). The text is `compose(oracle, foresight, vision, tick, rng)`:

`[tic] + [subject + verb, hedged by confidence] + [noun phrase from topic and lexicon] + [time phrase from horizon and scale] + [closer]`

Confidence picks the register of certainty: above 0.8 "it is WRITTEN" with the key word in capitals, 0.5 to 0.8 a plain claim, under 0.5 hedged, and nothing above the floor gives the 8-ball's honest "the bubbles are muddy. ask again." Topics outside his mask are never chosen. Each named event is logged as a `Claim` with a due tick and **resolved by the Almanac's own target**: the stimulus arrives (`prophecy_kept`, a gene pays pride) or it does not (`prophecy_broken`, sheepish). The record is visible on the phone (`PROPHECIES`) and is a feat source (`true_seer`).

Examples, same expectation (pellet at 0.74, Far), three lineages:

| Lineage | Line |
|---|---|
| founder (third person, terse, grave) | `brrrup. grungo has foreseen it. there will be a SNACK. soon.` |
| gen 6 (silly, caps on the verb) | `eep! the bubbles say SNACK!! probably.` |
| gen 11 (first person, ornate, anxious) | `i see... i think i see a snack. before dusk. please be a snack.` |

**Heredity.** `oracle` gene (chance, vibe) and `voice` gene (person, register, mood, tic, caps, hedge, four lexicon picks). Every voice byte indexes a phrase table in flash, so any mutation is a legal sentence. `Any` rules for tic and mood, `Nudge` for register and hedge, point mutation on a lexicon pick swaps a synonym. `describeDiff` prints "voice #52 mood grave to silly; foresight #50 topic +danger". The clutch screen shows each egg's **sample line** (compose with a canned vision), so choosing an egg is choosing a voice, a topic set and an oracle chance (a small eye glyph, filled to the chance).

**Owner influence.** Shake him when you want to hear it, and how often depends on his genes (selection through the clutch). Whether a prophecy comes true depends on how regular your home is. A frog that is always right gets a feat and a swagger; one that is always wrong learns, through the same claims, to hedge (appraisal on `prophecy_broken` raises his `hedge` drift).

### 5.3 Curiosity as learning progress

**What develops.** An appetite for things he is getting better at predicting, not for noise.

**Mechanism.** Per target the Almanac keeps a slow and a fast error EMA. `progress = max(0, slow - fast)`; when it crosses a bar (rate-limited to once a minute) the Almanac fires `eureka`, which genes turn into a `curiosity` drive relief and a little joy. `curiosity` has tonic production, so he itches for it, and it feeds `arousal`, which raises exploration noise and the pull of the marble and games. Raw surprise would trap him in static ("noisy TV"); progress does not, because a random source has no progress. A mastered toy stops paying, so he moves on.

**On screen.** A small `!` flicks above him on eureka. A curious frog circles the marble; a bored mastered one walks off.

**Heredity.** `progressGain` in `foresight`; the appraisal on `surprised` events, so one line takes surprise as joy (neophile) and another as fear (neophobe); the `curious` trait base. **Owner.** Introduce new learnable things (a tilt pattern, a game, a new flavor); a perfectly predictable home bores him, a random one gives no eureka.

### 5.4 Tricks: the clicker and chains (sequences)

**What develops.** Learned routines with a cue: "on a call, hop in circles", "after foresee, then hop", taught by the owner with a marker.

**Mechanism.** Two small additions, no special trick object.
1. **Click as a conditioned reinforcer, through chemistry only.** Pair `click` with a pellet. The Almanac learns `E[recent click][pellet_dropped or fed]`. The appraisal gene's anticipation field pays a share of the pellet's relief (and a `hope` chemical that reacts against `hunger`) the moment `expect` rises. So a click alone now lowers hunger a little. The brain's existing "observed drive change" credits the action running when the click came. It works best when he is hungry, as real training does, and the owner learns to train before a meal. Extinction (clicks with no pellet) lowers `E` and the effect fades.
2. **Credit across actions.** The brain keeps its 16 episodes and, when a window's observed drive change is not explained by the current action's prediction, shares the residual with the previous two episodes at 0.5 and 0.25. New `prev_<a>` loci (written when a flagged action ends, decaying over the memory span) let `W` express "after A, B", so the credit has something to land on.

The **trick book** is derived, not stored: cells where the cue is an owner event or a previous action, the action is visible, and the effect clears a bar, with a performance rate from the Journal's counters. It shows on the phone (`TRICKS`) and on screen as a "ta-da" burst with a flicker of foresee glow.

**Heredity.** `aptitude` genes (an action and a learning gain): lineages with natural spinners. Tricks themselves go through lore (5.10). **Owner.** Click inside about 1.2 s of the behaviour, then pour. Shape in steps. Pair first, train hungry, vary the reward so it persists.

### 5.5 Attachment and routine: he learns his owner

**What develops.** Who you are as a process: your hours, how often you answer, how roughly you handle him.

**Mechanism.** Two slow chemicals, `bond` (raised by gentle contact: cradle, click, call answered, pour) and `wary` (raised by shake, drop, flip; both with day-scale half-lives). The Almanac's far target `contact` (first owner event after a quiet minute) learns his owner's hours from the dayparts, and `E[recent call][contact]` is **responsiveness**, how reliably calling brings you. A `gap` locus counts time since contact against the *expected* gap for the daypart. Past 1.5 times expectation he is separated; the response is genetic. On the next contact the Dish fires `reunion` with the gap size. The same applies across an unplugged catch-up (DEVIATIONS 3), so "while you were away: 9 h" has a face.

The style is emergent, classified only for display and the lineage log: secure (bond high, wary low, settles fast), anxious (bond high, responsiveness low: calls repeatedly, takes long to settle), avoidant (wary high: curls at reunion), disorganised (rough and unpredictable).

**On screen.** Before your usual hour he waits at the centre facing the viewer (`beg` pose, small bob). At reunion a secure frog hops in circles and settles inside 30 s, an anxious one calls and keeps calling, an avoidant one hoods up and turns away. The status card shows the attachment glyph.

**Heredity.** Genes set bonding rate, separation threshold, reunion response. The Nest passes `Marks.wary` (the parent's lasting wariness, halved) to the egg, so a roughly handled line starts wary and a gentle owner has to earn trust back over generations. That is non-genetic and it fades. **Owner.** Regular hours, answering calls, gentleness, not vanishing for a week.

### 5.6 Temperament that drifts: sensitive periods

**What develops.** Five traits (bold, social, curious, patient, lively) = a genetic base plus a drift the life moves, **most while he is young**.

**Mechanism.** `experience()` once a second feeds a per-trait handler a few signals (fear, pain, surprise without harm, gentle contact, rough contact, neglect, play, progress, patience). Drift moves by `rate x sensitivity(stage)` and clamps to +-64. `plasticity` gives sensitivity per stage (typical: baby 255, child 200, adult 60, elder 20) and a `carry` share for marks. Traits are loci (232 to 236) that genes and the brain read: `bold` scales how hard a shake lands (an appraisal term), how far he roams and how fast he returns; `lively` the walk speed and hop height; `patient` the trace length; `social` contact seeking; `curious` the progress gain.

**On screen.** Excursion radius and speed (stations), blink rate, idle fidget, how fast he comes to a call, hop height. The status card is a five-petal radar. **Heredity.** The base is genes. `plasticity` is heritable, so a late-bloomer line stays impressionable. At laying, `Marks.drift = parent drift x carry` becomes the egg's starting drift, then it decays. **Owner.** The first two days matter most. Gentle novelty early makes a bold frog; the same exposure to an adult barely moves him.

### 5.7 Memory and dreams: sleep makes it stick

**What develops.** A day's lessons that survive the night, forgetting that is selective, dreams you can read.

**Mechanism.** Root cause 1 of `e2e-learning.md` (a flat 1 LSB per dream) is assumed fixed by the depth pass (proportional forgetting). On top of that, forgetting has two speeds. A **protected bit** per brain cell (630 B) marks a consolidated memory that forgets slowly; unprotected cells forget over hours. The **Journal** keeps the 32 most salient lessons of the day, salience = `|drive change| x (1 + surprise at the time)`, tagged by emotion (fear and pain weigh more). While asleep each dream replays one lesson by salience (trivia under a floor is never replayed), re-applies it, and counts a rehearsal. At `rehearsalsNeeded` and under the night's `capacity` the cell is protected and `dreamed` fires. A night interrupted by a shake loses the dreams it did not have time for: sleep-deprived frogs forget. With the heritable `mixRate`, a dream sometimes pairs one lesson's cue with another's action as a faint hypothesis in an *unprotected* cell. Reality keeps it or wipes it, which is how a dreamt trick is invented and how a dreamt superstition starts.

**On screen.** The dream bubble shows a glyph of the lesson being replayed (bolt for a shake, ring for the marble, palm for a cuddle, pellet), so peeking at a sleeping frog tells you what the day meant to him. **Heredity.** `dreamer` gene: capacity, rehearsals, salience floor, mixRate. The `dreamer` feat already exists and gets teeth. **Owner.** Protect the night: tuck him in (lid or lullaby), no shaking after dusk, a regular bedtime.

### 5.8 Life-history strategy: fast and slow lives

**What develops.** A heritable pace, with trade-offs built in, that the owner selects at every clutch.

**Mechanism.** `lifehistory {pace, brood, investment, maturity}` expresses by *adding rules* (the way every gene does): `pace` raises Life's decay catalyst (shorter life, and since stage thresholds read Life, shorter stages), raises the hunger tonic, lowers the dreamer capacity and the sensitive-period length (**less learning capital**), lowers body size, and raises the mutation policy's point rate and clutch cap. `investment` raises the egg's starting energy, incubation time and the `Marks` carry. No free lunch: fast lives are short, jittery and weird with many eggs; slow lives are big, deliberate, learn deeply, lay one invested egg.

**On screen.** Size and idle jitter (fast is small and twitchy, slow is bulky). The clutch screen shows each egg's pace as a small hourglass fill and a **lifespan estimate from the phenotype** ("about 6 d"), computed in closed form from the Life gene (no simulation). **Heredity.** Mutation nudges `pace` with a small sigma, so it responds to selection. **Owner.** Choosing eggs is breeding. Going on holiday, choose slow. A richly attentive home can use a fast, prolific line.

### 5.9 Taste: preferences and aversions

**What develops.** Likes and dislikes of what he eats, including one-trial aversions to a food that made him sick twenty minutes later.

**Mechanism.** Foods are pieces with a flavor (`foods.def`): moss (bland, safe), grub (rich, safe, scarce), murk (sweet, spoiled, delayed nausea), fly (only the snap game serves it). The choice of *which* pellet he walks to (`chooseFood`) weighs distance, innate taste (`taste` gene) and learned `liking[food]`. Liking rises with ingestion scaled by hunger. **Aversion bridges the delay through the Almanac:** the Far ring holds `E[ate_murk][nauseated]` (an `ate_<food>` echo with a 40-minute half-life is a cue), learned fast by a `garcia` rate in the gene (biological preparedness for toxins specifically). When he sees murk and `expect_nauseated` is high, appraisal pays anticipated discomfort and the choice score drops. The chemistry already makes toxin (`toxin` reacts into `injury`), a `nauseated` stimulus fires when toxin crosses a bar.

**On screen.** He reaches the pellet, **refuses it** (annoyed face, turns away, a short curl) and leaves it to rot. Loved flavors get `blep` and a hop. The pellet's colour is the palette tint of its flavor. **Heredity.** `taste` gene (innate valence per flavor, `tolerance`: a detox reaction rate), so a line can evolve to *eat murk safely*, a dietary niche selected by what the owner serves. Aversions pass through `Marks.aversions` and decay. **Owner.** Which bin you tip, and the order.

Phrasing note for the Never list: the sickness is a "tummy burbles" bubble, not illness language (open question 4).

### 5.10 Superstition and culture: lore that fades, drifts and hardens

**What develops.** Beliefs that are wrong on purpose and customs that outlive a life.

**Superstition as a feature.** It arises where it did in Skinner's pigeons: coincidence plus intermittent reward plus salience-weighted consolidation (5.7: an unexpected reward is high-salience, so a lucky coincidence is protected). A heritable `doubt` rate (in `culture`) decides how quickly an unrewarded ritual extinguishes, so there are skeptic and devout lines. It shows: before the supposed trigger he performs a stereotyped two-step (a hop, then a spin). It is named in the phone's text ("grungo is sure that hopping before supper makes supper come"), with the template table `beliefs.def`, tagged `Superstition` when the cue is his own previous action or a bias feature.

**Culture, with two inheritance channels.** The depth pass makes learned beliefs instinct genes immediately. That is Lamarckian and instant, and a lineage with a mistaken belief locks it in. v2 inserts a probation:
1. At death `layNest` takes the top three consolidated lessons as **lore** (with their kind: trick, aversion, superstition, habit) and the parent's trait drifts and wariness as **marks**.
2. Each generation `retell`s the lore: with the heritable `drift` chance an item swaps its cue or its action for a neighbour (the telephone game: a new superstition is born by copying error), and every item fades by `doubt`.
3. At hatch `teach` writes the lore into the new brain and Almanac as *unprotected priors*. The child's own life confirms or kills them.
4. An item confirmed (re-reinforced and re-consolidated) in **three consecutive generations** is **assimilated**: it becomes an `Heirloom`-flagged instinct gene (permanent, in the genome, shown "learned by gen 4, assimilated gen 7"). That is genetic assimilation, a custom hardening into instinct only after it has kept working.

**On screen.** At hatch, **the elder's shade**: a translucent silhouette of the previous creature plays the lore's action beside the hatchling while its cue glyph pulses, and the baby watches with the foresee face. The status card counts lore. **Heredity** is the whole point: genes (permanent, mutated) and lore (soft, drifting, decaying, fixing). The family tree shows gene diffs and lore items in different colours. **Owner.** Train a trick across several lives and it becomes the line's instinct. Neglect it and it is gone in three generations.

## 6. Minigames he learns from

A game is a row in `games.def` and a struct with `start/step/state/over/stop`. The Dish steps the active game. A game moves pieces and fires outcome stimuli (`game_hit`, `game_miss`, `game_start`, plus the pieces' own `clack`); it never writes a drive, so every effect on him is a gene that can mutate. Each game keeps a 16x4 skill table (136 B including log) learned by the same NLMS, so the brain's `W` does not grow per game. The brain only decides *whether* to play (a `play` action whose value is the learned `appeal` of each game, which is his preference) and learns what play does to his drives from the stimuli.

**He proposes; you accept with a tap.** When curiosity or boredom is high and a game is unlocked he walks home and a glyph appears above him (the `invite`): the highest-appeal game, with a tie-break for the least recently played. A `click` starts it. Ignore him and he plays alone or drops it. There is no menu. A `call` during a game ends it.

| Game (feat) | Rules | How he plays and learns | What he gains | What it teaches the lineage |
|---|---|---|---|---|
| **Pocket** (start) | One of six rim pockets lights. Get the marble in by tilting, or he can nose it. Scoring fires `game_hit`; a rim clack without a goal is silence, a goal is a chime-flash. | State: marble-to-pocket bearing bin x his position relative to the marble (16 states). Moves: wait, nose, flank-left, flank-right. He starts random, finds that **getting behind the marble then pushing** pays, a two-step sequence. | boredom and curiosity relief, joy, a bubble tally; skill up | an `aptitude` for flanking (nose-aim noise falls), a trick-book entry "flank, push" and, when confirmed, lore |
| **Snap** (adult) | Three flies orbit on a predictable path (speed rises with skill). His `snap` is a 3-tick tongue in his facing direction. A catch fires `game_hit` and serves a **fly**, the best food (nutrition 800, taste 950). Tilt drifts the flies; a shake scatters them. | State: nearest fly's bearing bin x radial velocity sign. He learns to **lead the target**: snap when it is about to cross his lane, not where it is. That is prediction in the body. | real nourishment, joy, the favourite flavor | a `lead` skill, a taste for fly, `lively` and `bold` drift up |
| **Freeze** (start) | You are "it". A `click` means freeze. He is caught if he moves during the next two seconds, and rewarded (`game_hit`) if he holds still. Two seconds becomes four with skill. | State: time since click bin x his current motion. Moves: wait, walk, hop, hood. He learns the click is a **stop signal** and that hooding up (curl) is the easiest way to hold still. | `patient` drift, a trained response to `click` | an inhibition `aptitude` and, from the shared click, a stronger marker for tricks |
| **Echo** (a week) | You tap a rhythm of two to four clicks, short and long. He answers with hops of matching length. A match fires `game_hit`; a mismatch `game_miss`. | State: last heard element x position in the sequence. Moves: wait, short, long, rest. He learns **imitation**: hear short, do short. | joy, `social` drift, a precise timing skill | imitation is the primitive of culture, so lines good at Echo teach tricks (lore) with less decay |
| **Peek** (start) | Lay him face down 2 to 8 seconds, then face up. Predictable delay, unpredictable delay. | State: seconds under the lid bin. Moves: wait, hood, peer, hop. He learns **object permanence**: the dark ends. The Almanac learns `E[lid_down][lid_up]` and he anticipates, hood up, then pops out; with a random delay the vision shows a muddy chip. | `bond` and `social` up, an anticipation response | a foresight `slots` benefit, and the owner learns to vary delays to keep it fun |

Each game ends after a bounded number of rounds or three minutes. Rounds can be won by the owner's help (tilting) as well as his skill, so playing together is the loop. Skill is saved in `GameLog` and shown on the status card as game stars, which is the "level" a roguelite player wants to see. Unlocks use the existing feats (`reached_adult`, `lived_a_week`), so a lineage's meta-progression opens games.

## 7. Proof: e2e scenarios, control versus treatment

Same machinery and rules as `e2e-learning.md`: 40 seeds, paired by seed and care schedule, effect = treatment minus control with a 95% bootstrap CI, and **Yes** needs the CI to exclude 0 on the predicted side with at least 75% of seeds agreeing (**Weak**, **No**, **Opposite** as before). The rules are fixed before a scenario runs. Every metric reads only what the phone's `STATE` reply and the screen show.

**Harness additions** (unit 0.4): rows `click()`, `call()`, `pour(bin)`, `spin()`, `holdBtn(ms)`, `lidFor(ms)`, `tiltPath(...)`, and **reactive rows** (a trainer who watches the screen and clicks when he hops). A **yoked control** builder: the control arm receives the *same gestures at the same times* as the treatment's recorded run, so the stimulus counts match and only the contingency is broken. This is the standard control for operant claims and it removes the most obvious confound. New tick fields: `pos`, `vision` (chip glyphs), `thought` (marquee hash), `dreamGlyph`. `Harness.ArmsDifferOnlyByTheStimulus` stays.

Predicted effects below are written before running and are guesses until measured.

**E1 Foresight and bracing.** T: days 1-3, a double knock 4 s before every shake (6 a day, awake hours). C: the same knocks and shakes, independent random times. Probe, day 4, 8 times: double knock, wait 4 s, shake. Metrics: hop share on probe (T lower), fear peak (T lower), share of Foresee windows in the 6 s after a double knock that show a shake chip (T higher). Predicted hop share about 0.2 vs 0.9. Excludes: shake count, knock count, and the cue itself are equal.

**E2 The prophecy is grounded.** Both arms: pellets at 17:00 sharp days 1-3, then on day 4 a shake at 16:55. T: founder with `oracle` chance 0.9 and topic mask food and hand. C: same genome, no routine (pellets at random hours). Metric: share of prophecies naming food (T about 0.8, C below 0.2 because the Almanac has no expectation and he says cloudy), and the share of named claims that come true within the horizon (T above 0.7).

**E3 The oracle chance is heritable and decides the branch.** T: genome with chance 230/255. C: chance 25/255, otherwise identical. 20 shakes spaced 60 s apart on day 3 after one training day of tells. Metric: share of shakes that start the `trance` (marquee shown) vs the hop reflex. Predicted 0.85 vs 0.1. Also checks the cooldown (no two prophecies within 20 s).

**E4 Voice is heritable and mutates.** 200 clutches from one parent: share of eggs whose composed sample line (same canned vision) differs from the parent's by at least one voice field. Predicted at least 70%, and under 100% (some mutations hit other genes). `describeDiff` names the changed voice field.

**E5 Topics are heritable.** Two founders, mask {food} vs {hand}, a home with both a 17:00 meal and a 17:30 cuddle for 3 days. Shake at 16:50 on day 4. Metric: topic of the first prophecy. Predicted each names its own topic in at least 80% of seeds. Falsified if both name the stronger expectation regardless of mask.

**E6 Curiosity is learning progress, not surprise.** Arms: A, the owner tilts the marble in a repeating left-right-left pattern; B, a random pattern with the same energy; C, no marble play (all days 1-5). Metric: share of waking time within 0.4 of the marble, per day, and `eureka` glints per day. Predicted A rises to a peak on days 2-3 then falls under 60% of its peak by day 5, B stays below 60% of A's peak throughout, C is lowest. A raw-surprise curiosity would make B the highest, so the scenario fails it.

**E7 Clicker pairing.** T: click then pellet 2 s later, 10 a day, days 1-2. C: yoked random times. Day 3, hungry (no meal for 3 h): one click, no pellet. Metric within 8 s: `beg` share, distance to the centre, `blep` face share. Predicted `beg` share about 0.6 vs 0.1.

**E8 Shaping and chains.** T: a reactive trainer clicks and pours whenever he does `hop_circles` while hungry, days 1-3. C: yoked. Day 4 hungry, no clicks. Metric: `hop_circles` share in the first hour (T higher). Chain variant: reward only when `foresee` is followed within 6 s by `hop_circles`; metric: P(`hop_circles` within 6 s of foresee), T higher.

**E9 Attachment from responsiveness.** T: days 1-3, every `call` is answered within 10 s with a click and a cuddle. C: yoked (same contacts, times independent of his calls). Day 4: the owner is silent for 3 h, then taps. Metric: seconds from the tap to Rest held 10 s (T shorter, predicted 20 s vs 70 s), calls during the gap, and radius from the centre in the 5 s after the tap (T near the centre).

**E10 Handling style.** T: three days of rough handling (a shake every 40 min, one flip a day). C: gentle (a cradle every 40 min). Equal contact counts. Probe day 4: a plain knock. Metric: curl share and distance fled in the next 5 s (T higher), approach to the centre (C higher). Predicted opposite signs.

**E11 The sensitive period.** 2x2. Exposure: gentle novelty (knocks, tilts, the marble) three hours a day for two days, vs quiet. Timing: starting at hatch vs starting on day 4 (adult). Probe on day 6: mean excursion radius over a waking day and seconds to approach a novel object (the marble dropped in). Predicted early-exposed is bolder than early-quiet by about 0.15 radius, late-exposed differs from late-quiet by under a third of that. The interaction term is the proof.

**E12 Sleep consolidates.** Both arms: days 1-3 he is shaken whenever he starts to chase the marble (the existing `shaken_for_chasing`). T: nights undisturbed, tucked in at 21:30. C: shaken awake at 01:00 and 04:00 each night (needs the `wakes` flag). Day 4 chase share. Predicted T about 0.01, C near the 0.064 baseline. This turns the existing `knownFailing` `shaken_for_chasing_next_day` into a Yes for the right arm. Secondary: the dream glyph at 02:00 on night 3 shows the shake bolt in T (at least 60% of seeds) and not in a no-shaking control.

**E13 Selection works.** Six generations, 20 seeds, three egg policies at every clutch: always the slowest pace hint, always the fastest, random. Metric: lifespan at death from the `Death` entries and clutch sizes. Predicted lifespan fast < random < slow with a monotone trend per generation (slope at least 0.3 days per generation), and fast lines lay more eggs but pass on fewer protected lessons (heirloom and lore counts lower). The second half is the "no free lunch" check.

**E14 Garcia aversion.** T: murk meals on days 1-2 (nausea follows 20 min later by design). C: moss meals, same times and counts. Day 3, 17:00: one murk and one moss pellet, symmetric. Metric: first pellet he walks to, bites first, refusal bouts. Predicted murk first about 0.15 in T vs 0.6 in C (innate taste favours murk, so C is above 0.5), and refusals only in T. The 20-minute delay is the point.

**E15 Superstition.** T: a pellet every 20 min on a fixed clock for two days, whatever he does. C: the same count at yoked random times. Metric on day 3 with pours withheld: in the 60 s before each (virtual) due time, the share of the single most common action (T higher). Report also which action it is: it should differ across seeds (an idiosyncratic ritual). Phone text names it.

**E16 Lore and assimilation.** Parent trained with the clicker (E8) for 3 days, then aged out with the existing "9 days unplugged" device. Child hatches with no training. Metric: on a call, P(trick within 6 s) in the child, vs a child of an untrained parent. Predicted child of trained about 0.35 vs 0.08. Then train again and repeat across three generations; the phone's `DIFF` must show the item assimilated in the third. Without retraining, the grandchild's rate decays toward baseline. Telephone drift across 100 seeds: between 3% and 15% of retold items change cue or action.

**E17 to E21 Games, one each, with yoked rewards as the control.**
- Pocket: goals per 10 minutes on day 4 vs day 1 (T rises, yoked control flat); share of nose actions preceded by a flank (T rises).
- Snap: catch rate by day (T rises), and the snap-before-crossing lead (distance of the fly from his lane at the snap falls).
- Freeze: false moves after click fall across five rounds (T falls, control with randomly timed clicks flat).
- Echo: match rate rises from chance (about 0.25) toward 0.7 over four days.
- Peek: at the moment of `lid_down`, the hood-up and wait share is higher with a fixed 4 s delay than with a random 2 to 8 s delay, and the muddy chip shows only in the random arm.

**Behaviour tests that are not e2e.** `Stations.RestIsHome`. `Gesture corpus`. `Physics.TiltRoll` (30 degrees, centre to rim in 0.8 s +-0.15 s), `Physics.FlickSlosh` (a 0.6 g flick east moves the marble west by at least 0.3), `Physics.SpinFling`. `body_only_full_life` runs a whole life with pour, click and cradle and no phone.

## 8. Phased build plan

Ordered by fun per cost. Every unit ends in a check someone can run. S is under 150 lines plus tests, M 150 to 500, L over 500. "Hash" means the 24-hour replay hash is re-blessed deliberately in that unit and nowhere else.

| # | Unit | What you see | Cost | Check |
|---|---|---|---|---|
| 0.1 | Re-run `learnsim/gates.sh` on the landed depth pass and store the baseline numbers | nothing | S | gates green, baseline file committed |
| 0.2 | Heritable memory span for the recent loci, if depth did not deliver it | a knock is visible for seconds, not half a second | S | a probe knock is still `recent` 3 s later |
| 0.3 | `BodySample` gains gyro; sim `!spin`; `GestureCorpus` with all existing gestures | nothing yet | M | all ten existing gestures pass the corpus; the 10 Hz shake is still undetected |
| 0.4 | Harness: reactive rows, yoked controls, new gesture rows, new tick fields | nothing | M | `ArmsDifferOnlyByTheStimulus` and a yoked-equals-treatment-gestures check |
| 0.5 | Budget lint: `static_assert` on brain bytes and a `sizeof` report; delete the dead `asleep` feature | nothing | S | build fails if W exceeds 12 KB |
| 1.1 | `DishWorld`: one piece pool, 50 Hz raw-accel physics, the marble moves in; delete `Habitat` | **the marble sloshes, rolls and flings** | L | Physics tests above; pellets keep working; hash |
| 1.2 | Stations: the `station` column and the walker | he rests at the centre and crosses to act | M | `Stations.RestIsHome`, hop and eat goldens |
| 1.3 | The Hand: `click`, `call`, card, lullaby; `button` retired; clutch uses click | tap sparkle, status card | M | corpus; clutch cursor test |
| 1.4 | Tip to pour with one flavor, three bins, pour hint | feeding by tilt | M | `body_only_full_life` feeds by pour; a cradle never pours |
| 2.1 | Almanac core (near ring), expectation loci, host test that it learns a cue | nothing yet | M | learns "double knock then shake" in at most 20 trials; hash |
| 2.2 | **Vision**: chips above him in Foresee, gold burst and grey smoke, seer pips | **he shows what he sees** | M | struct and frame goldens; chips only above floor and in mask |
| 2.3 | `appraisal` gene, braced shake, `foresight` gene | predicted shakes stop making him hop | M | E1 |
| 2.4 | Far ring, six dayparts replace sin/cos, `contact` and meal expectation | he foresees supper by the hour | M | E2 minus the oracle |
| 3.1 | **Oracle**: `oracle` gene, `trance` reflex, composer, claims, marquee contract | **shake him and he prophesies** | L | E2, E3 |
| 3.2 | `voice` gene, topic mask mutation, `describeDiff`, egg sample lines | lineages sound different | M | E4, E5 |
| R | **RAM gate**: first board boot measures free heap and PSRAM; if free is under 24 KB, banded canvas | nothing | M | minimum free heap at least 24 KB through a save and a death |
| 4.1 | Anticipation and `hope`, `beg` action | a click alone makes him beg | M | E7 |
| 4.2 | `prev_<a>` loci, credit across actions, trick book | tricks and chains | L | E8 |
| 5.1 | Games registry, `play` action, invites, `GameLog`, a stub game | he proposes a game | M | a stub game's outcome reaches a drive |
| 5.2 | Pocket | marble golf with a frog | M | E17 |
| 5.3 | Snap and the fly food | flies, tongue, lead | M | E18 |
| 5.4 | Freeze, Echo, Peek | three more | M each | E19 to E21 |
| 6.1 | `Journal`, salience, protected bits, dream glyphs, `dreamer` gene | the dream bubble | L | E12; hash |
| 7.1 | Progress, `eureka`, `curiosity` drive | a `!` when he gets it | M | E6 |
| 7.2 | Traits, `plasticity`, `Marks` plumbing | boldness shows in stations | M | E11 |
| 7.3 | `bond`, `wary`, `gap`, reunion | clingy, secure or avoidant | M | E9, E10 |
| 8.1 | `lifehistory`, pace hint on the clutch, closed-form lifespan | fast and slow eggs | M | E13 |
| 8.2 | Foods, `taste`, Garcia via the Far ring | he refuses murk | M | E14 |
| 8.3 | `Nest`: lore, marks, retell, assimilation, the elder's shade; `culture` gene | the shade teaches the baby | L | E15, E16 |
| 9 | Phone verbs, `prime` twist, website views of the vision, trick book, lore tree | richer phone | M | golden transcripts |

The first three phases are the playable core (units 1.1 to 1.4 and 2.2 to 3.1 are the ones a visitor feels). Unit R gates everything that grows the brain: units 4.2 onward spend RAM, so the gate must have run.

## 9. Budgets

Baseline from DESIGN section 8: 327,680 B internal DRAM, **peak 265 to 295 KB, so 32 to 62 KB free** with the 115 KB canvas, PSRAM unverified. Measured numbers are marked; everything else is arithmetic or an estimate.

| Item | Size | Label |
|---|---|---|
| Brain `W` at F=40 features, A=14, D=9 | 10,080 B (the `static_assert` cap is 12,288) | arithmetic |
| Brain episodes at F=40 | 3.2 KB (1.4 KB if stored as 8-bit) | arithmetic |
| Brain total, from 5,576 B | about 13.6 KB (+8 KB) | measured baseline, scaled |
| Almanac, 2,188 B at F=20 | about 4.1 KB at F=40 (E 1.3 KB, rings 2 x 1.3 KB, rest 0.2 KB) | measured, scaled |
| Journal 322 B, protected bits 220 B | about 0.95 KB at F=40 | measured, scaled |
| Traits, Nest, GameLogs (5 x 136 B) | 10 B, 38 B, 720 B | measured |
| DishWorld (8 pieces, hopper), replaces Habitat | 276 B (net about +0.1 KB) | measured |
| Appearance additions (vision, thought 57 B, card, shade, eggs' sample lines 3 x 57 B) | about 0.5 KB | estimate |
| **New static RAM** | **about 14.5 KB** | arithmetic |
| New heap at the save peak (brain and almanac chunks) | about +3.5 KB over 16.9 KB, so about 20.4 KB, which is over `Budget.*`'s 20 KB cap; the cap moves to 24 KB in unit 0.5 | estimate |
| **Free after v2, worst case** | **32 - 14.5 - 3.5 = 14 KB; best case 44 KB** | arithmetic |

The worst case is under the 24 KB gate in the main design. Three levers, in order: (1) 8-bit episodes (-1.8 KB); (2) the banded canvas, 40-row bands (23 KB each, 46 KB double-buffered) instead of 115 KB, which frees about 70 KB at some CPU and more draw code, and unit R decides it before any brain growth; (3) PSRAM, if the board has it, moves the canvas and the rings there. The brain's `static_assert` budget fails the build if a registry row would push `W` past 12 KB, so the lesson is in the structure, not a comment.

**Compute at 240 MHz, all estimates.** Almanac predict at 10 Hz: 13 targets x about 30 cues is 400 multiply-adds a tick (about 25 k cycles/s). Almanac 1 Hz update about 1,000 multiply-adds. Brain think at 5 Hz: 40 x 14 x 9 is 5,000 multiply-adds (about 100 k cycles/s). Physics at 50 Hz: 8 pieces plus 28 pair tests, about 2.7 k cycles a sample (135 k cycles/s). Journal and dream steps are a scan per dream every 8 s. Total added under 0.5 M cycles/s, about 0.2% of one core. Drawing stays the cost (about 9% at 25 fps); the vision chips, bubble and shade add about 5 draw calls.

**Flash.** 24 one-bit glyphs of 16x16 (768 B), the voice phrase tables (about 6 KB), pellet and piece colours from the palette, no new body art (the shade is the body dithered at half alpha). Keepsake: new chunks `Almanac`, `Journal`, `Traits`, `Nest`, `World`, `Games` (tags 12 to 17); the `Habitat` chunk is read for old saves and not written, so `kFormatVersion` becomes 2 with a `keepsake_v1.bin` migration fixture. A v2 save in v1 firmware boots `ReadOnlyNewer`.

## 10. Red-flag screen, tradeoffs, alternatives, and the rest

### Red-flag screen (design-red-flags.md)

- **Shallow module.** `Almanac` (four calls, hides NLMS, rings, horizons, EMAs), `DishWorld` (two calls), `Games` (generated dispatch, a game cannot write a drive). The `Hand` is three small detectors; its depth is the corpus lint. Not shallow.
- **Information leakage.** The risk is the Almanac and the brain sharing `FEATURES`. That is one source of truth (cues are derived, not listed twice), and the Almanac zeroes its own outputs from its cue vector so it cannot predict from its prediction. The closest call is `ProtectedCells` living beside `W` and written by `Journal`. Fix to apply in unit 6.1: the bits are a `Brain` member changed only by `Brain::protect(cell)`, so one module writes memory.
- **Temporal decomposition.** I grouped by knowledge, not by phase: expectation is one module used in five places, not "observe, then predict, then appraise" modules. `Nest` owns lay, retell and teach together because they protect one invariant (lore ages and fades).
- **Pass-through.** `Games::step` is a generated dispatch like `Behaviours`. Nothing else forwards.

### Tradeoffs accepted

- We accept a linear expectation table (no hidden units) in exchange for a 4 KB predictor the phone can print and a vision that is honestly what he expects.
- We accept that the brain grows from 5.6 KB to about 13.6 KB in exchange for tricks, tilt and time-of-day. The budget is a compile error, and the banded canvas is the escape hatch.
- We accept that games carry their own tiny skill tables in exchange for keeping the brain's `W` from multiplying by game, at the cost of two learners (sharing one update function).
- We accept that the owner can spill food by tipping the dish while carrying it in exchange for a one-gesture, menu-free, physical feeding that picks the flavor.
- We accept click-on-release (about 100 ms) in exchange for a hold and double-tap vocabulary on one button.
- We accept lore probation in place of instant heirloom genes, so a custom needs three confirmed generations before it hardens. It makes culture slower and rarer, and keeps one lucky coincidence from locking into a lineage.
- We accept that prophecies can be wrong and sheepish in exchange for a seer whose record means something. A frog that is always right would be a lie.
- We accept more registries (five new `.def` files) in exchange for one-row additions of games, foods, pieces, traits and topics.

### Alternatives considered

- **A deeper brain (a small neural net or an actor-critic with a value function).** Better credit assignment in principle. Lost: the table is legible, heirlooms fall out of it, the phone can print it, and a net costs more RAM than the 8 KB I spend. The Almanac plus a two-step credit queue buys the same behaviours for these cases at a fraction of the cost.
- **Ten separate subsystems, one per axis, each with its own state and registry.** Easiest to build in parallel. Lost on information leakage and depth: expectation would be reimplemented in curiosity, bracing, begging, the oracle and taste. One Almanac with five readers is the shape that makes the axes cohere.
- **Game "modes" with full-screen UIs and a menu.** Cleaner for a gamer, but it breaks the dish as the one scene and adds a navigation problem to a one-button device. Lost to pet-initiated in-dish invitations that the owner accepts with a tap.
- **BOOT as feed with long and short presses for other actions** (the minimal change). Lost: it keeps feeding on the one precise input and leaves the owner no way to talk to him.

### Synthesis decision

Left for arena.

### Implementation reconciliation

Empty until implementation starts; record accepted deviations and their acceptance sources here, and update sections 2 to 4 with them.

### Open questions and risks

1. Does the QMI8658 gyro read cleanly at 50 Hz on the 1.28 board, and which rim point is the hopper's "north" relative to the USB-C port in the printed case? Unit 0.3 and the first board boot answer both. If the gyro is unusable, spin is dropped and the marble loses its orbit but nothing else.
2. Is accidental spill from tipping while carrying acceptable, or should the hopper latch while `picked_up` is high and unlock after a settle? I assumed spills teach.
3. Should six `daypart` bins replace the sine and cosine, or should both stay? I assumed replace (net +4 features) to make room under the 40-feature cap.
4. SOUL.md's Never list forbids illness language. Garcia nausea is "tummy burbles" in my wording. Does the user want that axis at all, given death is plain but illness was cosy?
5. Does lore probation replace the depth pass's immediate heirloom genes, or run beside them (a short fast path for very strong lessons)? I assumed replace, with the delta-ranked list as the candidate set.
6. How many shakes should be prophecy? The founder's default chance is a guess; I set 0.25 with a 20 s cooldown. It should be tuned by the user holding the device.
7. What does the `thoughts` branch's marquee carry (length, speed, interrupt behaviour)? I assumed a bounded string with a one-pass scroll.
8. Will pet-initiated game invitations feel like a nag? A `social` drift and the feel of `ignored` (three ignores raise the interval) are my mitigation, untested.
9. The RAM gate. If the board has no PSRAM and the NimBLE host is large, the banded canvas is mandatory before unit 4.2.

### Next implementation step

Build unit 0.3 and then 1.1: put the gyro into `BodySample` with the gesture corpus green on today's gestures, then land `DishWorld` so the marble is driven by the raw accelerometer, which is the first thing the owner will feel.
