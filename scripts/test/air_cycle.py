# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Fight mode opened and closed over and over, watching the internal heap (#53).

    uv run scripts/test/air_cycle.py /dev/ttyACM0 --cycles 10 --log air_cycle.log

Opening the port resets the badge: the boot's heap is read from its own lines
(`[hw] ble name=... heap= big=`, `[hw] psram: ...`). Then each cycle is two
`DEBUG hang`s: the first fills the hang and opens fight mode, which opens the
ESP-NOW air (`[hw] air open ch=1 heap A -> B ... big=`); the second ends it
(`[hw] air close ... heap=`). It fails on `air open FAILED`, a Wi-Fi deinit
timeout, a crash or reset, or a cycle whose air never opened, and checks the
issue's bars: boot heap >= --boot-min (71000), heap with the air open >=
--open-min (26000). `DEBUG fps` at the end prints the task stacks' unused bytes.

The port is opened once and held: on Linux an open resets the board.
"""
import argparse
import re
import sys
import threading
import time

import serial

CRASH = re.compile(r"Guru Meditation|abort\(\)|Backtrace:|panic|Brownout|stack overflow|WDT|rst:0x(7|8|9|f|10) ")
FAIL = re.compile(r"air open FAILED|deinit.*(timeout|TIMEOUT)|esp_wifi_deinit.*fail|ESP_ERR_NO_MEM")
BOOT_HEAP = re.compile(r"\[hw\] ble name=\S+ heap=(\d+) big=(\d+)")
OPEN = re.compile(r"\[hw\] air open ch=\d+ heap (\d+) -> (\d+) minheap=(\d+) big=(\d+)")
CLOSE = re.compile(r"\[hw\] air close .* heap=(\d+)")


class Board:
    def __init__(self, port, log):
        self.t0, self.log = time.monotonic(), log
        self.p = serial.Serial()
        self.p.port, self.p.baudrate, self.p.timeout = port, 115200, 0.1
        self.p.dtr = self.p.rts = False
        self.p.open()
        self.lines, self.fails = [], []
        self.cv = threading.Condition()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        buf = b""
        while True:
            buf += self.p.read(4096)
            *whole, buf = buf.split(b"\n")
            for raw in whole:
                line = raw.decode("utf-8", "replace").rstrip("\r")
                if CRASH.search(line) or FAIL.search(line):
                    self.fails.append(line)
                self.log.write("%8.2f    %s\n" % (time.monotonic() - self.t0, line))
                self.log.flush()
                with self.cv:
                    self.lines.append(line)
                    self.cv.notify_all()

    def send(self, line):
        self.log.write("%8.2f >> %s\n" % (time.monotonic() - self.t0, line))
        self.p.write(b"\r" * 8)
        time.sleep(0.06)
        self.p.write(line.encode() + b"\n")

    def wait(self, rx, since, timeout):
        """The first line from index `since` matching rx, or None after `timeout` s."""
        end = time.monotonic() + timeout
        with self.cv:
            i = since
            while True:
                while i < len(self.lines):
                    m = rx.search(self.lines[i])
                    i += 1
                    if m:
                        return m
                left = end - time.monotonic()
                if left <= 0:
                    return None
                self.cv.wait(left)

    def mark(self):
        with self.cv:
            return len(self.lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--cycles", type=int, default=10)
    ap.add_argument("--hold", type=float, default=4.0, help="seconds in fight mode each cycle")
    ap.add_argument("--boot-min", type=int, default=71000)
    ap.add_argument("--open-min", type=int, default=26000)
    ap.add_argument("--log", default="air_cycle.log")
    a = ap.parse_args()
    with open(a.log, "w") as log:
        b = Board(a.port, log)
        boot = b.wait(BOOT_HEAP, 0, 20)
        if not boot:
            sys.exit("no boot heap line in 20 s (log: %s)" % a.log)
        boot_heap, boot_big = int(boot.group(1)), int(boot.group(2))
        print("boot heap=%d big=%d" % (boot_heap, boot_big))
        psram = b.wait(re.compile(r"\[hw\] psram: (.*)"), 0, 5)
        if psram:
            print("psram: %s" % psram.group(1))
        time.sleep(3)
        b.send("DEBUG power wake")
        time.sleep(1)
        opened, ok = [], True
        for c in range(a.cycles):
            at = b.mark()
            b.send("DEBUG hang")
            m = b.wait(OPEN, at, 15)
            if not m:
                print("cycle %d: the air never opened" % (c + 1))
                ok = False
                break
            heap_open, big_open = int(m.group(2)), int(m.group(4))
            opened.append(heap_open)
            time.sleep(a.hold)
            at = b.mark()
            b.send("DEBUG hang")
            m2 = b.wait(CLOSE, at, 15)
            print("cycle %d: open heap %s -> %d big=%d, close heap=%s" % (
                c + 1, m.group(1), heap_open, big_open, m2.group(1) if m2 else "?"))
            if not m2:
                print("cycle %d: the air never closed" % (c + 1))
                ok = False
                break
            time.sleep(2)
            if b.fails:
                break
        at = b.mark()
        b.send("DEBUG fps")
        stacks = b.wait(re.compile(r"\[fps\] stack (.*)"), at, 5)
        if stacks:
            print("stacks: %s" % stacks.group(1))
        if boot_heap < a.boot_min:
            print("FAIL boot heap %d < %d" % (boot_heap, a.boot_min))
            ok = False
        if opened and min(opened) < a.open_min:
            print("FAIL heap with the air open %d < %d" % (min(opened), a.open_min))
            ok = False
        if b.fails:
            print("FAIL %s" % b.fails[0])
            ok = False
        print("ok" if ok else "FAILED", "(log: %s)" % a.log)
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
