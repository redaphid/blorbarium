# Updating the firmware over Bluetooth

The website can put a new firmware on a dish over Bluetooth, from Android
Chrome, with no cable and no Wi-Fi. These dishes belong to children, so the
design rests on four promises:

1. **The pet survives every outcome.** The pet lives in the `pet` NVS
   partition and on `petfs`. An update writes only the inactive app slot.
   A save is written before the transfer starts and again just before the
   restart, and the dish does not restart without it. A new image on trial
   writes nothing to `pet` or `petfs` until it keeps itself.
2. **Only the project owner can make an image the dish accepts.** Every image
   is signed. The dish refuses an image whose signature, board or version is
   wrong before it writes a byte.
3. **Someone holding the dish says yes.** The dish installs nothing until its
   button is pressed while the screen asks. A stranger's phone in range can
   make the dish ask, and nothing more.
4. **A bad image undoes itself.** A new image runs on trial. It keeps itself
   only after it has resumed the same pet and a page has reached it over
   Bluetooth. If it crashes, hangs, loses the pet or hears from no page
   within 5 minutes, the bootloader goes back to the old image. No second
   update starts while an image is on trial: the old slot is the only one
   known to boot, and there is no factory image behind it.

## The protocol

The update uses the Nordic UART Service from `docs/ble.md` plus one more
characteristic in the same service.

| What | UUID | Direction |
|---|---|---|
| OTA data | `6E400004-B5A3-F393-E0A9-E50E24DCCA9E` | the page writes without response |

Control is ordinary request lines, `#<id> OTA <sub> [args]`. Image bytes go
on the data characteristic. Only the Bluetooth link answers `OTA`. On the
serial cable, and on a firmware without updates, `OTA` is
`ERR 404 UNKNOWN_VERB`, which is how a page learns that the dish cannot be
updated over the air.

### States

```
idle --BEGIN (right board, newer, fits, not on trial, not busy, signed)--> confirm
confirm --button pressed after 1.5 s (not on trial, not busy, save written)--> receiving
confirm --30 s with no press--> idle  last=no_confirm
receiving --last byte, hash and image check pass--> ready
receiving --5 min with no byte--> idle  last=timeout
receiving --ABORT, bad hash, bad image, flash error--> idle  last=<why>
ready --REBOOT, or 20 s; save written--> restart into the new image (on trial)
ready --the save fails twice--> idle  last=save_failed (the old image stays)
```

A disconnect changes nothing. The session waits for the page to come back,
which is how a transfer resumes.

A session belongs to the page's token: 16 random bytes that the page makes
once for each dish and version and keeps (in `localStorage`), so a reload or a
second tab of the same phone still owns its update. The image is public, so
anyone can send its BEGIN, but only the token's page can resume it, ABORT it,
REBOOT it, or CONFIRM the new image after the restart. Any other token gets
`ERR 403 NOT_YOURS` for as long as the session is open. Data writes also need
the connection that last presented the token, so a stranger's writes never
reach the slot.

The dish serves one phone at a time. `NusLink`'s `onConnect` drops a second
phone, counting live connections in NimBLE's own host table. NimBLE's
connection limit stays at the library's 3, and `src/hw/link_nus.h` asserts
it. A limit of 1 was tried on the board and broke Bluetooth: NimBLE's host
indexes per-connection arrays by the controller's raw handle with no bounds
check (`ble_gap.c` `slave_conn[MAX + 1]`), a dropped link left a stale peer,
and the dish stopped advertising until a power cycle. `NusLink::poll()` now
also advertises whenever no connection is live by the host's table, and logs
`[hw] ble conn h=`, `[hw] ble drop h= reason=`, `[hw] ble adv on|off` and
`[hw] ble: a stale peer, advertising again`.

A central must speak the protocol (a known verb, with or without an id, or an
`OTA` line) within 10 s of connecting, or it is dropped
(`[hw] ble drop ... reason=idle`) and its address is refused for a minute
(`[hw] ble refused`, said once). A page always sends TIME or an OTA line at
once, and a connection with an update in progress is never dropped this way.
This keeps another program that knows the board's address (on the test board,
a badge daemon from its earlier life) or a stranger from holding the dish's one
connection. The decision is `blorb::Squatters` (`lib/blorb/include/blorb/squatters.h`,
`test/test_squatters`). Each connection's line names the peer
(`[hw] ble conn h= addr= type=public|random`), and its first line is logged
once (`[hw] ble first line: ...`). Each line carries the connection
it arrived on, tagged as its bytes come in, and a new connection drops the last
one's unread bytes and half line (`lib/blorb/include/blorb/rx_lines.h`).

### OTA BEGIN

```
#5 OTA BEGIN lcd128 0.4.0 0 1 1180416 <sha256> <sig> <token>
#5 OK state=confirm at=0 size=1180416 id=8c1f00a2 chunk=240
```

The arguments are the board, the version (`major.minor.patch`), the flags,
the save format (`fmt`), the image size in bytes, the image's SHA-256, the
signature and the page's token. The hash, the signature and the token are
base64url without padding (43, 86 and 22 characters). The release tool writes
the first seven into the manifest, so the page copies them and adds its
token. The whole line stays under the 200-byte cap (about 195 bytes with a
three-digit id).

`id` is the first four bytes of the hash in hex. It names the image in every
later reply. `chunk` is the most image bytes one data write may carry: the
link's MTU, less 3 for ATT and 4 for the offset.

While a session is open, a BEGIN for its own image with its own token is not
an error. The dish answers with where that session stands (`state=confirm`, `state=receiving
at=<n>` or `state=ready`). This is the resume: after a dropped connection the
page sends its BEGIN again and carries on from `at`. Any other BEGIN while a
session is open is `ERR 409 OTA_ACTIVE` (send `OTA ABORT` first).

With no session open, the dish checks, in this order, and answers the first
that fails. The cheap checks come first, because the signature check stalls
the loop for a moment:

| Reply | When |
|---|---|
| `ERR 400 BAD_ARGS` | a field is missing or malformed |
| `ERR 403 WRONG_BOARD` | the image is for another board |
| `ERR 409 NOT_NEWER` | the version is not newer than the dish's, and flag 1 (allow older) is not set |
| `ERR 413 TOO_BIG` | the image is empty or bigger than the app slot |
| `ERR 409 TRIAL` | the running image is still on trial |
| `ERR 409 BUSY` | the pet is doing something that must not be interrupted |
| `ERR 429 TOO_SOON` | a signature was checked less than 2 s ago |
| `ERR 403 BAD_SIGNATURE` | the signature does not verify against the key built into the dish |

The limit of one signature check every 2 s means that a flood of BEGINs
cannot stall the pet or starve the trial's watchdog. The page waits 2 s and
tries once more.

Busy today means a clutch is on the dish (its eggs are chosen with the same
button) or the `pet` partition will not open (no save could be written).
`DishOtaHost::busy()` in `src/main.cpp` is where a future battle, or anything
else he must finish, refuses too.

### Confirming

While the state is `confirm`, the screen asks for the button. The button
press goes to the update and not to the pet. A press in the first 1.5 s of
the question does not count, so a child already pressing when the question
appears has not said yes. On a later press, the dish checks that it is still
not on trial and not busy, writes a save, opens the inactive slot and moves to
`receiving`. Busy at the press is `idle last=busy`, and a failed save is
`idle last=save_failed`. A `DEBUG press` over the serial cable answers too: the
cable is a hand on the dish.

After 30 s with no press, the session ends with `last=no_confirm`.

### OTA STATUS

```
#6 OTA STATUS
#6 OK state=receiving at=40960 size=1180416 id=8c1f00a2 chunk=240
#7 OTA STATUS
#7 OK state=idle board=lcd128 fw=0.4.0 chunk=240 trial=0 last=none rolledback=none
```

STATUS answers in every state and changes nothing, so it is safe to repeat.
It is the truth about a transfer. Events (below) are only hints that save the
page a poll. In `idle`, `board` and `fw` say what an update would need,
`trial=1` says that this image is still on trial (it may yet roll back),
`rolledback` names the last update that switched slots and then rolled back
(`none` once a later update keeps itself), and `last` says how the last
session ended:

| `last` | Meaning |
|---|---|
| `none` | no session since boot |
| `no_confirm` | nobody pressed the button within 30 s |
| `busy` | the pet became busy before the button was pressed |
| `save_failed` | the save before the transfer failed, so nothing was written |
| `aborted` | an `OTA ABORT` |
| `timeout` | 5 minutes with no image byte |
| `overflow` | more bytes arrived than the size said |
| `bad_hash` | the bytes do not hash to the signed SHA-256 |
| `bad_image` | the bytes hash correctly but are not a bootable image |
| `flash_error` | the slot refused to open, write or switch, or the handover record would not write |
| `on_trial` | the running image went on trial before the press (a safeguard; BEGIN already refuses) |

### Data writes

Each write on the data characteristic is a 4-byte little-endian offset, then
1 to `chunk` image bytes. The dish takes a write only when it is receiving
and the offset is exactly how much it has written so far (`at`). It drops
any other write, so a duplicate, an out-of-order write and a write after a
lost one all leave the image untouched.

### Events

```
! OTA at=4096
! OTA at=4096 gap
! OTA state=receiving at=0
! OTA state=ready
! OTA state=idle last=bad_hash
```

The dish sends `at=` each time the bytes written pass a multiple of 4096 (with
240-byte writes the first is `at=4320`), and once more at the end. It sends `gap` (at most
every 200 ms) when it drops a write whose offset is ahead of `at`, so the page
can go back to `at` at once. It sends `state=` on each change of state. An
event can be lost, so the page polls `OTA STATUS` whenever it has heard
nothing for 2 s.

### Other users of the radio

`Session::active()` is true from BEGIN until the session ends. Anything else
that wants the radio (a duel over ESP-NOW, one day) waits while it is true, and
also while `hw::Trial::onTrial()` is true. In the other direction, an update
refuses to start while `DishOtaHost::busy()` is true, so a duel in progress
belongs there. The session, the trial and the busy check all run on the loop
task, so these plain reads need no lock.

### OTA CONFIRM

```
#a OTA CONFIRM <token>
#a OK trial=1
```

CONFIRM is the half of the trial that the dish cannot check by itself: that
Bluetooth works from end to end. On trial, the token must be the one the old
image recorded when it switched (`ERR 403 NOT_YOURS` otherwise). Off trial it
answers `OK trial=0` and changes nothing. It is safe to repeat, and the page
sends it whenever it sees `trial=1`: after its own restart, when BEGIN answers
`ERR 409 TRIAL`, and whenever it connects (a reload, a phone that was locked,
a family that comes back at minute 3).

### OTA ABORT and OTA REBOOT

```
#8 OTA ABORT <token>
#8 OK state=idle
#9 OTA REBOOT <token>
#9 OK rebooting
```

ABORT ends any session and is safe to repeat; with no session open it answers
`OK state=idle` whatever the token. REBOOT works only in `ready`
(`ERR 409 NOT_READY` otherwise). Both need the session's token. About 300 ms after REBOOT, the dish writes a save, records
which pet it is handing over, and restarts. If the save fails, it tries once
more. If that fails too, it discards the update, keeps the old image as the
one that boots, and ends with `last=save_failed`. If no REBOOT comes, the dish
restarts by itself 20 s after it became ready. Either way, it waits while it
is busy.

### The page's side

1. `OTA STATUS`. A 404 means that the dish needs a cable flash first. Read
   `board` and `fw`.
2. Fetch `/fw/<board>/manifest.json`. If its version is not newer, the dish
   is up to date.
3. Fetch the image and check its length and SHA-256 against the manifest.
4. `OTA BEGIN` with the manifest's fields. Ask the child to press the button,
   and poll STATUS each second until `receiving`.
5. Send writes from `at`. Keep at most 6 KB unacknowledged past the last
   `at=` heard. On `gap`, go back to its `at`. After 2 s with no progress,
   poll STATUS and go back to its `at`.
6. On `state=ready` (or a STATUS that says so), send `OTA REBOOT`.
7. Reconnect, and read HELLO's `fw`. If it is the old version, the new image
   rolled back and the pet is as it was.
8. If it is the new version, send `OTA CONFIRM <token>`, then poll STATUS
   every 2 s until `trial=0`: the new image has kept itself. The page never
   says "Updated" while `trial=1`; if its wait runs out first, it says that
   the new version is still checking itself. A drop during the trial is
   a rollback restart, so the page reconnects and reads HELLO again.

The chooser opens once per dish. With no `?d=` link it lists a device only
if it advertises the UART service and its name starts with `blorb-` (up to
0.6.x) or a species from `species.def` (0.7.0 on); "Search wider" drops the
name. An update renames the dish (`blorb-1b3c` comes back as `mossby`), and
nothing after the chooser depends on the name: every reconnect is
`gatt.connect()` on the same `BluetoothDevice`, and update tokens are stored
under its `id`. "Connect another dish" disconnects this one and opens the
chooser again in the same tap.

A dropped connection before the restart means reconnecting (the same device,
so no chooser) and sending the same BEGIN. The dish answers with where it is.
`web/public/ota.js` is this list as a state machine; `ON_DROP` there says what
a drop means in each state.

## Release layout

```
web/public/fw/<board>/manifest.json
web/public/fw/<board>/blorbarium-<board>-<version>.bin
```

```json
{"board":"lcd128","version":"0.4.0","flags":0,"size":989184,
 "sha256":"<base64url>","sig":"<base64url>",
 "image":"blorbarium-lcd128-0.4.0.bin","fmt":1}
```

The manifest is published with the site. The image is the app image
(`firmware.bin`), not the merged `.hex`, because it goes into an app slot.
The page fetches `image` relative to the manifest. `fmt` is the save format
the image writes. It is signed (it is part of the `blorbota2` text) and carried
in BEGIN, so a manifest cannot claim another format for the same image. The
release tool also reads it to catch a format change.

The manifest is committed, as the record of the last release. The image is
not committed (`.gitignore` refuses `web/public/fw/**/*.bin`).
`tools/ota_release.py release` writes it, so a deploy follows a release from
the same tree.

## Offline

After one visit the site works with no network, update included.
`web/public/sw.js` is a service worker:

- **The page's files.** `SHELL` lists every file in `web/public` except
  `sw.js` and `fw/`, and `web/test/offline.test.mjs` fails when a new file is
  not in it. They are kept at install and fetched network first, falling back
  to the kept copy after 4 s.
- **The release.** Each `fw/<board>/` release is kept as a pair in its own
  cache: the image the manifest names, checked against the manifest's size and
  SHA-256, and then the manifest. A cache without a manifest is an unfinished
  download and is never served. The page always gets the kept pair's
  manifest, so the image it asks for next is the one beside it. Online, the
  worker first keeps the site's release. A new pair replaces the old one only
  once it is whole, so a deploy caught between manifest and image keeps
  serving the old pair. The release is kept at install and on every
  page load, so opening the site once before a flight is enough.
- **Install.** `manifest.webmanifest` makes it installable from Chrome.

Google Fonts are kept the first time a controlled page loads them; until then,
offline uses the system fonts.

## Signing

The signature is ECDSA P-256 with SHA-256 over this exact ASCII line:

```
blorbota2 <board> <version> <flags> <fmt> <size> <sha256 base64url>
```

It is sent as raw `r || s` (64 bytes). The hash binds the image, so the dish
checks the signature once, at BEGIN, before it asks for the button. At the end
it checks that the bytes it received hash to the signed value.

Flags: bit 0 allows an image that is not newer. A downgrade is a signed
decision, made on purpose with `tools/ota_release.py --allow-older`.

## The trial

`verifyRollbackLater()` returns true, so the Arduino core leaves a new image
in `ESP_OTA_IMG_PENDING_VERIFY`. It is defined `extern "C"` in
`src/hw/ota_esp.h`. The core declares it in a C file, so a C++ definition
would be a different symbol. The core's weak `return false` would then win,
and every new image would be marked valid at boot without a word. The built
ELF shows it as a strong `T verifyRollbackLater`. The core's bootloader is built with
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. If an image is still pending when the
dish next resets, the bootloader marks it aborted and boots the old slot.

The old image records which pet it is handing over (generation and genome
hash) in the default `nvs` partition, under namespace `ota`. It writes the
record once when the image is verified, before it switches slots, so a power
cut after the switch still leaves a record. It writes it again just before the
restart, after the last save, because the pet may have moved on in between. A
stale record can only cause a rollback, never a wrong keep. The record holds
no occupant kind, because the boot's catch-up can hatch the egg that was
handed over, and that is still the same pet.

The new image keeps itself (`esp_ota_mark_app_valid_cancel_rollback`) only
when all of these hold:

- The keepsake loaded as `Resumed`, the record exists, and the pet matches it.
- A page sent `OTA CONFIRM` over Bluetooth.
- The loop has run for 30 s.
- The panel driver's `init()` returned true. This does not show that the
  screen is lit: the 1.28's panel cannot be read back. It only catches a
  driver that failed outright.

It rolls back (`esp_ota_mark_app_invalid_rollback_and_reboot`) at once when the
pet, the record or the resume is wrong, and after 5 minutes with no CONFIRM.
A hang longer than 20 s during the trial restarts the dish, which also rolls
it back.

While the image is on trial, `rename`, `gene_edit`, `pick`, `teach` and
`unteach` twists answer `ERR 409 TRIAL` (`ota::TrialGuard`), because nothing
they change could be written; the page waits for `trial=0` before it offers a
rename. `say` writes nothing and stays allowed.

The Dish runs on `ota::HeldStorage`
(`lib/ota/include/ota/held_storage.h`). It reads the real storage and refuses
every write, so the keepsake and `lineage.log` stay exactly as the old image
left them. A rollback therefore always finds the save that the old image can
read, whatever format the new one would write, and an image that boots Fresh
or from the lineage writes no new egg over the pet. When the trial keeps the
image, the hold ends and the Dish saves at once. Two costs come with this:
- Anything the pet does during the trial (at most about 5 minutes) is saved
  only when the image keeps itself.
- A lineage entry made during the trial (a death, a birth, a rename) is
  dropped, because the lineage log takes no late appends.

The hold starts before the Dish, at provisioning (`blorb::provision(...,
hold)` in `lib/blorb/src/provision.cpp`, called by `NvsFsStorage::begin`). An
image on trial opens `pet` and `petfs` read-only and calls neither
`writable()` nor `format()`. A volume it would have formatted (blank, or
another firmware's) is reported `HELD`, not formatted. The board then runs
sealed, the image is not `Resumed`, and the trial rolls back to the image
that owns the flash. When the trial keeps the image,
`NvsFsStorage::releaseHold()` opens for writing what was opened read-only.

A sealed board (a keepsake left alone, or one saved by newer firmware) may
still be updated, through the same signed, confirmed path. A newer firmware is
how a `sealed=newer` pet wakes. The old image never read that pet, so its
handover names no pet, and the new image keeps itself if it resumes any pet.
If it is sealed too, it is not `Resumed` and rolls back. The sealed keepsake is
untouched either way: sealed writes nothing, and the hold refuses all writes.

A held store reports itself not `writable()`, so `Keepsake::save` returns
before it encodes the 9.5 KB snapshot. An urgent save waiting for the hold
costs nothing each tick.

This implements the last clause of unit 22 in `docs/design/DESIGN.md`. The Dish
itself is unchanged.

When the old image boots and finds a handover record but no trial, the
update it switched to never kept itself. It records that version as
`rolledback` (nvs key `rb`), clears the record, and logs
`[hw] ota: the last update rolled back`. If `esp_ota` cannot roll back (no
other valid slot to boot), the running image is the only one that boots. The
trial marks it valid (`esp_ota_mark_app_valid_cancel_rollback`), so the next
reset does not find it still pending with nowhere to go. It logs
`rollback FAILED: no other image; keeping this one` once, releases the hold
and lets the pet carry on.

An update that ends without a restart (an ABORT when ready, a slot that will
not switch, a save that fails twice) forgets the handover record it wrote.
Otherwise the next ordinary boot would report the version as `rolledback`.

The verdict is `ota::judge` in `lib/ota/include/ota/trial.h`, a pure function
that the native suite tests case by case. `hw::Trial` in `src/hw/ota_esp.h`
gathers the facts and acts on the verdict.

## Save formats

An image on trial writes nothing, so the old image never meets a save it
cannot read. Once a new image keeps itself it writes its own format, and
there is no going back by update: an older image signed with `--allow-older`
finds a newer keepsake, boots `ReadOnlyNewer`, is not `Resumed`, and rolls
back. `tools/ota_release.py release` refuses an image whose save format
differs from the last manifest's `fmt`, so that a format change is made on
purpose. Pass `--format-change` once the new format's `Keepsake::migrate` path
and its fixture are in place.

## The first update is a cable flash

Dishes that run 0.3.0 (`flash3`) have no updater. `OTA STATUS` answers 404, and
the page says that the dish needs one update over a cable. Flash 0.4.0 or later
once by cable (`docs/hardware-testing.md`). After that, updates go over the
air. A merged `.hex` written by cable also rewrites `otadata` to boot `app0`,
so a cable flash always takes priority over an update.

## Releasing

1. Bump `kFirmwareVersion` in `lib/blorb/include/blorb/protocol.h`. The dish
   refuses an image that is not newer than itself.
2. Commit, then release from the clean tree:

   ```
   uv run --with cryptography python tools/ota_release.py release
   ```

   The tool refuses a tree with uncommitted changes, so the image is a
   commit's. It builds `badge128` and checks the following:
   - `src/hw/ota_pubkey.h` holds this key's public half.
   - The image's own stamp (`kReleaseStamp` in `src/main.cpp`: version,
     board, save format and commit) agrees with `protocol.h`, the board and
     `git describe --always --dirty`. An image built from another commit is
     refused, `--no-build` included.
   - The save format matches the `fmt` of the last manifest as committed
     (`git show HEAD:...`), not whatever the working tree holds (see "Save
     formats").

   Then it signs the image, with the save format inside the signed text,
   writes `web/public/fw/lcd128/manifest.json` and the image, and verifies the
   result. `--no-build` signs the `firmware.bin` that is already built.
   `--allow-older` signs flag 1, for a deliberate downgrade.
   Run it again before committing and it puts its own last manifest back
   first. Any other uncommitted change is still refused.
3. Commit the manifest, and deploy the site from the same tree. Deploys are
   the owner's call. The image is not committed.

The build maps the build machine's paths out of the image
(`tools/strip_paths.py`, `-ffile-prefix-map`), so a public image names no
user's home directory.

To verify a published release against the key built into the tree, run
`tools/ota_release.py check web/public/fw/lcd128/manifest.json`.

A signed release is public, so anyone in range can replay it. For a newer
image this is harmless: the dish still asks for its button, and the image is
the owner's. A release signed with `--allow-older` can be installed on any
newer dish for as long as someone has a copy. Publish one only when a
downgrade is the point, and replace it soon after.

## The key

`tools/ota_release.py keygen` makes a P-256 key at
`~/.config/blorbarium/ota-signing-key.pem` (or the path in `--key`), never
inside the repo. It writes the public half to `src/hw/ota_pubkey.h`.
`.gitignore` also refuses `*signing-key*`. If the key already exists,
`keygen` keeps it and rewrites only the header.

The key in `src/hw/ota_pubkey.h` was made on 2026-10-13 on the desk machine,
by the session that built this feature. **Back it up somewhere safe.** Every
dish built from this tree trusts only that key. If the key is lost, the next
update to every dish goes over a cable, with firmware built against a new key.
If the key leaks, its holder can make images that the dishes accept, although
each install still needs a hand on the button.

On Windows the file's 0600 mode is not enforced, so the key is only as private
as the user's home directory.

## Code map

| What | Where |
|---|---|
| The session (states, checks, resume) | `lib/ota/include/ota/session.h`, `lib/ota/src/session.cpp` |
| SHA-256 and P-256 verify (mbedtls on the dish, OpenSSL natively) | `lib/ota/src/crypto.cpp` |
| The trial verdict | `lib/ota/include/ota/trial.h` |
| The storage hold while on trial | `lib/ota/include/ota/held_storage.h` |
| Lineage twists refused while on trial | `lib/ota/include/ota/trial_guard.h`, `lib/ota/src/trial_guard.cpp` |
| The trial's esp_ota and nvs half, and its watchdog | `src/hw/ota_esp.h` (`EspTrialPort`, `TrialWatchdog`) |
| Lines tagged by connection | `lib/blorb/include/blorb/rx_lines.h` |
| The screen | `lib/ota/src/screen.cpp` |
| The slot, packet ring, data characteristic and trial glue | `src/hw/ota_esp.h` |
| Wiring: busy, save, handover, button, screen, restart, release stamp | `src/main.cpp` (`DishOtaHost`, `readBody`, `restartIntoUpdate`, `kReleaseStamp`) |
| The public key | `src/hw/ota_pubkey.h` |
| Signing and releasing | `tools/ota_release.py` |
| Build paths kept out of the image | `tools/strip_paths.py` |
| The page's sender | `web/public/ota.js` |
| The mock dish for `?mock=1` | `web/public/ota-mock.js` |
| Native tests | `test/test_ota` |
| Page tests | `web/test/ota.test.mjs` (node), `web/test/ota_page.py` (Playwright) |

## Tests

```
wsl -d survivor --exec bash -lc 'cd /mnt/d/Projects/_wt/blorb-ota && bash tools/wsl_test.sh test_ota'
node --test web/test/*.test.mjs
uv run --with playwright==1.63.0 python web/test/ota_page.py
uv run --with playwright==1.63.0 python web/test/offline_page.py
```

`test_ota` signs its images with a fresh OpenSSL key on each run. It covers:

- Every BEGIN refusal: the wrong board, a bad signature, another key, changed
  signed fields, an image that is not newer, an image that is too big,
  malformed arguments, a busy pet, an image on trial, and a second signature
  check within 2 s. The cheap refusals come before the signature check.
- The button: no press within 30 s, a press in the first 1.5 s, data before
  the press, the save before the slot opens, a failed save, and a pet that
  becomes busy or an image that goes on trial before the press.
- Ownership: another connection's data, ABORT and REBOOT while the owner is
  up, and a resume that moves the session to the new connection.
- The switch: the handover written before `activate()`, a failed handover,
  `activate()` never called for a bad hash, and a restart whose save fails
  once, or twice.
- Bad bytes: a corrupted image, an image the slot rejects, an overflow, a
  truncated image that times out, and a flash write error.
- Order and loss: repeated writes, writes after a missing one (with the `gap`
  event and its rate limit), and out-of-order writes.
- Drops: a resume with the same BEGIN, a different image that waits for ABORT,
  a lost ready event that STATUS and BEGIN still answer, the restart by itself,
  and the restart that waits while the pet is busy.
- The trial verdicts: keep after CONFIRM and 30 s, wait for CONFIRM, roll
  back with no CONFIRM in 5 minutes, and roll back for another pet, no
  record, no resume or a failed panel driver. A real Dish on trial lives
  6 minutes through TIME and a rename, and the disk stays byte-identical until
  the hold ends. A Dish on trial that boots Fresh writes nothing.
- The release tool's own signature (`test/support/ota_fixture.h`, written by
  `tools/ota_release.py fixture`).

Each of these deliberate breaks made tests fail (the count in brackets).
From the first build:
- Skipping the signature check (3).
- Taking writes at any offset (2).
- Skipping the hash check (1).
- Ignoring busy at BEGIN (1).
- Never letting the question lapse (1).
- Asking again on a resume (2).

From the review's fixes:
- BEGIN on trial (1), and the press on trial (1).
- Keeping with no handover record (1).
- No handover before `activate()` (2).
- Keeping without CONFIRM (1), and never rolling back without one (1).
- Data from any connection (2).
- ABORT from any connection (1).
- A resume that does not rebind (1).
- A press too soon (1).
- No verify rate limit (1).
- A single save at restart (1).
- A restart after a failed save (1).
- Writes through the hold (2), and appends through it (2).

From the second review:
- A resume, an ABORT or a REBOOT by any token (1 each).
- Data from any connection (2), and a resume that keeps the old connection (1).
- A CONFIRM by any token (1).
- A rollback not recorded (1), `rolledback` kept after a later update keeps itself (1), and STATUS hiding it (1).
- A rollback that cannot happen, retried every loop (1).
- The trial guard switched off for `rename` (1).
- A keepsake encode while held (1), and a rename through the hold (1).
- A reconnect that keeps the last connection's bytes (1), or its half line (1); these are in `test_rx_lines`.
- `fmt` left out of the signed text (1).

The page's own breaks are in its tests' history: CONFIRM without the stored
token, no CONFIRM on connect, BEGIN `TRIAL` treated as a failure, "Updated"
at the end of the wait while `trial=1`, `rolledback` ignored, and rename left
enabled during a trial. Each failed a test.

The page's tests run the whole flow against the mock dish. The cases are a
drop at about 40%, three lost writes, a lost `at=`, no press, a corrupted byte,
and a new image that rolls back during its trial. The mock rolls back an image
that no page CONFIRMs, so a page that never sends CONFIRM fails the first
case. Two cases run the page's real Bluetooth code against
`web/test/fake_bluetooth.js`, a fake `navigator.bluetooth` over the mock dish:
a `blorb-1b3c` dish that updates and comes back as `mossby` with the chooser
opened once, and a second dish after the first. `chooser.test.mjs` checks the
filters against `species.def`.

`offline_page.py` visits once, then sets the browser offline and shuts down
the server: the mock update, a real release over the fake radio, and a deploy
caught halfway that keeps serving the old pair until the new image is up.

## Proving it on the test board

This procedure proves an update end to end on the test board. It covers one
update that keeps itself and one that must roll back. Only the test board may
be flashed (COM11, USB serial `5C94095006`, MAC `d4:05:92:77:1b:34`; see
`docs/hardware-testing.md`). The sender is the real one: the page on the
owner's Android phone. The desk PC has no Bluetooth adapter. The page is
served from this PC with `wrangler dev`, and the phone reaches it over USB
through `adb reverse`. Chrome treats `http://localhost` as a secure context,
so Web Bluetooth works there. The production site is https://blorb.hypnodroid.com
(`cd web && npx -y wrangler@4.149.0 deploy`), first deployed 2026-10-14 with 0.5.0.

Only one program can hold COM11 at a time. Run `serial_log.py` and `shot.py`
one after the other, never together.

### What the owner does

1. Plug the Android phone into this PC by USB, with USB debugging on. Accept
   the "Allow USB debugging" prompt once.
2. When the agent says so: on the phone, open Chrome at
   `http://localhost:8787/?d=blorb-1b34&debug=1`, tap **Connect**, pick
   `blorb-1b34` in the chooser, and tap **Update**. Keep the phone within a
   metre of the board until the page gives a result. This happens twice,
   once in step 5 and once in step 8.

Nothing else needs a hand. The agent presses the board's button with
`DEBUG press`.

### What the agent does

All commands run from the root of a checkout of `ota` (or of the branch it
merged into) on this PC. `E` and the daemon hold come from
`docs/hardware-testing.md` sections 1 and 2.

1. **Hold off the badge daemon** (hardware-testing section 1). Check that a
   full-flash backup of the test board exists (section 2).

2. **Cable-flash the updater, 0.4.0.**

       ~/.platformio/penv/Scripts/pio.exe run -e badge128
       tools/flash_board.sh COM11 5C94095006 d4:05:92:77:1b:34 .pio/build/badge128/merged.hex
       uv run tools/serial_log.py --port COM11 --seconds 20 --reset --send "#1 HELLO" --send "#2 OTA STATUS" > ota-0-boot.log

   Expect `[hw] blorbarium ... fw=0.4.0 fmt=1 lcd128`, `[hw] ble name=blorb-1b34`,
   `#1 OK fw=0.4.0 ...` and
   `#2 ERR 404 UNKNOWN_VERB`: updates are refused on the cable. There is no
   `ota trial` line, because a cable flash is not on trial. Note the
   `boot=... genome=<hash>` line. That is the pet the update must keep.

3. **Make release 0.4.1.** The release tool takes only a clean tree, so the
   test versions live on a throwaway branch. On it, set `kFirmwareVersion` to
   `"0.4.1"` in `lib/blorb/include/blorb/protocol.h`, then commit and release:

       git switch -c ota-proof
       git commit -am "0.4.1, for the board proof only" && git push -u origin HEAD
       uv run --with cryptography python tools/ota_release.py release

   This needs the signing key, `~/.config/blorbarium/ota-signing-key.pem`. It
   ends with `ok: lcd128 0.4.1 verifies against src/hw/ota_pubkey.h`.

4. **Serve the page and bridge it to the phone.**

       (cd web && npx -y wrangler@4.149.0 dev --port 8787 --ip 127.0.0.1) &
       adb reverse tcp:8787 tcp:8787
       curl -s http://127.0.0.1:8787/fw/lcd128/manifest.json

   The manifest must say `0.4.1`.

5. **Update to 0.4.1.** Ask the owner for owner step 2. When the phone says
   "Press the button on the dish to say yes", take a picture of the question,
   then press the button and log everything that follows:

       uv run tools/shot.py --port COM11 --out ota-1-asking.png --scale 2
       uv run tools/serial_log.py --port COM11 --seconds 600 --send "DEBUG press" > ota-1-run.log

   The question lasts 30 s. If the shot takes too long, skip it and press
   first. A lapsed question shows on the phone as "Nobody pressed the button";
   tap Update again.

   Expect these lines in `ota-1-run.log`:
   - `[hw] up=... heap=...` lines through the transfer. These are the heap
     during an update.
   - `[hw] ota: restarting into the update, save ok`.
   - The new boot: `boot=Resumed ... genome=<the same hash as step 2>`.
   - `[hw] ota trial: on trial, panel=1 resumed=1 handover=same pet`.
     **If this line is missing, stop.** The new image is not on trial, so
     nothing that follows proves the trial: the rollback hook did not hold, or
     the image never switched.
   - `[hw] ota trial: a page reached it over Bluetooth`, when the phone
     reconnects and sends `OTA CONFIRM <token>`.
   - `[hw] ota trial: kept`, once the image has run 30 s and the page has
     confirmed. Nothing is written to `pet` or `petfs` before this line, and
     the `writes=` count in the `[hw] up=` lines stays flat until it.

   The phone shows "Checking that the new version works…" (the rename form
   stays disabled meanwhile) and ends with "Updated to 0.4.1. Grungo is
   fine." The debug log on the phone timestamps
   the first data write and `state=ready`. The difference is the transfer
   time for the size in the manifest.

6. **Verify.**

       uv run tools/serial_log.py --port COM11 --seconds 5 --send "#3 HELLO"
       uv run tools/shot.py --port COM11 --out ota-1-after.png --scale 2

   Expect `fw=0.4.1` and the same pet on screen.

7. **Make release 0.4.2, which must roll back.** On `ota-proof`, set
   `kFirmwareVersion` to `"0.4.2"`, commit, and release a build that fails its
   trial:

       git commit -am "0.4.2, for the board proof only: built to roll back" && git push -u origin HEAD
       PLATFORMIO_BUILD_FLAGS=-DBLORB_OTA_FAIL_TRIAL uv run --with cryptography python tools/ota_release.py release

   `BLORB_OTA_FAIL_TRIAL` makes the image report its panel driver down, so its
   trial rolls it back at once, before any page can confirm it. It exists only
   for this test.

8. **Update to 0.4.2.** Repeat step 5, logging to `ota-2-run.log`. Expect:
   - `[hw] ota: restarting into the update, save ok`.
   - The 0.4.2 boot: `[hw] ota trial: on trial, panel=0 ...`, then
     `[hw] ota trial: FAILED (checks or no page), rolling back`.
   - Another boot, `[hw] blorbarium ... fw=0.4.1`, with `boot=Resumed`, the
     same genome, `[hw] ota: the last update rolled back`, and no `ota trial`
     line.

   The phone ends with "The update didn't take, so the dish went back to
   0.4.1. Grungo is fine." Step 6's HELLO says `fw=0.4.1`. When the page
   connects again, the Update section says "Last time, 0.4.2 didn't work on
   this dish, so it went back to 0.4.1" and offers "Try again" rather than a
   plain update.

9. **Optional: a page that comes back late, and one that never does.** Release
   a 0.4.3 the same way as 0.4.1 (no flag). Ask the owner to tap Update and to
   close the tab as soon as the phone says "Restarting the dish", then to open
   the page again about 3 minutes later. Expect `on trial`, then `a page
   reached it` when the page reopens, then `kept`. Repeat with a 0.4.4, and
   this time the owner does not reopen it. Expect `on trial`, no `a page
   reached it`, about 5 minutes later `FAILED (checks or no page)`, and a boot
   back to `fw=0.4.3` with `the last update rolled back`.

10. **Clean up.**

       adb reverse --remove tcp:8787
       rm -f web/public/fw/lcd128/*.bin
       git checkout -- web/public/fw
       git switch -

   Stop `wrangler dev`. It leaves a `workerd.exe` behind, so stop the node
   process tree that runs `wrangler ... dev --port 8787`. Then end the daemon
   hold. The board stays on 0.4.1. A cable flash of any build puts it back.
   The `ota-proof` branch is never merged.

Keep the logs and the PNGs, and note the transfer time and the heap during
the transfer. Those are the numbers this page still marks unproven.

This procedure does not cover a crash or a hang during the trial. Those
restart while the image is still pending, which the bootloader rolls back the
same way. A bad signature, the wrong board, and a corrupted image are proven
natively and are not repeated on the board.

## What is proven, and what is not

Proven without a board:

- Every path of the session, natively, with real P-256 signatures.
- The dish's verifier, through its OpenSSL twin, accepts the real 0.4.0 release
  that `tools/ota_release.py` signed (988,144 bytes). It refuses the same
  release with one signature bit changed.
- The firmware compiles for `badge128`, and the rollback hook links as the
  strong C symbol.
- The page's flow, including a drop and a resume, against the mock dish in a
  real browser.
- The screen's three frames, rendered natively and checked by eye.
- The review's fixes, natively, each with a test that fails without it (see
  "Tests"), including a real Dish on trial that leaves the disk
  byte-identical.
- The release tool's refusals, each seen: a dirty tree, an image stamped
  0.4.0 against `protocol.h` at 0.4.9, and a save format of 1 against a last
  release of 2.
- A built image contains no `C:/Users` or project path. The image before the
  path fix carried 15 such strings.

**Not proven until the owner allows a flash:**

- Any transfer over a real radio: the throughput, how the 6 KB window behaves
  at weak signal, and whether Chrome on Android keeps up with
  write-without-response. The sibling project measured 6 to 9 KB/s on a
  similar link. At that rate, a 1 MB image takes two to three minutes.
- The mbedtls half of the verifier on the ESP32. It is compiled but has not
  run.
- `esp_ota_*` on this board: the sequential-erase writes, the image check in
  `esp_ota_end`, and the switch of slots.
- A real rollback. The bootloader is configured for it and the hook links
  correctly, but no rollback has yet been seen on this board.
- The button question on the real panel, and the loop's frame rate while flash
  sectors erase.
- `OTA CONFIRM` from a real phone (and from a reopened page), the storage
  hold on the board, the `writes=` count staying flat through a trial, and
  `rolledback` after a real rollback.
- One central at a time on the board, and the stale-peer recovery: no second
  phone has tried to connect, and no stale peer has been seen since the fix.

Proven on the test board by a cable flash of a949710 (2026-10-13, no update
sent):

- It boots `boot=Resumed` with the same pet, and HELLO says `fw=0.4.0`.
- `OTA STATUS` over serial is `ERR 404 UNKNOWN_VERB`, as documented.
- The `otadata` entries read `ota_state=0xFFFFFFFF` (undefined), so a
  cable-flashed image is not on trial.
- The packet ring is in PSRAM: PSRAM free dropped by 7,888 B.

## Sizes

| Build | Static RAM | Flash |
|---|---|---|
| `flash3` + web, before this work | 163,232 B | 946,585 B |
| With updates, before merging main | 163,568 B (+336) | 986,757 B (+40,172) |
| With updates, main merged | 164,136 B | 987,777 B |
| With the review's fixes (961e900) | 164,168 B | 988,825 B |

Most of the added flash is the ECDSA and bignum code from mbedtls.

The heap at rest was measured on the test board, after a cable flash of
a949710, from the `[hw] up=` lines, with the same creature as main:

| Measure | main e47f271 | a949710 (with updates) |
|---|---|---|
| Free heap at rest | 135,580 B | 134,852 B (about 730 B less) |
| Lowest free heap (`minheap`, around a save) | | 130,648 B |
| Loop stack high-water mark | | 12,260 B free, flat |
| PSRAM free | | 7,888 B less: the packet ring |

The heap during an update is not measured yet. It is an estimate: a running
session adds a SHA-256 context and the OTA handle (well under 1 KB), and the
verify at BEGIN takes a few KB of short-lived bignum heap. Step 5 of the board
procedure records the real figure.
