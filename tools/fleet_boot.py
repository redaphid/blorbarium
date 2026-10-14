# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Reboots one board and logs its first boot, pinning the egg if asked:

    uv run tools/fleet_boot.py --port COM15 --seconds 20 [--species gilly] > boot.log

With --species it waits for the board's "[hw] fresh:" line, which only a
device that has never had a pet prints, and answers `DEBUG found <species>`
inside the five seconds the board waits. Exits 3 if a species was asked for
and the board never confirmed the pin.
"""
import argparse
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--species")
    args = ap.parse_args()

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.1
    port.dtr = port.rts = False
    port.open()
    port.rts = True
    time.sleep(0.15)
    port.rts = False

    t0, buf, sent, pinned = time.time(), b"", False, False
    while time.time() - t0 < args.seconds:
        buf += port.read(4096)
        *lines, buf = buf.split(b"\n")
        for raw in lines:
            line = raw.decode(errors="replace").rstrip("\r")
            print(f"{time.time() - t0:7.2f} {line}", flush=True)
            if args.species and not sent and "[hw] fresh:" in line:
                port.write(f"DEBUG found {args.species}\n".encode())
                sent = True
            if f"[hw] found pin {args.species}: ok" in line:
                pinned = True
    return 3 if args.species and not pinned else 0


if __name__ == "__main__":
    sys.exit(main())
