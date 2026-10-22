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

Four phases, each read back with `DEBUG latency` and cleared first:
  rest   awake on the dish: every control N times but the side hold
  fight  fight mode (a hang opens the lobby): knock knock and tilt, then a
         hang to leave
  side   side holds (each tucks him in, so last)
  low    `DEBUG power low`, then a short turn: the wake, to the first frame
         and the light

Prints a table per phase (p50/p95 ms after In) and PASS/FAIL per control
against TARGETS below. The port is opened once and held: on Linux an open
resets the board.

The controls mean what they mean to a living pet, awake or asleep: an egg
is hatched first (`DEBUG hatch`), and anything else (his remains, a clutch)
stops the probe unless --revive, which runs the pet's time past the vigil
(`DEBUG warp`), keeps the first egg of the clutch with a side hold, hatches
it and grows it to an adult (`DEBUG stage adult`): a new pet on the board.

It must leave the pet as it found him: a run on db24534 dropped pellets that
rotted while he slept, he ate them when it woke him for a fight, and he died
(2026-10-21). So his vitals (the protocol's STATE and CHEM: his drives, life,
injury, toxin, the pantry and the pellets on the floor) are printed at the
start and the end, and a rise in injury or toxin, or a fall in life, fails
the run loudly. A knock knock or a press drops a pellet while the pantry has
one, so the feeding controls stop once MAX_PELLETS lie uneaten, and every
feeding section and the run end with `DEBUG tidy` (every pellet taken, toxin
and injury to 0). The wake is made with a short turn, which feeds nobody.
His sleepiness and the idle timeout are put back. --revive first reports
the death it finds (the lineage's cause and age).
"""
import argparse
import re
import sys
import threading
import time

import serial

CRASH = re.compile(r"Guru Meditation|abort\(\)|Backtrace:|panic|Brownout|stack overflow|WDT|rst:0x(7|8|9|f|10) ")
LAT = re.compile(r"\[lat\] (\w+) n=(\d+) words=(\d+) silent=(\d+) noack=(\d+) (.*)")
CLUTCH, VIGIL, ASLEEP, AWAKE = 11, 12, 14, 15   # blorb::BootContext
MAX_PELLETS = 2   # uneaten pellets on the floor: no more feeding controls past this
STAGES = ["seen", "detect", "ack", "ack_panel", "word", "tick", "frame", "panel"]

# ms after In, p95. Why each (docs/notes/controls-latency.md): the floor for
# anything drawn is the sample's handoff (up to 20 ms after the read), the
# loop finishing the frame it is drawing (up to a frame, 20-28 ms), drawing
# the next (14-25 ms) and sending its rows (3-10 ms): ~45 ms typical, ~80 at
# worst. So:
#   ack_panel  the acknowledgement on the glass: that floor (80), plus the
#              second steady sample a pose needs (100), or the second knock's
#              ring-out for a knock knock (150);
#   word       the aim: two samples past the dead zone, and the queue (60);
#   panel      the engine's answer on the glass: the word, then up to one
#              100 ms tick and the floor.
TARGETS = {
    "knock_knock": {"ack_panel": 150, "panel": 800},  # the flash once the 2nd knock rings out; the pellet waits out a 3rd knock
    "knock": {"ack_panel": 80},                       # "one more" on the first knock
    "side": {"ack_panel": 100},                       # the hold ring's first eighth, from the turn
    "hang": {"ack_panel": 100},                       # the hang's charge, from the turn
    "press": {"ack_panel": 80, "panel": 400},         # the ring on BOOT down; the pellet after the release
    "double": {"panel": 900},
    "shake": {"panel": 200},                          # decided on its last jolt: the trance's first frame after the tick
    "tilt": {"word": 60},                             # the aim (AimDetector), two samples past the dead zone
    "wake": {"panel": 400, "ack_panel": 400},         # the first frame on the glass, and the light
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

    def protocol(self, verb, timeout=3):
        """A protocol request on the cable: its '#e + ' lines, until '#e OK' or '#e ERR'."""
        with self.cv:
            i = len(self.lines)
        self.send("#e " + verb)
        out, end = [], time.monotonic() + timeout
        with self.cv:
            while time.monotonic() < end:
                while i < len(self.lines):
                    line = self.lines[i]
                    i += 1
                    if line.startswith("#e OK") or line.startswith("#e ERR"):
                        return out
                    if line.startswith("#e + "):
                        out.append(line[5:])
                self.cv.wait(0.2)
        return out

    def vitals(self):
        """STATE and CHEM: {name: value} of his drives, body, chemicals and dish (permille, counts)."""
        out = {}
        for line in self.protocol("STATE"):
            m = re.match(r"(drives|body|dish) (.*)", line)
            for kv in (m[2].split() if m else []):
                k, _, v = kv.partition("=")
                if re.fullmatch(r"-?\d+", v):
                    out[k] = int(v)
        for line in self.protocol("CHEM"):
            m = re.match(r"chem=\d+ name=(\w+) level=(-?\d+)", line)
            if m and m[1] in ("toxin", "injury", "life"):
                out["chem_" + m[1]] = int(m[2])
        return out

    def pellets(self):
        return self.vitals().get("pellets", 0)

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
                        self.send("DEBUG latency last")   # the newest of each, into the log
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
    ap.add_argument("--trace-fight", action="store_true",
                    help="DEBUG trace through the first fight rep (raw samples, the aim, knock burst and ringing): slows that rep")
    ap.add_argument("--revive", action="store_true", help="a dead pet's clutch: keep an egg, hatch it, grow it (a new pet)")
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
        ctx = int(b.ask("DEBUG duel", r"\[duel\] now .*ctx=(\d+)")[1])
        if ctx not in (ASLEEP, AWAKE):
            deaths = [l for l in b.protocol("LINEAGE") if l.startswith("death ")]
            if deaths:
                print("the dish holds his remains. The lineage's last death: %s" % deaths[-1])
            if not a.revive:
                sys.exit("the dish holds no living pet (boot context %d: 11 clutch, 12 his remains, 13 an egg): "
                         "the controls mean other things there. --revive starts a new pet" % ctx)
            if ctx == VIGIL:
                b.ask("DEBUG warp 31", r"\[hw\] debug warp", timeout=120)
                ctx = int(b.ask("DEBUG duel", r"\[duel\] now .*ctx=(\d+)")[1])
            if ctx == CLUTCH:
                b.ask("DEBUG side 3000 r", r"\[hw\] debug side")   # a hold keeps the egg
                time.sleep(6.5)
            b.ask("DEBUG hatch", r"\[hw\] debug hatch", timeout=60, need=False)
            b.ask("DEBUG stage adult", r"\[hw\] debug stage", timeout=180)
            ctx = int(b.ask("DEBUG duel", r"\[duel\] now .*ctx=(\d+)")[1])
            if ctx not in (ASLEEP, AWAKE):
                sys.exit("--revive: still no living pet (boot context %d)" % ctx)
        before = b.vitals()
        print("vitals before: %s" % " ".join("%s=%s" % kv for kv in sorted(before.items())))
        b.ask("DEBUG sleepiness 0", r"\[hw\] debug sleepiness", need=False)
        ok = True

        def fed(what):
            """True while feeding is safe: few pellets lie uneaten."""
            p = b.pellets()
            if p >= MAX_PELLETS:
                print("%s: %d pellets lie uneaten: no more feeding this run" % (what, p))
                return False
            return True

        # ---- at rest, awake on the dish
        b.ask("DEBUG latency clear", r"\[lat\] cleared")
        if want("knock"):
            for _ in range(n):
                b.ask("DEBUG knock 1", r"\[hw\] debug knock")
                time.sleep(1.6)
        if want("knock_knock"):
            for _ in range(n):
                if not fed("knock_knock"):
                    break
                b.ask("DEBUG knock 2", r"\[hw\] debug knock")
                time.sleep(2.2)
        if want("press"):
            for _ in range(n):
                if not fed("press"):
                    break
                b.ask("DEBUG press 120", r"\[hw\] debug press")
                time.sleep(1.4)
        if want("knock_knock") or want("press"):
            b.ask("DEBUG tidy", r"\[hw\] debug tidy")   # what was fed and not eaten goes: it would rot
        if want("shake"):
            for _ in range(n):
                b.ask("DEBUG shake", r"\[hw\] debug shake")
                time.sleep(2.5)
        if want("tilt"):
            for i in range(n):
                b.ask("DEBUG lean %d 1200" % (i % 2), r"\[hw\] debug lean")
                time.sleep(1.8)
        if want("hang") or want("double"):
            for i in range(n):   # each opens fight mode, and the next leaves it
                if want("hang"):
                    b.ask("DEBUG hang 2000", r"\[hw\] debug hang")
                    time.sleep(2.0 + 3.0 + 0.5)
                if want("double") and fed("double"):   # its first press feeds
                    b.ask("DEBUG double", r"\[hw\] debug double")
                    time.sleep(2.0)
        if want("double"):
            b.ask("DEBUG tidy", r"\[hw\] debug tidy")
        ok = table("rest", b.latency(), only) and ok
        b.ask("DEBUG duel", r"\[duel\] now")

        # ---- fight mode: the lobby a hang opens (the hangs above may have left it open)
        if want("fight"):
            b.ask("DEBUG latency clear", r"\[lat\] cleared")
            b.ask("DEBUG sleepiness 0", r"\[hw\] debug sleepiness", need=False)
            if b.ask("DEBUG duel", r"\[duel\] now active=(\d)")[1] != "1":
                b.ask("DEBUG hang 2000", r"\[hw\] debug hang")
                time.sleep(5.5)
            m = b.ask("DEBUG duel", r"\[duel\] now active=(\d) .*ctx=(\d+)")
            if m[1] != "1":
                why = [l for l in b.lines[-40:] if "[diag] fight" in l or "[duel] refused" in l]
                print("fight: no lobby (boot context %s): %s" % (m[2], why[-1] if why else "no fight line in the log"))
            else:
                b.ask("DEBUG latency clear", r"\[lat\] cleared")
                for i in range(n):
                    if i == 0 and a.trace_fight:
                        b.ask("DEBUG trace 6", r"\[hw\] debug trace")
                    if i == 1 and a.trace_fight:
                        b.ask("DEBUG trace 0", r"\[hw\] debug trace")
                    if fed("fight knock_knock"):
                        b.ask("DEBUG knock 2", r"\[hw\] debug knock")
                        time.sleep(2.2)
                    b.ask("DEBUG lean %d 1200" % (i % 2), r"\[hw\] debug lean")
                    time.sleep(1.8)
                b.ask("DEBUG tidy", r"\[hw\] debug tidy")
                ok = table("fight", b.latency(), only) and ok
                # A hard shake leaves fight mode before a duel has started (#35).
                b.ask("DEBUG shake", r"\[hw\] debug shake")
                time.sleep(2.5)
                if b.ask("DEBUG duel", r"\[duel\] now active=(\d)")[1] != "0":
                    print("FAIL fight: a hard shake did not leave fight mode (#35)")
                    ok = False
                    b.ask("DEBUG hang 2000", r"\[hw\] debug hang")
                    time.sleep(5.5)
                else:
                    print("fight: a hard shake left fight mode (#35)")

        # ---- side holds: last, as each tucks him in (asleep, he would doze out of a fight)
        if want("side"):
            b.ask("DEBUG latency clear", r"\[lat\] cleared")
            for i in range(n):
                b.ask("DEBUG side 2600 %s" % "rl"[i % 2], r"\[hw\] debug side")
                time.sleep(2.6 + 3.0 + 0.5)
            ok = table("side", b.latency(), {"side"}) and ok

        # ---- low power: the wake
        if want("wake"):
            b.ask("DEBUG latency clear", r"\[lat\] cleared")
            for _ in range(n):
                if not b.ask("DEBUG power low", r"\[power\] now power=low", need=False):
                    print("low: refused (held awake): skipped")
                    break
                time.sleep(4.0)   # well asleep: light sleep, the panel off
                b.ask("DEBUG side 600 r", r"\[hw\] debug side", timeout=5)   # a turn wakes it and feeds nobody
                time.sleep(4.0)
            ok = table("low", b.latency(), {"wake"}) and ok
            b.ask("DEBUG power wake", r"\[power\] now")

        b.ask("DEBUG power idle default", r"\[power\] now")
        after = b.vitals()
        print("vitals after:  %s" % " ".join("%s=%s" % kv for kv in sorted(after.items())))
        harm = [k for k in ("injury", "chem_injury", "chem_toxin") if after.get(k, 0) > before.get(k, 0)]
        harm += [k for k in ("life", "chem_life") if after.get(k, 0) < before.get(k, 0)]
        if harm:
            print("\n!!! FAIL: THE RUN HARMED HIM: %s\n" % ", ".join(
                "%s %s -> %s" % (k, before.get(k), after.get(k)) for k in harm))
            ok = False
        b.ask("DEBUG tidy", r"\[hw\] debug tidy")
        if "sleepiness" in before:
            b.ask("DEBUG sleepiness %d" % round(before["sleepiness"] / 10), r"\[hw\] debug sleepiness", need=False)
        print("vitals tidied: %s" % " ".join("%s=%s" % kv for kv in sorted(b.vitals().items())))
        if b.crashes:
            print("FAIL crash: %s" % b.crashes[0])
            ok = False
        print("\n%s (log: %s)" % ("ok" if ok else "FAILED", a.log))
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
