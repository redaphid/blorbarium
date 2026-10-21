# The perf climb: drawing a frame faster, byte for byte

The owner: "go above and beyond to hillclimb perftest". Branch `perf-climb`,
from `framerate`. Every step draws the same bytes: `REF=main bash
scripts/test/paint_bench.sh` hashes 4,310 home frames and 54 duel frames
(2aac61f2caef5ae9), and the S3 build of the bench checks its own frames
against the host's hash of them.

## The sticks

- **Host**, `bash scripts/test/paint_bench_runs.sh 7 3 2`: µs a home frame,
  drawing only (the hash is no longer timed), the median of 7 runs pinned to
  one core, each the best of 3. Noise band about ±1 µs.
- **S3**, `QEMU=... bash scripts/test/paint_bench_qemu.sh`: the board's
  compiler and flags, every 8th home frame (539) and the 54 duel frames, on
  Espressif's QEMU with `-icount shift=0`: thousands of instructions a frame
  (kinstr), by phase. Deterministic: a change of one instruction shows.
- **Board**, `DEBUG fps` and `DEBUG fps bench` through
  `scripts/test/fps_probe.py`: the truth, when a badge is free.

The first board run (`framerate` 6044b45, then d76c1bf with phases) drew a
resting frame in 25.9 ms: bg 13.4, fig 11.0, mask 0.9, hint 0.6, look and over
1.8. QEMU counts 6.4M instructions for the average frame, and the board's
6.2M cycles for its frame put it near one instruction a cycle: no PSRAM (the
canvas is internal, a 115 KB memset is 168 µs), no stall to speak of. The
board is slow because it does the work, so the S3 instruction count is the
stick, and a frame on the board takes about kinstr / 240 ms.

## The climb

| # | idea | host µs/frame | S3 kinstr/frame | board reasoning | kept |
|---|------|---------------|-----------------|-----------------|------|
| 0 | baseline (`framerate` + review fixes, -Os) | 519.9 | 6,774 (bg 2,634, fig 3,036, mask 151) | 25.9 ms on the board | - |
| 1 | the dish's row spans at compile time (`kDish`), for the background, the masks and the amber | 519.9 → 520.2 | 6,774 → 6,422 (bg -150, mask -151) | `dishFrom` walked up to 120 `inDish` tests a row, twice a frame; the duel tested every pixel | kept |
| 2 | lib/paint at -O2 on the board (`lib/paint/library.json`: the framework builds everything -Os) | n/a (host is -O2) | 6,422 → 5,439 (bg 2,484 → 2,287, fig 2,993 → 2,370; duel 11,968 → 10,736) | inlining (blend, to565, floorDiv, the outline's lambdas) and loop unrolling; +14 KB flash, no RAM | kept |
| 2b | -O3 instead | n/a | 5,439 → 5,312 (duel 10,736 → 9,286) | another 17 KB of flash and a bigger I-cache footprint (16 KB) for 2%; tried again once the loops are hand-tuned | deferred |
| 3 | background inside the rim from a per-dither table: t by multiply-shift (checked exact at compile time for every d2 inside the rim), each step of t found once for a pixel and its mirror, four pixels a turn | 520.2 → 390.8 | 5,439 → 3,953 (bg 2,287 → 802) | the interior was ~33 instructions a pixel (three 8-bit table reads, a divide-by-multiply with its sign fix, the dither's 2-D index, literal reloads); now 4: a `mull`, an `extui`, a load and a store in a zero-overhead `loop`. 1.9 KB of .bss for the table | kept |
| - | the bench widened (not a step): adults and elders of every species; then the review's 9,720 heavy frames (scarred, mottled, 55-130%, hopping, away, crossfading). 15,630 home frames, hash 9e8b4d7adc51ddc4 as main's; the S3 takes every 16th (977) | 390.8 → 424.7 (the heavier set) | 3,953 → 5,759 (fig 4,450: an adult puffkin 6,931) | the first board runs: mudge 25.9 ms; puffkin, an adult, 49.2 (d76c1bf) and 32.8 (9785a79), fig 27.7 of it | - |
| 4 | mottling's cell in 32 bits (`floorDiv7`, a biased unsigned divide by a constant), from the review | 424.7 → 421.4 (inside the noise) | 5,759 → 5,295 (fig 4,450 → 3,986) | `spotted()` called `floorDiv(int64_t)` twice a skin pixel: two `__divdi3` calls on the board, ~70-120 cycles each | kept |
| - | the S3 dump takes 4 frames in a row of every 64 (not 1 in 16), so what a frame keeps for the next shows; step 4 re-measured | - | 5,295 → 5,377 (another sample) | - | - |
| 5 | the outline's sums (middle, envelope, headroom, and the stand point) memoised on what they are pure in: his two bodies (every field), the pack and stage (his faces), his size, where he stands. From the review, keyed on whole FrameRefs rather than the RLE pointer | 421.4 → 389.7 | 5,377 → 4,860 (fig 4,062 → 3,546) | at rest every frame hits: the walk of both bodies' rows under three transforms (~1.2M instructions, 64-bit projections and roots) is gone; a moving pet misses and pays as before. 230 B of .bss | kept |
