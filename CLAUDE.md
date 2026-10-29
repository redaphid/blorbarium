# blorbarium

ESP32-S3 pet badges for the owner's kids, plus the site at
https://blorb.hypnodroid.com. Work is tracked as GitHub issues on
redaphid/blorbarium.

## One badge, three transports: USB, Bluetooth and Wi-Fi do the same things

The owner, many times over: **USB serial, Bluetooth and Wi-Fi are only
transport layers. Every affordance the badge has must be reachable over all
three.** Logs, status, every `DEBUG` verb, Wi-Fi setup (setting, listing and
forgetting networks), update checks, OTA, diagnostics and anything added
later. A transport may change the framing and the gate in front of a
command, never which commands exist.

- **Gates.** Commands that can hurt the pet or take over the badge keep
  their gate on every transport:
  - Bluetooth: the unlock press (`docs/debug-ble.md`) or a turn (`docs/ota.md`).
  - Wi-Fi: the owner's admin token on the site (`docs/remote-debug.md`).
  - USB: having the cable is the gate.

  The gate is the same idea everywhere; what differs is how someone proves
  they are holding the badge, or that they are the owner.
- **Never cable-only.** Never add a verb, log line or setting that only the
  serial cable can reach. "DEBUG lines are serial-only" is the bug, not the
  design.
- **The why must reach every transport.** When something fails (a Wi-Fi
  join, an update, a provision), the reason has to be readable on every
  transport, not only in the serial log. The badge keeps its recent log and
  last outcomes in RAM so Bluetooth and Wi-Fi can read them back.
- **Tests and docs.** A new affordance comes with a test that drives it
  through each transport's front end, and with docs that list it once for
  all three.
- **Why:** the owner is usually far from the badges. On 2026-10-29 a
  provision from the site failed with only "couldn't connect", and the reason
  sat in a log that only USB could read.

## Standing rules

- Build only through `/home/redaphid/Worktrees/blorbarium/pio`, which is
  locked so one build runs at a time and uses a shared ccache.
- Never print, log or commit Wi-Fi SSIDs or passwords, the admin token, or
  the OTA signing key.
- Automatic update checks must never switch themselves off. Use a capped
  backoff plus rollback.
- Never make the controls or the fight UI harder for the kids.
- Don't police the tone or content of the pets' lines.
