# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Give a badge the test Wi-Fi over the cable, without the network's name or
password ever reaching a terminal, a log or a commit (docs/wifi-ota.md,
"Provisioning"; issue #27).

    uv run scripts/test/wifi_provision.py --port /dev/ttyACM0
    uv run scripts/test/wifi_provision.py --port /dev/ttyACM0 --check --seconds 120
    uv run scripts/test/wifi_provision.py --port /dev/ttyACM0 --check-url http://192.168.1.5:8000/manifest.json
    uv run scripts/test/wifi_provision.py --port /dev/ttyACM0 --env ~/Projects/blorbarium/.env

The credentials come from a .env (mode 600, git-ignored), two lines `ssid=...`
and `password=...`: --env, else this checkout's .env, else the main
checkout's (~/Projects/blorbarium/.env).

Steps, each waiting for the badge's answer (a board that was just reset, or
is still booting, drops a line sent too early):
  1. `DEBUG wifi` until the badge answers its status (up to --ready seconds);
  2. `DEBUG wifi set "<ssid>" <password>`, and its reply printed;
  3. `DEBUG wifi list`, and a verdict: is the network among them?
     Then `DEBUG wifi verify` the same: does the stored password match .env
     byte for byte (the badge answers lengths and yes or no only)?
  4. with --check or --check-url, `DEBUG ota check [url]`.
Then it prints what the badge says for --seconds. Every line is printed, none
dropped, with the SSID and the password replaced by <ssid> and <password>.
Errors never name them. The exit code is 0 only when step 3 saw the network.

Opening the port holds DTR and RTS low, as tools/serial_log.py does.
"""
import argparse
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN_ENV = Path.home() / "Projects" / "blorbarium" / ".env"


def find_env(arg):
    if arg:
        return Path(arg).expanduser()
    for p in (ROOT / ".env", MAIN_ENV):
        if p.exists():
            return p
    sys.exit(f"no .env at {ROOT / '.env'} or {MAIN_ENV} (or pass --env): it needs ssid=... and password=...")


def read_env(path: Path):
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
    if len(f'wifi verify "{ssid}" {password}'.encode()) > 127:
        sys.exit("the line would pass the badge's 128-byte DEBUG line (not shown)")
    return ssid, password


def masker(ssid: str, password: str):
    names = {ssid: "<ssid>", password: "<password>"}
    secrets = sorted({s for s in names if s}, key=len, reverse=True)

    def mask(line: str) -> str:
        for s in secrets:
            line = line.replace(s, names[s])
        return line

    return mask


class Badge:
    def __init__(self, port, mask):
        self.port, self.mask, self.buf, self.t0 = port, mask, b"", time.monotonic()

    def send(self, line: str, shown: str):
        self.port.write(line.encode() + b"\n")
        self.say(f">>> {shown}")

    def say(self, text: str):
        print(f"{time.monotonic() - self.t0:8.2f}    {text}", flush=True)

    def lines(self, seconds: float):
        """Every whole line the badge sends for `seconds`: raw, after printing it masked."""
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.buf += self.port.read(4096)
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                text = raw.decode(errors="replace").rstrip("\r")
                self.say(self.mask(text))
                yield text

    def until(self, seconds: float, pred):
        for text in self.lines(seconds):
            if pred(text):
                return text
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--env", help="the .env with ssid= and password= (default: this checkout's, else the main one's)")
    ap.add_argument("--check", action="store_true", help="then DEBUG ota check (the site's manifest)")
    ap.add_argument("--check-url", help="then DEBUG ota check <url> (a test manifest)")
    ap.add_argument("--ready", type=float, default=20, help="seconds to wait for the badge to answer at all")
    ap.add_argument("--seconds", type=float, default=40, help="seconds to listen after the last step")
    args = ap.parse_args()

    ssid, password = read_env(find_env(args.env))
    mask = masker(ssid, password)

    import serial

    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.2
    port.dtr = port.rts = False
    port.open()
    b = Badge(port, mask)

    # 1. Until the badge answers: a reset on open, or a boot under way, eats early lines.
    status = None
    for _ in range(int(args.ready / 2) or 1):
        b.send("DEBUG wifi", "DEBUG wifi")
        status = b.until(2, lambda t: "[wifi] window=" in t)
        if status:
            break
    if not status:
        sys.exit(f"the badge did not answer DEBUG wifi in {args.ready:.0f} s: is it this firmware, and booted?")

    # 2. The network, and the badge's reply to it.
    b.send(f'DEBUG wifi set "{ssid}" {password}', 'DEBUG wifi set "<ssid>" <password>')
    reply = b.until(5, lambda t: t.startswith("[wifi] network ") or "want: wifi set" in t)
    if not reply:
        b.say("!!! no reply to wifi set in 5 s")

    # 3. Is it remembered?
    b.send("DEBUG wifi list", "DEBUG wifi list")
    names = []
    b.until(5, lambda t: names.append(t) or t.startswith("[wifi] networks "))
    # Listed by number and SSID length only (no line names it): verify below says whether it is this one.
    saved = any(t.startswith("[wifi] network #") and f"ssid {len(ssid.encode())} B" in t for t in names)
    b.say(f"=== the network is {'REMEMBERED' if saved else 'NOT remembered'} (DEBUG wifi list)")
    if saved:   # byte for byte, answered with lengths and yes or no only
        b.send(f'DEBUG wifi verify "{ssid}" {password}', 'DEBUG wifi verify "<ssid>" <password>')
        verdict = b.until(5, lambda t: "[wifi] verify:" in t)
        saved = bool(verdict) and "MATCHES" in verdict
        b.say(f"=== the stored password {'matches .env' if saved else 'does NOT match .env'}")

    # 4. A check now.
    if args.check or args.check_url:
        url = f" {args.check_url}" if args.check_url else ""
        b.send(f"DEBUG ota check{url}", f"DEBUG ota check{url}")
    for _ in b.lines(args.seconds):
        pass
    port.close()
    sys.exit(0 if saved else 1)


if __name__ == "__main__":
    main()
