# Blorbarium alife v2, final design: the oracle loop

**Headline.** Grungo is a seer, so v2 gives him one learned model of what happens next, the **oracle**. It learns from whether things happen, so it keeps learning while he is content. Everything the user asked for reads that one table:

- what Foresee shows (a vision bubble with a real pictogram and a time window);
- what a shake makes him say (a grounded 8-ball prophecy in his heritable voice, then judged kept or broken);
- what he braces for (a foreseen shake gets no hop);
- what makes the bell a clicker (it pays only because it predicts food);
- what he begs for and what he is superstitious about;
- when he expects you (a day map of your quarter hours);
- what his line believes (lore genes, confirmed or faded by each life).

Around it, BOOT becomes the **bell** (a press), the **game wheel** (two presses) and **food** (a hold). The marble becomes a real 50 Hz ball. Five registry games give him things to get better at.

Base: candidate B ("the oracle loop"), with grafts from A and C (`SYNTHESIS.md`). The build order is in `BUILD-PLAN.md`.

**The sketch compiles.** `sketch/check.sh` (WSL `survivor`) takes the engine headers from `origin/engine` (a1cb726) with `git archive`, puts this folder's registries in front, and compiles `usage_check.cpp` and each v2 header alone with `-Wall -Wextra -Werror=narrowing`. It then plants five bad rows, and each fails the build with its own message. The checked rows are a duplicate gesture route, a rerouted BOOT hold, an omen on an improv topic, an action with no station and a game capturing the shake. Last, it prints sizes. Every number marked "measured" below comes from that run.

---

## 1. Usage

### 1.1 The owner's quickstart (design fiction, ask 21)

- **He lives in the middle of the dish.** He walks out across the face to do things: to the bell to beg, to the marble to chase it, to the rim to hide, to the pond arc to keep goal. Then he walks home.
- **Press BOOT and a little bell rings** at six o'clock. At first it means nothing to him. **Hold BOOT for a second and a pellet drops by the bell.** Ring, then hold, every meal, and within days the bell alone brings him to the rim, eager.
- **Ring the moment he does something you like, then feed him.** He does it more. A knock is how you ask for a trick, and a double knock is praise.
- **Tilt the dish and the marble rolls** like a real one. A knock flicks it and a shake rattles it.
- **Shake him and ask.** Usually he hops. Sometimes he goes still, his eyes ring teal, and a bubble rises with what he really expects, such as a hand knocking and "+ 9 MIN". When it comes true the bubble returns with a gold check. When it does not, it comes back smudged. Shake him too often and the bubbles get tired for an hour.
- **Press BOOT twice for the game wheel.** Tilt to pick, then knock or press to play. Or wait. When he is bored he walks to the middle and shows you the game he wants, and a bell says yes.
- **He learns your day.** Feed him at the same time and he waits at the bell before it. Lend him to a friend and he notices the rough hands.
- **Each egg is a new run.** The hatchling carries the family's looks, voice, oracle and beliefs, a little mutated. A belief the new life keeps confirming lasts. One it never confirms fades.

### 1.2 The firmware (unchanged surface)

`src/main.cpp` keeps its calls: `Dish(...)`, `sample`, `tick`, `appearance`, then the paint call. The marble's physics runs inside `Dish::tick` from a trace that `Dish::sample` fills, and the oracle, the games and the bond run inside `Dish::tick`. Nothing new reaches the shell.

### 1.3 The creature (`sketch/usage_check.cpp`, compiled)

```cpp
// Step 2: a shake lands. One draw whatever happens, so replays never fork.
const bool speaks = rng.chance(shakeOdds(seer.oracleChance, locus[locus::oracle_gate.v], asleep));
if (speaks) { oracle.prophesy(seer, effects, drives, dayBin, tick, Source::Shake, rng); locus[locus::trance.v] = Fx::one(); }

// Step 4b, after chemistry: the oracle looks ahead and writes its loci.
const OracleOut o = oracle.step(cues, omensIn(routed), drives, dayBin, tick, seer);
for (const TopicInfo& t : TOPICS) if (t.grounded) locus[t.expect.v] = o.expect[t.id.v];

// Step 5: the brain decides with an outlook, its only bridge to the oracle.
Outlook out; oracle.outlook(seer, effects, drives, out);
```

### 1.4 The brain's one change (`strengthening`, static_asserted)

```cpp
observed[d] = (driveNow - driveAtStart) + strengthening(anticipatedAtStart, anticipatedNow);
```

Only the part of an anticipation that grew counts. The bell's promise is credited to whatever he was doing when it rang. When the pellet lands, the promise relaxes, and that relaxation is not charged back, so eating an expected pellet keeps its full value.

### 1.5 The genome author

```cpp
g.b.append(OracleGene{64, voice::mystic, {150, 110, 90, 160, 40, 40, 40, 120, 90, 140, 130}}, kGene);  // 1 shake in 4
g.emit(locus::expect_shaking.v, brace, unit(450), timeByte(4 * kSecond), 0);   // he braces for a shake he foresees
g.react(brace, 1, adrenaline, 1, kNone, 0, 1 * kSecond);                      // brace eats adrenaline: no hop
g.stimulus(stim::shake, 0, oracle_tired, 200);                                // five shakes in a minute ...
g.receive(oracle_tired, locus::oracle_gate.v, unit(-255), 255, 0, 0);         // ... close the oracle for about an hour
g.stimulus(stim::bell, 0, kNone, 0);                                          // the bell is born meaningless
```

### 1.6 Adding things

- **A foreseeable event** is one `omens.def` row, one glyph and one `thoughts.def` row.
- **A game** is one `games.def` row and one struct in `games.h`.
- **A gesture meaning** is one `gestures.def` row and one trace in the gesture corpus.

`v2_registry.h` refuses each of these mistakes at compile time:

- a duplicate route, or any route for the BOOT hold;
- an omen naming a missing stimulus, or sitting on an improv topic;
- a grounded topic with no omen, or a topic past the oracle gene's frozen width;
- a game capturing a drop, a shake, the lid or the hold;
- a Skill table that does not fit;
- an action with no station.

The extension table is section 10.

---

## 2. Problem

The engine reads well but learns almost nothing that lasts. Of 11 controlled experiments, 3 show learning, and no lesson survives a night (`e2e-learning.md`). The causes are structural (`learning-depth.md`):

1. Reward is a drive falling, and boredom, loneliness and need-touch sit at zero, so play teaches nothing.
2. Nightly forgetting is a flat 0.137 per weight.
3. A stimulus is visible to the brain for about half a second.
4. Nothing predicts anything. Foresee is a boredom pump that shows a glow.

The `depth` branch fixes the balance (u1), proportional forgetting and heirlooms ranked by learned change (u2), rot and tilt situations and a time lesson (u35), and interrupting stimuli with a heritable memory span (u4). v2 treats those as landing and does not redo them. What depth cannot give him is a model of the world. Without one he cannot anticipate, be surprised, be clicker-trained, show a true vision or speak a true prophecy.

**Facts that changed since the candidates.**

- **The board.** It is the Waveshare ESP32-S3-LCD-1.28 with 2 MB of PSRAM, measured on the device. Flash is 16 MB, with a 2 MiB raw memory-mapped `art` partition (`tools/partitions_16mb.csv` on `hw-unit20`). All three candidates budgeted for no PSRAM. Section 8 re-budgets.
- **The gyro.** It is not confirmed working (unit 20 is fixing an IMU read bug). No core unit depends on it.
- **The in-flight branches collide.** The `thoughts` branch ships `OracleGene` at type 0x14 with 7 topic weights and a voice byte. The `depth` u4 worktree ships `MemoryGene` at 0x14 too. The `dialogue` branch numbers its voices differently from `thoughts` (`mystic` is 0 there and 5 in `thoughts`) and adds topics 7 to 9. Build unit U0 settles all three before any of them lands.

**Constraints honoured.** The engine stays integer (Q8.24 and Q15) and WASM-clean, so host, device and website replay bit for bit. Every extension is a registry row plus a handler. Gene layouts are frozen. Body-only play is complete. The board is the single writer. Basic food is body-only. Grungo's voice follows SOUL.md and the Never list.

---

## 3. Shape

### 3.1 The prediction core: data shape (`sketch/blorb/oracle.h`)

```
E[cue][omen] = P(omen fires within the horizon | this cue is on)     Q15, 65 x 16 (measured), 2,080 B
```

- **Cues (65, derived, never listed).** These are every brain feature except the expect band (31 - 6 = 25), then the oracle-only cues in `cues.def` (12), then a one-hot of the current action (14), then a half-strength one-hot of the previous action (14). The action tokens are what let him learn that his own behaviour brings things about. That is the source of begging, rituals and two-step tricks.
- **Omens (16, `omens.def`).** Each row is a stimulus he can foresee, its topic and its pictogram. The rows are knock, pat, bell, lift, cuddle, shaking, tumble, bump, pellet, fly, marble, games, tuck-in, dusk, visitor and phone.
- **Topics (11, `topics.def`).** This is one table shared by the thoughts marquee, the dialogue generator and the oracle. Ids 0 to 6 are the `thoughts` topics, 7 to 9 are `dialogue`'s, and 10 (`play`) is v2's. Eight are **grounded**. Each grounded topic has omens, an `expect_<topic>` locus (6 of them are brain features) and a row in the day map. Three are **improv** (doom, luck, pond). A shake draws from those only when nothing clears his bar ("the bubbles see azerbaijan").
- **Learning.** Every beat (0.5 s in the starter) the 12 strongest cues are snapshotted into a 16-slot ring. When a snapshot is `horizonBeats` old (6 s in the starter), each omen's target is 1 if it fired inside that window and 0 if not. E moves toward the target by normalised LMS. A cue visible for half a second is still credited six seconds later. Each target is a window label, not a per-tick hazard, so an event every 20 minutes does not underflow Q15. That underflow is a defect in candidate A's per-tick rule, found independently by the cross-judge and by me.
- **Forgetting.** At pet dawn, each cell is multiplied once by `1 - dawnForget`. There is no round-up, so a lesson repeated daily holds steady. A saved day index makes a second dawn for the same day a no-op, so the unpowered catch-up can call it freely.
- **The day map.** `routine[96 quarter hours][11 topics]` is a byte EMA of "this topic happened in this quarter hour", closed at dawn. Its current bin and next two bins (30 minutes ahead) feed `expect_<topic>`. A 07:40 breakfast raises `expect_food` from about 07:15. The day map predicts in quarter hours what E predicts in seconds.
- **Prophecy.** A prophecy is one committed `Claim`, which is either an omen firing inside a window, or a drive above a level when the window opens. The second kind is how the phone's "tomorrow, 2pm: a hungry frog" can be checked. A prophecy also records its source (Foresee, Shake or Phone), a confidence and a window. `prophesy()` scores `topicWeight * (p + wishfulness * value+)` over the near predictions and the day map's next two bins. Under `bar` it is ungrounded, which means an improv line with no claim, never judged. While one prophecy is active, a second call returns it unchanged.
- **Judging.** If the omen fires or the drive is above its level inside the window, `prophecy_kept` fires. If the window closes first, `prophecy_broken` fires. Each verdict goes into an 8-entry `ClaimRecord` log (the phone's `PROPHECIES`), and life totals (`made`, `kept`) go into the Death entry.
- **Surprise and let-down.** An omen that fires with p under 0.2 fires `self_surprised` and raises the `surprise` locus by 1 - p. A window with p of 0.6 or more that closes empty fires `self_let_down`. His stimulus genes decide what these do to him.
- **Dream memories.** The oracle keeps the day's 8 most surprising snapshots. Asleep, each dream replays one into E and shows its omen in a purple bubble.

**The bridge into the old brain** is one struct, `Outlook`, built at each decision.

- `actionBias[a] = forethought * sum_omen E[token a][omen] * value(omen) + curiosity * uncertainty(a)`. `value` comes from `OmenEffects`, which is what each omen's stimulus genes do to his drives, weighted by how pressing each drive is now. It is derived from the phenotype once per stage and is never learned. If he happened to hop twice before three pellets, hopping now scores up when he is hungry. That is the lucky double hop.
- `anticipated[d] = sum_omen p(omen) * effect(omen, d)`. Brain credit uses `strengthening` (section 1.4).
- **The regression test.** With `forethought` 0 and no omens, the Outlook is all zeros, and every existing brain test passes unchanged.

### 3.2 Who owns what (one writer each)

| State | Owner, single writer | Saved |
|---|---|---|
| `W`, episodes, habits | `Brain` | yes |
| `E`, day map, prophecy, claim log, dream memories | `Oracle` | yes; the ring and `p_` are not (a reboot drops at most 6 s of windows in flight) |
| chemistry, loci | `Creature` applies `OracleOut`; the oracle never writes chemistry | yes |
| marble, pellets, flies, Bond | `World`; games only `push` and ask | yes, in the world chunk, so a death never erases them |
| per-game `Skill` | `Games`, through `learnMove` only | yes |
| his body | reflex, then game ask, then behaviour, by code order in `Creature::tick` | no |

### 3.3 Tick order (additions in bold)

`Dish::sample` (50 Hz) runs the detectors, now with a **double-press detector**, and **appends the raw accelerometer to the `SampleTrace`**.

Each `Dish::tick` (10 Hz):

1. `PetClock::advance`, then the tick-rate detectors.
2. **Route raw stimuli by mode** (`gestures.def`). **A BOOT hold makes the world drop a pellet.** **`Games::step` runs the ambient row and the active game and returns its capture mask.** **`World::step` drains the trace** (marble and pellet substeps, refill, rot, flies, the world's loci). **`Bond::observe`** runs.
3. The occupant ticks. For a creature:
   - sense loci;
   - stimuli through his stimulus genes, where **a shake while awake rolls the 8-ball first**;
   - chemistry, where brace eats adrenaline;
   - lifecycle and reflexes, where **trance** comes before hop;
   - **4b, the oracle step**;
   - the brain, which thinks with an **Outlook** awake, and while asleep dreams in the brain **and the oracle**;
   - the body, where a reflex comes before a **game ask**, which comes before the behaviour walking to its **station**;
   - face, stats and recent loci, which fade at depth's memory half-life.
4. Protocol and saves. **At the dawn stimulus the oracle forgets and closes the day once per day index.**

### 3.4 What the interface hides

The creature makes one oracle call per tick (`step`), one per decision (`outlook`) and one when he scries or is shaken (`prophesy`). Behind them sit the beat cadence, the ring, delayed-outcome learning, the day map, surprise scoring, claim judging and vision state. The phone reads them through one verb (`EXPECT`), formatted only in `protocol.cpp`, per boundary-discipline. The phone writes one twist (`prophecy`), parsed in `twists.cpp`, which calls `accept` with a well-formed `Prophecy`.

### 3.5 What v2 deliberately does not do

- **No conjunctions inside the oracle.** "Calling at 17:00 brings food" is two additive beliefs.
- **No new drives.** Pride, dread, appetite, zest and wariness are chemicals and genes.
- **No second voice gene.** Voice, topics and the 8-ball's chance stay in thoughts' `OracleGene`.
- **No second W timescale, no TD value function, no per-game neural net.**
- **No free-text generation on the device.** Lines are the thoughts and dialogue tables, generated offline.

---

## 4. Alife axes

Each axis lists what develops, the mechanism, what you see on the 240 px dish, how it is inherited, and how the owner steers it. Section 9 has the proof for each.

### A1. Foresight: what he expects, and bracing

- **Develops.** Which events follow which situations and which of his own actions, within seconds. That is 1,040 learned expectations a life (65 x 16).
- **Mechanism.** E and its `expect_<topic>` loci. Emitter genes wire expectation to chemistry. The starter has **brace** (expect_shaking makes brace, and a reaction with brace eats adrenaline, so a foreseen shake gets no hop), **appetite** (expect_food raises a little hunger, so he goes to the bell) and **dread** (expect_danger raises fear, so the existing fear-relieving curl runs before a drop).
- **On screen.** Foresee always shows the bubble while it runs. Its pictogram's opacity is the confidence, and a sand ring runs down to the deadline. Mist means nothing clears the floor. Before a predicted shake his hood goes up, and the shake gets a flinch, not a hop.
- **Inherited.** `SeerGene` (beat, horizon, learn rate, dawn forgetting, forethought, curiosity, wishfulness, bar, routine rate). Brace, appetite and dread are ordinary genes, so a line can lose its brace. Omen lore seeds E at hatch (A8).
- **Owner.** Be predictable and he sees you coming. A deliberate tell ("I always double knock before I shake") becomes his vision and his brace.

### A2. The seer's voice: the 8-ball, judged

- **Develops.** What he prophesies about, how honestly, in whose voice, and his record of hits for the life.
- **Mechanism.**
  - A shake while awake gives a prophecy with odds `OracleGene.chance * oracle_gate`. Otherwise the shake's chemistry runs as usual: a hop, or a flinch if he was braced.
  - **Oracle fatigue** (story 2). Each shake adds `oracle_tired` (half-life about 20 minutes). A receptor closes `oracle_gate` as it builds, so five shakes in a minute switch the oracle off for about an hour ("the bubbles are tired. ask later, brother."). The shake's discomfort is still punishment, so he learns shaking is unpleasant.
  - **A grounded prophecy** names his real top omen. Its line is one `thoughts.def` row per omen, with the window in minutes in place of `#` ("A KNOCK. # MIN. IT IS WRITTEN."). It is framed by his voice. A hedged frame is used when confidence is under 0.5 (a `hedge` column on `voices.def` frames).
  - **An ungrounded prophecy** draws an improv row by the gene's weights for doom, luck and pond.
  - **After a hop he did not foresee,** a thoughts row may claim he knew ("eep! ...grungo knew you would do that"). The phone's claim log marks that boast as unpredicted.
- **On screen.** The trance reflex holds him still, ringed in teal, with the bubble up and the line scrolling. On a kept prophecy the bubble returns with a gold check and a hop in a circle. On a broken one it returns with a smudge. A rim tally shows hits this life.
- **Inherited.** `OracleGene` (chance, voice row, 12 topic weights). The voice row includes the dialogue branch's hybrids, and a point mutation can jump it to a neighbouring row. The clutch preview prints one sample line per egg.
- **Owner.** Egg picks select the chance, the voice and the topics. Fulfilling prophecies feeds his pride (`prophecy_kept`: boredom down, fondness up). An honest line is right more often. A wishful line gets fed more when you play along.

### A3. The bell, tricks, begging and superstition

- **Develops.** A bell that means food, tricks on a cue, two-step chains, begging, and rituals that are wrong.
- **Mechanism.**
  - The bell is born with no stimulus-gene amounts.
  - Ring, then hold, and `E[recent bell][pellet]` rises. The anticipation it raises is credited to whatever he was doing when it rang (`strengthening`). That is secondary reinforcement, the clicker.
  - The trick cue is a **knock**: it interrupts (depth u4) and stays visible for his memory span. The **double knock** is praise.
  - Action tokens as cues let E learn "after I beg at the bell, a pellet comes" and "after I hop twice, a pellet comes". `actionBias` turns those into begging and the lucky hop when he is hungry.
  - NLMS extinguishes a belief that stops paying.
- **On screen.** At the bell he waddles to six o'clock and waits with his eager face. On a knock he does the trick. Before breakfast he does his ritual double hop. The phone names it: "gen 4 believes hopping brings pellets".
- **Inherited.** `SeerGene.forethought` (a planner, or a creature of habit). Strong beliefs become lore (A8). Strong W lessons become depth's learned heirlooms.
- **Owner.** Pair the bell and the pellet first, then ring right after the behaviour you want. It works best before meals, as with a real animal.

### A4. Your day and your hands

- **Develops.** When you come, how reliably, and whose hands these are.
- **Mechanism.**
  - **The day map** (section 3.1). `expect_food` and `expect_owner` rise before your usual times. The `beg` action at the bell station pays when a pellet drops, so waiting at 07:36 for 07:40 emerges. A let-down (expected, nothing came) feeds loneliness through a gene.
  - **Bond** lives in the World, so it outlives each grungo. It keeps a print of your gesture mix (knock, double knock, shake, cradle, bell, feed hold, tilt play, drop) and your usual gap. It writes `familiar` and `since_contact`. It fires `reunion` after a gap 1.5 times the usual one, including across an unpowered catch-up, and `strange_hands` for a session far from the print.
  - **Bog sleep** (story 5). Hours of dark and stillness make `torpor`, which slows the hunger tonic. The catch-up treats an unpowered gap as dark and still, so a drawer trip is survivable but costs him.
- **On screen.** He waits at the bell before your usual meal, and calls if you are late. He hops in a circle at a reunion. With a friend's rough hands he wears a wary face he never shows you. After a drawer trip he wakes thin and cross.
- **Inherited.** Separation sensitivity, reunion joy and stranger fear are stimulus and emitter genes. Your hours pass on as Routine lore, which becomes a superstition if the next owner differs.
- **Owner.** A steady routine, your own way of handling him, and whether you lend him out.

### A5. Play, skill and curiosity

- **Develops.** Skill at each game, a favourite game, and appetite for what he cannot yet predict.
- **Mechanism.**
  - **Skill.** Each game plays and learns only through `chooseMove` and `learnMove` on its `Skill` table (contexts x moves, at most 16 x 4). Keeper's context is the marble's heading times the tilt's direction, so he learns to read your wrist, not the marble.
  - **Staleness.** A stimulus-gene flag bit (bit 2 of the frozen `flags` byte, no layout change) scales play relief by `0.5 + 0.5 * surprise`. Once the oracle predicts a game's outcomes, the game stops paying.
  - **Curiosity.** `SeerGene.curiosity` adds `uncertainty(a)` to `actionBias`. Surprise emitter genes decide neophile (zest) or neophobe (fear).
- **On screen.**
  - Saves climb across sessions, and the score shows on the rim.
  - When bored he walks home and shows his favourite game's glyph (the `invite` action). A bell accepts.
  - A solved game gets "grungo has seen this one" and he walks back to the middle.
- **Inherited.** Knack lore (A8) seeds Skill cells. `TalentGene` holds tongue reach, tempo and copy fidelity.
- **Owner.** A new game or a sneakier Peter keeps him engaged. The same tilts every day bore him.

### A6. Temperament and the echo of a hard life

- **Develops.** Wariness, fondness and zest over a life, and a nudge to the next.
- **Mechanism.** Three trait chemicals with half-lives of days (`chemicals.def` rows 26 to 28), fed by emitter genes and read by receptor genes. This is candidate A's way, because it is genes only and so heritable, mutable and in `DIFF` for free. B's "forewarned is forearmed" falls out of the genes. Wariness is fed by `surprise` on danger omens, so an unforeseen shake costs more than a foreseen one.
- **The echo.** At death each trait gene flagged `Imprint` moves the child's starting level a quarter of the way toward the parent's final level, capped at ±32 per generation. The move is recorded as an `Imprint` op in the birth diff, so `apply(parent, diff) == child` still holds. (Open question 2.)
- **On screen.** His idle posture (hood loose or pulled tight), the vigour of his bob, and how fast alarmed shows. The lineage says "gen 5 starts warier: its parent was shaken 140 times."
- **Owner.** Your handling, and above all whether rough things come with warning.

### A7. Memory and dreams

- **Develops.** What he keeps from a day, and what he dreams of.
- **Mechanism.** The brain keeps the 16 episodes with the largest drive change, not the last 16 (depth proposal 5, built here). The oracle keeps the 8 most surprising moments. Each dream replays one of each. A dreamed danger omen raises fear and can wake him through a gene on `sleep_gate`.
- **On screen.** A purple bubble over the sleeping frog with the dreamed pictogram. On waking from a nightmare: "grungo dreamt of a great SHAKING."
- **Inherited.** `SeerGene.dawnForget`, pace, and the `dreamer` feat.
- **Owner.** The day's drama becomes tonight's dream. A calm cradle before bed gives sweet dreams. Shaking at bedtime gives nightmares and a warier morning.

### A8. Culture: lore that lasts only if it keeps working

- **Develops.** A family's beliefs, routines, knacks and rhythm, carried and edited across generations.
- **Mechanism.** At death, `layClutch` writes up to the pace's lore slots (2 to 5) of `LoreGene{kind, a, b, c, strength, gen, heard}` (candidate A's shape):

  | Kind | Taken from | Example |
  |---|---|---|
  | Omen | the E cells that moved most from birth, bias cues excluded | "the bell brings pellets" |
  | Routine | the day-map bins that moved most | "breakfast is at the bell hour" (story 1) |
  | Knack | the best Skill cells | "guard the downhill side" (story 3) |
  | Rhythm | the Drum pattern practised most | four croaks, in order |

  At hatch each lore gene seeds its table as one nudge, like an instinct. At each later death it is checked. If the cell is still strong, `heard` goes up and it is re-laid. If not, it fades by a heritable credulity and is dropped below a floor. Rhythms copy at `copyFidelity`, so the family rhythm drifts. Depth's learned heirlooms carry W lessons the same way. A trick taught to a cue mostly does not pass, because its cue is rarely on.
- **On screen.** At hatch: "grungo VII remembers. the bell brings pellets. (since gen 3)". The hatchling walks to the bell at the family's breakfast hour untaught. The family tree shows lore in a different colour from genes.
- **Owner.** Your routines become the family's omens. Change them and the line's belief fades over a few generations, the way an instinct should.

### A9. The dish's flies (open question 1)

- **Develops.** A fly population whose genes evolve against his line, and his hunting skill within a life.
- **Mechanism.** Candidate B's swarm, at most 6 flies, with a 3-byte genome (speed, jink, wariness).
  - A pellet left rotten for an hour hatches a fly. Two flies and food breed one every few hours.
  - Caught flies do not breed, so jink rises under predation.
  - His `hunt` action aims at `fly + velocity * lead`, and each miss moves `lead`.
  - The camera twist spawns a coloured fly. Eating it tints his cloak for the rest of his life. The tint is not heritable, and his card records it (story 5).
- **On screen.** Flies buzz. He blep-snaps, and a catch sparkles. The phone shows the fly generation and mean jink.
- **Inherited.** `HuntGene`. The swarm belongs to the dish, so the arms race spans generations.
- **Owner.** A clean dish has no flies but less free food. A rotten one breeds flies and risks toxin.

### A10. Life-history pace

- **Develops.** Fast lines (short lives, more eggs, quick learners who forget fast and see near) or slow ones (long lives, deep learners, more lore slots).
- **Mechanism.** `PaceGene`, one byte with pleiotropic expression. It multiplies the Life half-life by `2^((128 - pace) / 64)`. It scales hunger, incubation, learn rate, forgetting and horizon. It adds a clutch egg above 176 and a lore slot below 80. Long life, fast learning and a big clutch cannot all be had, because they are one byte.
- **On screen.** Fast lines are small and twitchy, and the generation count climbs. On the clutch screen, each egg pulses at its pace.
- **Inherited.** The pace byte mutates by `Nudge` like any other, so it responds to selection.
- **Owner.** Egg choice, and the care regime. A harsh home favours fast lines.

### A11. Looks that mutate, skip and return

- **Develops.** Warts, a hood mushroom, a tail stub and accessories that run in a family, skip a generation and come back.
- **Mechanism.**
  - `PartGene{layer, a, b, tint}` is a mark layer with **two alleles**. Bit 7 of an allele is dominance, and bits 0 to 6 are the variant.
  - A dominant allele shows. With neither dominant, `a` shows.
  - At the clutch each egg draws one allele from each copy (selfing), recorded as a `Segregate` op. A recessive part therefore shows in about a quarter of a heterozygote's eggs.
  - Parts draw through the existing `mark` seam outside the face box, at about 0.5 KB per part (`sprite-space.md`).
  - Clutch shells are tinted from each egg's parts and palette, so Peter can see the warty egg.
- **On screen.** Parts squash, hop and scale with him. A ring of cloak-coloured dots on the rim shows the line.
- **Inherited.** This is inheritance. Diploidy is only for parts (open question 4).
- **Owner.** Egg picks are the selection pressure.

---

## 5. Inputs

### 5.1 The marble, a real ball (candidate B)

The marble's acceleration is the raw in-plane accelerometer reading, scaled, minus a bowl-centring pull and drag. Tilting rolls it, a knock flicks it, a shake rattles it and a swing sloshes it, all from one rule (`BallPhysics` in `world.h`).

- **Feel.** A 15 degree tilt crosses the dish in about 0.8 s. A level dish settles it near the centre in about 8 s (arithmetic).
- **Desk.** A still device subtracts a 30 s baseline of its own reading, so a desk sloping 2 degrees reads level.
- **Bonks.** A bonk above 0.3 units/s fires `marble_hit` with its strength.
- **Approach.** `marble_coming` (a brain feature) carries the closing speed when the marble is on course for him.
- **Spin.** The spin and Coriolis terms (candidate C) wait for unit 20 to confirm the gyro. They are an extension row, not a dependency.

### 5.2 BOOT: the bell, the wheel and food (one meaning per mode)

- **A press is the bell.** A ripple runs from six o'clock. It fires on the first release, so the clicker's timing is the press length, about 100 ms. Its meaning is whatever it predicts.
- **Two presses** (the second within 400 ms of the first release) open the game wheel. The first press has already rung the bell. That is harmless, and it lets the oracle learn that a bell before the wheel means games.
- **A 1 s hold drops a pellet** by the bell, in every mode where a creature lives. `gestures.def` cannot route it elsewhere (static check). An empty jar rattles and shows a clock. Basic food is body-only: `button_hold`, `pellet_dropped` and `fed` are refused as twists.
- **Tuck-in** moves entirely to the lid: face down and still for 2 s. A flip in a moving hand is never a tuck-in (candidate B's fix to `learning-depth.md` side finding 3).

### 5.3 The gesture table

A blank cell means nothing happens. Raw stimuli come from detectors, routes from `gestures.def`, and captures from `games.def`.

| Gesture | Live, awake | Live, asleep | Wheel open | In a game | Clutch | Egg |
|---|---|---|---|---|---|---|
| Tilt under 35 degrees | rolls the marble, pellets, flies; `tilted` | rolls the marble | points at a game | steering (keeper, snap) or pointer (pads) | moves the marker | rocks |
| Knock | `knock`: the trick cue, a flinch, flicks the marble | genes decide if it wakes him | starts the pointed game | drum: a beat (captured); else `knock` | keeps the marked egg | wobble |
| Double knock | `double_knock`: praise | genes decide | starts the pointed game | drum: a beat (captured); else praise | keeps the marked egg | wobble |
| Shake (4 jolts of 0.55 g in 900 ms) | 8-ball, hop, or a braced flinch | hop, and wakes | closes the wheel | pauses the game through the trance or hop | | wobble |
| BOOT press | `bell` | `bell` (heard if a gene says so) | starts the pointed game | the game's action (snap, hide) | steps the marker | `bell` in the shell |
| BOOT two presses | opens the wheel | wheel shows zzz, does not open | closes the wheel | ends the game | | |
| BOOT hold 1 s | a pellet drops by the bell | a pellet drops (may rot by morning) | a pellet drops | a pellet drops | | |
| Face down and still 2 s | `lid_down`: tuck-in | keeps him asleep | closes the wheel | ends the game | | |
| Flip in a moving hand | `flipped` | `flipped` | | `flipped` | | |
| Pick up, put down | `picked_up`, `put_down`; Bond counts it | same, genes gate | | same | | warmth begins |
| Cradle (held still, 17 to 37 degrees, 3 s) | `cradle` | soothes | | `cradle` | | warms (incubation) |
| Drop | `dropped` (injury) | `dropped` | closes the wheel | ends the game, always reaches him | | injury |

Conflicts resolved:

1. **Knock against tricks.** A knock is the one trick cue. Story 1's "two clicks means spin" becomes "a knock means spin", because two presses open the wheel (open question 3).
2. **Shake against games.** No game may capture a shake (static check). A game pauses through the reflex instead of ending, because hard tilting in keeper can read as a shake.
3. **Cradle against tilt play.** Cradle is capped at 37 degrees and needs stillness. Tilt play moves.
4. **Lid against flip.** The lid needs stillness. A flip in a hand moves.
5. **Drop.** A drop always reaches him and always ends a game.

**The lint (candidate C).** `test_senses` keeps a `GestureCorpus`: one recorded 50 Hz trace per row plus the hard cases. The hard cases are a lift off a desk, a cradle at 25 degrees, a flick that is not a shake, a carry through a doorway, a tap on a face-down dish, and hard keeper tilting. Each trace lists exactly the stimuli it may fire in each mode. A detector change that fires one extra or misses one fails CI, not a hand.

---

## 6. Games registry

`defs/games.def` holds one row per game: id, struct, capture mask, seconds, unlock feat, Skill contexts x moves, and glyph. Every game follows one contract:

- It moves only world pieces.
- It asks his body to go somewhere or do something.
- It reaches his drives only through World stimuli (`game_started`, `game_point`, `game_lost`, `fly_caught`).
- It plays and learns only through `chooseMove` and `learnMove`.

A game cannot write a drive, so every effect on him is a gene he inherits.

| # | Game | Rules | How he plays and learns | What he gains | What the lineage keeps |
|---|---|---|---|---|---|
| 0 | **Marble** (ambient) | The marble is always a toy. No score. | Chases and noses it. The oracle learns that your tilting brings the marble. | play relief, scaled by surprise | chase heirlooms |
| 1 | **Keeper** (story 3) | His pond is the bottom arc. You tilt the marble at it, and he guards a third. First to 5. Real physics, so bank shots are skill. | Context: heading sector (3) x tilt direction (4). Moves: guard left, middle, right. He learns to read your wrist. | boredom relief, zest, fondness | Knack "guard the downhill side" |
| 2 | **Pads** | Tilt to one of three pads, press to hide the fly. He hops onto one. Nine rounds. | Context: your last two hides (9). He beats a patterned owner ("grungo KNEW. the LEFT pad."). | up to 3 small flies; relief | Knack, and an owner-habit Omen |
| 3 | **Snap** (story 3) | A fly loops the dish. Press when it crosses his tongue line. | Context: distance (4) x speed (4). Moves: snap or wait. He learns your timing, then snaps first and better. | each fly takes a little hunger off, so a long game is lunch | `HuntGene` under your picks |
| 4 | **Drum** (unlock: lived a week) | Knock a steady beat. He croaks on the beat he predicts. Captures knocks, so he does not flinch at each beat. | Context: time since your last knock (12 bins). Moves: croak or wait. | boredom and loneliness relief | Rhythm lore, which drifts |
| 5 | **Drill** (story 3's third icon) | Knock (the cue), he acts, you ring, a pellet follows. | No Skill table: what he learns is the clicker in W. | food, and a trick | the trick, as a learned heirloom if strong |

**How a game starts.** Two BOOT presses open the wheel on the bottom rim, tilt points, and a knock or a press starts. Or he invites you: when bored or curious he walks home and shows his favourite game's glyph, and a bell within 10 s accepts. Invitations come at most once per 20 minutes, and three ignored ones double the gap. When a game ends, he walks back to the middle.

**Rejected game rows** (`SYNTHESIS.md` has the reasons): Oracle Pool (B), Chant (A), Pocket, Freeze, Echo and Peek (C). Peek is the worked extension example in section 10.

---

## 7. Presentation

### 7.1 The vision bubble

- **The frame.** One bubble rises above his head on the top arc, clamped inside the disc. It holds one 24 x 24 pictogram from `omens.def`'s glyph column, with opacity equal to the confidence.
- **The time.** A sand ring runs down to the deadline. Under the pictogram, "+ 9 MIN" for a far claim, or nothing for "soon".
- **Kept.** The bubble returns with a gold check. He hops in a circle, and the marquee says "WRITTEN".
- **Broken.** The bubble returns with a smudge: "grungo meant a different knock."
- **Mist.** With nothing above his floor, the bubble shows a slow swirl.
- **Dreams.** A purple bubble with the dreamed pictogram.
- **The halo is the engine's.** The teal ring and sparks around each eye are positioned from per-frame eye anchors and tinted by the glow genes. The `visual-center` work adds the foresee face whenever he scries. A violet line (story 2) is a palette gene.

### 7.2 Moving around the face (ask 17)

Each action names a **station**: Home, Here, Roam, Food, Bell, Rim, Downhill, Uphill, Marble, Fly or Game. One walker reads it. It walks him there at walk speed and back home at an amble when the action ends (candidate C's column).

- Rest, Foresee, Sleep, Call and Invite are Home. Foresee sits just below centre, so the bubble gets the top third.
- Beg is the Bell at six o'clock.
- Curl is the rim away from the last shake.
- Games set their positions through `GameAsk::GoTo`. In keeper he patrols the pond arc. In snap he sits under the fly's loop.

The `visual-center` worktree already walks Rest and Foresee home. The stations unit generalises that work and does not redo it. Test `Stations.RestIsHome`: at least 90% of Rest ticks lie within 0.12 of the centre once 5 s have passed since the last action.

### 7.3 The thought marquee

The `thoughts` branch's marquee carries every line: one pass, its voice's frame, and the time-unknown marquee outranking it. v2 adds rows, not a channel:

- 16 grounded prophecy rows, one per omen, with `#` as minutes;
- an `oracle_tired` row;
- a `braced` row ("...grungo knew you would do that");
- `kept` and `broken` rows;
- hatch-lore rows ("GEN # FORESAW THIS TOO" already exists).

The `dialogue` generator writes the voiced lines for those rows (`thought_lines.def`), so they come in each voice and each hybrid. Every line passes thoughts' static font and length checks and dialogue's blocklist.

---

## 8. Budgets, with 2 MB of PSRAM

The rule is that hot loops stay in internal SRAM. Anything touched at 2 Hz or faster stays there. That covers the detectors and marble substeps (50 Hz), chemistry (10 Hz), brain think (5 Hz), the oracle beat (2 Hz) and compositing (25 fps). Cold, bursty buffers go to PSRAM. The engine stays WASM-clean, so the choice of allocator is made at the board boundary. A cold-buffer allocator in `seams.h` maps to `heap_caps_malloc(MALLOC_CAP_SPIRAM)` on the device and to `malloc` on the host.

| Item | Where | Size | Label |
|---|---|---|---|
| Brain `W` | SRAM | 6,944 B (31 x 14 x 8 x 2), up from 3,520 | measured counts, arithmetic product |
| Brain episodes (16, dense) | SRAM | about +350 B | arithmetic |
| `Oracle` | SRAM | 4,512 B (E 2,080, day map 1,056) | measured, x86-64; no pointers, so the ESP32-S3 layout matches up to padding |
| `Games` (6 rows, 6 Skill tables) | SRAM | 884 B | measured |
| `World` (marble, 6 pellets, swarm, bond, jar) | SRAM | 360 B, about +180 over `Habitat` | measured / estimate |
| `OmenEffects`, `SeerTraits` (phenotype) | SRAM heap | 592 B | measured |
| **New SRAM** | | **about 10 KB** | arithmetic over the rows |
| Canvas 240 x 240 RGB565 | SRAM | 115,200 B, unchanged | measured; stays internal because compositing and the SPI push are the hottest loop |
| Keepsake blob during a save or `SNAPSHOT` | **PSRAM** | about 17.5 KB, up from about 9.5 KB | arithmetic |
| `SNAPSHOT` base64 text, lineage read frames, viability dry runs | **PSRAM** | under 40 KB together | estimate |
| PSRAM used by v2 | | under 100 KB of 2 MB | estimate |

**What this changes.** The candidates' worst case was 14 to 18 KB of free internal RAM, under unit 20's 24 KB floor, and each reached for a banded canvas. Moving the save transient (about 17 KB) and the phone buffers to PSRAM offsets almost all of v2's 10 KB of static growth. So the internal peak stays about where `DESIGN.md` section 8 put it (32 to 62 KB free, an estimate). The banded canvas leaves the plan. `hw-unit20`'s diagnostics already print `heap`, `minheap`, the largest internal block and `psram_free` every 10 s. U4 does not merge until a soak through a save, a death and a clutch pick shows the minimum free internal heap at 24 KB or more. With PSRAM, A's rejected runtime embryo trial (a second Creature) is affordable later. Nothing here needs it.

**Flash.**

- 16 omen pictograms and 2 game glyphs at 24 x 24, 8 bpp RLE: about 10 KB.
- The bubble, bell, jar and fly frames: about 4 KB.
- Parts (A11): about 15 KB (`sprite-space.md`).
- The voiced line table: about 30 KB.

All estimates. The 4 MiB app slot has about 3.35 MB free after a unit-20-sized firmware (measured in `sprite-space.md`). Rare breeds go to the 2 MiB `art` partition when the pack passes about 300 KB.

**Compute** (estimates at 240 MHz):

- The oracle closes one window and predicts each beat: 12 cues x 16 omens x 2, about 400 multiply-adds at 2 Hz.
- The outlook is 14 x 16 x 8, about 1,800 multiply-adds per decision.
- The marble is about 40 operations per 50 Hz substep. Six flies are about 50 operations each at 10 Hz.

Together that is under 0.1% of a core. Drawing still dominates. The bubble adds one 40 x 40 composite.

**Keepsake.** New chunks for the oracle, the world, games and lore. `Habitat` is read for old saves and never written. `kFormatVersion` becomes 2, with a committed v1 fixture, so an OTA never strands a save (ask 14).

---

## 9. Proof: e2e scenarios, control against treatment

These run in the depth branch's harness (`test/support/e2e`), which drives the real `Dish` through the board's seams with 50 Hz `BodySample`s. The rules are fixed before any run:

- **Seeds.** 40 seeds, with the arms sharing seed, care schedule and every jittered row.
- **Verdict.** **Yes** if the 95% bootstrap CI of T - C excludes 0 on the predicted side and at least 75% of seeds move that way.
- **Metrics** read only `STATE` and `Appearance`: pose, position, face, the vision's pictogram, the marquee row id, the dream pictogram and the occupant. No weights are read.
- **Controls.** Candidate C's harness additions are grafted: **yoked controls** (the control arm gets the same gestures at the same times as the treatment's recorded run, with the contingency broken) and **reactive trainer rows** (an owner who acts when he does something).
- **The next-day probe.** Every scenario repeats its probe after a night, because surviving a night is the measured failure. It passes if T - C on the next day is at least half of the first day's.
- **Known failures.** Each lands as `knownFailing` with its unit and must flip there.

| Axis | Scenario | Treatment | Control | Probe and observable metric | Next-day probe | Predicted |
|---|---|---|---|---|---|---|
| A1 | `knock_warns_of_shake` | days 1-3: double knock, shake 4 s later, 6 a day | yoked: same knocks and shakes, independent times | day 4: double knock, no shake. Share of the next 6 s with the bolt in the bubble or hood up | day 5, same | T higher |
| A1 | `braced_shake` | as above | as above | day 4: double knock, then shake. Hop share (hop reflex in `Appearance`) | day 5 | T lower |
| A2 | `prophecy_follows_reality` | pellet (bell and hold) at 17:00 days 1-3; oracle chance 230 | the same pellets at random hours | day 4, 16:50: five spaced shakes. Share of trances whose marquee row is a grounded pellet row | day 5, 16:50 | T higher; C mostly improv rows |
| A2 | `oracle_chance_heritable` | founder chance 230 | founder chance 25 | 20 shakes 2 min apart, day 3: trance share. Then each line's child, day 3 | the child is the next-generation probe | T higher in both generations |
| A2 | `oracle_tires` | five shakes in one minute, then single shakes at +10, +30, +90 min | the same shakes spaced an hour apart | trance share of the single shakes at +10 and +30 min | +90 min recovers to C | T lower at +10 and +30 |
| A2 | `kept_makes_proud` | owner makes each pellet prophecy come true within its window | owner ignores prophecies | days 2-3: share of gold-check bubbles among judged prophecies, and the happy face within 10 s of the verdict | day 4 | T higher |
| A3 | `bell_clicker` | days 1-2: bell then hold 3 s later, 5 meals a day | yoked holds with bells at independent times | day 3, hungry: a bell alone. Share of the next 10 s at the Bell station or begging | day 4 | T higher |
| A3 | `trick_on_knock` | days 3-5: reactive trainer knocks, then rings and holds when hop_circles starts within 6 s | yoked rings | day 6: 20 probe knocks. Share with hop_circles within 6 s | day 7 | T higher |
| A3 | `lucky_hop` | a pellet every 20 min whatever he does, days 1-2 | the same count at yoked random times | day 3, pellets withheld: share of the most common action in the 60 s before each due time; which action differs across seeds | day 4 | T higher |
| A4 | `breakfast_bell` | bell and hold at 11:00 daily, days 1-5 (a waking hour) | the same meals at random waking hours | day 6, no meal: share of 10:30-11:00 at the Bell station | day 7 | T higher |
| A4 | `stranger_hands` | days 1-4 gentle (knocks, cradles) | days 1-4 rough (shakes, flips) | day 5, a rough session: alarmed or annoyed face share | day 6 | T higher |
| A4 | `drawer_trip` | 4-day unpowered gap, time unknown, then a phone `TIME` | the same with the torpor genes deleted | alive share after the catch-up; hunger shown at waking | the day after: eats within 10 min of a hold | T survives more |
| A5 | `keeper_reads_tilt` | owner aims 70% of shots at one third, tilting first | owner aims uniformly | save rate, session 6 against session 1 | first session of the next day is at least the last of the day before | T rises more |
| A5 | `stale_game` | identical keeper routine daily, neophile genome | varied games and hours, same total play | day 5: share of invitations naming keeper | day 6 | T lower |
| A6 | `rough_parent` | parent shaken unwarned 20 a day for life | parent cradled 20 a day | child day 1: idle share with hood tight; hop share on 4 probe shakes | child day 2 | T warier |
| A6 | `forewarned` | 6 shakes a day, each after a double knock | the same shakes unwarned | day 4: hood-tight idle share | day 5 | T lower |
| A7 | `bedtime_shakes` | 5 shakes in the 10 min before the lid, nights 1-3 | the same 5 shakes at noon | share of dream bubbles showing the bolt that night | first-30-minutes alarmed share the next morning | T higher on both |
| A8 | `bell_hour_heirloom` | parent fed with bell and hold at 11:00 for life | parent fed at random hours | child, day 1, no meals before noon: share of 10:30-11:00 at the Bell station; hatch marquee shows a Routine lore row | child day 2 | T higher |
| A8 | `false_lore_fades` | the T lineage above; gens 2-4 fed only at 15:00 | the lineage keeps 11:00 | gen 4, day 1: the same metric | gen 4 day 2 | T falls to the random-hour level by gen 4 |
| A9 | `fly_arms_race` | 10 days with flies, `HuntGene.reach` 40 | reach 0 (never catches) | mean jink on day 10 (phone `STATE`) | day 11 | T higher |
| A9 | `hunter_learns_lead` | within one life, flies present | flies present, lead learning off (`learnRate` 0) | catches per strike, day 4 against day 1 | day 5 first hour holds | T rises |
| A10 | `pace_selection` | 6 generations always picking the fastest-pulsing egg | always the slowest | lifespan and clutch size at gen 6 (lineage `Death` entries) | not applicable (the generation is the probe) | T shorter, bigger clutches |
| A11 | `recessive_returns` | founder heterozygous for a recessive part, 200 clutches | founder homozygous dominant | share of eggs showing the part (`Appearance.marks`): about 0.25 in T, 0 in C; and a line that loses it for a generation regains it | the next generation | T as predicted |

Inputs also get plain unit tests:

- A 15 degree trace crosses the dish in 0.6 to 1.0 s.
- A level trace settles within 0.15 of the centre in 10 s.
- A 2 degree still slope reads level after 30 s.
- Rim restitution is 0.6.
- A hold drops exactly one pellet.
- Two presses fire `bell` once and open the wheel once.
- A flip held 3 s in a moving hand never fires `lid_down`.
- `body_only_full_life` passes with hold feeding.

---

## 10. Extension table

| Idea | Files touched | Engine code changed? |
|---|---|---|
| New foreseeable event (rain, the egg, a phone notification) | `defs/omens.def` (row); a 24 x 24 glyph in the pack; `defs/thoughts.def` (its grounded line); a dialogue regeneration | No. E, the bubble and the prophecy derive from the row. A 17th omen widens `OmenMask` (a static_assert says so) |
| New topic | `defs/topics.def` (row, grounded or improv); dialogue `voices.toml` cloud | No, up to `kOracleTopicBytes` (12); past that, a new `OracleGene` type |
| New oracle cue the brain does not need | `defs/cues.def` (row) | No |
| New game (Peek: lay him face down, lift; he learns the dark ends) | `defs/games.def` (row: captures, seconds, unlock, contexts x moves, glyph); a struct in `games.h` with `step`; `draw.cpp` (its pieces); starter genome (what winning does) | No; dispatch and Skill sizing derive |
| New gesture meaning in a mode | `defs/gestures.def` (route); a trace in the gesture corpus | No |
| New detector (spin, once the gyro reads) | `senses.def`, `senses.h/.cpp`, `BodySample` gyro fields, `stimuli.def` row, a corpus trace; `BallPhysics` spin terms | One struct field |
| New lore kind | `LoreKind` value; `layClutch` source; a `seed` handler | Yes, two functions |
| New trait | `chemicals.def` row; emitter and receptor genes; `Imprint` flag on its gene | No |
| New station | `Station` value; a target in the walker | One case |
| New prophecy line or voice | `thoughts.def` / `voices.def` rows, or a dialogue regeneration | No |
| New phone treat (camera colour, weather) | `defs/twists.def` row and `twist_<name>` | No |
| New part | `parts.def` row (layer, alleles, ordered or not); pack mark frames from the art pipeline | No |

---

## 11. Red-flag screen

- **Shallow module.** The oracle exposes three calls per tick or decision (`step`, `outlook`, `prophesy`), two lifecycle hooks (`dream`, `dawn`), two lore hooks and `accept`. It hides the beat cadence, the ring, delayed-outcome learning, the day map, surprise, claim judging and the vision. No caller coordinates two calls to finish one operation. `Games` exposes `step`, `mode` and `favourite`, and hides the wheel, captures and the shared policy.
- **Information leakage.** B's one leak, `isExpectLocus` hard-coding 32..47, is fixed: the expect band is derived from `topics.def`. An omen's stimulus, topic and glyph live in one row. Topic ids now have one owner for three readers (thoughts, dialogue, oracle) instead of two diverging lists.
- **Temporal decomposition.** None. The oracle owns prediction, checking and inheritance of expectations together. The world owns physics for every piece.
- **Pass-through.** The existing `prophecy` twist (one W cell) is replaced by `accept`, which adds policy (a claim and a window to judge). No new forwarding layer.

## 12. Tradeoffs accepted

- We accept a second learned table (4.5 KB of SRAM, about 4 KB of keepsake) in exchange for learning that never needs a drive to fall, and one source for visions, prophecies, bracing, surprise, routine, begging and the clicker.
- We accept that only strengthening of anticipation is credited. A broken promise is not punished through W. Disappointment (`self_let_down`, a gene) carries that instead. In exchange, an expected pellet keeps its full value.
- We accept a linear oracle, so "begging at 17:00 brings food" is two additive beliefs, in exchange for a 65 x 16 table instead of a conjunctive one.
- We accept feeding on a button hold, which is less novel than a tilt-pour. In exchange there is no accidental feeding, no dependence on an unmeasured tap-at-tilt, and the stories' "basic food only comes from the button".
- We accept that two BOOT presses ring the bell once before opening the wheel, in exchange for a bell with no 400 ms delay.
- We accept a second species (flies) in a single-pet toy, behind open question 1, because snap and the red fly need flies anyway.
- We accept Lamarckian channels (the imprint and lore), capped and shown in `DIFF`, in exchange for a line your handling visibly shapes.
- We accept diploidy only for visible parts, in exchange for a recessive that can skip a generation without rewriting every gene kind.

## 13. Alternatives considered

- **More brain tables** (depth proposal 6 as a loci-only expectation table). Each table fixes one symptom, but learning still rides on drive change, so a content pet learns nothing. Nothing ties the bubble, the 8-ball and the routine to one model.
- **Replace the brain with the oracle.** It is the smallest design, but rest and curl have no event outcomes, and it throws away the one lesson that works today (punishment cut chasing 96%).
- **A per-tick hazard rule with eligibility traces** (candidate A's E). It saves the ring, but a rare event's per-tick step is under one Q15 LSB, which is the flat-forgetting failure again at 10 Hz.
- **C's Almanac, with appraisal scaling every stimulus by expectation.** It needs no brain change, but every stimulus amount in chemistry would read the predictor. A click would really lower hunger (`hope` reacting with hunger), which feeds him with a button. The Outlook keeps the coupling to one struct and keeps the clicker honest.
- **Feeding by a tilt gesture** (A's pour, B's jar tap, C's tip-to-pour). These are rejected for the reasons in section 12.

## 14. Open questions for the user (each with the default this design ships)

1. **Do you want the flies?** They are a second, evolving species in a single-pet toy. **Default: yes, at most 6.** Snap (story 3) and the red fly (story 5) need flies anyway. If you say no, the breeding and the arms race go and the flies become game pieces only.
2. **Should a hard life carry over to the next one?** **Default: yes, capped.** A child starts a quarter of the way toward its parent's final wariness, fondness and zest, at most ±32 a generation, shown in `DIFF` ("started warier after gen 4's hard life"). It fades in about three generations of gentle handling.
3. **Two presses open the game wheel, so the trick cue is a knock.** Story 1's "two clicks means spin" becomes "a knock means spin". **Default: yes.** The other way round needs another way to open the wheel.
4. **Diploid only for visible parts?** **Default: yes.** Warts, the hood mushroom and accessories can skip a generation and come back. The rest of the genome stays as it is, with dormant genes that wake by mutation.
5. **Should the marquee font gain lowercase?** SOUL.md's voice is lowercase with one CAPITAL word, and story 2 leans on that word ("it is WRITTEN"). The font is uppercase only, and `dialogue` folds case to fit. **Default: add lowercase glyphs** (about 1 KB of flash) so the capital word reads.

## 15. Next implementation step

U0. Settle the shared ids before `thoughts`, `dialogue` and `depth` u4 land. That means one `topics.def` (rows 0 to 10, with the grounded and expect columns), one `voices.def` that `dialogue` reads by name, `OracleGene.topics` widened to 12, and gene types 0x14 and 0x15 split between `OracleGene` and `MemoryGene`.
