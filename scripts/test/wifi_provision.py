# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Give a badge the test Wi-Fi over the cable, without the network's name or
password ever reaching a terminal, a log or a commit (docs/wifi-ota.md,
"Provisioning"; issue #27).

    uv run scripts/test/wifi_provision.py --port /dev/ttyACM0
    uv run scripts/test/wifi_provision.py --port /dev/ttyACM0 --check --seconds 60
    uv run scripts/test/wifi_provision.py --port /dev/ttyACM0 --check-url http://192.168.1.5:8000/manifest.json

It reads the credentials from the repo's .env (mode 600, git-ignored), two
lines `ssid=...` and `password=...`, sends `DEBUG wifi set "<ssid>" <password>`,
then `DEBUG wifi` (and `DEBUG ota check [url]` with --check or --check-url),
and prints what the badge says for --seconds. Every line it prints has the
SSID and the password replaced by <ssid> and <password>, and any line that
carries the `wifi set` command itself is dropped. Errors never name them.

Opening the port holds DTR and RTS low, so it does not reset the board
(tools/serial_log.py).
"""
import argparse
import os
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read_env(path: Path) -> tuple[str, str]:
    if not path.exists():
        sys.exit(f"no {path}: it needs two lines, ssid=... and password=...")
    mode = path.stat().st_mode & 0o777
    if mode & 0o077:
        print(f"warning: {path} is mode {mode:o}; it should be 600", file=sys.stderr)
    values = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key.strip().lower()] = value
    ssid, password = values.get("ssid"), values.get("password", "")
    if not ssid:
        sys.exit(f"{path} has no ssid= line")
    if len(ssid.encode()) > 32 or len(password.encode()) > 64:
        sys.exit("the SSID must be 1-32 bytes and the password at most 64 (lengths only; not shown)")
    if '"' in ssid or "\n" in ssid or "\n" in password:
        sys.exit("the SSID holds a double quote or a newline, which DEBUG wifi set cannot carry (not shown)")
    if len(f'DEBUG wifi set "{ssid}" {password}') > 6 + 127:
        sys.exit("the line would pass the badge's 128-byte DEBUG line (not shown)")
    return ssid, password


def redactor(ssid: str, password: str):
    secrets = sorted({s for s in (ssid, password) if s}, key=len, reverse=True)
    names = {ssid: "<ssid>", password: "<password>"}

    def clean(line: str):
        if "wifi set" in line:
            return None
        for s in secrets:
            line = line.replace(s, names[s])
        return line

    return clean


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--env", default=str(ROOT / ".env"))
    ap.add_argument("--check", action="store_true", help="then DEBUG ota check (the site's manifest)")
    ap.add_argument("--check-url", help="then DEBUG ota check <url> (a test manifest)")
    ap.add_argument("--seconds", type=float, default=40)
    args = ap.parse_args()

    ssid, password = read_env(Path(args.env))
    clean = redactor(ssid, password)

    import serial

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.2
    port.dtr = port.rts = False
    port.open()
    lines = [f'DEBUG wifi set "{ssid}" {password}', "DEBUG wifi"]
    if args.check or args.check_url:
        lines.append("DEBUG ota check" + (f" {args.check_url}" if args.check_url else ""))
    for line in lines:
        port.write(line.encode() + b"\n")
        time.sleep(0.3)
    print(f"sent: wifi set <ssid> <password>, wifi{', ota check' if len(lines) > 2 else ''}", flush=True)

    t0, buf = time.monotonic(), b""
    while time.monotonic() - t0 < args.seconds:
        buf += port.read(4096)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            text = clean(raw.decode(errors="replace").rstrip("\r"))
            if text is not None:
                print(f"{time.monotonic() - t0:8.2f}    {text}", flush=True)
    port.close()


if __name__ == "__main__":
    main()
