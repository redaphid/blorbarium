# Wi-Fi and updating itself

The owner asked for this (2026-10-19): "wifi ota updates, with the standard BLE
characteristics for writing wifi usernames/passwords for joining wifi
networks [...]. When plugged in to power, the device should be more
aggressive about finding and connecting to the house wifi. Then, check the
ota and update itself when no one is using it (no user input)." Later
guidance: keep it simple, because these devices are hard to debug. Use the
fewest moving parts and states, plain code, no new tasks or timers, and every
state on serial and behind a DEBUG verb.

So, in one paragraph: a phone gives the badge one Wi-Fi network over
Bluetooth, using Improv Wi-Fi. When the badge is on external power, has gone
idle (low power, no input for the idle time), and nobody is connected or in
a duel, it brings Wi-Fi up. It joins that network, reads the release
manifest over HTTPS, and takes Wi-Fi down again. If the manifest names a
newer signed image, the badge downloads it through the same `ota::Session`
as a Bluetooth update, with the same signature, hash, trial and rollback.
Then it restarts at a quiet moment. The new image confirms its trial by
reaching the site over Wi-Fi.

| Part | Where |
|---|---|
| Improv codec and state machine | `lib/wifiota/include/wifiota/improv.h`, `src/improv.cpp` |
| The remembered network's stored form | `lib/wifiota/include/wifiota/remembered.h` |
| Manifest parser and the install verdict | `lib/wifiota/include/wifiota/manifest.h` |
| Power inference from the battery voltage | `lib/wifiota/include/wifiota/power_sense.h` |
| When a window opens and when it must close | `lib/wifiota/include/wifiota/schedule.h` |
| The unattended install (BEGIN's checks, no question) | `ota::Session::install`, `lib/ota/src/session.cpp` |
| Native tests for all of the above | `test/test_wifi_ota` |
| Improv GATT service | `src/hw/improv_ble.h` |
| Station and HTTPS GET | `src/hw/wifi_station.h` |
| NVS store | `src/hw/wifi_store.h` |
| Battery ADC | `src/hw/battery.h` |
| Pinned TLS roots | `src/hw/tls_roots.h` |
| The loop's Wi-Fi: windows, download, DEBUG verbs | `src/wifi_ota.h` (included once by `src/main.cpp`) |
| The website's "Wi-Fi" card | `web/public/improv.js`, `web/public/index.html`, test `web/test/improv.test.mjs` |

## Provisioning: Improv Wi-Fi over Bluetooth LE

[Improv Wi-Fi](https://www.improv-wifi.com/ble/) is the de-facto open
standard for handing a device a Wi-Fi network over BLE. ESPHome, WLED and
Home Assistant use it, and it works from a web page over Web Bluetooth. The
alternatives were heavier or would not work from our page:

- Espressif's BLE provisioning (protocomm, protobuf, optional SRP6a) needs
  Espressif's app or a protobuf stack on the page, and pulls in far more code.
- Wi-Fi Easy Connect (DPP) needs a QR code and a phone OS that supports it.
  No browser does.

The badge adds Improv's service `00467768-6228-2272-4663-277478268000` to
the NimBLE server that already serves the UART service. Both run on the same
connection to the same phone, and nothing in the UART protocol changed.
Characteristics `...8001` current state (read, notify), `...8002` error
state (read, notify), `...8003` RPC command (write), `...8004` RPC result
(read, notify) and `...8005` capabilities (read, `0`). The advertisement
keeps the UART UUID and flags. Improv's 16-bit service data (`0x4677`: state,
capabilities, four zeros) fills the rest of it. Improv's 128-bit UUID goes
in the scan response after the name, only if there is room, so it can never
push the name out. Boot logs `[wifi] improv service up: ...` with what fit.

The badge answers two RPCs:

- **Wi-Fi settings (1)**: state goes `authorized -> provisioning`, the loop
  opens a Provision window and tries the network for 15 s. If it joins, the
  badge saves the network and answers state `provisioned` with a result
  holding `https://blorb.hypnodroid.com/?d=<name>`. If not, it answers error
  `unable to connect (3)` and state `authorized`. During a duel the badge
  answers unable to connect at once.
- **Device info (3)**: `blorbarium`, the firmware version, `ESP32-S3`, the
  BLE name.

Identify (2) and scan (4) are optional in the spec and are not implemented
(see "What was cut"). Both answer `unknown RPC`. The badge has no
authorization step (state 1). See "Security".

**The website.** When the page connects to a dish that has the Improv
service, it shows a **Wi-Fi** card with a network name, a password and
**Connect to Wi-Fi**. `web/public/improv.js` encodes the packet, subscribes
to the three notify characteristics on first use (not on every connect), and
waits for the result or the error. Old firmware has no Improv service, so
the page shows no card. `?mock=1` has a pretend Improv in which the password
`wrong` fails.

## Credentials

The badge remembers one network, the last one sent. A new one replaces it.
The network is stored as one blob (`version 1, ssid length, ssid, password
length, password`) under key `net` in namespace `wifi` of the default `nvs`
partition, beside the OTA trial's `ota` records. It is never written to
`pet` or `petfs`. Boot only reads, opening read-only, so a badge that has
never been given a network has no `wifi` namespace at all. Nothing is
written before boot has decided (`blorb/provision.h`). The station runs with
`WiFi.persistent(false)`, so the Arduino core does not make its own copy in
the driver's NVS namespace. The Wi-Fi driver still keeps its PHY calibration
in the default `nvs` partition, as for duels.

The password is stored in plain text in NVS: the badges have no flash
encryption. A person with the badge and a cable can read it. Nothing reads it
back over Bluetooth.

## Knowing it is plugged in

The 1.28 board has no VBUS or charge-status pin. Its only power signal is
the battery through a divider on GPIO1, taken as 3:1 from Waveshare's demo.
`wifiota::PowerSense` reads the voltage every 10 s, averaged over 8 reads:

- 4170 mV or more counts as on power. Only a charger holds a cell there.
- A rise of 40 mV between readings counts as on power, because a charger
  starting lifts the terminal voltage.
- A fall of 40 mV counts as on battery, because the charger stopped.
- If the voltage stays 30 mV or more under its peak for 10 minutes while on
  power, the badge counts as on battery. This catches a badge unplugged after
  charging had finished, which steps down too little to see.
- Under 2500 mV there is no real reading, which counts as on battery.
  Unknown errs to battery, so Wi-Fi stays off.

These numbers are tested natively but **not yet checked on a board**.
`DEBUG wifi` prints the pin's own millivolts beside the cell's, so the
divider can be checked. `DEBUG wifi power on|off|auto` overrides the
inference until reset.

## When Wi-Fi is on

Wi-Fi is off unless a window is open. A window brings the radio up, joins,
does one thing and takes the radio down. Four kinds:

| Window | Opens when | Closes early when |
|---|---|---|
| `check` | on power, idle (low power), no phone, no question on screen, no duel or fight mode, network remembered, `DEBUG wifi on`, and due. Due at boot, 6 h after a manifest was read, 10 min after a failure | input (not idle), a phone connects, a question, a duel or fight mode wants the radio |
| `forced` | `DEBUG ota check [url]`, whatever the power and whoever is there | a duel wants the radio |
| `confirm` | on trial after an update over Wi-Fi, every 20 s until it confirms | never: on trial, no duel opens (`docs/ota.md` "Other users of the radio") |
| `provision` | a phone sent a network over Improv | a duel wants the radio |

A window's phases are `off -> joining -> (the manifest is read at once) ->
image -> off`. A failed join (15 s), a failed GET or an unreadable manifest
closes the window. A Check then retries in 10 minutes. On battery a Check
never opens. One already open finishes, because the voltage reading can sag
for a moment.

**Everything runs on the loop.** There is no task and no timer. The GET's
connect, TLS handshake and response headers block the loop. Each is capped:
4 s connect, 6 s handshake, 4 s headers, under the 20 s trial watchdog,
which is fed on both sides. The window logs how long it took. In practice
expect a second or two. The badge is dark and idle when a Check runs. The
image is then read without waiting, because the socket is non-blocking:
each loop pass takes up to 16 KB of what has arrived and hands it to
`ota::Session::data()` as offset-prefixed packets, the same path a page's
writes take. A download that hears nothing for 15 s fails.

**Heap.** Boot leaves about 76 KB of internal heap with Bluetooth up. A TLS
session needs about 45 KB. mbedtls allocations of 1 KB or more go to PSRAM
(`tlsCalloc` in `src/wifi_ota.h`). Smaller ones stay internal. If TLS
misbehaves, setting `kTlsPsramFrom` to `SIZE_MAX` puts everything back in
internal RAM. The window logs heap, minimum heap, largest block and the loop
stack's free words at radio up, around every GET, every 5 s of download, and
at close. Look for `[wifi] radio up heap A -> B`, `[wifi] GET ...
(heap= minheap= big= stack_free=)` and `[wifi] window ... closes
(heap= minheap=)`.

## Duels and the radio

A station joined to an access point pins the radio to the AP's channel.
ESP-NOW duels need channel 1 and the radio to themselves. The two never run
at the same time:

- A window counts as busy, and does not open, during fight mode, a duel, or
  while ESP-NOW is open.
- When a duel wants the radio, `RadioGate::open` in `main.cpp` first calls
  `wififw::yieldRadio()`. Any window except `confirm` closes on the spot:
  the radio goes down synchronously, so the duel can open in the same call.
  An update over Wi-Fi that was mid-download is aborted, and its slot is
  discarded. The gate's refusal reasons gain `wifi_up` (only during
  `confirm`, which is on trial and refused anyway).
- An update over Wi-Fi that has downloaded and switched (`ready`) no longer
  holds the radio. A page's update still does.

## Updating itself

1. The Check window reads `https://blorb.hypnodroid.com/fw/lcd128/manifest.json`
   (`docs/ota.md` "Release layout"). Unknown keys are skipped.
2. `wifiota::judge` decides, logged as `[wifi] manifest ... : <verdict>`:
   - `wrong_board`: the image is for a different board.
   - `up_to_date`: the version is not newer than `fw=`. An unattended update
     never goes back, even signed `--allow-older`. A downgrade needs a hand
     and Bluetooth.
   - `older_format`: the image writes an older save format than this badge's
     `fmt=`. It could not read the pet, so it would roll back. A newer format
     is fine, because the new image migrates the pet (`docs/ota.md` "Save
     formats").
   - `rolled_back`: this exact version switched in here before and rolled
     back. Only a page retries it.
   - `auto_off`: the manifest says `"auto": false`. This is **the owner's
     kill switch** for every badge's unattended install. Add it to
     `manifest.json` and deploy. Bluetooth updates are unaffected. Each badge
     also has its own: `DEBUG wifi off`, kept in NVS.
   - `install`.
3. `ota::Session::install` runs BEGIN's checks: board, newer, size, not on
   trial, pet not busy, the verify rate limit, and **the P-256 signature over
   the same signed text with the same key**. It then does what a button
   press does: it saves the pet (the flush) and opens the slot. A refusal is
   logged by BEGIN's name (`BAD_SIGNATURE`, `BUSY`, ...). A page's session
   and a Wi-Fi one exclude each other.
4. The badge downloads the image and checks size against Content-Length,
   then the SHA-256. `esp_ota_end` checks the image. The badge writes the
   handover record (the token is `WIFI` plus 12 random bytes) and switches
   the slot: `Session::finish`, as for Bluetooth.
5. **Restart at a quiet moment.** The update restarts only when the badge is
   in low power and no phone is connected (`wififw::mayRestart`). It flushes
   the pet again first (`prepareRestart`). A page's update restarts as it
   always has.
6. **Trial.** The new image boots on trial: it writes nothing to the pet and
   must keep itself within 5 minutes (`docs/ota.md` "The trial"). It sees a
   `WIFI` handover token, stays dark, and opens a `confirm` window every 20 s.
   Reaching the site and reading a manifest over Wi-Fi counts as the page's
   CONFIRM. Thirty seconds after boot it keeps itself, with the panel up and
   the same pet resumed. If it cannot reach the site in 5 minutes, or the
   checks fail, it rolls back to the old image. The old image then reports
   `rolledback=<version>` and never auto-installs that version again.

Rate: one manifest read per 6 h, one try per 10 min after failures, and only
on power and idle. The release site sees at most one manifest GET per badge
per 6 h, plus retries.

## Security

- **The image is trusted only because of its signature**, exactly as over
  Bluetooth. The signature is P-256 over `blorbota2 <board> <version> <flags>
  <fmt> <size> <sha256>`, checked with the key built into the firmware. A
  hostile network, a hostile manifest, or a compromised website cannot make a
  badge install an image the release key did not sign. They cannot make it
  install an older one (downgrades are never unattended), or one for another
  board.
- **TLS.** HTTPS uses only the roots in `src/hw/tls_roots.h`: ISRG Root X1
  and X2 (Let's Encrypt) and GTS Root R1 and R4 (Google Trust Services).
  Cloudflare serves the site from either CA and can switch. On 2026-10-19 the
  chain was `hypnodroid.com <- WE1 <- GTS Root R4`. There is no public CA
  bundle. A chain under any other root fails the handshake, and the badge
  tries again later. If Cloudflare ever moves to another CA, Wi-Fi updates
  stop until a Bluetooth update adds its root. They fail safe. TLS protects
  the parts the signature does not cover: which release is current, the
  image's file name, and the `auto` switch. Without TLS, an attacker could
  replay an old manifest to hold updates back. The badge does not keep wall
  time, so it does not check certificate dates (mbedtls skips them without a
  clock).
- **A hostile access point** can refuse service (no join, no answer, a
  wrong certificate). It cannot do more, given the two points above.
- **Improv has no authorization step.** Anyone in Bluetooth range can give
  the badge a network, as anyone can already rename the pet or start an
  update over the page. The worst outcome is the badge's network replaced
  with the stranger's: TLS and the signature still hold, so updates stop
  until a keeper sends the house network again. Nothing reads the stored
  password back. The spec's authorization (a button press) would cost the
  button-free controls a new gesture. The owner can ask for it.
- `http://` is allowed only through a cable's `DEBUG ota check http://...`,
  for a test manifest on the LAN. The signature check still applies.

## Sizes (badge128_pm)

| | Base 227dcff | This branch | Change |
|---|---|---|---|
| Flash (app) | 2,407,869 B (57.4%) | 2,605,573 B (62.1%) | +197,704 B: Wi-Fi driver, lwIP, HTTPClient, WiFiClientSecure |
| Static RAM | 212,624 B (64.9%) | 217,380 B (66.3%) | +4,756 B: the Wi-Fi and HTTP libraries' statics, Improv's receive buffer, the cable's DEBUG queue at 128 B a line (+576 B) |

Each OTA slot is 4 MB, so the image (about 2.6 MB) still fits with room. The
heap in a window has not been measured yet: it is step 3 of the test plan.

## Serial and DEBUG verbs

Every line starts `[wifi] `. Every window open and close, phase change,
power change and Check-hold change (`hold not_idle -> open`) is one line.
GETs log their result, time, heap and stack.

| Verb | Does |
|---|---|
| `DEBUG wifi` | Status: window, phase, network, auto, power (and why, vbat, pin), hold (why a Check is not open), next check, station, Improv state/error, trial, OTA owner, last outcome, heap |
| `DEBUG wifi set <ssid> [password]` | Remember a network (quote an SSID with spaces). Saved, not tried; `DEBUG ota check` tries it |
| `DEBUG wifi forget` | Erase the network |
| `DEBUG wifi on` / `off` | This badge's automatic checks (kept in NVS). Provisioning and `ota check` work either way |
| `DEBUG wifi power on` / `off` / `auto` | Override the power inference until reset |
| `DEBUG ota check [url]` | A check now, idle or not. The url (http or https, up to about 115 characters) points at a test manifest |

## What was cut

These were cut to keep the moving parts few (owner, 2026-10-19). Any of them
can come back if the owner asks.

- **Several remembered networks** with scan-ordered joining. The badge
  remembers one network. There is no scan before joining, so a hidden SSID
  works the same as any other.
- **Improv scan** (the page's network list) and **identify** (the screen
  saying "It's me!"). Both are optional in the spec. The page has a typed
  network name.
- **Exponential backoff.** After a failure the badge waits a fixed 10 min.
- **The background fetch task and its packet ring.** Everything runs on the
  loop, at the cost of a bounded blocking GET.

## Hardware test plan

You need a 1.28 badge on the cable (`docs/hardware-testing.md`: identify it
by USB serial and MAC, hold the badge daemon off), a 2.4 GHz network, the
Android phone, and for steps 4-6 the signing key on `soul`. Flash
`badge128_pm` from this branch:

    ~/.platformio/penv/bin/pio run -e badge128_pm -j 2
    tools/flash_board.sh <port> <serial> <mac> .pio/build/badge128_pm/merged.hex

Log with `uv run tools/serial_log.py --port <port> ...` and send DEBUG lines
with `--send`.

1. **Boot and Improv up.** Expect `[wifi] improv service up: service data
   advertised, uuid in scan response yes|no room`, `[wifi] network none,
   automatic checks on`, `[wifi] power battery|on (vbat=... pin=...)` and
   `[hw] ble name=...`. Check that the page's chooser still finds the badge,
   that HELLO, STATE and GENOME work, and that a duel with a second badge
   still works (Improv must not break anything). Compare `[hw] ble name=...
   heap=` with the base's.
2. **Provision from the website.** Serve the page (`docs/ota.md`, "Proving
   it": `wrangler dev` and `adb reverse`, or the deployed site once it is
   deployed). Connect, open the **Wi-Fi** card, and enter the house network.
   Expect `[wifi] improv: a phone sent network "..."`, `window provision
   opens`, `joined "..." rssi= ch= ip=`, `provision: provisioned "..."
   (saved)`, `window provision closes`, `radio down`. The page should say it
   joined. Repeat with a wrong password: the page should say it couldn't
   join, and the log should show `FAILED could not join`. Reboot, then run
   `DEBUG wifi`: `network="<ssid>"`.
3. **Plugged-in auto-connect.** Unplug USB (run on battery a minute), then
   plug in. Expect `[wifi] power on (... charger|rise)`. If the inference
   misses, record the vbat and pin values from `DEBUG wifi` on battery and on
   power. Those numbers settle the thresholds, and `DEBUG wifi power on`
   stands in meanwhile. Leave the badge alone until it goes dark (or run
   `DEBUG power idle 30`). Expect `hold not_idle -> open`, `window check
   opens`, `joined`, `GET https://...manifest.json: 200 in N ms (heap= minheap=
   big= stack_free=)`, `manifest lcd128 0.7.12 ...: up_to_date`, `window check
   closes`. **Record N, heap, minheap, big and stack_free.** Wake the badge
   during a window: it should close with `closed early: someone is using the
   badge`. Unplug: `power battery`, and no more windows.
4. **A forced check against a test manifest.** On a throwaway branch, bump
   `kFirmwareVersion` (for example 0.7.13), commit, and release it signed
   (`tools/ota_release.py release --env badge128_pm`, on `soul` with the key).
   Copy `web/public/fw/lcd128/manifest.json` and the `.bin` into a folder on
   the laptop, run `python3 -m http.server 8000` there, and restore the
   committed manifest. With the badge running this branch (an older
   version):

       DEBUG ota check http://<laptop-ip>:8000/manifest.json

   Expect `window forced opens`, `manifest ... 0.7.13 ...: install`,
   `downloading <size> bytes (signature ok, pet saved, slot open)`, `image at
   ... bytes` every 5 s, `downloaded and verified in N s`, `window forced
   closes`. Once the badge is dark and has no phone: the restart. Also try a
   manifest with one byte of `sig` changed: `install refused: BAD_SIGNATURE`,
   with no download. Try `"auto": false`: `auto_off`.
5. **A full update with trial confirm.** Continue from step 4. After the
   restart, expect `[hw] ota trial: on trial ...`, `[wifi] on trial after an
   update over Wi-Fi: confirming over Wi-Fi, screen dark`, `window confirm
   opens`, `trial confirmed over Wi-Fi (ok; ...)`, then `[hw] ota trial:
   kept` about 30 s after boot. Check `fw=0.7.13` and the same `genome=` as
   before. For the real path over TLS, do the same with the release deployed
   to the site and wait for an automatic Check (or `DEBUG ota check`).
6. **A rollback.** Build the test release with `-DBLORB_OTA_FAIL_TRIAL`
   (the panel reports down, so the trial must fail), or turn the access point
   off right after the download so the confirm can never reach the site.
   Expect `[hw] ota trial: FAILED ... rolling back`, the old version booting,
   `[hw] ota: the last update rolled back`, and on its next check
   `manifest ... : rolled_back`, with nothing downloaded.
7. **Duel coexistence.** With two badges, start a forced check on one
   (`DEBUG ota check`) and, while it says `joining` or `image`, start fight
   mode on it (hang it, or `DEBUG duel`). Expect `FAILED closed early: the
   radio is wanted for a duel`, `radio down`, then `[diag] radio open=1` and
   a duel that works. An interrupted download is aborted (`DEBUG wifi`:
   `ota=idle`; `OTA STATUS` over the page: `last=aborted`), and the next
   check starts over. Also confirm that no Check
   window opens while fight mode is searching (`DEBUG wifi`: `hold=radio_busy`).
8. **Kill switch and forget.** Run `DEBUG wifi off`, reboot, and check that
   `automatic checks OFF` appears and that no Check opens on power. Run
   `DEBUG wifi on` to restore it. Run `DEBUG wifi forget` and check that
   `DEBUG wifi` shows `network=none`.

## Open questions for the owner

- **Authorization for Improv.** Should a network only be accepted after a
  gesture on the badge (the spec's "authorization required")? Today anyone in
  range can set it. See "Security".
- **Check interval.** Every 6 h on power is a guess at "every few hours".
- **One network.** Is one enough (the house), or should a grandparent's be
  remembered too?
- **Power thresholds.** These must be checked on a real badge (step 3).
- **Idle time.** The badge uses low power's idle time (10 min, `BLORB_IDLE_SECONDS`).
  Should Wi-Fi wait longer?
