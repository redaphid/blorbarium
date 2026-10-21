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
