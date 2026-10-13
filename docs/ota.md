# Updating the firmware over Bluetooth

The website can put a new firmware on a dish over Bluetooth, from Android
Chrome, with no cable and no Wi-Fi. These dishes belong to children, so the
design rests on four promises:

1. **The pet survives every outcome.** The pet lives in the `pet` NVS
   partition and on `petfs`. An update writes only the inactive app slot.
   A save is written before the transfer starts and again just before the
   restart.
2. **Only the project owner can make an image the dish accepts.** Every image
   is signed. The dish refuses an image whose signature, board or version is
   wrong before it writes a byte.
3. **Someone holding the dish says yes.** The dish installs nothing until its
   button is pressed while the screen asks. A stranger's phone in range can
   make the dish ask, and nothing more.
4. **A bad image undoes itself.** A new image runs on trial. It keeps itself
   only after it has shown that it resumes the same pet with the screen
   working. If it crashes, hangs or loses the pet, the bootloader goes back
   to the old image.

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
idle --BEGIN (signed, right board, newer, not busy)--> confirm
confirm --button pressed (not busy, save written)--> receiving
confirm --30 s with no press--> idle  last=no_confirm
receiving --last byte, hash and image check pass--> ready
receiving --5 min with no byte--> idle  last=timeout
receiving --ABORT, bad hash, bad image, flash error--> idle  last=<why>
ready --REBOOT, or 20 s--> restart into the new image (on trial)
```

A disconnect changes nothing. The session waits for the page to come back,
which is how a transfer resumes.

### OTA BEGIN

```
#5 OTA BEGIN lcd128 0.4.0 0 1180416 <sha256> <sig>
#5 OK state=confirm at=0 size=1180416 id=8c1f00a2 chunk=240
```

The arguments are the board, the version (`major.minor.patch`), the flags,
the image size in bytes, the image's SHA-256 and the signature. The hash and
the signature are base64url without padding (43 and 86 characters). The
release tool writes all six into the manifest, so the page copies them.

`id` is the first four bytes of the hash in hex. It names the image in every
later reply. `chunk` is the most image bytes one data write may carry: the
link's MTU, less 3 for ATT and 4 for the offset.

While a session is open, a BEGIN for its own image is not an error. The dish
answers with where that session stands (`state=confirm`, `state=receiving
at=<n>` or `state=ready`). This is the resume: after a dropped connection the
page sends its BEGIN again and carries on from `at`. Any other BEGIN while a
session is open is `ERR 409 OTA_ACTIVE` (send `OTA ABORT` first).

With no session open, the dish checks, in this order, and answers the first
that fails:

| Reply | When |
|---|---|
| `ERR 400 BAD_ARGS` | a field is missing or malformed |
| `ERR 403 BAD_SIGNATURE` | the signature does not verify against the key built into the dish |
| `ERR 403 WRONG_BOARD` | the image is for another board |
| `ERR 409 NOT_NEWER` | the version is not newer than the dish's, and flag 1 (allow older) is not set |
| `ERR 413 TOO_BIG` | the image is empty or bigger than the app slot |
| `ERR 409 BUSY` | the pet is doing something that must not be interrupted |

Busy today means a clutch is on the dish (its eggs are chosen with the same
button) or the `pet` partition will not open (no save could be written).
`DishOtaHost::busy()` in `src/main.cpp` is where a future battle, or anything
else he must finish, refuses too.

### Confirming

While the state is `confirm`, the screen asks for the button. The button
press goes to the update and not to the pet. On the press, the dish checks
that it is still not busy, writes a save, opens the inactive slot and moves to
`receiving`. Busy at the press is `idle last=busy`, and a failed save is
`idle last=save_failed`. A `DEBUG press` over the serial cable answers too: the
cable is a hand on the dish.

After 30 s with no press, the session ends with `last=no_confirm`.

### OTA STATUS

```
#6 OTA STATUS
#6 OK state=receiving at=40960 size=1180416 id=8c1f00a2 chunk=240
#7 OTA STATUS
#7 OK state=idle board=lcd128 fw=0.4.0 chunk=240 trial=0 last=none
```

STATUS answers in every state and changes nothing, so it is safe to repeat.
It is the truth about a transfer. Events (below) are only hints that save the
page a poll. In `idle`, `board` and `fw` say what an update would need,
`trial=1` says that this image is still on trial (it may yet roll back), and
`last` says how the last session ended:

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
| `flash_error` | the slot refused to open, write or switch |

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

### OTA ABORT and OTA REBOOT

```
#8 OTA ABORT
#8 OK state=idle
#9 OTA REBOOT
#9 OK rebooting
```

ABORT ends any session and is safe to repeat. REBOOT works only in `ready`
(`ERR 409 NOT_READY` otherwise). The dish writes a save and restarts about
300 ms later. If no REBOOT comes, the dish restarts by itself 20 s after it
became ready. Either way, it waits while it is busy.

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
8. If it is the new version, poll STATUS every 2 s until `trial=0`: the new
   image has kept itself. A drop during the trial is a rollback restart, so
   the page reconnects and reads HELLO again.

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
{"board":"lcd128","version":"0.4.0","flags":0,"size":1180416,
 "sha256":"<base64url>","sig":"<base64url>",
 "image":"blorbarium-lcd128-0.4.0.bin"}
```

The manifest is published with the site. The image is the app image
(`firmware.bin`), not the merged `.hex`, because it goes into an app slot.
The page fetches `image` relative to the manifest.

## Signing

The signature is ECDSA P-256 with SHA-256 over this exact ASCII line:

```
blorbota1 <board> <version> <flags> <size> <sha256 base64url>
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

Before the restart, the old image saves the pet and records which pet it
handed over (generation and genome hash) in the default `nvs` partition, under
namespace `ota`. It does not record the occupant kind, because the boot's
catch-up can hatch the egg that was handed over, and that is still the same
pet. The new image keeps itself
(`esp_ota_mark_app_valid_cancel_rollback`) only when all of these hold:

- The panel came up.
- The keepsake loaded as `Resumed`, and the pet matches the record.
- The loop has run for 30 s.

If the pet does not match, the new image calls
`esp_ota_mark_app_invalid_rollback_and_reboot`. A hang longer than 20 s
during the trial restarts the dish, which also rolls it back.

The verdict is `ota::judge` in `lib/ota/include/ota/trial.h`, a pure function
that the native suite tests case by case. `hw::Trial` in `src/hw/ota_esp.h`
gathers the facts and acts on the verdict.

Unit 22 of `docs/design/DESIGN.md` also asks that an image on trial never
write a save in a newer format, so that a rollback never meets a save it cannot
read. This work changes no format (`fmt=1` before and after), so nothing
enforces that rule yet. The first format change must make the Dish read-only
for newer formats while `hw::Trial::onTrial()` is true.

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

   The tool builds `badge128`. It checks that `src/hw/ota_pubkey.h` holds this
   key's public half, signs the image, writes
   `web/public/fw/lcd128/manifest.json` and the image, and verifies the result.
   `--no-build` signs the `firmware.bin` that is already built.
   `--allow-older` signs flag 1, for a deliberate downgrade.
3. Commit the manifest and the image, and deploy the site as usual.

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
| The screen | `lib/ota/src/screen.cpp` |
| The slot, packet ring, data characteristic and trial glue | `src/hw/ota_esp.h` |
| Wiring: busy, save, button, screen, restart | `src/main.cpp` (`DishOtaHost`, `readBody`, `restartIntoUpdate`) |
| The public key | `src/hw/ota_pubkey.h` |
| Signing and releasing | `tools/ota_release.py` |
| The page's sender | `web/public/ota.js` |
| The mock dish for `?mock=1` | `web/public/ota-mock.js` |
| Native tests | `test/test_ota` |
| Page tests | `web/test/ota.test.mjs` (node), `web/test/ota_page.py` (Playwright) |

## Tests

```
wsl -d survivor --exec bash -lc 'cd /mnt/d/Projects/_wt/blorb-ota && bash tools/wsl_test.sh test_ota'
node --test web/test/ota.test.mjs
uv run --with playwright==1.63.0 python web/test/ota_page.py
```

`test_ota` signs its images with a fresh OpenSSL key on each run. It covers:

- Every BEGIN refusal: the wrong board, a bad signature, another key, changed
  signed fields, an image that is not newer, an image that is too big,
  malformed arguments, and a busy pet.
- The button: no press within 30 s, data before the press, the save before the
  slot opens, a failed save, and a pet that becomes busy before the press.
- Bad bytes: a corrupted image, an image the slot rejects, an overflow, a
  truncated image that times out, and a flash write error.
- Order and loss: repeated writes, writes after a missing one (with the `gap`
  event and its rate limit), and out-of-order writes.
- Drops: a resume with the same BEGIN, a different image that waits for ABORT,
  a lost ready event that STATUS and BEGIN still answer, the restart by itself,
  and the restart that waits while the pet is busy.
- The trial verdicts: keep, wait, and roll back for another pet, a fall-back
  boot or no panel.
- The release tool's own signature (`test/support/ota_fixture.h`, written by
  `tools/ota_release.py fixture`).

Each of these deliberate breaks made the named number of tests fail:

- Skipping the signature check: 3 tests.
- Taking writes at any offset: 2 tests.
- Skipping the hash check: 1 test.
- Ignoring busy at BEGIN: 1 test.
- Never letting the question lapse: 1 test.
- Asking again on a resume: 2 tests.

The page's tests run the whole flow against the mock dish. The cases are a
drop at about 40%, three lost writes, a lost `at=`, no press, a corrupted byte,
and a new image that rolls back during its trial.

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
- The packet ring in PSRAM. If PSRAM does not allocate, the code falls back to
  the internal heap.

## Sizes

| Build | Static RAM | Flash |
|---|---|---|
| `flash3` + web, before this work | 163,232 B | 946,585 B |
| With updates, before merging main | 163,568 B (+336) | 986,757 B (+40,172) |
| With updates, main merged | 164,136 B | 987,777 B |

Most of the added flash is the ECDSA and bignum code from mbedtls. The heap at
runtime is an estimate, not a measurement:

- The OTA objects add about 0.3 KB of static RAM.
- The 7.9 KB packet ring is in PSRAM.
- A running session adds a SHA-256 context and the OTA handle, well under 1 KB.
- The verify at BEGIN takes a few KB of short-lived bignum heap.

So the 136,244 B free that `HANDOFF.md` measured should stay above 135 KB at
rest. The `[hw] up=... heap=` status line will show the real figure.
