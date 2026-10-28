# Blorb relay (Android)

A small Android app that does for the badges what the website does over Web
Bluetooth, by itself and in the background, so nobody has to open a page. It
covers the badges at a house whose Wi-Fi they cannot join. It needs no
firmware change: it speaks the protocol the website already speaks
(`docs/ble.md`, `docs/ota.md`) and uses only the site's public endpoints. It
holds no accounts and no secrets.

## What it does

A foreground service (one quiet notification) keeps a low-power Bluetooth
scan running, filtered on the badges' Nordic UART service UUID. A WorkManager
job runs every 3 hours as well. A badge is synced when it is seen and is due
(`Policy.dueOnSight`): every 3 hours, 20 minutes after a failed sync, and at
once when it comes back after 15 minutes out of range. One badge is synced at
a time. Several badges are told apart by their Bluetooth address and listed by
the species name each one advertises.

One sync (`sync/BadgeSync.kt`):

1. Connect, MTU 247, subscribe to TX.
2. `TIME <unix> <tzMinutes>` at once, as the page does. This also keeps the
   badge from dropping a silent phone after 10 s.
3. `HELLO`, `STATE` (version, phase, stage, the Wi-Fi MAC the site knows it by).
4. `OTA STATUS`. If an image is on trial and this phone started that update,
   `OTA CONFIRM <token>` (ota.js `checkTrial`).
5. With a living pet: `GENOME`, then `POST /api/pet-lines` with the body the
   badge's own Wi-Fi window sends (mac, genome, stage, generation, fw,
   `wait:false`). This happens at most every 12 h.
6. `STATS json`, then `POST /api/stats` (life, parents, analytics, as
   `docs/life-stats.md` "Upload paths" says), at most every 6 h. It sends
   `STATS ack <n>` only when the site's reply names an `ack`.
7. The update. It fetches `https://blorb.hypnodroid.com/fw/<board>/manifest.json`
   and checks the manifest's shape. It checks the owner's ECDSA P-256
   signature against the key built into every badge (`src/hw/ota_pubkey.h`).
   If the version is newer (or a transfer stopped partway), it downloads the
   image, checks its size and SHA-256, and runs the update. The updater
   (`ota/Updater.kt`) is a state-for-state port of `web/public/ota.js`: BEGIN,
   wait for the yes, a 6 KB window past the last `at=`, gaps and stalls,
   REBOOT, reconnect, HELLO, CONFIRM, and STATUS until `trial=0`. Tokens
   persist per (badge address, version), so a phone that was killed
   mid-update still confirms its trial at the next sync.
8. `POST /api/debug` (kind `phone`, `via: "phone"`) with what this sync saw:
   HELLO, STATE, OTA STATUS and the line transcript. At most hourly.

**The yes.** Like the website, an update needs a hand on the badge: after
BEGIN the badge asks for 30 s ("Turn me around!"). The phone posts a
high-priority notification saying so. If nobody answers (`no_confirm`), the
phone does not ask that badge again for 3 hours. It never starts an unattended
ask between 21:00 and 07:00 phone time. A version that rolled back on a badge
is not retried automatically (as the page does). **Sync now** ignores all
three rules.

## The screen

It shows the badges seen, each with its version, when it was last seen and
synced, and the last result. Under them are the **Sync now** button, buttons
for the permissions and the battery exemption that are still missing, and the
recent log.

## Install on a phone

Sideload `app/build/outputs/apk/debug/app-debug.apk` (debug-signed):
`adb install -r app/build/outputs/apk/debug/app-debug.apk`, or copy it to the
phone and open it. On first launch, grant:

- Nearby devices (Android 12+), or Location on Android 8 to 11. On 10 and 11,
  choose "Allow all the time", or background scans stop.
- Notifications (Android 13+).
- "Don't optimize" for battery. Without it, Doze holds the scan's syncs and the
  periodic job for hours.

The service starts again after a reboot (`BOOT_COMPLETED`) and after an app update.

## Build

The toolchain on this laptop lives in `~/android`: JDK 21 (`~/android/jdk-21`),
SDK platforms 35 and 36, and build-tools 35 and 36. Gradle 8.14 is in the
wrapper cache.

```sh
./build.sh          # unit tests, then app/build/outputs/apk/debug/app-debug.apk
./build.sh test     # unit tests only
```

On a machine without the toolchain:

```sh
# JDK 17+ (any one)
sudo apt install openjdk-21-jdk-headless
# Android command-line tools, then the SDK pieces this build uses
mkdir -p ~/android/cmdline-tools && cd ~/android/cmdline-tools
curl -LO https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
unzip commandlinetools-linux-*_latest.zip && mv cmdline-tools latest
export ANDROID_HOME=~/android
yes | ~/android/cmdline-tools/latest/bin/sdkmanager --licenses
~/android/cmdline-tools/latest/bin/sdkmanager "platforms;android-36" "build-tools;36.0.0" "platform-tools"
JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64 ./build.sh
```

`compileSdk` is 36 because androidx core 1.17 needs it; `targetSdk` is 34 and
`minSdk` is 26.

## Tests

These are JVM unit tests (`app/src/test`), with no phone and no Bluetooth:

- `proto/LinesTest`: reassembling notifications into lines, including a
  character split across two; reply and event parsing; one request at a time;
  `#0 ERR`; timeouts; the 200-byte cap; GENOME/STATS chunk assembly and CRC.
- `ota/ReleaseTest`: the live 0.7.30 manifest's signature verifies against
  the badge key, and any changed field breaks it. Also bad manifests, version
  comparison, the size and hash checks, `imageId`, packet layout, chunking
  that covers an image exactly once, and BEGIN under 200 bytes.
- `ota/UpdaterTest`: the updater against `FakeBadge`, a port of
  `web/public/ota-mock.js`'s MockDish, on a virtual clock. It covers the cases
  in `web/test/ota.test.mjs`: a drop with lost writes and a lost `at=`, nobody
  saying yes, a corrupt byte, a rollback, an app killed after REBOOT that
  confirms at the next sync, a stranger who cannot confirm, BEGIN during an
  earlier trial, an unconfirmed trial, and another phone's BEGIN or ABORT. It
  also checks that an image that fails its manifest never reaches the badge.
  `FakePort.transcript` records the exchange in the protocol's own form
  (`#1 OTA BEGIN ...`, `#1 OK state=confirm ...`).
- `sync/PolicyTest`: the schedule, the quiet hours, and the pet-lines and
  stats bodies.

## Known gaps

- **Line packs cannot reach a badge over Bluetooth.** There is no protocol verb
  for it, so a pack installs only through the badge's own Wi-Fi window. The
  app registers the pet with the site, but the new lines wait for Wi-Fi.
  Delivering them would take a firmware change.
- `/api/badge/report` is built by the firmware from data no Bluetooth verb
  exposes (heap, log, crash). The app posts its own `/api/debug` note instead.
- The site's `/api/stats` does not answer with `ack` yet, so the badge keeps
  its closed analytics buckets, and every upload repeats them.
- Every update needs a child to turn the badge within 30 s of the ask.
- This app has not been tested against a real badge or on a real phone yet.
  Only the JVM tests above have run.
