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

### Board: 78f704f on the puffkin, a new pet (`--revive`), `--reps 6`

The old pet's lineage: `cause=injured age=118272`. Forensics: the
baseline run's presses and knock knocks dropped pellets while he slept, they
rotted (90 min), `DEBUG sleepiness 0` woke him for a fight, he ate them
(rot is not checked when eating: 0.4 toxin a rotten bite, which becomes
injury) and he died 20-30 min later. An old balance hole, not these builds;
the probe is now made never to do it again (below).

| control | seen | detect | ack | ack_panel | word | tick | frame | panel |
|---|---|---|---|---|---|---|---|---|
| knock knock | 18/23 | 74/82 | 99/110 | 103/124 | 493/510 | | | 600/629 |
| knock | | | | 63/96 | | | | |
| side hold | | 21/28 | 89/302 | 98/303 | | | | 2110/2178 |
| hang | | 25/32 | | 58/96 | | | | 1311/1352 |
| press | | | | 62/190 | | | | 192/287 |
| double | | | | 42/70 | | | | 252/273 |
| shake | | 3/21 | - | - | | 89/90 | 284/304 | 290/310 |
| tilt (rest) | | 161/161 | - | - | 161 | | 403/530 | 409/554 |
| wake | | | 77/82 (light) | | | | | 77/82 |

Fight mode was skipped: the rest phase's last double press had left the
lobby open, and the probe's hang to open it closed it. And each side hold
tucked him in, so a hang woke him into a lobby he dozed out of seconds later.

What it says:

- The side hold's ring at p95 303 ms: the ring is drawn in eighths, and a
  fill growing from zero over 2 s shows nothing for 250 ms.
- The knock knock's flash 103/124: the second knock's ring-out wait (two
  quiet samples) and then the frame.
- Shake: the tick takes it at 89 ms, but the first frame that changes is
  ~195 ms later. The trance's first ticks (the foresee face, intensity 1,
  `exprTicks` 0) draw the same pixels as before; the face changes two ticks
  on. That is the face's drawing, not the controls (open, for paint).
- Tilt at rest has nothing to draw (no aim outside a duel or, soon, the
  clutch): its "frame" is whatever next changed. The aim itself took 161 ms.

### 6. The aim from gravity itself, with hysteresis (be1869d)

The owner's priority (2026-10-21): tilt is the selection (the duel, and egg
picking next, which reuses the duel's aim and lock), so it should react as
fast as the IMU allows, without jitter or false re-aims: hysteresis, not
heavy smoothing. `AimDetector` (senses.h): the lean from gravity, halfway
to each sample; into a sector past 300 mg in the plane (DuelInput's dead
zone), out under 200 mg, to another sector only once it leads the held one
by 120 mg; a jolt is skipped. The tilt detector keeps rolling the food.

- The duel takes the aim as the tilt at the sector's middle, so DuelInput
  reads the same sector, and the side hold's "aim from before the turn"
  history keeps working.
- A knock locks the aim it was made at: the first knock's sample, not where
  the hand drifted in the ~0.6 s the knock takes to be decided.
- `Dish::duelView()` shows the aim as of the last sample: the HUD's
  highlight moves a frame after the hand instead of at the next tick (up to
  100 ms later). `Dish::aimNow()` is the same aim for egg picking.

| | before (tilt loci) | after |
|---|---|---|
| a 30 degree lean to the aim, samples | 15 (1/16), 7 (1/8) | 2 |
| board: lean to the aim read | 329/339 ms (1/16), 161 (1/8) | (next run) |
| the highlight after that | the next tick: +0-100 ms | the next frame |
| aim changes while a knock burst is under way, all recordings | 0 | 0 |
| aim changes in all, all recordings (handling turns it) | 109 | 148 |

A hand wobbling ±40 mg on the diagonal between two sectors flipped the old
aim with every wobble and holds the new one (`Aim.AHandOnASectorsEdge...`).

### 7. The knock knock heard on the first quiet sample; the ring's first eighth (bfae82d)

- `KnockSense::knockKnock` on the first sample after the second knock that
  is no spike and nothing knock-sized off gravity: on the recordings
  20/100 ms after the second knock (from 40/140). The sim's still-badge
  shake still never lights it. No count changes on any recording.
- The hold ring starts at its first eighth (`GestureCues::kRingFirst`) and
  fills to full as the hold fires, so it answers on the hold's first sample.

### The probe, made safe (045b48c)

Vitals (STATE and CHEM) at the start and the end, a loud FAIL if injury or
toxin rose or life fell; feeding stops at two uneaten pellets; `DEBUG tidy`
(new: every pellet taken, toxin and injury to 0) after every feeding section
and at the end; the wake made with a short turn, which feeds nobody;
sleepiness and the idle timeout put back; a death reported from the lineage
before `--revive` goes past it; the side holds last; fight mode entered only
if not already open, and why not when it cannot be.

## What was weighed and not done

- **Feeding the engine at sub-tick time.** A word waits for the next 100 ms
  tick: 30-60 ms on the board (word to tick). A tick could be run at once
  when a word arrives and the schedule kept by borrowing it from the next
  (the tick count never drifts from the clock). The engine stays a pure
  function of its tick inputs, so its replay is not broken in itself; but
  which samples share a tick would then depend on when words land, the
  smooth-motion tween interpolates by the time since the last tick (a
  borrowed tick runs that clock ahead and jerks him), and the native
  replays, the sim and its goldens tick on an exact 100 ms grid, so the
  badge would group its inputs unlike every replay of the same trace. It
  saves at most a tick, ~45 ms on average, on the engine's answer only;
  the acknowledgements already land before the tick. Not done.
- **Priorities.** The sense task (priority 2, either core) reads at 250 Hz
  and hands on a sample every 20 ms; the loop (1, core 1) takes the queue
  once a pass; the send task (1, core 0) sends rows under the radio's
  tasks. Seen is 1-33 ms: the handoff (the decisive read up to 16 ms before
  it) and the loop finishing the frame it is drawing. Raising a priority
  changes neither: the loop is the consumer and it is mid-draw. Handing a
  knock-sized read on at once would break the 50 Hz cadence every detector
  times itself by. Not done.
- **Low power's wake**: 77/82 ms from the waking read to the light, the
  first frame at ~50 ms. Already within its target.

### Board: 1ef6f1a on the puffkin, `--reps 6`

No harm: injury 0, life 620 before and after, tidied. p50/p95 ms:
knock knock ack_panel 102/111; hang ack_panel 79/90; side ack_panel 94/104;
knock 86/119; double, shake (panel 105/161), wake pass. Tilt: the aim read
100/207 at rest (from 161), 46/247 in fight mode, its panel 171/270 (from
409/554). Press ack_panel 196/228: a 120 ms BOOT tap where a hold does
nothing shows no ring (a tap is no hold), so its acknowledgement is the
flash on its release, which the harness timed from the press.

### 8. The aim's tail: two samples' mean, held through a knock

The aim's p95 came from where the badge started: lying on the desk it leans
~340 mg one way, just past the dead zone, and a smoothing that halves the
distance each sample crawls the last of the way to a threshold (4-5
samples to cross 300 mg toward 340). The aim now reads the mean of the last
two steady samples, so a lean is all there on its second sample. That lets a
knock's ringing in the hand through at half weight (4 re-aims in L4), so the
aim holds while a knock burst is under way (`BodySample::knocking`): a knock
is what locks the aim, and it never moves it. Recordings: 0 re-aims while
knocked; 171 aim changes in all (148 with the halving, 109 the old tilt),
all in handling, where the badge really turns.

The harness now times a BOOT tap that showed nothing while down from its
release (`Recorder::rebase`).

### Board: 8029a0d, `--reps 6`: the aim held 3.5 s in fight mode

At rest the aim was read 36/68 ms after the lean (from 100/207). In fight
mode, 3501/3648: the aim held until the next knock knock's burst ended.
Holding the aim for a knock burst's whole window (`KnockSense::busy`, up to
0.6 s after its last knock, and on the board longer: the burst's table side
can stay busy) froze the aim long past the knock.

### 9. The aim holds only while a knock rings

`KnockSense::ringing()`: a burst in a hand, within kMergeMs (170 ms) of its
last spike or bounce. The aim holds only then (`BodySample::knockRinging`).
What a knock locks is the aim on its first sample (the Dish's knockAim), and
the screen shows that aim until the burst is decided, so the hand's sway
between the knocks moves neither. Recordings: no first knock moved the aim
it locks (`Aim.OnTheRecordingsAKnockLocksTheAimTheHandHeld`, every first
knock on every recording); a lean right after a single knock or a knock knock
is the aim within 60 ms (`Aim.ATiltRightAfterAKnockIsTheAimAtOnce`; with the
burst's window it was 390 and 230 ms). `DEBUG trace` lines now end with the
aim, busy and ring; `latency_probe.py --trace-fight` traces the first fight
rep, should busy stick again.

### 10. The cable never stalls the loop

The cable's UART had only its 128-byte FIFO: a longer line held the loop
until it went out at 115200 baud (the 10 s status line ~20 ms, a protocol
reply more), and a gesture's samples and its frame waited with it (seen p95
51-53 ms while the probe asked STATE). Serial now queues 2 KB.

### Board: 5cf3409: fight fixed, the rest's tail back

Fight mode: the aim 28/72 ms, panel 121/213. At rest the aim's p95 came
back, 45/249: the tilt events that trailed were the badge's returns from
the cable's injected poses (a side hold, the hang, the wake's turn): each is
a one-sample step, which KnockSense takes for a knock and rings, and the aim
held through the ring, 200-225 ms (the side and low phases' tilts, all of
them).

### 11. The aim holds through a ring only while the badge sways (Refs #25)

While a knock rings the aim now holds only on samples that sway (one
steady sample to the next past kSwayMg, 60 mg in the plane): a knock's
ringing in a hand sways, a badge set down or turned with a thump settles at
once and aims where it settled. `Aim.AThumpOntoAnAimIsTheAimAtOnce`: 180 ms
with the ring hold, 20 ms with this. No first knock on any recording moves
the aim it locks.

### 12. A hard shake dismisses what is modal, never a duel (Refs #35)

The owner: "shaking should dismiss any 'modal' stuff (but not back out of a
fight), including 'fight mode'." 0.7.11's "a shake leaves fight mode"
(41a4e10) was only ever on a cloud branch (origin/ccr-82507c31-iksr3h) and
never reached main: nothing regressed, it was never there.

Only a hard shake dismisses: `MotionDetector::vigorous()`, 15 of the last 50
samples 0.8 g or more off gravity. Of the recordings' three Shakes, the
pick-up from face down in L2 has 9 such samples and dismisses nothing; the
shake (9) has 28 and the vigorous carrying in 8 has 31 (that one would: it
is shake-strength). A Shake still tells the future as before.

What it dismisses:
- fight mode before a duel has started: the lobby, a pair greeting, the
  stakes picker (the friend hears it as leaving and goes back to searching);
- an ask for fight mode waiting on an update, and why no fight;
- a refusal's words when feeding (ALL GONE!, ZZZ..., I'M FULL!);
- the update's "Turn me around!" (a no: it lapses as unanswered, the page
  sees `last=no_confirm`) and the debug link's unlock question.
Never: once the stakes are agreed (choosing, acting, mercy) a shake is the
prophecy and is no stance. A thought bubble is already replaced by the
shake's trance. Improv's "Join Wi-Fi?" is on wifi-ota-2: not done here.

Acknowledgement: a white ring sweeps from the middle to the rim and fades
in 300 ms (`paint::drawDismissed`, `Appearance::dismissedMs`), on the first
frame after the tick that dismissed it (the shell's questions: the first
frame after the sample).

Tests: `Shake.*` in test_two_dishes (the lobby, a soft shake leaves nothing,
the stakes with the friend hearing it, never once the duel has started),
`OtaSession.ADeclineEndsTheQuestionAsUnanswered`, and test_controls counts
the dismissals on every recording (0 outside 8 and 9). The probe's fight
phase now ends with a hard shake and fails if fight mode stays open.

### Fight mode "gate=wifi result=waiting" on the board (e3bf472)

Not NVS: the ESP-NOW air could not open. Opening it takes ~38 KB of internal
heap (46 KB -> 8.5 KB free); after two opens and closes the Wi-Fi driver's
deinit timed out ("wifi:timeout when WiFi un-init", "Failed to deinit Wi-Fi
driver"), and the next `esp_wifi_init` failed (257, no memory): `air open
FAILED at wifi`, so fight mode waited, retrying each second, and held the
badge awake ("low: refused (held awake)"). Free internal heap at boot was
57.7 KB on e3bf472 where it was 75.4 KB on 78f704f. That is #26's (the
internal heap with the air open); this branch's part is the cable's 2 KB
transmit queue (10).
