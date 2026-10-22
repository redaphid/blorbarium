# Neighbours

Issue #33. The owner: "Slight multiplayer effects, where the pet would
comment, or get items or abilities occasionally when near a different pet."

This is a design. Nothing here is built into the pet yet. The one piece of
code is a prototype that nothing calls: `src/hw/neighbours.h`, tested by
`test/test_neighbours` (see "Prototype" at the end). Engine work waits until
`sleep-idle` (#20, #21) lands, because that changes the engine's behaviour and
its hashes.

## In fifteen lines

1. **Sensing uses Bluetooth, which is already on.** Every badge advertises
   all the time it lives, awake or in low power. A neighbour badge adds a
   **passive** scan: it only listens and sends nothing new. It listens for
   200 ms every 20 s while awake (1% of the time), every 5 s while a friend
   is near, and every 2 min with the screen dark (0.17%). It never scans
   during a duel, an update, a trial image or the off state.
2. **ESP-NOW is not used for this.** It stays the duel's, opened only by a
   kid's own gesture (`air.h`'s consent contract).
3. **Each advert gets 8 bytes** of manufacturer data (species, a few flags,
   a gift epoch, a thanks tag). That fills the 10 bytes free in the advert.
   Old firmware sends none and scans for nothing. A new badge still knows an
   old one by its address in `badges.def`.
4. **Nearness comes from RSSI in three bands**, *heard*, *near* (the same
   room) and *together* (side by side). A band changes on a 3-round median,
   with hysteresis and a dwell time. Walking past is not a visit.
5. **Noticing:** he looks to the side, waves and shows the friend's face on
   the rim.
6. **Comments:** one line in his own voice about that species. There is a
   line for each of the 72 pairs, listed below.
7. **Friends met** go in a small file, `friends.bin` on petfs, beside
   `taught.lines`. It is not part of the save, its hash or the lineage.
8. **Gifts:** after a minute side by side, the giver's species may give a
   small keepsake (a leaf, a feather, a bolt). Both badges decide it the
   same way from the same bytes, so no message has to agree on it. The
   receiver's thanks tag in its advert tells the giver that the gift landed.
9. **Abilities:** each species he meets teaches him one idle trick, drawn
   by the screen only. **No duel effect** is recommended. Two small options
   are below for the owner.
10. **Rare:** at most one gift a pet day from any one friend and three a day
    in all. A comment needs 30 min per pair and 5 min between any two. These
    are counted in pet ticks, so a reboot farms nothing.
11. **Presentation only, by default.** No neighbour event is an engine input,
    so the creature's hash and replay do not change. The engine stays as it is.
12. **The save format does not change.** It needs one new petfs file and no
    migration. Rolling back to old firmware is safe, because the old firmware
    never opens the file.
13. **Kids see it** as a wave, the friend's face on the rim, a line, an emoji
    bubble (#19) and a gift floating in from the rim. A friend who came by
    while the screen was dark is told about on waking.
14. **The cost** is about 1 mA awake and about 0.2 mA in low power (estimates,
    see "Power"). That is a few percent of what low power draws now.
15. **The phases** are N1 sensing on a badge, N2 noticing and comments, N3
    gifts and the shelf, N4 tricks and N5 optional engine and duel hooks.
    Each phase has its tests.

## What the badges already have

| | Bluetooth (NimBLE, `src/hw/link_nus.h`) | ESP-NOW (`src/hw/air_espnow.h`) |
|---|---|---|
| On when | always, from boot: advertising, connectable | only in fight mode and a duel (`Dish` opens it on a kid's hang or double press) |
| In low power | still advertising (`docs/power.md`); BT modem sleep on `badge128_pm` | off (fight mode keeps the badge awake) |
| In the off state (#7, planned) | off | off |
| Sends | an advert every 100-150 ms (interval 160-240 x 0.625 ms) at +21 dBm asked (the S3 tops out near +20) | lobby beacons every 300 ms at 20 dBm, then unicast State |
| Listens | **never scans**. It hears only a phone that connects | promiscuous plus ESP-NOW receive, only while open |
| What a listener learns | advert: flags and the NUS UUID (21 of 31 bytes). **The name (= species) is only in the scan response**, which a passive scan never asks for | a beacon's species, look and stakes. "HEARD n" on the fight banner counts these |
| Cost to keep on | already paid | Wi-Fi up: ~40 KB heap, receiver on continuously, keeps the badge awake |

So the answer to "is a passive BLE scan or ESP-NOW listen already on while
awake?" is **no, neither**. Every badge is already *talking* (advertising)
the whole time, and none is *listening*. The cheapest way to know a friend is
near is to listen briefly to the adverts that are already in the air.

## Sensing

### Why passive Bluetooth, not ESP-NOW

- **Consent.** `air.h`: "a closed Air hears nothing and sends nothing, and
  only the local owner's gesture opens it." ESP-NOW neighbours would break
  the one rule that makes duels safe for children. Bluetooth already
  broadcasts the species to the room as the badge's name, so listening for it
  adds no new contact.
- **Power.** ESP-NOW has no cheap listen. Its receiver is on whenever Wi-Fi
  is up, and it costs ~40 KB of heap. A BLE scan can be 200 ms in 20 s.
- **No new transmission.** A passive scan sends no scan requests. Nothing a
  badge sends changes except 8 more bytes in an advert it already sends.
- **The lobby still owns fight mode.** While the air is open, the scan is off
  and the duel's own peers are the neighbours.

### The advert (prototyped)

The advert is flags (3 B) + the NUS 128-bit UUID (18 B) = 21 of 31 bytes. A
manufacturer-specific AD structure costs 2 bytes of header, so **8 bytes of
data fit exactly**:

    [company 0xFFFF LE][0xB0 | version][species][flags][epoch][thanks u16 LE]

- **0xFFFF** is the Bluetooth SIG's company id for tests and internal use. No
  company owns it. The marker's high nibble 0xB tells our bytes apart from
  anyone else's 0xFFFF data.
- **flags:** hatched, asleep, low (screen dark), dueling. Bits 4-7 are
  reserved: kept and never read.
- **epoch:** the sender's gift epoch, `petTicks / 20 h`, mod 256 (see "Gifts").
- **thanks:** `thanksTag(giver, giver's epoch)` of the last gift this badge
  got, or 0.
- **Compatibility.** Bytes 2-7 keep these meanings forever. A later version
  may use the reserved flag bits, or bytes past the eighth (in the scan
  response, or an extended advert if the core ever has one). `decode()` takes
  any version >= 1 and ignores trailing bytes. Phones and the website are
  unaffected: they filter on the NUS UUID, which stays where it is.
- **Old firmware** sends no manufacturer data and never scans. A new badge
  still recognises it by the NUS UUID, and by its address: the Bluetooth
  address is the `badges.def` MAC plus one (`tools/ble_ota.py`; check this
  on a badge in N1). That address gives its species. An old badge can be
  noticed and commented on. It cannot give gifts, thank, or notice anyone.
- The advert is rewritten only when a byte changes (sleep, wake, low power,
  a new epoch, a gift), at most once a minute.

### When to listen (prototyped: `scanEveryMs`)

One scan window of **200 ms** hears every advertiser in range at least once:
the slowest advertising interval is 150 ms, plus up to 10 ms of the
advertiser's own random delay. The scan is NimBLE's
(`setActiveScan(false)`, the duplicate filter off so each advert's RSSI
arrives, `start(200)`). Its results are copied into a FreeRTOS queue in the
callback and drained on the loop task, in the same way as ESP-NOW's receive.

| State | Every | Listening |
|---|---|---|
| Awake, nobody near | 20 s | 1% |
| Awake, a friend near or together | 5 s | 4% (only while someone is near) |
| Low power (screen dark) | 2 min | 0.17% |
| Off state (#7), fight mode, a duel, an OTA session, a trial image | never | 0 |

A sighting is **never user input**. It must not reset `power::Policy`'s idle
timer, wake the screen or hold the badge awake. Otherwise two badges on one
table would keep each other awake forever. A phone connection does not stop
the scan, because NimBLE runs a scan and a peripheral link together, but the
scan is skipped while an OTA image is streaming.

### Power

There are no measurements yet (#10 owes current draw). These are
datasheet-class estimates:

- With the receiver on and the CPU running, the S3 draws roughly 80-100 mA.
  The chip cannot light-sleep through a scan window.
- **Awake:** 200 ms every 20 s is about 1% duty, about **0.8-1 mA** on top of
  an awake badge (the backlit panel and a 240 MHz CPU, probably 60-120 mA).
  That is about 1%.
- **Low power:** 200 ms every 2 min is about 0.17%, about **0.15-0.2 mA**.
  Low power today is ~70% light sleep, with the CPU at 80 MHz the rest of the
  time and BLE advertising. That is probably several mA, so the scan adds a
  few percent.
- **Advertising** 8 more bytes lengthens each advert packet by ~64 µs per
  channel. That is small next to the packets already sent.
- **The off state** (#7) turns Bluetooth off, so it costs nothing there.

The budget lever is `kLowEveryMs`. At 5 min it halves the cost again, and a
visit of a few minutes is still caught. N1 measures it with `DEBUG power`
and a meter.

### Nearness from RSSI (prototyped: `Proximity`)

RSSI is a poor ruler. A hand, a body or the case moves it 10-20 dB, and two
badges' antennas in different poses differ by as much. So:

- **Per round**, the loudest reading of each friend counts. Fades only ever
  make a reading quieter.
- **The level is the median of the last three rounds.** A single hand over
  the antenna moves nothing (`Proximity.OneLoudFadeDoesNotMoveABand`).
- **Bands:** *heard* (in range), *near* at >= -70 dBm (the same room, about),
  and *together* at >= -50 dBm (side by side in two kids' hands). Both
  numbers are guesses for +20 dBm advertisers, and N1 sets them.
- **Hysteresis:** a band is left only 6 dB below its threshold.
- **Dwell:** *near* takes 2 rounds in a row and *together* takes 3. A friend
  is gone after 3 rounds unheard.
- **Events,** in table order so that two runs agree: Arrived, Together,
  Apart, Left.
- **A full table** (12) drops only the quietest friend who is merely heard.
  A stranger far away never pushes out a friend who is near.
- **No bearing.** One antenna and RSSI cannot say which side the friend is
  on. "Looks toward the friend" is a convention: the badge with the lower
  address looks right, the higher one looks left. Two badges held side by
  side the right way look at each other, and kids will learn the trick.

**N1 calibrates the thresholds** with two badges and `tools/ble_ota.py scan`
(it prints RSSI). It measures 30 cm, 1 m, 3 m and the next room, in hand, on
a table, and with one badge in a pocket.

## What happens

### Noticing

On **Arrived**, when the ledger says it is a new meeting (the first ever, or
after 10 minutes apart):

- His eyes go to the side and stay there for a beat, using the lively
  layer's gaze, held. Then a small wave or hop, a reflex the screen draws.
- The friend's species face sits on the rim, small and faint, as the duel
  lobby draws ready friends. Up to four faces show, *near* faces dim and
  *together* faces bright. This also tells a kid who is around to fight.
- **A first meeting** gets a line: "NICE TO MEET YOU, MOSSBY", in his voice.
- **Asleep:** he does not wake. He smiles in his sleep, and on waking:
  "MOSSBY CAME BY WHILE I SLEPT."
- **Low power:** nothing is drawn. The visit is remembered, and the next
  wake says so in the same line.

### Comments (species to species)

The plan is a new thought situation, `neighbour`. Its predicate reads a new
`Observed::friendNear` field, as `FromPhone` is read: the friend's species,
its band, and whether the ledger allows a comment. Thoughts are presentation:
"no line changes him, and the Thinker is not saved or hashed" (`thoughts.h`).
So this adds lines without touching the creature.

Lines come from a pair table `BLORB_PAIR_LINE(speaker, friend, text)`,
generated from a new `NEIGHBOURS` section in each `personalities/*.txt` by
`tools/personality.py` (the `pet-personality` skill). If a pair has no line,
the speaker's generic line is used, with `@` standing for the friend's name:
"@! HELLO, @!". The longest name, MOTHWICK or HUSHWING, has 8 letters, so a
line with `@` is checked at 8. Then the voice wraps it, as it wraps every line.

First drafts for every pair among the nine badge species, in the font
(capitals and `. , ! ? - ' :`), at most 30 characters, before the voice
wraps them:

| Speaker \ friend | Line |
|---|---|
| **grungo** (mystic) | mossby: THE SNAIL WAS FORESEEN. SLOWLY · puffkin: THE PUFFBALL BRINGS SNEEZES. · gilly: A POND COUSIN! THE REEDS AGREE · mothwick: THE MOTH SEEKS MY MOON. NO. · mudge: MUDGE. YOU SMELL OF GOOD DAMP. · fizz: THE SPARKY ONE. KEEP HIM DRY. · hushwing: AN OWL. I AM ALREADY HOODED. · tinker: THE RACCOON EYES MY HOOD. |
| **mossby** (rambling) | grungo: A FROG IN A HOOD. HOW COSY. · puffkin: PUFFKIN, DON'T BOUNCE ON ME · gilly: GILLY CAME TO VISIT MY HOUSE · mothwick: MY SHELL IS NOT A LAMP, MOTH · mudge: MUDGE WENT PAST. I THINK. · fizz: FIZZ, MY SHELL IS ALSO SHINY · hushwing: SHH YOURSELF, HUSHWING. · tinker: TINKER, MY HOUSE IS NOT PARTS |
| **puffkin** (cheerful doom) | grungo: A FROG! HE FORESAW MY ACHOO! · mossby: MOSSBY! RACE YOU! I WIN! · gilly: GILLY! HUG! HUG! HUG! · mothwick: MOTHWICK! ACHOO! SORRY! AGAIN! · mudge: MUDGE IS FAST! I AM ROUND! · fizz: FIZZ ZAPPED ME! POOF! AGAIN! · hushwing: HUSHWING SAID SHH. ACHOO! · tinker: TINKER WANTS MY SPORES! YAY! |
| **gilly** (cheerful doom) | grungo: GRUNGO! POND PALS FOREVER! · mossby: MOSSBY! NICE SHELL! · puffkin: PUFFKIN! BEST HUG IN THE DISH! · mothwick: MOTHWICK! YOU GLOW SO NICE! · mudge: MUDGE! MUD BATH BUDDY! · fizz: FIZZ ZAPPED ME. IT GREW BACK! · hushwing: HUSHWING! I WILL BE QUIET-ISH! · tinker: TINKER FIXED MY FRILL! KIND OF |
| **mothwick** (rambling) | grungo: THAT FROG HAS THE MOON. HUMMM · mossby: MOSSBY'S SHELL IS... WARM? · puffkin: PUFFKIN SNEEZED. SPARKLY. OOH · gilly: GILLY'S FRILLS GLOW? NO. SADLY · mudge: MUDGE IS A BLUR. HUMMM. · fizz: FIZZ GLOWS. I MUST... NO. · hushwing: HUSHWING, ARE YOU A LAMP? NO. · tinker: TINKER HAS A TORCH. OH NO. OOH |
| **mudge** (terse) | grungo: FROG SPOTTED, RACE YOU · mossby: SNAIL, I LAPPED YOU TWICE · puffkin: BOUNCY ROOKIE, NEEDS MORE LAPS · gilly: GILLY, MUD BATH, NOW, GO · mothwick: MOTH, THE LAMP IS NOT A FINISH · fizz: FIZZ, STOP ZAPPING THE TRACK · hushwing: OWL, QUIET LAPS ONLY, FINE · tinker: TINKER, BUILD ME A FASTER FIN |
| **fizz** (hoarder) | grungo: FROG IS WET. STAY BACK. · mossby: THAT SHELL? SECOND SHINIEST · puffkin: PUFFKIN SNEEZED ON MY GOLD · gilly: GILLY IS WET. DO NOT HUG. · mothwick: MOTH WANTS MY GLOW. · mudge: MUDGE SPLASHED. NO. NO. · hushwing: OWL IS WATCHING MY STASH · tinker: TINKER. HANDS OFF MY GOLD. |
| **hushwing** (ominous) | grungo: THE FROG SEES TOO. NOTED. · mossby: THE SNAIL APPROACHES. SLOWLY · puffkin: THE PUFFBALL IS LOUD. SHH. · gilly: THE AXOLOTL SMILES. SUSPECT. · mothwick: THE MOTH FLIES AT NIGHT TOO · mudge: THE MUDSKIPPER. SO LOUD. SHH · fizz: THE BEETLE BUZZES. SHH. · tinker: THE RACCOON PROWLS. NOTED. |
| **tinker** (hoarder) | grungo: THAT HOOD. NICE FABRIC. · mossby: THAT SHELL. GREAT PARTS. · puffkin: PUFFKIN SPORES. FOR A GADGET · gilly: GILLY GROWS BACK. TELL ME HOW · mothwick: MOTHWICK. I HAVE A LAMP. · mudge: MUDGE NEEDS WHEELS. · fizz: FIZZ'S SPARKS. FOR MY BATTERY · hushwing: HUSHWING. NICE QUIET GEARS. |

More situations to write later, each a generic line with `@`:
`neighbour_first` (NICE TO MEET YOU, @), `neighbour_back` (@ IS BACK!),
`neighbour_slept` (@ CAME BY WHILE I SLEPT), `gift_got` (@ GAVE ME A
THING!), `gift_gave` (I GAVE @ A THING. NICE.), `neighbour_left`
(BYE, @...), and the duel tie-in `neighbour_rival` (@ AGAIN. REMATCH?) for
a friend this pet has fought, which the duel book knows.

### Remembering friends

The new file is `friends.bin` on petfs, a sibling of `taught.lines` and
`friend.egg`. It holds a magic `BLFR`, a version, a CRC32, and:

- **Up to 16 friends** (`neighbours::Friend`, ~32 B each): address, species,
  meetings, first met, last noticed, last comment, last gift (all in pet
  ticks), the last epoch taken from and thanked by, and flags.
- **The shelf:** up to 12 gifts kept, each {item, from species, pet tick}.
  The oldest common one goes first. A rare one is never dropped for a
  common one.
- **Tricks known:** a 16-bit mask, one bit per species met.
- **The ring of the last 3 gift ticks**, for the daily cap.

That is about 650 B, written atomically through `Storage::writeAtomic` and
only when something changed. A sighting alone is not a change. A new
meeting, a comment, a gift or a thanks is. On an image still on trial, or a
sealed pet, the file is never written, as `teach` refuses. Without a write,
nothing that would need one happens: a gift is not given if it cannot be
saved first.

Each file belongs to a badge, not to one life. It outlives a death, like
`duels.log`. A new pet meeting an old friend says "GEN 3 KNEW YOU" once, and
then it is his friend too. Whether the shelf passes down the line or starts
again is an owner question.

### Gifts

Gifts need a **visit**: the two badges *together* for a minute (60 s in the
band), both hatched, neither in fight mode. Two kids holding their pets side
by side is a ritual a 6-year-old can do, and walking past never does it.

**Agreement without a message (prototyped: `Ledger::receive`, `thanked`).**
The receiver decides. The giver's advert carries its epoch, so the receiver
rolls `giftRoll(giver, receiver, giver's epoch)`. That is FNV-1a over the
two addresses and the epoch, the same bytes on both badges. About half of
visits pass the roll (`chanceIn256` 128). The giver makes the same roll from
its own address, epoch and the receiver's address, so it can start its
"giving" animation at the same time. The receiver's caps can still say no:
the giver cannot see the receiver's daily cap. So the giver's animation is
only a wind-up, and the gift is *shown as given* only when the receiver's
advert carries `thanksTag(giver, epoch)`. The receiver advertises that tag
once its file is saved. So:

- the receiver gets each (giver, epoch) at most once, and a replayed advert
  or a reboot gives nothing more (`Ledger.OneGiftPerGiverEpochEvenReplayed`);
- the giver shows the giving at most once per (receiver, its epoch);
- a lost advert only delays the giving. It never splits the two stories:
  the receiver's gift is real once saved, and the giver's "gave" follows
  the thanks;
- a friend on old firmware never thanks, so nothing is shown as given to it.

**When both directions pass** in the same visit, it is a **trade**, and both
screens show the swap. Trades are not designed in. They happen about a
quarter of the time.

**What a species gives** is three items from its own list: two common and
one rare keepsake (1 in 8 of gifts, `giftKind`). Items are drawn in code, as
#18's emoji are, at about 16 px:

| Giver | Common | Common | Rare keepsake |
|---|---|---|---|
| grungo | a fly | a damp pebble | an omen stone (glows) |
| mossby | a leaf | a moss tuft | a tiny shell house |
| puffkin | a sparkly spore | a dandelion puff | a glitter cloud |
| gilly | a pond pebble | a bubble | a mended heart (plaster) |
| mothwick | a lamp bulb | a sock (he denies it) | a moonlight jar |
| mudge | a mud pie | a stopwatch | a gold medal |
| fizz | a crumb of gold | a spark | a gold nugget (and he complains: "FIZZ GAVE ME GOLD. HE IS SULKING.") |
| hushwing | a feather | a quiet hoot (a note) | a starry feather |
| tinker | a bolt | a spring | a tiny gadget |

**Where gifts go:** onto the shelf in `friends.bin`. Two places to show them
are open to the owner (question 1):

- **(a) Keepsakes only.** The pet admires a gift now and then, and a
  thought shows it: "@'S LEAF. STILL HAVE IT." The phone page lists the shelf.
- **(b) Usable.** A snack-type gift (fly, mud pie, crumb) drops a real pellet
  into the dish once. That is an engine input (`Habitat`), so it belongs to
  N5.

### Abilities

**Idle tricks (recommended).** Each species met teaches one trick, one bit in
the tricks mask. The lively layer (`lib/paint/src/lively.cpp`), which already
draws each species' breath, sway, bob, glance and blinks, adds the trick now
and then. It is drawn by the screen only and is not saved or hashed. He shows
it off when he meets that species again.

| Learned from | Trick |
|---|---|
| grungo | pulls an invisible hood up, then peeks |
| mossby | tucks in and peeks out slowly |
| puffkin | a sparkle sneeze (two pixels of glitter) |
| gilly | a frill wiggle |
| mothwick | a little loop, as if round a lamp |
| mudge | a sprint lap round the dish |
| fizz | a crackle of static on his outline |
| hushwing | a slow head swivel |
| tinker | rummages, finds nothing, looks pleased |

Collecting all eight from his siblings is a goal a kid can see: "I KNOW 5
TRICKS" on the phone page, or a row of faces on the rim when asked.

**Duel perks: recommend none.** A duel's `resolve()` is pure,
mirror-symmetric and checked by both badges' `outcome_hash`. Anything that
changes a duel has to cross the wire in the BattleCard. That bumps
`kCardVersion`, and a badge that has not updated then refuses a newer card
and cannot duel at all (`duel_wire.h` refuses an unknown card version). If
the owner wants one anyway, the least harmful forms are:

- **(i) Cosmetic:** a friend's gift shows as a charm in his corner of the
  duel screen. It needs no wire change, because it is drawn from the local
  file only.
- **(ii) One charge:** one extra spark pip at the start of his next duel only,
  spent or not, once per gift. It needs a card field, card version 2,
  balance runs in `tools/duel_balance.cpp`, and every badge updated before
  it is turned on.

## How kids see it

| Moment | On the screen |
|---|---|
| A friend arrives (near) | eyes to the side, a wave; the friend's face fades in on the rim |
| A first meeting | a line: NICE TO MEET YOU, @ (in his voice) |
| Now and then, near | a pair line (the table above), at most one per 30 min per friend |
| Together a minute | a heart, star or wave emoji in a bubble (#19's bubble), both faces brighter |
| A gift | the item floats in from the rim on the friend's side, bounces, sparkles; bubble: the item's emoji; line: @ GAVE ME A THING! |
| The giver, on thanks | the item floats out to the rim; line: I GAVE @ A THING. NICE. |
| A trade | both at once, crossing |
| On waking after a visit | @ CAME BY WHILE I SLEPT |
| A new trick | his first try of it, wobbly, with a "!" bubble |

Every piece stays clear of gestures and the duel HUD. Nothing shows during a
duel, a question, or a pellet the kid is aiming. The emoji go through #19's
cooldowns, so neighbours never flood the bubble.

## Rarity and fairness

| What | Limit | Why |
|---|---|---|
| A wave | once per arrival, and an arrival needs 10 min apart | a fade and back is the same visit |
| A comment | 30 min per friend, 5 min between any two, and the Thinker's own gaps | lines stay a treat |
| A gift received | once per (giver, giver's epoch); once per pet day (20 h) per giver; 3 per pet day in all; a minute together; a roll of 1 in 2 | no farming, and still fairly frequent among siblings |
| A gift shown as given | once per (receiver, my epoch), only after the thanks | both screens tell the same story |
| A trick | one per species, once ever | collectible, finite |

**No farming:**

- All time is pet ticks (10 Hz, saved with him), so a reboot or a power cycle
  resets nothing.
- The giver's epoch comes from its own saved clock. A badge whose clock
  jumps (a phone's TIME, a reset) can show a new epoch, but the receiver's
  "one per pet day per giver" still holds
  (`Ledger.FromOneFriendAtMostOneAPetDay`).
- Two kids together all day get at most one gift each way a day.
- **A phone pretending to be a badge.** nRF Connect can advertise any 0xFFFF
  bytes. The fix is to accept gifts only from addresses in `badges.def`,
  where an Android phone cannot easily put a public address. Gifts are
  keepsakes with no engine effect, so the worst case is a kid with extra
  stickers.

**No lasting duel advantage:** none at all by default. The two options above
are cosmetic, or one charge.

**Determinism and replay:**

- **The engine's inputs do not change.** The Dish is replayed from its saved
  state with the same samples, ticks, lines and duel datagrams. Neighbours
  feed none of these. The comments, gaze, wave, rim faces, emoji, gifts and
  tricks are all presentation, like the Thinker and `taught.lines`. The
  `hash()` of a creature is the same with neighbours on or off. The prototype
  is pure and its tests check that the same sightings give the same events
  (`Proximity.SameSightingsSameEvents`).
- **The ledger is plain data** (`Ledger`). A copy decides exactly as the
  original does, so a saved and reloaded ledger can never give a gift twice.
  The order is: decide, save `friends.bin`, then show and advertise thanks. A
  power cut before the save loses the gift and gives no thanks. A power cut
  after the save keeps it and thanks on the next boot.
- **If N5 makes a neighbour an engine input** (a `friend` stimulus that eases
  loneliness, or a pellet gift), it enters through one call like `sample()`.
  It is quantised to one event per arrival and species and lands at the next
  tick. Tests script it as they script the IMU. The hash then changes only
  when that event fires.

**Save format and migration:**

- Keepsake: no change. `kFormatVersion` stays 1, no chunk is added, and no
  migration is needed.
- New: `friends.bin` on petfs, with its own magic, version and CRC. A torn or
  foreign file reads as no friends, as `Taught::load` does. A newer
  version's file is left alone and not written, as the keepsake treats
  newer formats.
- Rollback: older firmware never opens it.
- If N5 ever puts neighbour state into the creature, it goes in a new
  keepsake chunk (12). Older firmware carries unknown chunks verbatim.

**Protocol compatibility:**

- Bluetooth: the advert gains a manufacturer AD. Old badges ignore it (they
  never scan), and phones and the site filter on the NUS UUID, which is
  unchanged. `decode()` accepts newer versions and ignores trailing bytes
  (`Advert.ANewerVersionIsReadForWhatThisOneKnows`).
- ESP-NOW: no change. The duel wire stays at version 2. A badge in fight
  mode advertises `dueling` and stops scanning.
- The phone protocol may later gain a `FRIENDS` verb that lists the file for
  the page. It is read-only, and an unknown verb is already answered with an
  error.

**Privacy (BATTLES.md's "no stranger contact"):** the advert carries no name
or text, only the species, four flags, an epoch and a 16-bit thanks tag.
The Bluetooth address is already public and fixed today. The thanks tag
says only that *someone* thanked a given address in a given epoch, and it
changes every epoch. The friend list never leaves the badge except to its
own phone page.

## Phased plan

Each phase is pushed as a branch and merged only with its gates green.
Phases N2 and later wait for #20/#21.

**N0 (done, this branch): the prototype.** `src/hw/neighbours.h` holds the
advert codec, `Proximity`, `scanEveryMs` and `Ledger`. `test/test_neighbours`
has 26 tests. Nothing includes it.

**N1: sensing on a badge, nothing shown.**
- `src/hw/scan_ble.h`: a NimBLE passive scan driven by `scanEveryMs`. Its
  callback copies {address, RSSI, manufacturer bytes} into a queue, and the
  loop drains the queue into `Proximity`. It is blocked by `RadioGate`'s
  air, `updater.active()`, the trial and the off state.
- The advert's manufacturer data, set from the pet's species and flags and
  rewritten only on change.
- `DEBUG near` prints the table (address, species, level, band, rounds) and
  the events. The debug link gets a `NEAR` verb.
- Gates: native `test_neighbours`; `pio run -e badge128_pm` builds. On two
  badges: the RSSI calibration table (30 cm, 1 m, 3 m, next room, in hand
  and on a table); `tools/power_e2e.py` still passes and low power is still
  reached with two badges side by side (a sighting is not input); a duel
  soak (`tools/duel_soak.py`) is unchanged with the scan on; heap before and
  after; the website still finds and updates the badge; the thresholds are
  set from the measurements.

**N2: noticing and comments (presentation only).**
- `Observed::friendNear`, the `neighbour*` thought rows, `BLORB_PAIR_LINE`
  and `personalities/*.txt` NEIGHBOURS sections through
  `tools/personality.py`. The gaze is held to a side and the wave is drawn.
  Rim faces, as in the lobby.
- `friends.bin` with its codec and `Ledger` save/load; "came by while I
  slept".
- Tests: the codec's round trip, a torn file and a newer file; the creature's
  `hash()` byte for byte the same over a scripted day with a friend arriving
  and leaving (presentation only); each pair has a line within 30 characters
  with `@` at 8; Thinker cadence: no more than one neighbour line per 5 min;
  sim goldens of a friend on the rim and a first-meeting line, looked at
  before blessing.

**N3: gifts and the shelf.**
- `Ledger::receive` and `thanked` wired in: thanks in the advert after the
  save, the item art (drawn in code), the exchange animation, the gift lines.
- Tests: two simulated badges over a fake scan (FakeAir's style: drops,
  doubles, lateness), with both stories agreeing; a power cut at each step
  (before the save, after the save, before the thanks) gives no doubles; the
  daily caps; the badge table allowlist; goldens of the gift arriving and
  the trade.

**N4: tricks.** The tricks mask and the lively layer's tricks, drawn by the
screen only. Tests: the paint bench hashes are unchanged with no tricks known
(the layer stays byte for byte); each trick drawn in a golden.

**N5 (owner's call): engine and duel hooks.** These are a `friend` stimulus
(eases loneliness) and a pellet gift, as engine inputs, plus the duel perk
option (i) or (ii). Each needs the owner's yes, a replay test with the input
scripted, and for (ii) a card version and a fleet-wide update before it is
turned on.

## Alternatives considered

- **ESP-NOW listening.** It breaks the consent contract, costs ~40 KB of heap
  and keeps a receiver on, and fight mode already uses it. Rejected.
- **Active BLE scanning** to read the name from the scan response. It works
  with old firmware's adverts as they are, but every badge would transmit
  scan requests to every other, and it still carries no epoch or thanks.
  Kept only as a fallback if the 8 bytes ever cannot fit.
- **A two-way handshake** (a BLE connection between badges, or ESP-NOW) to
  agree each gift. It needs a central role on every badge (NimBLE
  connections are tuned for one phone, `link_nus.h`), consent questions,
  and a protocol. The symmetric roll plus the thanks tag gives the same
  agreement with nothing sent but adverts.
- **Gifts decided by the giver alone and pushed.** That needs a transmission
  to a particular badge, which is the handshake above.
- **Neighbour state in the keepsake.** It would change the hash and need a
  chunk. A file beside it is enough until N5.

## Risks

- **RSSI is too noisy to tell *together* from *near*** through cases and
  hands. The fallback is to require a clink (both IMUs see a knock within
  300 ms, as the lobby's match does) for a gift. That costs nothing on the
  radio and is very kid-legible.
- **Scanning beside a phone link or OTA hurts throughput.** The scan is
  skipped during an OTA session, and N1 measures the rest.
- **The BT address is not MAC + 1 on every badge.** Old-firmware friends
  would then show as the wrong species, or as unknown. N1 checks all nine
  with `ble_ota.py scan`.
- **Light sleep and scanning.** On `badge128_pm`, a scan window must hold off
  light sleep, so it takes a PM lock for its 200 ms. N1 proves the scan works
  in low power and that the badge sleeps between scans.

## Open questions for the owner

1. **Should gifts be things a kid can see and use?** Keepsakes on a shelf
   (seen on the phone page and in his thoughts) are recommended. Should
   snack gifts drop one real pellet, which is a small engine effect?
2. **Should neighbours affect duels at all?** None is recommended. If yes,
   should it be (i) a cosmetic charm, or (ii) one extra spark pip in the
   next duel only? (ii) needs every badge updated before it is turned on.
3. **Does the friend list and shelf belong to the badge or to one life?** A
   badge list that outlives deaths (like the duel book) is recommended,
   with the new pet saying "GEN 3 KNEW YOU".
4. **How often should gifts come?** The proposal is at most one a pet day
   from each friend, three a day in all, and half of minute-long visits.
   Among siblings who meet daily, that is about one gift a day per kid.
5. **Should a visit wake a dark screen,** so the kids see it happen, or only
   be told on the next wake? Not waking it is recommended (battery).
6. **Should gifts need a gesture?** A minute side by side is automatic. A
   clink (knock the two badges together) would make it deliberate, and also
   works if RSSI proves too noisy.
7. *(Withdrawn: it rested on a "gift device" that doesn't exist (#36). Every
   board, grungo and 237c included, is a neighbour like the rest.)*
8. **Should comments ever be unkind?** The drafts tease (fizz hoards, mudge
   brags, hushwing shushes) but are never mean. Is that the right line for
   ages 6-13?
9. **Should friends' faces stay on the rim** while a friend is near, or appear
   only on arrival? Staying (faint) is recommended, because it also shows
   who is around to fight.

## Prototype

`src/hw/neighbours.h` is pure C++17 with no Arduino and no NimBLE, like
`power.h`. Nothing includes it but its test. `test/test_neighbours` checks:

- **Advert:** the round trip; the exact bytes; that it fits the advert's 31
  bytes beside the flags and the NUS UUID; refusing other companies, other
  markers and version 0; reading a newer version with reserved bits and
  trailing bytes; that the thanks tag is never 0.
- **Proximity:** Arrived after two rounds near; far away is never near;
  together and its time; a single fade moves nothing; hysteresis; apart,
  then left after three missed rounds; an old-firmware badge known by its
  table guess; a stranger never displaces a friend; the same sightings give
  the same events.
- **Schedule:** never during a duel or off; 1% awake and 0.2% dark at most;
  a window covers the slowest advertiser.
- **Ledger:** a wave at a first meeting and after 10 min apart; comment gaps;
  a minute together for a gift; both badges agree with no message, and the
  thanks closes it once; one gift per (giver, epoch) even replayed or after
  a reboot; one a pet day per friend; three a day in all; eggs and duelists
  give nothing; about half of visits give, and 1 in 8 gifts is rare; a
  crowd of strangers forgets the least recently met.
