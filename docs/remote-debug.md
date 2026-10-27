# Remote debug

The owner asked, before flying away from the badges: "I need remote debug
capabilities somehow." Wi-Fi OTA ([wifi-ota.md](wifi-ota.md)) is then the only
way to reach them, so remote debug rides on its windows.

## What the badge sends

Every Check window (and a cable's `DEBUG ota check`) that read the manifest
ends with one more HTTPS POST, after the pet's lines: `/api/badge/report`
(src/remote_debug.h). An update being downloaded skips it; the boot after does
its own check soon. The report is JSON:

| field | what |
|---|---|
| `fw`, `board`, `uptime_s`, `reset` | the firmware, how long it has run, why the boot before ended (`panic`, `task_wdt`, `brownout`, `sw`, ...) |
| `crash` | when that was a panic or a watchdog: the core dump's summary (task, PC, cause, up to 16 backtrace PCs, the ELF's SHA prefix), read at boot from the `coredump` partition. Decode with `xtensa-esp32s3-elf-addr2line -e firmware.elf <pcs>` |
| `ota` | automatic checks on (`auto`, must be 1), the window, seconds to the next check, the interval, the absent and server streaks, on trial, the version that rolled back, crashes inside a window in a row, how the last window ended |
| `heap`, `minheap`, `big`, `psram` | internal heap now and its low-water mark since boot, the largest internal block, free PSRAM |
| `vbat_mv`, `rssi`, `dark` | the battery, the signal, the screen asleep |
| `pet` | kind, generation, stage, age in minutes, hunger % |
| `results` | the last commands' results |
| `state`, `shot` | a `state` command's capture, a `shot` command's frame (the cable's shot lines) |
| `log` | the serial log's last 8 KB |

The log is everything printed through `Serial` (src/hw/log_tee.h: main.cpp's
`Serial` is a tee into an 8 KB PSRAM ring). The IDF's own `ESP_LOG` lines and a
panic's register dump are not in it; the core dump covers a panic.

Memory: the ring, the report's body (28 KB, more with a shot) and everything
else is PSRAM, made at the first use. The POST's body is borrowed by the GET
task (`NetJob::start(..., borrowLen)`), not copied. Internal RAM gains a few
words and two RTC words; the TLS handshake is the same one the lines' POST makes.

A report that fails is not retried in the window: the next window's is newer
and carries the same unsent results.

## Commands

The owner queues them in `/admin` (a badge's page, "Remote debug"). The reply
to the badge's next report hands them over (plain text, `blorb-cmds 1` then
`<id> <verb> [arg]` a line), each once; the badge runs them after the window
has closed, one a loop pass, and the next report carries the results.

| verb | does |
|---|---|
| `check` | the next update check is due now; it still waits until dark and alone |
| `interval <30..720>` | minutes between checks until the next restart (the default is 6 h) |
| `state` | STATE, `DEBUG power status`, `power stats`, `fps` and `wifi status` printed, and what they printed (6 KB) captured for the next report |
| `shot` | the screen's last frame (it is dark: nothing repaints it) for the next report; `/admin` draws it |
| `reboot` | restarts once dark and alone, dark after the restart (like an update's trial); its result comes from the next boot |
| `marquee <text>` | a message for the kids: `showMarquee(text)`, full-screen scrolling text. At most 120 printable ASCII characters (`rdebug::kMarqueeMax`); the site turns accents into plain letters, anything else into spaces, and cuts it to 120. Until the marquee branch lands, main.cpp's stub only logs it |

Nothing else exists: no verb can turn the automatic checks off, forget or
change a network, erase anything, or install firmware. The interval's bounds
keep the checks coming at least every 12 h. The allowlist is checked when a
command is queued, again before the site sends it, and again on the badge
(lib/rdebug, test/test_remote_debug).

## Security

Commands are authenticated by TLS, not by a signature of their own. The badge
POSTs only to `https://blorb.hypnodroid.com/` (`rdebug::trustedReplyUrl`) and
its HTTPS client checks the server's chain against the two pinned CAs
(src/hw/tls_roots.h, `setCACert`: `MBEDTLS_SSL_VERIFY_REQUIRED`) and its name
(`mbedtls_ssl_set_hostname`), so a network attacker cannot answer in the site's
place. The site queues commands only for `ADMIN_TOKEN`. Each id runs at most
once (the site hands it over once and marks it delivered; the badge drops ids
it has seen), and a command not fetched within 7 days expires.

What that leaves: someone holding the admin token, or the Cloudflare account,
could queue the commands above, at worst a reboot per window or checks every
30 minutes. They could already replace the pets' lines with that access, and
still could not install firmware (manifests are signed with the owner's
offline key). A signed-command scheme would close that gap at the cost of a
tool on the owner's laptop and a key in the field; with no command able to do
harm, it was not worth it. The badge routes (`/api/badge/*`) are keyed by MAC
only: someone who knows a MAC could post a false report, which misleads the
dashboard but never reaches a badge.

## The owner's side

- `/admin` → **Health**: every badge, its last report's age, firmware, uptime,
  last reset (red after a crash), updates (auto on, next check, interval,
  streaks, rollbacks, how the last window ended), heap and low-water mark,
  battery, signal, pet. Red: updates off or crashed; amber: silent 36 h or
  minheap under 4 KB.
- A badge's page → **Remote debug**: the command buttons, the commands and
  their results, and the last 40 reports, each with its log, crash, state and
  screenshot.
