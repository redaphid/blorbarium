# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Record a badge's raw IMU and what it detected, while a person does gestures.

    uv run tools/imu_record.py /dev/ttyACM0 --out rec.log &
    echo "knock_knock start" > rec.log.cmd     # a labelled mark, timestamped on arrival
    echo "send DEBUG hatch" > rec.log.cmd      # a line to the badge
    echo "quit" > rec.log.cmd

Keeps `DEBUG trace` on (renewed every 60 s) and the badge awake (idle 3600).
Every line of the log is `<host seconds> <line>`; marks are `<t> ## <text>`.
Commands arrive through `<out>.cmd`, polled every 50 ms (each write replaces it).
tools/imu_analyze.py reads the log.
"""
import argparse
import os
import sys
import threading
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    p = serial.Serial()
    p.port, p.baudrate, p.timeout = a.port, 115200, 0.05
    p.dtr = p.rts = False
    p.open()
    t0 = time.time()
    log = open(a.out, "w", buffering=1)
    lock = threading.Lock()

    def write(s):
        with lock:
            log.write("%.3f %s\n" % (time.time() - t0, s))

    def send(line):
        write(">> " + line)
        p.write(b"\r" * 8)
        time.sleep(0.06)
        p.write(line.encode() + b"\n")

    stop = threading.Event()

    def reader():
        buf = b""
        while not stop.is_set():
            buf += p.read(8192)
            *whole, buf = buf.split(b"\n")
            for raw in whole:
                write(raw.decode("utf-8", "replace").rstrip("\r"))

    threading.Thread(target=reader, daemon=True).start()
    time.sleep(3)   # the open may reset it: let it boot
    send("DEBUG power wake")
    send("DEBUG power idle 3600")
    send("DEBUG trace 90")
    renew = time.time() + 60
    cmd = a.out + ".cmd"
    while True:
        if time.time() > renew:
            send("DEBUG trace 90")
            renew = time.time() + 60
        if os.path.exists(cmd):
            with open(cmd) as f:
                text = f.read().strip()
            os.remove(cmd)
            for line in text.splitlines():
                if line == "quit":
                    send("DEBUG trace 0")
                    time.sleep(0.3)
                    stop.set()
                    return 0
                if line.startswith("send "):
                    send(line[5:])
                else:
                    write("## " + line)
        time.sleep(0.05)


if __name__ == "__main__":
    sys.exit(main())
