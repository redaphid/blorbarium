# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Reboot a freshly wiped badge and answer its found-pin window over the cable.

    python3 scripts/test/found_pin_boot.py --port /dev/ttyACM0 --species puck --seconds 120 > boot.log

A board with no pet asks `[hw] fresh: DEBUG found <species> in 5 s pins the egg`
(src/main.cpp awaitFoundPin). This pulses RTS (EN) with DTR (IO0) low, an ordinary
boot like tools/serial_log.py --reset, waits for that line, sends
`DEBUG found <species>` and keeps logging, timestamped. Exits 1 if the window never
came or the pin was refused.
"""
import argparse
import sys
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--species", required=True)
    ap.add_argument("--seconds", type=float, default=90)
    ap.add_argument("--send", action="append", default=[], help="a line to send once the pin is in (repeatable)")
    args = ap.parse_args()

    import serial

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.2
    port.dtr = port.rts = False
    port.open()
    port.rts = True
    time.sleep(0.1)
    port.rts = False
    t0 = time.monotonic()
    buf, asked, pinned = b"", False, None
    while time.monotonic() - t0 < args.seconds:
        buf += port.read(4096)
        *whole, buf = buf.split(b"\n")
        for raw in whole:
            line = raw.decode("utf-8", "replace").rstrip("\r")
            print("%7.2f %s" % (time.monotonic() - t0, line), flush=True)
            if "[hw] fresh: DEBUG found" in line and not asked:
                port.write(f"DEBUG found {args.species}\n".encode())
                asked = True
            if line.startswith("[hw] found pin "):
                pinned = line.endswith(": ok")
                if pinned:
                    for s in args.send:
                        port.write(s.encode() + b"\n")
    port.close()
    if not asked:
        print("!! no found-pin window: the board was not fresh", file=sys.stderr)
        return 1
    if not pinned:
        print("!! the pin was not taken", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
