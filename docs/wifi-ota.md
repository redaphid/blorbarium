# Wi-Fi and updating itself

The owner, 2026-10-19: "wifi ota updates, with the standard BLE
characteristics for writing wifi usernames/passwords [...] check the ota and
update itself when no one is using it." And 2026-10-21, before leaving the
badges with family hundreds of miles away: "I need to be able to update them
from hundreds of miles away. But I don't want any problem if they can't
connect to wifi: the use case is primarily offline car."

In one paragraph: the badge is told up to three Wi-Fi networks, over the
cable (`DEBUG wifi set`) or from the website over Bluetooth (Improv Wi-Fi,
after a hand on the badge says yes). When it has been dark (low power) for a
minute, with no phone connected, no question on the screen, no duel and no
low battery, and a check is due, it brings Wi-Fi up, scans once, and if a
remembered network is in range joins it, reads the release manifest over
HTTPS, and takes Wi-Fi down again. If the manifest names a newer signed
image, it downloads it through the same `ota::Session` as a Bluetooth update,
with the same signature, hash, trial and rollback, restarts while still dark
and alone, and the new image keeps itself only after reaching the site over
Wi-Fi. With no remembered network in range (a car), a check is one scan of
about 2 s, a few times a day at most, and nothing else changes.

| Part | Where |
|---|---|
| Improv codec and state machine (with authorization) | `lib/wifiota/include/wifiota/improv.h`, `src/improv.cpp` |
| The hand's yes before Improv takes a network | `lib/wifiota/include/wifiota/consent.h` |
| Up to three remembered networks, their stored form, which to join | `lib/wifiota/include/wifiota/remembered.h` |
| Manifest parser and the install verdict | `lib/wifiota/include/wifiota/manifest.h` |
| When a window opens, when it closes, the backoff | `lib/wifiota/include/wifiota/schedule.h` |
| The unattended install (BEGIN's checks, no question) | `ota::Session::install`, `lib/ota/src/session.cpp` |
| Native tests for all of the above | `test/test_wifi_ota` |
| Improv GATT service | `src/hw/improv_ble.h` |
| Station: up, scan, join, down | `src/hw/wifi_station.h` |
| The HTTPS GET, on its own task | `src/hw/net_job.h` |
| NVS store | `src/hw/wifi_store.h` |
| Battery ADC (GPIO1) | `src/hw/battery.h` |
| Pinned TLS roots | `src/hw/tls_roots.h` |
| The loop's Wi-Fi: windows, download, the question, DEBUG verbs | `src/wifi_ota.h` (included once by `src/main.cpp`) |
| The website's "Wi-Fi" card | `web/public/improv.js`, `web/public/index.html`, test `web/test/improv.test.mjs` |
| Home orientation per badge (survives updates) | `src/hw/home.h`, `src/hw/home_store.h`, tests in `test/test_orient`, `test/test_feeding` |

## Offline first: the policy, and why

**Power is not sensed.** The Waveshare ESP32-S3-LCD-1.28 brings out no VBUS
or charge-status pin (Waveshare's wiki: GPIO1 is the battery through a
200K/100K divider; the charger, an ETA6096, reports nothing to the chip). The
first branch guessed "plugged in" from the battery voltage (4170 mV or more,
or a 40 mV step); that guess was never checked on a board, a freshly charged
battery reads the same as a charger, and a wrong guess either way is bad
(never updating, or Wi-Fi in a car). So the policy does not depend on it:

- **Only dark and alone.** A Check opens only when the badge has been in low
  power for `kDarkMs` (1 min; low power itself needs 10 min with no input),
  no phone is connected (the web app, `tools/ble_ota.py`), no question is on
  the screen, no duel, fight mode or ESP-NOW, and the battery is not clearly
  low (a real reading under 3550 mV). The screen is off, so nothing can stutter
  and nobody is waiting on input.
- **Gone the moment someone comes.** Any input (low power ends), a phone, a
  question or a duel closes a Check at once: the scan is dropped, the GET is
  told to stop, a half download is discarded (the slot is not the running
  image's), and the radio goes down once the GET's task has ended.
- **One scan, then nothing.** A Check scans once (active, 150 ms a channel,
  about 2 s). None of the remembered networks in range closes the window: the
  radio was on for about 2 s.
- **Backoff to hours.** Due once dark after boot, then 6 h after a manifest
  was read. With no network in range: 15 min, 30, 1 h, 2, 4, 8 h, then every
  8 h. With the network there but the site failing: 15 min doubling to 6 h. A
  week in a car is about 25 scans in all (`Schedule.NoNetworkInRangeBacksOffToHours`).
- **No reboot loops, and never off.** A window (not a trial's confirm) marks
  RTC memory while it is open. A boot that finds the mark (a crash, watchdog
  or brownout inside a window) waits 30 min before the next Check, doubling
  with each in a row to 4 h at most. Nothing turns checks off: not crashes,
  not a setting (there is no `DEBUG wifi off`), not a battery that reads low
  (it holds checks for 12 h at most). The owner: "Automatic updates should
  never be switched off. It will be my only access to the pets when I'm
  gone." An update over Wi-Fi restarts only dark and with no phone.
- **Nothing waits on the loop.** The scan runs on the Wi-Fi driver's task and
  is polled; the join is polled; the GET (DNS, connect, TLS, headers, body)
  runs on its own task (`hw::NetJob`, core 1, priority 1: never above the
  loop, always under the 250 Hz sense task at priority 2, and off core 0,
  which is Bluetooth's, the Wi-Fi driver's, ESP-NOW's and the panel's send
  task's). The loop writes the image to flash 8 KB a pass (about two sector
  erases). What is left on the loop is `WiFi.mode()` up and down, tens of ms,
  while dark.
- **No effect on Bluetooth or duels.** A phone connected holds every Check.
  A duel wants the radio: any window but the trial's confirm gives it up
  (`yieldRadio`), and the gate says `wifi_up` until the radio is down.
- **Battery.** Wi-Fi is off between windows (`WIFI_OFF`), and light sleep is
  held off only while a window is open. Offline, a day costs a few 2 s scans
  (~100 mA each): well under 1 mAh.

A badge with no network remembered never touches Wi-Fi at all.

## Provisioning

**Over the cable (recommended in person).** The cable is a hand on the badge
already, so no yes is asked:

    DEBUG wifi set "Home Network" the-password
    DEBUG wifi set GrandmasWifi her-password
    DEBUG wifi                      # lists them, newest first
    DEBUG ota check                 # tries it now: expect "up_to_date, nothing to install"

Up to three are kept, newest first; the same SSID again replaces its
password; a fourth pushes out the oldest. `DEBUG wifi forget "Home Network"`
forgets one, `DEBUG wifi forget` all.

**Over Bluetooth (Improv).** [Improv Wi-Fi](https://www.improv-wifi.com/ble/)
is the open standard for handing a device a network over BLE (ESPHome, WLED,
Home Assistant). The service `00467768-6228-2272-4663-277478268000` sits on
the same NimBLE server and connection as the UART service. The badge starts
in Improv's **authorization required**. When a page opens the service (reads,
subscribes or writes), the badge asks on its screen, "Join Wi-Fi? Turn me
around!", as the update's question does: a turn of the badge or a press of
its button, after 1.5 s and within 30 s, says yes. Improv then says
**authorized** for 2 minutes, and the page sends the network. Before the yes,
Wi-Fi settings answer error 4 (not authorized) and join nothing; device info
needs no yes. After a join, or when the phone goes, the next network needs a
new yes. The question holds the badge awake and takes the button and the
knock knock, as the update's does.

The website's **Wi-Fi** card (only for a dish with the Improv service) asks,
tells the person "Say yes on the dish: turn it around", waits for the yes,
sends the network, and says whether the dish joined. The card ships with the
site, so it needs a site deploy; the cable needs nothing.

## Credentials

One blob, `nets` (version 2, a count, then each network's SSID and password,
length-prefixed), under namespace `wifi` of the default `nvs` partition,
beside the OTA trial's `ota` records and the home orientation's `badge`. Never
`pet` or `petfs`, so a pet wipe keeps them, and an update never touches NVS.
The first branch's one-network `net` blob is still read. The station runs
with `WiFi.persistent(false)`, so the core keeps no copy of its own. The
password is in plain text in NVS (no flash encryption): a person with the
badge and a cable can read it. Nothing reads it back over Bluetooth.

## Windows

| Window | Opens when | Closes early when |
|---|---|---|
| `check` | the policy above | low power ends, a phone connects, a question, a duel or fight mode |
| `forced` | `DEBUG ota check [url]`, whoever is there | a duel wants the radio |
| `confirm` | on trial after an update over Wi-Fi, every 20 s until it confirms | never: on trial no duel opens |
| `provision` | a phone sent a network over Improv after the yes | a duel wants the radio |

Phases: `off -> scanning -> joining -> manifest -> image -> closing -> off`.
`provision` joins without a scan (the network may be hidden); so does a
check whose scan failed. A failed scan (6 s), join (12 s), GET or manifest
closes the window; a Check then backs off. The GET's own limits: 5 s
connect, 20 s TLS handshake (4.5 to 8.7 s seen on a weak signal), 6 s headers, and 20 s with no byte in the body.
A GET still running 60 s after it was told to stop has its connection cut.

**Internal RAM** is the scarce kind: bringing the radio up (for a window or
a duel's ESP-NOW alike) takes about 37 KB of it, and TLS about 45 KB more.
So Wi-Fi keeps nearly nothing in internal RAM: the GET's buffers and its
32 KB image ring, the scan table, the networks and the outcome text are in
PSRAM, made at first use; the image ring is a plain one (FreeRTOS's stream
buffer puts 1.4 KB of code in IRAM); mbedtls blocks of 1 KB or more go to
PSRAM (`tlsCalloc`). What Wi-Fi still costs internally: lwIP's DNS table
(1.2 KB), mbedtls's tables (0.9 KB), Improv's GATT service and buffers (about
2.5 KB), and while a GET runs, its task's 12 KB stack. To leave a duel more
than before Wi-Fi, the cable's DEBUG queue, `FaultAir` and the latency
recorder (about 10.5 KB, touched only by tasks, no locks or atomics) are made
in PSRAM too; boot says where they landed (`[hw] psram: faultAir=1 lat=1`).
Static internal RAM (badge128_flip, a753dec) is 7 KB under controls-latency's
build and 17 KB under 291260b's, whose boot heap was 59.7 KB against main's
71.5 KB. Logs: `[wifi] radio up heap A -> B`, `GET manifest:
N bytes, open N ms, task stack free N`, `window ... closed after N ms (heap=
minheap=)`.

## Updating itself

1. The Check reads `https://blorb.hypnodroid.com/fw/lcd128/manifest.json`.
2. `wifiota::judge`, logged `[wifi] manifest ...: <verdict>`: `wrong_board`,
   `up_to_date` (never a downgrade unattended), `older_format`, `rolled_back`
   (this version rolled back here three times: only a page retries it), `auto_off`
   (the manifest's `"auto": false`, **the owner's kill switch** for every
   badge), or `install`.
3. `ota::Session::install` runs BEGIN's checks, among them **the P-256
   signature with the built-in key**, flushes the pet and opens the slot.
4. The image streams into the slot; size, SHA-256 and `esp_ota_end` check
   it. The handover record (token `WIFI` + 12 random bytes) is written and the
   slot switched.
5. It restarts only when dark with no phone (`mayRestart`), and boots with
   the backlight off.
6. **Trial.** It writes nothing to the pet, opens a `confirm` window every
   20 s, and keeps itself 30 s after boot once it has read the manifest over
   Wi-Fi with the panel up and the same pet. No site in 5 minutes, or a failed
   check: it rolls back, and installs that version again at the next check,
   three times in all (a weak signal for 5 minutes is not a bad image), then
   never (publish a newer patch version instead). A failed join gets one
   more try inside the same window.

## The time, without a phone

Every HTTPS reply a window gets (the manifest, the image, a line pack, a 202
included) carries a `Date`, and the site adds `X-Blorb-Tz`: the offset in
minutes east of UTC of the zone Cloudflare places the request in
(`request.cf.timezone`, web/src/zone.js). The badge hands both to
`Dish::netTime`, which reads them as a phone's TIME is read: an unlived gap
is lived, the pet hours snap to the local day, and his wall-clock lines
(o'clock, morning, Monday) can speak. A zone a phone gave this boot outranks
the site's guess; no `X-Blorb-Tz` (an older Worker) still sets the wall, just
not the hour. Plain http (a cable's test URL) is never believed. It is not a
phone: no "returns" welcome, and he still misses the phone after half a day
without one. Offline nothing changes. The log line is
`[wifi] clock: <unix> from the server (zone ...)`.

## Home orientation survives updates

Three badges (USB serials 5C94094497, 5C94094999, 5C94095003) sit in cases
held USB-C down. That was a build flag (`BLORB_ORIENT_HOME=0`, env
`badge128_flip`), so a normal release would have turned them back. Now each
badge keeps its home in NVS (namespace `badge`, key `home`, 0 or 2), which
neither an update nor a pet wipe touches:

- At boot: NVS wins; else a flip build writes "down" (it migrates itself the
  first time it runs this code); else the build's default (up). The boot log
  says `[hw] home rot=0 (USB-C down, migrated: written; built down)`, then
  `nvs` on later boots.
- `DEBUG home` shows it; `DEBUG home down` / `DEBUG home up` sets it at once
  and keeps it.
- The screen's rotation, the shell's frame (orient::Up, the hang, the side
  hold, the aim) and the DEBUG poses all follow the runtime value
  (`Home.*` tests: a shell told its home at run time feels every sample as
  one built there).

## Security

- **The image is trusted only by its signature**, as over Bluetooth. A
  hostile network, manifest or website cannot make a badge install anything
  the release key did not sign, an older one, or one for another board.
- **TLS** uses only the pinned roots (`tls_roots.h`: ISRG X1/X2, GTS R1/R4).
  Another CA fails safe: updates stop until a Bluetooth update adds its root.
  TLS guards what the signature does not: which release is current and the
  `auto` switch.
- **Improv needs a hand's yes.** A stranger in range cannot give the badge a
  network without someone turning it or pressing its button while it asks.
  Even then, the worst is updates stopping until the family's network is set
  again: TLS and the signature still hold.
- `http://` only through the cable's `DEBUG ota check http://...` (a LAN
  test manifest); the signature still applies.

## Serial and DEBUG verbs

Every line starts `[wifi] `; every window, phase and Check-hold change is one line.

| Verb | Does |
|---|---|
| `DEBUG wifi` | Window, phase, networks, auto, hold (why no Check), next check, backoff streaks, vbat, station, Improv state, consent, trial, OTA owner, windows and scans since boot, last outcome, heap |
| `DEBUG wifi set <ssid> [password]` | Remember a network (quote an SSID with spaces) |
| `DEBUG wifi forget [ssid]` | Forget one, or all |
| `DEBUG wifi due` | The next automatic check is due now (still only dark and alone) |
| `DEBUG ota check [url]` | A check now, whoever is there; the url (http or https) for a test manifest |
| `DEBUG home [up\|down]` | Which way up is home on this badge (NVS) |
| `DEBUG wpa3 [plain\|both\|hnp\|h2e] [cpu] [psnone] [mac=<12 hex>]` | How WPA3 joins are made until reset: the core's plain join or option B with that SAE PWE, 240 MHz, PS_NONE (turns Bluetooth off for good: `DEBUG reboot` after), another MAC. The join logs its cell and lwIP's DHCP state every 2 s |

## Hardware test plan

A 1.28 on the cable, a 2.4 GHz network, the Android phone, and for steps 5-7
a test release signed with the owner's key (docs/ota.md "The key"). Flash this branch (`./flash.sh`, or
`./flash.sh --flip` for a flipped badge). Log with `tools/serial_log.py`.

1. **Boot, flip.** On a flipped badge flashed with `--flip`: `[hw] home rot=0
   (USB-C down, migrated: written; built down)`; reboot: `nvs`. Flash the
   *normal* build over it: still `rot=0 (USB-C down, nvs; built up)`, and the
   screen, hang (USB-C up) and side hold work as before. `DEBUG home up` then
   `DEBUG home down` flip it live. A `WIPE` over the debug link keeps it.
2. **Offline, quick.** No network remembered: `DEBUG wifi` says
   `networks=[] hold=disabled`, and no `[wifi] window` line ever appears.
   Then `DEBUG wifi set NoSuchNet x`, `DEBUG power idle 30`, leave it: one
   minute after it goes dark expect `window check opens`, `scanning`,
   `FAILED none of 1 remembered networks in range (N seen) (no network: next
   check later)`, `radio down`, `window check closed after ~2000 ms`.
   `DEBUG wifi`: `next_check_in=900s absent_streak=1`. Wake it during a scan:
   `closed early: someone is using the badge`, and the screen lights at once.
3. **Offline soak (the hard requirement).** Two badges, both with
   `DEBUG wifi set NoSuchNet x` and default idle. Hours of normal use: play,
   feed, hang, side hold, knock, aim, duels (Spar and Scrap), the web app
   connected and disconnected, `tools/ble_ota.py status`, and stretches left
   alone dark. Expect zero visible difference: no prompt, no error, no stutter.
   Measure against the same badge with no network remembered for the same script:
   `DEBUG fps` (fps, worst_ms, same), `DEBUG latency` (p50/p95 per control),
   `DEBUG power stats` (time light-sleeping), and the battery drop over the
   same hours (`DEBUG wifi` vbat at start and end). Expect differences within
   noise, `[wifi] window` lines only while dark, and scans spacing out
   15, 30, 60 min... (`absent_streak`). No `[wifi] !!!` line, no reset.
4. **Provision.** Over the cable: `DEBUG wifi set "<home>" <pw>`, `DEBUG ota
   check`: `scanning`, `joining "<home>"`, `joined ... rssi= ip=`, `GET
   manifest: N bytes, open N ms, task stack free N`, `manifest lcd128 0.7.12
   ...: up_to_date`, `radio down`. **Record open ms, heap, minheap, stack
   free.** From the website (once deployed, or `wrangler dev`): the Wi-Fi card
   shows "Say yes on the dish"; the badge shows "Join Wi-Fi? Turn me around!";
   turn it; the page says it joined; `DEBUG wifi` lists it. Let the question
   lapse once: the page says nobody said yes; nothing was joined.
5. **Automatic check at home.** Leave the badge alone on the table: about 11
   minutes later `window check opens` ... `up_to_date` ... `closed after N
   ms`, and `next_check_in` about 6 h.
6. **Remote-update rehearsal.** On a throwaway branch from this one, bump
   `kFirmwareVersion` past what the badge runs (e.g. 0.7.14), commit, build
   `badge128` through the wrapper, and sign it on this laptop
   (`tools/ota_release.py release --no-build`). Serve that
   manifest and image from the laptop (`python3 -m http.server 8000` in a
   folder holding both), or publish it to R2 (`tools/ota_release.py publish`).
   On the badge: `DEBUG ota check http://<laptop-ip>:8000/manifest.json` (or
   plain `DEBUG ota check` for the published one): `install`,
   `downloading ... (signature ok, pet saved, slot open)`, `image at ...`,
   `downloaded and verified`, then, dark and alone, the restart; `on trial
   after an update over Wi-Fi: confirming over Wi-Fi, screen dark`, `trial
   confirmed over Wi-Fi`, `[hw] ota trial: kept`; same pet; same home. Also:
   a manifest with one byte of `sig` changed (`install refused:
   BAD_SIGNATURE`, nothing downloaded), `"auto": false` (`auto_off`), and the
   access point switched off mid-download (`FAILED ... stalled`, the badge
   unchanged, `DEBUG wifi` `ota=idle`). For the real path, deploy the test
   release to a staging site or the live one and wait for step 5's check.
7. **Rollback.** Switch the access point off right after `downloaded and
   verified`: the trial cannot reach the site, rolls back after 5 min, and the
   next check says `rolled_back`.
8. **Duels.** `DEBUG ota check` on one badge and, while `scanning` or
   `joining`, start fight mode on it: `closed early: the radio is wanted for a
   duel`, `radio down`, `[diag] radio open=1`, and a duel that works.

## Releasing from afar

See docs/ota.md, "Away from home: releasing": `tools/ota_release.py ship`
builds `badge128` (one image for upright and flipped badges alike; never
`badge128_pm`, whose internal RAM cannot hold Wi-Fi up for its trial),
signs it and publishes it to the live prefix. Each badge picks it up at its
next Check: within about 6 h of being left alone at home, sooner after a
trip. To stop every badge's unattended installs at once, publish the
manifest with `"auto": false`.
