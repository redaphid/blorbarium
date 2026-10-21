# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""What a real badge's frames cost, over its cable (docs/power.md, "Awake: full speed").

    uv run scripts/test/fps_probe.py /dev/ttyACM0 --log fps.log
    uv run scripts/test/fps_probe.py /dev/ttyACM0 --clocks 40,80 --seconds 15

For each SPI clock (`DEBUG lcd <MHz>`): settle, then `DEBUG fps` over a
window at rest. Then low power and back: `DEBUG power low` must show no
frames and the clock down, `DEBUG power wake` the clock at 240 again and
frames running. Last, at each clock, a hang (`DEBUG hang`, the red ring
filling), which may leave the badge in fight mode: a second hang ends it.

The SPI clock's real proof is the glass: watch the panel while it runs at 80
for torn rows, speckle or colour shifts (a screenshot reads the canvas, not
the panel, so it cannot see them).

The port is opened once and held: on Linux an open resets the board.
"""
import argparse
import re
import sys
import threading
import time

import serial

CRASH = re.compile(r"Guru Meditation|abort\(\)|Backtrace:|panic|Brownout|stack overflow|WDT|rst:0x(7|8|9|f|10) ")
FPS = re.compile(r"\[fps\] (fps=.*)")


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

    def ask(self, line, pattern, timeout=3.0):
        with self.cv:
            i = len(self.lines)
        self.log.write("%8.2f >> %s\n" % (time.monotonic() - self.t0, line))
        # badge128_pm light-sleeps in low power and loses a line's first bytes: '\r's first.
        self.p.write(b"\r" * 8)
        time.sleep(0.06)
        self.p.write(line.encode() + b"\n")
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
                    sys.exit("no %r after %r" % (pattern, line))
                self.cv.wait(left)

    def fps(self):
        return dict(kv.split("=", 1) for kv in self.ask("DEBUG fps", FPS.pattern).group(1).split())


def window(b, name, seconds, during=None):
    b.fps()   # starts the window
    if during:
        b.ask(during, r"\[hw\] debug ")
    time.sleep(seconds)
    f = b.fps()
    print("%-14s fps=%-5s draw_ms=%-5s push_ms=%-5s worst_ms=%-4s rows=%-4s cpu_now=%-4s spi_mhz=%s" % (
        name, f["fps"], f["draw_ms"], f["push_ms"], f["worst_ms"], f["rows"], f["cpu_now"], f["spi_mhz"]))
    return f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--clocks", default="40,80", help="SPI write clocks to try, MHz")
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--log", default="fps_probe.log")
    a = ap.parse_args()
    with open(a.log, "w") as log:
        b = Board(a.port, log)
        time.sleep(1)
        b.ask("DEBUG power wake", r"\[power\] now power=full")
        clocks = [int(m) for m in a.clocks.split(",")]
        for mhz in clocks:
            b.ask("DEBUG lcd %d" % mhz, r"\[hw\] lcd spi_mhz=%d" % mhz)
            time.sleep(2)
            window(b, "rest@%d" % mhz, a.seconds)
        b.ask("DEBUG power low", r"\[power\] now power=low")
        time.sleep(3)
        low = window(b, "low", 3)
        b.ask("DEBUG power wake", r"\[power\] now power=full")
        time.sleep(1)
        woke = window(b, "woke", 3)
        # Last: a full hang opens fight mode, which holds the badge awake for minutes.
        for mhz in clocks:
            b.ask("DEBUG lcd %d" % mhz, r"\[hw\] lcd spi_mhz=%d" % mhz)
            window(b, "hang@%d" % mhz, 3, during="DEBUG hang")
            time.sleep(5)
        ok = True
        if low["frames"] != "0" or int(low["cpu_now"]) > 80:
            print("FAIL low power drew frames or kept the clock up: %s" % low)
            ok = False
        if woke["cpu_now"] != "240" or woke["frames"] == "0":
            print("FAIL awake is not 240 MHz and drawing: %s" % woke)
            ok = False
        if b.crashes:
            print("FAIL crash: %s" % b.crashes[0])
            ok = False
        print("ok" if ok else "FAILED", "(log: %s)" % a.log)
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
