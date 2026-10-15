# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Flash a batch of badges, one after another, each hatching its own pet.

    ~/.platformio/penv/Scripts/pio.exe run -e badge128     # build merged.hex first
    uv run tools/flash_batch.py                            # badges 1, 2, 3 ... until q
    uv run tools/flash_batch.py --start 12                 # carry on numbering from 12
    uv run tools/flash_batch.py --dry-run                  # the whole loop on pretend boards

Per badge: "Plug in badge N, press Enter". It finds the CH343 port that
appeared (or the one badge already attached), reads its USB serial and MAC,
and writes merged.hex with tools/flash_board.sh --no-reset, which checks both
again before writing. Then it boots the board with the log open and reads the
firmware commit, how it booted, the genome and the BLE name. One line goes to
the screen and to flash_log.csv, then "Unplug badge N" waits for the port to
go. A failure asks: retry, skip or quit. A board held in download mode
(BOOT down while plugging in) flashes the same way.

Every badge is reset: its `pet` partition is first copied to
pet_backups/<mac>-<time>.bin, then flash_board.sh --wipe-pet erases pet and
petfs before writing, so the board boots boot=Fresh and founds its pet from
its own MAC (foundingGenome in lib/blorb/src/starter_genome.cpp). A boot that
is not Fresh fails the badge. The log refuses to stay quiet about a genome or
BLE name that another MAC in it already has.

On this desk the Claude Code badge daemon grabs every CH343 board it sees;
the script holds it off for as long as it runs (--daemonctl, docs/hardware-testing.md).
"""
import argparse
import csv
import os
import random
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
USB_ID = "1A86:55D3"                       # the CH343 on the Waveshare 1.28 (platformio.ini custom_usb_id)
IMAGE = ROOT / ".pio/build/badge128/merged.hex"
DAEMONCTL = Path("D:/Projects/claude-notification-screen/host/daemonctl.py")
PENV = Path.home() / ".platformio/penv"
ESPTOOL = Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py"
BACKUPS = ROOT / "pet_backups"
PET_NVS = (0x810000, 0x40000)            # tools/partitions_16mb.csv `pet`
FIELDS = ["time", "badge", "port", "usb_serial", "mac", "ble_name", "boot", "genome", "fw", "result", "note"]


class Failed(Exception):
    """One badge's step went wrong; the loop reports it and asks what next."""


# ---- pure parts (tests/test_flash_batch.py) ------------------------------------------


def arrived(before, now):
    """The badge port that appeared between two snapshots ({port: usb serial}), else None.

    Two at once is a mistake the person has to sort out, not a guess."""
    new = sorted(set(now) - set(before))
    if len(new) > 1:
        raise Failed("%d badges appeared at once (%s): plug in one at a time" % (len(new), ", ".join(new)))
    return new[0] if new else None


def choose_port(before, now):
    """(port, how): the newly attached badge, or the one badge that was already there."""
    port = arrived(before, now)
    if port:
        return port, "new"
    if len(now) == 1:
        return next(iter(now)), "already attached"
    return None, "none" if not now else "several attached, none new"


def parse_boot(lines):
    """What the boot log says: fw, boot, occupant, genome, gen, ble_name (the ones it printed)."""
    got = {}
    for line in lines:
        if m := re.search(r"\[hw\] blorbarium (\S+) fw=(\S+)", line):
            got["fw"] = "%s %s" % (m.group(2), m.group(1))
        elif m := re.search(r"\[hw\] boot=(\w+) occupant=(\w+) gen=(\d+) genome=([0-9a-f]{8})", line):
            got.update(boot=m.group(1), occupant=m.group(2), gen=int(m.group(3)), genome=m.group(4))
        elif m := re.search(r"\[hw\] ble name=(\S+)", line):
            got["ble_name"] = m.group(1)
        elif "!!! SEALED" in line or "!!! the pet is not being saved" in line:
            got.setdefault("alarms", []).append(line.strip())
    return got


def clashes(rows, mac, genome, ble_name):
    """Notes for anything another MAC in the log already has (a genome, a BLE name)."""
    notes = []
    for r in rows:
        if r.get("result") != "ok" or r.get("mac") == mac:
            continue
        if genome and r.get("genome") == genome:
            notes.append("DUPLICATE GENOME %s (badge %s, %s)" % (genome, r.get("badge"), r.get("mac")))
        if ble_name and r.get("ble_name") == ble_name:
            notes.append("duplicate BLE name %s (badge %s, %s)" % (ble_name, r.get("badge"), r.get("mac")))
    return notes


def fnv1a(data, h=0x811C9DC5):
    """blorb::fnv1a: the species seed a board founds its pet from is fnv1a(MAC bytes)."""
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def read_log(path):
    if not path.exists():
        return []
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def append_log(path, row):
    new = not path.exists()
    with path.open("a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        if new:
            w.writeheader()
        w.writerow({k: row.get(k, "") for k in FIELDS})


# ---- the bench: real boards, or pretend ones for --dry-run ---------------------------


class Bench:
    """The real desk: pyserial for ports and the log, esptool and flash_board.sh for writes."""

    def __init__(self, image, boot_seconds):
        self.image, self.boot_seconds = image, boot_seconds

    def badges(self):
        from serial.tools import list_ports
        return {p.device: (p.serial_number or "") for p in list_ports.comports()
                if USB_ID.lower() in (p.hwid or "").lower()}

    def esptool(self, port, *args):
        py = PENV / "Scripts/python.exe"
        if not py.exists():
            py = PENV / "bin/python"
        return subprocess.run([str(py), str(ESPTOOL), "--chip", "esp32s3", "--port", port, *args],
                              capture_output=True, text=True)

    def backup(self, port, mac):
        BACKUPS.mkdir(exist_ok=True)
        out = BACKUPS / ("%s-%s.bin" % (mac.replace(":", ""), datetime.now().strftime("%Y%m%d-%H%M%S")))
        r = self.esptool(port, "-b", "921600", "read_flash", hex(PET_NVS[0]), hex(PET_NVS[1]), str(out))
        if r.returncode != 0 or not out.exists() or out.stat().st_size != PET_NVS[1]:
            tail = (r.stdout + r.stderr).strip().splitlines()[-2:]
            raise Failed("could not back up the pet partition, so it was not erased: %s" % " / ".join(tail))
        return out

    def mac(self, port):
        r = self.esptool(port, "read_mac")
        m = re.search(r"^MAC:\s*([0-9a-f:]{17})", r.stdout, re.M)
        if not m:
            tail = (r.stdout + r.stderr).strip().splitlines()[-3:]
            raise Failed("esptool could not read the MAC on %s: %s" % (port, " / ".join(tail)))
        return m.group(1)

    def flash(self, port, serial, mac):
        cmd = [git_bash(), str(ROOT / "tools/flash_board.sh"), "--wipe-pet", "--no-reset", port, serial, mac, self.image.as_posix()]
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        out = (r.stdout + r.stderr).strip().splitlines()
        for line in out:
            if line.startswith("flash_board:") or "Wrote" in line or "Hash of data verified" in line:
                print("    " + line)
        if r.returncode != 0:
            raise Failed("flash_board.sh failed (exit %d): %s" % (r.returncode, " / ".join(out[-3:])))

    def boot(self, port):
        """Boot it with the log open (tools/serial_log.py's RTS pulse) and read until the BLE name."""
        import serial
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, 115200, 0.2
        s.dtr = s.rts = False
        s.open()
        try:
            s.rts = True
            time.sleep(0.1)
            s.rts = False
            lines, buf, t0 = [], b"", time.monotonic()
            while time.monotonic() - t0 < self.boot_seconds:
                buf += s.read(4096)
                *whole, buf = buf.split(b"\n")
                lines += [w.decode("utf-8", "replace").rstrip("\r") for w in whole]
                if any("[hw] ble name=" in line for line in lines):
                    break
            return lines
        finally:
            s.close()


class DryBench:
    """Pretend boards: a port turns up a moment after Enter and leaves when asked. Nothing is written."""

    def __init__(self):
        self.attached = {}
        self.rng = random.SystemRandom()
        self.n = 0

    def badges(self):
        return dict(self.attached)

    def plug(self):
        self.n += 1
        self.attached["COM%d" % (20 + self.n)] = "DRY%08d" % self.n

    def unplug(self):
        self.attached.clear()

    def mac(self, port):
        self.last = ":".join("%02x" % b for b in [0xD4, 0x05, 0x92] + [self.rng.randrange(256) for _ in range(3)])
        return self.last

    def backup(self, port, mac):
        return BACKUPS / ("%s-dry-run.bin" % mac.replace(":", ""))

    def flash(self, port, serial, mac):
        print("    (dry run) would run: tools/flash_board.sh --wipe-pet --no-reset %s %s %s merged.hex" % (port, serial, mac))

    def boot(self, port):
        """A fresh board's boot lines; the genome stands in as its MAC seed (the real one is the board's)."""
        raw = bytes.fromhex(self.last.replace(":", ""))
        return ["[hw] blorbarium dry-run fw=0.4.0 fmt=1 badge128",
                "[hw] boot=Fresh occupant=egg gen=0 genome=%08x age=0" % fnv1a(raw),
                "[hw] ble name=blorb-%02x%02x heap=0 big=0" % (raw[4], raw[5])]


# ---- the loop ------------------------------------------------------------------------


def git_bash():
    """Git's bash, never WSL's: WSL cannot see COM ports."""
    for p in (Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe",
              Path("C:/Program Files/Git/bin/bash.exe")):
        if p.exists():
            return str(p)
    return "bash"


def hold_daemon(path):
    """Keeps the Claude Code badge daemon off the port while this process lives (refresh every badge)."""
    if not path or not Path(path).exists():
        return
    r = subprocess.run([sys.executable, str(path), "hold", str(os.getpid())], capture_output=True, text=True)
    if r.returncode != 0:
        print("  warning: could not hold the badge daemon off: %s" % (r.stderr or r.stdout).strip()[-200:])


def ask(prompt):
    try:
        return input(prompt).strip().lower()
    except EOFError:
        return "q"


def wait_for_port(bench, before, seconds):
    """The badge to flash: one that appears within `seconds`, or the one already attached."""
    t0 = time.monotonic()
    while True:
        port, how = choose_port(before, bench.badges())
        waited = time.monotonic() - t0
        if how == "new" or waited >= seconds or (how == "already attached" and waited >= 2):
            break
        time.sleep(0.3)
    if not port:
        raise Failed("no badge found (%s). Is the cable a data cable? Is the CH343 driver installed?" % how)
    return port, how


def wait_gone(bench, port):
    while port in bench.badges():
        time.sleep(0.3)


def one_badge(bench, n, port, rows):
    serial = bench.badges().get(port, "")
    if not serial:
        raise Failed("%s has no USB serial number; flash_board.sh needs one to be sure of the board" % port)
    mac = bench.mac(port)
    print("  %s  usb %s  mac %s  (seed %08x)" % (port, serial, mac, fnv1a(bytes.fromhex(mac.replace(":", "")))))
    print("  pet backed up to %s" % bench.backup(port, mac))
    bench.flash(port, serial, mac)
    print("  booting it with the log open ...")
    got = parse_boot(bench.boot(port))
    missing = [k for k in ("boot", "genome", "ble_name") if k not in got]
    if missing:
        raise Failed("flashed, but the boot log never showed %s; it may not have booted" % ", ".join(missing))
    if got["boot"] != "Fresh":
        raise Failed("booted %s (genome %s), not Fresh: the pet was not reset" % (got["boot"], got["genome"]))
    notes = clashes(rows, mac, got["genome"], got["ble_name"]) + got.get("alarms", [])
    return {"time": datetime.now().isoformat(timespec="seconds"), "badge": n, "port": port, "usb_serial": serial,
            "mac": mac, "ble_name": got["ble_name"], "boot": got["boot"], "genome": got["genome"],
            "fw": got.get("fw", ""), "result": "ok", "note": "; ".join(notes)}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--start", type=int, default=1, help="the first badge's number (default 1)")
    ap.add_argument("--image", type=Path, default=IMAGE, help="merged .hex to write (default %(default)s)")
    ap.add_argument("--log", type=Path, default=ROOT / "flash_log.csv", help="CSV to append to (default %(default)s)")
    ap.add_argument("--wait", type=float, default=20, help="seconds to wait for a port after Enter (default 20)")
    ap.add_argument("--boot-seconds", type=float, default=45, help="how long to read the boot log (default 45)")
    ap.add_argument("--daemonctl", default=str(DAEMONCTL), help="badge daemon control script to hold off ('' for none)")
    ap.add_argument("--dry-run", action="store_true", help="pretend boards; writes nothing, logs to a .dry-run.csv")
    args = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)   # each line shows at once, even through a pipe or tee

    if args.dry_run:
        bench, log = DryBench(), args.log.with_suffix(".dry-run.csv")
    else:
        if not args.image.exists():
            sys.exit("no %s: build it first with  ~/.platformio/penv/Scripts/pio.exe run -e badge128" % args.image)
        if args.image.suffix != ".hex":
            sys.exit("%s is not a merged .hex; a flat image erases the pet partitions" % args.image)
        bench, log = Bench(args.image, args.boot_seconds), args.log
        hold_daemon(args.daemonctl)
    print("Writing %s; logging to %s" % (args.image if not args.dry_run else "nothing (dry run)", log))

    n = args.start
    while True:
        hold_daemon(None if args.dry_run else args.daemonctl)
        before = bench.badges()
        if ask("\nPlug in badge %d, press Enter (q to quit): " % n) == "q":
            break
        if isinstance(bench, DryBench):
            bench.plug()
        port = None
        while True:
            try:
                if port is None or port not in bench.badges():
                    port, how = wait_for_port(bench, before, args.wait)
                    if how != "new":
                        print("  using %s, which was already attached" % port)
                row = one_badge(bench, n, port, read_log(log))
                break
            except Failed as e:
                print("  FAILED badge %d: %s" % (n, e))
                append_log(log, {"time": datetime.now().isoformat(timespec="seconds"), "badge": n,
                                 "port": port or "", "result": "failed", "note": str(e)})
                choice = ask("  r to retry, s to skip this badge, q to quit [r]: ")
                if choice == "q":
                    return 1
                if choice == "s":
                    row = None
                    break
                if port not in bench.badges():
                    port = None     # it was replugged or moved: find it again
        if row:
            append_log(log, row)
            print("  OK badge %d: %s  mac %s  %s  genome %s  boot=%s%s" % (
                n, row["port"], row["mac"], row["ble_name"], row["genome"], row["boot"],
                ("  !! " + row["note"]) if row["note"] else ""))
        if port:
            print("Unplug badge %d" % n)
            if isinstance(bench, DryBench):
                bench.unplug()
            wait_gone(bench, port)
        n += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
