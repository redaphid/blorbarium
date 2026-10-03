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

The board advertises the service UUID and puts its name, `blorb-xxxx` (the
last two bytes of its MAC, lower-case hex), in the scan response. Filter on
the service UUID or on the name prefix `blorb-`. There is no pairing and no
owner token.

## Framing

- Lines are ASCII and end in `\n`. A `\r` is ignored.
- The board asks for an MTU of 247 and splits each reply line into
  notifications of MTU - 3 bytes, then a separate `\n`. Reassemble by
  buffering notifications until `\n`; never treat one notification as a line.
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
#1a OK fw=0.3.0 fmt=1 lineage=9f31c2d04a7b gen=0 name=Grungo phase=egg feats=0x00
```

`phase` is `egg`, `creature` or `clutch`. Trailing words after `HELLO` are ignored.

### STATE

```
#2b STATE
#2b + phase=creature gen=0 stage=baby age=309 action=hop_circles face=neutral asleep=0
#2b + drives hunger=301 sleepiness=0 boredom=2 loneliness=1 fear=8 pain=1 discomfort=3 need_touch=1
#2b + body life=1000 injury=0 glow=436 dreaming=0
#2b + dish pantry=4 pellets=0 night=1 time=unknown
#2b OK
```

Levels are thousandths (1000 is full). `time=unknown` turns `known` once a
`TIME` has been accepted this boot.

### TIME

```
#7 TIME <unix seconds> [tzMinutes]
#7 OK caught_up=72000
```

`tzMinutes` is -720 to 840 and defaults to 0. TIME is more than a clock set:
the board compares the wall time against its own record, lives through any
unpowered gap it finds (the pet ages, eats from the pantry and can hatch or
die), snaps the pet's day and night to the real day, and saves at once.
`caught_up` is how many 100 ms ticks that catch-up ran (72000 is two hours).
A bad number or tz is `#7 ERR 400 BAD_ARGS`.

### TWIST rename

```
#4e TWIST rename Grungo_V
#4e OK rename
```

A name fits in 15 characters (the saved name is 16 bytes). A missing, too long
(`Grungo_the_sixteenth`) or invalid name is
`ERR 400 BAD_ARGS`, as in `TWIST rename bad!name`.

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
answers it with an error and never runs it.
