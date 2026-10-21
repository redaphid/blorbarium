# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""How fast each control shows on a real badge, over its cable (docs/notes/controls-latency.md).

    uv run scripts/test/latency_probe.py /dev/ttyACM0
    uv run scripts/test/latency_probe.py /dev/ttyACM0 --reps 8 --only knock_knock,hang --log lat.log

Each control is made through the raw IMU path by the cable's injectors
(`DEBUG knock 1|2`, `side`, `hang`, `press`, `double`, `lean`, `shake`), so the
real detectors decide, and the badge times every stage (src/latency.h): from
the decisive raw read (In) to the loop taking it (seen), the detector acting
(detect), the first frame acknowledging it and its rows on the panel (ack,
ack_panel), the word controls.h names (word), the engine tick that took it
(tick), and the first frame after that and its rows out (frame, panel).

Three phases, each read back with `DEBUG latency` and cleared first:
  rest   awake on the dish: every control N times
  fight  fight mode (a hang opens the lobby): knock knock, side hold, tilt,
         then a hang to leave
  low    `DEBUG power low`, then a knock knock: the wake, to the first frame
         and the light

Prints a table per phase (p50/p95 ms after In) and PASS/FAIL per control
against TARGETS below. The port is opened once and held: on Linux an open
resets the board. A pet that is an egg is hatched first (`DEBUG hatch`).
"""
import argparse
import re
import sys
import threading
import time

import serial

CRASH = re.compile(r"Guru Meditation|abort\(\)|Backtrace:|panic|Brownout|stack overflow|WDT|rst:0x(7|8|9|f|10) ")
LAT = re.compile(r"\[lat\] (\w+) n=(\d+) words=(\d+) silent=(\d+) noack=(\d+) (.*)")
STAGES = ["seen", "detect", "ack", "ack_panel", "word", "tick", "frame", "panel"]

# ms after In, p95. Why each (docs/notes/controls-latency.md):
#   ack_panel  the acknowledgement on the glass: 50 ms is "instant" to a hand
#              (a 20 ms frame, the 20 ms sample, the queue and the send);
#   panel      the engine's answer on the glass: the word, then up to one
#              100 ms tick, a frame and its send.
TARGETS = {
    "knock_knock": {"ack_panel": 50, "panel": 800},   # the flash on the 2nd knock; the pellet waits out a 3rd knock
    "knock": {"ack_panel": 50},                       # "one more" on the first knock
    "side": {"ack_panel": 50},                        # the hold ring from the turn
    "hang": {"ack_panel": 50},                        # the hang's charge from the turn
    "press": {"ack_panel": 50, "panel": 400},         # the ring on BOOT down; the pellet after the release
    "double": {"panel": 900},
    "shake": {"panel": 200},                          # decided on its last jolt: the prophecy's first frame
    "tilt": {"word": 400},                            # the aim read off the dead zone
    "wake": {"panel": 400, "ack_panel": 400},          # the first frame on the glass, and the light
}


class Board:
    def __init__(self, port, log):
        self.t0, self.log = time.monotonic(), log
        self.p = serial.Serial()
        self.p.port, self.p.baudrate, self.p.timeout = port, 115200, 0.1
        self.p.dtr = self.p.rts = False
        self.p.open()
        self.lines, self.crashes = [], []
        self.cv = threading.Condition()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        buf = b""
        while True:
            buf += self.p.read(4096)
            *whole, buf = buf.split(b"\n")
            for raw in whole:
                line = raw.decode("utf-8", "replace").rstrip("\r")
                if CRASH.search(line):
                    self.crashes.append(line)
                self.log.write("%8.2f    %s\n" % (time.monotonic() - self.t0, line))
                self.log.flush()
                with self.cv:
                    self.lines.append(line)
                    self.cv.notify_all()

    def send(self, line):
        self.log.write("%8.2f >> %s\n" % (time.monotonic() - self.t0, line))
        # badge128_pm light-sleeps in low power and loses a line's first bytes: '\r's first.
        self.p.write(b"\r" * 8)
        time.sleep(0.06)
        self.p.write(line.encode() + b"\n")

    def ask(self, line, pattern, timeout=3.0, need=True):
        with self.cv:
            i = len(self.lines)
        self.send(line)
        rx, end = re.compile(pattern), time.monotonic() + timeout
        with self.cv:
            while True:
                while i < len(self.lines):
                    m = rx.search(self.lines[i])
                    i += 1
                    if m:
                        return m
                left = end - time.monotonic()
                if left <= 0:
                    if need:
                        sys.exit("no %r after %r" % (pattern, line))
                    return None
                self.cv.wait(left)

    def latency(self):
        """`DEBUG latency`: {control: {n, words, silent, noack, stage: (p50, p95, max)}}."""
        with self.cv:
            i = len(self.lines)
        self.send("DEBUG latency")
        end = time.monotonic() + 3
        out = {}
        with self.cv:
            while time.monotonic() < end:
                while i < len(self.lines):
                    line = self.lines[i]
                    i += 1
                    if "[lat] done" in line:
                        return out
                    m = LAT.search(line)
                    if not m:
                        continue
                    row = {"n": int(m[2]), "words": int(m[3]), "silent": int(m[4]), "noack": int(m[5])}
                    for kv in m[6].split():
                        k, v = kv.split("=")
                        row[k] = None if v == "-" else tuple(float(x) for x in v.split("/"))
                    out[m[1]] = row
                self.cv.wait(0.2)
        sys.exit("no [lat] done")


def table(name, lat, only):
    print("\n== %s (ms after the decisive moment, p50/p95)" % name)
    print("%-12s %3s %5s %6s %6s  " % ("control", "n", "words", "silent", "noack")
          + " ".join("%-12s" % s for s in STAGES) + "  verdict")
    ok = True
    for ctl, row in lat.items():
        if only and ctl not in only:
            continue
        cells = []
        for s in STAGES:
            v = row.get(s)
            cells.append("%-12s" % ("-" if v is None else "%.0f/%.0f" % (v[0], v[1])))
        why = []
        if row["silent"]:
            why.append("silent=%d" % row["silent"])
        for stage, limit in TARGETS.get(ctl, {}).items():
            v = row.get(stage)
            if v is None:
                why.append("no %s" % stage)
            elif v[1] > limit:
                why.append("%s p95 %.0f > %d" % (stage, v[1], limit))
        verdict = "PASS" if not why else "FAIL " + ", ".join(why)
        ok = ok and not why
        print("%-12s %3d %5d %6d %6d  %s  %s" % (ctl, row["n"], row["words"], row["silent"], row["noack"], " ".join(cells),
                                                 verdict))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--reps", type=int, default=6)
    ap.add_argument("--only", default="", help="controls to run, comma separated (default: all)")
    ap.add_argument("--log", default="latency_probe.log")
    a = ap.parse_args()
    only = set(x for x in a.only.split(",") if x)
    want = lambda c: not only or c in only
    n = a.reps
    with open(a.log, "w") as log:
        b = Board(a.port, log)
        time.sleep(1)
        b.ask("DEBUG power wake", r"\[power\] now power=full")
        b.ask("DEBUG power idle 3600", r"\[power\] now")
        b.ask("DEBUG hatch", r"\[hw\] debug hatch", timeout=20, need=False)
        b.ask("DEBUG duel", r"\[duel\] now")
        ok = True

        # ---- at rest, awake on the dish
        b.ask("DEBUG latency clear", r"\[lat\] cleared")
        if want("knock"):
            for _ in range(n):
                b.ask("DEBUG knock 1", r"\[hw\] debug knock")
                time.sleep(1.6)
        if want("knock_knock"):
            for _ in range(n):
                b.ask("DEBUG knock 2", r"\[hw\] debug knock")
                time.sleep(2.2)
        if want("press"):
            for _ in range(n):
                b.ask("DEBUG press 120", r"\[hw\] debug press")
                time.sleep(1.4)
        if want("shake"):
            for _ in range(n):
                b.ask("DEBUG shake", r"\[hw\] debug shake")
                time.sleep(2.5)
        if want("tilt"):
            for i in range(n):
                b.ask("DEBUG lean %d 1200" % (i % 2), r"\[hw\] debug lean")
                time.sleep(1.8)
        if want("side"):
            for i in range(n):
                b.ask("DEBUG side 2600 %s" % "rl"[i % 2], r"\[hw\] debug side")
                time.sleep(2.6 + 3.0 + 0.5)
        if want("hang") or want("double"):
            for i in range(n):   # each opens fight mode, and the next leaves it
                if want("hang"):
                    b.ask("DEBUG hang 2000", r"\[hw\] debug hang")
                    time.sleep(2.0 + 3.0 + 0.5)
                if want("double"):
                    b.ask("DEBUG double", r"\[hw\] debug double")
                    time.sleep(2.0)
        ok = table("rest", b.latency(), only) and ok
        b.ask("DEBUG duel", r"\[duel\] now")

        # ---- fight mode: the lobby a hang opens
        if want("fight"):
            b.ask("DEBUG latency clear", r"\[lat\] cleared")
            b.ask("DEBUG sleepiness 0", r"\[hw\] debug sleepiness", need=False)   # a side hold tucked him in
            b.ask("DEBUG hang 2000", r"\[hw\] debug hang")
            time.sleep(5.5)
            if b.ask("DEBUG duel", r"\[duel\] now active=(\d)")[1] != "1":
                print("fight: the hang opened no lobby (asleep? hungry?): skipped")
            else:
                b.ask("DEBUG latency clear", r"\[lat\] cleared")
                for i in range(n):
                    b.ask("DEBUG knock 2", r"\[hw\] debug knock")
                    time.sleep(2.2)
                    b.ask("DEBUG lean %d 1200" % (i % 2), r"\[hw\] debug lean")
                    time.sleep(1.8)
                ok = table("fight", b.latency(), only) and ok
                b.ask("DEBUG hang 2000", r"\[hw\] debug hang")
                time.sleep(5.5)

        # ---- low power: the wake
        if want("wake"):
            b.ask("DEBUG latency clear", r"\[lat\] cleared")
            for _ in range(n):
                if not b.ask("DEBUG power low", r"\[power\] now power=low", need=False):
                    print("low: refused (held awake): skipped")
                    break
                time.sleep(4.0)   # well asleep: light sleep, the panel off
                b.ask("DEBUG knock 2", r"\[hw\] debug knock", timeout=5)
                time.sleep(3.0)
            ok = table("low", b.latency(), {"wake"}) and ok
            b.ask("DEBUG power wake", r"\[power\] now")

        b.ask("DEBUG power idle default", r"\[power\] now")
        if b.crashes:
            print("FAIL crash: %s" % b.crashes[0])
            ok = False
        print("\n%s (log: %s)" % ("ok" if ok else "FAILED", a.log))
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
