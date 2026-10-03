# blorbarium alife v2, candidate B: the oracle loop

Grungo is a seer, so v2 makes prediction his core loop. One new structure, the **oracle**, learns what happens next in the dish from what is going on and what he is doing. Everything the user asked for comes out of it. Foresee shows the oracle's real top expectation as a vision bubble. A shake can make him speak it aloud like an 8-ball, in a voice and on topics his genes choose. When a prophecy comes true he is rewarded for being right. Surprise drives curiosity, expectation drives dread and appetite, and a BOOT click becomes rewarding only because it predicts food, which is how clicker training works on real animals. Around that core, the marble becomes a real accelerometer-driven ball, feeding moves to a jar you tap, BOOT becomes the owner's clicker and game button, five registry games give him things to learn, and a fly population in the dish evolves against his lineage.

Sketch headers in `sketch/` compile with `g++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror=narrowing` against `origin/engine`'s headers (a read-only copy is in `_ref/`; the sketch's `stimuli.def` and `loci.def` shadow the engine's). `sketch/usage_check.cpp` built with `-DSIZES` prints the sizes quoted in section 9. `check_sketch.sh` (run in WSL `survivor`) compiles the usage file and each header alone, plants a duplicate gesture route and confirms the build rejects it, then prints the sizes.

## 1. Problem

The engine reads well but learns almost nothing that lasts (`learning-depth.md`, `e2e-learning.md`). Of 11 controlled scenarios, 3 show learning, and no brain lesson survives a night of sleep. The causes are structural, not tuning:

1. **Learning rides on drive change.** Reward is the fall of a pressing drive. Boredom sits below 0.02 for 100% of waking time and an attentive owner pins loneliness and need-touch at zero, so play, the marble, knocks and cuddles teach nothing.
2. **Nightly forgetting is a fixed 0.137 per weight**, because `forget` rounds every decrement up to one Q15 LSB. No day's lesson is bigger.
3. **A stimulus is visible to the brain for about 0.5 s**, and the brain reads features only when an action starts.
4. **Nothing predicts anything.** Foresee is a free boredom pump that shows nothing. There is no expectation, no routine, no owner model, and heirlooms filter on absolute weights, so a full life passes on zero.

The depth pass (branch `depth`) fixes the balance, heirlooms from learned change, rot and tilt situations, interrupting stimuli and one time-based lesson. This design treats those as landed and asks what to build on top.

The user's new requests are a marble driven by the sensor, a BOOT button that does something other than feeding, minigames he learns from, and "significant alife development across a variety of non-trivial, interesting axes". The coordinator added four more: he rests at the centre and moves across the face when acting; foresee should show what he actually sees; a shake can make him tell the future like an 8-ball, sometimes, in his voice; and the topics and the way he speaks are heritable and mutate.

Constraints the design honours: body-only play is complete; the engine is integer and deterministic and stays WASM-clean; everything extends through registries (one row plus a handler); the 1.28 board may have no PSRAM; the phone is an optional source of twists and the board is the only writer; basic food is body-only; neglect can kill; grungo's voice obeys SOUL.md (third person, lower case, one capitalised word, the Never list).

## 2. Usage (caller's view)

### The owner's quickstart

This is the README the person who owns the toy would read.

- **He lives in the bowl.** He rests in the middle and walks out across the face when he does something, such as chasing the marble or a fly, hiding at the rim, or begging at the jar.
- **Tilt the bowl** and the marble rolls like a real one. Knock the case and it jumps. He chases it, and sometimes it bonks him.
- **Feed him from the jar.** The jar sits at 12 o'clock on the rim. Raise that side and knock once. A pellet tumbles out and rolls down to him. The pips on the jar show how many are left.
- **BOOT is your clicker.** A short press makes a click. At first it means nothing to him. Click right before you feed him a few times and the click starts to mean food. Then click the moment he does something, and he will do it more.
- **Hold BOOT** to open the game ring. Tilt or knock to choose, press to start, hold to stop.
- **Ask him the future.** Shake him and, sometimes, instead of hopping he goes still, his eyes glow and he tells you what is coming. Make it come true and watch him gloat.
- **Watch the bubble.** When his eyes glow, a bubble above his head shows what he expects next. Faint means unsure. If it happens before the ring of sand runs out, he was right.
- **He learns your day and your hands.** Feed him at the same hour and he waits at the jar before it. Hand him to a friend and he notices.
- **Each egg is a new run.** The hatchling carries the family's looks, temperament, beliefs, tricks, song and voice, a little mutated.

### Engine call sites

The `Dish` facade does not change: `sample`, `tick`, `appearance`. `sketch/usage_check.cpp` holds the call sites below as compiling code.

The creature's tick gains one step between chemistry and the brain:

```cpp
// creature.cpp, step 4b: the oracle looks ahead
Fx cues[kCueCount];
cuesInto(cues);                                     // features, oracle-only cues, action one-hots
OracleOut o = oracle_.step(cues, omensIn(routed), clock.petHour(), tick, seer_);
for (const TopicInfo& t : TOPICS) chem_.set(t.expect, o.expect[t.id.v]);   // expect_food, expect_danger...
chem_.set(locus::surprise, o.surprise);
for (uint8_t i = 0; i < o.fireCount; ++i) pendingSelf_.fire(o.fire[i]);    // vindicated, mistaken, surprised, let down
```

The brain decides with an outlook, which is the only bridge from the oracle into the old learner:

```cpp
Outlook out;
oracle_.outlook(seer_, effects_, drives, out);      // action bias + anticipated drive change
Decision d = brain_.think(features, drives, arousal, temperament, actionDone_, rng_, out);
```

A host test reads like the behaviour it proves:

```cpp
TEST(Oracle, LearnsThatATiltComesBeforeAShake) {
  // 40 pairings: tilt, then a shake 3 s later, once a minute
  ...
  EXPECT_GT(o.predicted(omen::shaking), Fx::ratio(1, 2));
  EXPECT_EQ(o.prophesy(t, effects, drives, tick, hour, false).omen, omen::shaking);
}
```

Adding a foreseeable event is one `omens.def` row (stimulus, topic, glyph, noun) and a glyph in the pack. Adding a game is one `games.def` row and one struct in `games.h`. Adding a gesture remap is one `gestures.def` row. The registries' static checks (`sketch/blorb/v2_registry.h`) refuse a duplicate route, an omen naming a missing stimulus, a topic with no omen, or a game that captures BOOT hold, a drop or the jar.

## 3. Shape

### Data structures first

**The oracle** (`sketch/blorb/oracle.h`). Its core is one table:

```
E[cue][omen] = P(omen happens within the horizon | this cue is on)        Q15, 64 x 16
```

- **Cues** (64 once v2's two actions land) are derived, never listed. They are every brain feature except the expect band, then the oracle-only cues in `cues.def` (things worth predicting from that the brain need not see, such as "the jar was just tapped"), then a one-hot of the current action, then a half-strength one-hot of the previous action. The action tokens are what let him learn that his own behaviour brings things about.
- **Omens** (16, `omens.def`) are the stimuli he can foresee. Each row names its stimulus, a topic, the glyph his bubble draws and the noun his voice uses.
- **Topics** (6, `topics.def`: food, touch, play, danger, night, visit) group omens. The brain sees four `expect_<topic>` loci, not sixteen columns, so the brain table grows by four features, not sixteen.

**Learning** is NLMS on delayed outcomes. Every beat (genetic, 0.5 s in the starter) the oracle snapshots the 12 strongest cues into a 16-slot ring. When a snapshot is `horizonBeats` old (genetic, 6 s in the starter), each omen's target is 1 if it fired inside that window and 0 if not, and E moves toward it. The prediction for a snapshot is recomputed from current E at closing time. The oracle learns from whether things happen, so it learns while he is content and never needs a drive to fall. That removes root cause 1 for everything the oracle carries. The ring holds the cues for the whole horizon, so a cue visible for half a second is still credited six seconds later, which removes root cause 3 for prediction. Forgetting is one proportional multiply per cell at pet dawn, with no round-up, which removes root cause 2.

**The day map.** `routine_[24 pet hours][6 topics]` is a byte EMA of "this topic happened in this hour today", closed once per pet day. It predicts in hours what E predicts in seconds. Its next two hours feed `expect_<topic>`, so the meal hour raises `expect_food` and an appetite emitter can make him hungry and waiting.

**The prophecy** is one committed prediction: an omen, a confidence, a deadline, and whether it came from the Foresee action or the 8-ball. `prophesy()` picks `argmax topicWeight * (p + wishfulness * value+)` over the near predictions and the day map's next two hours. While a prophecy is active, `prophesy()` returns it unchanged, so a double trigger is a no-op. When the omen fires before the deadline the oracle fires `self_vindicated`; when the deadline passes it fires `self_mistaken`. Either way the prophecy closes once.

**Surprise and disappointment.** When an omen fires with p below 0.2, the oracle fires `self_surprised` and raises the `surprise` locus by 1 - p. When a window closes with p at least 0.6 and nothing came, it fires `self_let_down` and raises `let_down`. What these do to him is his stimulus genes, so one lineage finds surprise delightful and another finds it frightening.

**Dream memories.** The day's 8 most surprising snapshots. Asleep, each dream replays one into E at full rate (consolidation) and shows its omen in a purple bubble.

**The bridge into the old brain** is one struct, `Outlook`, built at each decision:

- `actionBias[a] = forethought * sum_omen E[token a][omen] * value(omen) + curiosity * uncertainty(a)`. `value(omen)` is what the omen's stimulus genes do to his drives, weighted by how pressing each drive is now. It is read from the phenotype (`OmenEffects`, derived once per stage), so it needs no learning. If calling has preceded a pour often enough, calling scores up when he is hungry. That is begging, and chance pairings make superstitions.
- `anticipated[d] = sum_omen p(omen) * effect(omen, d)`. The brain's observed drive change becomes `(drive_now + anticipated_now) - (drive_start + anticipated_start)`. When a click raises `p(snack)`, the action before the click is credited with hunger relief at once, before any food lands. That is secondary reinforcement, and it is how a click becomes a reward. With `forethought` and anticipation at zero the brain behaves exactly as today, which is the regression test.

**The world** (`sketch/blorb/world.h`) replaces `Habitat`. The marble, the pellets and the flies are bodies. The marble integrates at 50 Hz from the accelerometer's raw in-plane reading (section 5). `Dish::sample` appends each sample to a `SampleTrace`; `World::step` drains it as substeps inside `Dish::tick`, so every world write still happens in tick order and replays bit for bit. The world also holds the fly `Swarm` and the owner's hands signature, because both outlive any one grungo.

**Games** (`sketch/blorb/games.h`) are rule overlays on the world, one `games.def` row and one struct each. A game sees every routed stimulus, may capture some (they then reach only the game), and reports outcomes only as World stimuli. The brain and the oracle therefore learn a game like anything else, and deleting a game touches nothing in the creature.

**Gesture routes** (`gestures.def`) map a raw body stimulus to what it means in a mode (Egg, Live, Ring, Game, Clutch). A row is a remap; no row means unchanged. The original design rejected a rebinding table because nothing rebound in v1 (DESIGN.md section 12). In v2, BOOT means click, game ring, game input or egg cursor depending on mode, so the requirement now exists, and a table with a static uniqueness check is the honest shape for it.

### Tick order, v2

Each 100 ms tick, in this order and no other:

1. `PetClock::advance`; tick-rate detectors.
2. Route this tick's raw stimuli by mode (`route(mode, stim)`). `Games::step` runs the ambient row and the active game over the routed stimuli and returns the captured mask, which is cleared before the creature sees it. `World::step` drains the `SampleTrace` (marble and pellet substeps), refills the jar, rots pellets, steps the flies, writes its loci and fires World stimuli.
3. The occupant ticks. For a creature: sense loci; stimuli through his stimulus genes (a shake while awake rolls the 8-ball first, and a prophecy suppresses this tick's hop); chemistry; lifecycle and reflexes; **4b the oracle step**; the brain (think with an Outlook awake; brain dream and oracle dream asleep); the behaviour, unless a reflex or a game's ask owns the body; face, stats, recent loci.
4. Protocol and saves. At the `dawn` stimulus the oracle forgets proportionally and the day map closes the day, once per pet day (a saved day index makes a replay a no-op).

### What the interface hides

The creature makes one oracle call per tick and one per decision. Behind `step` sit the beat cadence, the ring, delayed-outcome learning, the day map, surprise scoring, prophecy resolution and vision state. Behind `outlook` sit planning and anticipation. Behind `prophesy` sit topic weights, wishfulness and the choice between near and far futures. The creature never sees E, the ring or the day map. The phone reads them through one new verb, `EXPECT`, which streams a summary through `SCHEMA`'s existing row names, per boundary-discipline: wire text is parsed and formatted only in `protocol.cpp`.

### Invariants and where they live

- An omen always names a real stimulus, every topic has an omen, and no gesture route is ambiguous. These are compile-time checks in `v2_registry.h`, per encode-lessons-in-structure.
- Every line he can say obeys SOUL.md. The phrase corpus is finite (`phrases.def` times omen nouns times voice bytes), so `test_voice` enumerates all of it and checks lower case, one CAPS word, third person, at most 47 characters and no Never-list word. A test that enumerates the corpus is stronger than a style note.
- Foresee always glows (ask 7) and now always shows a bubble while the action runs.
- The oracle never writes chemistry. It returns `OracleOut` and the creature applies it. One writer per state, per separate-before-serializing-shared-state.
- The world owns the pieces and games own only rules. The fly swarm and the hands signature live in the world chunk of the keepsake, so a death never erases them, and the creature never writes them.

### What v2 deliberately does not do

- No conjunctions of cue and action inside the oracle. "Calling at 17:00 brings food" is learned as two beliefs, "calling brings food" and "17:00 brings food", which the brain combines additively. The conjunctive table would be 31 x 13 x 16 cells instead of 64 x 16.
- No new drives. Curiosity, pride and dread are stimulus and emitter genes over existing drives and the new loci.
- No generated text. The voice is a finite, tested corpus.
- No second creature. The flies are the dish's only other life, at most six, with a three-byte genome.

## 4. Alife axes

Eleven axes. Each lists what develops, the mechanism, how it shows on the 240 px screen, how it is inherited or mutates, and how the owner influences it. The proof for each is in section 7.

### A1. Foresight: what he expects, and what he sees

| | |
|---|---|
| Develops | Which omens follow which situations and actions within seconds: 1,024 learned expectations per life (64 cues x 16 omens). |
| Mechanism | The oracle's E table and its `expect_<topic>` loci. Emitter genes wire expectation into chemistry. The starter has dread (expect_danger raises fear, so the brain's existing fear-relieving curl runs before a shake) and appetite (expect_food raises a little hunger, so he goes to the jar). |
| On screen | Foresee shows a bubble above his head with the top omen's glyph. Opacity is confidence and a ring of sand runs down to the deadline. If it comes true, the bubble bursts teal with "it is WRITTEN". If not, it fogs over. The hood goes up before a predicted shake. |
| Heredity | `SeerGene`: beat, horizon, learn rate, dawn forgetting, forethought. `PaceGene` shortens or lengthens the horizon. Lore heirlooms seed E at hatching (A10). |
| Owner | Be predictable and he sees you coming. A deliberate pattern ("I always tilt before I shake") becomes his vision. |

### A2. The oracle's voice: what he prophesies, how honestly, how he says it

| | |
|---|---|
| Develops | Which topics he prophesies about, how honest his prophecies are, how he phrases them, and his record of hits for the life. |
| Mechanism | `prophesy()` scores `topicWeight * (p + wishfulness * value+)`. A shake while awake gives a prophecy instead of a hop with probability `oracleChance * (1 - expect_danger)`, so a frightened frog hops. The line is an `Utterance` (phrase row, omen noun, a "when" from the near horizon or a named pet hour, a hedge picked by confidence and the hedge byte, an opener and a closer) rendered for the `thoughts` marquee. The prophecy is grounded: the noun is his real top omen, and the hedge is his real confidence. |
| On screen | The marquee in his voice ("brrrup. perhaps a CUDDLE comes soon. it is WRITTEN."), the bubble, and a rim tally of hits this life. |
| Heredity | `SeerGene.topic[6]`, `wishfulness`, `oracleChance`; `VoiceGene` (gravity, verbosity, hedge, opener, closer, phrase bias, chattiness, croak pitch). A voice change counts as a Mind change, so siblings sound different, and DIFF says so ("voice: hedges more, opens with croooak"). The clutch preview can print one sample prophecy per egg. |
| Owner | Picking eggs selects the topics and the voice. Fulfilling prophecies feeds his vindication reward, and an honest line is right more often while a wishful line gets fed more when the owner plays along. |

### A3. Curiosity from surprise

| | |
|---|---|
| Develops | Whether he seeks the unpredicted (neophile) or avoids it (neophobe), and how stale the predictable becomes. |
| Mechanism | The planning term `curiosity * uncertainty(a)`, with `uncertainty(a) = sum p(1 - p)` over the omens his action token predicts. The `self_surprised` stimulus genes decide whether surprise relieves boredom or raises fear. A stimulus-gene flag bit (flags bit 2, no layout change) scales play relief by `0.5 + 0.5 * surprise`, so a knock he saw coming is half as much fun. |
| On screen | Wide eyes and a "brrrup?" on a surprise. A neophile walks to new things (the first fly, a marble that just bounced oddly); a neophobe watches from the rim. A routine knock gets a smaller reaction each week. |
| Heredity | `PersonalityGene.curious`, `SeerGene.surpriseGain`, the novelty bit on each stimulus gene. |
| Owner | Varied play keeps him engaged. The same knock at the same time every day bores him. |

### A4. Superstition and begging

| | |
|---|---|
| Develops | Beliefs that his own actions bring owner events, true or false. |
| Mechanism | Action tokens are oracle cues, so E learns "after I call, a snack comes" if the owner tends to pour when he calls. `Outlook.actionBias` then favours calling when hunger makes a snack valuable. A chance pairing (he happened to spin before three pours) makes a superstition the same way. NLMS extinguishes a belief that stops paying. |
| On screen | He performs the ritual and looks up at the jar. The phone and the lineage name it: "gen 4 believes spinning brings snacks". |
| Heredity | `SeerGene.forethought` (planner versus creature of habit). A strong learned belief can become lore (A10), so a child is born believing it. |
| Owner | Shape rituals on purpose or by accident, and break them by not responding. |

### A5. Tricks and sequences

| | |
|---|---|
| Develops | Cue to action chains taught with a clicker, including two-step tricks. |
| Mechanism | The click is neutral at birth (no stimulus gene amounts). Pairing click then pour makes `E[recent click][snack]` high, and anticipation credits whatever action was running when the click came. The trick cues (wave, left, right) exist only in Trick School and are brain features; the depth pass's interrupting stimuli make a cue start a fresh decision. The previous-action token lets E learn "spin, then curl, then click", which is a two-step trick. |
| On screen | In Trick School you give a cue, he performs, you click, a pellet pops out of the jar, and the rim counts the session's clicks. |
| Heredity | A strong cue to action belief becomes a born instinct, flagged heirloom ("born knowing SPIN on wave, learned by gen 3"). Learn rate and pace decide how fast a line learns. |
| Owner | Clicker training, entirely body-only. |

### A6. Attachment: your day and your hands

| | |
|---|---|
| Develops | A model of the owner's routine, recognition of the owner's handling, an imprint at hatching, and greeting and missing. |
| Mechanism | The day map learns which topic happens in which pet hour. `expect_visit` and `expect_food` rise before the usual times, and a let-down (expected, did not come) feeds loneliness through a stimulus gene. A 50 Hz `HandsDetector` measures six handling features while he is held (tremor energy, jerk, tilt angle, tilt rate, tap rate, hold length) and keeps a slow owner signature in the world. After 4 s of handling it writes `familiar` and fires `known_hands` or `strange_hands`. The first handling after 6 h fires `reunion`, including after an unpowered gap. For the first `imprintMinutes` after hatching, handling raises his sociability set point. |
| On screen | He waits at the jar and croaks before the usual meal hour. He greets the familiar hands after an absence with a hop and a croak ("croooak! the HANDS are back."). He hoods up for a stranger ("eep. these are not the HANDS."). |
| Heredity | `PersonalityGene.social`, `strangerFear`, `reunionJoy`, `imprintMinutes`. The signature belongs to the dish, so each hatchling must earn its own trust, but the dish already knows you. |
| Owner | A steady routine, gentle hands, and whether you lend the toy to a friend. |

### A7. Temperament drift, and the echo a hard life leaves

| | |
|---|---|
| Develops | Boldness, curiosity, sociability and liveliness over one life. |
| Mechanism | `Personality` holds live values that start at the gene's set points. Each surprise moves them by the omen's valence times the surprise, so an unforeseen bad thing costs boldness and a foreseen one costs little ("forewarned is forearmed"). They stay within plus or minus `plasticity` and relax toward the set point over days. They act as multipliers: fear amounts scale with 1.5 - bold, the planning curiosity term with curious, the loneliness tonic with social, and walk speed and roam radius with lively and bold. At death a quarter of the drift (times plasticity) becomes an `Epigenetic` op on the child's `PersonalityGene`, shown in DIFF ("started jumpy after gen 4's hard life"). |
| On screen | A bold frog roams to the rim and hops higher. A cowed one stays near the middle and hoods up at knocks. Stance scales by up to 5%. |
| Heredity | Set points and plasticity mutate. The echo passes a capped share of experience. |
| Owner | Gentle or rough handling, and above all whether rough things come with warning. |

### A8. Memory and dreams

| | |
|---|---|
| Develops | What he keeps from each day, and what he dreams about. |
| Mechanism | The oracle keeps the day's 8 most surprising moments, and the brain keeps its 16 episodes with the largest drive change (depth proposal 5). Each dream replays one of each. Oracle dreams consolidate at full rate. Forgetting is proportional at dawn, so a lesson reinforced daily keeps a steady level instead of being wiped. A dreamed danger omen raises fear and can wake him, and he says what he dreamt. |
| On screen | A purple bubble above the sleeping frog with the dreamed omen's glyph. On waking from a nightmare: "grungo dreamt of a great SHAKING." |
| Heredity | `SeerGene.dawnForget` and pace. The existing `dreamer` feat still applies. |
| Owner | The day's dramatic moments become tonight's dreams. A lesson repeated on three days survives the nights. |

### A9. Life-history pace

| | |
|---|---|
| Develops | Across generations, a line becomes fast (short lives, many eggs, quick learners who forget fast and see near futures) or slow (long lives, deep learners, far visions, more lore slots). |
| Mechanism | `PaceGene`, one byte with pleiotropic expression. It multiplies the Life half-life by `2^((128 - pace) / 64)`, scales hunger, incubation, learn rate, forgetting and horizon, adds a clutch egg above 176 and a lore slot below 80. The viability dry run checks both extremes. |
| On screen | Fast lines are small and twitchy and the generation count climbs. In the clutch, egg size and speckle show pace. |
| Heredity | Mutates like any byte; the owner's egg choice is the selection. |
| Owner | Choosing eggs, and the care regime. Harsh care favours fast lines, because a short life reaches its egg before neglect kills it. |

### A10. Culture: lore, songs and heirloom tricks

| | |
|---|---|
| Develops | A family's beliefs, tricks and song, carried and edited across generations. |
| Mechanism | At death, the strongest learned expectations (ranked by learned change, bias cues excluded) become `OmenPriorGene`s, seeded into the child's oracle as one nudge each at hatching. Learned brain beliefs become heirloom instincts (depth unit). The most practised Drum Circle pattern becomes the child's `SongGene`, with mutation. A child confirms or extinguishes each inherited belief against its own owner, so false beliefs die out under a consistent owner and true ones persist. Culture is selected by predictive truth. |
| On screen | A hatchling performs the family ritual untrained and croaks the family song when content. The lineage names each belief and how many generations held it ("the belief of gen 3: spinning brings snacks. held for 4 generations."). |
| Heredity | These are genes, mutated as usual. Pace sets the number of slots. |
| Owner | Confirm or break the family's beliefs; teach new songs. |

### A11. The dish ecology: flies that evolve

| | |
|---|---|
| Develops | A fly population whose genes (speed, dodge chance, flee radius) evolve against his lineage's hunting, and his hunting skill within a life. |
| Mechanism | A pellet left rotten for an hour hatches a fly. Two live flies and food in the dish breed one fly every few hours (mid-parent genome plus mutation, at most six). Flies dodge a tongue strike with their jink chance, and caught flies do not breed, so jink rises over days of hunting. His `Hunt` action aims at `fly + velocity * lead`, and each miss moves `lead` by the sign of the miss along the fly's velocity, so his catch rate rises within a life. `HuntGene` sets reach, tongue speed, lead prior and patience. The swarm belongs to the world and outlives every grungo, so the arms race spans generations. |
| On screen | Flies buzz over the bowl; he flicks his tongue (the blep face) and a catch sparkles. The phone shows the fly generation and mean jink. |
| Heredity | `HuntGene` mutates; the flies have their own heredity. |
| Owner | Let rot breed flies (free food, but rot is a toxin risk) or keep the bowl clean; herd flies in Fly Hunt. |

## 5. Inputs, redesigned

### The marble is a real ball

A ball resting in a box feels the box's specific force, which is exactly what an accelerometer measures. So the marble's acceleration is the raw in-plane reading, scaled, minus a centring pull and drag. Tilting rolls it, a knock flicks it, a shake rattles it and swinging the device sloshes it, all from one rule with no special cases.

- **Integration.** Semi-implicit Euler at 20 ms, one substep per IMU sample, drained from the `SampleTrace` inside `Dish::tick` so replays stay bit-identical.
- **Constants** (`BallPhysics` in `world.h`). Gravity scale 24 dish units per s² per 1 g (the 5/7 rolling factor included); bowl centring pull 1.0 per unit radius; viscous drag 0.3/s; rolling friction 0.15 units/s²; rim restitution 0.6; his body is a circle with restitution 0.5.
- **Feel.** A 15° tilt gives 24 x sin 15° = 6.2 units/s², so the marble crosses the 2-unit dish in about 0.8 s (arithmetic, ignoring drag and the bowl). The bowl pull gives a 6.3 s swing period and drag decays it with a 6.7 s time constant, so a level dish settles the marble near the centre in about 8 s (arithmetic). A knock peak of about 1.5 g for one 20 ms sample gives 0.72 units/s, which the bowl pull turns into a swing about 0.7 units from the centre, a third of the way across (arithmetic; the 1.5 g tap peak is an estimate, unmeasured).
- **A desk is level.** A still device subtracts a 30 s baseline of its own reading, so a desk that slopes 2° reads level, while a hand's tilt passes through. Without this, a 1° slope would park the marble 0.4 units off centre (arithmetic: 24 x sin 1° / 1.0).
- **Bonks.** The marble bounces off him. A relative speed above 0.3 units/s fires `marble_hit`, and he wobbles. Pellets are heavy bodies that slide only past about 10°.

### BOOT becomes the clicker and the game button

BOOT stops being feeding. A short press in Live mode fires `click`. The click starts meaningless (no stimulus gene amounts) and gains value only through what it predicts (A5). That makes BOOT the owner's voice, and it is the cleanest demonstration in the toy that he learns: you can watch the click become rewarding. A 1.2 s hold opens the game ring. The hold stays at 1.2 s, under the 1.46's 4 s power-off.

### Feeding moves to the jar

The pantry becomes a jar drawn on the rim at 12 o'clock, with its pips. Raise the jar side at least 25° and knock once. The knock detector reads the smoothed tilt, and a knock with the jar raised fires `jar_tap` instead of `knock` (exactly one of the two, never both). A pellet tumbles out at the rim and rolls into the bowl under the same physics as the marble, so you can steer the meal toward him. An empty jar gives a hollow clack and the jar wobbles. The care hint for hunger becomes the jar glyph with a knuckle. Flies are a second, passive food source (A11). Feeding stays body-only, deliberate (no accidental pours when picking him up), and one-handed.

### The gesture table

Every gesture in every mode. A blank cell means the gesture does nothing in that mode. Raw stimuli come from the detectors; routes come from `gestures.def`; game captures come from `games.def`.

| Gesture | Live, awake | Live, asleep | Ring (game picker) | Game | Clutch | Egg |
|---|---|---|---|---|---|---|
| Tilt | rolls marble and pellets, herds flies; `tilted` feature | rolls marble | moves the ring cursor | game input (all games see it) | | |
| Knock | `knock` (flinch; also kicks the marble physically) | `knock` (his genes decide if it wakes him) | steps the cursor | Drum Circle: a beat (captured); else `knock` | next egg | wobble |
| Knock, jar raised 25° | `jar_tap`: pours a pellet | `jar_tap`: pours (he smells it on waking) | steps the cursor | `jar_tap` | next egg (routed to knock) | wobble (routed to knock) |
| Double knock | `double_knock`, a pat | `double_knock` | steps the cursor | Drum Circle: a beat (captured); else pat | pick egg | wobble |
| Shake | the 8-ball: a prophecy (odds `oracleChance * (1 - expect_danger)`) or the hop | hop, and wakes | closes the ring | ends the game, except Oracle Pool, where it asks | | wobble |
| Pick up, put down | `picked_up`, `put_down`; after 4 s, `known_hands` or `strange_hands`; after 6 h away, `reunion` first | same, gated by his genes | | same | | warmth begins |
| Cradle (held still upright 3 s) | `cradle` | `cradle` | | `cradle` | | warmth |
| Flip (upside down, moving) | `flipped` | `flipped` | | `flipped` | | |
| Face down and still 2 s | `lid_down`, tuck-in | `lid_down` | closes the ring | ends the game | | |
| Drop (free fall) | `dropped` | `dropped` | closes the ring | ends the game; always reaches him | | |
| BOOT press | `click` | `click` (ignored unless a gene hears it asleep) | starts the aimed game | game input; Trick School: the click | next egg | `click` (he hears it in the shell) |
| BOOT hold 1.2 s | opens the game ring | ring shows "zzz" and will not start | closes the ring | ends the game | pick egg | |
| Wave, left, right tilts | swallowed (routed to none) | | | Trick School: `cue_wave`, `cue_left`, `cue_right` | | |

Conflicts this table resolves:

1. **A flip was a tuck-in.** A face-down device in a hand moves; a device on a table is still. `lid_down` now needs 2 s face down and still.
2. **Knock versus feeding.** The jar's tilt decides, and the detector fires exactly one of `knock` or `jar_tap`.
3. **The 8-ball versus the hop.** One roll per shake decides. Asleep he always hops.
4. **Tilt play versus trick cues.** The wave, left and right cues exist only in Trick School, so playing with the marble never issues a command.
5. **BOOT.** The mode decides between click, ring, game input and egg cursor. A hold always ends or closes something and never opens a second thing.
6. **Drum beats are not knocks.** Drum Circle captures knocks, so he does not flinch at every beat.
7. **A drop always reaches him.** No game may capture `dropped`, BOOT hold or `jar_tap` (static check).

## 6. Minigames he learns from

A registry, `defs/games.def`. Row 0 is ambient. The others start from the game ring.

| Game | Rules | How he plays and learns | What he gains | What it teaches the lineage |
|---|---|---|---|---|
| **0. Marble Bowl** (ambient, always on) | The marble is always in the bowl. No score. A bonk is `marble_hit`. | Chases and noses it. The oracle learns that your tilting brings the marble (tilted to marble), so `expect_play` rises when you pick him up. Novelty scaling means a marble trick he has not seen is worth more. | Boredom relief from play that is not pinned, because the depth pass rebalanced boredom and novelty scaling keeps play worth something. | Chase instincts as heirlooms; neophile or neophobe lines. |
| **1. Fly Hunt** (90 s) | The jar releases three flies. Tilt herds them; they drift downhill like gnats in wind. Score is his catches. | `Hunt` action. Aims with a lead that improves with each miss. The oracle learns that herding brings flies near. | Food (a fly is a small meal) and boredom relief. | `HuntGene` selection, and through the swarm, flies that are harder to catch for the next generation. |
| **2. Drum Circle** (120 s) | You knock a beat. He predicts the next onset from your intervals and croaks on it. A knock within 80 ms of his croak is `in_sync`. Score is synced beats. | Prediction in its purest form: you can see him anticipating. Timing precision improves with practice. In-sync onsets pull his remembered pattern toward yours. | Boredom and loneliness relief; a song. | The family song (`SongGene`), which mutates, so songs drift across generations. |
| **3. Trick School** (5 min) | Give a cue (wave, left, right). If he does what you wanted, press BOOT. That is the click, and a pellet follows from the jar if it has one. Score is clicks earned. | Clicker training through anticipation (A5). Chains through the previous-action token. | Food and the vindication of a click he expected. | Heirloom tricks: a child born doing the family trick on cue. |
| **4. Oracle Pool** (3 min) | Every shake asks him. He always prophesies (no hop). Make it come true before the sand runs out. Score is the streak. | The oracle learns that his prophecies come true when you are playing, so prophecy becomes a way to ask for things. A wishful line exploits this faster. | Vindication (being right is his favourite thing) and whatever the fulfilled omen brings. | Selection on oracle chance, topics, wishfulness and voice through the owner's egg picks. |

Every game's result appends a short lineage line ("gen 6: Drum Circle best 14"), so a lineage has high scores. The ring does not start a game while he sleeps.

## 7. Proof: e2e scenarios

Same method as `e2e-learning.md`: the depth branch's harness (`test/support/e2e`) drives the real `Dish` through the board's seams with 50 Hz `BodySample`s, 40 seeds, treatment and control arms that share seed, care schedule and every jittered row. Metrics use only what `STATE` and `Appearance` show. Verdict rules as before: **Yes** if the 95% bootstrap CI of T - C excludes 0 on the predicted side and at least 75% of seeds move that way. Each scenario starts as `knownFailing` and its mark is removed by the unit that makes it pass. Care in both arms feeds by `jar_tap` in waking hours.

| Axis | Scenario | Treatment vs control | Prediction (fixed before running) | Observable metric |
|---|---|---|---|---|
| A1 | `tilt_warns_of_shake` | Days 1-3, every 20 min awake: tilt left 2 s, then shake 3 s later. Control: the same tilts and shakes at independent times | Day 4, probe tilt with no shake: he foresees it and hides | Share of 6 s after a probe tilt with the vision showing `shaking` or the curl pose |
| A1 | `tilt_warns_overnight` | Same, probes on day 5 after a night | The lesson survives the night (today's failure) | Same metric on day 5, T - C at least half of day 4's |
| A2 | `prophecy_follows_reality` | After each shake, the owner cradles him within 5 s. Control: cradles at random times, same count | His 8-ball prophecies name touch omens more often | Share of spoken prophecies with a touch noun, days 3-4 |
| A2 | `topics_inherit` | Founder genomes differing only in `topic[]` (food-heavy vs touch-heavy), each line run 3 generations with mutation | Each line's topic share stays on its side | Food-noun share per generation; parent-child correlation over 40 lines |
| A2 | `honest_vs_wishful` | `wishfulness` 0 vs 220, same owner | Honest lines are right more often | Vindicated / prophesied per life |
| A3 | `neophile_seeks_flies` | `PersonalityGene.curious` 220 vs 40; flies first appear on day 2 | The curious line approaches sooner | Seconds from the first `fly_hatched` to his first strike or approach within 0.2 |
| A3 | `routine_knock_goes_stale` | Days 1-4: a knock at 12:00 daily. Control: at a random hour | The predictable knock earns a smaller reaction | Day 4 happy-face intensity in the 10 s after the knock |
| A4 | `spin_brings_snacks` | Days 1-3: the owner pours whenever he starts `hop_circles`, if not fed in the last 30 min. Control: the same number of pours at random | He spins more when hungry | Day 4 `hop_circles` share while hunger > 0.4 |
| A4 | `superstition_extinguishes` | Treatment continues 3 more days with no pairing | The ritual fades | Spin share on day 7 vs day 4 within the treatment arm |
| A5 | `clicker_shapes_call` | Day 1: 20 click-then-pour pairings. Days 2-3: click (no food) every time he starts `call`. Control: day 1 pours with no click, days 2-3 identical | He calls more | Day 3 `call` share (today `trainer` showed no gain) |
| A5 | `trick_on_cue` | Trick School 10 min daily: wave cue, click on `hop_circles`. Control: clicks at random times | He spins on the wave cue | Spin share in 5 s after a probe wave, day 4 |
| A6 | `waits_for_supper` | Fed at 17:00 daily, days 1-4. Control: fed at random hours, same count | He waits at the jar before supper (today's `feeding_hour` failed) | Day 5, 16:30-17:00: share of time in the jar zone or in `call` facing the jar |
| A6 | `strange_hands` | Days 1-3 handled by profile A (slow tilts, soft taps). Day 4 probe pick-up by profile B (fast jerky tilts, hard taps). Control: probe by A | He is wary of the stranger | Curl or alarmed share in the 30 s after the probe pick-up |
| A6 | `reunion` | A 12 h unplugged gap (RTC source) vs a 12 h powered night with no handling | A greeting on the first pick-up | `reunion` fired and the hop within 5 s of the first pick-up |
| A7 | `rough_childhood` | Days 1-2 (child): 6 unwarned shakes or drops a day. Control: the same events each preceded by a tilt warning | The unwarned child grows timid | Adult day 4: mean roam radius and hops per probe knock |
| A7 | `maternal_echo` | Children of the two arms above, hatched with the same seed | The child of the hard life starts timid | First-hour roam radius of the child; the Epigenetic op in DIFF |
| A8 | `dreams_of_the_shake` | One violent shake at 20:00. Control: none | He dreams it | Share of dream bubbles showing `shaking` that night |
| A9 | `fast_vs_slow` | `pace` 200 vs 60, same care, 3 generations | Fast lines live shorter, lay more eggs, learn faster | Lifespan, clutch size, trials to criterion in `trick_on_cue` |
| A10 | `ritual_inherited` | Parent trained as in `spin_brings_snacks`; child hatched and observed before any training. Control: untrained parent | The child spins when hungry, untrained | Child's spin share while hungry, first 6 waking hours |
| A10 | `false_lore_dies` | Child of the trained parent, owner never pours after spins. Control: owner keeps the pairing | The inherited belief fades only without confirmation | Spin share on the child's day 3, T vs C |
| A10 | `song_inherited` | Parent drilled one 4-beat pattern in Drum Circle daily. Control: no drumming | The child croaks the pattern | Onset pattern of the child's idle croaks vs the parent's (edit distance) |
| A11 | `fly_arms_race` | 10 days with flies and `HuntGene.reach` 40. Control: reach 0 (he never catches) | Fly jink rises under predation | `Swarm::meanJink()` on day 10 |
| A11 | `hunter_learns_lead` | Within one life, flies present | His catch rate rises | Catches per strike, day 1 vs day 4 |

Inputs get plain unit tests too: a 15° trace crosses the dish in 0.6 to 1.0 s; a level trace settles within 0.15 of the centre in 10 s; a 2° still slope reads level after 30 s; rim speed after a bounce is 0.6 of the incoming (restitution); a flip held 3 s in a moving hand never fires `lid_down`; a knock with the jar raised fires `jar_tap` and not `knock`; `body_only_full_life` passes with jar feeding; the gesture static checks fail a planted duplicate route.

## 8. Phased build plan

Small units, each ending in a check that runs. Ordered by fun per cost. Per experience-first, units 1 to 8 are the core v2: a polished toy with a real marble, a jar, a clicker, a seer who shows and says what he expects, and two games. Units 9 onward deepen it and each is independently droppable. Per subtract-before-you-add, unit 2 deletes the BOOT pellet path and the flip-as-tuck-in before adding anything.

| # | Unit | Files | Check |
|---|---|---|---|
| 1 | The marble is a ball: `SampleTrace`, `World` substeps, desk baseline, bonks | `world.h/.cpp` (from `habitat.*`), `dish.cpp`, `test/test_world/` | the input tests above; replay hash moved deliberately; a "marble mid-roll" frame golden |
| 2 | Gestures and feeding: `gestures.def`, modes, `click`, `jar_tap`, lid needs stillness; delete the BOOT pellet and the jar-less pantry | `defs/gestures.def`, `v2_registry.h`, `senses.cpp`, `dish.cpp`, e2e owner drivers | static checks; senses traces; `body_only_full_life` with jar feeding |
| 3 | He rests at the centre: Rest, Foresee and Sleep walk home; the bubble anchor clamps to the disc | `actions.cpp`, `appearance.*` | Rest's mean radius below 0.15 after 10 s; goldens re-blessed and reviewed as PNGs |
| 4 | The oracle core: omens, topics, cues, `Oracle::step`, expect loci, the keepsake chunk | `oracle.h/.cpp`, `defs/omens.def`, `topics.def`, `cues.def`, `keepsake.cpp`, `test/test_oracle/` | tilt-then-shake learned (p above 0.5 in 40 pairings); an uncorrelated omen stays below 0.15; dawn forgetting is proportional; a reboot keeps E and drops the ring; stack check green |
| 5 | Foresee shows the vision; prophecy, vindication; `SeerGene` | `appearance.h`, `draw.cpp`, `creature.cpp`, `genes.*` | goldens for the bubble, the burst and the fog; `tilt_warns_of_shake` and `tilt_warns_overnight` pass |
| 6 | The 8-ball and the voice: `phrases.def`, `VoiceGene`, `compose`, `render`, wired to the `thoughts` marquee | `voice.cpp`, `defs/phrases.def`, the `thoughts` Thinker | `test_voice` enumerates every line against SOUL.md; `prophecy_follows_reality` passes |
| 7 | The outlook: anticipation and planning into `Brain::think`; the click | `brain.*`, `creature.cpp` | with forethought 0 the old brain tests pass unchanged; `clicker_shapes_call` and `spin_brings_snacks` pass |
| 8 | The game ring, `games.def`, Trick School | `games.h/.cpp`, `defs/games.def`, `draw.cpp` | `trick_on_cue` passes; a stub game is one row and one struct |
| 9 | Drum Circle and the family song | `games.cpp`, `SongGene`, `SongMemory` | a synthetic 600 ms beat is matched within 60 ms by the fourth onset; `song_inherited` passes |
| 10 | Flies: `Swarm`, the `Hunt` action, `HuntGene`, Fly Hunt | `world.cpp`, `actions.*`, `games.cpp` | breeding replays bit for bit; `fly_arms_race` and `hunter_learns_lead` pass |
| 11 | The day map and reunion | `oracle.cpp`, `dish.cpp` (catch-up fires reunion) | `waits_for_supper` and `reunion` pass |
| 12 | Curiosity and dreams | `oracle.cpp`, `brain.cpp` (episodes by drive change), stimulus novelty bit | `neophile_seeks_flies`, `routine_knock_goes_stale`, `dreams_of_the_shake` pass |
| 13 | Lore: `strongestLearned`, `OmenPriorGene`, named beliefs in `describeDiff` | `clutch.cpp`, `mutate.cpp`, `lineage.cpp` | `ritual_inherited`, `false_lore_dies`, `superstition_extinguishes` pass |
| 14 | The owner's hands and imprinting | `HandsDetector` in `senses.*`, `defs/senses.def` | synthetic profile traces separate; `strange_hands` passes |
| 15 | Temperament drift and the echo | `personality.cpp`, `mutate.cpp` (Epigenetic op) | `rough_childhood`, `maternal_echo` pass |
| 16 | Pace | `genes.cpp` (pleiotropic expression), viability | both pace extremes pass the dry run; `fast_vs_slow` passes |
| 17 | Oracle Pool and wishfulness | `games.cpp`, `SeerGene` | `honest_vs_wishful` passes |
| 18 | Phone | `EXPECT` verb, an `omen_prior` twist so the phone's simulated futures can seed his visions | golden transcripts; a malformed twist changes nothing |

## 9. Memory and compute budgets

Labels: **measured** (the sketch built with `-DSIZES` on x86-64 in WSL `survivor`; the structures hold no pointers, so the sizes carry to the ESP32-S3), **arithmetic**, or **estimate**.

| Item | Size | Label |
|---|---|---|
| Brain features today / with v2's rows | 20 / 31 | measured (`kFeatureCount` from the shadowed defs) |
| Oracle object at 11 actions | 3,056 B, of which E is 1,920 B | measured |
| Oracle at 13 actions (v2 adds `hunt` and `sing`) | about 3,190 B (E 64 x 16 x 2 = 2,048 B) | arithmetic |
| Brain `W` today / v2 | 3,520 B / 6,448 B (31 x 13 x 8 x 2) | arithmetic from measured counts |
| Brain episodes (16, dense features) | about +700 B | arithmetic |
| `World` (marble, 6 pellets, swarm, jar) vs `Habitat` | 320 B, about +180 B | measured / estimate |
| `OmenEffects` in the phenotype (derived) | 512 B | measured |
| Games, Personality, Hands, Song, Hunt | 80 + 36 + 60 + 12 + 8 B | measured |
| **New static RAM** | **about 7.7 KB** | arithmetic over the rows |
| Keepsake growth (E, day map, memories, prophecy, W, episodes, world, personality) | about 6.4 KB, so a creature's blob grows from about 9.5 KB to about 16 KB | arithmetic |
| Engine heap peak (save transient) | 16.9 KB today, about 23.3 KB in v2 | measured today, arithmetic for v2 |
| Free internal RAM at peak | 32 to 62 KB today (DESIGN.md section 8), about 18 to 48 KB in v2 | arithmetic |
| Flash: 16 omen glyphs at 24 x 24 8 bpp, the bubble, the jar, two fly frames, the phrase corpus | under 20 KB | estimate |

The pessimistic end (18 KB) is under the 24 KB floor that unit 20 enforces. Three levers, cheapest first. Store episodes sparsely (12 strongest features each, as the oracle's snapshots do), which saves about 1.8 KB static and 1.8 KB per save. Split the snapshot into two NVS keys (body and mind), which halves the save transient, because NVS blob writes need the whole blob in RAM. Draw the canvas in 48-row bands (23 KB instead of 115 KB), the lever DESIGN.md already names. If unit 20 finds PSRAM on the 1.28, none is needed.

Compute, all **estimates** at 240 MHz. The oracle closes one window and predicts each beat: about 12 active cues x 16 omens x 2 = 400 multiply-adds at 2 Hz. The outlook is 13 actions x 16 omens x 8 drives = 1,664 multiply-adds per decision, about one decision every 2 s. The marble is about 40 operations per substep at 50 Hz; six flies about 50 operations each at 10 Hz. Together under 10,000 operations a second, under 0.01% of a core. Drawing still dominates. The vision bubble adds a 40 x 40 composite, about 1% of the frame's compositing cost (estimate).

## Red-flag screen

- **Shallow module.** The oracle's surface is three per-tick or per-decision calls (`step`, `outlook`, `prophesy`), two lifecycle hooks (`dream`, `dawn`) and two lore hooks. Behind it sit the beat cadence, the ring, delayed-outcome learning, the day map, surprise, prophecy resolution and the vision. No caller coordinates two calls to finish one operation. `dream` and `dawn` could fold into `step` (it could take the asleep flag and see the dawn edge), which would make the surface smaller still; I kept them separate because the brain's dream has the same shape and the creature already paces both.
- **Information leakage.** The omen-to-topic-to-locus mapping exists only in `omens.def` and `topics.def`. Nouns live in `omens.def` and only the voice reads them. One leak remains. `isExpectLocus` hard-codes the 32..47 band, as `kRecentBase` does for recent loci; it belongs in `ids.h` as a named constant.
- **Temporal decomposition.** None. The oracle owns all knowledge of expectation, whenever it runs. The world owns physics for the marble, pellets and flies alike.
- **Pass-through.** None added. `Creature` applies `OracleOut` (it owns chemistry) rather than forwarding it.

## Synthesis decision

Filled in by arena.

## Tradeoffs accepted

- We accept a second learned table (about 3.2 KB of RAM and 2.3 KB of keepsake) in exchange for learning that never needs a drive to fall, and one source for visions, prophecies, surprise, routines, begging and clicker training.
- We accept that the oracle is linear in its cues, so "calling at 17:00 brings food" is two additive beliefs, in exchange for a 64 x 16 table instead of a 31 x 13 x 16 one.
- We accept losing the windows in flight on a reboot (at most 6 s of pending outcomes) in exchange for not saving the ring.
- We accept a two-part feeding gesture (raise the jar side, knock) in exchange for no accidental feeding and a BOOT button free to be the clicker.
- We accept a finite phrase corpus instead of generated text, so a test can check every line he can ever say against SOUL.md.
- We accept a capped Lamarckian echo (a quarter of the drift, scaled by a heritable plasticity) in exchange for a heredity the owner can read: "a hard life made the next one jumpy".
- We accept a second species, the flies, against the single-pet brief, because they are food, prey, omens and an opponent that evolves, which no other single addition gives (open question 2).
- We accept a gesture rebinding table, which the original design rejected, because v2's modes make BOOT and knock mean different things in different modes.
- We accept brain growth from 3.5 KB to 6.4 KB in exchange for the expect, click, cue and fly features he acts on.

## Alternatives considered

- **More brain tables** (the obvious path: more features, cross-action traces, an expectation table that only writes loci, as in depth proposal 6). Each table fixes one symptom, but learning still rides on drive change, so a content pet still learns nothing, and nothing ties the visions, the 8-ball and the routines to one model the owner can watch. The interface grows with every table; the oracle hides all of it behind `step`, `outlook` and `prophesy`.
- **Replace the brain with the oracle** (fully model-based: every action outcome an omen, every value from the genes). It is the smallest design, but rest, curl and follow-tilt have no event outcomes (fear falls by itself over time), so it would need a self-stimulus for every action, and it would throw away the one lesson that works today (punishment cut chasing by 96%). Keeping both, with `Outlook` as the only bridge, costs 3.5 KB and keeps what works.
- **A hazard-rate oracle with eligibility traces instead of the ring.** It saves about 700 B and needs one update per beat. But an event every 20 minutes has a per-beat probability of 1/2,400, about 14 Q15 LSB, so learning steps underflow, which is the same failure as nightly forgetting. It would need 4-byte cells, doubling E. It lost on resolution.
- **An ecology-first design** (the dish as an ecosystem of several species). On a 240 px screen with one pet the ecosystem becomes illegible. It survives as one axis, the flies, because they earn their place several times over.
- **The marble as a game that owns its piece** (DESIGN.md unit 24). Physics belongs to the world and games are rules; otherwise pellets and flies would roll only while the marble's game ran.

## Implementation reconciliation

Empty until implementation starts.

## Open questions and risks

1. Does the QMI8658 tap engine detect a knock reliably while the device is held tilted at 25°? Unit 20 must measure it. If not, should the jar pour on a steep tilt held 1 s (a salt shaker), at some risk of accidental pours?
2. Do you want the flies? They are a second species in a single-pet toy. Without them, A11 and Fly Hunt go, and feeding is the jar alone.
3. Is the Lamarckian echo (A7) acceptable heredity, or should drift die with each life?
4. Should the starter be an honest seer (wishfulness 40) or a wishful one? Honest makes the bubble trustworthy; wishful makes him funnier and hungrier.
5. Pet-hour names in prophecies ("a SNACK at the hour of the eel") need 24 short names in grungo's world. Do you want to write them?
6. The `thoughts` branch adds a heritable oracle chance. Which gene owns that byte, `SeerGene.oracleChance` or the branch's? There should be one.
7. Does the 1.28 have PSRAM? The pessimistic memory end needs one of the three levers in section 9 if it does not.
8. Should the rim show his prophecy tally all the time, or only after a prophecy resolves?

## Next implementation step

Unit 1: replace `Habitat` with `World`, feed it the 50 Hz `SampleTrace` from `Dish::sample`, integrate the marble with `BallPhysics` including the desk baseline and bonks, and land `test_world` with the crossing, settling, slope and restitution checks and a deliberately moved replay hash.
