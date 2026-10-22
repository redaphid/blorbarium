# Life stats and analytics

Two records the server reads from a badge, kept in one file:

- **Life stats** (#38): what one pet's life did, from the kid's hands
  (knocks, holds, hangs, time held) to his meals, spells of hunger and
  sickness, naps, duels, prophecies and death, plus his parent's and
  grandparent's lives, carried into the egg. An input to the personality
  and diary the server writes (#37, #39, #40).
- **Analytics** (#50): how the badge itself is doing (power, resets, frames,
  gesture latency, duels round by round, links, updates, errors), by day,
  for the owner to improve the system.

Both are bookkeeping only. The engine never reads a counter back, so no
decision, keepsake byte, replay hash or golden moves with them
(`Determinism.TheStatsAreNeverAnInput`).

| Part | Where |
|---|---|
| Life-stat slots | `lib/blorb/include/blorb/defs/life_stats.def` |
| Analytics slots and histograms | `lib/blorb/include/blorb/defs/analytics.def`, `HISTOGRAMS` in `analytics.h` |
| The file, the life record, the counting | `lib/blorb/include/blorb/life_tally.h`, `src/life_tally.cpp` |
| Day buckets, roll-up, acknowledgement | `lib/blorb/include/blorb/analytics.h`, `src/analytics.cpp` |
| Where the engine counts | `lib/blorb/src/dish.cpp` (`stats_.*` calls), `thoughts.cpp` (`foretold_`), `dish_duel.cpp` (`DuelStepped::met`) |
| Where the firmware counts | `src/analytics_fw.h`, and `fps::note`/`fps::same` and the trial's poll in `src/main.cpp` |
| Gesture latency into the analytics | `latency::onFinish` in `src/latency.h` |
| The verb | `STATS` in `lib/blorb/src/protocol.cpp` |
| Tests | `test/test_life_stats`, `LifeStats.*` in `test/test_two_dishes` |

## Cost

On the hot path an event is one increment, or one histogram bin (a loop
over at most nine edges), in RAM. Nothing is logged, printed or allocated
per event. The sense task (250 Hz) is not touched at all; the frame loop
gets two increments and a bin a frame (`fps::note`).

The file is written only when the keepsake is saved anyway (every 5 min),
and of those at most every 30 min (`Stats::kSaveEveryTicks`), unless a
life event (an egg laid, a hatch, a stage, a death), a flush (an update, a
reboot) or the badge going dark asks. Going dark is when the panel is off,
so no frame can wait on the write. A day of analytics is about 100 bytes
(LEB128); the whole file with three lives and sixteen buckets stays under
one 4 KB LittleFS block. Wear: at most ~50 writes a day plus one per
dark spell, spread by LittleFS over 1440 blocks.

A crash or power cut loses at most the counting since the last write (30
min awake; nothing in low power, which was written as it began).

## The file: `life.stats` on petfs

All little-endian:

    "BLLS"  version u8 (1)  seq u32  sections...  crc32 u32 (of all before it)
    section: tag u8, len u16, len bytes

`seq` counts the file's writes, across lives: the server keeps the highest.
Sections: tag 1 the life, tag 2 the analytics. A section this firmware does
not know is carried and written back as it was; a different version byte
is not read (the file is then started afresh, as a missing one is).

**Life section:** spells u8 (spells under way, so a reboot mid-spell does
not count it twice), lineage id u64, generation u16, this life's counters,
an ancestor count u8 (0 to 2), and each ancestor as generation u16 and its
counters. Counters: a slot count u8, then that many u32 in slot order. A
reader zeroes slots the file lacks and carries slots past its own.

**Analytics section** (LEB128 throughout): powered seconds, the next bucket
sequence number, the closed-bucket count, each closed bucket length-prefixed,
then the open bucket length-prefixed. A bucket: first, last, powered start,
span, wall first, wall last, the slot count and each slot, the histogram
count and each histogram's bin count and bins.

Slots and histograms are only ever added at the end, never moved, renamed
or removed.

**Lifetimes.** The life record follows the pet: when an egg is laid it
becomes the child's empty record, with this life as the parent and this
life's parent as the grandparent (`LifeTally::laid`). A boot that finds the
record one generation behind (the egg was laid and the power went before
the write) rolls it over the same way. Another lineage's record is not
read. The analytics belong to the badge and carry on across lives. A wipe
(`WIPE`) erases petfs, and both with it. An update never touches petfs.

## Reading it: `STATS`

On the phone's Bluetooth link and the cable (the protocol's grammar,
`docs/ble.md`):

    #1 STATS
    #1 + life lineage=00d15400000b gen=3 seq=118
    #1 + life presses=40 knock_knocks=31 knocks=12 double_presses=0 holds=5 ...
    #1 + life ... death=none
    #1 + parent gen=2
    #1 + parent presses=... death=starved
    #1 + grandparent gen=1
    #1 + grandparent ...
    #1 + analytics powered_s=86532 buckets=2 open=2
    #1 OK lives=3

    #2 STATS json            the whole document below, base64 in "+ <offset> <b64>" lines,
    #2 + 0 eyJzY2hlbWEiOi...   closed by "OK <bytes> <crc32>" (as SNAPSHOT)
    #2 OK 4210 9c1e02ab

    #3 STATS ack 1           the server has every closed bucket up to 1: they go
    #3 OK dropped=1 kept=0

`STATS` answers `ERR 409 SEALED` on a sealed board (nothing was read).
`STATS ack` writes the file at once.

## The JSON (schema `blorb.life-stats`, v 1)

```json
{
  "schema": "blorb.life-stats", "v": 1, "fw": "0.7.12",
  "mac": "d4:05:92:77:1b:44",                 // Wi-Fi upload only: the phone knows which badge it read
  "lineage": "00d15400000b", "gen": 3, "seq": 118,
  "name": "Mossby", "species": "mossby", "phase": "creature", "stage": "adult",
  "genome_fnv": "8a3c19f2",
  "genome": "QkxSRwEA...",                    // Wi-Fi upload only, base64 (the phone can read GENOME)
  "life": { "presses": 40, "knock_knocks": 31, "held_s": 1290, ..., "age_s": 51840,
            "child_at_s": 3600, "adult_at_s": 21000, "elder_at_s": 0, "death": null },
  "parent":      { "gen": 2, "life": { ..., "death": "starved" } },
  "grandparent": { "gen": 1, "life": { ... } },
  "analytics": {
    "powered_s": 86532, "bucket_s": 86400,
    "hist_edges": { "frame_ms": [10,20,33,50,100,200], "gesture_ms": [50,100,150,200,300,500,1000], ... },
    "closed_upto": 1,
    "buckets": [
      { "first": 1, "last": 1, "open": false, "powered_start_s": 0, "span_s": 86400,
        "wall_first": 1792000000, "wall_last": 1792070000,
        "counts": { "awake_s": 4100, "low_power_s": 82300, "frames": 190000, "knock_knocks": 22, "turns": 41, ... },
        "hist": { "frame_ms": [180000,9000,800,150,40,8,2], "battle_turns": [0,1,2,0,0,0] } },
      { "first": 2, "last": 2, "open": true, ... }
    ]
  }
}
```

- Every life slot is present (0 when nothing happened). Slots with unit
  Ticks are sent in seconds as `<name>_s`. `death` is `null` while he
  lives, else `old_age`, `starved`, `injured`, `poisoned` or `unknown`.
- A bucket's `counts` and `hist` hold only what happened: a slot or a
  histogram that is absent is 0. Seconds-valued slots end in `_s`. A
  histogram has one more bin than it has edges: a value lands in the first
  bin whose edge it is under, the last bin is everything past the last
  edge.
- `parent` and `grandparent` are absent for a founding line.

What every slot means is the comment on its row in the two `.def` files.
Notable ones: `presses` is the press word (BOOT or its twin, the knock
knock), so a knock knock counts as a press and as a knock knock;
`refused_asleep` and `refused_full` are pellets that dropped and were not
wanted (they also count in `pellets`), `refused_empty` a press that found
the pantry empty; `starving_spells` opens at hunger 0.8 and closes under
0.5; `turns_by_pet` is a round the kid let time out, so the pet picked.

## How the server stores it (#37's D1)

- **Life:** one row per `(lineage, gen)`, upserted with the highest `seq`
  (counters only grow within a life). `parent` and `grandparent` fill the
  rows of generations the server never saw.
- **Analytics:** one row per bucket per badge (by `mac`, or the phone's
  badge identity), keyed by `[first, last]`. A bucket replaces every row
  whose range lies inside its own: an open bucket is sent again, grown, at
  every upload, and a weeks-offline badge merges its oldest buckets into
  one range (so a merged bucket replaces the day rows it was made of).
  `wall_first`/`wall_last` place a bucket in time when the badge knew the
  wall clock; otherwise place it from the upload's own time and
  `powered_start_s` against the upload's `powered_s`.
- Answer `{"ack": <closed_upto>}` once stored. Only then does the badge
  drop those buckets. With no ack it keeps everything and sends it again.

## Never losing what was counted

- Counted in RAM, written to petfs at least every 30 min awake, as the
  badge goes dark, after every life event, and before an update or reboot.
- A closed bucket leaves the badge only on an ack naming it.
- Offline for weeks: past 16 buckets the oldest two closed ones merge
  (counts and bins add, the range and span widen), so the file has a fixed
  size and nothing is dropped (`Analytics.WeeksOfflineRollUpAndLoseNothing`).
- A file that will not read (torn, foreign, a newer version) is left as it
  is until the next write replaces it, and the badge counts afresh.

## Upload paths

**Bluetooth (the phone app, #42, and the website).** On connect: `STATS json`,
POST the document to the server with the badge's identity, then send
`STATS ack <n>` with the server's `ack`. Hours or days late is fine.

**Wi-Fi (wifi-ota-2, #26/#27; not on main).** In the existing window, never
an extra one: after the manifest is read (phase `manifest`), a phase
`stats` POSTs `blorb::lifeStatsJson(*dish, {macString, true})` to the
stats endpoint through the same `hw::NetJob` (it needs a POST: the body
from PSRAM, the same TLS roots and limits), reads the reply's body, and
calls `dish->ackStats(*blorb::statsAckOf(body))` when it names one. A
failed or cut POST changes nothing on the badge and does not fail the
window or its backoff. It goes before the image phase, so a download that
restarts the badge never strands it; the window's close still waits for
the job as it does now. Count it with `anx::uploads`, and the window's own
outcomes with `wifi_windows`, `wifi_joined`, `wifi_failed`,
`wifi_installed`; a battery reading a minute goes to
`hist::battery_mv` (`src/hw/battery.h` there).

## On a board

    STATS                       the counters, readable
    STATS json                  the document, chunked
    DEBUG fps / DEBUG latency   unchanged; compare before and after (below)

Proof of no responsiveness cost, on one badge with the cable:

1. Flash `origin/main` (the base), `DEBUG fps` after 10 s still, then knock
   knock five times, side hold three times, hang once, `DEBUG latency`.
2. Flash `life-stats`, the same steps. `fps=`, `draw_ms=`, `worst_ms=` and
   every control's `panel=` p50/p95 should match within run-to-run noise.
3. `STATS` should show the knock knocks, holds and the hang you made.
