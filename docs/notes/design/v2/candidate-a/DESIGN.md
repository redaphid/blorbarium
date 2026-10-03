# Blorbarium alife v2, candidate A: the seer's brain

**Headline.** Grungo is a seer, so his brain should be a predictor. v2 adds one learned table, **Foresight**: what he expects to happen next, cued by what is happening now. That table is what he sees when he scries (a vision bubble with real content). It is what he braces for (a foreseen shake gets no hop), what surprises him (curiosity), what he anticipates (the bell that means flies becomes a clicker, so tricks finally land), what he says when you shake him like an 8-ball (a grounded prophecy in his heritable voice, then held to account), and what he passes on (omens, the lineage's culture). BOOT becomes a **bell**. Feeding becomes a **pour** from a jar at the rim. The marble becomes a real 50 Hz physics toy. Five registry minigames are prediction problems he gets visibly better at, and each leaves something in the lineage.

Sketch files (they compile against `origin/engine` headers, with this folder's registries first; `sketch/check.sh` in WSL `survivor`):

| File | What it pins down |
|---|---|
| `sketch/include/blorb/foresight.h` | the expectation table, vision, prophecy, verdicts, omens |
| `sketch/include/blorb/games.h` | the games registry, `Skill`, the shared move policy, the wheel |
| `sketch/include/blorb/marble.h` | the 50 Hz marble, single writer of its position |
| `sketch/include/blorb/bond.h` | the owner print: familiar hands, the usual gap |
| `sketch/include/blorb/v2_genes.h` | ForesightGene, TalentGene, VoiceGene, LoreGene, LifeHistoryGene |
| `sketch/include/blorb/defs/{expect,games}.def` | the two new registries |
| `sketch/include/blorb/defs/{stimuli,loci,chemicals,care}.def` | v2 rows; `registry.h`'s static asserts pass on them |
| `sketch/usage_v2.cpp` | the call sites below, as code |

Measured by `check.sh`: every header and `usage_v2.cpp` compile with `-Wall -Wextra -Werror=narrowing`. The engine's registry derives 32 brain features from the v2 rows, up from 20. Sizes on x86-64 are `Brain` 8,504 B at 32 features, `Foresight` 1,584 B, five `Skill` tables 700 B, `Bond` 40 B, `MarbleToy` 28 B and `Games` 60 B.

---

## 1. Problem

The user wants "significant alife development across a variety of non-trivial, interesting axes", a marble driven by the IMU, a BOOT button that does something other than feed, and minigames he learns from. The coordinator added four asks: he rests at the centre and moves when acting (17), foresee shows what he really sees (18), a shake sometimes gets an 8-ball prophecy in his voice (18), and topics and voice are heritable (19).

Two measured facts shape everything. The e2e experiments found that only punishment changes him, and that nothing survives a night (`e2e-learning.md`, 3 of 11 scenarios yes). The depth survey found the cause is structural, not tuning alone (`learning-depth.md`):

- Reward is a drive falling, and the drives sit at zero.
- A stimulus is visible for half a second.
- Credit reaches one action only.
- Forgetting is a flat 0.14 a night.

The depth pass fixes the balance, the forgetting floor, interrupts, rot and tilt situations, learned heirlooms and one time lesson; I treat it as landed. What depth does not give him is **any model of the world**. W learns "doing a here changes drive d". It never learns "after the bell, a fly comes". So he cannot anticipate, cannot be surprised, cannot be clicker-trained with a cue that predicts food, has nothing to show in a vision, and has nothing true to prophesy. That missing model is the shape problem. Every axis below hangs off it.

Constraints honoured:

- Integer and deterministic: Fx and Q15 only, and the same seed and inputs give the same hash on host, device and WASM.
- Registries: one row plus a handler.
- Gene layouts frozen: a changed layout is a new type byte.
- Body-only play complete: the phone only adds special food and windows (asks 4 and 15).
- The board is the single writer.
- 327 KB internal RAM, PSRAM unverified.
- The SOUL.md voice (third person, lowercase, one CAPITALISED word, never doom).

## 2. Usage (caller's view)

### The owner, one evening (design fiction, ask 21)

At 18:00 you **ring the bell** (BOOT). Grungo, who was foreseeing at the centre of the face, now has a fly glowing in his vision bubble. He waddles to the jar at six o'clock and waits. You **tip the dish toward the jar** and a fly tumbles out. He eats it. Ten days of this and the bell alone makes him go to the jar, even in a new life, because his mother left the omen "the bell brings flies".

You **shake** him. He was not expecting it, so "eep!", a hop, and a moment later "...grungo knew. it is WRITTEN." Shake him the same way every evening after a double knock, and he stops hopping. His eyes glow and he announces "after the two knocks, the shaking. it is FORETOLD." Then he watches to see if he was right.

You **hold BOOT**. The game wheel opens on the rim. You **tilt** to the lily pads and **ring**. He hops onto the pad you hid the fly under three times running, because you always hide it left after right. He tells you so.

### The firmware (unchanged surface)

`src/main.cpp` keeps its five calls (`Dish(...)`, `sample`, `tick`, `appearance`, `flush`). v2 adds nothing to the shell. The marble physics runs inside `Dish::sample`, and the games and Foresight run inside `Dish::tick`. The new interfaces are deep: everything v2 adds hides behind a facade the shell already calls.

### The creature (the one new wiring, `usage_v2.cpp` section 1)

```cpp
// Creature::tick step 5, awake. Foresight runs first, so the expect_* loci and
// the anticipation are this tick's.
ForesightOut fo = foresight_.observe(features, fired, strength, n, drives, pheno_.foresight, tick);
for (size_t s = 0; s < kExpectCount; ++s) self.set(expectLocus(s), fo.expect[s]);
self.set(kSurpriseLocus, fo.surprise);
if (fo.verdict == Verdict::Fulfilled) self.fire(stim::self_fulfilled);
Decision d = brain_.think(features, drives, fo.anticipated, arousal, t, actionDone_, rng_);
```

### The genome author (most axes are genes on existing kinds)

```cpp
g.emit(expectLocus(6).v, brace, unit(300), timeByte(2 * kSecond), 0);       // he braces for a shake he sees coming
g.react(brace, 1, adrenaline, 1, kNone, 0, 1 * kSecond);                  // brace eats adrenaline: no hop
g.emit(locus::recent(stim::shake).v, wariness, 0, timeByte(3 * kDay), 0);  // shakes make him warier, slowly
g.receive(wariness, locus::posture.v, unit(200), 0, 128 + 64, 0);          // ...hood pulled tight
g.b.append(VoiceGene{/*style*/0, /*gravity*/60, /*oracle*/70, /*bar*/110, /*chatter*/40, {200,160,120,90,60}}, kGene);
```

### A game (`usage_v2.cpp` section 3): its rules and its context, nothing more

```cpp
uint8_t context = uint8_t(g.last[0] * 3 + g.last[1]);          // your last two hides
uint8_t guess = chooseMove(c.skill, context, 3, c.talent, c.rng);
learnMove(c.skill, context, guess, guess == g.hidden, c.talent);
c.out.fire(guess == g.hidden ? stim::game_point : stim::game_lost);
```

### The phone (optional window)

`STATE` gains `vision=fly:0.72,bell:0.31` and `prophecy=shake:0.64/40s`. New read verbs (one `commands.def` row and one handler each):

- `LORE` lists his omens, songs, knacks and rituals, with the generation that learned each.
- `SKILL` prints each game's table and favourite.
- `EXPECT` dumps the strongest expectation cells.

The website renders them from `SCHEMA` as today. No new twists are needed. `TWIST stimulus pour` joins `button`/`fed` as `403 BODY_ONLY`.

---

## 3. Shape

### Core data

| Structure | Shape | Owner | Saved |
|---|---|---|---|
| `W` (existing) | `Q15[feature][action][drive]`, now 44 x 13 x 8 | Brain | yes, remapped by stable ids |
| `E` | `Q15[feature][expect]`, 44 x 12: chance event e fires within his horizon while cue f is on | Foresight | yes, remapped by stable ids |
| `V` | `Q15[expect][drive]`, 12 x 8: mean drive change in the horizon after event e | Foresight | yes |
| cue traces | `Fx[feature]`, each the max of its value and its decayed trace | Foresight | no; a reboot forgets a few seconds |
| pending prophecy | slot, deadline, chance | Foresight | yes (so a save between promise and verdict is honest) |
| `Skill` | per game `Q15 win[16][4]`, fun, sessions | Creature | yes |
| `Bond` | gesture print (8 Q15), session mix, usual gap | Creature | yes |
| `MarbleToy` | position, velocity, queued pushes | Dish (replaces `Habitat::marble`) | yes |
| trait chemicals | `wariness`, `fondness`, `zest`, `brace`: ordinary chemicals | Chemistry | yes (already) |
| lore | `LoreGene`s in the genome | Genome | yes (already) |

Load-bearing decisions:

1. **Prediction is a second table, not more W.** W's columns are drives. Events are not drives. Folding "a fly is coming" into W would need a fake chemical per event and would make every vision and prophecy unreadable. E is small (1 KB) and legible. Each cell is a sentence: "after the bell, a fly within 8 s, 0.72". Per **foundational-thinking**, I traced the five readers (vision, loci, surprise, anticipation, prophecy) through this one structure, and none needs an index or cache.
2. **Expect loci are derived, never listed.** `expect.def` rows generate the loci at 40 + slot, and `registry.h` appends them to `FEATURES` the way it already appends the recent loci. One row gives a column, a locus, a feature, an icon and a topic. Per **encode-lessons-in-structure**, `expectSlotsDense()` and the locus-band `static_assert` make a bad row fail to compile.
3. **Anticipation reaches W as a value, not a reference.** `Brain::think` gains one argument, `anticipated[kDriveCount]`. Brain never sees E. That is the whole coupling, and it is how secondary reinforcement works:
   - The W update's observed change becomes the drive change plus the strengthening of anticipation since the action started.
   - Only strengthening counts. A promise made is felt. A promise kept is felt through the drive itself, so eating an expected fly is not fined for having been expected.
   - This asymmetry is the one rule in the design a reviewer should check twice (Tradeoffs).
4. **Most axes are genes, not code.** These need no engine code beyond the loci they read:
   - traits (wariness, fondness, zest);
   - bracing;
   - neophilia and neophobia;
   - separation sensitivity;
   - nightmare waking;
   - stress-accelerated maturation.

   Each is a chemical with emitters, receptors and reactions on existing kinds, so it is heritable, mutable and visible in `DIFF` for free. Creatures' central lesson (`research-creatures.md` B3) is applied rather than restated.
5. **Graded stimuli.** `SenseOut::fire(StimId, Fx strength = 1)`. A repeat in one tick merges by max. The recent locus takes the strength, and stimulus gene amounts scale by it. Two reserved bits of the frozen `StimulusGene.flags` byte become `soft` (below 0.5 only) and `hard` (0.5 and up), so one gene can make a marble kiss fun and another make a bonk hurt. The layout is unchanged.
6. **One writer per thing per tick**, per **separate-before-serializing-shared-state**:
   - The marble's position is written only by the physics. Chase and games call `push()` for impulses.
   - His body is owned by exactly one of reflex > game > behaviour, decided by code order in `Creature::tick`, not by a flag.
   - Foresight and Brain share no table.

### Data flow per tick

```
Dish::sample (50 Hz)  detectors -> SenseOut;  MarbleToy::sample (raw accel, contact -> marble_hit with strength)
Dish::tick   (10 Hz)  clock, habitat (jar pantry, flies rot), Games::control (wheel, bell, invitation)
  Creature::tick
    1 sense loci  2 stimuli x strength -> stimulus genes   3 chemistry (traits, brace, relive)
    4 lifecycle, reflexes (hop only if adrenaline survived the brace)
    5 even ticks: Foresight::observe -> expect_* / surprise / disappointment / verdict
                  Brain::think(features, drives, anticipated, ...)    | asleep: dream (salient episode, relive), Foresight::dream
    6 body: reflex, else game (Games::step), else behaviour
    7 face, stats, recent loci decay at the depth pass's heritable span
  Bond::observe (gesture print, familiar, since_contact, reunited)
```

### Invariants and where they live

| Invariant | Mechanism |
|---|---|
| a foreseen shake gives no hop | chemistry: expect_shake -> brace -> eats adrenaline (gene, so it can mutate away) |
| every prophecy is grounded | `ProphecyContent` comes only from `Foresight::prophesy`; vague when nothing clears the voice's bar |
| each prophecy is judged once | `Pending::live` cleared by the verdict; a save mid-window persists it |
| game moves are learned the same way everywhere | games call `chooseMove` / `learnMove`, nothing else |
| contexts fit the table | `static_assert(gamesFitSkill())` |
| basic food is body-only | `pour`, `fed`, `pellet_dropped` refused as twists |
| lore never grows the genome without bound | lore slots from pace (2 to 5) and a cap of 8 lore genes; weakest unconfirmed dropped first |
| imprint replays | an `Imprint{uid, before, after}` op in `MutationDiff`, so `apply(parent, diff) == child` still holds |

What the design deliberately does not do:

- no second W timescale (no STW/LTW pair, which doubles 9 KB);
- no general TD value function;
- no per-game neural net;
- no free-text generation (phrases are templates).

---

## 4. Alife axes

Eight axes. Each one names what develops, the mechanism, what you see, how it is inherited, and how you steer it.

### A1. Foresight: what he expects

- **Develops.** A map of cues to coming events: the bell brings a fly, the double knock brings a shake, dusk brings the lid, a marble on course brings a bump, 18:00 brings you.
- **Mechanism.** E learns by normalised LMS over cue traces that decay at the horizon gene's half-life (2 to 60 s). On a tick when e fires, the target is its strength. On a quiet tick the target is 0, at 1/H of the rate, so many quiet ticks do not drown a rare event and the cell converges to the chance within the horizon. While asleep E fades proportionally (no round-up floor, unlike today's W). `expect_<e>` loci are brain features, so W learns to act ahead: hide before a foreseen shake, wait at the jar after the bell. The hour tents from the depth pass are cues, so routines become expectations.
- **On screen.** While Foresee runs, a teal vision bubble rises above his head with up to two icons (fly, bell, hand, bolt, marble, pad, moon), each with opacity equal to its chance. A faint glyph of the cue that predicts it most shows inside (the "because"). With no confident expectation the bubble is swirling mist. Outside Foresee you see anticipation: he goes to the jar at the bell and hoods up at the double knock.
- **Inherited.** `ForesightGene` (learn rate, horizon, fade, credulity, value rate) mutates like any Mind gene. A long-horizon line connects causes minutes apart. A short one lives in the moment. Omens (A8) seed E at hatch.
- **Owner.** Be consistent and he predicts you. Be random and his vision stays misty.

### A2. Curiosity from surprise

- **Develops.** An appetite for what he cannot yet predict, which wanes as each thing becomes predictable.
- **Mechanism.** `observe` writes two loci:
  - `surprise`: the event's strength times one minus the chance he gave it;
  - `disappointment`: a chance that peaked above 0.5 and lapsed without the event.

  Genes decide what surprise is. A neophile line has an emitter that turns surprise into boredom relief and zest. A neophobe line turns it into fear. W then learns which actions lead to surprises: inviting a new game, chasing the marble, wandering. As those become predictable, their surprise and so their reward fade, and he moves on. Disappointment feeds loneliness (a missed meal or a missed visit).
- **On screen.** A "?" spark over the eye-stalks on a surprise. A neophile perks up and approaches. A neophobe hoods up and backs to the rim. He invites you to games he has played least.
- **Inherited.** The sign and gain of the surprise emitters, and the zest trait's imprint (A4).
- **Owner.** Vary play and he stays engaged. Run the same routine and he grows bored and restless, unless he is a neophobe, who settles into it happily. Genotype times environment, visibly.

### A3. Tricks and sequences

- **Develops.** Cue to action to reward tricks ("on a knock, spin"), and two-step chains ("spin, then croak").
- **Mechanism.** Three pieces. With the depth pass's interrupts and memory span, the parts already exist.
  - The bell becomes a conditioned reinforcer through E and V. Once the bell predicts flies, ringing it raises his anticipated hunger relief, which W credits to whatever he was doing. That is clicker training.
  - The knock is the command cue, since it interrupts and stays visible for his memory span.
  - A chain trace keeps the last two decisions and credits them at λ and λ², with λ from the depth temperament byte.

  The double knock is praise: it lowers boredom and loneliness directly, for owners who never feed with the bell.
- **On screen.** Knock, and he does his trick. The phone's `BRAIN` lists tricks as "on knock: hop circles (-0.31 hunger)".
- **Inherited.** Strong tricks become Ritual lore (A8) or depth's learned heirlooms. The chain λ is in the temperament gene.
- **Owner.** Pair bell and fly first, then ring the bell right after the behaviour you want. It works best before meals, because a full frog values flies less. That is honest, like a real animal, and he tells you through the glow.

### A4. Temperament drift and the imprint

- **Develops.** A personality that moves with how he is handled.
  - **Wariness** rises with shakes, drops and rough hands, and wears down with cradles.
  - **Fondness** rises with familiar hands, kept routines and fulfilled bells, and wears down with neglect.
  - **Zest** rises with games and surprises, and wears down with dull stretches.
- **Mechanism.** Three trait chemicals with half-lives of days, fed by emitter genes on the relevant recent and situation loci. Receptor genes read them:
  - wariness lowers his startle threshold (loci sum) and writes `posture`;
  - fondness damps the loneliness tonic while `familiar` is high;
  - zest raises arousal and the bounce of his idle bob.

  At death each trait chemical gene flagged `Imprint` (a free `GeneFlags` bit) moves its child's starting level a quarter of the way toward the parent's final level, capped at ±32 per generation. The move is recorded as an `Imprint` op in the Birth diff.
- **On screen.** Posture (hood loose or pulled tight at idle), bob vigour, and how quickly alarmed shows. The lineage text says "gen 5 starts warier: its parent was shaken 140 times."
- **Inherited.** Genetically, through the trait genes' gains. Epigenetically, through the imprint. A gentle line drifts gentle over generations, even before selection.
- **Owner.** Your handling is the parenting. It shows in this life within days and in the next egg at birth.

### A5. Attachment to the owner

- **Develops.** A bond, knowledge of your hours, and recognition of your hands.
- **Mechanism.** `Bond` keeps a print: the long-run mix of eight gestures (knock, double knock, shake, cradle, pour, bell, tilt play, drop) and the usual gap between sessions. It writes three things:
  - `familiar`: the cosine of this session's mix against the print;
  - `since_contact`: 0 just touched, rising to 1 over your usual gap;
  - `reunited`: a World stimulus fired when contact ends a gap longer than 1.5 times the usual.

  Hour tents in E learn when you come. A new `await` action walks him to the door (the bottom rim) to wait. W credits it when you then arrive. Genes decide how loneliness grows with `since_contact` (separation sensitivity) and what `reunited` does.
- **On screen.**
  - He waits at the door around your usual hour, and calls if you are late.
  - On your return he hops in circles (`reunited`).
  - With a friend's rough, unfamiliar hands he shows a wary face that he never shows with yours.
- **Inherited.** Separation sensitivity and the reunion response are stimulus and emitter genes (anxious, secure and aloof lines). The owner's hours can pass on as Omen lore. A superstition if the next owner differs.
- **Owner.** Keep a routine. Handle him your way.

### A6. Memory and dreams

- **Develops.** What he consolidates, and what he relives.
- **Mechanism.**
  - The 16-episode buffer keeps the most salient episodes (largest drive change plus surprise) instead of the last 16. This is depth proposal 5, and v2 depends on it.
  - Each dream replays one episode into W and fades E proportionally.
  - Dreams also relive: a new `relive` act locus (receptor genes, so vividness is heritable) adds that share of the episode's fear, discomfort and boredom change back into chemistry.
  - A nightmare can raise fear past an inverted fear receptor on `sleep_gate`, and he wakes startled. That one is genes only.
  - A sweet dream (a cradle episode) lowers need-touch overnight.
- **On screen.** The dream bubble shows the replayed episode's key event icon: the bolt for the shake before bed, the fly for supper. A nightmare wakes him with alarmed eyes in the dark.
- **Inherited.** Vividness (relive receptor gain), the dream rate and length (temperament), and the memory span (depth).
- **Owner.** What happens just before the lid matters most. A calm cradle before bed gives sweet dreams. Shaking at bedtime gives nightmares and a warier morning (it feeds A4).

### A7. Life-history strategy

- **Develops.** Fast or slow lives as a lineage strategy, the roguelite "build".
- **Mechanism.** `LifeHistoryGene{pace}` is one byte. `express_life_history` decodes it along a fixed curve:

  | Pace | Life | Learning | Clutch | Lore slots |
  |---|---|---|---|---|
  | 0 (slow) | about 15 days, long childhood | 0.6x | -1 egg | 5 |
  | 255 (fast) | about 5 days, short childhood | 1.6x | +1 egg | 2 |

  Life's clock is a Life-decay reaction the decode adds to `ChemRules`. Long life, fast learning and a big clutch cannot all be had, because they are one byte. The illegal "all good" build is unrepresentable. Plasticity is genes: a reaction where wariness catalyses Life's fall, and a receptor where wariness adds to `become_adult`, so a frightened childhood grows up and ages faster.
- **On screen.**
  - A life bar on the rim (the Life chemical).
  - Growth speed.
  - Clutch size.
  - The eggs pulse at their pace on the clutch screen, so you can choose a quick or a slow egg with the body alone.
- **Inherited.** Pace mutates like any byte. The plasticity genes mutate on their own.
- **Owner.** Selection at the clutch, and the environment (a harsh home makes faster lives). Speedrun lines churn generations and mutations. Deep lines carry more lore.

### A8. Omens: superstition and culture

- **Develops.** A lineage's beliefs, songs and knacks, passed down, confirmed or worn away.
- **Mechanism.** At death, `Creature::layClutch` writes up to the pace's lore slots of `LoreGene`s:

  | Kind | Taken from | Example |
  |---|---|---|
  | Omen | the E cells that moved most from birth (bias cues excluded) | "the bell brings flies" |
  | Ritual | the strongest W belief whose cue is a bias feature: a superstition he acts out | "at dawn, hop before eating" |
  | Knack | the best `Skill` cells | "guard left" |
  | Song | the Chant sequence sung most | four stones, in order |

  At hatch each lore gene seeds its table as a prior. Each later death checks it:
  - **Confirmed**: the cell is still strong. `heard` goes up and it is re-laid.
  - **Unconfirmed**: it fades by the credulity gene, and below a floor it is dropped.

  Songs copy note by note at the talent gene's fidelity, so the family song drifts. That is cultural evolution with copy errors.
- **On screen.**
  - The hatch marquee speaks the inheritance: "grungo VII remembers. the bell brings flies. (since gen 3)".
  - When content he hums the family song (the stones on the rim light in order, croak by croak).
  - Rituals are visible quirks.
  - The phone's Book of Omens shows each belief, the generation that learned it, and how many generations confirmed it.
- **Inherited.** This is inheritance. Credulity (how long a line keeps an unconfirmed superstition) is heritable, so skeptic and believer lines exist.
- **Owner.** Your routines become the family's omens. A new owner's lineage keeps the old owner's superstitions for a few generations, which is the point.

### Voice and the 8-ball (asks 18 and 19), across A1 and A8

- **Shake.** A shake he foresaw is braced: no adrenaline spike, no hop. Then the `oracle` locus (the voice gene's base plus vision) is the chance he prophesies instead. A shake he did not foresee gives the hop, and at the same chance he afterwards claims he knew. That is SOUL.md's "eep! hood up. ...grungo knew that would happen", now a mechanism.
- **What he says.** `Foresight::prophesy` weighs each event's chance by the voice gene's topic weights (Food, Owner, Danger, Play, Night). It names the strongest one and its "because" cue, or says "the mist is thick" when nothing clears the voice's `bar`. The thoughts registry phrases it through `style` (grave, fretful, smug, dreamy, terse) and `gravity` (how often a word gets capitals).
- **Held to account.** `promise` records the prophecy. If the event fires inside the window, `self_fulfilled` lifts vision (a glow burst) and fondness when you made it come true. If not, `self_mistaken` and a grumble ("the bubbles LIED"). A life's hit rate is a stat. The feat `true_seer` (12 fulfilled at 60% or better) wakes the dormant bright-glow gene.
- **Siblings differ.** Two siblings foresee different things in different words, and both drift down the line.

Example lines, grave style against fretful style, from the same E cell (bell to fly, 0.72):

- grave: "brrrup. the bell rings, and a fly falls. it is WRITTEN."
- fretful: "the bell... grungo thinks a fly comes? grungo HOPES so."

### Where he goes (ask 17)

He rests at the centre. Every action has a place:

| Action | Where |
|---|---|
| rest, sleep | centre (sleep curls slightly low) |
| foresee | just below centre, so the vision bubble has the top third |
| eat | the fly, wherever it rolled |
| await, call | the door (bottom rim), facing out |
| curl | the rim, away from the last shake's direction |
| chase | the marble |
| follow or flee tilt | downhill or uphill |
| hop circles | a small ring around where he is |
| play | the game's station (pond arc, pads, stones) |

---

## 5. Inputs, redesigned

### The marble, a real toy

`MarbleToy::sample` runs at 50 Hz from `Dish::sample` on the raw accelerometer, not the smoothed tilt locus:

- **Gravity.** The in-plane acceleration is scaled to a virtual 30 cm tray, so a 10 degree tip crosses the dish in about half a second (arithmetic).
- **Friction.** Rolling friction, plus a 1.5 degree static threshold so it rests on a desk.
- **Rim.** Restitution 0.6 with a click.
- **Grungo.** A soft disc (restitution 0.4). Contact fires `marble_hit` with strength equal to closing speed.
- **Shake.** No special case. The shake's jolts are in-plane accelerations, so the marble really flies around.
- **Knock.** A knock adds an impulse away from the tapped edge (the QMI8658 tap engine reports axis and sign; `BodySample.tapCode` gains them).
- **`marble_coming`.** A locus with the closing speed when the marble is on course for him, so E learns that a bump is coming, and W learns to dodge (flee) or meet it (chase).
- **One writer.** Chase and Rally only `push()`.

Starter genes:

- a `soft` marble_hit gene relieves boredom (a kiss is fun);
- a `hard` one adds discomfort (a bonk is not).

### BOOT is the bell

A short press rings the bell, a ripple from the bottom rim. At birth it means almost nothing: a small orienting turn toward the door, and boredom -0.05. It becomes what you make it:

- **Paired with flies**, it calls him to the jar and works as a clicker.
- **Paired with play**, it means "game".
- **Paired with shakes**, it makes him flinch.

It also accepts his game invitations, and starts the pointed game on the wheel. A **hold** (1.2 s) opens the game wheel, or ends a running game. Tuck-in moves entirely to face down (it was redundant with the lid).

### Feeding moves to the pour

The jar sits at six o'clock, with the pantry pips beside it.

- **Pour.** Tip the dish so the six o'clock edge drops more than 50 degrees, for 0.6 s, and one fly tumbles out. It re-arms once the dish is back under 35 degrees.
- **Empty jar.** It rattles and shows a clock glyph.
- **Food.** Flies drift with tilt (like pellets today) and die and grey after `rotMinutes` (today's rot).
- **Discoverable.** The `feed` care hint shows a tipping-dish glyph, so a friend with no manual still learns it.
- **Other food.** Pads and Snap give a few extra flies as enrichment. Never enough to replace the jar.

### Gesture table (no conflicts)

Modes are exclusive: one occupant, and at most one of wheel or game. A test drives each 50 Hz gesture trace in each mode and asserts it fires exactly its own row's stimulus and no other gesture's (`test_senses` `Gestures.EachTraceFiresOnlyItsOwn`).

| Gesture | How it is told apart | Alive (dish) | Wheel open | In a game | Asleep | Egg | Clutch |
|---|---|---|---|---|---|---|---|
| Gentle tilt, under 35 degrees | raw in-plane accel | rolls the marble, drifts flies; follow/flee tilt | points at a game | steering (Rally, Snap) or pointer (Pads, Chant) | marble rolls; he sleeps on | rocks the egg | points at an egg |
| Pour: six o'clock edge down past 50 degrees, 0.6 s | `PourDetector`; 35 to 50 degrees is a dead band | a fly from the jar | ignored | ignored (steep tilts steer) | a fly drops (it may rot by morning) | ignored | ignored |
| Knock | tap engine single, 400 ms gap | `knock`: command cue, flinch | ignored | Drum beat; else felt only | genes decide if it wakes | wobble | next egg |
| Double knock | tap engine double | praise | ignored | ignored | genes decide | wobble | pick |
| Shake | 4 jolts of 0.55 g in 900 ms | hop, or the 8-ball | closes the wheel | the game pauses through the hop | startle, genes decide | the egg rattles | ignored |
| BOOT press, under 1.2 s | release before 1.2 s | `bell` | starts the pointed game | game action (Pads hide, Chant stone, Rally launch) | felt, does not wake (gene) | ignored | next egg |
| BOOT hold, 1.2 s | reaching 1.2 s | opens the wheel | closes it | ends the game | ignored | ignored | pick |
| Face down 2 s | lid | tuck-in (melatonin) | closes the wheel | ends the game | keeps him asleep | ignored | ignored |
| Brief flip | face down under 2 s | discomfort | ignored | ignored | genes decide | ignored | ignored |
| Pick up, put down | tilt past 17 degrees for 0.4 s | felt | felt | felt | felt | felt | felt |
| Cradle | held, still, upright, 17 to 37 degrees, 3 s | comfort | ignored | ignored | soothes | warms (incubation) | ignored |
| Drop | free fall 100 ms, then an impact | injury | injury | injury, game ends | injury | injury | injury |

Conflicts resolved:

- Cradle is capped at 37 degrees and the pour starts at 50, so a still pour never counts as a cradle. Today's cradle has no cap, so a pour held 3 s would cradle.
- A pour is held, so it fires `picked_up` first. That is correct: you picked him up to pour.
- Games suppress the pour, because steep tilts are steering.
- On the 1.46 the BOOT hold is PWR, and the existing 1.5 s cap from DESIGN open question 2 still applies.

---

## 6. Minigames he learns from

`defs/games.def` holds one row per game (id, struct, unlock feat, context and move counts, icon) plus a struct in `games.h`. Every game follows the same contract:

- It owns his body while it runs.
- It fires `game_started`, `game_point`, `game_lost` and `game_won` (and `fed` for a caught fly).
- It plays and learns only through `chooseMove` and `learnMove` on its `Skill` table.

So W learns "when bored in the evening, inviting Pads pays" like anything else. A new **`play` action** walks him to the bottom with his favourite game's icon in a bubble (`Games::favourite`, by `Skill.fun`). A bell within 10 s accepts. Silence fires `invite_ignored` (disappointment).

Unlocks are roguelite meta: Rally and Pads from generation 0, the rest gated on lineage feats.

| Game | Rules | How he plays and learns | What he gains | What it teaches the lineage |
|---|---|---|---|---|
| **Rally** (marble) | His pond is the top arc. You start at the bottom rim and tilt the marble at the pond. He guards a third of it. If it enters outside his guard you score; if he blocks, he scores. First to 5. Real physics, so curved shots off the rim are skill. | Context: the marble's heading sector (3) times your last shot (3). Move: guard left, middle or right, chosen as the marble crosses the centre. He learns your aim habits and to read the heading. Block rate climbs over sessions. | boredom relief and zest; fondness for an owner who plays | a Knack ("guard left") and an Omen ("the hand shoots left") |
| **Pads** (mind games) | Three lily pads. Tilt to a pad and ring to hide the fly under it. He hops onto one. Nine rounds. He wins at 6 or more. | Context: your last two hides (9). Move: a pad. A random owner holds him at a third. A patterned owner gets beaten, and he says so ("grungo KNEW. the LEFT pad."). | up to 3 small flies per session; boredom relief; fondness | a Knack, and an owner-habit Omen that is a superstition for the next owner |
| **Snap** (timing) | Three swamp flies buzz in from the rim and drift with tilt, lighter and twitchier than the marble. He sits at the centre and snaps (the blep) or waits. A snap catches only if the fly is in reach on the next tick. | Context: distance bin (4) times speed bin (4). Move: snap or wait. He learns when to strike, since early snaps miss. Your role is cooperative: tilt slowly to bring flies into reach. | up to 3 fresh flies (the best food: fed plus boredom relief); zest | a Knack of strike timing; `TalentGene.tongueReach` under your egg choice |
| **Chant** (sequences, culture) | Four stones at N, E, S and W. Turns alternate. **He leads**: he croaks a sequence and you echo it (tilt to a stone, ring). **You lead**: you play a sequence and he echoes. A correct echo scores for both. | Context: position in the song (4) times the previous note (4). Move: a stone. Length is capped by `chantSpan` (2 to 8, heritable). The sequence you play most becomes his song. | boredom relief and fondness; he hums his song when content | **Song** lore, copied at `copyFidelity`, so the family song drifts and you can re-teach it |
| **Drum** (time) | Knock a steady beat. He croaks on the beat he predicts. Eight on-beat croaks win, and he dances (hop circles). | Context: time since your last knock, in 12 bins of 200 ms. Move: croak or wait. He learns your interval. The Skill prior peaks at the family tempo (`TalentGene.tempo`), so he learns near it fastest. | boredom relief and zest | a Knack of tempo; when content he taps his foot at the inherited tempo |

---

## 7. Proof: one e2e scenario per axis

These run in the existing harness (`test/support/e2e`, branch `e2e`) under the same rules, fixed before any run:

- 40 paired seeds; arms differ only by the treatment row.
- Effect is treatment minus control, with a 95% bootstrap CI.
- **Yes** means the CI excludes 0 on the predicted side and at least 75% of seeds moved that way.

Every metric is observable: `Appearance` (pose, position, face, vision icons, thought topic, dream icon) or `STATE`. No weights are read. Each scenario lands as `knownFailing` with its unit and must flip in that unit. Each has a **next-day** probe, because surviving a night is the measured failure.

| Axis | Scenario | Treatment | Control | Probe and metric | Predicted |
|---|---|---|---|---|---|
| A1 | `bell_before_meal` | days 1 to 3: bell, then pour 10 s later, at each of 5 meals | the same pours; bells at random waking times | day 4 at 15:00 (no meal due): ring and do not pour. Share of the next 20 s with a fly icon in `vision`, and seconds within 0.2 of the jar. Repeat day 5. | T higher on both days |
| A1 | `marble_on_course` | days 1 to 2: roll the marble at him (bumps) every 20 min | the same rolls, aimed to miss | day 3: roll at him. Share of rolls where he leaves the line before contact | T higher |
| A2 | `stale_world` | neophile genome; days 1 to 3, the identical play routine at the same hours | the same total play, varied gestures and hours | day 4: an invitation share for Snap, never played. Second arm with the neophobe genome | neophile T higher; neophobe T lower (sign flips with genotype) |
| A3 | `clicker_spin` | days 1 to 2, the bell before meals; days 3 to 4, knock, and ring when hop circles starts within 6 s | same, but the ring comes at a random time after the knock | day 5: knock 20 times. Share with hop circles within 6 s. Day 6 the same, after a night | T higher both days |
| A3 | `two_step` | as above, rewarding only hop circles followed by call | rewarding hop circles alone | day 6: share of hop circles followed by call within 5 s | T higher |
| A4 | `rough_parent` | parent shaken 20 times a day for life; dies, egg hatches in an unplugged gap | parent cradled 20 times a day | the child, untreated, on day 1: share of idle with `posture` hood pulled tight, and alarmed share after 4 probe knocks | T child warier on both |
| A4 | `gentle_lineage` | 4 generations of cradling owners | 4 generations of neutral owners | gen 4 hatchling: startle (hop) rate to 4 probe shakes | T lower |
| A5 | `routine_owner` | days 1 to 4: contact (pour plus cradle) at 18:00 daily | the same contact at random hours | day 5, no contact: share of 17:30 to 18:30 at the door (await), and calls. Day 6, return at 18:00: hop circles within 30 s | T higher on both |
| A5 | `stranger_hands` | days 1 to 4 gentle (knocks, cradles) | days 1 to 4 rough (shakes, flips) | day 5: a rough session. Share of alarmed or annoyed face | T higher (unfamiliar to T, familiar to C) |
| A6 | `bedtime_shakes` | 5 shakes in the 10 minutes before the lid, nights 1 to 3 | the same 5 shakes at noon | share of dreams showing the bolt icon; night wakings; first-30-minutes alarmed share each morning | T higher on all three |
| A7 | `harsh_childhood` | shaken 15 times a day during Baby and Child | cradled instead | age in ticks at the stage change to Adult | T earlier |
| A7 | `pace_selection` | 8 generations, always picking the fastest-pulsing egg | always the slowest | gen 8 lifespan and clutch size | T shorter and bigger |
| A8 | `omen_inheritance` | the parent's whole life with bell before meals; the child gets no bells | the parent's bells random | the child, day 1: probe bell; fly icon share in `vision` within 20 s, and the hatch thought topic is Food or Owner | T higher |
| A8 | `omen_extinction` | lineage from `omen_inheritance`; gens 2 to 4 never pair the bell with food | the lineage keeps pairing | gen 4 day 1 probe as above | T falls to control level by gen 4 (credulity default) |
| A8 | `song_inheritance` | the parent taught one 4-stone song 10 times in Chant | the parent never played Chant | the child, content and idle for a day: share of hummed sequences matching the parent's song | T above the 1/256 chance level, 95% CI |
| 8-ball | `predictable_shaker` | days 1 to 3: a shake always 10 s after a double knock | shakes at random times | day 4: double knock, then shake. Share prophecy (no hop); prophecy topic Danger | T higher on both |
| 8-ball | `prophecy_hits` | owner fulfils every Food prophecy (pours within the window) | owner ignores prophecies | `self_fulfilled` share by day 3, and fondness posture (open) | T higher |
| Games | `pads_owner_habit` | owner hides by a fixed cycle (L, M, R, L, ...) | owner hides at random | session 6 find rate | T above 0.6; C within 0.33 ± 0.1 |
| Marble | `marble_play` (today **no**) | marble rolled at him for 2 min when bored, days 1 to 3 | no rolls | day 4: chase share in the 3 min after a roll | T higher (flips with V1, V2 and the depth balance) |

---

## 8. Build plan

Every unit is small and names its check. Checks run through the depth pass's `tools/learnsim/gates.sh` (stack check, `native`, `native_san`, e2e). A unit is done when its e2e scenario flips from `knownFailing` to yes, or its unit test is green.

**Preconditions.** Both branches must have landed:

- `depth`: rebalance, proportional forgetting, learned heirlooms, interrupts and memory span, rot and tilt situations, hour tents.
- `thoughts`: the marquee registry and the oracle/voice gene.

V5 reconciles `VoiceGene` with whatever `thoughts` shipped.

The order is by fun per cost.

| # | Unit | Files (main) | Check | Fun | Cost |
|---|---|---|---|---|---|
| V1 | Graded stimuli (strength, soft/hard flag bits) | `senses.h/.cpp`, `creature.cpp`, `genes.cpp` | a hard knock raises fear more than a soft one; replay hash moved on purpose | enabler | S |
| V2 | Marble toy at 50 Hz, contact, `marble_coming` | `marble.h/.cpp`, `dish.cpp`, `habitat.*` (marble leaves), `draw.cpp` | traces: a 10 degree roll crosses in 0.4 to 0.7 s, rim restitution, bonk strength; golden `marble_roll`; e2e `marble_play` flips | high | S to M |
| V3 | Bell, wheel shell, pour and jar, tuck-in by lid only, care hint, pour refused as a twist | `senses.cpp` (`PourDetector`, cradle cap), `stimuli.def`, `care.def`, `dish.cpp`, `twists.cpp`, `draw.cpp` | `Gestures.EachTraceFiresOnlyItsOwn`; `body_only_full_life` fed by pours; golden `pour` | med (user ask) | S |
| V4 | Foresight core, expect loci as features, the vision bubble | `foresight.h/.cpp`, `expect.def`, `registry.h` (feature append), `creature.cpp`, `keepsake.cpp` (chunk), `appearance.*`, `draw.cpp` | `test_foresight`: learns bell to fly within 30 pairings, proportional fade, remap on a new row; e2e `bell_before_meal` and `marble_on_course`; golden `foresee_vision` | high | M |
| V5 | 8-ball grounding: brace genes, `prophesy`/`promise`/verdict, thoughts phrasing, `self_fulfilled` | `foresight.cpp`, `creature.cpp`, `starter_genome.cpp`, thoughts registry | e2e `predictable_shaker`, `prophecy_hits`; golden `prophecy` | high | S |
| V6 | Anticipation into `think` (strengthening only), chain trace, double-knock praise | `brain.h/.cpp` | `test_brain`: an expected fly being eaten is not fined; e2e `clicker_spin`, `two_step` | high | M |
| V7 | Games registry, wheel, `play` action and invitation, Pads | `games.h/.cpp`, `games.def`, `actions.*`, `dish.cpp`, `draw.cpp` | `test_games`: Pads above 0.6 against a cycle, about 0.33 against random; e2e `pads_owner_habit` | high | M |
| V8 | Rally | `games.cpp` (Rally), `draw.cpp` | block rate rises against a biased shooter over 6 sessions | high | S |
| V9 | Lore: omens, rituals, knacks; seeding, confirmation, credulity fade; hatch marquee | `v2_genes.h`, `genes.cpp` (lore), `clutch.cpp`, `mutate.cpp`, `lineage.cpp` | e2e `omen_inheritance`, `omen_extinction`; `apply(parent, diff) == child` over 10k seeds still holds | high | M |
| V10 | Traits and imprint | `chemicals.def`, `loci.def` (posture), `starter_genome.cpp`, `mutate.cpp` (`Imprint` op), `draw.cpp` (hood) | e2e `rough_parent`, `gentle_lineage` | med | S |
| V11 | Dreams: salient buffer (if depth did not), dream icon, relive, fear closes the sleep gate | `brain.cpp`, `creature.cpp`, `starter_genome.cpp`, `draw.cpp` | e2e `bedtime_shakes` | med | S |
| V12 | Surprise and curiosity genes | `foresight.cpp` (loci), `starter_genome.cpp` (neophile founder, a dormant neophobe gene) | e2e `stale_world`, both genotypes | med | S |
| V13 | Bond, `await`, `reunited`, `familiar` | `bond.h/.cpp`, `actions.*`, `stimuli.def`, `starter_genome.cpp` | e2e `routine_owner`, `stranger_hands` | med to high | M |
| V14 | Snap, Chant (with Song lore and copy errors), Drum | `games.cpp`, `genes.cpp` (talent), `draw.cpp` | each game's skill curve test; e2e `song_inheritance` | high | M each |
| V15 | Life history: pace decode, egg pulse on the clutch screen, stress-ageing genes | `genes.cpp` (life_history), `clutch.cpp`, `appearance.*`, `starter_genome.cpp` | e2e `harsh_childhood`; 8-generation `pace_selection` | med | S |
| V16 | Phone windows: `LORE`, `SKILL`, `EXPECT`, vision and prophecy in `STATE` | `commands.def`, `protocol.cpp` | golden transcripts | med | S |

### Memory budget, 1.28 board, no PSRAM assumed

| Item | Today | v2 | Label |
|---|---|---|---|
| Features x actions x drives | 20 x 11 x 8 | 44 x 13 x 8 (32 measured in the sketch, plus 6 hour tents from depth, 6 of the 12 expect loci flagged as features, surprise, disappointment, minus day_sin/cos) | arithmetic |
| `Brain` | 5,560 B (measured, ESP32-S3) | about 11.6 KB: W 9,152, episodes stored as Q15 1,696, chain trace 420, rest about 300 | arithmetic; 8,504 B measured at 32 features (x86-64) |
| `Foresight` | none | about 2.0 KB at 44 features | arithmetic; 1,584 B measured at 32 |
| `Skill` x 5, `Bond`, `MarbleToy`, `Games` | none | about 0.9 KB | measured (828 B, x86-64) |
| Static increase | | **about +9 KB** | arithmetic |
| Creature keepsake blob | about 9.5 KB (measured) | about 17 KB | arithmetic |
| Engine heap peak (one save's blob) | 16,879 B (measured) | about 24.5 KB | arithmetic |
| Free internal RAM | 32 to 62 KB (DESIGN 8) | **about 15 to 45 KB** | arithmetic |
| Flash: phrases (about 120 templates), game and vision art, code | none | about 6 + 15 + 25 KB of a 4 MB slot | estimate |

The low end breaks unit 20's floor of 24 KB free. The lever is already designed: draw the canvas in 48-row bands (23 KB instead of 115 KB), or move it to PSRAM if `ESP.getPsramSize()` finds some. **V4 must not merge before unit 20 has measured the real heap.** If the heap is short, the first cut is only 4 expect loci as features (saving about 1.7 KB of W).

### Compute

All figures are arithmetic at 240 MHz.

- Foresight's observe is about 1.1k operations at 5 Hz.
- `think` is about 4.6k multiply-adds at 5 Hz, only when deciding.
- The marble is about 50 operations at 50 Hz.
- Games are trivial.
- A prophecy is one template fill per shake.

All together that is under 0.05% of a core. Drawing is still the cost. The vision bubble and marquee add about 5% of the disc's pixels, so 25 fps holds.

Determinism:

- Everything new is Fx or Q15.
- The marble steps on samples, which are inputs.
- The coarse catch-up skips toys, games and Foresight, as it skips the brain.

---

## Synthesis decision

*Filled in by arena.*

## Tradeoffs accepted

- We accept that only strengthening of anticipation is credited. TD's symmetric form would fine an expected meal. In exchange, clicker training works and eating stays rewarding. The cost is that a cue whose promise is broken is not punished through W. Disappointment (a gene-driven drive) carries that instead.
- We accept a second learned table (about 2 KB) in exchange for visions, prophecies and omens that are readable sentences, rather than inferences from W.
- We accept about +9 KB static and +7.5 KB at save time, which may force the banded canvas. In exchange we get 44 features and 13 actions.
- We accept Lamarckian channels (the imprint, lore) in a genetics toy. In exchange your handling visibly shapes the line. Both are bounded (±32 per generation, at most 8 lore genes) and both show in `DIFF`, so heredity stays legible.
- We accept that game sessions are not saved (a reboot ends one) in exchange for no game state in the keepsake. Skills persist.
- We accept feeding by a 50 degree pour, more effort than a button. In exchange BOOT becomes the bell, and the feed gesture is physical and drawn (the jar).
- We accept template phrases over generated text, in exchange for SOUL.md safety. Every line is authored and filtered by the Never list.

## Alternatives considered

- **Predict events inside W**, with a fake "event drive" per stimulus. It needs no new module, but it exposes chemistry noise to every reader, and it makes "what does he see" and "why did he say that" unanswerable. It lost on interface depth: callers would learn W's internals to get a vision.
- **A full TD(λ) value function or Q-table over situations.** Textbook, but opaque. No cell is a sentence, so no vision, no prophecy and no omen falls out of it. It also costs more RAM than E plus V.
- **Games as brain actions** (one W column per move). It reuses W, but the columns would swamp it (5 games x 4 moves), and the contexts (your last two hides) are not features. Small per-game Skill tables with one shared policy keep a game to its rules.
- **Keep BOOT as feed and give the bell to a new gyro "spin" gesture.** The user asked for BOOT to do something besides eating, and the 1.28's spin needs a BodySample change and has no glyph a friend would guess. Rejected.
- **Feeding only through games (foraging).** Charming, but a sick or sleepy frog cannot play, and basic care must be one gesture. Games give extra flies only.
- **Copy the parent's whole W and E to the child.** Strong inheritance, but it erases the "born innocent" texture and swamps genetics. Lore is the curated, decaying slice.

## Implementation reconciliation

*Empty until implementation. Record each accepted deviation from this sketch and its acceptance source here.*

## Open questions and risks

1. Should the pour need the six o'clock edge specifically (default), or would any steep tip do? Directional pours prevent accidental feeding when he is carried in a pocket.
2. Is a lineage that keeps a previous owner's superstitions charming or confusing if the toy changes hands? The default is credulity fades in about 3 generations.
3. Should Pads and Snap feed at all? The default is at most 3 small flies a session, so a pet that plays lots is not unfed. Is that too generous?
4. Risk: the anticipation rule (V6) is the subtlest change to learning. If it destabilises the depth pass's balance, fall back to secondary reward from the bell only (a stimulus gene learned through V). That keeps clicker training and drops general anticipation.
5. Risk: 44 features thins each feature's share of NLMS credit, the same problem the bias features caused. Measure in learnsim; the depth pass's bias-feature handling should carry over.
6. Ask 20 (visual mutations, decals, clothes) is out of scope here. Lore could earn accessories (a `true_seer` bead). Should culture show on his body?
7. The ids for the v2 loci (21 to 24) may collide with the depth pass's new loci. They are renumbered at V4 against whatever depth shipped.

## Next implementation step

V1: add a strength to `SenseOut::fire`, scale stimulus gene amounts by it, and land the soft and hard flag bits, with the replay hash moved deliberately.

## Red-flag screen

- **Shallow module.** `Foresight` exposes `observe`, `vision`, `prophesy`, `promise`, `dream`, `seed` and `omens`, and hides traces, NLMS, V, verdict timing and the birth diff. `Games` exposes `control`, `step` and `active`, and hides the wheel, the variant and the policy. Neither makes callers coordinate stages.
- **Information leakage.** An expectable event's icon, topic, locus and column come from one `expect.def` row. A game's sizes and icon come from one `games.def` row.
- **Temporal decomposition.** Prediction, verification and inheritance of expectations live in the one module that owns E, not in predict, check and inherit modules.
- **Pass-through.** The existing `Creature::prophesy` (a twist setting one W cell) stays a pass-through to `Brain::setWeight`. v2 adds no new one: the 8-ball path adds policy (the oracle chance, the claim after a hop).
