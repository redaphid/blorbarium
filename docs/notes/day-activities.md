# Different things through the day, by how he feels, his age and his genes (#61)

The owner, 2026-10-22: "I also want the pets to appear to be doing different
things throughout the day. The behavior and animation should vary depending on
how it's feeling/its state." And then: "the behavior should represent it's
genetics."

Branch `day-activities`, from `origin/integration` 3476b7d. It builds on
sleep-idle's idles (`docs/notes/sleep-idle.md`): the same chooser
(`idle.cpp pickIdle`), the same "an idle over a rest" rule, more acts and more
things that weigh them.

## The parts of his day

`DayPart` (idle.h), shown in `STATE` as `day=`:

| part | when (a phone's clock) | when (no clock: his own day) |
|---|---|---|
| waking | the first 10 minutes after any sleep of 20 minutes or more, or hatching | the same |
| morning | 05:00 to 10:00 | the first 2 hours after a night's sleep (3 hours or more), or hatching |
| midday | 10:00 to 14:00 | 2 to 5 hours after |
| afternoon | 14:00 to 18:00 | 5 to 9 hours after |
| evening | 18:00 to 22:00 | 9 hours after, or 3 hours after and sleepy (0.35) |
| night | the wall's night, lidded or dim (PetClock::dark) | lidded or dim |

The wall's hour is read back from the day loci (`hourOfDayLoci`), which carry
it only while the clock is trusted, so an unknown time never makes a day part
(as sleep-idle made sure it never makes a night). With no clock his day starts
when he wakes from a night's sleep: on a badge that is left alone at night
(dim after 45 minutes) and picked up in the morning, that is the kid's morning.
`IdleState::wokeAt` and `dayAt` hold when he woke (saved, hashed, only once set).

Each act leans to its parts (`idle.cpp kWhen`, tenths): the stretch, the wash
and the yawn on waking; exercises, a song, tidying, a wave and a peek in the
morning; the sun, romps and digging in the middle of the day; daydreams and
finds in the afternoon; fireflies, yawns, a lullaby and an elder's memories in
the evening; the stars and fireflies in the dark.

## New acts (idles.def 23 to 36)

| act | what | who and when |
|---|---|---|
| wash | a paw round his face, bubbles | waking, morning, evening; mothwick, hushwing, gilly |
| sunbathe | lies back under a turning sun | midday and afternoon, never in the dark; mossby basks |
| fireflies | wanders after blinking fireflies, swipes up | evening and the dark; mothwick, puffkin, grungo |
| exercise | squats, each counted ("1!" "2!"), a drop of sweat | mornings; mudge |
| yawn | up into a great yawn | waking, evening, or sleepy (0.35) |
| tummy | hunched, rubbing a rumbling tummy | hungry (0.5) |
| beg | up at the glass, his empty bowl held up | hungry (0.5), the more when lonely |
| nod_off | his head droops with a "z", jerks up with a "!" | sleepy (0.4) and awake |
| nurse | rocking, a plaster on his side, patting it | hurt: pain, discomfort or injury 0.25 |
| queasy | a slow wobble, a green swirl | poisoned: toxin 0.05 |
| mope | low, eyes down, a sigh, a little raincloud | lonely or bored past 0.55 |
| beam | a happy bounce, hearts floating up | content (no drive past its mark) |
| tumble | trips, over he goes, stars round his head | babies and children only |
| reminisce | a thought bubble of the egg he was | elders only |

Tummy, beg, nod_off, nurse and queasy are the `Feel` family: they show how he
feels and ease nothing. They play only while that feeling lasts (`feels()`),
and they are the only acts that play while something presses (not calm: hunger
0.55, pain 0.3, sleepiness 0.5 ...), in place of a rest or a wander the brain
picked; never in place of eating or sleeping, never when afraid. Before, a pet
that was hungry, sleepy or hurt only stood about.

## Age

`kAge` (tenths, by family): a baby romps 1.4x and muses 0.7x, an elder romps
0.5x (zoomies 0.3x) and muses 1.4x; tumble is a little one's, reminisce an
elder's.

## Genes

Owner: "the behavior should represent it's genetics". What his genes do to it
(the starter's values are the 1x):

- **size base** (`bulk`): a big one romps less and muses more ((512 -/+ (base -
  128)) / 512), plays at a slower pace, and breathes, sways and bobs slower
  (paint/lively.cpp); a small one the other way.
- **size squash**: how far his acts squash and stretch him, 0.5x to 1.5x.
  (Paint never read it before.)
- **temperament explore**: noses about more (it was already).
- **temperament habituation**: tires of a thing sooner: cooldowns 0.6x to 1.6x,
  and less of the family he just played (the starter's 0.43 keeps sleep-idle's
  halving).
- **temperament dreamLen**: a long dreamer muses more.
- And as before, his genome hash moves each favourite 0.75x to 1.25x, his
  species sets the favourites (`kAffinity`), and his chemistry genes set how
  his drives run, which now shows in the Feel acts.

## How he plays it: the pace

`idlePace` (percent, 55 to 140), fixed when an act begins (`IdleState::pace`,
`Appearance::idlePace`): baby 1.15x, child 1.1x, elder 0.8x; big slower, small
quicker; sleepy, hurt, poisoned or hungry slower; content a little brisker. A
slow act runs the longer (its length / pace), and paint runs its motion and
props on a clock at that pace, its sway and bounce bigger when brisk. Fixed
for the act, so no motion jumps mid-act (tween identity holds).

## Verified

`test_idle_day`:

- `Day.PartsByTheWallsHour`, `Day.NoClockHisOwnDayFromWhenHeWoke`.
- `Day.WhatHeDoesChangesThroughTheDay`: four species, a phone's clock, a kid
  looking every 4 to 25 minutes from 07:00 to 22:00; the morning's acts are
  more than 1.8x their afternoon share, the sun 3x more at midday than in the
  evening, fireflies only from evening, daydreams more in the afternoon,
  yawns more in the evening; 12+ different acts in every part.
  `DAY_MIX=1` prints each part's mix.
- `Feel.HowHeFeelsShows`: hungry, sleepy, hurt and poisoned each show (over
  20% of what he plays); content, never; content, he beams.
- `Feel.HowHeFeelsSetsThePace`, `Age.ABabyRompsAnElderMuses`.
- `Genes.EachMovesTheMixTheWayItsNameSays` (the chooser, per gene), and
  `Genes.TwoPetsOfOneKindDifferByTheirGenes`: two gillies, the same seed and
  desk, only size and temperament genes differ; the small curious one romps
  and noses more and plays briskly, the big dreamy one muses more and slowly.
- `Genes.HisBodysGenesShowInHisMotion`: the squash gene in an act's squash,
  the size gene in his breaths a minute.
- `Film.TheDaysActs` (`DAY_ACTS_OUT=<dir>`; `scripts/test/idle_strips.py`):
  each new act at six points, on a puffkin and a grungo.
