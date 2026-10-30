# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""tools/serial_log.py with timed sends: each line goes out at its own second, so a
command is not lost to a boot that opening the port may cause.

    python3 scripts/test/serial_session.py --port /dev/ttyACM0 --seconds 120 \
        --at "8:DEBUG hatch" --at "30:DEBUG knock 2" > session.log

DTR and RTS are held low on open, as in serial_log.py.
"""
import argparse
import sys
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--at", action="append", default=[], help="SECONDS:LINE, sent that long after opening")
    args = ap.parse_args()

    import serial

    plan = sorted((float(s.split(":", 1)[0]), s.split(":", 1)[1]) for s in args.at)
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.2
    port.dtr = port.rts = False
    port.open()
    t0 = time.monotonic()
    buf = b""
    while time.monotonic() - t0 < args.seconds:
        now = time.monotonic() - t0
        while plan and plan[0][0] <= now:
            _, line = plan.pop(0)
            print("%7.2f >> %s" % (now, line), flush=True)
            port.write(line.encode() + b"\n")
        buf += port.read(4096)
        *whole, buf = buf.split(b"\n")
        for raw in whole:
            print("%7.2f %s" % (time.monotonic() - t0, raw.decode("utf-8", "replace").rstrip("\r")), flush=True)
    port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
