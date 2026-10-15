# The debug link

The debug link jumps the pet between states over Bluetooth, so a person with
a phone can test the game: hatch now, grow up, die, change species, set
chemicals, fire a stimulus, force a thought, set the time, fast-forward,
wipe. It is in every firmware build, release included.

These are children's pets, and a stranger's phone in range can connect. So
the commands that can hurt, end or replace the pet need a press on the
dish's own button first, and nothing that writes runs while an update or its
trial holds the dish.

## The characteristic

The debug link is one more characteristic in the Nordic UART Service of
`docs/ble.md`.

| What | UUID | Direction |
|---|---|---|
| Debug | `6E400005-B5A3-F393-E0A9-E50E24DCCA9E` | the page writes (write or write without response); the board notifies |

It has its own buffers, so a debug reply never lands inside a protocol reply
on TX (6E400003). It shares the NUS connection: one phone at a time, and the
same connection numbers.

A central must still speak the line protocol on RX within 10 s of
connecting, or it is dropped (`docs/ota.md`, "squatters"). Lines on the debug
characteristic do not count. A page sends `TIME` at once. By hand (nRF
Connect), send `HELLO` on RX first.

## Framing

The framing is the protocol's (`docs/ble.md`):

- Lines are ASCII and end in `\n`. A `\r` is ignored. A line is at most
  200 bytes.
- A request is `#<id> VERB args`, with `<id>` 1 to 6 hex digits. Without an
  id, the reply's id is 0.
- Zero or more `#<id> + ...` lines come first, then one final line:
  `#<id> OK ...` or `#<id> ERR <code> <TEXT>`.
- Replies come back as notifications on the debug characteristic, split at
  MTU - 3 bytes, then a `\n`. Buffer to `\n`.
- Events are `! DEBUG ...` lines on the same characteristic.

A line from a connection that has already dropped is not answered.

## Who may run what

Every verb has a reach. The reach decides when the board runs it.

| Reach | What it may change | Needs the unlock | Refused while sealed, on trial or during an update |
|---|---|---|---|
| look | nothing | no | no |
| show | only what the screen shows; nothing is saved | no | no |
| write | what the save holds, but no more than his own life, a phone's `TIME` or a `TWIST stimulus` already could | no | yes |
| harm | could hurt, end, age, replace or rewrite him | yes | yes |

The checks run in this order, and the first that fails answers:

| Reply | When |
|---|---|
| `ERR 413 TOO_LONG` | the line is over 200 bytes |
| `ERR 400 BAD_ID` | the id is not 1 to 6 hex digits |
| `ERR 404 UNKNOWN_VERB` | no such verb |
| `ERR 409 SEALED` | write or harm, and the dish is sealed (its keepsake would not open, or newer firmware wrote it) |
| `ERR 409 TRIAL` | write or harm, and this image is on trial: its storage writes nothing, so nothing it changed would be kept |
| `ERR 409 OTA_ACTIVE` | write or harm, and an update session is open |
| `ERR 423 LOCKED` | harm, and this connection has not been unlocked |

Then the verb checks its own arguments: `ERR 400 BAD_ARGS` for a malformed
or missing word, and `ERR 416 OUT_OF_RANGE` for a number out of range or a
name the dish has no row for.

## The unlock

The unlock works like the update's question (`docs/ota.md`, "Confirming"):

1. Send `UNLOCK`. The screen shows "Debug unlock?" and "Press the button",
   with an amber ring that runs down over 30 s.
2. Someone holding the dish presses BOOT. A press in the first 1.5 s does
   not count, so a child who is already pressing has not said yes.
3. The board sends `! DEBUG unlock=open`. Harm verbs now run for this
   connection.

After 30 s with no press, the board sends `! DEBUG unlock=lapsed`.

While the question shows, the button belongs to it. The pet feels neither
the press nor the rest of that hold. The update's question comes first: an
update that opens cancels the unlock's question, and `UNLOCK` is refused
while an update is open.

The unlock lasts until `LOCK` or until the connection ends. A reconnect, or
another phone, starts locked. A press over the serial cable (`DEBUG press`)
answers too, since the cable is a hand on the dish. No Bluetooth command
presses the button.

## Commands

`HELP` lists every verb with its reach and arguments, from
`lib/debugble/include/debugble/verbs.def`.

### HELP, STATUS (look)

```
#1 HELP
#1 + HELP look
#1 + STATUS look
...
#1 + WIPE harm [species]
#1 OK 17

#2 STATUS
#2 OK unlock=locked scale=1 trial=0 ota=0 sealed=0 phase=creature stage=adult age=1987200 species=grungo look=own time=known dropped=0
```

`unlock` is `locked`, `asking` or `open` (for this connection). `scale` is
the time scale. `look` is the species he is drawn as, or `own`. `dropped` is
how many fast-forward ticks a slow loop could not run since boot.

### UNLOCK, LOCK (look)

```
#3 UNLOCK
#3 OK unlock=asking ms=30000
! DEBUG unlock=open
#4 UNLOCK
#4 OK unlock=open
#5 LOCK
#5 OK unlock=locked
```

Sending `UNLOCK` again while the question shows changes nothing, and its
`ms` is the time left. `UNLOCK` is refused `ERR 409 SEALED`, `ERR 409 TRIAL`
or `ERR 409 OTA_ACTIVE` in those states.

### THINK `<thought>` (show)

```
#6 THINK crunch
#6 OK think=crunch prophecy=0
#7 THINK p_feast
#7 OK think=p_feast prophecy=1
```

He says that line now, as if its condition held. Any row of
`lib/blorb/include/blorb/defs/thoughts.def`, by name or number, including
prophecies. With no creature: `ERR 409 NO_CREATURE`.

### FORTUNE `[prophecy]` (show)

```
#8 FORTUNE
#8 OK fortune=p_snack
#9 FORTUNE p_feast
#9 OK fortune=p_feast
```

A prophecy now. With no name, one is drawn from every prophecy row. A name
that is not a prophecy is `ERR 416 OUT_OF_RANGE`.

### LOOK `<species|off>` (show)

```
#a LOOK gilly
#a OK look=gilly
#b LOOK off
#b OK look=own
```

He is drawn as that species until `LOOK off` or a reset. His genome and his
save never see it. This is the serial `DEBUG species`.

### FIRE `<stim>` (write)

```
#c FIRE button
#c OK fire=button
```

Any stimulus of `defs/stimuli.def`, by name or number, at the next tick, as
the phone's. Unlike `TWIST stimulus`, the body-only `button` and `fed` are
allowed: `FIRE button` drops a pellet from the pantry.

### WALL `<unix> [tzMinutes]` | `WALL clear` (write)

```
#d WALL 1790259200 60
#d OK time=known wall=1790259200 tz=60 night=0
#e WALL clear
#e OK time=unknown
```

`WALL` sets the wall time without living a gap: the old anchor is forgotten,
so he does not catch up on the hours between. The day snaps to the new time,
as with `TIME`. `tzMinutes` is -720 to 840. A time before 2024 is
`ERR 416 OUT_OF_RANGE`. `TIME` on the RX characteristic still lives the gap,
up to 12 hours.

`WALL clear` forgets the wall time and its saved anchor, and unsets the
board's system clock, as a board that never met a phone. The next `TIME`
anchors afresh. A page sends `TIME` when it connects, so reconnecting makes
the time known again.

### SCALE `<1..60>` (write)

```
#f SCALE 60
#f OK scale=60
#10 SCALE 1
#10 OK scale=1
```

Fast-forward. At 60, each real second also runs 59 extra ticks of his life.
The board runs them as real ticks, so his age in the save is exactly the
ticks he lived, and the loop's clock only moves forward. At most 40 ms of
each loop goes to extra ticks. Ticks owed past one pet minute are dropped
(STATUS `dropped`), so a slow board fast-forwards less than asked, but never
in a burst.

The scale is never saved. It goes back to 1 on reboot, when the connection
that set it ends, and when an update opens.

### HATCH (harm)

```
#11 HATCH
#11 OK phase=creature stage=baby age=1
```

The egg incubates tick by tick until it hatches, then the dish saves. With
no egg: `ERR 409 NOT_EGG`.

### STAGE `<baby|child|adult|elder>` (harm)

```
#12 STAGE adult
#12 OK phase=creature stage=adult age=864213
```

An egg hatches first. Then he ages into the stage: life is lowered a little
at a time and his own stage receptors fire, whatever thresholds his genes
carry. Then his drives are settled (fed, rested, unhurt) and the dish saves.
This is the serial `DEBUG stage`, and both run `debugble::growTo`.
`hatchling` is accepted for `baby`.

| Reply | When |
|---|---|
| `ERR 409 PAST_STAGE` | he is already past that stage |
| `ERR 409 NO_CREATURE` | a clutch waits for a pick |
| `ERR 409 DIED` | his life ran out first (the clutch is in the dish now) |

### WARP `<minutes>` (harm)

```
#13 WARP 30
#13 OK phase=creature stage=adult age=882213
```

1 to 120 minutes of his life, now, then the dish saves. The board does
nothing else while it runs. On the board this takes seconds for long warps.

### CHEM `<chem> <permille> ...` (harm)

```
#14 CHEM hunger 900 fear 0 adrenaline 500
#14 OK 3
```

Up to 8 pairs. Each name is a drive (`defs/drives.def`) or a chemical
(`defs/chemicals.def`), and each level is 0 to 1000 thousandths, as STATE
reports them. Every pair is read before one is set, so a line with one bad
pair changes nothing. The dish saves at the next tick. `CHEM life 0` lets
his genes end the life as they would.

There is no DIE verb. 0.7.0 had one; it went with the duel's Finish (owner,
2026-10-15: "Death should not be a choice"). A pet dies only the ways the
owner described: of old age, of neglect, or of wounds left untended, each by
his own chemistry over time. To test a death, bring him there and let time
finish it: `WARP` an elder through his last minutes, or `CHEM` him to the
brink (`life 0`, `hunger 1000`) and let the next ticks run his genes.

### SPECIES `<species>` (harm)

```
#17 SPECIES gilly
#17 OK species=gilly
```

His species gene and power gene are set in place, so he keeps his life, his
age and his name, and becomes a pure-bred gilly. Both edits are recorded in
the lineage as checkpoints, the path `TWIST gene_edit` takes, and the dish
saves. His other genes stay as they were. A line founded before species has
no species gene: `ERR 404 NO_SPECIES_GENE`, and only `WIPE <species>` gives
it a species. With no creature: `ERR 409 NO_CREATURE`.

### WIPE `[species]` (harm)

```
#18 WIPE mossby
#18 OK wiping found=mossby
#19 WIPE
#19 OK wiping found=nest
```

The pet, his lineage, his duel book and any friend egg are erased, and the
board restarts as a fresh one. 500 ms after the reply, the board:

1. erases the `pet` partition whole and the two superblocks of `petfs`;
2. provisions them by the rule boot uses (`blorb::provision`), which finds
   them blank and foreign beside no pet, and formats both, as on a board
   fresh from the factory;
3. writes `found.pin` when a species was named, so the founding egg is that
   one (`DEBUG found`);
4. restarts. Boot then founds, exactly as on a new board.

Nothing ticks the pet between the erase and the restart, so the old pet in
RAM is never saved over the fresh flash. The default `nvs` partition, where
the OTA trial keeps its records, is not touched. A sealed board refuses
(`ERR 409 SEALED`): its keepsake is waiting for a person to rescue it.

## What is gated, and why

- **Harm needs the unlock**: `HATCH`, `STAGE`, `WARP`, `CHEM`,
  `SPECIES`, `WIPE`. Each changes the saved pet in a way he cannot undo or
  live back from: it ages him, rewrites his genome or erases him, and
  `CHEM` can bring him to the edge of death (`life 0`, `injury 1000`), where
  his own genes take it from there. Hatching early
  takes a moment the child was waiting for. One press per connection is
  cheap for the person holding the dish and impossible for a phone across
  the room.
- **Write needs no press**: `FIRE`, `WALL`, `SCALE`. The owner asked for
  these to need none. Each is bounded by what a stranger can already do:
  `TIME` and `TWIST stimulus` are open to any phone today. `WALL` lives no
  gap at all, so it is milder than `TIME`. `FIRE` adds `button` and `fed`,
  which feed him. `SCALE` could age him to death if it outlived the phone,
  so it ends with the connection that set it.
- **Show and look need nothing**: `THINK`, `FORTUNE`, `LOOK`, `HELP`,
  `STATUS`. Nothing is saved.
- **Write and harm wait out a trial** (`ERR 409 TRIAL`). On trial the
  storage writes nothing (`ota::HeldStorage`), so a change would be lost or
  kept only partly, and the trial compares the pet it resumed with the pet
  the old image handed over. `UNLOCK` is refused too.
- **Write and harm wait out an update** (`ERR 409 OTA_ACTIVE`). The update
  saved the pet before its transfer and owns the button for its question.
  An update that opens also ends the time scale and the unlock's question.
- **Write and harm are refused on a sealed dish** (`ERR 409 SEALED`). A
  sealed dish writes nothing until a grown-up repairs it.

Saves stay atomic: every change goes through the Dish's own save
(`Keepsake`), and every jump runs real ticks, so the state it leaves is one
the engine itself could reach.

## What it does not do

- **No battle against a simulated opponent.** A duel opens only from the
  owner's own double press (`blorb::Air`'s consent rule; `test_two_dishes`
  `Consent.ThePhoneCanNeitherOpenADuelNorLockAMove`), by design. A
  simulated foe would need a second Dish in RAM (a 10 KB creature, its own
  keepsake) ticking on the loop, a bot for the consent gestures, and every
  outcome would write wounds, `duels.log` entries and a friend egg into the
  real pet. Two boards and `DEBUG double` over the cable test duels today
  (`tools/duel_hw.py`).
- **No button press over Bluetooth.** A remote press would answer the
  unlock's question and the update's, which is the one thing a stranger's
  phone must never do. The button stays in the hand.

## The website's panel

`index.html?debug=true` (or `debug=1`) shows a Debug section with a control
for each command, an Unlock button and a log of every line. Without the
parameter, `web/public/debug.js` draws nothing and sends nothing. (Any
`debug` parameter still shows the raw line log of `docs/ota.md`.)

The panel opens the debug characteristic on the connection the page made.
A firmware without the debug link has no such characteristic, and the panel
says so. `?mock=1&debug=true` runs it against the mock dish
(`web/public/ota-mock.js`, `MockDish.debug`), with a pretend BOOT press.

## Code map

| What | Where |
|---|---|
| Verbs, their reach and arguments | `lib/debugble/include/debugble/verbs.def` |
| Parsing, the gates, the handlers | `lib/debugble/include/debugble/console.h`, `lib/debugble/src/console.cpp` |
| The unlock | `lib/debugble/include/debugble/unlock.h` |
| The time scale | `lib/debugble/include/debugble/time_scale.h` |
| Hatch, grow, die (shared with serial `DEBUG stage`) | `lib/debugble/include/debugble/jump.h`, `lib/debugble/src/jump.cpp` |
| Forgetting the wall time | `Dish::forgetWall` in `lib/blorb/src/dish.cpp` |
| The characteristic | `src/hw/debug_char.h`, joined to the service by `NusLink::extend` |
| The board: warp, look, clock, wipe, the question on screen, the press | `src/debug_ble.h` |
| The wipe's erase and provision | `NvsFsStorage::wipe` in `src/hw/storage_nvs_fs.h` |
| The panel | `web/public/debug.js` |

## Tests

```
MSYS_NO_PATHCONV=1 wsl -d survivor --exec bash tools/wsl_test.sh test_debug_ble
uv run --with playwright==1.63.0 python web/test/debug_page.py
```

`test_debug_ble` covers the grammar; every harm verb refused locked with the
state unchanged; the unlock's 1.5 s, its lapse, its connection and LOCK;
every write and harm verb refused on trial (on a held store whose bytes stay
identical), during an update, and on a sealed dish; each jump; that WALL
lives no gap; that SCALE 60 lives 60 times the ticks with no clock jump and
ends with its connection; and that a whole debug session, then a reboot,
resumes the same pet (same hash, stage, age, species) and lives on, and
that a clutch left by a death resumes and hatches the next generation.

`debug_page.py` checks that the panel is absent without the parameter
(`debug` missing, `0` or `false`), and that with `debug=true` and `debug=1`
a locked HATCH is refused, the pretend press unlocks, and HATCH, SCALE,
STAGE and FORTUNE reach the dish.

## Cost

`badge128`, against `release-0.7` at a1b7cca:

| | Static RAM | Flash |
|---|---|---|
| Before | 191,516 B | 2,196,065 B |
| With the debug link | 194,500 B (+2,984) | 2,207,165 B (+11,100) |

Most of the RAM is the characteristic's buffers: a 2 KB notify queue and a
512 B receive ring.
