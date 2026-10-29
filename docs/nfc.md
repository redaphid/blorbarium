# NFC links

Each badge gets an NFC sticker with a link to the website that finds that one
badge. Tap it with an Android phone: Chrome opens the page, which says
"Looking for Mudge's Blorb", and Connect opens Chrome's Bluetooth chooser with
only that badge in it.

## The link

    https://blorb.hypnodroid.com/?b=<mac>

`<mac>` is the badge's Wi-Fi MAC, 12 hex digits, any case, with or without
colons or dashes between the bytes (`d40592771b30`, `D4:05:92:77:1B:30`).
`?b=blorb-1b30` (the last two bytes) works too. It is the MAC that
`defs/badges.def`, `blorb-xxxx`, the serial log and `/admin` show. Write the
bare 12-digit form on stickers: it is the shortest and never changes.

## How the page finds the badge

The chooser lists a device that has the UART service and any one of:

1. **Its id** (firmware 0.7.33 and later): manufacturer data, company
   `0xFFFF`, starting with the six MAC bytes (`docs/ble.md`, "Advertisement").
   It never changes, whatever the pet becomes.
2. **`blorb-xxxx`**: what firmware before 0.7.0 advertised, and what an image
   on an OTA trial still advertises.
3. **The name it advertises now**, for firmware before 0.7.33: the species of
   the newest pet the site has met on that MAC (`GET /api/ble-name?mac=`, which
   answers `{name, pet}` and nothing else), and the species `defs/badges.def`
   gives that badge. The page asks the site when it loads, not when Connect is
   pressed, since the chooser must open within the press.

"Can't find it? Search wider" lists every device with the UART service, as
without a link. A `?d=<name>` link still works as before; beside `?b=` it adds
its name to the chooser.

A badge on firmware before 0.7.33 whose pet has changed species since the site
last saw it, and that the site has never met, is found only by its table
species or by searching wider. Updating it to 0.7.33 makes its link exact.

## The badges

| Badge | Pet (species) | Link |
|---|---|---|
| 0 (237c, the owner's) | grungo | https://blorb.hypnodroid.com/?b=d4059277237c |
| 1 | mossby | https://blorb.hypnodroid.com/?b=d40592771b3c |
| 2 | puffkin | https://blorb.hypnodroid.com/?b=d40592771b44 |
| 3 | gilly | https://blorb.hypnodroid.com/?b=d40592771b34 |
| 4 (moth) | mothwick | https://blorb.hypnodroid.com/?b=d40592771cb0 |
| 5 (Midge) | mudge | https://blorb.hypnodroid.com/?b=d40592771b30 |
| 6 | fizz | https://blorb.hypnodroid.com/?b=d40592771b40 |
| 7 | hushwing | https://blorb.hypnodroid.com/?b=d40592772258 |
| 8 | tinker | https://blorb.hypnodroid.com/?b=d40592772240 |
| not in the table (seen by the site, 0.7.13) | grungo | https://blorb.hypnodroid.com/?b=3c0f026f2aec |

Rows 0 to 8 are `defs/badges.def` (237c's full MAC is inferred from its name
and the batch prefix there). The last is the one other MAC the site's
database holds (as of 2026-10-28). A new badge's MAC is in its serial log at
boot, or in `/admin` once it has been online.

`web/test/nfc.test.mjs` checks that every link here parses to a badge.
