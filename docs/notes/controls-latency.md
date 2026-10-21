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

### 3. The knock knock decided in the kid's rhythm (1599276)

The burst waited kChainMs (600 ms) after its last knock for one more, before
saying knock knock. The first knock must still wait that long for its second
(the recordings' slowest pair is 0.46 s apart), but once two are in, a third
comes in their rhythm: the burst is now over `KnockSense::moreMs(gap)` after
its last knock, the first two's gap plus 150 ms, 400 to 600 ms. Swept with a
fixed window first: 500, 450, 400 and 350 ms changed no count on any
recording; 320 split a triple. 400 is the floor, with margin; a slow knock
knock waits as long as before.

| recordings | before | after |
|---|---|---|
| second knock to the word | 620/1020 ms, max 1120 | 460/660, max 980 |
| (a fixed 400 ms window, for scale) | | 420/560, max 820 |
| knock knocks, singles, false fires | | the same |

### Board: 1599276 on the puffkin, `--reps 6`

| control | seen | detect | ack | ack_panel | word | tick | frame | panel |
|---|---|---|---|---|---|---|---|---|
| knock knock | 14/18 | 67/77 | 105/109 | 129/133 | 496/512 | 555/581 | 588/614 | 608/634 |
| knock | 17/33 | 17/33 | 38/54 | 42/58 | | | | |
| side hold | 1/8 | 24/29 | 346/355 | 349/358 | 2021/2021 | 2069/2109 | 2623/2628 | 2628/2632 |
| hang | 6/17 | 27/38 | - | - | 1215/1217 | 1260/1295 | 1772/2028 | 1796/2032 |
| press | 14/18 | 14/18 | 168/178 | 192/202 | 145/157 | 197/215 | 235/1043 | 258/1067 |
| double | 27/33 | 27/33 | 63/68 | 85/91 | 141/155 | 174/192 | 208/226 | 230/248 |
| shake | 1/6 | 1/6 | - | - | 1/6 | 89/89 | 608/1729 | 620/1753 |
| tilt | 6/17 | 201/339 | - | - | 201/339 | 249/411 | 644/1280 | 648/1282 |
| wake | 12/17 | 12/17 | 71/76 (light) | 71/76 | | | 48/53 | 71/76 |

The knock knock's pellet reaches the glass 0.6 s after the second knock
(was ~1.0 s with the harness bug's panel, 0.76 s by its frame), its flash
0.13 s after it (from 0.16 s, the 80 ms wait now 40 ms plus a frame); the
side hold's and the hang's detectors act 24-27 ms after the turn (from
~800-850 ms).

But **the pet had died between the two runs** (the board ran ~33 minutes
between them on other work; the log's first STATUS reads `occupant=clutch`,
boot context 12, his remains): every control meant what it means at a vigil.
A side hold does nothing there, so its ring was the grey one, drawn only
after kVoidRingAfterMs (300 ms, made so a BOOT tap shows no ring): ack 346.
The hang's charge was drawn only for a creature: no ack at all. A shake
foretells nothing over remains, a tilt rolls no food: their "frame" is
whatever next changed on the screen (silent 2 and 3). And since main's frame
memo (4d18aeb) a frame that changes nothing is not drawn, so "frame" is now
the first frame that showed anything after the tick, not the next frame:
honest, but it also means a response that draws nothing waits for the next
unrelated change.

### 4. Every pose shows at once, for anyone in the dish

- A side hold's ring shows on its first sample: grey at once where a hold
  does nothing (only a BOOT tap needs kVoidRingAfterMs to be told from a
  hold), and a sliver of the real ring on the hold's first sample rather
  than the second (`GestureCues::dress`).
- The hang's charge is drawn for an egg, a clutch and remains too: they
  refuse the fight in words, but the hand that asked sees it felt first.
- `latency_probe.py` stops when the dish holds no living pet, and
  `--revive` warps past the vigil (`DEBUG warp <min>`, new), keeps an egg
  with a side hold, hatches it and grows it to an adult.

### 5. Tilt: the smoothing halved (9652db7)

The tilt detector's smoothing, 1/16 a 50 Hz sample (~320 ms to carry a 30
degree lean across the aim's dead zone), is 1/8 (~140 ms). It still skips
any sample a quarter g off 1 g (a knock, a jolt), and the 0.15 dead zone is
unchanged. All 946 native cases pass unchanged, so no golden moved. Board
before: 329/339 ms to the aim (and 543/586 in fight mode, where each lean
started from the last).
