# Constant sleep, and things to do at idle (#20)

The owner, 2026-10-21: "They all also sleep constantly! I need them to do more
interesting things at idle. Wide variety please. Fix whatever is causing the
constant sleep." And then: "just make the pet more interesting at idle
generally."

Branch `sleep-idle`, from `origin/main` 1be6aec.

## The cause

The test puffkin's `STATE`, every reading: `dish ... night=1 time=unknown`,
sleepiness 448 to 994, `action=sleep` or `face=asleep`, `[diag] gate=asleep:sleepy:0.99`.

With no phone the pet has no clock, but it kept a night of its own and
believed it (all file:line at 1be6aec):

1. **Unknown time meant night.** `PetClock::night()` (`senses.cpp:140-143`) is
   21:00 to 07:00 of *pet* hours, which run from the founding hour
   (`dish.cpp:114`: 9 for a fresh no-phone board, 0 for every board founded
   on 0.4.0) and never meet the wall's again without a phone, since every
   reflash, OTA and flat battery stops pet time. `DayDetector`
   (`senses.cpp:474`) set `light` to 0 for those ten pet hours in every 24
   powered ones, whatever the room.
2. **Dark makes sleep.** The starter genome's melatonin emitter fires on low
   `light` (`starter_genome.cpp:191`, a 15-minute fill for a day species, an
   hour for a night one), melatonin reacts into sleepiness
   (`starter_genome.cpp:192`), and the `sleep_gate` receptor
   (`starter_genome.cpp:193`, gain 2 from sleepiness 0.35) holds Sleep from
   sleepiness 0.475 (`creature.cpp` `kSleepOnset`) until it falls under 0.35.
   Through a pet night sleepiness climbs to about 1.0: the readings' 994.
3. **Nothing woke him.** A sleep ended only when the gate fell
   (`creature.cpp:174`: a stimulus gene's `wakes` bit, which no starter gene
   sets, or a duel's wake). From 1.0 at pet dawn, sleepiness halving every 90
   minutes takes about 2.3 hours to pass under 0.35, with melatonin still
   turning into it: every pet night ran on two to three hours into the pet
   morning, and no knock, pick-up or shake got him up.

So about 12.5 of every 24 powered hours asleep, at hours that have nothing to
do with the kid's. Measured (`test_idle_day` sweep, below): a 0.4.0-founded
pet (pet 00:00), no clock, on a desk with a kid looking every 4 to 25
minutes from 07:00 to 21:00, is awake 44 to 59% of the kid's day and asleep
0 to 13% of the kid's night. `STATE`'s `night=1 time=unknown` is item 1.

Not the cause, checked: the dark panel (low power is the shell's,
`main.cpp`; the engine never hears of it) and a still badge (nothing in the
engine read stillness) fed nothing. The flip build (`BLORB_ORIENT_HOME=0`)
turns x and y only (`orient.h toFrame`), so it cannot make a badge read face
down. A face-up badge reading face down would (lidded is dark, and keeps him
asleep): that is #15, still open on hardware, and the new `STATE` fields
below show it at once (`dark=1` while lying face up and handled).

The 2/5 sleep cut (7110168) made day naps rarer but left the night: it cut
the melatonin reaction too, so a night slept in dozes.

## The fix

Night needs evidence. `PetClock` (`senses.h`):

- `aligned` (saved): a phone's `TIME` set the pet hours to the wall's.
- `trusted` (each tick, not saved): `aligned` and wall time known this boot,
  so no unpowered gap since has gone unlived. `night()` is the pet hours'
  night only while trusted. A board with no clock never has a night.
- `stillRunTicks` (saved): ticks since anyone moved or touched the badge (a
  Body stimulus, or more motion than a table's, `dish.cpp handled`). After
  45 minutes (`kDimAfterTicks`) it is `dim()`: nobody is there and most
  likely it is night.
- `dark()` = night, lidded (face down and still 30 s), or dim. `light` is 0
  only when dark. With no clock, day_sin and day_cos read 0.5 (no hour), so
  a sleepy pet weighs sleep the same at any pet hour.

A hand gets him up (`creature.cpp kRousing`): a knock, a pick-up, a shake, a
drop, a flip, being righted or turned back up wakes him, his sleepiness drops
to 0.45 (under the nap's onset) and, where it is light, the night's
melatonin goes, so he stays up. A knock knock does not (it is a press: a
pellet, and feeding a sleeper leaves him asleep, `test_feeding`). Lidded he
sleeps on. In the wall's night he wakes and dozes off again. The hand that
ends a dim stretch rouses him too, unless it is the wall's night
(`Dish::runOneTick`).

The sleep cut keeps cutting the awake tonic (day naps stay rare); melatonin's
reaction runs at its genes' rate again, since it only flows in real dark now,
so a night he gets is slept through.

`STATE`'s dish line gains `dark=` and `still=` (minutes): `night=` is the
wall's night, `dark=` what he lives by.

## Before and after

`IDLE_DAY_SWEEP=1 IDLE_DAY_HOURS=72 IDLE_DAY_OUT=<dir> pio test -e native -f test_idle_day`;
`python3 scripts/test/idle_day_chart.py out.png before=<dir> after=<dir>`.
72 hours from 07:00, five species, no clock or a phone's `TIME` at the start,
face up on the nightstand or tucked face down at 21:00, a fresh no-phone
founding (pet 09:00) or a 0.4.0 one (pet 00:00). "Day awake" is 07:00 to
21:00, while the kid looks every 4 to 25 minutes.

| run (no clock, face up) | day awake before | after | night asleep before | after |
|---|---|---|---|---|
| puffkin, 0.4.0 founding | 46% | 99% | 13% | 89% |
| puffkin, fresh | 87% | 99% | 100% | 89% |
| mudge, 0.4.0 founding | 46% | 99% | 13% | 90% |
| mossby, 0.4.0 founding | 44% | 100% | 12% | 86% |
| grungo (sleeps by day), 0.4.0 founding | 59% | 100% | 0% | 63% |
| hushwing (sleeps by day), 0.4.0 founding | 59% | 100% | 0% | 57% |
| grungo, the Gift recipe, 0.4.0 founding | 82% | 100% | 0% | 51% |

Tucked in face down, every pet sleeps 99.9% of the night before and after.
With a phone's clock the fresh pets were already right by the wall (84 to
100% awake by day) and stay so (97 to 100%). A species that sleeps by day
(species.def `nocturnal`) is up for the first of the night, as its genes
say, and asleep from about 01:00. Before, a fresh pet's night also ran two
to three hours into the morning (asleep 07:00 to 10:00); now picking him up
at 07:00 gets him up. Charts: `outputs/sleep-idle/awake_asleep_72h.png`,
`awake_asleep_noclock_faceup.png`, `chart_before.txt`, `chart_after.txt`.

## Things to do at idle

When the brain picks something that would only show him standing about (a
rest, a wander, a tilt-follow or -flee on a level desk, a curl with nothing
to fear), his liveliness (per species, idle.cpp `kLively`, more when bored)
often makes it play: one of his idles (`defs/idles.def`), over a rest. The
action stays a rest, so no brain column, save layout or pet's beliefs move;
the brain learns what playing did as a rest's. `STATE` says `action=<idle>`.
The idle runs to its end unless something presses (hunger 0.55, fear 0.3,
pain or discomfort 0.3, sleepiness 0.5) or anything reaches him from outside
(a knock, a pellet, the phone).

| idle | family | where it lives |
|---|---|---|
| explore: round the rim, looking out | Nose | engine Rim move; lively gaze |
| chase_tail: spinning after his tail | Romp | engine Spin; lively whirl; props whirl lines; line |
| dance: wiggle, bounce, notes | Romp | lively; props notes; line |
| juggle: two pebbles over his head | Romp | lively; props pebbles; line |
| dig: dirt flying, a mound | Nose | lively squash; props dirt; line |
| stretch: up tall, then a yawn | Muse | lively; idleFace yawn; props effort lines |
| look_around: this way, that, "!" | Nose | lively gaze; props "?" "!" |
| peek: up to the glass, "HI!" | Show | engine Home; lively lean in; props; line |
| sing: notes rising | Show | lively; props notes; line |
| shadow_box: jabs at the air | Romp | lively jabs; props pow; line |
| sniff: steps, nose down, sniff | Nose | engine Roam; lively; props puffs |
| tidy: sweeps the dish | Muse | engine Roam; lively; props broom, dust |
| wave: at the kid | Show | engine Home; lively; props paw |
| roll_over: onto his back and up | Romp | lively shear and squash; props stars |
| daydream: a thought bubble | Muse | lively gaze up; props cloud with a cookie, heart, moon or star; line |
| stargaze: only in the dark | Night | lively; props twinkling stars; line |
| toy: pushes his ball about | Romp | engine Push; props ball |
| sneeze: ah... ah... CHOO (sparkles) | Muse | lively; idleFace; props burst; line |
| discover: a flower or beetle at his feet | Nose | lively; props flower or beetle, "!"; line |
| boo: sneaks up low, BOO! | Show | engine Home; lively crouch and pop; props "BOO!"; line |
| hide: in his hood, shell or wings | Show | engine curl pose; lively scrunch; props "!"; line |
| hiccup: hic, a hop each | Muse | lively; props "HIC!"; line |
| zoomies: laps of the rim, flat out | Romp | engine Zoom; lively lean; props speed lines; line |

Who does what: each species has its favourites (idle.cpp `kAffinity`, from
personalities/*.txt: puffkin bounces, rolls and sneezes sparkles; hushwing
looks about and stargazes; mudge laps the dish and races his tail; tinker
digs, discovers and plays with his ball; octopus shadow-boxes and juggles
with eight arms), each pet's own genes move every favourite 0.75x to 1.25x
(his genome hash), his drives pull a family (boredom romps, loneliness and
need for touch show off for the kid, curiosity noses about, sleepiness
muses, only the dark stargazes), and each idle has a cooldown (90 s to 400 s)
and never comes twice running, a family not twice if it can help it.

What it does to him: a romp eases boredom 0.18 and tires him 0.003 (naps
after play), nosing about eases boredom 0.12, showing off eases loneliness
0.12 and boredom 0.06, musing boredom 0.06, a stretch wakes him 0.02.

Small moments a kid notices:

- **A hello.** A knock, a pick-up, the badge righted (at most once a minute):
  his next play is for the kid, a Show idle or a dance, whatever its cooldown.
- **Moods in his posture** (`Appearance::moods`, lively.cpp `posture`), each
  in proportion to how far its drive is past its mark, so it moves as
  smoothly as the drive: perky with a little hop of good cheer when content,
  slumped and eyes to the floor when bored, drooping and watching the glass
  when lonely, heavy when sleepy, fidgety when hungry, hunched when hurting.
- **Surprise and mischief**: boo, hide, hiccup, zoomies, discover, sneeze.
- An idle's motion fades in over a quarter second and out over its last
  eighth (lively.cpp `idleEnvelope`), so none starts or stops with a jump
  (test_tween's tick-to-tick identity holds).

Pets near other pets are #33's: `IdleMood::friendsNear` is the hook, unread.

Lines (thoughts.def, text from personalities/_shared.txt through
`tools/personality.py build`), placeholders for #34 to rewrite: 136 la_la
(singing), 137 dance_break (dancing), 138 tail_get (chasing_tail), 139
treasure (digging), 140 peekaboo (peeking), 141 hyah (shadow_boxing), 142
pebble (juggling), 143 dream_big (daydreaming), 144 wish_star (stargazing),
145 achoo (sneezing), 146 boo_hee (booing), 147 hic (hiccuping), 148 zoom
(zooming), 149 ooh_what (discovering), 150 cant_see (hiding). All priority 7
or 8 (they answer what he is doing now), each a situation in
`.claude/skills/pet-personality/references/situations.md`.

Variety, measured: over 72 hours by day, 29 to 32 different things on the
glass (an idle by name, anything else by its pose), 11 to 15 of them in an
average two minutes awake (`Idle.AWideVarietyEveryFewMinutes` holds at least
3 and 14). Film strips: `outputs/sleep-idle/strip_before.png`,
`strip_after.png` (5 species, a frame every 2.5 s for 4 minutes, from 40
minutes after founding, each labelled with `STATE`'s action).

## Hashes and goldens that moved

All from this change, re-blessed after looking:

- `Replay.TwentyFourHoursOfTheSameRoutineGiveTheCommittedHash`: c1a2eec8 ->
  c74fada3 (and the device's `HASH` reply): the still run and `aligned` in
  the clock's hash, no believed night, rousing, idles (their rng draws, drives
  and faces) and the sleep cut's reaction undone.
- `Gift.*` and `NoImmortals.TheGiftAndEveryStarterStayViable`: the Gift's
  lifePrint c6f5005b -> 3c6de705 (all feats 69356d6e -> 76ee538c). The genome
  bytes and hash are unchanged; the phenotype moved with 7110168's sleep cut
  (never re-blessed then) and with the melatonin reaction restored here.
  Every starter stays viable.
- `test_present` Golden.*, Mouth: `night=0` (unknown time shows his day),
  and what the scripted pets did (idles over rests, no foresight glow from a
  pet hour he no longer has).
- `test_protocol` Golden.State, Chem, Clutch: `night=0 ... dark=0 still=N`,
  `light=1000` and day_sin/day_cos 500 with no clock, no melatonin; the
  clutch's previews follow the life before it.
- `FirstHours.AFreshPetPlaysThroughHisFirstSixHours`: re-floored. Seated (a
  look every 3 to 10 minutes): at most 0.5 h asleep, no sleep over 30
  minutes. Nobody: he goes to bed once it is dim (first sleep after 45
  minutes). The old floors (a nap, at most 2.5 h asleep) came from the pet
  night he no longer has.
- `FieldPets.ASleepingPetStillTalks`: tucked in face down rather than left in
  a pet night that no longer exists; the sleep talk floors are unchanged.
- The tests skipped since 7110168 (Replay, Gift, NoImmortals, FirstHours,
  FieldPets) run again.
- Keepsake: the clock's two fields and the idle state are trailing bytes,
  written only when set, so the committed v1, scars and friend-egg fixtures
  re-encode byte for byte.
