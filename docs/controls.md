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
| Clutch (tilt toward an egg) | keep the egg tilted toward | 1.2 s: keep it | |
| Egg / remains | peek / a flower | | |

Two changes of meaning came with it: a knock knock in a clutch used to keep
the egg and moved on instead (keeping is for good, so it was a hold), and in a
turn a knock knock throws a quick special when one fits (it still cheers
when one does not). Since 2026-10-21 the clutch is picked as a duel aims
(below, "Choosing an egg"): tilt toward an egg, then knock.

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
  a turn back lets go within two samples; the update's "Tip me on my side!"
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
  "Tip me on my side!" (or the debug unlock asks), a knock knock answers
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
| A pellet leaves the pantry | that cookie is tossed, its shadow waiting below, and lands in a puff; the pantry bar drains a step, a chunk flying off |
| It lies in the dish | as a cookie; one that would lie on, under or behind him, or by his feet, is set down beside him, clear of his body |
| He bites it | it is at his mouth (held up by his eyes when a species' mouth is tagged low, as the octopus's), a bite out of it each third of the chew; a burst and crumbs, YUM! and three hearts (rotten: grey-green, YUCK) |
| Any of these | his belly: a meter up the left rim, its level 1 - hunger, rising as he eats |
| He is full (hunger under 0.15) | he says I'M FULL! in a bubble. The cookie still lies there |
| He is asleep | ZZZ... in a bubble. The cookie waits for him |
| The pantry is empty | no words: a thought bubble beside his head plays emoji drawn in code: the food he wants pops in, a red NO slams over it, then a sad face (four variants in turn: a drumstick; the food then an empty plate; an empty jar; another food, with a tear); the pantry bar shakes hard and flashes red |
| Always | the pantry: a bar on the rim from ten to twelve o'clock, a cookie at its start, filled to the pellets left, green full through yellow and amber to red, the next pellet's return a lighter sliver past the fill |

The first version (2026-10-21, 94e22a3) flashed a gold ring round the whole
rim on the second knock ("a little much, and didn't help me understand I was
feeding it") and said a refusal in pictures: the owner, knocking until the
pantry was empty, read its grey cookie and hourglass as "two emojis, the
globe and something else". Both are gone: the knock brings a cookie, and a
refusal is words (checked against the font, `test_paint` `Font.*`). Then
the owner: "For that 'all gone', it needs to be an obvious icon and not
text. For the 6 year old." A jar drawn for it read as a blue box or a
battery, so (owner: "a mini animation with a couple obvious emojis in a
thought bubble") it is the emoji thought, and the pantry's pips became a
bar "that goes down upon feeding, and changes color green->red". The
emoji and the bubble are `paint/emoji.h` (`drawEmoji`, `drawThought`,
`drawThoughtFrames`), for the pet's inner state too (#19).

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

## Choosing an egg, as the kid sees it

Owner, 2026-10-21: "It also was difficult for me to understand how to pick an
egg." Then: "Use the same mechanism as we do in fights: tilt the badge. Make
the egg choices next to the selection egg ... reacting more quickly, with
more frames, making the delay between user gesture and action as low as
possible."

What it was (`bash scripts/test/clutch_film.sh`, `outputs/egg-ux/strip_*_before.png`):

- **The vigil** is 30 minutes of his remains fading to nothing, with a
  flower glyph at 1 o'clock (a knock knock lays a flower). Nothing says eggs
  are coming, or when; for the last minutes the dish is empty.
- **The eggs** then lie in a row, all one size. The one "selected" is marked
  by a 6 px cream caret under it, and nothing else (it did not even rock: the
  clutch's Appearance had no pose clock).
- **Next egg** was a knock knock, which nothing on screen said: the rim's
  glyph at 1 o'clock was an arrow, and its "one more" glow lit there, far
  from the eggs. The caret moved about 0.6 s after the second knock.
- **Keep** was a turn onto a side held 1.2 s (about 2.5 s with the turn's
  settle), and its ring filled round that same "next" arrow.
- **Kept**, the eggs vanished and one big egg was there: no moment, no
  sign which one it was.
- **How many** showed (all were drawn); **what differs** was only colour;
  **nothing done**: 30 minutes after the vigil the egg under the caret (the
  first, unless moved) is kept, silently, and a clutch of one is laid as the
  vigil ends.

What it is now (`blorb/clutch_show.h`, `lib/paint/src/clutch.cpp`; the engine
rule is `pickEgg` in `creature.h`, the shared aim is `blorb/tilt_aim.h`):

| When | What shows |
|---|---|
| The vigil | the eggs on their way, small under the remains, clearer as it ends |
| The eggs come out | they lie where a duel's stances do: left, right and (a third) toward the kid. Level, a little badge in the middle leans toward each egg in turn, its chevron to that egg lit, and TILT TO PICK; the last egg tilted toward (the one the timeout keeps) glows as the duel's lean does |
| A tilt reaches an egg | that sample (`AimDetector`, two samples into a lean), not the next tick: the egg grows toward the middle (an eased 140 ms slide, every frame drawn), rocks, the duel's reticle rings it, the others dim, the duel's knock taps beside it and the words say KNOCK TO KEEP. Until he has tilted from one egg to another the words take turns with TILT TO PICK, since a badge held upright already leans toward the near egg |
| The knuckle lands | as it lands (`BodySample::knockWaiting`, ~0.6 s before a lone knock is decided) the ring starts filling, orange, as the duel's hold ring does |
| The egg is kept | the ring closes lime (a locked stance's), sparks fly, the others fade and drop away, and the egg glides to the middle and becomes the egg screen's egg (2.4 s in all, `ClutchShow::kChosenMs`) |

The rules (`pickEgg`): the aim (`AimDetector`, the duel's) at each tick
makes the egg there the cursor; while tilted toward an egg, a knock keeps it
(the egg it was made at, as a knock locks a duel's stance), and so do a
knock knock, a BOOT press or a hold (a side hold keeps the egg tilted toward
before the turn, as a heavy special lands where the kid aimed). Level or tilted
away, nothing is kept: a knock knock out of habit feeds no egg into a choice.
The timeout still keeps the cursor after 30 minutes, never a friend egg. The
clutch's seeds, the eggs they hatch and the friend egg's rules are untouched.

**A badge left lying chooses nothing** (`tilt_aim.h` `AimArming`, shared by
the eggs and the duel). On a board (2026-10-21) a puffkin left lying tilted on
a desk with its eggs out kept one within 20 minutes, nobody near it: a stray
jolt read as the knock on an aim no hand had made. Now a choice screen (the
eggs, fight mode and the duel) opens unarmed, so a pose the badge already lay
in points at nothing. A hand arms it by moving the badge: gravity 250 mg away
from where it rested, on two steady samples running (a jolt is not steady), which
any tilt into a lean or turn onto a side is. It stays armed 20 s after the
last move. Unarmed, every reader of the aim sees level: the screen points at
nothing, no knock, knock knock, press or hold selects, a side hold of a badge
already on its side keeps nothing, and the timeouts keep only what an armed
aim chose (the clutch: the cursor egg after 30 minutes, as before).

Caveat for a try with a kid: as in a duel, a badge **held upright** in a hand
is tilted toward the kid (the near egg, a duel's Trick) as far as it can be,
so with three eggs the near one is aimed until the badge is laid back like a
tray. The words take turns until he has tilted between eggs, so the tilt is
taught before the knock.

The rim's hint for the clutch is now the check (a press keeps), with no hold
ring (the hold also keeps). Previews: `bash scripts/test/clutch_film.sh`
films a death to the next hatch, on this checkout and on the commit before
the picker driven the old way, into `outputs/egg-ux/`
(`strip_<pet>_before.png`, `strip_<pet>_after.png`).

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
