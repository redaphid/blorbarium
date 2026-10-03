# OTA over BLE for blorbarium: survey of cyber-puck and claude-notification-screen

Abbreviations: **CNS** = `D:\Projects\claude-notification-screen` (checkout on `main` @ b1b5e3e).
**FW** = `~/.platformio/packages/framework-arduinoespressif32` (v3.20017 = arduino-esp32 2.0.17, the core these builds use).
`git:<ref>:<path>:<line>` means the file as it exists on that branch or commit, not on disk.

## 0. The short version

- **There is only one OTA implementation, not two.** cyber-puck has no OTA code of its own. Its firmware is the CNS repo as a git submodule (`D:\Projects\cyber-puck\.gitmodules:1-3`), and that submodule is not checked out on disk. `scripts/flash.sh:27-33` checks out CNS branch `cyber-puck`, and that branch **does not contain `ble_ota.h`** (`git ls-tree origin/cyber-puck src/` has no ota file, and 196fbda is not an ancestor). cyber-puck's phone app has no Web Bluetooth code at all (no `requestDevice`/`gatt` in `phone/js/*`). So the "OTA examples in cyber-puck" are CNS's `src/ble_ota.h` plus `host/ble_link.py`.
- **CNS BLE OTA has never completed end to end.** The best run reached 95% or more at about 9 KB/s and then timed out waiting for the last ack (`git:origin/ota-proof-docs:docs/bluetooth-link.md:308-347`; `CLAUDE.md:336-341`; `HANDOFF.md:160-163`). Treat it as a good skeleton with known holes, not as proven code.
- **No web sender exists anywhere.** The only sender is Python/bleak (`host/ble_link.py:450-542`). The Web Bluetooth sender has to be written new.

## 1. How CNS does OTA, end to end

### Device: `CNS/src/ble_ota.h` (main)

| Aspect | What it does | Cite |
|---|---|---|
| Wiring | Included from `ble.h` and created on the same NimBLE server as NUS. Polled from `blePoll` on the Arduino loop. | `src/ble.h:141`, `:172`, `:219` |
| Service | Its own service `6E400010-…`, separate from the NUS UART because "a megabyte of binary does not belong in a line protocol, and the UART's 2K ring would drop most of it". | `ble_ota.h:5-7`, `:36` |
| CTRL char `6E400011` | WRITE + NOTIFY, text. Host sends `B <size> <sha256hex> <board>`, `E` (end) or `A` (abort). Device replies `OTA ready`, `OTA at <n>`, `OTA ok <slot>` or `OTA err <why>`. | `:9-10`, `:37`, `:116` |
| DATA char `6E400012` | WRITE_NR (write without response). Raw image bytes in order. | `:11`, `:38`, `:118` |
| Threading | NimBLE-task callbacks only copy bytes: DATA goes into a ring, CTRL into a one-slot command buffer. All flash work happens on the loop. | `:89-112` |
| Ring | 32 KB, PSRAM first and internal RAM as fallback. A full ring sets `otaOverrun`, and the loop then aborts with `overrun`. | `:46`, `:96`, `:141-143`, `:201` |
| MTU | `NimBLEDevice::setMTU(247)`, so 244-byte writes. The host asks bleak for `max_write_without_response_size`. | `ble.h:158`, `ble.h:121,131`; `host/ble_link.py:510` |
| Conn interval | Deliberately **not** shortened. Asking for 7.5-15 ms stalled on Windows at -97 dBm (missed "instant" parameter updates). | `ble_ota.h:159-163`; `docs/bluetooth-link.md:240-247` |
| Flow control | A window rather than per-packet acks. The device notifies `OTA at <n>` every 4096 bytes on flash and once at the end. The host keeps at most 24 KB unacked (`OTA_WINDOW`), which is below the 32 KB ring. | `ble_ota.h:13-17`, `:47`, `:213-216`; `ble_link.py:42`, `:515-516` |
| Begin | Board-name check, `esp_ota_get_next_update_partition`, size ≤ slot, then `esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, …)`, which erases sector by sector instead of freezing the loop for seconds. SHA-256 is started with mbedtls. | `:130-158`, `:138-140`, `:144-146`, `:151-152` |
| Write | The ring is drained in contiguous runs: `esp_ota_write` plus `mbedtls_sha256_update` on each run. More bytes than the declared size aborts with `too long`. | `:202-212` |
| End | Checks `written == size`, compares the streamed SHA-256 with the declared one, then `esp_ota_end` (IDF's own image validation), then `esp_ota_set_boot_partition`. Notifies `OTA ok <label>` and reboots 800 ms later. | `:167-182` |
| Liveness | 20 s with no data gives `stalled`. A disconnect aborts. The compare is signed; the unsigned version killed every OTA on its first pass (fixed in 196fbda). | `:49`, `:200`, `:217-221`; commit 196fbda message |
| Verification | Board name, size, SHA-256 (integrity only, sent by the same host, so **no authenticity**), and `esp_ota_end`'s image check. "OTA image signing stays out of scope, on purpose" (commit 5db6daf message). | `:19-24` |
| Rollback | `bool verifyRollbackLater(){return true;}` overrides the core's weak default, so the core does not auto-validate at boot. The app calls `esp_ota_mark_app_valid_cancel_rollback()` once uptime passes 15 s. A crash before then makes the bootloader revert. | `ble_ota.h:48`, `:51-53`, `:186-191`; FW `cores/esp32/esp32-hal-misc.c:207-208` (weak `false`), `:225-233` (auto-validate path) |
| Bootloader rollback support | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1` is in the core's prebuilt S3 sdkconfig, so rollback works without a custom bootloader. | FW `tools/sdk/esp32s3/dio_qspi/include/sdkconfig.h:27` (same line in the opi/qspi variants) |

**Better trial logic on an unmerged branch** (`flash-through-daemon`, b4b0574). A new image is marked valid only once it has run 15 s **and** a host has authenticated over the air on it, which proves the radio (the thing needed for the next update) works. If no host reaches it within `OTA_PROVE_MS` = 180 s, it calls `esp_ota_mark_app_invalid_rollback_and_reboot()`. It detects a trial boot with `esp_ota_get_state_partition(running) == ESP_OTA_IMG_PENDING_VERIFY`, and has `OTA_TEST_CRASH` / `OTA_TEST_NEVER_PROVE` test builds. Cites: `git:b4b0574:src/ble_ota.h:18-28`, `:52-53`, `:192-219`.

**Encrypted variant on an unmerged branch** (`ble-encrypt`, 5db6daf/80f53b0). LE Secure Connections Just Works. CTRL/DATA add `WRITE_ENC`, and `otaSay` stays silent on an unencrypted link. Cites: `git:origin/ble-encrypt:src/ble_ota.h:77,117,119`; `git:origin/ble-encrypt:src/ble.h:25-30`.

### Host: `CNS/host/ble_link.py` (Python/bleak, main)

`BleLink.ota(image, board)` at `:450-464` runs `_ota` at `:466-542` on the link's own asyncio loop:
1. Subscribe to CTRL. `on_ctrl` treats `OTA at n` as an ack and queues any other line (`:472-479`).
2. `B <len> <sha256> <board>` is written **with response**, then it waits 30 s for `OTA ready` (`:506-508`).
3. Chunk size is `max(20, max_write_without_response_size)`, which comes out at 244 (`:509-511`).
4. Loop: block while `sent - acked >= 24K`, then `write_gatt_char(DATA, chunk, response=False)` (`:514-519`). It logs progress every 128 KB (`:520-525`).
5. Wait until `acked == len`, write `E` with response, and wait for `OTA ok` (`:526-529`). On any exception it writes `A` (`:532-537`).

**The `ota-last-mile` branch (d4b39ba) fixes the 95% hang on the host side, with no firmware change.** Every 16 data writes (`OTA_PACE`) it writes a `P` on CTRL **with response**. The device ignores the command, but the round trip proves the queued writes have left, because bleak/CoreBluetooth does not wait for `canSendWriteWithoutResponse`. If the last ack never arrives it sends `E` anyway, and the device's own size and SHA check decides between `ok` and `short`. Cites: `git:origin/ota-last-mile:host/ble_link.py:44-49`, `:516-520`, `:529-530`, `:543`; commit d4b39ba message.

### Unfixed device-side hole

`otaSay` ignores `notify()`'s return value, so a final `OTA at` can be lost when NimBLE has no buffer (`ble_ota.h:76`; `git:origin/ota-proof-docs:docs/bluetooth-link.md:335-341`).

### Tests

`CNS/tests/test_ble_ota.py:37-59` compiles the real `ble_ota.h` against stubs (`tests/ble_ota/esp_ota_ops.h`, `tests/ble_ota/mbedtls/sha256.h`, `tests/ble_ota/poll.cpp`) and drives `otaPoll`. This harness is worth copying.

## 2. Partition tables

**CNS sets no `board_build.partitions`.** `platformio.ini:1-95` only sets `board_build.flash_size = 16MB` (`:47`). The board manifest's default therefore applies: `~/.platformio/platforms/espressif32/boards/esp32-s3-devkitc-1.json:5` is `"partitions": "default_8MB.csv"`. The built artifact confirms it: `gen_esp32part.py CNS/.pio/build/leader/partitions.bin` decodes to the same table. **The top 8 MB of the 16 MB flash is unused.** The table is FW `tools/partitions/default_8MB.csv:1-7`:

```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x330000,
app1,     app,  ota_1,   0x340000,0x330000,
spiffs,   data, spiffs,  0x670000,0x180000,
coredump, data, coredump,0x7F0000,0x10000,
```

For reference, FW `tools/partitions/default_16MB.csv:1-7` has the same shape with app0/app1 at 0x640000 each and spiffs at 0xc90000 (0x360000).

**What survives an OTA.** `esp_ota_*` writes only the inactive app slot and `otadata` (`ble_ota.h:139`, `:146`, `:178`). NVS, spiffs and coredump are never touched by OTA. The bootloader and the partition table **cannot** be changed by OTA, so the table shipped on the first cable flash is final.

**What does not survive, and the traps:**
- **Cable flashes.** A flat merged `.bin` pads over NVS with 0xFF and wiped the owner on every flash (`CNS/CLAUDE.md:218-224`; `merge_image.py:13-15`). Even the merged `.hex` is suspected of being flattened by esptool 4.7 (`CNS/HANDOFF.md:152-157`). A plain `pio run -t upload` writes only the bootloader, the table, `boot_app0` (to otadata) and the app.
- **The Arduino core auto-erases the first `data/nvs` partition.** On `ESP_ERR_NVS_NO_FREE_PAGES` or `ESP_ERR_NVS_NEW_VERSION_FOUND` (for example after an IDF/NVS format change) it erases that partition, whichever it is in table order (FW `cores/esp32/esp32-hal-misc.c:249-262`). A **separately labelled** NVS partition opened through `Preferences::begin(ns, ro, "label")` is never erased: `nvs_flash_init_partition` failing just returns `false` (FW `libraries/Preferences/src/Preferences.cpp:39-44`; `Preferences.h:32`). `LittleFS.begin()` defaults to `formatOnFail=false` (FW `libraries/LittleFS/src/LittleFS.h:27`). Keep it that way.
- CNS keeps its owner token in the default `nvs` partition, namespace `badge` (`src/ble.h:152-155`, `:336-339`). That is exactly the partition the core may wipe.

## 3. Throughput

**Measured** on the Mac (CoreBluetooth, bleak 3.0.2), badge beside the laptop, 244-byte writes, connection logged as `50 6000`. Progress reached 106 K at 7.8 KB/s, then 747 K at 83 s, about **9.0 KB/s** cumulative. It then timed out on the last ack (`git:origin/ota-proof-docs:docs/bluetooth-link.md:308-333`). Earlier runs:
- Windows at -97 dBm: one image ran 8.5 minutes and died (`docs/bluetooth-link.md:240-243`).
- BlueZ at close range: died immediately, which was the unsigned-compare bug (`docs/bluetooth-link.md:249-273`; commit 196fbda).

For comparison, the cable takes **9 s** for the same image (`git:origin/ota-proof-docs:docs/bluetooth-link.md:309`).

**A 1.5 MB image:** 1536 KB / 9 KB/s ≈ **170 s, about 3 minutes**. At the run's worst cumulative rate, 6.3 KB/s (`…:317`), it is about 4 minutes. CNS firmware is about 826 KB with radios (git log message "826KB of firmware with the radio, 406KB" at CNS history line ~590), so blorbarium may come in under 1.5 MB.

*Not measured, reasoned:* Chrome on Android negotiates the MTU itself, usually larger than 247. The device should report the usable chunk size in `OTA ready`, because JS cannot read the MTU. Measure before promising a number.

## 4. Gating

**CNS** allows flashing only when the badge is claimed (owner token in NVS) and the connection has sent `o <token>` (`ble_ota.h:26-30`, `:132-133`; `ble.h:316-320`). The token went over the air in the clear until `ble-encrypt` (commit 5db6daf message). blorbarium has no owner token (`blorbarium/HANDOFF.md`, "owner-token scheme … probably does not belong here").

**Recommended gate for blorbarium (no token).** Two independent checks: authenticity from a signature, and consent from the device itself.

1. **A signed manifest instead of a bare SHA.**
   - `B` carries a manifest of `{magic, board:"blorb-128", fw_version, save_schema_min, save_schema_max, size, sha256}` plus an ECDSA-P256 signature over it.
   - The public key is compiled into the firmware and the private key stays on the build machine. The core already has `CONFIG_MBEDTLS_ECDSA_C 1` and `CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED 1` (FW `tools/sdk/esp32s3/qio_qspi/include/sdkconfig.h:610,613`), so no new library is needed.
   - Verify the manifest at `B`, before `esp_ota_begin` erases anything. At `E`, keep CNS's streamed SHA compare, now checked against the *signed* hash.
   - Avoid IDF secure boot / signed-app-in-bootloader. It needs a custom bootloader and sdkconfig the prebuilt Arduino core does not have, and eFuse burns are irreversible.
2. **Physical confirmation on the device.**
   - After a valid `B`, the screen shows "update to vX? turn me over and hold" and the device answers `OTA ready` only after the gesture. Otherwise it says `OTA err not confirmed` after about 30 s.
   - Use an IMU gesture through the existing `orient.h` hold/flip/knock detectors. The board has no touch, and the BOOT button (GPIO0) may be buried in a printed case. BOOT can be a fallback if the case exposes it.
   - This replaces the token: a stranger in BLE range cannot flash without hands on the pet, and even with them cannot flash an unsigned build.
3. Refuse a manifest whose `save_schema_max` is below the schema already stored, so an old build cannot be flashed over a newer save. Skip BLE pairing/encryption. Just Works adds no authorization, and Android pairing prompts would confuse a non-programmer.

## 5. Concrete recommendation for blorbarium

### Copy as-is (from CNS `src/ble_ota.h` on main)

- The separate service with CTRL/DATA (`:5-11`, `:36-38`, `:114-119`). The UUIDs can stay or move to blorbarium's own base.
- The callback-copies, loop-writes split (`:89-112`).
- The PSRAM ring (`:141-143`).
- `OTA_WITH_SEQUENTIAL_WRITES` (`:144-146`).
- Streaming SHA-256 (`:151-152`, `:208`).
- The size, SHA and `esp_ota_end` checks, then `set_boot_partition` and a delayed restart (`:167-182`).
- The signed-millis stall detector (`:217-221`).
- Abort on disconnect (`:200`).
- `verifyRollbackLater(){return true;}` (`:53`).
- The trial logic from `git:b4b0574:src/ble_ota.h:192-219`: valid only after 15 s **and** the site has reconnected on the new image, otherwise roll back at 180 s.
- The test harness `tests/test_ble_ota.py` plus `tests/ble_ota/*`.

### Trim

- The owner/claim gate (`:132-133`), `bleAuthed`, and the `leader`/`badge` board names (`:40-44`).
- The `ble-encrypt` work.
- The `Serial.printf` (`:158`).

### Add

- The signed manifest and the physical confirmation (§4).
- `OTA ready <chunk>` reporting the usable write size.
- Check `notify()`'s return and retry the final `OTA at` (`:76`).
- Progress on screen, so the pet visibly "molts" instead of freezing.

### Web sender (new, JS)

Port `_ota` (`host/ble_link.py:466-542`) together with the `ota-last-mile` additions: a `P` with response every 16 writes, and `E` sent even if the last ack is lost (`git:origin/ota-last-mile:host/ble_link.py:44-49,529-530,543`). Specifically:
- Use `writeValueWithoutResponse` and await each one.
- Keep the 24 KB window against `OTA at`.
- After `OTA ok`, auto-reconnect, which is what "proves" the trial image.
- The site fetches the `.bin` together with its `.sig`/manifest from the same HTTPS origin.

### Partition table (16 MB). Ship it on the first cable flash; it can never change by OTA.

```
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,      0x9000,   0x5000,
otadata,  data, ota,      0xe000,   0x2000,
app0,     app,  ota_0,    0x10000,  0x400000,
app1,     app,  ota_1,    0x410000, 0x400000,
pet,      data, nvs,      0x810000, 0x40000,
petfs,    data, spiffs,   0x850000, 0x7A0000,
coredump, data, coredump, 0xFF0000, 0x10000,
```

- The two 4 MB app slots leave more than 2x headroom over a 1.5 MB image.
- `pet` is a separately labelled NVS partition, opened only through `Preferences.begin("blorb", ro, "pet")`. It is never first in the table, so the core's auto-erase (FW `esp32-hal-misc.c:249-262`) cannot hit it, and a labelled init that fails returns false instead of formatting (`Preferences.cpp:39-44`). NVS gives atomic per-key writes and wear levelling.
- `petfs` (LittleFS, `formatOnFail=false`) is reserved for a history journal, backups and future assets, sized now because it cannot be added later.
- The default `nvs` holds only disposable things (BLE/PHY data).
- Set it with `board_build.partitions = partitions.csv`. Never `erase_flash`. Cable-flash with `pio run -t upload` (per-image offsets), not a padded merged bin (CNS `CLAUDE.md:218-224`).

### Keeping the save readable across versions

- Each save is a record of `{magic, schema, seq, len, crc32}` followed by tagged fields. Tags are append-only, never reused or redefined. Unknown tags are skipped and carried along on rewrite.
- Write two keys alternately (`save_a`/`save_b`, highest valid `seq` wins). A torn or corrupt record falls back to the other one.
- New firmware must read every older schema and migrate in RAM.
- **While the image is still on trial** (`ESP_OTA_IMG_PENDING_VERIFY`, `git:b4b0574:src/ble_ota.h:193-198`), it must keep writing in the schema the previous firmware reads. Only after `mark_app_valid` may it write the new schema. Otherwise a rollback lands the old firmware on a save it cannot read.
- If the `pet` partition will not open (for example an NVS format change after a core upgrade), show "save unreadable", write nothing, and never create a fresh pet over it.
- Optional insurance: a BLE "export save" so the site can keep a copy of the keepsake.

## GitHub search

Fresh mirror of `redaphid/claude-notification-screen` (all heads, tags and `refs/pull/*`) at `scratchpad/cns-remote.git`, working tree at `scratchpad/cns-remote/`. Scope is limited to that repo, its PRs and issues, and the CNS commits that cyber-puck pins. Other redaphid repos were dropped at the user's request.

**Verdict: nothing on GitHub is better than the local `src/ble_ota.h`. No completed BLE OTA exists, and no Web Bluetooth OTA sender exists.** The remote holds exactly the material the local survey already found, and it is missing one piece that only the local clone has (see "Remote is missing" below).

### cyber-puck's pinned commits

- Local `D:\Projects\cyber-puck`, `.gitmodules:1-3`, has `claude-notification-screen` → `git@github.com:redaphid/claude-notification-screen.git`. HEAD 6a69890 pins **b1b5e3e** (`git ls-tree HEAD claude-notification-screen`). That is CNS `main`'s tip ("Merge pull request #47 … leader-on-battery", 2026-09-23). It exists on the remote and is the same commit as the local CNS checkout, so the submodule contributes nothing new. Its `src/ble_ota.h` is blob f6d207e, identical to the one already surveyed.
- Pins on cyber-puck's remote branches (via `gh api …/contents/claude-notification-screen?ref=`):
  - `main`, `wrestle-puck` and both `claude/*` branches pin b1b5e3e.
  - `qwen-sprite-turnaround` pins 61e18e9 (2026-09-17).
  - `web-flasher` pins 15119f7 (2026-09-13).
  - Both older pins exist on the CNS remote, are ancestors of CNS `main`, and predate OTA: neither has `src/ble_ota.h`.

### Every version of `src/ble_ota.h` on the remote

`git log --all -- src/ble_ota.h` lists only 118a999, 60f5768, 196fbda, 5db6daf and 80f53b0. There are four distinct blobs:

| Blob | Introduced by | Branches | What it is |
|---|---|---|---|
| 7d39057 | 118a999 (2026-09-19) | 17 branches, e.g. `flash-through-daemon`, `ask-before-flash`, several `worktree-agent-*` | Original, **with** the unsigned stall-compare bug that kills every OTA at begin |
| 3d8f970 | 60f5768 (2026-09-19) | only `worktree-agent-a2d2160b75ef9f0a2` (not in main) | 7d39057 plus a comment on why one line uses `Serial` (`:158-171`); no functional change |
| f6d207e | 196fbda (2026-09-21, PR #22) | `main` and 14 others, incl. `ota-last-mile` and `ota-proof-docs` | The surveyed version, with the signed stall compare (`:217-221`) |
| 461ab36 | 5db6daf / 80f53b0 | `ble-encrypt` (PR #38, open) | f6d207e plus `WRITE_ENC`, already covered in §1 |

`git log --all -S esp_ota` hits only 118a999 and 196fbda. `-S Update.begin` has no hits, so Arduino `Update`-library OTA was never used. `-i -G 'ota'` and `--grep` turn up no OTA commit that §1 does not already cover.

### Remote is missing: the trial/prove-or-rollback logic

**b4b0574 ("A new image is kept only once its owner reaches it over the air", 2026-09-19) is not on GitHub.** `git cat-file` fails in the mirror, and no remote ref (heads or `refs/pull/*`) contains `OTA_PROVE_MS` or `mark_app_invalid_rollback`. It exists only in the local clone, on the local `flash-through-daemon` branch (`D:\Projects\claude-notification-screen`, reflog `refs/heads/flash-through-daemon@{2}`). The remote `flash-through-daemon` ends at 1a10852 and was merged without it. §1 and §5 cite `git:b4b0574:src/ble_ota.h:18-28,52-53,192-219`; that code must be copied from the local clone, because it is unpublished and unreviewed.

### PRs and issues (`gh pr/issue list --state all --search ota`)

- **Issue #37, open**: "Over-the-air update streams to 95% on the Mac and then never gets its last ack" (https://github.com/redaphid/claude-notification-screen/issues/37). Its handoff comment (2026-09-21) says PR #22 is merged and that the air flash "streamed 747K of 786K at 7-9 KB/s, then hit `TimeoutError` waiting for the last ack". The remaining work it lists ends with "ONE air attempt with the badge off the cable", and that attempt has not been recorded. The proof criterion was `FW 1.1.1+ble.puck.always` still being reported 60 s or more after the reboot. Nothing since 2026-09-22.
- **PR #22, merged** (https://github.com/redaphid/claude-notification-screen/pull/22, `ota-proof`, 10ae6e4): the begin-pass stall fix. Its own tip commit is 9dced2c "The harness does not claim a hardware proof that has not happened".
- **PR #36, open** (https://github.com/redaphid/claude-notification-screen/pull/36, `ota-proof-docs` @ 5d1d048): the measurement write-up. `git:5d1d048:docs/bluetooth-link.md:316-321` logs 106K→747K of 786K at 7.8→9.0 KB/s with 244-byte writes. `:346` says "still not proven end to end, but it now fails at 95% or later".
- **PR #40, open draft, 0 comments, never reviewed** (https://github.com/redaphid/claude-notification-screen/pull/40, `ota-last-mile` @ 09e6eb2, code in d4b39ba): the host-side pacing fix covered in §1. It has **never been run on hardware**.
- **PR #38 / issue #31, open**: `ble-encrypt`, which touches the same characteristics.
- No PR or issue claims a completed OTA. `git grep` for "OTA ok", "update completed" and similar across all branches' `*.md` finds nothing.

**Throughput.** The best measured figure anywhere is about 9 KB/s cumulative (Mac/CoreBluetooth, 244-byte writes, 50 ms/6000 connection), the same number §3 gives. Nothing faster exists.

### Web senders in the repo (none do OTA)

- **Web Bluetooth control page, not OTA.** It is at `git:gh-pages:control.html:171` (`navigator.bluetooth.requestDevice({filters:[{services:[SVC]}]})`), `:174` (`getPrimaryService`), `:189` and `:195` (`startNotifications`) and `:235` (`ctl.writeValue`). The same page is at `git:chorus-main:web/control.html:199-266`, from commits 13251eb, 549adbc and 85004e1 (2026-09-04/05, "Phone conductor: a browser page that drives the swarm over BLE"). It is a small GATT write-and-notify client for a different service, so it can serve as boilerplate for connect, notify and write. It has no OTA, no `writeValueWithoutResponse` and no flow control.
- **Browser USB flasher.** It is `web/index.html`, from 320d84b, cde10cc and 8136936 (2026-09-02), live as `git:gh-pages:index.html` and `leader.html`. It uses ESP Web Tools 10.4.0 over **WebSerial**, so it works over the cable only. It is useful for blorbarium's first flash, which has to ship the partition table, but it is not an OTA sender.
- No branch or PR has any JS/TS/HTML that writes to the OTA characteristics (`6E400011`/`6E400012`), and none has an OTA sender in JS. The **Web Bluetooth OTA sender still has to be written from scratch**, porting `host/ble_link.py:466-542` plus the `ota-last-mile` changes as §5 describes.

### Bottom line

Nothing better than the local `ble_ota.h` exists. The best available combination is still main's `src/ble_ota.h` (f6d207e) plus PR #40's untested host pacing plus the local-only b4b0574 trial logic. None of these has ever completed a full update.
