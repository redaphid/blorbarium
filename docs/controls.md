# Controls with no button

The kids' cases cover BOOT, and a 6-year-old cannot be asked to find it.
Until now that meant a badge in a case could open fight mode (the hang) and
answer an update (the turn) and nothing else: **no food reached the pet**,
nobody could tuck him in, keep an egg, pick Mortal, throw a special,
Finish, or ask for a rematch.

So every word the button says now has a **twin made with the body**. A twin
fires the button's own stimulus, so each screen's row in `boot.h` (`kBoot`)
means the same thing whichever way it was said, and the rim's hint glyph and
hold ring serve both. The table and the switches are in
`lib/blorb/include/blorb/controls.h`.

| BOOT says | Its twin | Detector |
|---|---|---|
| press | **knock knock**: the case tapped two to four times, in a hand or lying on a table | `KnockSense` (shell) then `KnockDetector` |
| hold (a level: 1.2 s, and the 3 s holds) | **turn onto a side and hold it there**, on edge | `SideHoldDetector` |
| double press | **the hang**: upside down on edge (USB-C down), about a second | `HangDetector` |

**Which way is up.** In the kids' cases the USB-C port faces up, so that is
home (`ORIENT_HOME` = 2 in `src/hw/board_lcd128.h`): the hang is USB-C
**down**, the side hold USB-C to either side. (Until 2026-10-19 the firmware
took the opposite for home, so the hang meant holding the case the right way
up.) Badges whose cases hold them USB-C down build env `badge128_flip`
(`BLORB_ORIENT_HOME=0`), and their hang is USB-C up. The screen no longer
turns with the badge: it stays at home, so in the hang the kid sees him on
his head.

The rest is unchanged: tilt aims, one knock locks the aimed stance and
spares, a shake tells the future, a cuddle hugs, a turn answers an update.

## What that means on each screen

| Screen | knock knock (press) | turn and hold (hold) | hang (double) |
|---|---|---|---|
| Awake, asleep | a pellet | 1.2 s: tuck in | fight mode |
| Fight mode (lobby) | a pellet | | leave |
| Stakes | the next tier | 3 s: lock the pick (Mortal only this way) | |
| A turn | a quick special at the aim (else a cheer) | 1.2 s: a heavy special at the aim | |
| Mercy, winner | spare | 3 s: Finish | |
| Mercy, downed | plead | | |
| Friends | ask for a rematch | 3 s: hug (a friend egg) | back to searching |
| Clutch | the next egg | 1.2 s: keep this egg | |
| Egg / remains | peek / a flower | | |

Two changes of meaning came with it: a knock knock in a clutch used to keep
the egg and now moves on (keeping is for good, so it is a hold), and in a
turn a knock knock throws a quick special when one fits (it still cheers
when one does not).

## Why these three

Reliability first, as the owner asked: the hang that opens fight mode was
already the model, a **pose held on edge** whose progress a kid can see.

- **Turn and hold** is the hang's sibling: a quarter turn from home, judged
  from gravity itself (`src/hw/poses.h`: two steady samples past 45
  degrees, jolts and flat rejected), held 2 s (`kSideHoldMs`), on edge
  (`|az| <= 500 mg`, so a badge lying slanted on a table holds nothing),
  only after he was the right way up (a badge switched on sideways holds
  nothing), one hold per turn, and **a release is never a press**, so a
  turn let go early does nothing at all. While it is held it is a level, as
  BOOT down is: the duel times its 3 s holds by it and the rim's ring fills.
  The ring is on the rim, so the kid watches it fill from any side.
- **Knock knock** comes from the samples themselves (`blorb/knocks.h`,
  `KnockSense`), not the QMI8658's tap engine: with real hands the tap
  engine reported 0 of 5 deliberate knock knocks and about 34 double taps
  from handling (each a pellet). The IMU is read at 250 Hz while awake and
  each 50 Hz sample carries the farthest of its reads as a peak. In a hand a
  knock is a jump of about 1 g off gravity in one step, after the badge was
  steady, leaving it pointing the same way; on a table it is a 150-500 mg
  blip of under 100 ms with quiet around it (a desk rattles longer). Two to
  four knocks within 0.6 s are a knock knock (once two are in, a third is
  waited for 0.4-0.6 s, in the rhythm of the first two:
  `KnockSense::moreMs`); one is a knock. While a burst
  is under way the shake detector ignores the knock's bounce. Every
  recording is `test/fixtures/hands_2026-10-19.txt`, checked by
  `test/test_knocks` (2026-10-19: 17/17 held, 9/9 on a table, 0 false).
- **The hang** is unchanged, except as below: upside down from home, judged
  from gravity like the side hold, steady and on edge for 1.2 s in all
  (`kHangHoldMs`).
- **Each shows from the first frame** (2026-10-21,
  `docs/notes/controls-latency.md`): the hold ring and the hang's charge
  start ~20 ms after the turn (they waited ~0.85 s for the panel rotation's
  settle), and the rim flashes ~40 ms after the second knock of a knock
  knock. The holds took the settle's place, so every recording fires
  exactly as it did.
- **One fire per pose** (`PoseRearm`): after a side hold or a hang fires,
  the same gesture waits until the badge has been the right way up and
  steady for 0.4 s, so a badge laid on its side, or leaning upside down,
  fires once however long it lies there (owner, 2026-10-19).
- **Face down is bedtime**: about 30 s face down and still puts him to sleep
  whatever his genes say, and keeps him asleep while he lies there; the
  badge goes to low power 2 minutes after the last input instead of 10.
- **The IMU's z axis** points into the screen on the 1.28; `imu_qmi8658.h`
  negates it (before 0.7.12 face up on a desk counted as lidded).

Wii-style swings were considered and left out: a swing is one or two jolts,
which is also a pothole, a bump on a table or a badge put down hard, and a
shake (five jolts) already owns the big motion. The QMI8658's gyro would
make a twist reliable, but it is off to save the battery; turning it on
while awake is the way to add one later.

### Details that came from the hardware

- **The rotation lagged a turn back** by about 1.5 s (`orient.h`'s settle
  and smoothing): found on badge 1, a 1 s turn held at 2.4 s, after the kid
  had let go. The poses no longer read the rotation (`src/hw/poses.h`), so
  a turn back lets go within two samples; the update's "Turn me around!"
  still reads it.
- **While turned onto a side, the duel's aim is the aim from before the
  turn** (the tilt of 1.5 s before), since turning him tips the tilt: a
  heavy special lands where the kid aimed.
- **Nobody feeds him unseen.** The press that wakes a dark badge only lights
  it, and so does a knock knock decided on the sample that woke it. In a
  hand the first knock's jolt usually wakes the badge, and the knock knock,
  decided 0.4-0.6 s later, then feeds him on a lit screen
  (`test_feeding` `KnockKnock.ADarkBadgeIsLitAndNeverFedInTheDark`).
- **A question owns the knock knock as it owns BOOT**: while the update asks
  "Turn me around!" (or the debug unlock asks), a knock knock answers
  nothing and feeds nobody (`hw/feel.h`). Until 2026-10-21 it dropped a
  pellet behind the update's screen, where it rotted unseen.

## Feeding, as the kid sees it

Owner, 2026-10-21: "The 'feeding' mechanism on double knock either doesn't
work, or its effects are not obvious." It worked (`test/test_feeding` plays
real knocks, the recordings included, through the shell and the Dish at
either home and any frame rate), but almost nothing showed it: a 15 px
pellet appeared somewhere, 0.6 s after the second knock, and he ate it only
if he wanted it. Four pellets in the pantry and one back every two hours
(the metabolism gene) meant a kid who knocked five times was then fed
nothing, with only a greyed glyph to say so. Now (`blorb/feed_show.h`,
`lib/paint/src/feeding.cpp`, presentation only: the engine is unchanged):

People who then played with the badges said the pellet "looks like
pooping" and they could not tell if he ate: the pack's small dark pellet
appeared on the floor by his feet. So the food is now a fat cookie drawn in
code (golden, chocolate chips, a dark outline, a highlight), never by his
feet or behind him, and it goes into his mouth:

| When | What shows |
|---|---|
| The second knock lands | as it rings out (`KnockSense::knockKnock`, ~0.1 s), before the knock knock is even decided: a small ring round the rim's pellet glyph, and a cookie springs out of it and waits there (none if the pantry is empty) |
| A pellet leaves the pantry | that cookie is tossed, its shadow waiting below, and lands in a puff; the pantry's pip goes |
| It lies in the dish | as a cookie; one that would lie on, under or behind him, or by his feet, is set down beside him, clear of his body |
| He bites it | it is at his mouth (held up by his eyes when a species' mouth is tagged low, as the octopus's), a bite out of it each third of the chew; a burst and crumbs, YUM! and three hearts (rotten: grey-green, YUCK) |
| Any of these | his belly: a meter up the left rim, its level 1 - hunger, rising as he eats |
| He is full (hunger under 0.15) | he says I'M FULL! in a bubble. The cookie still lies there |
| He is asleep | ZZZ... in a bubble. The cookie waits for him |
| The pantry is empty | ALL GONE! in a bubble; the pantry's empty slots shake at the top, the next one filling like a clock |

The first version (2026-10-21, 94e22a3) flashed a gold ring round the whole
rim on the second knock ("a little much, and didn't help me understand I was
feeding it") and said a refusal in pictures: the owner, knocking until the
pantry was empty, read its grey cookie and hourglass as "two emojis, the
globe and something else". Both are gone: the knock brings a cookie, and a
refusal is words (checked against the font, `test_paint` `Font.*`).

**The food art's seam** is `paint::drawFood` (`lib/paint/src/feeding.cpp`,
declared in `lib/paint/src/figure.h`): every cookie on screen goes through
it, so generated art can replace its body (a sprite at the centre and
radius it is given) without touching the feeding. The packs' `item()`
pellet frames are no longer drawn.

Previews: `bash scripts/test/feeding_film.sh` films a feeding (grungo,
puffkin, a mudge hatchling, and the three refusals) on this checkout and on
the commit before it, into `outputs/feeding/` (`sheet_<film>.png`: before
above after). The sim's `!knock` and `!dtap` now knock the raw samples, as
a knuckle does (they had set the tap engine's register, which `KnockSense`
overrules, so no sim knock had reached the pet since 0.7.12); goldens
`knock_knock` and `pellet_toss` film it.

### More food, and when it comes (#57)

Owner, 2026-10-22: "I keep running out of food and I don't know how to get
more but it keeps complaining about being starving." Measured
(`test/test_food_balance`, `FOOD_SWEEP=1`): a kid who fed him only when he
asked never ran dry, but one who also knocked for pellets for the fun of it
found the pantry empty on 9 presses in 10, for two hours at a time. Every
number is now in `lib/blorb/include/blorb/food.h`:

| | Before | Now |
|---|---|---|
| Pantry | 4 | 8 (the metabolism gene's byte x2, at most 12) |
| Refill | one every 2 h | one every 5 min 20 s (the byte x 40 s): empty to full in 43 min |
| Foraging | none | a dig that ends in its 3-hour window's 150 s slot turns up a snack at his feet, sparkling: one a window at most |
| Coming home | none | the first hand after 2 h untouched, or after the board was off, tops the pantry up by 2 |
| A fight won | nothing | a pellet from the loser's pantry to the winner's (both badges agree) |

| When | What shows |
|---|---|
| Always | the pantry's slots along the top-left rim, from the pellet glyph: a cookie in each full one, the next one filling like a clock, the rest hollow |
| A press with none left | MORE IN 2:05 in a bubble (ALL GONE! when nothing is coming), the slots ringed red and shaking |
| He is hungry and none are left | the next slot breathes, and a dark pill under it counts down (2:05) |
| He digs up a snack | a ring and twinkles round it where it lands |
| Coming home | the slots it filled pop in |
| A fight won or lost | once the dish is back on screen, a cookie flies in over the rim to the winner's pantry, and out of the loser's |

The viability dry run (`mutate.cpp`) still feeds at the old two-hour pace
on purpose, so mutations it passed before pass now. Starving is still the
chemistry's.

Measured, every species, two seeds, two days (`FOOD_SWEEP=1 FOOD_SEEDS=2
FOOD_DAYS=2`; the kid is there 12 hours a day and glances every 2 to 6
minutes):

| | Before | After |
|---|---|---|
| Feeds when he asks (attentive or eager) | never dry; starving 15-20 h of 48, overnight | never dry; starving 10-20 h of 48, overnight (the chemistry: a 12 h night unfed) |
| Also knocks for fun, half his glances | 152-170 of 170-188 presses find it empty; starving 9-10 h | 0 empty presses; starving 3-6 h |
| Snacks he dug up | none | 0-11 in two days |
| Neglect: fed 12 h, then nobody | starving 1.4-1.8 h after the last hand, dead at 57 h | starving 1.5-26 h after it, dead at 57-82 h |

Open, for the owner: a pet fed whenever he asks still starves overnight
(energy halves every 4 hours, so a 12 hour night unfed pins hunger by
morning and adds injury about 0.2 a night, healed by day). That is the
starter chemistry, not the pantry; slowing his burn asleep would fix it.

Not changed, for the owner to decide: that a pellet drops for a full or sleeping pet (it rots in 90 min, and a
starving pet that eats it later is poisoned); where the engine drops it (a
random spot within 0.6 of the middle, then it rolls downhill, to the bottom
rim when the badge is held upright); and his mouth opening toward it (pose
art).

## He teaches them

The pet says what nobody can guess: `KNOCK KNOCK ON ME FOR SNACKS`
whenever he is hungry (at any age: it is the only way food reaches him),
and on his first day `TO FIGHT, HOLD ME UPSIDE DOWN`, `BEDTIME? HOLD ME
SIDEWAYS.` and `SHAKE ME. I SEE THINGS.` Each species says these in its own
words (`personalities/<species>.txt`, `teach_*`), always as advice, goal
first, so nobody reads them as "I am upside down". A lone knock gets a nudge
toward the second (`WHO'S THERE? TRY KNOCK KNOCK!`). The fight mode line
says `HOLD ME UPSIDE DOWN TO STOP`, and a hungry pet refused a fight says
`TOO HUNGRY - KNOCK KNOCK ON ME TO FEED`.

He says he is upside down (`sky_down`, `WHY IS THE SKY DOWN THERE`) only
when he is: held in the hang's pose from home, either home
(`HangDetector`'s `hanging` full). Until 2026-10-20 that line read
`TiltDetector`'s `upside_down`, which is face down and stays set until the
screen faces well up again, so a badge laid face down a moment and then
held on edge or worn on a lanyard said it every five minutes the right way
up; before 0.7.12 the IMU's inverted z made a badge lying face up on a desk
say it too (`test_thoughts`
`Thoughts.TheSkyIsDownOnlyWhenHeIsUpsideDownFromHome`).

## Testing

- `test/test_senses` (`Twins.*`, `Hang.TheRotationsLag...`): the detectors.
- `test/test_two_dishes` (`NoButton.*`, `BootRows.NoButton*`): two whole
  Dishes, kids with no button at all: a Mortal duel stepped to Mortal,
  agreed, fought with knocks and specials and Finished; one spared; a hug's
  friend egg; the aim kept through a side hold; feeding, tuck-in, the lobby
  and the clutch.
- On a badge: `uv run tools/gesture_e2e.py /dev/ttyACM0` drives the real IMU
  path over the cable (`DEBUG knock [1|2]`, `DEBUG side [ms] [l|r]`,
  `DEBUG hang`) and over Bluetooth (`KNOCK`, `SIDE`), and reads the result
  from the log and `STATE`. A badge holding an egg is hatched first
  (`DEBUG hatch`).

To turn a twin off, set `kKnockKnockPresses` or `kSideHoldHolds` to false.
