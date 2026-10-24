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
- **Traits** (`Phenotype::traits`, traits.h, from gene-variety #60: 0 is the
  recipe, +100 one doubling of a gene's rate): playfulness, more romps, more
  play at all and a brisker pace; affection, more showing off for the kid and
  begging; sleepiness, more musing and a slower pace; nightOwl, more of the
  evening's and the dark's acts (fireflies, the stars, a lullaby); cheer, more
  beaming; nerves, more hiding and looking about; boldness, more boos, boxing
  and zoomies. Curiosity is explore's, already in the Nose pull. (Under a kid's
  normal care boredom and loneliness sit near 0 all day, #60 measured, so
  those genes show here through the traits, not the drives.)
- **His own taste** (`IdleMood::taste`): each favourite 0.75x to 1.25x his
  species', seeded from his belly, eye and glow colours and his knack, which a
  child nearly always shares with his parent. It was the genome hash, which
  every egg changes, so no family shared its habits; two founders still differ.
- His species sets the favourites (`kAffinity`), and his chemistry genes set
  how his drives run, which now shows in the Feel acts.

## How he plays it: the pace

`idlePace` (percent, 55 to 140), fixed when an act begins (`IdleState::pace`,
`Appearance::idlePace`): baby 1.15x, child 1.1x, elder 0.8x; big slower, small
quicker; sleepy, hurt, poisoned or hungry slower; content a little brisker. A
slow act runs the longer (its length / pace), and paint runs its motion and
props on a clock at that pace, its sway and bounce bigger when brisk. Fixed
for the act, so no motion jumps mid-act (tween identity holds).

## Under the cooldowns: keen acts come round sooner

A pet awake and calm plays most of the time, and every act has a cooldown, so
weights alone wash out: whatever is off cooldown gets played. Each act's
cooldown is divided by how keen he is on it now (`keen`, 0.35x to 4x: its part
of the day, his age, genes and traits, his drives, and for the Feel acts how
strongly he feels it), and whatever he feels most damps the rest of his play
(down to 0.2x), so the day, a feeling and the genes show in what a kid sees.
A `kWhen` of 0 is a hard no: no sun in the morning or evening, no fireflies
before the evening, no exercises after midday.

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

A day's mix, `DAY_MIX=1` (four species, a phone's clock, 07:00 to 22:00):

    waking    yawn 16%  look_around 11%  stretch 9%  wash 8%  ...
    morning   look_around 8%  sing 6%  explore 6%  beam 6%  sniff 6%  wash 6%  discover 5%  exercise 5%
    midday    explore 9%  look_around 8%  sunbathe 7%  sniff 7%  discover 6%  dig 6%  beam 5%
    afternoon look_around 9%  discover 9%  explore 8%  sniff 7%  daydream 5%  sunbathe 4%  dig 4%
    evening   look_around 10%  yawn 10%  fireflies 6%  sniff 6%  explore 6%  discover 5%  sing 5%
    night     look_around 14%  stargaze 13%  yawn 6%  sniff 6%  fireflies 6%  explore 5%

Cost: badge128 RAM 217124 B (integration 3476b7d: 217044 B, +80 B: the 14
new acts' cooldown slots and the day's fields in IdleState), flash +4 KB. On
the host every new act draws at 0.7x to 1.3x the cost of him standing about,
inside sleep-idle's range (its daydream 1.8x): `DAY_ACTS_COST=1`.

Seen while testing: a pet left untouched in the dim goes to bed (sleep-idle)
and can sleep on for many hours while his hunger climbs to 1.0; no hunger
wakes him. Not changed here.

## Goldens and tests that moved

- `Replay.TwentyFourHoursOfTheSameRoutineGiveTheCommittedHash` (and the
  device's `HASH` reply): 6fce18b3 -> d349a3b4.
- `test_protocol` Golden.State (`day=waking`, what the hatchling does), Chem
  (sleepiness, boredom, a `recent_woke`), Clutch (the eggs follow the life
  before).
- `test_present` Golden.Hatchling: 30 s after hatching he is waking, and yawns.
- Sim frames (`tools/sim_film.sh`): 18 of 29 matched; the 7 known (#29:
  eating, shake_hop, hatch, no_phone, thought, prophecy, pellet_toss) left
  alone; re-blessed after looking, the four this change moved: idle_between,
  mothwick_between, walk_between, thought_between (a breath or step at
  another phase: the size gene's tempo and what he was doing).
- `Tween.EachTickPicksUpWhereTheFramesBeforeItLeftOff`: 80 ticks, not 40. A
  waking hatchling yawns and stretches, faces that move mid-act, and the test
  skips a tick whose face changed, so 40 ticks held too few; no jump found.
- `Thoughts.HalfADayWithoutAPhoneHeMissesItAndAVisitQuietsHim`: a knock every
  20 minutes beside the button. Untouched, the pet goes to bed in the dim
  (sleep-idle) and sleeps it out; whether he ever woke inside those 17 hours
  hung on the exact path of his day (the same scenario passed or failed as
  single factors of this change were switched off), and a sleeper misses
  nothing. The line itself is unchanged.
