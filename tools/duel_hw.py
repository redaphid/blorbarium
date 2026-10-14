# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""A Spar between two boards on the cable, played over ESP-NOW by DEBUG verbs.

    uv run tools/duel_hw.py COM11 COM14 > duel.log

Each board gets what a kid's hands would give it, in answer to its own
[duel] lines: a double press opens the lobby, a lean toward the friend and a
press picks them, a lean to the Spar edge and a press confirms the Spar. The
pets play every turn themselves. Nothing here holds BOOT, so a Mortal cannot
be agreed. It ends when both boards print their `met` line, then compares the
outcome hashes: exit 0 only when both met and the hashes match.
"""
import argparse
import re
import sys
import threading
import time

import serial


class Board:
    def __init__(self, name, t0):
        self.name, self.t0 = name, t0
        self.port = serial.Serial()
        self.port.port, self.port.baudrate, self.port.timeout = name, 115200, 0.2
        self.port.dtr = self.port.rts = False   # opening must not reset it
        self.port.open()
        self.lines, self.lock = [], threading.Lock()
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        buf = b""
        while True:
            buf += self.port.read(4096)
            *whole, buf = buf.split(b"\n")
            for raw in whole:
                line = raw.decode("utf-8", "replace").rstrip("\r")
                print("%7.2f %-5s %s" % (time.monotonic() - self.t0, self.name, line), flush=True)
                with self.lock:
                    self.lines.append(line)

    def take(self):
        with self.lock:
            out, self.lines = self.lines, []
        return out

    def send(self, line):
        print("%7.2f %-5s >> %s" % (time.monotonic() - self.t0, self.name, line), flush=True)
        self.port.write(line.encode() + b"\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ports", nargs=2)
    ap.add_argument("--seconds", type=float, default=240)
    args = ap.parse_args()

    t0 = time.monotonic()
    boards = [Board(p, t0) for p in args.ports]
    met = {}
    picked = set()
    for b in boards:
        b.send("DEBUG double")
    while time.monotonic() - t0 < args.seconds and len(met) < 2:
        time.sleep(0.05)
        for b in boards:
            for line in b.take():
                if "[duel] lobby hears" in line and not line.startswith("[duel] lobby hears 0") and b.name not in picked:
                    picked.add(b.name)
                    b.send("DEBUG lean 0 4000")
                    time.sleep(0.8)
                    b.send("DEBUG press 150")
                elif "-> staking" in line:
                    b.send("DEBUG lean 0 4000")
                    time.sleep(0.8)
                    b.send("DEBUG press 150")
                elif "boot context" in line and not line.rstrip().endswith("boot context 15"):
                    print("%s: the pet is not awake (14 is asleep), so the double press fed him instead; "
                          "wait for him to wake, or `DEBUG stage <his stage>` rests him" % b.name, flush=True)
                    return 1
                elif "-> closed" in line and b.name not in met:
                    print("%s closed before it met anyone" % b.name, flush=True)
                    return 1
                m = re.search(r"\[duel\] met .*outcome_hash=([0-9a-f]+)", line)
                if m:
                    met[b.name] = m.group(1)
    for b in boards:
        b.send("DEBUG double")   # leave the friends screen: the radio closes
    time.sleep(3)
    for b in boards:
        b.take()
    print("outcome hashes: %s" % met, flush=True)
    return 0 if len(met) == 2 and len(set(met.values())) == 1 else 1


if __name__ == "__main__":
    sys.exit(main())
