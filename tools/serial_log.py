# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""The board's serial log, timestamped, without resetting it by accident.

    uv run tools/serial_log.py --port COM11 --seconds 300 --reset > boot.log
    uv run tools/serial_log.py --port COM11 --send "DEBUG stage adult" --seconds 20

DTR and RTS are held low on open, so opening the port does not reset the
board. --reset pulses RTS (EN) on purpose, with DTR (IO0) low, which is an
ordinary boot: the way to catch the boot lines after a flash.
"""
import argparse
import sys
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--reset", action="store_true", help="reboot the board first")
    ap.add_argument("--send", action="append", default=[], help="a line to send once open (repeatable)")
    args = ap.parse_args()

    import serial

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.2
    port.dtr = port.rts = False
    port.open()
    t0 = time.monotonic()
    if args.reset:
        port.rts = True
        time.sleep(0.1)
        port.rts = False
    for line in args.send:
        port.write(line.encode() + b"\n")
    buf = b""
    while time.monotonic() - t0 < args.seconds:
        buf += port.read(4096)
        *whole, buf = buf.split(b"\n")
        for raw in whole:
            print("%7.2f %s" % (time.monotonic() - t0, raw.decode("utf-8", "replace").rstrip("\r")), flush=True)
    port.close()


if __name__ == "__main__":
    sys.exit(main())
