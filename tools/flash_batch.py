# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Flash a batch of badges, one after another, on Linux or Windows.

    pio run -e badge128                                    # build merged.hex first
    ~/.platformio/penv/bin/python tools/flash_batch.py     # badges 1, 2, 3 ... until q (keeps each pet)
    uv run tools/flash_batch.py --start 12                 # carry on numbering from 12
    uv run tools/flash_batch.py --wipe-pet                 # reset every badge to a new egg
    uv run tools/flash_batch.py --dry-run                  # the whole loop on pretend boards

Per badge: "Plug in badge N, press Enter". It finds the CH343 port that
appeared (USB id 1A86:55D3: /dev/ttyACM* on Linux, COMn on Windows), or the
one badge already attached, reads its USB serial and MAC, and writes
merged.hex with tools/flash_board.sh --no-reset, which checks both again
before writing. Then it boots the board with the log open and reads the
firmware commit, how it booted, the genome, the species and the BLE name. One
line goes to the screen and to flash_log.csv, then "Unplug badge N" waits for
the port to go. A failure asks: retry, skip or quit. A board held in download
mode (BOOT down while plugging in) flashes the same way.

The pet stays by default. Its `pet` partition is first copied to
pet_backups/<mac>-<time>.bin, then the merged .hex is written, whose gaps
leave `pet` and `petfs` alone, and the board must boot Resumed (FellBack
passes with a note). A Fresh boot passes only on a board that never held a
pet (its storage line says it was formatted); a pet that does not come back
fails the badge. --wipe-pet resets instead: flash_board.sh --wipe-pet erases
pet and petfs after the backup, and the board must boot Fresh, founding its
pet from its own MAC.

The firmware gives the 8 known badges one species each from a fixed MAC
table (docs/notes/decisions-2026-10-15.md D3, BADGE_SPECIES below) and
advertises the species as its BLE name. The log refuses to stay quiet about a
species or BLE name that another MAC in it already has, or a known badge that
comes up as another species than the table's.

On the Windows desk the Claude Code badge daemon grabs every CH343 board it
sees; the script holds it off for as long as it runs (--daemonctl,
docs/hardware-testing.md), and skips that where the script is not there.
LINUX.md has the Linux setup (the dialout group or a udev rule).
"""
import argparse
import csv
import os
import random
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
USB_ID = "1A86:55D3"                       # the CH343 on the Waveshare 1.28 (platformio.ini custom_usb_id)
VID_PID = (0x1A86, 0x55D3)
IMAGE = ROOT / ".pio/build/badge128/merged.hex"
DAEMONCTL = Path("D:/Projects/claude-notification-screen/host/daemonctl.py")
BACKUPS = ROOT / "pet_backups"
PET_NVS = (0x810000, 0x40000)            # tools/partitions_16mb.csv `pet`
MIN_ESPTOOL = (4, 7)                     # the first to write a .hex by its segments; older ones write the text itself
FIELDS = ["time", "badge", "port", "usb_serial", "mac", "ble_name", "boot", "genome", "fw", "result", "note",
          "species", "pet"]               # pet: the mode, keep or wipe
SPECIES = ("grungo", "mossby", "puffkin", "gilly", "mothwick", "mudge", "fizz", "hushwing", "tinker")
# docs/notes/decisions-2026-10-15.md D3, which the firmware's MAC table follows. The gift device is the one grungo.
BADGE_SPECIES = {
    "d4:05:92:77:1b:3c": "mossby",
    "d4:05:92:77:1b:44": "puffkin",
    "d4:05:92:77:1b:34": "gilly",
    "d4:05:92:77:1c:b0": "mothwick",
    "d4:05:92:77:1b:30": "mudge",
    "d4:05:92:77:1b:40": "fizz",
    "d4:05:92:77:22:58": "hushwing",
    "d4:05:92:77:22:40": "tinker",
}
PERMISSION_FIX = """this user may not open %s. Either join the serial group, then log out and back in:
      sudo usermod -aG dialout $USER        (on Arch: uucp instead of dialout)
  or let anyone open the badge's USB id with a udev rule, then unplug and replug it:
      echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55d3", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"' | sudo tee /etc/udev/rules.d/99-blorbarium.rules
      sudo udevadm control --reload-rules && sudo udevadm trigger"""
NO_ESPTOOL = """no esptool. Either install PlatformIO and build once (it brings its own):
      pio run -e badge128          (then ~/.platformio/packages/tool-esptoolpy/esptool.py is there)
  or put one on PATH:
      pipx install esptool         (or: pip install esptool)"""


class Failed(Exception):
    """One badge's step went wrong; the loop reports it and asks what next."""


# ---- pure parts (tests/test_flash_batch.py) ------------------------------------------


def is_badge(port):
    """A CH343 by USB id. pyserial gives vid/pid on Linux and Windows; the hwid text is the fallback."""
    vid, pid = getattr(port, "vid", None), getattr(port, "pid", None)
    if vid is not None and pid is not None:
        return (vid, pid) == VID_PID
    return USB_ID.lower() in (getattr(port, "hwid", "") or "").lower()


def badge_ports(ports):
    """{device: usb serial} for the badges among pyserial's list_ports records."""
    return {p.device: (p.serial_number or "") for p in ports if is_badge(p)}


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
    """What the boot log says: fw, pet_storage, boot, occupant, genome, gen, species, ble_name.

    Reads 0.4.0's `ble name=blorb-1b34` and 0.7.0's `ble name=mossby` alike. The species is the one
    an `[hw] adopted species=` line gives (a 0.4.0 pet taking his badge's), else the boot line's
    `species=`, else a BLE name that is a species (rel-species 7b557a6, src/main.cpp)."""
    got = {}
    for line in lines:
        if m := re.search(r"\[hw\] blorbarium (\S+) fw=(\S+)", line):
            got["fw"] = "%s %s" % (m.group(2), m.group(1))
        elif m := re.search(r"\[hw\] storage pet: (.*)", line):
            got["pet_storage"] = m.group(1).strip()
        elif m := re.search(r"\[hw\] boot=(\w+) occupant=(\w+) gen=(\d+) genome=([0-9a-f]{8})", line):
            got.update(boot=m.group(1), occupant=m.group(2), gen=int(m.group(3)), genome=m.group(4))
        elif m := re.search(r"\[hw\] ble name=(\S+)", line):
            got["ble_name"] = m.group(1)
        elif m := re.search(r"\[hw\] adopted species=([a-z]+)", line):
            got["adopted"] = m.group(1)     # a 0.4.0 pet taking his badge's species, just after the BLE name
        if "!!! SEALED" in line or "!!! the pet is not being saved" in line:
            got.setdefault("alarms", []).append(line.strip())
        if m := re.search(r"\[hw\] boot=.* species=([a-z]+)", line):
            got["boot_species"] = m.group(1)     # what booted (`none` when sealed)
    name = got.get("ble_name", "")
    species = got.get("adopted") or got.get("boot_species") or (name if name in SPECIES else "")
    if species in SPECIES:
        got["species"] = species             # what it is now
    return got


def judge_boot(wipe, got):
    """(ok, note): whether the boot is what the mode wants, and why not or what to know."""
    boot = got.get("boot")
    if got.get("alarms"):
        return False, "the boot raised an alarm: %s" % got["alarms"][0]
    if wipe:
        return (True, "") if boot == "Fresh" else (False, "booted %s, not Fresh: the pet was not reset" % boot)
    if boot == "Resumed":
        return True, ""
    if boot == "FellBack":
        return True, "pet resumed from its older snapshot (the newest was torn)"
    if boot == "Fresh" and got.get("pet_storage", "").startswith("formatted"):
        return True, "a new egg: this board never held a pet (%s)" % got["pet_storage"]
    if boot == "Fresh":
        return False, ("PET NOT FOUND: booted Fresh with its pet storage %s. The old pet may still be on the flash "
                       "and in pet_backups/: do not flash this board again, and never with --wipe-pet"
                       % got.get("pet_storage", "?"))
    if boot == "FromLineage":
        return False, "the pet's snapshots would not read: it came back as an egg from its lineage"
    return False, "booted %s: the pet did not come back" % boot


def clashes(rows, mac, species, ble_name):
    """Notes for anything another MAC in the log already has (a species, a BLE name)."""
    notes = []
    for r in rows:
        if r.get("result") != "ok" or r.get("mac") == mac:
            continue
        if species and r.get("species") == species:
            notes.append("DUPLICATE SPECIES %s (badge %s, %s)" % (species, r.get("badge"), r.get("mac")))
        elif ble_name and r.get("ble_name") == ble_name:
            notes.append("duplicate BLE name %s (badge %s, %s)" % (ble_name, r.get("badge"), r.get("mac")))
    return notes


def table_note(mac, species):
    """A note when a badge in the MAC table came up as another species."""
    want = BADGE_SPECIES.get(mac)
    if want and species and species != want:
        return "NOT THE TABLE'S SPECIES: %s is %s, should be %s" % (mac, species, want)
    return None


def fnv1a(data, h=0x811C9DC5):
    """blorb::fnv1a: the species seed a board founds its pet from is fnv1a(MAC bytes)."""
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def esptool_version(text):
    """(major, minor) from `esptool version` ("esptool.py v4.11.0", "esptool v5.0.2"), else None."""
    m = re.search(r"v?(\d+)\.(\d+)", text or "")
    return (int(m.group(1)), int(m.group(2))) if m else None


def find_esptool(home=None, which=shutil.which, windows=os.name == "nt"):
    """The esptool command as an argv prefix: PlatformIO's own, else one on PATH."""
    home = home or Path.home()
    script = home / ".platformio/packages/tool-esptoolpy/esptool.py"
    for py in (home / ".platformio/penv/Scripts/python.exe", home / ".platformio/penv/bin/python"):
        if py.exists() and script.exists():
            return [str(py), str(script)]
    for name in ("esptool", "esptool.py"):
        found = which(name)
        if found:
            return [sys.executable, found] if windows and found.lower().endswith(".py") else [found]
    raise Failed(NO_ESPTOOL)


def find_bash(which=shutil.which, windows=os.name == "nt", program_files=None):
    """Git's bash on Windows (System32's bash.exe is WSL's, which cannot see COM ports), else the system's."""
    if windows:
        for base in (program_files or os.environ.get("ProgramFiles", "C:/Program Files"), "C:/Program Files"):
            p = Path(base) / "Git/bin/bash.exe"
            if p.exists():
                return str(p)
    return which("bash") or "/bin/bash"


def check_access(port, access=os.access):
    """On Linux, refuse early (and say the fix) when this user may not open the port."""
    if port.startswith("/dev/") and not access(port, os.R_OK | os.W_OK):
        raise Failed(PERMISSION_FIX % port)


def denied(text):
    return "Permission denied" in text or "Errno 13" in text or "PermissionError" in text


def read_log(path):
    if not path.exists():
        return []
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def append_log(path, row):
    """One row; a log with an older header (no species, no pet) gets the new columns first."""
    if path.exists():
        with path.open(newline="") as f:
            header = next(csv.reader(f), [])
        if header != FIELDS:
            rows = read_log(path)
            tmp = path.with_suffix(path.suffix + ".tmp")
            with tmp.open("w", newline="") as f:
                w = csv.DictWriter(f, fieldnames=FIELDS + [h for h in header if h not in FIELDS])
                w.writeheader()
                w.writerows(rows)
            os.replace(tmp, path)
            header = FIELDS + [h for h in header if h not in FIELDS]
    else:
        header = FIELDS
    new = not path.exists()
    with path.open("a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=header)
        if new:
            w.writeheader()
        w.writerow({k: row.get(k, "") for k in header})


# ---- the bench: real boards, or pretend ones for --dry-run ---------------------------


class Bench:
    """The real desk: pyserial for ports and the log, esptool and flash_board.sh for writes."""

    def __init__(self, image, boot_seconds):
        self.image, self.boot_seconds = image, boot_seconds
        self.esptool_cmd = find_esptool()
        r = subprocess.run(self.esptool_cmd + ["version"], capture_output=True, text=True)
        v = esptool_version(r.stdout + r.stderr)
        if not v or v < MIN_ESPTOOL:
            raise Failed("%s is esptool %s; this needs %d.%d or newer, which writes a .hex by its segments "
                         "and so leaves the pet alone (pipx install esptool)"
                         % (" ".join(self.esptool_cmd), "%d.%d" % v if v else "of no known version", *MIN_ESPTOOL))
        self.bash = find_bash()

    def badges(self):
        from serial.tools import list_ports
        return badge_ports(list_ports.comports())

    def esptool(self, port, *args):
        r = subprocess.run(self.esptool_cmd + ["--chip", "esp32s3", "--port", port, *args],
                           capture_output=True, text=True)
        if r.returncode != 0 and denied(r.stdout + r.stderr):
            raise Failed(PERMISSION_FIX % port)
        return r

    def backup(self, port, mac):
        BACKUPS.mkdir(exist_ok=True)
        out = BACKUPS / ("%s-%s.bin" % (mac.replace(":", ""), datetime.now().strftime("%Y%m%d-%H%M%S")))
        r = self.esptool(port, "-b", "921600", "read_flash", hex(PET_NVS[0]), hex(PET_NVS[1]), str(out))
        if r.returncode != 0 or not out.exists() or out.stat().st_size != PET_NVS[1]:
            tail = (r.stdout + r.stderr).strip().splitlines()[-2:]
            raise Failed("could not back up the pet partition, so nothing was written: %s" % " / ".join(tail))
        return out

    def mac(self, port):
        r = self.esptool(port, "read_mac")
        m = re.search(r"^MAC:\s*([0-9a-f:]{17})", r.stdout, re.M)
        if not m:
            tail = (r.stdout + r.stderr).strip().splitlines()[-3:]
            raise Failed("esptool could not read the MAC on %s: %s" % (port, " / ".join(tail)))
        return m.group(1)

    def flash(self, port, serial, mac, wipe):
        cmd = [self.bash, str(ROOT / "tools/flash_board.sh"), *(["--wipe-pet"] if wipe else []), "--no-reset",
               port, serial, mac, self.image.as_posix()]
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        out = (r.stdout + r.stderr).strip().splitlines()
        for line in out:
            if line.startswith("flash_board:") or "Wrote" in line or "Hash of data verified" in line:
                print("    " + line)
        if r.returncode != 0:
            raise Failed("flash_board.sh failed (exit %d): %s" % (r.returncode, " / ".join(out[-3:])))

    def boot(self, port):
        """Boot it with the log open (tools/serial_log.py's RTS pulse) and read until the BLE name,
        then up to 5 s more for a species adoption."""
        import serial
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, 115200, 0.2
        s.dtr = s.rts = False
        try:
            s.open()
        except serial.SerialException as e:
            raise Failed(PERMISSION_FIX % port if denied(str(e)) else "could not open %s: %s" % (port, e))
        try:
            s.rts = True
            time.sleep(0.1)
            s.rts = False
            lines, buf, t0, named = [], b"", time.monotonic(), None
            while time.monotonic() - t0 < self.boot_seconds:
                buf += s.read(4096)
                *whole, buf = buf.split(b"\n")
                lines += [w.decode("utf-8", "replace").rstrip("\r") for w in whole]
                if named is None and any("[hw] ble name=" in line for line in lines):
                    named = time.monotonic()
                if named and (time.monotonic() - named > 5 or any("[hw] adopted" in line for line in lines)):
                    break
            return lines
        finally:
            s.close()


class DryBench:
    """Pretend boards: a port turns up a moment after Enter and leaves when asked. Nothing is written.

    The first eight are the MAC table's badges, in order; later ones get made-up MACs."""

    def __init__(self, wipe=False):
        self.wipe = wipe
        self.attached = {}
        self.rng = random.SystemRandom()
        self.n = 0
        self.known = list(BADGE_SPECIES)

    def badges(self):
        return dict(self.attached)

    def plug(self):
        self.n += 1
        self.attached[("COM%d" if os.name == "nt" else "/dev/ttyACM%d") % (20 + self.n)] = "DRY%08d" % self.n

    def unplug(self):
        self.attached.clear()

    def mac(self, port):
        self.last = self.known.pop(0) if self.known else ":".join(
            "%02x" % b for b in [0xD4, 0x05, 0x92] + [self.rng.randrange(256) for _ in range(3)])
        return self.last

    def backup(self, port, mac):
        return BACKUPS / ("%s-dry-run.bin" % mac.replace(":", ""))

    def flash(self, port, serial, mac, wipe):
        print("    (dry run) would run: tools/flash_board.sh%s --no-reset %s %s %s merged.hex"
              % (" --wipe-pet" if wipe else "", port, serial, mac))

    def boot(self, port):
        """0.7.0's first boot: a fresh egg, or a 0.4.0 pet that resumes and adopts his badge's species.
        The genome stands in as its MAC seed (the real one is the board's)."""
        raw = bytes.fromhex(self.last.replace(":", ""))
        species = BADGE_SPECIES.get(self.last) or SPECIES[fnv1a(raw) % len(SPECIES)]
        how = ("formatted: it was blank", "Fresh", "egg", 0) if self.wipe else ("ok", "Resumed", "creature", 86400)
        lines = ["[hw] blorbarium dry-run fw=0.7.0 fmt=1 badge128",
                 "[hw] storage pet: %s" % how[0],
                 "[hw] boot=%s occupant=%s gen=0 genome=%08x age=%d species=%s"
                 % (how[1], how[2], fnv1a(raw), how[3], species if self.wipe else "grungo"),
                 "[hw] ble name=%s heap=0 big=0" % species]
        if not self.wipe and self.last in BADGE_SPECIES:
            lines.append("[hw] adopted species=%s gen=0 genome=%08x name=Pip (the Bluetooth name follows at the next boot)"
                         % (species, fnv1a(raw[::-1])))
        return lines


# ---- the loop ------------------------------------------------------------------------


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
        raise Failed("no badge found (%s). Is the cable a data cable? On Linux, is it /dev/ttyACM*"
                     " (ls /dev/ttyACM*)? On Windows, is the CH343 driver installed?" % how)
    return port, how


def wait_gone(bench, port):
    while port in bench.badges():
        time.sleep(0.3)


def one_badge(bench, n, port, rows, wipe=False):
    serial = bench.badges().get(port, "")
    if not serial:
        raise Failed("%s has no USB serial number; flash_board.sh needs one to be sure of the board" % port)
    if isinstance(bench, Bench):
        check_access(port)
    mac = bench.mac(port)
    want = BADGE_SPECIES.get(mac)
    print("  %s  usb %s  mac %s  (%s)" % (port, serial, mac, "the table's %s" % want if want else
                                          "not in the MAC table: seed %08x" % fnv1a(bytes.fromhex(mac.replace(":", "")))))
    print("  pet backed up to %s" % bench.backup(port, mac))
    bench.flash(port, serial, mac, wipe)
    print("  booting it with the log open ...")
    got = parse_boot(bench.boot(port))
    missing = [k for k in ("boot", "genome", "ble_name") if k not in got]
    if missing:
        raise Failed("flashed, but the boot log never showed %s; it may not have booted" % ", ".join(missing))
    ok, why = judge_boot(wipe, got)
    if not ok:
        raise Failed("%s (genome %s)" % (why, got["genome"]))
    species = got.get("species", "")
    info = [why] if why else []
    if got.get("adopted"):
        info.append("the pet adopted species %s" % got["adopted"])
    warnings = clashes(rows, mac, species, got["ble_name"]) + [t for t in [table_note(mac, species)] if t]
    return {"time": datetime.now().isoformat(timespec="seconds"), "badge": n, "port": port, "usb_serial": serial,
            "mac": mac, "ble_name": got["ble_name"], "boot": got["boot"], "genome": got["genome"],
            "fw": got.get("fw", ""), "result": "ok", "note": "; ".join(info + warnings), "species": species,
            "pet": "wipe" if wipe else "keep", "info": info, "warnings": warnings}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--start", type=int, default=1, help="the first badge's number (default 1)")
    ap.add_argument("--image", type=Path, default=IMAGE, help="merged .hex to write (default %(default)s)")
    ap.add_argument("--log", type=Path, default=ROOT / "flash_log.csv", help="CSV to append to (default %(default)s)")
    ap.add_argument("--wait", type=float, default=20, help="seconds to wait for a port after Enter (default 20)")
    ap.add_argument("--boot-seconds", type=float, default=45, help="how long to read the boot log (default 45)")
    ap.add_argument("--wipe-pet", action="store_true",
                    help="erase each badge's pet (backed up first) so it founds a new one; it must boot Fresh")
    ap.add_argument("--daemonctl", default=str(DAEMONCTL), help="badge daemon control script to hold off ('' for none)")
    ap.add_argument("--dry-run", action="store_true", help="pretend boards; writes nothing, logs to a .dry-run.csv")
    args = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)   # each line shows at once, even through a pipe or tee

    mode = ("WIPING each badge's pet (backed up first): it must boot Fresh" if args.wipe_pet else
            "KEEPING each badge's pet: it must boot Resumed (--wipe-pet resets instead)")
    if args.dry_run:
        bench, log = DryBench(args.wipe_pet), args.log.with_suffix(".dry-run.csv")
        try:
            print("esptool: %s" % " ".join(find_esptool()))
        except Failed as e:
            print("esptool: %s" % e)
        print("bash: %s" % find_bash())
    else:
        if not args.image.exists():
            sys.exit("no %s: build it first with  pio run -e badge128" % args.image)
        if args.image.suffix != ".hex":
            sys.exit("%s is not a merged .hex; a flat image erases the pet partitions" % args.image)
        try:
            bench, log = Bench(args.image, args.boot_seconds), args.log
        except Failed as e:
            sys.exit(str(e))
        hold_daemon(args.daemonctl)
    print("Writing %s; logging to %s" % (args.image if not args.dry_run else "nothing (dry run)", log))
    print(mode)
    pet = "wipe pet" if args.wipe_pet else "keep pet"

    n = args.start
    while True:
        hold_daemon(None if args.dry_run else args.daemonctl)
        before = bench.badges()
        if ask("\nPlug in badge %d (%s), press Enter (q to quit): " % (n, pet)) == "q":
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
                row = one_badge(bench, n, port, read_log(log), args.wipe_pet)
                break
            except Failed as e:
                print("  FAILED badge %d: %s" % (n, e))
                append_log(log, {"time": datetime.now().isoformat(timespec="seconds"), "badge": n,
                                 "port": port or "", "result": "failed", "note": str(e),
                                 "pet": "wipe" if args.wipe_pet else "keep"})
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
            print("  OK badge %d: %s  mac %s  %s  species %s  genome %s  boot=%s  pet %s%s" % (
                n, row["port"], row["mac"], row["ble_name"], row["species"] or "?", row["genome"], row["boot"],
                row["pet"], "".join("  (%s)" % i for i in row["info"]) + "".join("  !! " + w for w in row["warnings"])))
        if port:
            print("Unplug badge %d" % n)
            if isinstance(bench, DryBench):
                bench.unplug()
            wait_gone(bench, port)
        n += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
