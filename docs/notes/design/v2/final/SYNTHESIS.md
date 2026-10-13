# Synthesis: blorbarium alife v2 (arena phases D to F)

Inputs:

- three candidates: A "the seer's brain", B "the oracle loop", C "the Almanac";
- the binding `user-asks.md` (1 to 24);
- the five stories in `stories/stories.md`;
- `origin/engine` at a1cb726;
- the in-flight worktrees `depth` (u1, u2, u35, u4), `thoughts`, `dialogue`, `hw-unit20` and `visual-center`, read from their working trees because `thoughts` and `dialogue` have no commits yet.

No candidate dropped out.

## Convergence

All three candidates built the same core: a learned linear table `E[cue][event]` that predicts which stimulus arrives within a horizon. They agree on the rest of the shape too:

- it feeds `expect_*` loci to the brain;
- it is the source of the foresee vision, the 8-ball and surprise;
- it hands anticipation to `Brain::think`;
- it passes strong expectations to the next generation as lore genes.

Per the arena rule, the final design ships that consensus shape. Grafts change its details, not its form.

## Base: candidate B

Each candidate was scored per criterion. My scores and the cross-judge's (Fable, read-only, rubric only) match on the pick.

| Criterion | A | B | C | Deciding evidence |
|---|---|---|---|---|
| Extensibility (boundary, API size, one-row additions with compile checks) | 4 | 5 | 3 | B's `Outlook` is the only coupling into the brain. `v2_registry.h` checks routes, omens, topics and game captures at compile time. C's `Journal::dreamStep(Brain&, ProtectedCells&)` writes W from outside the brain |
| Learning that works where e2e failed | 3 | 5 | 4 | B labels whole windows 0 or 1, so rare events do not underflow Q15. A's per-tick target of 0 "at 1/H of the rate" underflows (about 0.06 LSB per step for p = 0.05, H = 60), which repeats the flat-forgetting failure at 10 Hz |
| Coverage of asks and stories | 3 | 4 | 4 | B lacked a scored marble game; C lacked a body-only way to choose a game; none covered the hour-long oracle lockout |
| Body-only, conflict-free gestures | 4 | 5 | 3 | C needs the unconfirmed gyro and gives BOOT four timing-windowed meanings |
| Proof | 4 | 4 | 5 | C's yoked controls and reactive trainer rows are the strongest method |
| Coherence | 5 | 4 | 3 | A's "most axes are genes on existing kinds" is the tightest model |
| **Cross-judge total** | 23 | **27** | 22 | |

**Why B.** A maintainer extends B most easily. Its new state hides behind three calls (`step`, `outlook`, `prophesy`). The brain gains one struct argument and nothing else. A foreseeable event, a game and a gesture meaning are each one row, refused at compile time when malformed. It is also the only candidate whose learning rule reasons about Q15 resolution. `user-asks.md` item 6 named B as the base in the v1 arena, and the v2 choice agrees, but the scores above were made against the rubric, not that item.

## Grafts, each with its source

| Graft | From | Why, and how it was folded in |
|---|---|---|
| Learns from whether things happen (delayed window labels in a ring); proportional dawn forgetting | B (base) | Kept as the core. Dawn now takes a saved day index, so the catch-up can call it twice safely |
| **Only strengthening of anticipation is credited** (`strengthening`, static_asserted in `oracle.h`) | A | Fixes a defect in B. B's symmetric `(drive + anticipated)` credit charges the relaxing promise back to Eat, so an expected pellet was worth 0.08 instead of 0.4 |
| The bell gains value only by predicting food | B | BOOT's press is `bell`, born with no stimulus-gene amounts, so the clicker effect comes only from E. B's version was named `click`; story 1 calls it a bell |
| **Bracing**: a foreseen shake gets no hop | A | One emitter gene (`expect_shaking` makes brace) and one reaction (brace eats adrenaline). B's rule did the opposite: its odds `chance * (1 - expect_danger)` made a frightened frog hop more |
| **Kept-or-broken prophecy log** and lifetime hit counts | C | An 8-entry `ClaimRecord` log in the oracle, the `PROPHECIES` verb, and `made`/`kept` in the Death entry. Story 2 asks for hit rate as a lifetime stat |
| Claim kinds (omen fires, or a drive above a level) | new, from story 5 | The phone's "tomorrow, 2pm: a hungry frog" is a drive claim, not a stimulus. One enum in `Claim` makes it checkable |
| **Fly population** that evolves against his line | B | Kept as A9 and U15 behind open question 1. The camera twist's coloured fly reuses it (story 5) |
| **Culture**: lore laid at death from learned change, confirmed or faded | B (mechanism), **A (`LoreGene` shape)** | A's single gene kind with `kind`, `gen` and `heard` replaces B's separate `OmenPriorGene` and `SongGene`. Routine (story 1's bell hour) and Knack (story 3) kinds are added |
| Per-game `Skill` table and the shared `chooseMove`/`learnMove` policy, with `gamesFitSkill` | A | B's games learned ad hoc. Every game now plays and learns through one policy, so a game is its rules and its context |
| **Keeper** (A's Rally) as the scored marble game | A, story 3 | B had only the ambient marble. Its Skill context is heading x tilt direction, which is how story 3's "watch the tilt" is learned |
| Pads, Snap, Drum | A (Pads, Drum), A and C (Snap), B (Drum Circle) | One list (section 6). Snap takes story 3's mechanic: you press, he learns your timing, then snaps first |
| Drill (B's Trick School), with a knock as the cue | B, story 3 | B's three tilt cues (wave, left, right) are gone. A knock is the one trick cue, so tilt play never issues a command |
| Pet-initiated game invitations, a bell accepts | A and C | With a rate limit (once per 20 min, ignores double the gap) for C's nag risk |
| **Stations**: one column, one walker | C | Generalises the `visual-center` worktree's walk-home. Each game names its places through `GameAsk::GoTo` |
| Gesture corpus lint (recorded traces, exact stimuli per mode) | C | Beside B's compile-time route checks. One catches detector regressions, the other catches registry mistakes |
| Yoked controls and reactive trainer rows in the e2e harness | C | Every operant scenario uses a yoked control |
| A next-day probe on every scenario | A | Surviving a night is the measured failure |
| Trait chemicals (wariness, fondness, zest) and the capped `Imprint` op | A | Replaces B's `Personality` struct. Genes only, so heritable and in `DIFF` for free. B's "forewarned is forearmed" is kept as genes on the `surprise` locus |
| Owner print (`Bond`: gesture mix, usual gap, `reunion`, `strange_hands`) | A's content, placed in B's `World` | Replaces B's six-feature `HandsDetector`, whose raw handling features need unmeasured hand traces to tune. In the world chunk, the dish keeps knowing you across lives |
| Graded stimuli and the soft/hard flag bits | A | The marble's bonk strength. Bit 2 of the same byte is B's novelty bit, which makes a solved game stop paying |
| Life-history pace, one pleiotropic byte | A and B | `PaceGene` with A's decode table |
| Every line he can say is checked against SOUL.md and the Never list | B's corpus test | Folded into the `thoughts` static checks and `dialogue`'s blocklist rather than a new test |

## Rejections, each with a reason

| Rejected | From | Reason |
|---|---|---|
| Per-tick E update toward 0 at 1/H | A | Underflows Q15 for rare events (arithmetic above). The ring's window labels do not |
| Learned value table `V[event][drive]` | A | `OmenEffects` derives the same values from the stimulus genes with no learning, no extra table and no extra failure mode |
| Symmetric anticipation credit | B | Discounts an expected meal (above) |
| `oracleChance * (1 - expect_danger)` | B | A frightened frog hopping more contradicts bracing. Story 2's lockout comes from `oracle_tired` instead |
| `SeerGene.oracleChance`, `topic[6]` and `VoiceGene` (B); `VoiceGene` (A); `voice` and `oracle` genes (C) | A, B, C | `thoughts` already ships `OracleGene` (chance, voice, topic weights). It stays the one owner, widened to 12 topic bytes. `SeerGene` keeps only how he predicts |
| B's phrase corpus (`phrases.def`, `Utterance`, `render`) | B | Duplicates the thoughts registry and the dialogue tables. Grounded prophecies are thoughts rows, one per omen |
| Feeding by tilt: A's 50 degree pour, B's jar tap, C's tip-to-pour from three bins | A, B, C | Stories 1 and 5 put basic food on the button ("a hold drops a pellet", "basic food only comes from the button"). B's own open question doubts the tap engine at 25 degrees. C accepts accidental spills. A hold is one-handed, discoverable and never accidental |
| BOOT as a status card, lullaby and call (C) | C | Four timing windows on one button. Tuck-in moves to the lid |
| Trick School's tilt cues (wave, left, right) | B | Three more gestures that collide with tilt play |
| Oracle Pool | B | The 8-ball is already ambient, so a game that only removes the hop adds a row without a new experience |
| Chant | A | Overlaps Drum's family rhythm; sequence culture can return as a row |
| Pocket | C | Keeper covers marble skill and is the story's game |
| Freeze | C | Uses the click as a stop signal while the click is the conditioned reinforcer. A Freeze miss would extinguish the clicker (the cross-judge found this) |
| Echo | C | Overlaps Drum |
| Peek | C | Good and cheap, but not on a story's path. It is the worked example in the extension table |
| Almanac appraisal scaling of every stimulus; a click paying real hunger relief through `hope` | C | Couples the predictor to every chemistry amount, and feeds him with a button |
| Two rings of full feature snapshots | C | B's 12-cue sparse snapshots cost a fraction. The day map covers the minutes-to-hours scale C's far ring was for |
| `Journal` with protected bits; dream "mix" hypotheses | C | A second forgetting path beside depth u2's proportional forgetting and the oracle's dawn. Salient episodes (depth proposal 5) are kept |
| Lore probation and genetic assimilation after three confirmed generations | C | Lore is already genes that fade unless confirmed (`heard`, credulity). A second hardening path adds rules without a new behaviour |
| Taste, three foods and Garcia aversion | C | No story or ask needs it. It conflicts with "basic food is the button". SOUL.md's Never list makes nausea copy risky. Deferred |
| Curiosity as learning progress (two error EMAs per target, a `curiosity` drive) | C | In this dish the noisy source is the owner, so the noisy-TV trap it guards against is a feature. The novelty bit plus `SeerGene.curiosity` gives story 3's staleness |
| Spin and Coriolis on the marble | C | Needs the gyro, which unit 20 has not confirmed. Kept as an extension row |
| Sensitive periods for traits | C | No story demands it. A one-line stage multiplier can come later |
| B's `HandsDetector` (six raw handling features) | B | Needs real hand traces to tune; A's gesture-mix print gives the same stranger story from existing detectors |
| Hour-bin brain features for routine | depth proposal 7 | The day map gives per-topic routine at quarter-hour resolution without widening W. If depth u35 lands hour bins anyway, they stay |
| A chain trace in W (A) and residual credit across episodes (C) | A, C | The oracle's current and previous action tokens give begging, the lucky hop and two-step chains with no second credit path in the brain |
| The banded canvas and other no-PSRAM levers | A, B, C | 2 MB PSRAM is measured. Cold buffers move there; the canvas stays internal because it is the hottest loop |
| Whole-genome diploidy | story 4 | Touches every gene kind, mutate, the diff and the keepsake. Diploid `PartGene` gives "skips a generation and comes back" where it is visible (open question 4) |

## Story demands, one by one

**Story 1, the breakfast bell.**

1. Button is a bell; a click is a cue; a hold drops a pellet. **Met.**
2. One click and two clicks are separate stimuli the brain sees. **Changed.** Two presses open the wheel (story 3), and the trick cue is a knock (open question 3).
3. Interrupting stimuli and a heritable memory span. **Met by depth u4** (dependency of U8).
4. Hour bins and a Beg action that pays when a pellet drops. **Met** by the quarter-hour day map and the `beg` action at the Bell station (U8, U9), not hour-bin features.
5. Forgetting proportional to the weight. **Met by depth u2** for W, and by the oracle's dawn for E.
6. Heirlooms ranked by learned change, so the bell hour passes and a cue trick mostly does not. **Met** by depth u2 for W and Routine lore for the bell hour (U12).
7. Credit for the last few actions; the lucky double hop. **Met differently**, by the oracle's action tokens and `actionBias`, not a W chain trace.

**Story 2, shake him and ask.**

8. The shake forks on a heritable oracle chance. Shakes close together raise stress, switch the oracle off for an hour and teach that shaking is unpleasant. **Met** (`OracleGene.chance`, `oracle_tired`, `oracle_gate`, the shake's discomfort; U7).
9. An expectation table that predicts the next stimulus in a window; the vision shows the top prediction and window; a later check marks it kept or missed. **Met** (U4, U5, U7).
10. Pictograms (knock, bell, pellet, marble, dark, egg, phone) and an absurd corpus for low confidence. **Met except the egg.** Foreseeing his own clutch sits near the Never list and is not a stimulus today. It can be one row later. Improv topics carry the absurd bits.
11. A phrase grammar driven by voice genes (thinking sound, capital word, hedging, length, topics); every line through the Never list. **Met** by thoughts' voices, frames and `firstOnly`, a `hedge` frame column, and dialogue's voiced lines and blocklist. The capital word needs lowercase (open question 5).
12. A kept prophecy makes him proud; hit rate is a lifetime stat. **Met** (`prophecy_kept`, the claim log, `made`/`kept` in the Death entry).
13. Halo colour and strength from the glow genes. **Already in the engine**; violet is a palette gene.

**Story 3, marble season.**

14. A game registry; a double press opens it, tilt picks, a knock starts; play feeds drives and learning. **Met** (U10).
15. Marble physics from the IMU at 50 Hz with rim bounces and a goal arc. **Met** (U2, U11).
16. Tilt direction and marble speed as brain features. **Met in part.** `marble_coming` is a brain feature. Tilt direction is in Keeper's Skill context, where the reading happens, instead of widening W by four columns.
17. Curiosity from surprise; a predictable game stops paying; a heritable curiosity gene sets staleness. **Met** (the novelty bit, `SeerGene.curiosity`, surprise emitter genes).
18. Per-game scores on the rim and in the lineage book. **Met** (`Skill.best`, `GameResult`).
19. He rests at the centre and each game names its positions. **Met** (U1, `GameAsk`).
20. Fly snap where he learns Peter's timing and snaps first. **Met** (U15).
21. Inventing fetch (pushing the marble back). **Deferred.** It needs a fourth Keeper move ("push uphill"). Add it after playtests of U11.

**Story 4, seven grungos.**

22. Decal genes as overlays in body coordinates (warts with count and placement, mottling, rare growths). **Met** by `PartGene` layers through the existing mark seam (U23), pending art frames.
23. Accessory slots filled by mutation and by the hoard; cloak and skin recolour. **Mutation and recolour met** (U23 and existing palette genes). **The hoard is deferred** to U25 because it needs trinket pieces and accessory art.
24. A diploid genome with dominance. **Met for visible parts only** (`PartGene`, `Segregate`); whole-genome diploidy rejected above.
25. Clutches whose shells hint at their genes, picked with tilt and a knock. **Met** (U3 gestures, U24 shells).
26. Plain death with a cause, remains with the egg, a run summary. **Already in the engine**; the run summary card is U24.
27. Lineage feats as meta-progression and a cloak-dot ring on the rim. **Feats exist**; the ring is U24.

**Story 5, tap me with your phone.**

28. Bog sleep. **Met** (`torpor` genes; the catch-up treats a gap as dark and still; U16).
29. A time-unknown marquee and an idempotent catch-up. **Already in the engine** (DEVIATIONS 3; `hw-unit20`'s system clock). v2 adds only the oracle's idempotent dawn.
30. A snapshot to a WASM engine, hundreds of futures, a prophecy twist with a window and a checkable condition. **Board side met** (`Prophecy` with an omen or drive `Claim`, `accept`; U18). The WASM page is the engine's DESIGN unit 21 and is not part of this design.
31. A twist registry (prophecy, coloured fly, rain, name). **Met** (U18; `rename` exists).
32. Prophecy outcomes, kept or broken by Peter, as stimuli with lines. **Met** (U7, U18).
33. A phone page with the futures fan, the family tree and cards. **Website track**, outside the engine.

## User asks: notes where the design departs from a literal reading

- **Ask 13 names the shake hop as a games seed.** The hop stays a reflex and the 8-ball's other branch. Making the shake a game would let a game capture it, which the design forbids, because a shake must always reach him.
- **Ask 16 says BOOT does something besides eating.** BOOT now rings the bell (a press) and opens the wheel (two presses), and a hold still feeds, as the stories asked.
- **Asks 22 to 24.** The dialogue generator must cover the new grounded prophecy rows and the `play` topic. U0 gives it one id source.

## Defects found in in-flight work (for the coordinator, not fixed here)

1. **Gene type 0x14 is claimed twice.** `thoughts` uses it for `OracleGene`, and the `depth` u4 worktree uses it for `MemoryGene`. Whichever lands second would break genome decoding of the first. U0 settles it.
2. **Voice ids disagree.** `thoughts`' `voices.def` has `terse` 0 to `mystic` 5. `dialogue`'s `voices.toml` has `mystic` 0, `paranoid_hoarder` 1 and adds `forecast` 6. Generated lines would land in the wrong voice. U0 makes `voices.def` the one source, read by name.
3. **`OracleGene.topics` is frozen at 7**, but `dialogue` already generates topics 7 to 9 (shaking, sleep, held). Widening after it lands needs a new gene type. U0 widens it to 12 first.
4. **The marquee font has no lowercase**, so story 2's one capital word cannot show (open question 5).

## Verification

- `sketch/check.sh` in WSL `survivor` against `origin/engine` a1cb726:
  - `usage_check.cpp` and each of `v2_registry.h`, `genes_v2.h`, `oracle.h`, `world.h` and `games.h` compile alone with `-Wall -Wextra -Werror=narrowing`;
  - five planted rows each fail with their own message (duplicate route, routed hold, improv omen, missing station, shake capture);
  - the four `strengthening` cases are static_asserts;
  - sizes are measured: 31 features, 14 actions, 65 cues, 16 omens, 11 topics, 6 games; W 6,944 B; Oracle 4,512 B; Games 884 B; World 360 B.
- The story-demand list covers all 30 bullets under the stories' "What this demands of the design" headings (counted with awk, 6 per story). One bullet is split (story 1's clicks), and two demands from the story text are added (fly snap and fetch), giving 33 items.
- Not verified, because only the build can verify them: learning rates, the e2e predictions, the marble's feel and the free internal heap on the board. Each is a named check in `BUILD-PLAN.md`.
