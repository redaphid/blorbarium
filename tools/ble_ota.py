# /// script
# requires-python = ">=3.9"
# dependencies = ["bleak>=0.22", "cryptography"]
# ///
"""Update badges over Bluetooth from this laptop, as the website does from a phone.

    uv run tools/ble_ota.py scan                           # badges in range: name, address, RSSI
    uv run tools/ble_ota.py scan --probe                   # ... and connect to each to read its firmware
    uv run tools/ble_ota.py status mossby                  # one badge's update state (CONFIRMs a trial this laptop began)
    uv run tools/ble_ota.py push mossby                    # the site's release onto one badge
    uv run tools/ble_ota.py push d4:05:92:77:1b:3d gilly   # several, one after another
    uv run tools/ble_ota.py push --all                     # every badge in range, nearest first
    uv run tools/ble_ota.py push mossby --manifest web/public/fw/lcd128/manifest.json   # a local release
    uv run tools/ble_ota.py push mossby --image ~/blorbarium-lcd128-0.7.13.bin          # its manifest beside it
    uv run tools/ble_ota.py push --dry-run                 # the whole flow against pretend badges

It speaks docs/ota.md exactly as web/public/ota.js does, over BlueZ through
bleak: request lines on the Nordic UART RX characteristic, replies and
`! OTA` events on TX, image bytes on 6E400004 (a 4-byte little-endian offset,
then up to `chunk` bytes). Per badge: OTA STATUS and HELLO; OTA BEGIN with
the manifest's fields and this laptop's token; the badge asks its holder
("Turn me around!" from 0.7.11, "Press the button" before); the image goes
in a 6 KB window past the last `at=`, back to `at` on a `gap` or 2 s of
quiet; OTA REBOOT; a reconnect and HELLO; OTA CONFIRM; STATUS until
`trial=0`, when the new image has kept itself. A dropped connection before
the restart is resumed: reconnect, the same BEGIN, carry on from `at`.

What it can push. Every image must be signed by the owner's key, which only
tools/ota_release.py reads; this tool never signs (docs/ota.md "The key"). The signature is in
the manifest, over `blorbota2 board version flags fmt size sha256`, so an
image is pushable only with the manifest tools/ota_release.py wrote for it:
the site's release (the default), or a release copied here (--manifest, or
--image with its manifest.json beside it). It cannot push a firmware.bin
built here, change a release's version or flags, re-push the version a badge
already runs, or downgrade, unless that image was signed with
--allow-older. It checks the signature against src/hw/ota_pubkey.h first;
the badge checks it again and refuses anything else (BAD_SIGNATURE).

The token. A session belongs to the token its BEGIN carried, so the token
for each badge and version is kept in ~/.config/blorbarium/ble-ota-tokens.json.
A rerun after this tool died resumes its own transfer, and `status` sends
the CONFIRM a new image on trial waits for (it rolls back after 5 minutes
without one).

A badge is named by its advertised name (its pet's name, lowercase; through
0.7.42, blorb-xxxx while an image was on trial) or by its Bluetooth address,
which an update never changes. The address is the MAC in badges.def plus one.
A badge is told apart from other devices by the UART service it advertises,
whatever its name.
"""
import argparse
import asyncio
import base64
import hashlib
import json
import os
import re
import secrets
import sys
import time
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import urljoin

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from flash_batch import read_badges  # noqa: E402

NUS = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"         # this laptop writes request lines
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"         # the badge notifies replies and events
OTA_DATA = "6e400004-b5a3-f393-e0a9-e50e24dcca9e"   # image writes, without response
SITE = "https://blorb.hypnodroid.com/fw/lcd128/manifest.json"
BOARD = "lcd128"
PUBKEY_H = ROOT / "src/hw/ota_pubkey.h"
TOKENS = Path.home() / ".config/blorbarium/ble-ota-tokens.json"
SPECIES = ("grungo", "mossby", "puffkin", "gilly", "mothwick", "mudge", "fizz", "hushwing", "tinker")
BADGE_SPECIES = read_badges()              # {wifi mac: species}; the Bluetooth address is that plus one
WINDOW = 6144                              # bytes in flight past the last at= heard (ota.js)
TURN_FROM = (0, 7, 11)                     # the first firmware whose question a turn answers (src/hw/turn_yes.h)
CONFIRM_S = 30                             # Session::kConfirmMs

# How each session ended (STATUS `last=`), and BEGIN's refusals, in words (docs/ota.md).
WHY = {
    "no_confirm": "nobody turned the badge or pressed its button within 30 s, so nothing changed",
    "busy": "the pet became busy (a clutch or a duel) before the yes, so nothing changed",
    "save_failed": "the badge could not save the pet first, so nothing was written",
    "aborted": "the update was aborted",
    "timeout": "5 minutes passed with no image byte",
    "overflow": "more bytes arrived than the size said",
    "bad_hash": "the bytes did not hash to the signed SHA-256",
    "bad_image": "the bytes hashed right but are not a bootable image",
    "flash_error": "the app slot refused to open, write or switch",
    "on_trial": "the running image went on trial before the yes",
    "lost_session": "the badge forgot the session (did it restart?)",
    "BAD_ARGS": "the badge could not read the BEGIN line",
    "WRONG_BOARD": "this image is for another board",
    "TOO_BIG": "the image is empty or bigger than the app slot",
    "TRIAL": "the badge is still checking its last update, and that one is not this laptop's to confirm; "
             "try again in 5 minutes",
    "BUSY": "the pet is busy (a clutch on the dish, or a duel); try again in a minute",
    "TOO_SOON": "the badge checked a signature less than 2 s ago",
    "BAD_SIGNATURE": "the badge does not trust this image's signature",
    "NOT_YOURS": "another phone or laptop (or a Wi-Fi install, on firmware that has one) owns the update in "
                 "progress on this badge; it is left alone. It ends by itself (30 s with no yes, 5 min with no "
                 "bytes); try again after",
    "OTA_ACTIVE": "another update (a phone's, another laptop's, or a Wi-Fi install on firmware that has one) holds "
                  "this badge; it is left alone, never aborted from here. It ends by itself (30 s with no yes, "
                  "5 min with no bytes); try again after",
    "UNKNOWN_VERB": "this badge has no updater (0.3.0): it needs one cable flash first",
    "unreachable": "the badge could not be reached again (out of range, or off)",
    "TIMEOUT": "the badge did not answer",
}


# ---- pure parts (tests/test_ble_ota.py) ----------------------------------------------


def kv(text):
    """`key=value` words of a reply or event, as ota.js reads them."""
    return dict(re.findall(r"([A-Za-z_]+)=(\S*)", text or ""))


def semver(v):
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", v or "")
    return tuple(int(x) for x in m.groups()) if m else None


def is_newer(a, b):
    x, y = semver(a), semver(b)
    return bool(x and y and x > y)


def b64url(b):
    return base64.urlsafe_b64encode(b).rstrip(b"=").decode()


def unb64url(s):
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def image_id(sha):
    """The badge names an image by the first four bytes of its hash, in hex (STATUS `id=`)."""
    return unb64url(sha)[:4].hex()


def parse_manifest(obj, board=BOARD):
    """A release's manifest, checked as ota.js checks it; ValueError says what is wrong."""
    m = obj if isinstance(obj, dict) else {}
    checks = [
        (m.get("board") == board, "board %r, not %s" % (m.get("board"), board)),
        (semver(m.get("version")) is not None, "version %r" % m.get("version")),
        (isinstance(m.get("flags"), int) and m["flags"] >= 0, "flags %r" % m.get("flags")),
        (isinstance(m.get("fmt"), int) and m["fmt"] >= 0, "fmt %r" % m.get("fmt")),
        (isinstance(m.get("size"), int) and m["size"] > 0, "size %r" % m.get("size")),
        (bool(re.fullmatch(r"[A-Za-z0-9_-]{43}", str(m.get("sha256", "")))), "sha256 is not 43 base64url characters"),
        (bool(re.fullmatch(r"[A-Za-z0-9_-]{86}", str(m.get("sig", "")))), "sig is not 86 base64url characters"),
        (isinstance(m.get("image"), str) and m["image"] != "", "no image named"),
    ]
    for ok, what in checks:
        if not ok:
            raise ValueError("bad manifest: %s" % what)
    return {k: m[k] for k in ("board", "version", "flags", "fmt", "size", "sha256", "sig", "image")}


def check_image(m, data):
    if len(data) != m["size"]:
        raise ValueError("the image is %d bytes, the manifest says %d" % (len(data), m["size"]))
    if b64url(hashlib.sha256(data).digest()) != m["sha256"]:
        raise ValueError("the image does not hash to the manifest's sha256")


def signed_text(m):
    """What the owner's key signs (docs/ota.md "Signing"; tools/ota_release.py signed_text)."""
    return ("blorbota2 %s %s %d %d %d %s" % (m["board"], m["version"], m["flags"], m["fmt"], m["size"],
                                             m["sha256"])).encode()


def embedded_point(header_text):
    """The 65-byte public key src/hw/ota_pubkey.h builds into every badge."""
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", header_text.split("{", 1)[1]))


def verify_signature(m, point):
    """True when the manifest's signature verifies against `point`; None without the cryptography package."""
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
    except ImportError:
        return None
    raw = unb64url(m["sig"])
    der = encode_dss_signature(int.from_bytes(raw[:32], "big"), int.from_bytes(raw[32:], "big"))
    try:
        ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), point).verify(
            der, signed_text(m), ec.ECDSA(hashes.SHA256()))
        return True
    except (InvalidSignature, ValueError):
        return False


def begin_line(m, token):
    return "OTA BEGIN %s %s %d %d %d %s %s %s" % (m["board"], m["version"], m["flags"], m["fmt"], m["size"],
                                                 m["sha256"], m["sig"], token)


def new_token():
    return b64url(secrets.token_bytes(16))   # 22 characters


def packet(image, off, n):
    """One data write: the offset, little-endian, then the bytes from it."""
    return off.to_bytes(4, "little") + bytes(image[off:off + n])


class LineReader:
    """Notifications are pieces of lines: buffer to each \\n (docs/ble.md "Framing")."""

    def __init__(self):
        self.buf = b""

    def feed(self, data):
        self.buf += bytes(data)
        *whole, self.buf = self.buf.split(b"\n")
        return [w.decode("utf-8", "replace").replace("\r", "") for w in whole if w.strip(b"\r")]


def parse_line(line):
    """("reply", id, "OK"|"ERR"|"+", body), ("event", text) or None."""
    if line.startswith("!"):
        return ("event", line[1:].strip())
    m = re.match(r"#([0-9a-fA-F]+) (\+|OK|ERR)(?: (.*))?$", line)
    return ("reply", int(m.group(1), 16), m.group(2), m.group(3) or "") if m else None


class Err(Exception):
    """`#id ERR <code> <TEXT>`."""

    def __init__(self, code, text):
        super().__init__("%s %s" % (code, text))
        self.code, self.text = code, text

    @classmethod
    def parse(cls, body):
        m = re.match(r"(\d+) ?(.*)", body)
        return cls(int(m.group(1)), m.group(2)) if m else cls(0, body)


class Dropped(Exception):
    """The connection went."""


class Failure(Exception):
    def __init__(self, reason):
        super().__init__(reason)
        self.reason = reason


def confirm_hint(fw):
    """What the person holding the badge has to do while it asks."""
    v = semver(fw)
    if v and v >= TURN_FROM:
        return ('The badge says "New version! Turn me around!". Hold it up (not lying flat), wait a moment, '
                "then turn it a quarter turn onto its side and hold it there a second. "
                "Pressing BOOT (behind the screen) answers too.")
    return ('The badge says "New version! Press the button". Press BOOT, the button behind the screen '
            "(at about 1 o'clock). This firmware (%s) does not take a turn yet." % (fw or "?"))


def why(reason):
    return WHY.get(reason, reason)


def bt_to_badge(addr):
    """(badge species from badges.def, wifi mac) for a Bluetooth address: the S3's BT MAC is its base plus one."""
    try:
        n = int(addr.replace(":", ""), 16) - 1
    except ValueError:
        return None, None
    mac = ":".join("%02x" % b for b in n.to_bytes(6, "big"))
    return BADGE_SPECIES.get(mac), mac


def is_badge(name, uuids):
    """The UART service or a blorb- or species name, either one: never the advertisement's exact contents,
    which a later firmware may add to (the wifi-ota branch puts Improv's service data and UUID in it)."""
    name = name or ""
    return NUS in [u.lower() for u in uuids or []] or name.startswith("blorb-") or name in SPECIES


@dataclass
class Seen:
    address: str
    name: str
    rssi: int
    badge: bool = True   # advertises the UART service and a blorb- or species name


def pick_targets(wanted, seen):
    """The badges named on the command line, in their order, by address or advertised name.

    ([Seen], [problems]): a name two badges share, or one not in range, is a problem, never a guess."""
    out, problems = [], []
    for w in wanted:
        if re.fullmatch(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", w):
            hit = [s for s in seen if s.address.upper() == w.upper()]
        else:
            hit = [s for s in seen if s.name.lower() == w.lower()]
        if not hit:
            problems.append("%s: not seen in the scan" % w)
        elif len(hit) > 1:
            problems.append("%s: %d badges have that name (%s); name one by its address"
                            % (w, len(hit), ", ".join(s.address for s in hit)))
        elif hit[0].address in [s.address for s in out]:
            problems.append("%s: named twice" % w)
        else:
            out.append(hit[0])
    return out, problems


class TokenStore:
    """One token per (badge address, version), kept across runs (in memory only for a dry run)."""

    def __init__(self, path=None):
        self.path = path
        self.tokens = {}
        if path and path.exists():
            try:
                self.tokens = json.loads(path.read_text())
            except ValueError:
                self.tokens = {}

    def __call__(self, address, version, make=True):
        key = "%s:%s" % (address.upper(), version)
        if key not in self.tokens and make:
            self.tokens[key] = new_token()
            self.save()
        return self.tokens.get(key)

    def save(self):
        if self.path:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            tmp = self.path.with_suffix(".tmp")
            tmp.write_text(json.dumps(self.tokens, indent=1) + "\n")
            os.replace(tmp, self.path)

    def note(self, address, ident, version):
        """This laptop BEGAN image `ident` (STATUS `id=`) on this badge, as `version`."""
        key = "%s#%s" % (address.upper(), ident)
        if self.tokens.get(key) != version:
            self.tokens[key] = version
            self.save()

    def for_badge(self, address):
        return BadgeTokens(self, address)


class BadgeTokens:
    """One badge's tokens: by version, and which images this laptop began on it."""

    def __init__(self, store, address):
        self.store, self.address = store, address.upper()

    def __call__(self, version, make=True):
        return self.store(self.address, version, make)

    def note(self, ident, version):
        self.store.note(self.address, ident, version)

    def owned(self, ident):
        """The token of a session this laptop began for image `ident`, else None (a phone's, a Wi-Fi install's)."""
        version = self.store.tokens.get("%s#%s" % (self.address, ident))
        return self(version, False) if version else None


def progress_text(at, size, rate):
    pct = at * 100 // size if size else 0
    eta = " %d s left" % ((size - at) / rate) if rate > 0 else ""
    return "  sending %3d%%  %7d / %d B  %5.1f KB/s%s" % (pct, at, size, rate / 1000, eta)


# ---- the release -----------------------------------------------------------------------


@dataclass
class Release:
    manifest: dict
    image: bytes
    source: str
    signature: object = None   # True, False, or None when not checked


def fetch(src):
    if re.match(r"https?://", src):
        req = urllib.request.Request(src, headers={"User-Agent": "blorbarium-ble-ota", "Cache-Control": "no-cache"})
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.read()
    return Path(src).expanduser().read_bytes()


def find_manifest(image_path, data):
    """The manifest beside a local image that names it (or its hash). None when there is none."""
    sha = b64url(hashlib.sha256(data).digest())
    for cand in [image_path.parent / "manifest.json", *sorted(image_path.parent.glob("*manifest*.json"))]:
        try:
            m = json.loads(cand.read_text())
        except (OSError, ValueError):
            continue
        if isinstance(m, dict) and (m.get("image") == image_path.name or m.get("sha256") == sha):
            return cand
    return None


def load_release(manifest_src=None, image_path=None, fetch=fetch, pubkey_h=PUBKEY_H):
    """A signed release: its manifest checked, the image matched to it, the signature checked against
    the key built into this tree. ValueError when it cannot be pushed."""
    data = None
    if image_path:
        image_path = Path(image_path).expanduser()
        data = image_path.read_bytes()
        if not manifest_src:
            found = find_manifest(image_path, data)
            if not found:
                raise ValueError(
                    "%s has no manifest beside it naming it, so it carries no signature. A badge takes only an "
                    "image the owner's key signed: release it with tools/ota_release.py, which writes the "
                    "manifest.json beside the image (or pass --manifest)."
                    % image_path)
            manifest_src = str(found)
    manifest_src = manifest_src or SITE
    try:
        m = parse_manifest(json.loads(fetch(manifest_src)))
    except ValueError as e:
        raise ValueError("%s: %s" % (manifest_src, e))
    if data is None:
        base = manifest_src if re.match(r"https?://", manifest_src) else str(Path(manifest_src).expanduser().parent) + "/"
        data = fetch(urljoin(base, m["image"]) if base.startswith("http") else str(Path(base) / m["image"]))
    check_image(m, data)
    ok = verify_signature(m, embedded_point(Path(pubkey_h).read_text()))
    return Release(m, data, manifest_src, ok)


# ---- one update: docs/ota.md "The page's side", as web/public/ota.js does it -----------


@dataclass
class Timing:
    stall: float = 2.0          # no progress for this long: ask STATUS
    confirm_poll: float = 1.0
    reboot_wait: float = 15.0   # how long to wait for the restart's drop
    trial_wait: float = 90.0    # the badge keeps itself 30 s after CONFIRM
    trial_poll: float = 2.0
    too_soon: float = 2.1


# Where a dropped connection sends each state: before the restart, the same BEGIN resumes; after REBOOT,
# the drop is the restart itself.
ON_DROP = {"begin": "resuming", "confirm": "resuming", "sending": "resuming",
           "finishing": "rebooting", "rebooting": "rebooting", "verifying": "rebooting", "trial": "rebooting"}


class Updater:
    """One update of one badge. port: send(cmd) -> OK body | raises Err/Dropped, write_data(bytes),
    reconnect(), max_data (the largest data write the link carries, or None). tokens: version -> token."""

    def __init__(self, port, manifest, image, tokens, fw=None, say=print, progress=None, timing=None):
        self.port, self.m, self.image, self.tokens = port, manifest, image, tokens
        self.token = tokens(manifest["version"])
        self.fw = fw                       # what it ran when we met: which question it asks
        self.say = say
        self.progress = progress or (lambda at, size: None)
        self.t = timing or Timing()
        self.dish = {"state": "idle", "at": 0, "last": "none"}
        self.chunk = 0
        self.rewind = None
        self.lost = False
        self.detour = self.aborted = self.waited = False
        self.outcome = None
        self.begins = self.rewinds = 0
        self.asked = False
        self.wake = asyncio.Event()
        port.on_event = self.event
        port.on_drop = self.dropped

    def event(self, text):
        if not text.startswith("OTA"):
            return
        k = self.absorb(text)
        if "at" in k and re.search(r"\bgap\b", text):
            self.rewind = self.dish["at"]
            self.rewinds += 1
        self.wake.set()

    def dropped(self):
        self.lost = True
        self.wake.set()

    def absorb(self, text):
        k = kv(text)
        if "state" in k:
            self.dish["state"] = k["state"]
        if "at" in k:
            self.dish["at"] = int(k["at"])
        if "last" in k:
            self.dish["last"] = k["last"]
        if k.get("chunk"):
            self.chunk = int(k["chunk"])
        return k

    async def sleep(self, s):
        self.wake.clear()
        try:
            await asyncio.wait_for(self.wake.wait(), max(0.0, s))
        except asyncio.TimeoutError:
            pass

    def finish(self, kind, fw):
        self.outcome = {"ok": True, "kind": kind, "fw": fw}
        return "done"

    def fail(self, reason):
        self.outcome = {"ok": False, "reason": reason}
        return "failed"

    async def status(self):
        return self.absorb(await self.port.send("OTA STATUS"))

    async def reconnect(self):
        self.say("  connection lost; reconnecting ...")
        try:
            await self.port.reconnect()
        except Exception as e:  # noqa: BLE001
            raise Failure("unreachable") from e
        self.lost = False
        self.say("  reconnected")

    async def run(self, state="begin"):
        while True:
            if state in ("done", "failed"):
                return self.outcome
            try:
                state = await getattr(self, state)()
            except Failure as e:
                state = self.fail(e.reason)
            except Exception as e:  # noqa: BLE001
                if isinstance(e, Dropped):
                    self.lost = True
                if self.lost and state in ON_DROP:
                    state = ON_DROP[state]
                else:
                    state = self.fail(e.text if isinstance(e, Err) else (str(e) or type(e).__name__))

    async def begin(self):
        m = self.m
        try:
            r = await self.port.send(begin_line(m, self.token))
        except Err as e:
            # Another image holds the session. If this laptop began it (an older release), end it; the question still
            # guards the new one. Anyone else's (a phone, a Wi-Fi install) is never aborted: its token is not ours.
            if e.text == "OTA_ACTIVE" and not self.aborted:
                self.aborted = True
                held = kv(await self.port.send("OTA STATUS")).get("id")
                token = getattr(self.tokens, "owned", lambda ident: None)(held)
                if token:
                    self.say("  an earlier update from this laptop (image %s) holds the badge; aborting it" % held)
                    await self.port.send("OTA ABORT %s" % token)
                    return "begin"
            if e.text == "NOT_NEWER":
                return self.finish("up_to_date", None)
            if e.text == "TRIAL" and not self.detour:   # an earlier update on trial: confirm it if ours, wait it out
                self.detour = True
                return "trial"
            if e.text == "TOO_SOON" and not self.waited:
                self.waited = True
                await self.sleep(self.t.too_soon)
                return "begin"
            raise Failure(e.text)
        self.begins += 1
        self.detour = False
        self.absorb(r)
        if hasattr(self.tokens, "note"):
            self.tokens.note(image_id(m["sha256"]), m["version"])
        if self.begins > 1:
            self.say("  resumed: the badge is at %s, %d of %d B" % (self.dish["state"], self.dish["at"], m["size"]))
        return self.next() or "confirm"

    def next(self):
        s = self.dish["state"]
        if s == "receiving":
            return "sending"
        if s == "ready":
            return "finishing"
        if s == "idle":
            return self.fail("lost_session" if self.dish["last"] == "none" else self.dish["last"])
        return None

    async def confirm(self):
        if not self.asked:
            self.asked = True
            self.say("  >>> %s" % confirm_hint(self.fw))
            self.say("  >>> waiting up to %d s for it ..." % CONFIRM_S)
        while True:
            await self.status()
            n = self.next()
            if n:
                if n == "sending" and self.dish["at"] == 0:
                    self.say("  yes: the badge said receiving")
                return n
            await self.sleep(self.t.confirm_poll)
            if self.lost:
                raise Dropped()

    async def sending(self):
        size = self.m["size"]
        per = self.chunk
        if self.port.max_data:
            per = min(per, self.port.max_data - 4)
        if per <= 0:
            raise Failure("the badge gave no chunk size")
        nxt = heard = self.dish["at"]
        moved = time.monotonic()
        self.rewind = None
        self.progress(heard, size)
        while True:
            if self.lost:
                raise Dropped()
            if self.dish["state"] != "receiving":
                self.progress(self.dish["at"], size)
                return self.next() or "confirm"
            if self.rewind is not None:
                nxt, self.rewind = self.rewind, None
            if self.dish["at"] != heard:
                heard = self.dish["at"]
                moved = time.monotonic()
                self.progress(heard, size)
            if nxt < size and nxt - self.dish["at"] < WINDOW:
                n = min(per, size - nxt)
                await self.port.write_data(packet(self.image, nxt, n))
                nxt += n
                continue
            quiet = time.monotonic() - moved
            if quiet < self.t.stall:
                await self.sleep(self.t.stall - quiet)
                continue
            await self.status()
            nxt = self.dish["at"]
            moved = time.monotonic()

    async def finishing(self):
        self.say("  the badge has the whole image and checked its hash (%d rewinds after lost writes); "
                 "telling it to restart" % self.rewinds)
        try:
            await self.port.send("OTA REBOOT %s" % self.token)
        except Err as e:
            if e.code != 409:
                raise
            await self.status()
            return self.next() or "confirm"
        return "rebooting"

    async def rebooting(self):
        until = time.monotonic() + self.t.reboot_wait
        while not self.lost and time.monotonic() < until:
            await self.sleep(until - time.monotonic())
        if self.lost:
            await self.reconnect()
        return "verifying"

    async def verifying(self):
        fw = kv(await self.port.send("HELLO")).get("fw")
        if fw == self.m["version"]:
            self.say("  it came back as %s, on trial; confirming it from here" % fw)
            return "trial"
        if self.detour:
            return "begin"
        await self.status()   # still waiting to restart (busy), or the drop was not the restart
        if self.dish["state"] == "ready":
            return "finishing"
        return self.finish("rolled_back", fw)

    async def trial(self):
        until = time.monotonic() + self.t.trial_wait
        asked = False
        while True:
            k = await self.status()
            if k.get("trial") == "1" and not asked:
                asked = True
                if await confirm_trial(self.port, self.tokens, k) == "not_yours":
                    raise Failure("TRIAL")
                if self.detour:
                    self.say("  an earlier update (%s) is on trial; waiting for it to keep itself" % k.get("fw"))
            if k.get("trial") != "1":
                if k.get("fw") == self.m["version"]:
                    return self.finish("updated", k.get("fw"))
                return "begin" if self.detour else "verifying"
            if time.monotonic() > until:
                return self.finish("unconfirmed", k.get("fw"))
            await self.sleep(self.t.trial_poll)
            if self.lost:
                raise Dropped()

    async def resuming(self):
        await self.reconnect()
        return "begin"


async def confirm_trial(port, tokens, status):
    """CONFIRM an image on trial with this laptop's token for its version: "confirmed", "not_yours" or None."""
    if status.get("trial") != "1":
        return None
    token = tokens(status.get("fw"), False)
    if not token:
        return "not_yours"
    try:
        await port.send("OTA CONFIRM %s" % token)
        return "confirmed"
    except Err as e:
        if e.code == 403:
            return "not_yours"
        raise


# ---- the radio: bleak over BlueZ --------------------------------------------------------


class BlePort:
    """One badge's Nordic UART link, reconnectable to the same address."""

    def __init__(self, address):
        self.address = address.upper()
        self.client = None
        self.lost = True
        self.name = ""
        self.max_data = None
        self.on_event = lambda text: None
        self.on_drop = lambda: None
        self.pending = {}
        self.ids = 0
        self.lock = None
        self.small_writes = False

    async def open(self, scan_s=10.0):
        from bleak import BleakClient, BleakScanner
        self.lock = self.lock or asyncio.Lock()
        dev = await BleakScanner.find_device_by_address(self.address, timeout=scan_s)
        if not dev:
            raise Dropped("%s not seen in %.0f s" % (self.address, scan_s))
        self.name = dev.name or ""
        self.reader = LineReader()
        client = BleakClient(dev, disconnected_callback=self._gone, timeout=20.0)
        await client.connect()
        try:
            await client.start_notify(TX, self._notify)
            ch = client.services.get_characteristic(OTA_DATA)
            self.max_data = None
            if ch is not None:
                for _ in range(10):   # BlueZ may take a moment to learn the MTU
                    if ch.max_write_without_response_size > 20:
                        self.max_data = ch.max_write_without_response_size
                        break
                    await asyncio.sleep(0.2)
        except BaseException:
            await client.disconnect()
            raise
        self.client = client
        self.lost = False

    def _gone(self, client):
        if client is not self.client:
            return
        self.lost = True
        for fut in self.pending.values():
            if not fut.done():
                fut.set_exception(Dropped("disconnected"))
        self.on_drop()

    def _notify(self, _sender, data):
        for line in self.reader.feed(data):
            p = parse_line(line)
            if not p:
                continue
            if p[0] == "event":
                self.on_event(p[1])
                continue
            _, i, kind, body = p
            if kind == "+":
                continue
            if i == 0 and kind == "ERR" and len(self.pending) == 1:   # #0 ERR: a line whose id did not parse
                i = next(iter(self.pending))
            fut = self.pending.get(i)
            if fut and not fut.done():
                fut.set_result((kind, body))

    async def _write_line(self, text):
        data = text.encode()
        if not self.small_writes:
            try:
                return await self.client.write_gatt_char(RX, data, response=True)
            except Exception:  # noqa: BLE001
                if self.lost or not self.client.is_connected:
                    raise
                self.small_writes = True   # an MTU stuck at 23: 20-byte writes for this link
        for i in range(0, len(data), 20):
            await self.client.write_gatt_char(RX, data[i:i + 20], response=True)

    async def send(self, cmd, timeout=8.0):
        async with self.lock:   # the badge answers each line before it reads the next
            if self.lost or not self.client:
                raise Dropped("not connected")
            self.ids = self.ids % 0xFFFFFF + 1
            i = self.ids
            fut = asyncio.get_running_loop().create_future()
            self.pending[i] = fut
            try:
                await self._write_line("#%x %s\n" % (i, cmd))
                kind, body = await asyncio.wait_for(fut, timeout)
            except asyncio.TimeoutError:
                if self.lost:
                    raise Dropped("disconnected")
                raise Err(0, "TIMEOUT")
            except Dropped:
                raise
            except Exception as e:  # noqa: BLE001
                if self.lost or not self.client.is_connected:
                    raise Dropped(str(e))
                raise
            finally:
                self.pending.pop(i, None)
        if kind == "OK":
            return body
        raise Err.parse(body)

    async def write_data(self, data):
        if self.lost or not self.client:
            raise Dropped("not connected")
        try:
            await self.client.write_gatt_char(OTA_DATA, data, response=False)
        except Exception as e:  # noqa: BLE001
            if self.lost or not self.client.is_connected:
                raise Dropped(str(e))
            raise

    async def reconnect(self, seconds=120.0):
        """Back to the same address, for about two minutes (a restart takes a few seconds)."""
        await self.close()
        until, wait = time.monotonic() + seconds, 0.5
        while True:
            try:
                return await self.open(scan_s=min(10.0, max(1.0, until - time.monotonic())))
            except Exception:  # noqa: BLE001
                if time.monotonic() + wait > until:
                    raise
                await asyncio.sleep(wait)
                wait = min(wait * 2, 5.0)

    async def close(self):
        client, self.client = self.client, None
        self.lost = True
        if client:
            try:
                await client.disconnect()
            except Exception:  # noqa: BLE001
                pass


async def ble_scan(seconds, wide=False):
    from bleak import BleakScanner
    found = await BleakScanner.discover(timeout=seconds, return_adv=True)
    rows = []
    for addr, (dev, adv) in found.items():
        name = adv.local_name or dev.name or ""
        badge = is_badge(name, adv.service_uuids)
        if wide or badge:
            rows.append(Seen(addr.upper(), name, adv.rssi, badge))
    return sorted(rows, key=lambda s: -s.rssi)


# ---- pretend badges for --dry-run and the tests: ota::Session, in Python ---------------


class FakeBadge:
    """lib/ota/src/session.cpp's states and checks, with a pair of pretend hands. `scale` shrinks its clocks.

    hands: seconds after the question that the holder turns it (None: nobody does). drop_at: the fraction
    of the image at which the connection drops once. lose: data writes (by count) that never arrive.
    fail_trial: the new image rolls itself back at boot. key: the point it trusts."""

    def __init__(self, name, address, fw, key, hands=2.0, drop_at=None, lose=(), fail_trial=False, scale=1.0,
                 keep_s=3.0, board=BOARD, updater=True, busy=False):
        self.name, self.address, self.fw, self.key, self.board = name, address, fw, key, board
        self.hands, self.drop_at, self.lose, self.fail_trial = hands, drop_at, set(lose), fail_trial
        self.s, self.keep_s, self.has_updater, self.busy = scale, keep_s, updater, busy
        self.state, self.last, self.trial, self.rolledback = "idle", "none", False, None
        self.gen, self.writes, self.last_verify, self.confirmed = 0, 0, None, None
        self.port = None
        self.say = None     # where a dry run says what the pretend hands did
        self.log = []       # what it was sent, for the tests
        self.up_at = 0.0    # restarting until this time
        self.boot = time.monotonic()

    now = staticmethod(time.monotonic)

    def emit(self, text):
        if self.port:
            self.port.deliver("! OTA " + text)

    def handle(self, line):
        self.tick()
        self.log.append(line)
        m = re.match(r"#([0-9a-f]+) (\S+) ?(.*)$", line)
        rid, verb, rest = m.group(1), m.group(2), m.group(3).split()
        ok = lambda body: "#%s OK %s" % (rid, body)          # noqa: E731
        err = lambda c, t: "#%s ERR %d %s" % (rid, c, t)     # noqa: E731
        if verb == "HELLO":
            return ok("fw=%s fmt=1 lineage=000000000000 gen=0 name=%s phase=creature feats=0x00"
                      % (self.fw, self.name.title()))
        if verb != "OTA" or not self.has_updater or not rest:
            return err(404, "UNKNOWN_VERB")
        sub, args = rest[0], rest[1:]
        if sub == "STATUS":
            return ok(self.status())
        if sub == "BEGIN":
            return self.begin(args, ok, err)
        token = args[0] if args else None
        if sub == "CONFIRM":
            if not self.trial:
                return ok("trial=0")
            if token != self.handover:
                return err(403, "NOT_YOURS")
            self.confirmed = self.confirmed or self.now()
            return ok("trial=1")
        if sub not in ("ABORT", "REBOOT"):
            return err(400, "BAD_ARGS")
        if self.state != "idle" and token != self.owner:
            return err(403, "NOT_YOURS")
        if sub == "ABORT":
            if self.state != "idle":
                self.end("aborted")
            return ok("state=idle")
        if self.state != "ready":
            return err(409, "NOT_READY")
        asyncio.get_running_loop().call_later(0.3 * self.s, self.restart)
        return ok("rebooting")

    def begin(self, a, ok, err):
        if len(a) != 8 or not semver(a[1]) or not all(x.isdigit() for x in a[2:5]) or len(a[5]) != 43 \
                or len(a[6]) != 86 or len(a[7]) != 22:
            return err(400, "BAD_ARGS")
        im = {"board": a[0], "version": a[1], "flags": int(a[2]), "fmt": int(a[3]), "size": int(a[4]),
              "sha256": a[5], "sig": a[6]}
        if self.state != "idle":
            if im != self.image:
                return err(409, "OTA_ACTIVE")
            if a[7] != self.owner:
                return err(403, "NOT_YOURS")
            self.peer = self.gen     # the resume moves the data to this connection
            return ok(self.status())
        if im["board"] != self.board:
            return err(403, "WRONG_BOARD")
        if not is_newer(im["version"], self.fw) and not im["flags"] & 1:
            return err(409, "NOT_NEWER")
        if not 0 < im["size"] <= 0x400000:
            return err(413, "TOO_BIG")
        if self.trial:
            return err(409, "TRIAL")
        if self.busy:
            return err(409, "BUSY")
        if self.last_verify is not None and self.now() - self.last_verify < 2.0 * self.s:
            return err(429, "TOO_SOON")
        self.last_verify = self.now()
        if self.key is not None and verify_signature(im, self.key) is False:
            return err(403, "BAD_SIGNATURE")
        self.image, self.owner, self.peer, self.since = im, a[7], self.gen, self.now()
        self.state, self.at = "confirm", 0
        if self.hands is not None:
            asyncio.get_running_loop().call_later(self.hands * self.s, self.press)
        return ok(self.status())

    def status(self):
        chunk = 240
        if self.state == "idle":
            return "state=idle board=%s fw=%s chunk=%d trial=%d last=%s rolledback=%s" % (
                self.board, self.fw, chunk, self.trial, self.last, self.rolledback or "none")
        at = self.at if self.state == "receiving" else self.image["size"] if self.state == "ready" else 0
        return "state=%s at=%d size=%d id=%s chunk=%d" % (self.state, at, self.image["size"],
                                                          image_id(self.image["sha256"]), chunk)

    def press(self):
        if self.state != "confirm" or self.now() - self.since < 1.5 * self.s:
            return
        if self.say:
            self.say("  (dry run: the pretend hands %s)" % ("turn it" if semver(self.fw) >= TURN_FROM else "press BOOT"))
        self.state, self.at, self.heard, self.gap_said = "receiving", 0, self.now(), 0.0
        self.got, self.hash = bytearray(), hashlib.sha256()
        self.emit("state=receiving at=0")

    def data(self, p, peer):
        self.tick()
        self.writes += 1
        if self.writes in self.lose or self.state != "receiving" or len(p) < 5 or peer != self.peer:
            return
        off, b = int.from_bytes(p[:4], "little"), p[4:]
        if off < self.at:
            return
        if off > self.at:
            if self.now() - self.gap_said >= 0.2 * self.s:
                self.gap_said = self.now()
                self.emit("at=%d gap" % self.at)
            return
        if len(b) > self.image["size"] - self.at:
            return self.end("overflow")
        before = self.at
        self.got += b
        self.hash.update(b)
        self.at += len(b)
        self.heard = self.now()
        if self.at == self.image["size"]:
            if b64url(self.hash.digest()) != self.image["sha256"]:
                return self.end("bad_hash")
            if self.got[0] != 0xE9:
                return self.end("bad_image")
            self.handover = self.owner
            self.state, self.since = "ready", self.now()
            self.emit("at=%d" % self.at)
            self.emit("state=ready")
            return
        if self.at // 4096 != before // 4096:
            self.emit("at=%d" % self.at)
        if self.drop_at is not None and self.at >= self.drop_at * self.image["size"]:
            self.drop_at = None
            self.port.drop()

    def end(self, last):
        self.state, self.last = "idle", last
        self.emit("state=idle last=%s" % last)

    def tick(self):
        t = self.now()
        if self.state == "confirm" and t - self.since >= CONFIRM_S * self.s:
            self.end("no_confirm")
        elif self.state == "ready" and t - self.since >= 20 * self.s:
            self.restart()
        if self.trial and self.confirmed and t - self.confirmed >= self.keep_s * self.s:
            self.trial, self.rolledback = False, None
        elif self.trial and not self.confirmed and t - self.boot >= 300 * self.s:
            self.roll_back()

    def restart(self):
        if self.state != "ready":
            return
        self.old, self.fw = self.fw, self.image["version"]
        self.state, self.last, self.trial, self.confirmed = "idle", "none", True, None
        self.name_before = self.name   # a trial keeps the pet's name (0.7.42 and older said blorb-xxxx)
        self.boot, self.up_at = self.now(), self.now() + 1.0 * self.s
        if self.port:
            self.port.drop()
        if self.fail_trial:
            self.roll_back()

    def roll_back(self):
        self.rolledback, self.fw, self.trial = self.fw, self.old, False
        self.name = self.name_before
        self.up_at = self.now() + 1.0 * self.s
        if self.port:
            self.port.drop()


class FakePort:
    """The radio to a FakeBadge: the same face as BlePort."""

    def __init__(self, badge):
        self.badge, badge.port = badge, self
        self.address, self.name = badge.address, badge.name
        self.lost = True
        self.max_data = 244
        self.on_event = lambda text: None
        self.on_drop = lambda: None
        self.dropped = 0

    def deliver(self, line):
        if not self.lost:
            p = parse_line(line)
            asyncio.get_running_loop().call_soon(self.on_event, p[1])

    def drop(self):
        if not self.lost:
            self.lost = True
            self.dropped += 1
            asyncio.get_running_loop().call_soon(self.on_drop)

    async def open(self):
        await asyncio.sleep(max(0.0, self.badge.up_at - time.monotonic()))
        self.badge.gen += 1
        self.name = self.badge.name
        self.lost = False

    async def reconnect(self):
        await asyncio.sleep(0.05 * self.badge.s)
        await self.open()

    async def send(self, cmd, timeout=8.0):
        await asyncio.sleep(0)
        if self.lost:
            raise Dropped("not connected")
        reply = self.badge.handle("#1 " + cmd)
        _, _, kind, body = parse_line(reply)
        await asyncio.sleep(0)
        if self.lost:
            raise Dropped("disconnected")
        if kind == "OK":
            return body
        raise Err.parse(body)

    async def write_data(self, data):
        await asyncio.sleep(0)
        if self.lost:
            raise Dropped("not connected")
        self.badge.data(bytes(data), self.badge.gen)

    async def close(self):
        self.lost = True


def fake_fleet(names, manifest, key, scale=1.0):
    """Pretend badges for a dry run, at the table's addresses: the first runs 0.7.11 and drops at 40% with
    three lost writes, the second 0.7.10 (a BOOT press), the third already runs the release."""
    table = {s: mac for mac, s in BADGE_SPECIES.items()}
    v = semver(manifest["version"])
    older = "%d.%d.%d" % (v[0], v[1], max(0, v[2] - 1))
    plans = [dict(fw="0.7.11" if is_newer(manifest["version"], "0.7.11") else older, drop_at=0.4, lose=(40, 41, 90)),
             dict(fw="0.7.10" if is_newer(manifest["version"], "0.7.10") else older),
             dict(fw=manifest["version"])]
    out = []
    for i, n in enumerate(names or ["mossby", "puffkin", "gilly"]):
        mac = table.get(n, "d4:05:92:77:%02x:%02x" % (0x30, 0x10 + i))
        addr = ":".join("%02X" % b for b in (int(mac.replace(":", ""), 16) + 1).to_bytes(6, "big"))
        out.append(FakeBadge(n, addr, key=key, scale=scale, **plans[i % len(plans)]))
    return out


# ---- the command line ------------------------------------------------------------------


class Screen:
    """Lines, and one progress line redrawn in place on a terminal."""

    def __init__(self):
        self.tty = sys.stdout.isatty()
        self.t0 = self.at0 = None
        self.shown = -1

    def say(self, s):
        if self.shown >= 0 and self.tty:
            print()
        self.shown = -1
        print(s, flush=True)

    def progress(self, at, size):
        now = time.monotonic()
        if self.t0 is None or at < (self.at0 or 0):
            self.t0, self.at0 = now, at
        rate = (at - self.at0) / (now - self.t0) if now > self.t0 else 0
        pct = at * 100 // size
        if self.tty:
            print("\r" + progress_text(at, size, rate), end="", flush=True)
            self.shown = pct
        elif pct // 10 != self.shown // 10 or at == size:
            print(progress_text(at, size, rate), flush=True)
            self.shown = pct

    def reset(self):
        if self.shown >= 0 and self.tty:
            print()
        self.t0 = self.at0 = None
        self.shown = -1


async def ask(prompt):
    try:
        return (await asyncio.to_thread(input, prompt)).strip().lower()
    except EOFError:
        return "q"


def label(s):
    species, mac = bt_to_badge(s.address)
    return "%s (%s%s)" % (s.name or "?", s.address, ", badges.def %s" % species if species else "")


async def update_one(port, rel, tokens, screen, timing=None):
    """One badge, connected: what it runs, then the update. The outcome dict."""
    m = rel.manifest
    try:
        st = kv(await port.send("OTA STATUS"))
    except Err as e:
        return {"ok": False, "reason": e.text, "before": "?"}
    hello = {}
    try:
        hello = kv(await port.send("HELLO"))
    except Err:
        pass
    fw = st.get("fw") or hello.get("fw")
    screen.say("  runs %s (pet %s), state=%s trial=%s last=%s rolledback=%s" % (
        fw or "?", hello.get("name", "?"), st.get("state"), st.get("trial", "-"), st.get("last", "-"),
        st.get("rolledback", "-")))
    if st.get("board") and st["board"] != m["board"]:
        return {"ok": False, "reason": "WRONG_BOARD", "before": fw}
    halfway = st.get("state", "idle") != "idle" and st.get("id") == image_id(m["sha256"])
    start = "begin"
    if st.get("trial") == "1":   # as the page does on connect: an image on trial waits for its page's CONFIRM
        c = await confirm_trial(port, tokens, st)
        screen.say("  %s is on trial: %s" % (fw, "this laptop sent it, so CONFIRM sent" if c == "confirmed" else
                                             "not this laptop's to confirm; it keeps itself or rolls back by itself"))
        if fw == m["version"]:
            if c != "confirmed":
                return {"ok": False, "reason": "TRIAL", "before": fw}
            start = "trial"
    if not halfway and st.get("trial") != "1" and fw and not is_newer(m["version"], fw) and not m["flags"] & 1:
        return {"ok": True, "kind": "up_to_date", "fw": fw, "before": fw}
    if st.get("rolledback") == m["version"]:
        screen.say("  note: %s rolled back on this badge before; trying again" % m["version"])
    screen.say("  %s %s -> %s" % ("carrying on" if halfway else "updating", fw, m["version"]))
    u = Updater(port, m, rel.image, tokens, fw=fw, say=screen.say, progress=screen.progress, timing=timing)
    out = await u.run(start)
    screen.reset()
    out["before"] = fw
    return out


def verdict(name, out, m):
    if out["ok"]:
        return {
            "updated": "UPDATED to %s and kept: the new image passed its trial. The pet is fine." % out["fw"],
            "up_to_date": "up to date (%s); nothing sent" % out.get("fw", m["version"]),
            "rolled_back": "ROLLED BACK: %s did not keep itself, and the badge went back to %s. The pet is as it was."
                           % (m["version"], out.get("fw")),
            "unconfirmed": "%s is still checking itself (trial=1). Run `status %s` within 5 minutes, or it rolls back."
                           % (out.get("fw"), name),
        }[out["kind"]]
    return "FAILED: %s" % why(out["reason"])


def describe_release(rel):
    m = rel.manifest
    sig = {True: "verifies against src/hw/ota_pubkey.h", False: "DOES NOT verify against src/hw/ota_pubkey.h",
           None: "not checked here (no cryptography package)"}[rel.signature]
    return "%s %s, %d B, sha256 %s (id %s), from %s; signature %s" % (
        m["board"], m["version"], m["size"], m["sha256"][:12] + "...", image_id(m["sha256"]), rel.source, sig)


async def cmd_scan(args):
    if args.dry_run:
        rows = [Seen(b.address, b.name, -40 - 9 * i) for i, b in enumerate(fake_fleet(None, {"version": "0.7.12"}, None))]
    else:
        rows = await ble_scan(args.seconds, args.wide)
    if not rows:
        print("no badges seen in %.0f s (each advertises the UART service and its species as its name)" % args.seconds)
        return 1
    print("%-10s %-17s %5s  %s" % ("name", "address", "rssi", "badges.def (address - 1)"))
    for s in rows:
        species, mac = bt_to_badge(s.address)
        print("%-10s %-17s %5d  %s" % (s.name or "?", s.address, s.rssi, "%s %s" % (mac, species) if species else ""))
    print("The firmware version is not advertised; --probe connects to each badge to read it.")
    if args.probe and not args.dry_run:
        for s in rows:
            port = BlePort(s.address)
            try:
                await port.open()
                st = kv(await port.send("OTA STATUS"))
                hello = kv(await port.send("HELLO"))
                print("%-10s %s fw=%s state=%s trial=%s last=%s rolledback=%s pet=%s" % (
                    s.name, s.address, st.get("fw") or hello.get("fw"), st.get("state"), st.get("trial"),
                    st.get("last"), st.get("rolledback"), hello.get("name")))
            except Exception as e:  # noqa: BLE001
                print("%-10s %s could not read: %s" % (s.name, s.address, e))
            finally:
                await port.close()
    return 0


async def cmd_status(args):
    store = TokenStore(TOKENS)
    seen = await ble_scan(args.seconds, wide=True)
    targets, problems = pick_targets([args.badge], seen)
    if problems:
        print("\n".join(problems))
        return 1
    port = BlePort(targets[0].address)
    try:
        await port.open()
        st = kv(await port.send("OTA STATUS"))
        print("%s: %s" % (label(targets[0]), " ".join("%s=%s" % kv_ for kv_ in st.items())))
        c = await confirm_trial(port, store.for_badge(port.address), st)
        if c == "confirmed":
            print("on trial: sent this laptop's CONFIRM; it keeps itself about 30 s after boot")
        elif c == "not_yours":
            print("on trial, but not from this laptop: the phone or laptop that sent it must confirm it")
    except Err as e:
        print("%s: %s" % (label(targets[0]), why(e.text)))
        return 1
    finally:
        await port.close()
    return 0


async def cmd_push(args):
    screen = Screen()
    try:
        rel = load_release(args.manifest, args.image)
    except (ValueError, OSError) as e:
        print("cannot push: %s" % e)
        return 1
    print("release: " + describe_release(rel))
    if rel.signature is False and not args.unverified:
        print("cannot push: every badge built from this tree would refuse it (BAD_SIGNATURE). --unverified sends it "
              "anyway, for a badge built with another key.")
        return 1
    if args.dry_run:
        fakes = fake_fleet(args.badges, rel.manifest, embedded_point(PUBKEY_H.read_text()) if rel.signature else None)
        seen = [Seen(b.address, b.name, -40 - 9 * i) for i, b in enumerate(fakes)]
        ports = {b.address: FakePort(b) for b in fakes}
        for b in fakes:
            b.say = screen.say
        store = TokenStore(None)
        print("(dry run: %d pretend badges; nothing is sent over the air)" % len(fakes))
    else:
        print("scanning %.0f s ..." % args.seconds)
        seen = await ble_scan(args.seconds, wide=True)
        ports, store = {}, TokenStore(TOKENS)
    if args.all:
        targets, problems = [s for s in seen if s.badge], []
    else:
        targets, problems = pick_targets(args.badges or ([s.name for s in seen] if args.dry_run else []), seen)
    for p in problems:
        print("  " + p)
    if problems or not targets:
        print("nothing to push to%s" % ("" if targets else ": name badges, or --all"))
        return 1
    results = []
    for i, s in enumerate(targets):
        print("\n[%d/%d] %s" % (i + 1, len(targets), label(s)))
        if args.prompt and not args.dry_run:
            a = await ask("  Hold it up in front of you, close to this laptop. Enter to start, s to skip, q to quit: ")
            if a == "q":
                break
            if a == "s":
                results.append((s, {"ok": False, "reason": "skipped", "before": "?"}))
                continue
        while True:
            port = ports.get(s.address) or BlePort(s.address)
            try:
                await port.open()
                out = await update_one(port, rel, store.for_badge(s.address), screen,
                                       Timing() if not args.dry_run else Timing(trial_poll=1.0))
            except Exception as e:  # noqa: BLE001
                out = {"ok": False, "reason": "could not connect: %s" % (e or type(e).__name__), "before": "?"}
            finally:
                await port.close()
            print("  " + verdict(s.name, out, rel.manifest))
            if out["ok"] or not args.prompt or args.dry_run:
                break
            a = await ask("  r to retry (an update in progress resumes), s to skip, q to quit [r]: ")
            if a in ("s", "q"):
                break
        results.append((s, out))
        if not out["ok"] and args.prompt and not args.dry_run and a == "q":
            break
    print("\nsummary (release %s):" % rel.manifest["version"])
    for s, out in results:
        print("  %-10s %s  %s -> %s" % (s.name, s.address, out.get("before", "?"),
                                        verdict(s.name, out, rel.manifest)))
    return 0 if all(o["ok"] for _, o in results) and len(results) == len(targets) else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("scan", help="list badges in range")
    s.add_argument("--seconds", type=float, default=8.0)
    s.add_argument("--wide", action="store_true", help="every device with a name, not only badges")
    s.add_argument("--probe", action="store_true", help="connect to each badge and read its firmware and update state")
    s.add_argument("--dry-run", action="store_true")
    st = sub.add_parser("status", help="one badge's update state; CONFIRMs a trial this laptop began")
    st.add_argument("badge", help="advertised name or Bluetooth address")
    st.add_argument("--seconds", type=float, default=8.0)
    p = sub.add_parser("push", help="update badges, one after another")
    p.add_argument("badges", nargs="*", help="advertised names or Bluetooth addresses, in order")
    p.add_argument("--all", action="store_true", help="every badge in range, nearest first")
    p.add_argument("--manifest", help="a release's manifest.json, path or URL (default %s)" % SITE)
    p.add_argument("--image", help="a local signed image; its manifest must be beside it (or --manifest)")
    p.add_argument("--seconds", type=float, default=8.0, help="how long to scan first (default 8)")
    p.add_argument("--no-prompt", dest="prompt", action="store_false",
                   help="do not wait for Enter before each badge, nor ask after a failure")
    p.add_argument("--unverified", action="store_true",
                   help="send a release even when its signature does not verify against src/hw/ota_pubkey.h")
    p.add_argument("--dry-run", action="store_true", help="pretend badges; nothing goes over the air")
    args = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    try:
        return asyncio.run({"scan": cmd_scan, "status": cmd_status, "push": cmd_push}[args.cmd](args))
    except KeyboardInterrupt:
        print("\nstopped. An update in progress waits on the badge (5 min); run the same push again to resume it.")
        return 130


if __name__ == "__main__":
    sys.exit(main())
