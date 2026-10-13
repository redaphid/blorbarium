# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""The board's screen as a PNG, over the cable: nobody here can see the glass.

    uv run tools/shot.py --port COM11 --out dish.png
    uv run tools/shot.py --port COM11 --send "DEBUG press" --wait 800 --out pressed.png
    uv run tools/shot.py --from serial.log --out dish.png     decode a captured log

It sends `DEBUG shot` (serial-only, src/main.cpp) and decodes the lines the
board answers with; lib/paint/include/paint/shot.h is the format. The crc is
over the decoded pixels, so a shot that writes is a shot that is right. The
port is opened with DTR and RTS held low: a tool that toggles them resets the
board.
"""
import argparse
import base64
import binascii
import re
import struct
import sys
import time
import zlib

W = H = 240
PREFIX = "[hw] shot "
END = re.compile(r"end bytes=(\d+) crc=([0-9a-f]{8})")


class ShotError(Exception):
    pass


def unpack(data):
    """PackBits over little-endian 16-bit pixels, back to a list of pixels."""
    px, i = [], 0
    while i < len(data):
        h = data[i]
        i += 1
        if h < 128:
            (p,) = struct.unpack_from("<H", data, i)
            px.extend([p] * (h + 1))
            i += 2
        else:
            n = h - 127
            px.extend(struct.unpack_from("<%dH" % n, data, i))
            i += 2 * n
    return px


def decode(lines):
    """Pixels from the lines of one shot, begin to end. Anything else is skipped."""
    chunks, begun = [], False
    for line in lines:
        line = line.strip()
        if not line.startswith(PREFIX):
            continue
        body = line[len(PREFIX):]
        if body.startswith("begin"):
            chunks, begun = [], True
            if "w=%d h=%d rle565" % (W, H) not in body:
                raise ShotError("unknown shot format: " + body)
            continue
        if not begun:
            continue
        m = END.match(body)
        if not m:
            chunks.append(body)
            continue
        try:
            data = b"".join(base64.b64decode(c, validate=True) for c in chunks)
            if len(data) != int(m.group(1)):
                raise ShotError("got %d bytes, the board sent %s" % (len(data), m.group(1)))
            px = unpack(data)
        except (binascii.Error, struct.error) as e:
            raise ShotError("damaged shot: %s" % e)
        if len(px) != W * H:
            raise ShotError("got %d pixels, want %d" % (len(px), W * H))
        crc = zlib.crc32(struct.pack("<%dH" % len(px), *px))
        if crc != int(m.group(2), 16):
            raise ShotError("crc %08x, the board sent %s" % (crc, m.group(2)))
        return px
    raise ShotError("no complete shot in the input")


def rgb(p):
    r, g, b = p >> 11 & 31, p >> 5 & 63, p & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def write_png(path, px, scale):
    rows = []
    for y in range(H):
        row = bytearray()
        for x in range(W):
            row += bytes(rgb(px[y * W + x])) * scale
        rows.extend([b"\0" + bytes(row)] * scale)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W * scale, H * scale, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(b"".join(rows), 9)))
        f.write(chunk(b"IEND", b""))


def open_port(name):
    import serial

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = name, 115200, 0.2
    port.dtr = port.rts = False
    port.open()
    return port


def lines_until(port, done, seconds):
    """Lines from the port until done(lines) or the time runs out. Others go to stderr."""
    got, buf, deadline = [], b"", time.monotonic() + seconds
    while time.monotonic() < deadline:
        buf += port.read(4096)
        *whole, buf = buf.split(b"\n")
        for raw in whole:
            line = raw.decode("utf-8", "replace").rstrip("\r")
            got.append(line)
            if not line.startswith(PREFIX):
                print(line, file=sys.stderr)
        if done(got):
            return got
    return got


def shoot(port, timeout, tries):
    for attempt in range(1, tries + 1):
        port.write(b"DEBUG shot\n")
        got = lines_until(port, lambda ls: any(END.match(l[len(PREFIX):]) for l in ls if l.startswith(PREFIX)), timeout)
        try:
            return decode(got)
        except ShotError as e:
            print("shot.py: try %d: %s" % (attempt, e), file=sys.stderr)
    raise ShotError("no good shot in %d tries" % tries)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="the board's serial port, e.g. COM11")
    ap.add_argument("--from", dest="source", help="decode a captured log instead of asking a board")
    ap.add_argument("--out", required=True, help="the PNG to write")
    ap.add_argument("--scale", type=int, default=1, help="nearest-neighbour upscale")
    ap.add_argument("--send", action="append", default=[], help="a line to send first (repeatable)")
    ap.add_argument("--wait", type=int, default=500, help="ms to wait after each --send")
    ap.add_argument("--timeout", type=float, default=40, help="seconds a shot may take")
    ap.add_argument("--tries", type=int, default=3)
    args = ap.parse_args()
    if bool(args.port) == bool(args.source):
        ap.error("give one of --port or --from")
    try:
        if args.source:
            with open(args.source, encoding="utf-8", errors="replace") as f:
                px = decode(f)
        else:
            with open_port(args.port) as port:
                for line in args.send:
                    port.write(line.encode() + b"\n")
                    lines_until(port, lambda _: False, args.wait / 1000)
                px = shoot(port, args.timeout, args.tries)
    except ShotError as e:
        sys.exit("shot.py: %s" % e)
    write_png(args.out, px, args.scale)
    print("shot.py: wrote %s" % args.out)


if __name__ == "__main__":
    main()
