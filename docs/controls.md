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
| press | **knock knock**: the case tapped twice (the IMU's double tap) | `KnockDetector` |
| hold (a level: 1.2 s, and the 3 s holds) | **turn onto a side and hold it there**, on edge | `SideHoldDetector` |
| double press | **the hang**: upside down on edge, about a second | `HangDetector` |

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
  The screen turns with the badge, so the kid watches the ring fill.
- **Knock knock** is the tap engine's double tap, which the low-power rules
  already trust as deliberate (one tap is a bumped desk or a keyboard). It
  is masked like every knock after the road pushes the whole body up, so a
  car's potholes feed nobody (`test_senses` Road cases).
- **The hang** is unchanged.

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
- **The knock knock that wakes a dark badge only lights it**, as the press
  that wakes it does: nobody feeds him unseen.

## He teaches them

As with the hang and the shake, the pet says what nobody can guess: `KNOCK
KNOCK ON ME FOR SNACKS` whenever he is hungry (at any age: it is the only way
food reaches him), and `TIP ME SIDEWAYS. NIGHT NIGHT` on his first day.

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
