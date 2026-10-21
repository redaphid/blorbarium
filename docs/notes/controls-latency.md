# Controls latency: measured, then climbed (2026-10-21)

The owner: "We should also perftest and hillclimb control responsiveness."
The frame rate had just been fixed (main d4c0a44: ~49 fps at rest, ~36 in a
hang). This is every control, from the moment the hand does the thing that
counts to the glass, and what was done about each.

## The harness

**On the badge, `DEBUG latency`** (`src/latency.h`, hooks in `src/main.cpp`).
One event per gesture, timed in microseconds after its decisive raw read
(In), always on (a few KB, a few us a sample):

| stage | what |
|---|---|
| seen | the loop took that sample off the sense task's queue |
| detect | the detector acted: the knock counted, the side hold's level, the hang's charge, the aim off the dead zone |
| ack / ack_panel | the first frame drawn that acknowledges it before the engine has, and its rows out on the panel |
| word | the stimulus `controls.h` names fired (press, hold, hang, ...) |
| tick | the 100 ms engine tick that took it |
| frame / panel | the first frame after that tick that sent the panel rows, and its rows out (the core-0 send task done) |

`DEBUG latency` prints p50/p95/max of each stage per control over the last
16, `DEBUG latency last` the newest of each, `DEBUG latency clear` starts
over. `silent` counts gestures that were detected or spoke and showed
nothing at all; `noack` those with no acknowledgement before the engine's
answer. The decisive moment: a knock knock's second knock (where "one more"
goes out with no word), a knock's read, a pose's first steady sample past
45 degrees of the run that held, BOOT's down, the read a shake's last jolt
came in, the read whose tilt first points at an aim, the read that woke a
dark badge.

**On the cable, `scripts/test/latency_probe.py`** drives each control N
times through the raw IMU path (`DEBUG knock 1|2`, `side`, `hang`, `press`,
`double`, `lean`, and the new `DEBUG shake`), at rest, in fight mode (the
lobby a hang opens) and from low power (the wake), and prints PASS/FAIL per
control against its targets.

**Natively, `test/test_controls`** replays every recorded hand
(`test/fixtures/hands_2026-10-19.txt`) through the shell (`hw::Feel`) and
every detector: per segment, the words fired (the false fires) and the
acknowledgements shown, and in sample time how long from the decisive moment
to the acknowledgement and to the word. It fails if any count changes.

## Targets, and why

- **Acknowledgement on the glass within 50 ms** (p95, In to ack_panel) for a
  knock knock, a knock, a side hold, a hang and BOOT. A frame is 20 ms, a
  sample 20 ms, the send ~2-10 ms: 50 ms is one sample, one frame and the
  send, the fastest the pipeline can be without drawing faster. Revised for
  the knock knock: its acknowledgement can only come once the second knock
  has rung out (a shake from a still badge looks like knocks every 60 ms
  until then), ~40 ms after it at p50 on the recordings, so its target is
  the ring-out plus a frame: 100 ms.
- **The word's answer** (In to panel): the word waits on the gesture itself
  (a hold is held, a knock knock waits out a third knock); after the word
  it is at most one 100 ms tick, a frame and its send.
- **Never silent**: every gesture that is detected shows something.
- **No new false fire** on any recording.

## Baseline: db24534 on the puffkin (5C94094999, badge128_flip), `--reps 6`

ms after the decisive moment, p50/p95:

| control | seen | detect | ack | ack_panel | word | tick | frame | panel |
|---|---|---|---|---|---|---|---|---|
| knock knock | 13/20 | 114/127 | 146/159 | (bug) | 668/674 | 712/739 | 745/762 | (bug) |
| knock | 21/33 | 21/33 | 45/70 | (bug) | | | | |
| side hold | 3/8 | 852/941 | 932/1002 | (bug) | 2041/2153 | 2082/2173 | 2144/2401 | |
| hang | 17/22 | 794/801 | 839/890 | (bug) | 1678/1703 | 1680/1754 | 1720/1791 | |
| press (BOOT) | 8/14 | 8/14 | 50/187 | (bug) | 147/154 | 177/210 | 215/248 | |
| double | 15/35 | 15/35 | 54/74 | | 134/152 | 163/230 | 199/267 | |
| shake | 10/12 | 10/12 | - | - | 10/12 | 74/88 | 100/189 | 115/193 |
| tilt (rest) | 14/33 | 329/339 | - | - | 329/339 | 365/419 | 390/459 | 555/776 |
| tilt (fight) | 37/45 | 543/586 | - | - | 543/586 | 590/639 | 628/686 | |
| wake | 17/19 | 17/19 | 83/85 (light) | 83/85 | | | 60/61 | 83/85 |

(bug): the harness read a frame's send time only when it could take the
send task's semaphore, which the next frame nearly always held, so
ack_panel and panel came out ~1 s late. Fixed in 7a8b838 (each frame's time
in a ring the task writes); the numbers above that do not depend on it stand.

What it says:

- **The poses were the big lag**: the hold ring started 0.93 s after the turn
  and the hang's charge 0.84 s after it, all of it the panel rotation's
  settle (orient.h), which the gestures no longer need.
- **The knock knock's flash** came 146 ms after the second knock: the 80 ms
  quiet KnockSense::knockKnock waited for restarted on every bounce.
- **The knock knock's pellet** waits 668 ms for the word: the 600 ms window
  for a third knock.
- **Tilt** takes 330 ms to read an aim: the tilt detector's smoothing.
- The rest of the pipeline is small: seen 3-37 ms, tick +30-50 ms, frame +30.

## Attempts

Each: the change, latency before and after, false fires on the recordings.
"Recordings" numbers are sample time from `test_controls`.

### 1. Poses from gravity itself (d740670)

`hw::Poses` (`src/hw/poses.h`) replaces the settled rotation for the side
hold and the hang: a pose counts on its second steady sample past 45 degrees
(out under 600 mg; jolts skipped). Swept on the recordings: one sample
flickered the ring through handling (12 rings, not 5), three cost 20 ms more
for nothing.

So the words did not come 0.8 s sooner and buy false fires, the holds took
the lag's place: kSideHoldMs 2000 (was 1200 after the lag) and kHangHoldMs
1200 (was 900). Swept: side 2000 with a side hold no longer completing while
being let go keeps the owner's short turns (1.5-1.9 s) holding nothing; hang
900 fired a hang in 9_shake (2.1 s of handling upside down), 1000 had no
margin, 1200 does.

| recordings | before | after |
|---|---|---|
| side hold ring | 780/980 ms | 20/20 ms |
| side hold word | 2000/2180 | 1920/2040 |
| hang charge | (after the settle) | 21/120 |
| hang word | 1400/1720 | 1160/1280 |
| holds / hangs fired per segment | 4,4,0,0 / 2,1 | the same |

The ring now fills to kSideHoldMs for a side hold, and the hang's charge on
the rim is the detector's as of the last sample, not the last tick.
Visible side effect: a slow turn upside down passes 0.4-0.6 s on a side, and
the hold ring shows for that moment (honest: he was on his side); handling
showed 5 ring flashes in 30 s where it showed 3.

### 2. The knock knock heard as it rings out (4b256b3)

KnockSense::knockKnock (feeding's rim flash) waited for 80 ms with no spike
at all, and each bounce of a knock in a hand restarted the wait. Now: the
first two samples after the second knock with no spike and nothing
knock-sized off gravity (kNextDevMg), latched until the report. The sim's
shake from a still badge sits 1.5 g off gravity and never flashes.

| recordings | before | after |
|---|---|---|
| second knock to flash | 80/200 ms, max 580 | 40/140, max 180 |
| knock knocks, singles, false flashes | | the same |

### 3. The knock knock decided sooner after its second knock

(in progress)
