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

- **Turn and hold** is the hang's sibling: the panel's settled rotation
  (`orient.h`: a quarter turn held 0.6 s, jolts and flat rejected), on edge
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
  four knocks within 0.6 s are a knock knock; one is a knock. While a burst
  is under way the shake detector ignores the knock's bounce. Every
  recording is `test/fixtures/hands_2026-10-19.txt`, checked by
  `test/test_knocks` (2026-10-19: 17/17 held, 9/9 on a table, 0 false).
- **The hang** is unchanged, except as below.
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

- **The rotation lags a turn back** by about 1.5 s (`orient.h`'s settle and
  smoothing). Through that lag the sample in the screen's frame already
  shows gravity off the screen's down, so a pose counts only while gravity
  is within about 45 degrees of it (`kPoseDownMg`). Found on badge 1: a 1 s
  turn held at 2.4 s, after the kid had let go. The hang had the same lag
  and now has the same rule.
- **While turned onto a side, the duel's aim is the aim from before the
  turn** (the tilt of 1.5 s before), since turning him tips the tilt: a
  heavy special lands where the kid aimed.
- **Nobody feeds him unseen.** The press that wakes a dark badge only lights
  it, and so does a knock knock decided on the sample that woke it. In a
  hand the first knock's jolt usually wakes the badge, and the knock knock,
  decided 0.6 s later, then feeds him on a lit screen
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
| The second knock lands | a warm ring sweeps in from the rim as it rings out (`KnockSense::knockKnock`, ~0.1 s), before the knock knock is even decided |
| A pellet leaves the pantry | a cookie, big, is tossed from the rim's pellet glyph, its shadow waiting below, and lands in a puff; the pantry's pip goes |
| It lies in the dish | as a cookie; one that would lie on, under or behind him, or by his feet, is set down beside him, clear of his body |
| He bites it | it is at his mouth (held up by his eyes when a species' mouth is tagged low, as the octopus's), a bite out of it each third of the chew; a burst and crumbs, YUM! and three hearts (rotten: grey-green, YUCK) |
| Any of these | his belly: a meter up the left rim, its level 1 - hunger, rising as he eats |
| He is full (hunger under 0.15) | a bubble beside his head: the cookie struck through. It still lies there |
| He is asleep | a bubble: the moon, zz. The cookie waits for him |
| The pantry is empty | a bubble: a grey cookie and the hourglass; the pantry's empty slots shake at the top, the next one filling like a clock |

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

Not changed, for the owner to decide: the pantry (4, one back every 2 h);
that a pellet drops for a full or sleeping pet (it rots in 90 min, and a
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
