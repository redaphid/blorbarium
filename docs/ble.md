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
The name is always the pet's name, lowercase (`lib/blorb/include/blorb/ble_name.h`).
A pet nobody renamed is named for his species (`Mossby`), so he advertises
the slug from `species.def` (`mossby`, `hushwing`), as every badge has since
0.7.0. A pet the owner renamed (`TWIST rename Zorp_V`) advertises that name
(`zorp_v`). It is also the GAP device name. The name changes with the pet, so
from 0.7.33 the board also advertises an id that never changes: its MAC.

### Advertisement

| Packet | AD structure | Bytes |
|---|---|---|
| advertisement | flags | 3 |
| advertisement | the UART service's 128-bit UUID | 18 |
| advertisement | manufacturer data: company `0xFFFF` (little-endian `ff ff`), then the 6 MAC bytes in print order | 10 |
| scan response | the name (a species slug, at most 8 characters: 10; a pet's longest name, 15: 17) | 3-17 |
| scan response | Improv's 128-bit UUID, only if it fits beside the name (a name of up to 11 characters; every species slug) | 18 |

The advertisement is full (31 of 31). The MAC is the Wi-Fi station MAC, the
one `defs/badges.def`, `blorb-xxxx` and `/admin` print (the Bluetooth address
is that plus 2; Web Bluetooth shows neither). `0xFFFF` is the Bluetooth SIG's
id for testing and internal use. The layout is `lib/blorb/include/blorb/ble_id.h`,
checked by `test/test_ble_id`.

Before 0.7.33 the advertisement's last 10 bytes held Improv's service data
(16-bit UUID `0x4677`, its boot state and capabilities). It no longer fits, so
it is left out: Improv clients that filter on Improv's UUID (improv-wifi.com)
still find the board, Home Assistant's matcher, which wants both, does not.
The website opens Improv after it connects by the UART service and needs neither.

On the same connection, Improv's service only asks "Join Wi-Fi?" when a
client reads its Current State or writes to it (0.7.36). Subscribing alone
never asks: BlueZ subscribes a reconnecting client again by itself, and on
0.7.35 a Linux client that sent only HELLO after a provision was asked again.
Reading or subscribing still counts as speaking, so a page that only
provisions is never dropped as a squatter (docs/wifi-ota.md, "Over Bluetooth").

Until 0.7.35 the scan response's Improv UUID was left out on every board, so
improv-wifi.com found none: NimBLE-Arduino's `getScanData()` returns the
advertisement (already full), not the scan response, and the UUID never fit
beside it. The scan response is now built whole (`NusLink::applyScan` in
`src/hw/link_nus.h`, at boot and at every rename); the boot line says
`scan response <n> B: name <n> B, uuid yes`. A renamed pet whose name is 12 to
15 characters keeps his whole name and leaves the Improv UUID out
(`uuid left out`): improv-wifi.com then misses the badge, the website does not.

A page asks Chrome's chooser for one board with
`{services: [NUS], manufacturerData: [{companyIdentifier: 0xFFFF, dataPrefix: <the 6 MAC bytes>}]}`.
That is what an NFC link `?b=<mac>` does (`docs/nfc.md`).

When (`blorb::advertisedName`, checked by `test/test_adopt_species` and
`test/test_ble_id`):

- From the first advertisement of every boot: an egg, a fresh board and one
  whose storage was empty all have a name, since a line is named for its
  founder's species the moment it is founded.
- On an OTA trial too. Through 0.7.42 an image on trial advertised
  `blorb-xxxx` (the last two bytes of its MAC), and a trial that keeps its
  image does not restart, so after every update, over Bluetooth or Wi-Fi, the
  badge stayed `blorb-xxxx` until its next reboot. Badges that run 0.7.42 or
  older still do that after an update until they restart. The page keys its
  OTA token by the Web Bluetooth `device.id`, never the name, so a trial needs
  no name of its own.
- Within a second of a change, without a restart: a rename, or a pet from
  0.4.0 adopting his board's species (`Dish::adoptSpecies` renames him from
  Grungo). The serial log says `[hw] ble name now <new> (was <old>)`.
- The one board with no pet name it can read is a sealed one (its pet was
  not read). It advertises its board's species (`badgeSpecies`), never
  `blorb-xxxx`.

Filter on the service UUID. The site's chooser, with no link, lists a device
with the UART service and either the id's company `0xFFFF` (0.7.33 and later,
whatever the pet is called) or a `blorb-` or species name (older firmware). A
`?d=<name>` link must use the current name: after the update, `?d=mossby`, not
`?d=blorb-1b3c`. A `?b=<mac>` link never goes
stale (`docs/nfc.md`). There is no pairing and no owner
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

A board that ships with `hatchNeedsPhone` holds its egg's clock still, with
no incubation by time, until the owner adopts it. A phone is never needed to
hatch it: a few seconds of hard shaking crack it open and hatch it, adopted
or not (`Egg::shaken`, test/test_shake_hatch). Connecting, `TIME` and
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
If he is asleep he stays asleep and says it with his eyes shut. While a
question shows (an update's, "Join Wi-Fi?", the debug unlock's) he says
nothing: the line is answered `OK say` and dropped, never kept for after
(0.7.36).

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

### MARQUEE

```
#80 MARQUEE Happy birthday!
#80 OK shown=1 len=15
```

The whole screen says the rest of the line: a crimson ground and enormous
black letters (FreeSans Bold 24pt at 2.5, three on the glass) marching right
to left at 110 px a second, as cyber-puck's banner does. It goes round twice
(more for a short word, at least 8 s; once, for a line too wide for two in a
minute), then the pet comes back. A press of the button takes it down
early, and the pet never feels that press. It
wakes a dark screen and keeps it lit while it runs. The text is uppercased
and kept to printable ASCII (the font has all of it), runs of spaces become
one, and the line stops at 120 characters, remote debug's limit.
Nothing showable is `ERR 400 BAD_ARGS`. During a duel, or while fight mode
looks for one, it is `OK queued=1` and goes up when the duel is over; one
running when a duel starts gives way and comes back after it. Only the
newest line waits. It is never saved. The cable's `DEBUG marquee <text>`
does the same (src/marquee.h).

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

`DEBUG` lines are not part of this protocol: sent here, one is answered with
an error and never run. They run over Bluetooth on the debug link's own
characteristic (`DEBUG <line>`, `LOG`, `WIFI`), at each line's reach, harm
gated by a press on the dish, and over Wi-Fi as remote debug's `debug`:
USB, Bluetooth and Wi-Fi are only transports, and each verb is listed once
for all three in `docs/debug-ble.md` "Every verb, every transport".
