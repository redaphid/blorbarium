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

The dish checks, in this order, and answers the first that fails:

| Reply | When |
|---|---|
| `ERR 400 BAD_ARGS` | a field is missing or malformed |
| `ERR 403 BAD_SIGNATURE` | the signature does not verify against the key built into the dish |
| `ERR 403 WRONG_BOARD` | the image is for another board |
| `ERR 409 NOT_NEWER` | the version is not newer than the dish's, and flag 1 (allow older) is not set |
| `ERR 413 TOO_BIG` | the image is empty or bigger than the app slot |
| `ERR 409 OTA_ACTIVE` | a different image is already in progress (send `OTA ABORT` first) |
| `ERR 409 BUSY` | the pet is doing something that must not be interrupted |

A BEGIN for the image already in progress is not an error. The dish answers
with where that session stands (`state=confirm`, `state=receiving at=<n>` or
`state=ready`). This is the resume: after a dropped connection the page sends
its BEGIN again and carries on from `at`.

### Confirming

While the state is `confirm`, the screen asks for the button. The button
press goes to the update and not to the pet. On the press, the dish checks
that it is still not busy, writes a save, opens the inactive slot and moves to
`receiving`. A failed save is `idle last=save_failed`.

After 30 s with no press, the session ends with `last=no_confirm`.

### OTA STATUS

```
#6 OTA STATUS
#6 OK state=receiving at=40960 size=1180416 id=8c1f00a2 chunk=240
#7 OTA STATUS
#7 OK state=idle board=lcd128 fw=0.4.0 chunk=240 last=none
```

STATUS answers in every state and changes nothing, so it is safe to repeat.
It is the truth about a transfer. Events (below) are only hints that save the
page a poll. In `idle`, `board` and `fw` say what an update would need, and
`last` says how the last session ended:

| `last` | Meaning |
|---|---|
| `none` | no session since boot |
| `no_confirm` | nobody pressed the button within 30 s |
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

The dish sends `at=` after every 4096 bytes written. It sends `gap` (at most
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
7. Reconnect, and read HELLO's `fw`. If it is the new version, the update
   worked. If it is the old version, the new image rolled back and the pet is
   as it was.

A dropped connection at any step means reconnecting (the same device, so no
chooser) and sending the same BEGIN. The dish answers with where it is.

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
in `ESP_OTA_IMG_PENDING_VERIFY`. The core's bootloader is built with
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. If an image is still pending when the
dish next resets, the bootloader marks it aborted and boots the old slot.

Before the restart, the old image records the pet it is handing over (the
occupant kind, generation and genome hash) in the default `nvs` partition,
under namespace `ota`. The new image keeps itself
(`esp_ota_mark_app_valid_cancel_rollback`) only when all of these hold:

- The panel came up.
- The keepsake loaded as `Resumed`, and the pet matches the record.
- The loop has run for 30 s.

If the pet does not match, the new image calls
`esp_ota_mark_app_invalid_rollback_and_reboot`. A hang longer than 20 s
during the trial restarts the dish, which also rolls it back.
