# The Bluetooth link

The board speaks the same line protocol over Bluetooth LE that it speaks on
the serial cable. This page is what a web page needs to talk to it. Every
reply line below is copied from a test that asserts it exactly
(`test/test_protocol`, `test/test_dish`), so the tests keep this page honest.

## Service

Nordic UART Service (NUS), from `src/hw/link_nus.h`.

| What | UUID | Direction |
|---|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | |
| RX characteristic | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | the page writes (write or write without response) |
| TX characteristic | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | the board notifies |

The board advertises the service UUID and puts its name in the scan response.
The name is its creature's species, as the lowercase slug from
`species.def` (`mossby`, `hushwing`). It is the same whatever the owner named
the pet. Each badge in `defs/badges.def` holds a different species, so the name
also tells the badges apart. The advertisement carries the flags and the
128-bit UUID (21 of its 31 bytes). The scan response holds the name, and the
longest slug, 8 characters, takes 10 of its 31 bytes.

Which species (`advertisedSpecies` in `founding.h`):

- The pet's own species.
- For a pet from 0.4.0, which has no species yet, the species his board
  will adopt him as (`Dish::adoptSpecies`): the table's, else one of the
  regular cast from a hash of the MAC (`badgeSpecies`).
- For a sealed board, whose pet was not read, the board's species
  (`badgeSpecies`).
- An image on an OTA trial still advertises the old `blorb-xxxx` (the last
  two bytes of its MAC, lower-case hex), so a trial looks the same as the
  board the page updated. The page from rel-site keys its OTA token by the
  Web Bluetooth `device.id`, which stays the same whatever the board
  advertises. An older page keys it by the name, and when that page is
  reloaded during the trial it must still find its token to send
  `OTA CONFIRM`. The species name starts at the first boot after the trial
  keeps the image.

Filter on the service UUID. A `?d=<name>` link must use the current name: after
the update, `?d=mossby`, not `?d=blorb-1b3c`. There is no pairing and no owner
token.

## Framing

- Lines are ASCII and end in `\n`. A `\r` is ignored.
- The board asks for an MTU of 247 and splits each reply line into
  notifications of MTU - 3 bytes, then a separate `\n`. Reassemble by
  buffering notifications until `\n`; never treat one notification as a line.
- When the radio's queue is full, the board holds the next notification and
  resends it a few ms later. A slow connection interval delays a long reply
  such as GENOME but never drops its bytes.
- A request line is at most 200 bytes. A longer one is answered
  `#0 ERR 413 TOO_LONG` (or `#<id> ERR 413 TOO_LONG` when its id parsed).
- The board asks for a 30 to 50 ms connection interval and a 6 s supervision
  timeout about 2 s after connecting.

## Requests and replies

A request is `#<id> VERB args`, with `<id>` 1 to 6 hex digits the page
chooses. Every reply line starts with the same `#<id>`. Zero or more
`#<id> + ...` continuation lines come first, then exactly one final line:
`#<id> OK ...` or `#<id> ERR <code> <TEXT>`. A reply always goes back over
the link its request came from, so serial traffic never reaches the phone.

### HELLO

```
#1a HELLO
#1a OK fw=0.4.0 fmt=1 lineage=9f31c2d04a7b gen=0 name=Grungo phase=egg feats=0x00
```

`phase` is `egg`, `creature` or `clutch`. Trailing words after `HELLO` are ignored.

### STATE

```
#2b STATE
#2b + phase=creature gen=0 stage=baby age=309 action=hop_circles face=neutral asleep=0 species=grungo mac=3c:0f:02:6f:2a:ec
#2b + drives hunger=301 sleepiness=0 boredom=2 loneliness=1 fear=8 pain=1 discomfort=3 need_touch=1
#2b + body life=1000 injury=0 glow=436 dreaming=0
#2b + dish pantry=4 pellets=0 night=0 time=unknown dark=0 still=0
#2b OK
```

Levels are thousandths (1000 is full). `time=unknown` turns `known` once a
`TIME` has been accepted this boot.

The first line ends with `species=`, what the pet is (as the boot line's
`species=`; `none` while sealed), and `mac=`, the board's own MAC (`none`
in the sim and the host tests). They are on `STATE`'s reply only, not on the
`! STATE` event.

`action` is what he is doing: an action's name, or while he plays on his own
the idle's (`lib/blorb/include/blorb/defs/idles.def`: `dance`, `juggle`,
`dig`, `sing`, `peek`, `stargaze` and the rest).

`night` is the wall's night: 1 only once a phone's `TIME` has set his pet
hours and wall time is known this boot. A board with no clock never has a
night of its own. `dark` is what he lives by (no light, so melatonin and
sleep): the wall's night, lying face down (lidded), or nobody having moved the
badge for `still` minutes past 45 (`PetClock::kDimAfterTicks`).

A board whose keepsake would not open at boot (see HANDOFF, issue #2) ends the
first line with ` sealed=1`. Its occupant is then a placeholder egg that never
changes: the real pet is on the flash, untouched, and nothing on the board
reads or writes it until a grown-up repairs it. A board whose save was written
by newer firmware (an update rolled back) is sealed the same way and says
` sealed=newer`: updating the firmware wakes it. Any other board omits the field.

### ADOPT

```
#8 ADOPT status
#8 OK adopted=0 waiting=1
#9 ADOPT
#9 OK adopted=1 waiting=0
```

A board that ships with `hatchNeedsPhone` holds its egg still, with no
incubation and no hatch, until the owner adopts it. Connecting, `TIME` and
flashing do not count; only `ADOPT` does (the page's Adopt button). It is
kept in its own file (`adopted.flag`) beside the keepsake, so a replug does
not ask again, and a wipe clears it. `ADOPT status` only reports. `ERR 409
NOT_NOW` when the flag cannot be written (an image on trial, a sealed pet).
The debug link's `FREEHATCH` runs the egg without adopting.

### TIME

```
#7 TIME <unix seconds> [tzMinutes]
#7 OK caught_up=72000 away=7200 hurt=0
```

`tzMinutes` is -720 to 840 and defaults to 0. TIME is more than a clock set:
the board compares the wall time against its own record, lives through any
unpowered gap it finds (the pet ages, eats from the pantry and can hatch or
die), snaps the pet's day and night to the real day, and saves at once.
`caught_up` is how many 100 ms ticks that catch-up ran (72000 is two hours);
it is at most 12 hours however long the gap. `away` is the whole unpowered
gap in seconds, 0 when there was none. `hurt=1` means the gap was three weeks
or more: he woke starving and injured, and needs feeding now.
A bad number or tz is `#7 ERR 400 BAD_ARGS`.

TIME also gives his thoughts the owner's local time: until the board is next
unplugged, some lines know the hour, the morning, late night, Monday, the
weekend and Friday. Send the real `tzMinutes`, or they read UTC. Without a
TIME he thinks and foretells all the same, just without those lines.

### TWIST rename

```
#4e TWIST rename Grungo_V
#4e OK rename
```

A name fits in 15 characters (the saved name is 16 bytes). A missing, too long
(`Grungo_the_sixteenth`) or invalid name is
`ERR 400 BAD_ARGS`, as in `TWIST rename bad!name`.

Once he has a name from a rename, a few of his lines say it (a `_` reads as a
space). His species' own name never counts as one.

### TWIST say

```
#60 TWIST say Hello, Grungo!
#60 OK say
```

He says the rest of the line on the marquee, in his croak face, for one pass
across the dish (7 s plus 0.4 s a character), and then his own thoughts
resume. Nothing cuts it short. The text is uppercased and kept to what the
font draws: A to Z, 0 to 9, space and `. , ! ? - ' :`. Other characters are
dropped, spaces at either end are trimmed, and the line stops at 56
characters. An empty line, or one with nothing left (`TWIST say @#$`), is
`ERR 400 BAD_ARGS`. During an egg or a clutch it is `ERR 409 NOT_NOW`. It
works before `TIME`. It is never saved or hashed, and a reboot forgets it.
If he is asleep he stays asleep and says it with his eyes shut.

### TWIST teach

```
#70 TWIST teach I love you!
#70 OK teach
```

He keeps the line and says it now and then as one of his own thoughts, in his
own voice, long after the phone has gone. If he has hatched, he also says it
once right away. The text is cleaned as for `say` and stops at 30
characters. He keeps the last 4 lines taught; teaching one he already knows
makes it the newest instead of a twin. It works in any phase, egg included.

The lines live in their own file on the board (`taught.lines`), not in the
pet's save: they survive a reboot and a new generation, and they never change
the save or `HASH`. An empty line is `ERR 400 BAD_ARGS`. When the board cannot
write (a sealed pet) it is `ERR 409 NOT_NOW`, and while a new firmware is on
trial it is `ERR 409 TRIAL` (docs/ota.md, "The trial"). Either way nothing
he knows changes.

### TWIST unteach

```
#75 TWIST unteach
#75 OK unteach
```

He forgets every taught line. It takes no arguments (`TWIST unteach now` is
`ERR 400 BAD_ARGS`) and fails like `teach` when the board cannot write.

### HASH

```
#7 HASH
#7 OK hash=dc490887 tick=863999
```

`hash` is the whole saved state's hash, `tick` the pet's tick count. Two
runs fed the same lines give the same hash.

## Errors

`#<id> ERR <code> <TEXT>`. The ones a page meets:

| Line | When |
|---|---|
| `#0 ERR 400 BAD_ID` | the id is missing, not hex, or longer than 6 digits |
| `#0 ERR 413 TOO_LONG` | the line is over 200 bytes |
| `#5 ERR 404 UNKNOWN_VERB` | no such verb (there is no FEED: feeding is the body's) |
| `ERR 400 BAD_ARGS` | wrong arguments |
| `ERR 403 BODY_ONLY` | a stimulus only the body can give |
| `ERR 409 NOT_NOW` | not in this phase, such as a pick with no clutch |
| `ERR 409 NO_CREATURE` | a creature verb while he is an egg or a clutch |
| `ERR 416 OUT_OF_RANGE` | a number out of range |

`DEBUG` lines are serial-only. Over Bluetooth one reaches the protocol, which
answers it with an error and never runs it. Testing over Bluetooth has its
own characteristic, gated by a press on the dish: `docs/debug-ble.md`.
