# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial", "bleak>=0.22", "cryptography"]
# ///
"""Do a badge's saved networks survive a real update? One cabled badge (moth).

    uv run scripts/test/upgrade_keeps.py /dev/ttyACM0 --from releases/0.7.32/blorbarium-lcd128-0.7.32.bin \
        --to 0.7.35 --via wifi --manifest-url https://blorb.hypnodroid.com/fw/lcd128-test/manifest.json --log .jobs/up.log
    uv run scripts/test/upgrade_keeps.py /dev/ttyACM0 --from <bin> --to 0.7.35 --via ble \
        --manifest releases/0.7.35/manifest.json --log .jobs/up.log
    uv run scripts/test/upgrade_keeps.py /dev/ttyACM0 --no-flash --to 0.7.36 ...   # the badge as it is

1. The old image by the cable, app only (boot_app0 at 0xe000, the app at
   0x10000): NVS is left alone, as an update leaves it.
2. Its home, then two networks by the cable: a dummy, then this laptop's
   2.4 GHz network (#1). The badge's list says 2.
3. The update: over Wi-Fi (`DEBUG ota check <manifest>`) or over Bluetooth
   (tools/ble_ota.py push, as the website does). Every question the badge
   asks is answered by the cable's `DEBUG press` (the button), never a hand.
4. It boots the new version on trial, keeps it (trial=0), and its list
   still holds both networks, by length (the 19 B one and the 10 B dummy),
   its home still stored. Then a forced check still joins.

Nothing secret is printed or logged (the masking of scripts/test/moth_0735.py).
"""
import argparse
import asyncio
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "scripts", "test"))
from power_e2e import Board, Fail  # noqa: E402
from moth_0735 import Masked, laptop_network, secrets  # noqa: E402

DUMMY = ("blorbdummy", "notapassword1")
BOOT = r"\[hw\] blorbarium .*fw=(\S+)"
ASK = re.compile(r"Tip me|Turn me|asks? for a yes|press the button", re.I)


def flash(port, image, erase_nvs):
    if erase_nvs:   # the nvs partition (0x9000, 20 KB) only: a field badge's, as the old image alone wrote it
        r = subprocess.run(["uv", "run", "-q", "--with", "esptool==4.5.1", "esptool.py", "--chip", "esp32s3", "--port",
                            port, "--baud", "115200", "--after", "no_reset", "erase_region", "0x9000", "0x5000"],
                           capture_output=True, text=True, cwd=ROOT)
        if r.returncode != 0:
            return False, (r.stdout + r.stderr).strip()[-300:]
    cmd = ["uv", "run", "-q", "--with", "esptool==4.5.1", "esptool.py", "--chip", "esp32s3", "--port", port,
           "--baud", "115200", "--before", "default_reset", "--after", "hard_reset", "write_flash", "-z",
           "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
           "0xe000", os.path.join(ROOT, "outputs/station/boot_app0.bin"), "0x10000", image]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    return r.returncode == 0 and r.stdout.count("Hash of data verified.") == 2, (r.stdout + r.stderr).strip()[-300:]


class Up:
    def __init__(self, b, args, masked, net):
        self.b, self.args, self.masked, self.net = b, args, masked, net
        self.results = []

    def say(self, s):
        s = self.masked.mask(s)
        print(s, flush=True)
        self.b.note(s)

    def check(self, ok, what):
        self.results.append((ok, what))
        self.say(("  PASS " if ok else "  FAIL ") + what)
        return ok

    def cable(self, line, want, timeout=6):
        mark = self.b.mark()
        self.b.send(line)
        return self.b.wait(want, timeout, mark)

    def secret_cable(self, verb, ssid, pw, i):
        mark = self.b.mark()
        self.b.note('>> DEBUG wifi %s "<ssid%d>" <password%d>' % (verb, i, i))
        self.b.p.write(b"\r" * 8)
        time.sleep(0.06)
        self.b.p.write(('DEBUG wifi %s "%s" %s\n' % (verb, ssid, pw)).encode())
        return self.b.wait(r"\[wifi\] (network .*saved.*|verify.*)", 6, mark)

    def wifi(self):
        mark = self.b.mark()
        self.b.send("DEBUG wifi")
        self.b.wait(r"\[wifi\] networks \d+", 6, mark)
        time.sleep(0.3)
        return [l for _, l in self.b.lines[mark:] if l.startswith("[wifi] ")]

    def networks(self, lines):
        n = next((re.search(r"networks[ =](\d+)", l) for l in lines if re.search(r"\[wifi\] networks \d+", l)), None) or \
            next((re.search(r"networks=(\d+)", l) for l in lines if "networks=" in l), None)   # before 0.7.34: the count only
        sizes = [re.search(r"ssid (\d+) B", l).group(1) for l in lines if re.search(r"network #\d+: ssid \d+ B", l)]
        if not n:   # before 0.7.2x: networks=["<ssid>" ...], the names themselves (masked here)
            q = next((re.search(r"networks=\[(.*?)\] auto", l) for l in lines if "networks=[" in l), None)
            if q:
                return q.group(1).count('"') // 2, [], next(
                    (re.search(r"trial=(\d)", l).group(1) for l in lines if "trial=" in l), "?")
        trial = next((re.search(r"trial=(\d)", l).group(1) for l in lines if "trial=" in l), "?")
        return (int(n.group(1)) if n else -1), sizes, trial

    def answer_questions(self, since):
        """Press for every question asked since `since` (the button: a yes on every version):
        on the serial log, and in tools/ble_ota.py's own words ("waiting up to")."""
        i, seen = since, 0
        while True:
            try:
                ble = open(self.args.log + ".ble").read().count(">>> waiting up to")
            except OSError:
                ble = 0
            if ble > seen:
                seen = ble
                self.say("  ble_ota says the badge asks: the cable presses")
                time.sleep(1.8)   # a press in the first 1.5 s is no yes
                self.b.send("DEBUG press 150")
            with self.b.cv:
                lines = self.b.lines[i:]
            for _, l in lines:
                i += 1
                if ASK.search(l) and not l.startswith("[hw] debug") and "improv" not in l and "consent=" not in l:
                    self.say("  the badge asks (%s): the cable presses" % l[:90])
                    time.sleep(1.8)   # a press in the first 1.5 s is no yes
                    self.b.send("DEBUG press 150")
            yield

    async def main(self):
        b, a = self.b, self.args
        boot = b.wait(BOOT, 25, since=0)
        if not boot:
            raise Fail("no boot line")
        self.say("booted fw=%s" % boot.group(1))
        name = b.wait(r"\[hw\] ble name=(\S+)", 10, since=0).group(1)
        b.wait(r"\[hw\] ble adv on", 10, since=0)
        time.sleep(3)
        if not a.no_flash:
            h = self.cable("DEBUG home %s" % a.home, r"\[hw\] home rot=.*")
            self.say("home: %s" % (h.group(0) if h else "(no answer)"))
            f = self.cable("DEBUG wifi forget", r"\[wifi\] forgot .*")
            self.say("forget: %s" % (f.group(0) if f else "(no answer)"))
            for i, (ssid, pw) in enumerate([DUMMY, self.net]):
                r = self.secret_cable("set", ssid, pw, i)
                self.say("set #%d: %s" % (i, r.group(0) if r else "(no answer)"))
        n, sizes, trial = self.networks(self.wifi())
        self.check(n == 2 and (not sizes or sorted(sizes) == ["10", "19"]), "before: %d networks (ssid sizes %s) on %s" % (
            n, ",".join(sizes), boot.group(1)))

        if a.power_low:   # an old image on the cable may never go dark by itself
            time.sleep(2)
            self.b.send("DEBUG power low")
        if a.wait_dark:
            self.say("waiting %d s for the badge to go dark and alone" % a.wait_dark)
            time.sleep(a.wait_dark)
        self.say("== the update over %s to %s" % (a.via, a.to))
        mark = b.mark()
        answers = self.answer_questions(mark)
        proc = None
        if a.via == "wifi" and not a.no_ask:
            b.send("DEBUG ota check %s" % a.manifest_url)
        elif a.via == "ble":
            proc = subprocess.Popen(["uv", "run", "-q", os.path.join(ROOT, "tools/ble_ota.py"), "push", name, "--no-prompt",
                                     "--manifest", a.manifest], cwd=ROOT, stdout=open(a.log + ".ble", "w"), env=dict(os.environ, PYTHONUNBUFFERED="1"),
                                    stderr=subprocess.STDOUT)
        end = time.monotonic() + a.timeout
        new = None
        tries, failed_at = 1, mark
        while time.monotonic() < end:
            next(answers)
            f = b.wait(r"\[wifi\] forced: FAILED.*", 0.01, failed_at)
            if f and a.via == "wifi" and not a.no_ask and tries < 5:
                self.say("  try %d failed (%s): again in 20 s" % (tries, f.group(0)))
                failed_at = b.mark()
                time.sleep(20)
                tries += 1
                b.send("DEBUG ota check %s" % a.manifest_url)
            m = b.wait(BOOT, 1, mark)
            if m and m.group(1).startswith(a.to):
                new = m
                break
            if proc and proc.poll() is not None and not b.wait(BOOT, 0.1, mark):
                pass
        self.check(bool(new), "booted %s after the update" % (new.group(1) if new else "NOTHING new"))
        if not new:
            return
        t_new = b.mark()
        home = b.wait(r"\[hw\] home rot=.*|stored (up|down)", 5, mark)
        kept, trial = None, "?"
        end = time.monotonic() + a.timeout
        while time.monotonic() < end:
            next(answers)
            back = b.wait(BOOT, 0.1, t_new)
            if back and not back.group(1).startswith(a.to):
                self.check(False, "ROLLED BACK to %s" % back.group(1))
                return
            if proc and proc.poll() is None:
                time.sleep(2)
                continue
            n, sizes, trial = self.networks(self.wifi())
            if trial == "0":
                kept = True
                break
            time.sleep(10)
        if proc:
            proc.wait(timeout=30)
            tail = open(a.log + ".ble").read().strip().splitlines()[-3:]
            self.say("ble_ota: rc=%s; %s" % (proc.returncode, " | ".join(self.masked.mask(t) for t in tail)))
        self.check(bool(kept), "the trial %s (trial=%s)" % ("kept the new image" if kept else "did NOT end", trial))
        lines = self.wifi()
        for l in lines:
            self.say("  " + l)
        n, sizes, trial = self.networks(lines)
        self.check(n == 2 and (not sizes or sorted(sizes) == ["10", "19"]), "after: %d networks (ssid sizes %s) on %s" % (
            n, ",".join(sizes), new.group(1)))
        h = self.cable("DEBUG home", r"\[hw\] home rot=.*")
        self.check(bool(h and a.home in h.group(0)), "home after: %s" % (h.group(0) if h else "(no answer)"))
        if a.check_after:
            mark = b.mark()
            b.send("DEBUG ota check")
            j = b.wait(r"\[wifi\] (joined in .*|forced: FAILED.*)", 90, mark)
            c = b.wait(r"\[wifi\] window forced closed.*", 60, mark)
            self.check(bool(j and "joined in" in j.group(0)), "a forced check after: %s" % (j.group(0) if j else "(none)"))


async def amain(args):
    net = laptop_network()
    if not net:
        sys.exit("this laptop has no 2.4 GHz network here")
    masked = Masked(open(args.log, "w"), secrets(net) + list(DUMMY))
    if not args.no_flash:
        ok, out = flash(args.port, args.frm, args.erase_nvs)
        masked.write("flash %s: %s\n" % (args.frm, out))
        print("flash %s: %s" % (os.path.basename(args.frm), "ok" if ok else "FAILED " + out[-200:]), flush=True)
        if not ok:
            return 2
    b = Board(args.port, masked)
    up = Up(b, args, masked, net)
    try:
        await up.main()
    except Fail as e:
        up.check(False, "stopped: %s" % e)
    up.say("crashes: %d%s" % (len(b.crashes), "".join("\n  " + c for c in b.crashes[:5])))
    up.say("secrets seen in captured text: %d" % len(masked.hits))
    ok = all(r[0] for r in up.results) and not b.crashes
    up.say("RESULT %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("port")
    p.add_argument("--from", dest="frm")
    p.add_argument("--no-flash", action="store_true")
    p.add_argument("--erase-nvs", action="store_true", help="empty the nvs partition first (not the pet's)")
    p.add_argument("--to", required=True)
    p.add_argument("--via", choices=["wifi", "ble"], required=True)
    p.add_argument("--manifest-url", default="https://blorb.hypnodroid.com/fw/lcd128-test/manifest.json")
    p.add_argument("--manifest")
    p.add_argument("--home", default="down")
    p.add_argument("--timeout", type=int, default=600)
    p.add_argument("--check-after", action="store_true")
    p.add_argument("--wait-dark", type=int, default=0, help="seconds to leave it alone before the update")
    p.add_argument("--power-low", action="store_true", help="DEBUG power low before the update")
    p.add_argument("--no-ask", action="store_true", help="leave the update to the badge's own check (no DEBUG ota check)")
    p.add_argument("--log", required=True)
    sys.exit(asyncio.run(amain(p.parse_args())))
