# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial", "esptool==4.5.1"]
# ///
"""The flash station: every badge, one at a time, onto the release the live
site serves, flipped where it must be, on the home Wi-Fi, checked (docs/ota.md
"Pre-flight").

    uv run scripts/flash_station.py prep     # once: build badge128, keep its images in outputs/station/
    uv run scripts/flash_station.py          # then plug badges in one at a time; Ctrl-C to stop
    uv run scripts/flash_station.py --once   # one badge and exit

For each badge that appears in /dev/serial/by-id (a CH343, 1a86:55d3; the
Flipper and anything else are never touched):
  1. writes the prebuilt badge128 image as `pio run -t upload` does (bootloader,
     partition table, boot_app0, app): the pet and NVS stay;
  2. checks the boot line says the prebuilt version;
  3. the flipped badges (5C94094497, 5C94094999, 5C94095003): `DEBUG home down`,
     and checks `home rot=0 (USB-C down, nvs`; every other badge `DEBUG home up`.
     The home lives in NVS, which a release never touches;
  4. gives it the Wi-Fi in .env (ssid=, password=; never printed: lines are
     shown with them masked) and checks it is remembered and matches;
  5. `DEBUG ota check` against the live manifest: it must say `up_to_date`;
  6. prints one line: PASS or FAIL <serial> <why>.

The image is one for every 1.28 (releases are badge128 only); `prep` warns when
its version is not the one the live manifest serves, so the flashed badges and
the site agree and every later release is an upgrade.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "outputs" / "station"
BYID = Path("/dev/serial/by-id")
FLIPPED = {"5C94094497", "5C94094999", "5C94095003"}
LIVE = "https://blorb.hypnodroid.com/fw/lcd128/manifest.json"
MAIN_ENV = Path.home() / "Projects" / "blorbarium" / ".env"
IMAGES = [("0x0", "bootloader.bin"), ("0x8000", "partitions.bin"), ("0xe000", "boot_app0.bin"), ("0x10000", "firmware.bin")]


def say(msg):
    print(time.strftime("%H:%M:%S ") + msg, flush=True)


def live_version():
    try:
        req = urllib.request.Request(LIVE, headers={"User-Agent": "blorbarium-flash-station"})   # the site refuses Python's own
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.load(r).get("version")
    except Exception as e:  # noqa: BLE001
        return f"unknown ({e})"


def source_version():
    m = re.search(r'kFirmwareVersion = "([\d.]+)"', (ROOT / "lib/blorb/include/blorb/protocol.h").read_text())
    return m.group(1)


def prep(args):
    pio = os.environ.get("BLORB_PIO") or str(Path.home() / ".platformio/penv/bin/pio")
    if not args.no_build:
        subprocess.run([pio, "run", "-e", "badge128"], cwd=ROOT, check=True)
    build = ROOT / ".pio/build/badge128"
    OUT.mkdir(parents=True, exist_ok=True)
    boot_app0 = Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    for src in (build / "bootloader.bin", build / "partitions.bin", boot_app0, build / "firmware.bin"):
        shutil.copy(src, OUT / src.name)
    version = source_version()
    (OUT / "version").write_text(version + "\n")
    live = live_version()
    say(f"prepared badge128 {version} in {OUT.relative_to(ROOT)}; the live manifest serves {live}")
    if live != version:
        say(f"WARNING: the live site serves {live}, not {version}: release {version} live "
            "(tools/ota_release.py ship) so the badges and the site agree")


def read_env(path):
    vals = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            v = v.strip()
            if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
                v = v[1:-1]
            vals[k.strip().lower()] = v
    if not vals.get("ssid"):
        sys.exit(f"{path} has no ssid= line")
    return vals["ssid"], vals.get("password", "")


def ch343_ports():
    """{serial: by-id path} for every CH343 badge plugged in."""
    found = {}
    if not BYID.exists():
        return found
    for p in BYID.iterdir():
        m = re.fullmatch(r"usb-1a86_USB_Single_Serial_([0-9A-Fa-f]+)-if00", p.name)
        if not m:
            continue   # the Flipper (0483:5740) and anything else: never touched
        dev = Path(os.path.realpath(p))
        sysdev = Path(os.path.realpath(f"/sys/class/tty/{dev.name}/device"))
        ids = [(d / "idVendor", d / "idProduct") for d in sysdev.parents if (d / "idVendor").exists()]
        if ids and (ids[0][0].read_text().strip(), ids[0][1].read_text().strip()) != ("1a86", "55d3"):
            continue
        found[m.group(1).upper()] = str(p)
    return found


class Console:
    def __init__(self, port, secrets):
        import serial
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.2
        self.s.dtr = self.s.rts = False   # toggling them resets the board
        self.s.open()
        self.buf, self.secrets, self.log = b"", secrets, []

    def mask(self, t):
        for v, m in self.secrets:
            if v:
                t = t.replace(v, m)
        return t

    def send(self, line, shown=None):
        self.s.write(line.encode() + b"\n")
        self.log.append(">>> " + (shown or line))

    def until(self, seconds, pred):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.buf += self.s.read(4096)
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                t = raw.decode(errors="replace").rstrip("\r")
                self.log.append(self.mask(t))
                if pred(t):
                    return t
        return None

    def close(self):
        self.s.close()


def station_one(serial_no, port, ssid, password, verbose):
    version = (OUT / "version").read_text().strip()
    flipped = serial_no in FLIPPED
    say(f"{serial_no}: {'FLIPPED (USB-C down is up)' if flipped else 'upright'}; writing badge128 {version}")
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port, "--baud", "115200",   # the owner: 115200 always (faster failed on some boards)
           "--before", "default_reset", "--after", "hard_reset", "write_flash", "-z",
           "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"]
    for off, name in IMAGES:
        cmd += [off, str(OUT / name)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:   # a tired cable or board: once more
        say(f"{serial_no}: flash failed; once more")
        r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or r.stdout.count("Hash of data verified.") != len(IMAGES):
        return False, "flash failed: " + (r.stdout + r.stderr).strip().splitlines()[-1][:160]
    mac = re.search(r"MAC: ([0-9a-f:]+)", r.stdout)
    c = Console(port, [(password, "<password>"), (ssid, "<ssid>")])
    try:
        boot = c.until(25, lambda t: " fw=" in t and t.startswith("[hw] blorbarium"))
        if not boot:
            return False, "no boot line in 25 s"
        if f"fw={version} " not in boot + " ":
            return False, f"booted {boot.split('fw=')[1].split()[0]}, not {version}"
        c.until(4, lambda t: t.startswith("[hw] ble name="))   # let it finish booting
        want = "down" if flipped else "up"
        c.send(f"DEBUG home {want}")
        home = c.until(6, lambda t: t.startswith("[hw] home rot="))
        if not home or not home.startswith(f"[hw] home rot={0 if flipped else 2} (USB-C {want}, nvs"):
            return False, f"home: {home!r}"
        # Fed and mended before it goes (the owner, 2026-10-27: no pet starves
        # before the feeding fix arrives over the air).
        fed = []
        for chem in ("hunger", "pain", "injury"):
            c.send(f"DEBUG chem {chem} 0")
            got = c.until(4, lambda t: t.startswith(f"[hw] debug chem {chem}"))
            fed.append(got.split("debug chem ")[1] if got else f"{chem}=?")
        if all("no creature" in f for f in fed):
            fed = ["egg"]   # an egg or an empty dish: nothing to feed
        elif any(not f.endswith("=0") for f in fed):
            return False, f"feed: {' '.join(fed)}"
        c.send(f'DEBUG wifi set "{ssid}" {password}', 'DEBUG wifi set "<ssid>" <password>')
        if not c.until(6, lambda t: t.startswith("[wifi] network ") and "saved" in t):
            return False, "wifi set: no saved line"
        c.send(f'DEBUG wifi verify "{ssid}" {password}', 'DEBUG wifi verify "<ssid>" <password>')
        v = c.until(6, lambda t: t.startswith("[wifi] verify"))
        if not v or "password MATCHES" not in v:
            return False, f"wifi verify: {c.mask(v or 'no answer')}"
        for attempt in range(5):
            c.send("DEBUG ota check")
            got = c.until(120, lambda t: t.startswith("[wifi] manifest ") or "forced: FAILED" in t)
            if got and t_ok(got):
                c.until(30, lambda t: "window forced closed" in t)
                return True, f"fw={version} home={want} fed={','.join(fed)} wifi=ok live={got.split()[3]} up_to_date mac={mac.group(1) if mac else '?'}"
            if got and got.startswith("[wifi] manifest "):
                return False, f"ota check: {c.mask(got)}"
            c.until(30, lambda t: "window forced closed" in t)
        return False, f"ota check failed 5 times: {c.mask(got or 'no answer')}"
    finally:
        if verbose:
            print("\n".join("    " + l for l in c.log), flush=True)
        c.close()


def t_ok(line):
    return line.startswith("[wifi] manifest ") and ": up_to_date " in line


def run(args):
    if not (OUT / "version").exists():
        sys.exit("run `uv run scripts/flash_station.py prep` first")
    env = Path(args.env).expanduser() if args.env else (ROOT / ".env" if (ROOT / ".env").exists() else MAIN_ENV)
    ssid, password = read_env(env)
    version = (OUT / "version").read_text().strip()
    live = live_version()
    say(f"station: badge128 {version} (live site: {live}); plug badges in one at a time")
    if live != version:
        say(f"WARNING: the live site serves {live}: the OTA check will not say up_to_date")
    done = set(ch343_ports()) if not args.include_present else set()
    if done:
        say(f"already plugged in, skipped (unplug and replug to do them): {', '.join(sorted(done))}")
    results = []
    try:
        while True:
            now = ch343_ports()
            done &= set(now)   # unplugged: may come back for another go
            new = [s for s in now if s not in done]
            if not new:
                time.sleep(0.5)
                continue
            s = new[0]
            done.add(s)
            time.sleep(1.5)   # let it enumerate
            try:
                ok, why = station_one(s, now[s], ssid, password, args.verbose)
            except Exception as e:  # noqa: BLE001
                ok, why = False, f"{type(e).__name__}: {e}"
            line = f"{'PASS' if ok else 'FAIL'} {s} {why}"
            results.append(line)
            say(line)
            say("unplug it; plug in the next" if not args.once else "done")
            if args.once:
                break
    except KeyboardInterrupt:
        pass
    print("\n".join(results))
    sys.exit(0 if results and all(l.startswith("PASS") for l in results) else 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", nargs="?", default="run", choices=["run", "prep"])
    ap.add_argument("--no-build", action="store_true", help="prep: take the badge128 images already built")
    ap.add_argument("--once", action="store_true", help="one badge, then exit")
    ap.add_argument("--include-present", action="store_true", help="also do the badges already plugged in at start")
    ap.add_argument("--env", help="the .env with ssid= and password= (default: this checkout's, else ~/Projects/blorbarium/.env)")
    ap.add_argument("-v", "--verbose", action="store_true", help="print each badge's console, masked")
    args = ap.parse_args()
    prep(args) if args.cmd == "prep" else run(args)


if __name__ == "__main__":
    main()
