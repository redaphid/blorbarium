# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Tonight's end-to-end acceptance on one real badge (issue #55), over its cable.

    uv run scripts/test/acceptance.py --serial 5B91046680 --log acceptance.log
    uv run scripts/test/acceptance.py --port /dev/ttyACM0 --serial 5B91046680 --phase wifi \\
        --test-manifest https://blorb.hypnodroid.com/fw-test/lcd128/manifest.json --log acceptance.log
    uv run scripts/test/acceptance.py --serial 5B91046680 --phase lifecycle --generations 3 --log acceptance.log
    uv run scripts/test/acceptance.py --serial 5B91046680 --phase offline,server --log acceptance.log

Phases (--phase, comma separated; `all` runs them in this order):

  wifi       DEBUG wifi set from .env (the SSID and password never printed or
             logged: every line is masked), DEBUG wifi lists it, DEBUG ota check
             against the live manifest (a refusal: up_to_date, or the old key's
             signature refused), then DEBUG ota check <--test-manifest>: download,
             verify, DEBUG power low so the restart happens dark, the new version
             boots on trial, confirms over Wi-Fi, and keeps the same pet (GENOME,
             gen) and the same home (DEBUG home). Each stage timed. After the
             live check's window, DEBUG air open/close: fight mode's radio fits.
  offline    The networks swapped for one that is not in range, DEBUG wifi due
             (a reboot on a firmware without it) so a check is due, then dark:
             one short scan per window, the backoff growing (next_check_in,
             absent_streak), each later check made due by DEBUG wifi due with
             the streak kept, 80 MHz while dark, DEBUG air open/close after the
             windows, and DEBUG fps / DEBUG latency awake unchanged against a
             baseline. The real network is given back from .env at the end.
  lifecycle  DESTROYS THE PET (the owner allows it). For --generations N:
             hatch, grow, idle variety, night only with evidence, feeding
             (DEBUG chem hunger), rot refused unless starving (DEBUG chem makes
             him starving once he has refused it hungry), sleep and wake (a
             knock; DEBUG pickup with DEBUG trace: picked_up and nothing else),
             elder, death, vigil, clutch, a badge left lying picks nothing, a
             lean plus a knock picks the aimed egg, the hatch of the next
             generation, LINEAGE's new rows, STATS carrying the parent's life,
             and a reboot that keeps him.
  server     STATE's mac= and species= (the board's own MAC, --mac only when
             STATE has none), the current genome POSTed to /api/pet-lines (a
             pack with every situation covered), then with .env's admin_token
             the admin API: the creature record for that MAC, matching STATE's
             species and gen, and its stats (uploaded from the badge's STATS
             json by this script unless --no-upload-stats).

The verbs come from test-verbs (DEBUG chem, DEBUG pickup, STATE species=/mac=),
test-verbs-wifi (DEBUG wifi due) and wifi-ota-2 (DEBUG wifi, ota, home, air). A
firmware without one is named at the end; chem, pickup and wifi due fall back
to the slower way (warps, DEBUG cradle, a reboot).

The port is opened once and held (DTR and RTS low, so the open itself should
not reset the board). If the board drops off the bus it is found again by its
USB serial. Nothing runs unless the port's USB serial is --serial.
Exit 0 when nothing failed. The table at the end has every step with its
evidence; --log has the whole conversation, masked.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ENV = Path("/home/redaphid/Projects/blorbarium/.env")
SITE = "https://blorb.hypnodroid.com"
LIVE_MANIFEST = SITE + "/fw/lcd128/manifest.json"
DEFAULT_MAC = "3c:0f:02:6f:2a:ec"   # the test board 5B91046680 (issue #55)
FAKE_SSID = "blorb-e2e-nosuchnet"
SECRET_KEYS = ("ssid", "password", "admin_token")

CRASH = re.compile(r"Guru Meditation|abort\(\)|Backtrace:|panic'ed|Brownout|stack overflow|rst:0x(7|8|9|f|10) ")
BANNER = re.compile(r"\[hw\] blorbarium \S+ fw=(\S+) fmt=(\d+)")
BOOT = re.compile(r"\[hw\] boot=(\w+) occupant=(\w+) gen=(\d+) genome=([0-9a-f]{8}) age=(\d+) species=(\S+)")
UNKNOWN_VERB = r"\[hw\] debug: unknown verb '(\w+)'"
VIGIL_TICKS = 30 * 600            # creature.h Clutch::kVigilTicks (30 pet minutes of 100 ms ticks)
STARVING = 800                    # creature.h kEatsRotAt, in permille
STAGE_VERB = {"baby": "hatchling", "child": "child", "adult": "adult", "elder": "elder"}
STAGE_ORDER = ["baby", "child", "adult", "elder"]


class Fail(Exception):
    pass


class MissingVerb(Fail):
    pass


# ---- secrets -----------------------------------------------------------------------------

def read_env(path) -> dict:
    """`key=value` lines (quotes stripped, keys lower-cased); {} when there is no file."""
    path = Path(path)
    if not path.exists():
        return {}
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
    return values


class Redactor:
    """Every secret, and the forms it can take in a line (JSON-escaped, URL-encoded),
    replaced by <name>; a secret cut off at the end of a line (a board line truncated at
    its cap) is masked too. Applied to everything printed or logged."""

    def __init__(self, secrets: dict):
        pairs = []
        for name, value in secrets.items():
            if not value:
                continue
            for form in {value, json.dumps(value)[1:-1], urllib.parse.quote(value, safe=""),
                         urllib.parse.quote_plus(value)}:
                pairs.append((form, "<%s>" % name))
        self.pairs = sorted(pairs, key=lambda p: -len(p[0]))

    def __call__(self, text) -> str:
        s = str(text)
        for value, name in self.pairs:
            s = s.replace(value, name)
        for value, name in self.pairs:
            for n in range(len(value) - 1, 2, -1):
                if s.endswith(value[:n]):
                    s = s[:-n] + name + "~"
                    break
        return s


class Out:
    """The terminal and the log, both through the redactor."""

    def __init__(self, log_path, redact, stream=None):
        self.redact = redact
        self.f = open(log_path, "a", encoding="utf-8") if log_path else None
        self.stream = stream or sys.stdout
        self.t0 = time.monotonic()
        self.lock = threading.Lock()

    def log(self, tag, text):
        if not self.f:
            return
        with self.lock:
            self.f.write("%8.2f %s %s\n" % (time.monotonic() - self.t0, tag, self.redact(text)))
            self.f.flush()

    def say(self, text):
        text = self.redact(text)
        with self.lock:
            print(text, file=self.stream, flush=True)
        self.log("##", text)


# ---- parsing (pure; tests/test_acceptance.py) ---------------------------------------------

KV = re.compile(r'(\w+)=("[^"]*"|\[[^\]]*\]|\S+)')


def _value(v):
    try:
        return int(v)
    except ValueError:
        return v


def parse_kv(text) -> dict:
    return {k: _value(v) for k, v in KV.findall(text)}


def parse_state(plus) -> dict:
    """STATE's lines: the occupant's (phase=, gen=, stage=, action=, eggs=...), then
    `drives ...`, `body ...`, `dish ...` as sub-dicts."""
    out = {"drives": {}, "body": {}, "dish": {}}
    for line in plus:
        head = line.split(" ", 1)[0]
        if head in out:
            out[head] = parse_kv(line)
        else:
            out.update(parse_kv(line))
    return out


def parse_chem(plus) -> dict:
    out = {"chem": {}, "locus": {}}
    for line in plus:
        kv = parse_kv(line)
        if "chem" in kv:
            out["chem"][kv.get("name")] = kv.get("level", 0)
        elif "locus" in kv:
            out["locus"][kv.get("name")] = kv.get("level", 0)
    return out


def parse_lineage(plus) -> list:
    rows = []
    for line in plus:
        kind = line.split(" ", 1)[0]
        rows.append({"kind": kind, **parse_kv(line)})
    return rows


def parse_stats(plus) -> dict:
    """STATS: `life lineage= gen= seq=`, `life k=v...`, `parent gen=`, `parent k=v...`,
    `grandparent ...`, `analytics ...`: one dict per whose."""
    out = {}
    for line in plus:
        whose = line.split(" ", 1)[0]
        out.setdefault(whose, {}).update(parse_kv(line))
    return out


def decode_chunks(plus, ok_payload) -> bytes:
    """`<offset> <base64>` lines and `OK <len> <crc32>`: the bytes, checked."""
    parts = []
    for line in plus:
        off, _, b64 = line.partition(" ")
        parts.append((int(off), base64.b64decode(b64)))
    data = b"".join(p for _, p in sorted(parts))
    want = ok_payload.split()
    if len(want) >= 2:
        if int(want[0]) != len(data):
            raise Fail("chunks: %d bytes, the board said %s" % (len(data), want[0]))
        if "%08x" % (zlib.crc32(data) & 0xFFFFFFFF) != want[1].lower():
            raise Fail("chunks: crc32 %08x, the board said %s" % (zlib.crc32(data) & 0xFFFFFFFF, want[1]))
    return data


def parse_fps(line) -> dict:
    kv = parse_kv(line)
    for k in ("fps", "draw_ms", "push_ms"):
        if k in kv:
            kv[k] = float(kv[k])
    return kv


def parse_latency(lines) -> dict:
    """`[lat] knock n=3 words=3 silent=0 noack=0 seen=0.4/1.1/1.3 ack=-`: ctl -> {n:, stage: (p50, p95, max)}."""
    out = {}
    for line in lines:
        m = re.search(r"\[lat\] (\w+) n=(\d+)", line)
        if not m or m.group(1) in ("last", "cleared", "done"):
            continue
        row = {"n": int(m.group(2))}
        for stage, p50, p95, mx in re.findall(r"(\w+)=([\d.]+)/([\d.]+)/([\d.]+)", line):
            row[stage] = (float(p50), float(p95), float(mx))
        out[m.group(1)] = row
    return out


def parse_wifi_status(lines) -> dict:
    out = {}
    for line in lines:
        out.update(parse_kv(line.split("[wifi]", 1)[-1]))
    nets = out.get("networks")
    out["networks"] = re.findall(r'"([^"]*)"', nets) if isinstance(nets, str) else []
    if isinstance(out.get("next_check_in"), str) and out["next_check_in"].rstrip("s").isdigit():
        out["next_check_in"] = int(out["next_check_in"].rstrip("s"))   # "900s"
    return out


MANIFEST = re.compile(r"\[wifi\] manifest (\S+) (\d+\.\d+\.\d+) size=(\d+) fmt=(\d+) auto=(\d): (\w+) \(running (\S+) fmt=(\d+)\)")


def parse_manifest_line(line):
    m = MANIFEST.search(line)
    if not m:
        return None
    return {"board": m.group(1), "version": m.group(2), "size": int(m.group(3)), "verdict": m.group(6),
            "running": m.group(7)}


# What a forced check against the live manifest ended with (wifi_ota.h): a verdict that
# installs nothing, a refusal at BEGIN (the old key's signature), a download, or a failure.
LIVE_OUTCOMES = [r"\[wifi\] forced: (\w+), nothing to install",
                 r"\[wifi\] forced: FAILED (install refused: .*)",
                 r"\[wifi\] (downloading \d+ bytes.*)",
                 r"\[wifi\] forced: FAILED (.*)"]


def live_verdict(k, outcome, verdict) -> tuple:
    """(refused, why) for LIVE_OUTCOMES' index k."""
    if k == 0:
        running = verdict["running"] if verdict else "?"
        site = verdict["version"] if verdict else "?"
        return True, "%s: running %s, the site has %s (nothing installed)" % (outcome, running, site)
    if k == 1:
        return True, "refused at BEGIN: %s (the old key's image is never taken)" % outcome
    if k == 2:
        return False, "it took the live image: %s" % outcome
    return False, "the check failed: %s" % outcome


MAC_RE = re.compile(r"^[0-9a-f]{2}(:[0-9a-f]{2}){5}$")

# What `DEBUG pickup` must never be read as (stimuli.def names, as DEBUG trace prints them).
NOT_A_PICKUP = ("knock", "double_knock", "shake", "dropped", "flipped", "button", "button_hold", "button_double",
                "hang", "cradle", "lid_down")


def parse_stims(lines) -> list:
    """DEBUG trace's `[stim] <ms> <name>` lines: the names, in order."""
    return [m.group(1) for m in (re.search(r"\[stim\] \d+ (\w+)", l) for l in lines) if m]


def pickup_verdict(stims) -> tuple:
    """DEBUG pickup through the real detectors: picked_up fired, and nothing a pick-up is not."""
    wrong = sorted(set(stims) & set(NOT_A_PICKUP))
    ok = "picked_up" in stims and not wrong
    return ok, "stimuli %s%s" % (" ".join(stims) or "none", "; never a pick-up: %s" % wrong if wrong else "")


def state_board(st, want_mac) -> tuple:
    """STATE's species= and mac= (test-verbs): (ok, mac to use, note). The mac is the board's own;
    --mac is only a fallback for a firmware whose STATE has none."""
    mac, species = st.get("mac"), st.get("species")
    if not isinstance(mac, str) or not MAC_RE.match(mac):
        return False, want_mac, "STATE has no board mac (mac=%s, species=%s): this firmware lacks test-verbs" % (mac, species)
    ok = mac == want_mac.lower() and isinstance(species, str) and species != "none"
    return ok, mac, "STATE mac=%s species=%s (--mac %s)" % (mac, species, want_mac)


def version_tuple(v):
    return tuple(int(x) for x in re.findall(r"\d+", str(v))[:3])


def idle_names(path=ROOT / "lib/blorb/include/blorb/defs/idles.def") -> set:
    try:
        return set(re.findall(r"^BLORB_IDLE\(\s*\d+,\s*(\w+)", Path(path).read_text(), re.M))
    except OSError:
        return set()


def idle_verdict(actions, idles) -> tuple:
    distinct = sorted(set(actions))
    played = sorted(set(actions) & idles) if idles else []
    ok = len(distinct) >= 3 and (not idles or len(played) >= 1)
    return ok, "%d distinct actions %s; idles played %s" % (len(distinct), distinct, played or "none")


def night_verdict(dish) -> tuple:
    """#20: night only with evidence: with no wall time known, never night."""
    night, known = dish.get("night"), dish.get("time")
    ok = not (night == 1 and known == "unknown")
    return ok, "night=%s time=%s dark=%s still=%s" % (night, known, dish.get("dark"), dish.get("still"))


def rot_verdict(samples) -> tuple:
    """samples: [(minute, hunger, pellets, toxin)] after the pellet rotted, one rotten pellet
    at the start. Refused: every rise of the clock that ended with it eaten began at a hunger
    of 0.6 or more (a 5-10 min step cannot carry him from under 0.6 past 0.8), and while it
    lay there the toxin stayed low. Returns (refused_ok, ate_when_starving, note)."""
    refused_ok, ate, prev_h = True, False, None
    notes = []
    for i, (minute, hunger, pellets, toxin) in enumerate(samples):
        if i and pellets == 0 and samples[i - 1][2] > 0:
            if prev_h is not None and prev_h < 600:
                refused_ok = False
                notes.append("eaten at %s min with hunger %s before" % (minute, prev_h))
            else:
                ate = True
                notes.append("eaten at %s min, hunger before %s, toxin now %s" % (minute, prev_h, toxin))
        if pellets > 0 and hunger < STARVING and toxin > 100:
            refused_ok = False
            notes.append("toxin %s at %s min while not starving" % (toxin, minute))
        prev_h = hunger
    if not notes:
        notes.append("never eaten (hunger reached %s)" % (samples[-1][1] if samples else "?"))
    return refused_ok, ate, "; ".join(notes)


def pick_target(gen, eggs) -> int:
    """The egg to aim at: never 0 when there is a choice (0 is where a timeout would land)."""
    choices = [i for i in (1, 2) if i < min(eggs, 3)]
    return choices[gen % len(choices)] if choices else 0


def stats_carried(snapshot_life, parent) -> list:
    """The parent's record in the child's STATS against the life seen just before he died:
    every counter there, none smaller, and a death recorded. Problems, or []."""
    problems = []
    if not parent:
        return ["no parent record"]
    if parent.get("death") in (None, "none"):
        problems.append("parent death=%s" % parent.get("death"))
    for k, v in snapshot_life.items():
        if k in ("lineage", "gen", "seq", "death") or not isinstance(v, int):
            continue
        if k not in parent:
            problems.append("%s missing" % k)
        elif isinstance(parent[k], int) and parent[k] < v:
            problems.append("%s %s < %s" % (k, parent[k], v))
    return problems


def pack_coverage(pack, situations) -> tuple:
    """(missing, fallback): situations with no generated line, and those the pack says
    fell back to the built-in lines."""
    lines = pack.get("lines") or {}
    fallback = list(pack.get("fallback") or [])
    missing = [s for s in situations if not lines.get(s)]
    return missing, fallback


def situations(path=ROOT / "web/src/catalog.json") -> list:
    """The situations every pack must cover (web/src/catalog.json; from origin/main when this
    checkout has none yet: the deployed site is built from main)."""
    try:
        text = Path(path).read_text()
    except OSError:
        import subprocess
        r = subprocess.run(["git", "-C", str(ROOT), "show", "origin/main:web/src/catalog.json"], capture_output=True,
                           text=True)
        text = r.stdout if r.returncode == 0 else ""
    try:
        return [s["name"] for s in json.loads(text)["situations"]]
    except (ValueError, KeyError, TypeError):
        return []


def perf_close(before, after) -> tuple:
    """fps within 15% and the worst frame no more than twice (+20 ms) the baseline."""
    fb, fa = before.get("fps", 0.0), after.get("fps", 0.0)
    wb, wa = before.get("worst_ms", 0), after.get("worst_ms", 0)
    ok = fa >= 0.85 * fb and wa <= max(2 * wb, wb + 20)
    return ok, "fps %.1f -> %.1f, worst_ms %s -> %s" % (fb, fa, wb, wa)


def latency_close(before, after, ctl="knock", stage="ack") -> tuple:
    b, a = before.get(ctl, {}).get(stage), after.get(ctl, {}).get(stage)
    if not b or not a:
        return False, "%s %s: before %s after %s" % (ctl, stage, b, a)
    ok = a[1] <= 1.5 * b[1] + 20
    return ok, "%s %s p50/p95 %.1f/%.1f -> %.1f/%.1f ms" % (ctl, stage, b[0], b[1], a[0], a[1])


# ---- the board ---------------------------------------------------------------------------

def find_port_by_serial(serial_no):
    from serial.tools import list_ports
    for p in list_ports.comports():
        if (p.serial_number or "") == serial_no:
            return p.device
    return None


def serial_of(device):
    from serial.tools import list_ports
    real = os.path.realpath(device)
    for p in list_ports.comports():
        if os.path.realpath(p.device) == real:
            return p.serial_number or ""
    return None


def open_serial(device):
    import serial
    p = serial.Serial()
    p.port, p.baudrate, p.timeout = device, 115200, 0.1
    p.dtr = p.rts = False   # no EN/IO0 toggle on open
    p.open()
    return p


class Reply:
    def __init__(self, plus, ok, err):
        self.plus, self.ok, self.err = plus, ok, err


class Board:
    def __init__(self, out, serial_no, device, opener=open_serial, finder=find_port_by_serial):
        self.out, self.serial_no, self.device = out, serial_no, device
        self.opener, self.finder = opener, finder
        self.p = opener(device)
        self.t0 = time.monotonic()
        self.lines = []
        self.cv = threading.Condition()
        self.boots = 0
        self.crashes = []
        self.alive = True
        self.seq = 0
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def close(self):
        self.alive = False
        self.thread.join(timeout=1)
        try:
            self.p.close()
        except Exception:
            pass

    def _read(self):
        buf = b""
        while self.alive:
            try:
                data = self.p.read(4096)
            except Exception as e:   # serial.SerialException, OSError: it left the bus
                self.out.log("##", "serial lost (%s): finding %s again" % (type(e).__name__, self.serial_no))
                self._reopen()
                continue
            if not data:
                continue
            buf += data
            *whole, buf = buf.split(b"\n")
            for raw in whole:
                self._line(raw.decode("utf-8", "replace").rstrip("\r"))

    def _reopen(self):
        try:
            self.p.close()
        except Exception:
            pass
        while self.alive:
            dev = self.finder(self.serial_no)
            if dev:
                try:
                    self.p = self.opener(dev)
                    self.device = dev
                    self.out.log("##", "reopened %s (USB serial %s)" % (dev, self.serial_no))
                    return
                except Exception:
                    pass
            time.sleep(0.5)

    def _line(self, line):
        if BANNER.search(line):
            self.boots += 1
        if CRASH.search(line):
            self.crashes.append(line)
        self.out.log("  ", line)
        with self.cv:
            self.lines.append((time.monotonic(), line))
            self.cv.notify_all()

    def mark(self):
        with self.cv:
            return len(self.lines)

    def since(self, m):
        with self.cv:
            return [l for _, l in self.lines[m:]]

    def send(self, line, shown=None):
        self.out.log(">>", line if shown is None else shown)
        for _ in range(20):
            try:
                # A light-sleeping badge loses a line's first bytes and stays awake 3 s:
                # '\r's first (the link ignores them), then the line.
                self.p.write(b"\r" * 8)
                time.sleep(0.06)
                self.p.write(line.encode() + b"\n")
                return
            except Exception:
                time.sleep(0.5)
        raise Fail("could not write to the board")

    def wait(self, patterns, timeout, since=None):
        """The first line from `since` matching any pattern: (index, match), or (None, None)."""
        if isinstance(patterns, str):
            patterns = [patterns]
        rx = [re.compile(p) for p in patterns]
        i = self.mark() if since is None else since
        end = time.monotonic() + timeout
        with self.cv:
            while True:
                while i < len(self.lines):
                    line = self.lines[i][1]
                    i += 1
                    for k, r in enumerate(rx):
                        m = r.search(line)
                        if m:
                            return k, m
                left = end - time.monotonic()
                if left <= 0:
                    return None, None
                self.cv.wait(left)

    def request(self, verb, timeout=10.0) -> Reply:
        """A protocol line (STATE, LINEAGE, ...) with its own id; its `+` lines and OK/ERR."""
        self.seq += 1
        rid = "%x" % (0x1000 + self.seq % 0xE000)
        m = self.mark()
        self.send("#%s %s" % (rid, verb))
        k, r = self.wait([r"^#%s OK ?(.*)$" % rid, r"^#%s ERR (.*)$" % rid], timeout, since=m)
        if r is None:
            raise Fail("no reply to %s in %.0f s" % (verb, timeout))
        prefix = "#%s + " % rid
        plus = [l[len(prefix):] for l in self.since(m) if l.startswith(prefix)]
        return Reply(plus, r.group(1) if k == 0 else None, r.group(1) if k == 1 else None)


# ---- the report --------------------------------------------------------------------------

class Report:
    def __init__(self):
        self.steps = []

    def add(self, phase, name, status, secs, note, evidence):
        self.steps.append({"phase": phase, "name": name, "status": status, "secs": secs, "note": note,
                           "evidence": list(evidence)})

    def failed(self):
        return [s for s in self.steps if s["status"] == "FAIL"]

    def table(self, evidence_lines=4) -> str:
        w = max([len("%s/%s" % (s["phase"], s["name"])) for s in self.steps] + [10])
        rows = ["%-*s  %-4s  %7s  %s" % (w, "step", "", "secs", "note"), "-" * (w + 40)]
        for s in self.steps:
            rows.append("%-*s  %-4s  %7.1f  %s" % (w, "%s/%s" % (s["phase"], s["name"]), s["status"], s["secs"],
                                                  s["note"]))
            for e in s["evidence"][:evidence_lines]:
                rows.append("%-*s        %s" % (w, "", e[:200]))
        n = {k: sum(1 for s in self.steps if s["status"] == k) for k in ("PASS", "FAIL", "SKIP")}
        rows.append("-" * (w + 40))
        rows.append("PASS %d  FAIL %d  SKIP %d" % (n["PASS"], n["FAIL"], n["SKIP"]))
        return "\n".join(rows)


# ---- the run -----------------------------------------------------------------------------

def http_json(method, url, body=None, token=None, timeout=60):
    """(status, parsed body or text). The token goes only into the header, never a message."""
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("user-agent", "blorbarium-acceptance/1")
    if data is not None:
        req.add_header("content-type", "application/json")
    if token:
        req.add_header("authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            status, text = r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        status, text = e.code, e.read().decode("utf-8", "replace")
    try:
        return status, json.loads(text)
    except ValueError:
        return status, text


class Run:
    def __init__(self, board, out, args, env, http=http_json):
        self.b, self.out, self.args, self.env, self.http = board, out, args, env, http
        self.report = Report()
        self.gens = []           # one dict per lifecycle generation
        self.missing = set()     # verbs the firmware did not know
        self.home = None

    # -- recording --

    def record(self, phase, name, ok, note="", evidence=(), t0=None, status=None):
        st = status or ("PASS" if ok else "FAIL")
        secs = time.monotonic() - t0 if t0 else 0.0
        ev = [self.out.redact(e) for e in evidence]
        self.report.add(phase, name, st, secs, self.out.redact(note), ev)
        self.out.say("  %-4s %s/%s (%.1f s) %s" % (st, phase, name, secs, note))
        for e in ev[:6]:
            self.out.say("         | " + e[:220])
        return st == "PASS"

    def say(self, s):
        self.out.say(s)

    # -- the board's verbs --

    def debug(self, line, pattern, timeout=10.0, shown=None):
        """A DEBUG line and the board's answer; MissingVerb when the firmware does not know it."""
        m = self.b.mark()
        self.b.send(line, shown=shown)
        k, r = self.b.wait([pattern, UNKNOWN_VERB], timeout, since=m)
        if k == 1:
            self.missing.add("DEBUG " + r.group(1))
            raise MissingVerb("this firmware has no DEBUG %s" % r.group(1))
        if r is None:
            raise Fail("no %r after %r" % (pattern, line if shown is None else shown))
        return r

    def req(self, verb, timeout=10.0) -> Reply:
        r = self.b.request(verb, timeout)
        if r.err and "UNKNOWN_VERB" in r.err:
            self.missing.add(verb.split()[0])
            raise MissingVerb("this firmware has no %s" % verb.split()[0])
        return r

    def state(self):
        r = self.req("STATE")
        st = parse_state(r.plus)
        st["_lines"] = r.plus
        return st

    def chem(self):
        r = self.req("CHEM")
        return parse_chem(r.plus) if r.ok is not None else {"chem": {}, "locus": {}}

    def lineage(self):
        r = self.req("LINEAGE")
        return parse_lineage(r.plus), r.plus

    def stats(self):
        r = self.req("STATS")
        if r.err:
            raise Fail("STATS: ERR %s" % r.err)
        return parse_stats(r.plus), r.plus

    def stats_json(self):
        r = self.req("STATS json", timeout=20)
        if r.err:
            raise Fail("STATS json: ERR %s" % r.err)
        return json.loads(decode_chunks(r.plus, r.ok or ""))

    def genome(self) -> bytes:
        r = self.req("GENOME")
        if r.err:
            raise Fail("GENOME: ERR %s" % r.err)
        return decode_chunks(r.plus, r.ok or "")

    def hello(self):
        r = self.req("HELLO")
        return parse_kv(r.ok or "")

    def power(self):
        r = self.debug("DEBUG power", r"\[power\] (now|refused \w+) (power=.*)")
        return parse_kv(r.group(2)), r.group(0)

    def awake(self):
        self.debug("DEBUG power wake", r"\[power\] (now|refused)")
        self.debug("DEBUG power idle 3600", r"idle_ms=3600000")

    def warp(self, minutes, timeout=240):
        r = self.debug("DEBUG warp %d" % minutes, r"\[hw\] debug warp \d+ min: occupant=(\w+)", timeout)
        return r.group(1)

    def stage_to(self, stage):
        """DEBUG stage: grows him to it (or, at it, settles his drives: hunger 0, rested)."""
        r = self.debug("DEBUG stage %s" % STAGE_VERB.get(stage, stage),
                       r"\[hw\] debug stage \w+: (.*?) stage=(\S+) occupant=(\w+) gen=(\d+) genome=([0-9a-f]+)", 120)
        return {"why": r.group(1), "stage": r.group(2), "occupant": r.group(3), "gen": int(r.group(4)),
                "genome": r.group(5), "line": r.group(0)}

    def wifi_status(self):
        m = self.b.mark()
        self.b.send("DEBUG wifi")
        k, r = self.b.wait([r"\[wifi\] station=", UNKNOWN_VERB], 5, since=m)
        if k == 1:
            self.missing.add("DEBUG wifi")
            raise MissingVerb("this firmware has no DEBUG wifi")
        if r is None:
            raise Fail("no DEBUG wifi status")
        lines = [l for l in self.b.since(m) if "[wifi] window=" in l or "[wifi] station=" in l]
        return parse_wifi_status(lines), lines

    def chem_set(self, name, permille):
        """DEBUG chem <name> <permille> (test-verbs): one chemical or drive, as the debug link's
        CHEM sets it. The level he holds after; Fail with the board's reason when refused."""
        r = self.debug("DEBUG chem %s %d" % (name, permille),
                       r"\[hw\] debug chem (?:(\w+)=(-?\d+)$|\S+ \S*: (.*))")
        if r.group(3) is not None:
            raise Fail("DEBUG chem %s %d refused: %s" % (name, permille, r.group(3)))
        return int(r.group(2))

    def pickup(self, during=None):
        """DEBUG pickup (test-verbs) with DEBUG trace on: (the stimuli it fired, its line). It
        runs ~5.5 s (the board says how long); then up to 2.5 s for the put_down of a badge
        lying on the desk. `during(end)` is called while it runs (a state poll; `end`, on
        time.monotonic(), is when the pose is over)."""
        self.debug("DEBUG trace 12", r"\[hw\] debug trace 12")
        m = self.b.mark()
        try:
            r = self.debug("DEBUG pickup", r"\[hw\] debug pickup: .*over in (\d+) ms")
            end = time.monotonic() + int(r.group(1)) / 1000.0
            if during:
                during(end)
            self.b.wait(r"\[stim\] \d+ put_down", max(0.0, end - time.monotonic()) + 2.5, since=m)
        finally:
            self.debug("DEBUG trace 0", r"\[hw\] debug trace 0")
        return parse_stims(self.b.since(m)), r.group(0)

    def wifi_due(self):
        """DEBUG wifi due (test-verbs-wifi): the next automatic check due now, on the real clock
        that DEBUG warp does not move. (status, lines), or None on a firmware without it."""
        m = self.b.mark()
        self.b.send("DEBUG wifi due")
        k, r = self.b.wait([r"\[wifi\] due: .*", r"\[wifi\] want: wifi", UNKNOWN_VERB], 5, since=m)
        if k in (1, 2):
            self.missing.add("DEBUG wifi due")
            return None
        if r is None:
            raise Fail("no answer to DEBUG wifi due")
        self.b.wait(r"\[wifi\] station=", 5, since=m)
        lines = [l for l in self.b.since(m) if "[wifi] due:" in l or "[wifi] window=" in l or "[wifi] station=" in l]
        return parse_wifi_status(lines), lines

    def air_fits(self, P, name):
        """DEBUG air open, then close (wifi-ota-2): the duel's radio opens through RadioGate as
        fight mode opens it, after a Wi-Fi window. A window still closing says wifi_up for a
        moment: a few tries, a second apart."""
        t0 = time.monotonic()
        ev, why, tries, r = [], None, 0, None
        try:
            for tries in range(1, 7):
                r = self.debug("DEBUG air open", r"\[hw\] debug air open: (\S+) heap=(\d+) minheap=(\d+) big=(\d+)")
                ev.append(r.group(0))
                why = r.group(1)
                if why == "ok" or why not in ("wifi_up", "ota_active"):
                    break
                time.sleep(1)
            c = self.debug("DEBUG air close", r"\[hw\] debug air close: open=(\d) heap=(\d+) minheap=(\d+) big=(\d+)")
            ev.append(c.group(0))
        except MissingVerb as e:
            return self.record(P, name, False, str(e), ev, t0)
        ok = why == "ok" and c.group(1) == "0"
        return self.record(P, name, ok, "open: %s after %d tr%s, heap %s big %s; closed: open=%s heap %s" % (
            why, tries, "y" if tries == 1 else "ies", r.group(2), r.group(4), c.group(1), c.group(2)), ev, t0)

    def fps_window(self, seconds=10):
        self.debug("DEBUG fps", r"\[fps\] fps=")   # starts a new window
        time.sleep(seconds)
        r = self.debug("DEBUG fps", r"\[fps\] fps=.*")
        return parse_fps(r.group(0)), r.group(0)

    def latency_probe(self):
        self.debug("DEBUG latency clear", r"\[lat\] cleared")
        for _ in range(4):
            self.debug("DEBUG knock 1", r"\[hw\] debug knock 1")
            time.sleep(1.8)
        for aim in (0, 1):
            self.debug("DEBUG lean %d 1500" % aim, r"\[hw\] debug lean")
            time.sleep(2.5)
        m = self.b.mark()
        self.b.send("DEBUG latency")
        self.b.wait(r"\[lat\] done", 5, since=m)
        lines = [l for l in self.b.since(m) if l.startswith("[lat]")]
        return parse_latency(lines), lines

    def wait_boot(self, timeout=60, since=None):
        k, r = self.b.wait(BOOT.pattern, timeout, since=since)
        return r

    # -- preflight --

    def preflight(self):
        t0 = time.monotonic()
        self.say("waiting up to 12 s for a boot banner (an open may reset the board)")
        _, banner = self.b.wait(BANNER.pattern, 12, since=0)
        boot = self.wait_boot(3, since=0) if banner else None
        hello = self.hello()
        ev = [x.group(0) for x in (banner, boot) if x] + ["HELLO " + " ".join("%s=%s" % kv for kv in hello.items())]
        try:
            self.home = self.debug("DEBUG home", r"\[hw\] home rot=(\d+).*").group(0)
            ev.append(self.home)
        except MissingVerb:
            ev.append("no DEBUG home on this firmware")
        self.awake()
        self.record("pre", "board answers", bool(hello), "fw=%s phase=%s gen=%s" % (hello.get("fw"), hello.get("phase"),
                                                                                     hello.get("gen")), ev, t0)

    # -- wifi --

    def phase_wifi(self):
        P = "wifi"
        ssid, password = self.env.get("ssid", ""), self.env.get("password", "")
        if not ssid:
            return self.record(P, "provision", False, "no ssid= in %s" % self.args.env, status="FAIL")
        line = 'DEBUG wifi set "%s" %s' % (ssid, password)
        if '"' in ssid or "\n" in ssid + password or len(line) > 6 + 127 or len(ssid.encode()) > 32:
            return self.record(P, "provision", False, "the SSID/password cannot ride DEBUG wifi set (lengths or a quote; not shown)")
        t0 = time.monotonic()
        m = self.b.mark()
        self.b.send(line, shown='DEBUG wifi set "<ssid>" <password>')
        k, r = self.b.wait([r'\[wifi\] network "(.*)" \((with a|no) password, (saved|SAVE FAILED)', UNKNOWN_VERB], 5, since=m)
        if k == 1:
            self.missing.add("DEBUG wifi")
            return self.record(P, "provision", False, "this firmware has no DEBUG wifi (wifi-ota-2 not merged)", t0=t0)
        saved = bool(r) and r.group(1) == ssid and r.group(3) == "saved"
        st, lines = self.wifi_status()
        listed = ssid in st["networks"]
        self.record(P, "provision", saved and listed, "saved=%s, DEBUG wifi lists it=%s (%d networks)" % (
            saved, listed, len(st["networks"])), ([r.group(0)] if r else []) + lines, t0)
        if not (saved and listed):
            return
        self._ota_live()
        self.air_fits(P, "fight radio fits after a window")
        if self.args.test_manifest:
            self._ota_test(self.args.test_manifest)
        else:
            self.record(P, "test manifest install", False, "no --test-manifest given", status="SKIP")

    def _forced_check(self, url, timeout):
        """DEBUG ota check [url] and the window's lines up to its verdict: (stage times, lines, verdict dict)."""
        t0 = time.monotonic()
        m = self.b.mark()
        self.debug("DEBUG ota check" + (" " + url if url else ""), r"\[wifi\] ota check asked", 5)
        stages = {}
        pats = [("opens", r"\[wifi\] window forced opens"), ("joined", r"\[wifi\] joined "),
                ("manifest", MANIFEST.pattern)]
        fail = r"\[wifi\] (forced: FAILED .*|ota check: .*)"
        verdict = None
        for name, pat in pats:
            k, r = self.b.wait([pat, fail], timeout, since=m)
            if r is None:
                stages[name] = None
                break
            if k == 1:
                stages["failed"] = r.group(1)
                break
            stages[name] = time.monotonic() - t0
            if name == "manifest":
                verdict = parse_manifest_line(r.group(0))
        return stages, m, verdict

    def _ota_live(self):
        P = "wifi"
        t0 = time.monotonic()
        live = None
        try:
            status, live = self.http("GET", LIVE_MANIFEST, timeout=15)
            live = live if status == 200 and isinstance(live, dict) else None
        except Exception as e:
            self.say("  (could not read the live manifest from here: %s)" % e)
        stages, m, verdict = self._forced_check("", 60)
        k, r = self.b.wait(LIVE_OUTCOMES, 30, since=m)
        self.b.wait(r"\[wifi\] window forced closed after (\d+) ms", 70, since=m)
        lines = [l for l in self.b.since(m) if l.startswith("[wifi]")]
        refused, why = live_verdict(k, r.group(1) if r else stages.get("failed", "no outcome"), verdict)
        times = ", ".join("%s %.1f s" % (k2, v) for k2, v in stages.items() if isinstance(v, float))
        self.record(P, "live manifest refused", refused, "%s; live=%s; %s" % (
            why, live.get("version") if live else "?", times), lines[-12:], t0)

    def _ota_test(self, url):
        P = "wifi"
        before = {}
        try:
            before["genome"] = hashlib.sha256(self.genome()).hexdigest()
            before["hello"] = self.hello()
        except Fail as e:
            self.say("  (before: %s)" % e)
        want = None
        try:
            status, man = self.http("GET", url, timeout=15)
            want = man.get("version") if status == 200 and isinstance(man, dict) else None
        except Exception as e:
            self.say("  (could not read the test manifest from here: %s)" % e)
        t0 = time.monotonic()
        stages, m, verdict = self._forced_check(url, 60)
        lines = lambda: [l for l in self.b.since(m) if l.startswith("[wifi]") or l.startswith("[hw]") or
                         l.startswith("[power]")]
        times = ", ".join("%s %.1f s" % (k, v) for k, v in stages.items() if isinstance(v, float))
        ok = bool(verdict) and verdict["verdict"] == "install"
        self.record(P, "test manifest offered", ok, "%s; %s" % (
            "verdict %s %s (running %s)" % (verdict["verdict"], verdict["version"], verdict["running"]) if verdict
            else stages.get("failed", "no manifest line"), times), lines()[-10:], t0)
        if not ok:
            return
        t1 = time.monotonic()
        k, r = self.b.wait([r"\[wifi\] forced: downloaded and verified in (\d+) s",
                            r"\[wifi\] forced: (FAILED .*|install refused: .*)"], self.args.download_timeout, since=m)
        ok = k == 0
        self.record(P, "download and verify", ok, ("verified, %s s on the board" % r.group(1)) if ok else
                    (r.group(1) if r else "no verdict in %d s" % self.args.download_timeout),
                    [l for l in lines() if "downloading" in l or "image at" in l or "verified" in l or "FAILED" in l][-6:], t1)
        if not ok:
            return
        # The restart waits for dark and alone: darken it now.
        t2 = time.monotonic()
        mr = self.b.mark()
        pw = self.debug("DEBUG power low", r"\[power\] (now|refused \w+) (power=\S+)", 5)
        k, r = self.b.wait([r"\[hw\] ota: restarting into the update", r"\[hw\] ota: save FAILED"], 60, since=mr)
        self.record(P, "restart while dark", k == 0, "%s; %s" % (pw.group(0)[:80], r.group(0) if r else "no restart in 60 s"),
                    [l for l in self.b.since(mr) if "[power]" in l or "ota: " in l][:6], t2)
        if k != 0:
            return
        t3 = time.monotonic()
        _, banner = self.b.wait(BANNER.pattern, 90, since=mr)
        _, trial = self.b.wait(r"\[hw\] ota trial: on trial.*", 30, since=mr)
        _, dark = self.b.wait(r"\[wifi\] on trial after an update over Wi-Fi.*", 30, since=mr)
        new_fw = banner.group(1) if banner else None
        ok = bool(banner) and (want is None or new_fw == want) and bool(trial)
        self.record(P, "new version boots on trial", ok, "fw=%s (manifest %s); trial=%s; dark=%s" % (
            new_fw, want, bool(trial), bool(dark)), [x.group(0) for x in (banner, trial, dark) if x], t3)
        t4 = time.monotonic()
        k, conf = self.b.wait([r"\[wifi\] confirm: trial confirmed over Wi-Fi \((\w[^;]*);.*",
                               r"\[hw\] ota trial: FAILED.*"], 330, since=mr)
        _, kept = self.b.wait([r"\[hw\] ota trial: kept", r"\[hw\] ota trial: FAILED.*"], 90, since=mr)
        ok = k == 0 and conf.group(1) == "ok" and bool(kept) and "kept" in kept.group(0)
        try:
            pw2 = self.power()[1]
        except Fail:
            pw2 = "no DEBUG power"
        self.record(P, "trial confirmed", ok, "%s; %s; still dark: %s" % (
            conf.group(0) if conf else "no confirm in 330 s", kept.group(0) if kept else "no kept", "power=low" in pw2),
                    [x.group(0) for x in (conf, kept) if x] + [pw2], t4)
        t5 = time.monotonic()
        after = {}
        try:
            after["genome"] = hashlib.sha256(self.genome()).hexdigest()
            after["hello"] = self.hello()
        except Fail as e:
            self.say("  (after: %s)" % e)
        same = bool(before.get("genome")) and before.get("genome") == after.get("genome") and \
            before.get("hello", {}).get("gen") == after.get("hello", {}).get("gen")
        self.record(P, "same pet", same, "genome sha256 %s -> %s, gen %s -> %s, fw %s -> %s" % (
            str(before.get("genome"))[:12], str(after.get("genome"))[:12], before.get("hello", {}).get("gen"),
            after.get("hello", {}).get("gen"), before.get("hello", {}).get("fw"), after.get("hello", {}).get("fw")),
                    t0=t5)
        try:
            home = self.debug("DEBUG home", r"\[hw\] home rot=(\d+).*").group(0)
            rot = lambda s: re.search(r"rot=(\d+)", s or "")
            same_home = bool(self.home) and rot(home) and rot(self.home) and rot(home).group(1) == rot(self.home).group(1)
            self.record(P, "same home", bool(same_home), "before: %s | after: %s" % (self.home, home), t0=t5)
        except MissingVerb:
            self.record(P, "same home", False, "no DEBUG home on the new firmware")
        self.awake()

    # -- offline --

    def phase_offline(self):
        P = "offline"
        t0 = time.monotonic()
        self.awake()
        base_fps, fps_line = self.fps_window(10)
        base_lat, lat_lines = self.latency_probe()
        self.record(P, "baseline", bool(base_fps.get("fps")), "fps %.1f worst_ms %s" % (
            base_fps.get("fps", 0), base_fps.get("worst_ms")), [fps_line] + lat_lines, t0)
        t1 = time.monotonic()
        st0, _ = self.wifi_status()
        self.debug("DEBUG wifi forget", r"\[wifi\] forgot every network")
        self.debug("DEBUG wifi set %s x" % FAKE_SSID, r'\[wifi\] network "%s"' % FAKE_SSID)
        st, lines = self.wifi_status()
        ok = st["networks"] == [FAKE_SSID]
        self.record(P, "only a network not in range", ok, "networks %d -> %s" % (len(st0["networks"]), st["networks"]),
                    lines, t1)
        if not ok:
            return self._restore_network()
        if st.get("auto") != 1:
            self.debug("DEBUG wifi on", r"\[wifi\] automatic checks on")
        if st.get("next_check_in", 0) != 0:
            # The schedule keeps real time (DEBUG warp does not move it): DEBUG wifi due makes the
            # next check due now; a firmware without it reboots, which makes one due at boot.
            if self.wifi_due() is None:
                mr = self.b.mark()
                self.b.send("DEBUG reboot")
                self.wait_boot(60, since=mr)
                self.debug("DEBUG wifi on", r"\[wifi\] automatic checks on")
        scans0 = self.wifi_status()[0].get("scans", 0)
        windows = []
        nudged = False   # the check about to open was made due by DEBUG wifi due
        checks = max(1, self.args.offline_checks)
        for n in range(checks):
            tw = time.monotonic()
            m = self.b.mark()
            if n == 0:
                pw = self.debug("DEBUG power low", r"\[power\] (now|refused \w+) power=(\w+)")
                if pw.group(2) != "low":
                    self.record(P, "dark", False, pw.group(0))
                    break
            due = 150 if n == 0 or nudged else 60 + (900 << (n - 1))
            k, opened = self.b.wait(r"\[wifi\] window check opens.*", due, since=m)
            k2, closed = self.b.wait(r"\[wifi\] window check closed after (\d+) ms", 30, since=m)
            wl = [l for l in self.b.since(m) if l.startswith("[wifi]")]
            scans = sum(1 for l in wl if "-> scanning" in l)
            absent = any("none of 1 remembered networks in range" in l for l in wl)
            ms = int(closed.group(1)) if closed else None
            ok = bool(opened) and bool(closed) and scans == 1 and absent and ms is not None and ms <= 8000
            power, pline = self.power()
            st, slines = self.wifi_status()
            want_next = 900 << n
            backoff = st.get("absent_streak") == n + 1 and want_next - 120 <= st.get("next_check_in", -1) <= want_next
            windows.append((ms, st.get("next_check_in"), st.get("absent_streak")))
            self.record(P, "check %d: one short scan" % (n + 1), ok, "%s scan(s), window %s ms, absent=%s" % (
                scans, ms, absent), wl[-8:], tw)
            self.record(P, "check %d: backoff" % (n + 1), backoff, "next_check_in=%ss absent_streak=%s (want ~%ss, %s)" % (
                st.get("next_check_in"), st.get("absent_streak"), want_next, n + 1), slines, tw)
            self.record(P, "check %d: dark at 80 MHz" % (n + 1), power.get("power") == "low" and power.get("cpu") == 80,
                        "power=%s cpu=%s sleep=%s" % (power.get("power"), power.get("cpu"), power.get("sleep")), [pline], tw)
            if not opened:
                break
            nudged = False
            if n + 1 < checks and self.args.offline_due:
                # The backoff is proven; the next check is made due now rather than waited
                # for (15 min, then 30...). The streak must stand, so the wait after it grows.
                td = time.monotonic()
                got = self.wifi_due()
                if got:
                    nudged = True
                    dst, dlines = got
                    self.record(P, "check %d: DEBUG wifi due" % (n + 2), dst.get("next_check_in") == 0 and
                                dst.get("absent_streak") == n + 1, "next_check_in=%ss absent_streak=%s (the streak stands)" % (
                                    dst.get("next_check_in"), dst.get("absent_streak")), dlines, td)
        scans1 = self.wifi_status()[0].get("scans", 0)
        self.record(P, "no other scans", scans1 - scans0 == len(windows), "scans %s -> %s over %d window(s)" % (
            scans0, scans1, len(windows)))
        t2 = time.monotonic()
        self.awake()
        self.air_fits(P, "fight radio fits after the windows")
        aft_fps, fps2 = self.fps_window(10)
        aft_lat, lat2 = self.latency_probe()
        ok, note = perf_close(base_fps, aft_fps)
        self.record(P, "frames unchanged", ok, note, [fps_line, fps2], t2)
        ok_k, note_k = latency_close(base_lat, aft_lat, "knock", "ack")
        ok_t, note_t = latency_close(base_lat, aft_lat, "tilt", "ack")
        self.record(P, "latency unchanged", ok_k and (ok_t or "tilt" not in base_lat), "%s; %s" % (note_k, note_t), lat2, t2)
        self._restore_network()

    def _restore_network(self):
        P = "offline"
        t0 = time.monotonic()
        try:
            self.debug('DEBUG wifi forget %s' % FAKE_SSID, r"\[wifi\] (forgot|forget: no network)")
        except Fail:
            pass
        ssid, password = self.env.get("ssid", ""), self.env.get("password", "")
        if not ssid:
            return self.record(P, "network given back", False, "no ssid in .env: the badge has none now", status="SKIP")
        self.debug('DEBUG wifi set "%s" %s' % (ssid, password), r'\[wifi\] network ".*" \(', 5,
                   shown='DEBUG wifi set "<ssid>" <password>')
        st, lines = self.wifi_status()
        self.record(P, "network given back", st["networks"] == [ssid], "%d network(s)" % len(st["networks"]), lines, t0)
        self.awake()

    # -- lifecycle --

    def creature(self):
        """A creature on the dish: an egg hatched, a clutch left by an earlier run picked."""
        st = self.state()
        if st.get("phase") == "clutch":
            if st.get("vigil", 0) < VIGIL_TICKS:
                self.warp(31)
            self._pick("pre", st.get("gen", 0), self.state())
            st = self.state()
        if st.get("phase") == "egg":
            r = self.debug("DEBUG hatch", r"\[hw\] debug hatch: (.*?) ?occupant=(\w+) genome=([0-9a-f]+)", 120)
            self.say("  hatched: " + r.group(0))
            st = self.state()
        if st.get("phase") != "creature":
            raise Fail("no creature to raise: %s" % " | ".join(st["_lines"]))
        return st

    def phase_lifecycle(self):
        P = "life"
        self.awake()
        self.debug("DEBUG tidy", r"\[hw\] debug tidy")
        self.creature()
        idles = idle_names()
        for g in range(self.args.generations):
            try:
                self._generation(P, g, idles)
            except Fail as e:
                self.record(P, "generation %d" % (g + 1), False, "aborted: %s" % e)
                try:
                    self.creature()
                except Fail:
                    return

    def _generation(self, P, g, idles):
        tg = time.monotonic()
        st = self.state()
        gen = st.get("gen")
        info = {"gen": gen}
        try:
            sj = self.stats_json()
            info["species"], info["genome_fnv"] = sj.get("species"), sj.get("genome_fnv")
        except Fail:
            pass
        if st.get("species") not in (None, "none"):
            info["species"] = st["species"]   # STATE's own (test-verbs)
        info["genome_sha"] = hashlib.sha256(self.genome()).hexdigest()
        self.say("generation %d: gen=%s species=%s genome=%s" % (g + 1, gen, info.get("species"), info["genome_sha"][:12]))
        Pg = "%s g%s" % (P, gen)

        # grow
        t0 = time.monotonic()
        ev, ok = [], True
        for stage in ("child", "adult"):
            if STAGE_ORDER.index(self.state().get("stage", "baby")) > STAGE_ORDER.index(stage):
                ev.append("already past %s" % stage)
                continue
            s = self.stage_to(stage)
            ev.append(s["line"])
            ok = ok and s["why"] == "ok" and s["stage"] == stage
            info.setdefault("genome", s["genome"])
        self.record(Pg, "grow to adult", ok, "stage=%s" % self.state().get("stage"), ev, t0)

        # idle variety, and night only with evidence
        t0 = time.monotonic()
        self.stage_to(self.state().get("stage", "adult"))   # settled: fed and rested
        self.debug("DEBUG sleepiness 0", r"debug sleepiness")
        actions, end = [], time.monotonic() + self.args.idle_seconds
        last = None
        while time.monotonic() < end:
            last = self.state()
            actions.append(last.get("action", "?"))
            time.sleep(2)
        ok, note = idle_verdict(actions, idles)
        self.record(Pg, "idle actions vary", ok, note, [" ".join(actions)], t0)
        ok, note = night_verdict(last["dish"] if last else {})
        self.record(Pg, "night only with evidence", ok, note, t0=t0)

        self._feed(Pg)
        self._rot(Pg)
        self._sleep_wake(Pg)

        # elder, then the life as it stands, then his death
        t0 = time.monotonic()
        s = self.stage_to("elder")
        self.record(Pg, "elder", s["why"] == "ok" and s["stage"] == "elder", s["why"], [s["line"]], t0)
        self.debug("DEBUG tidy", r"\[hw\] debug tidy")
        rows0, _ = self.lineage()
        snap = None
        try:
            snap = self.stats()[0].get("life")
        except Fail as e:
            self.say("  (STATS: %s)" % e)

        t0 = time.monotonic()
        try:
            r = self.debug("DEBUG die remains", r"\[hw\] debug die: (.*?) ?occupant=(\w+)", 60)
            died = r.group(2) == "clutch"
            ev = [r.group(0)]
        except MissingVerb:
            ev, died = ["no DEBUG die (egg-ux adds it): his life to nothing (DEBUG chem), then warping until he dies"], False
            try:
                ev.append("DEBUG chem life: %s" % self.chem_set("life", 0))
            except MissingVerb:
                pass
            for _ in range(self.args.die_fallback_warps):
                if self.warp(600, timeout=600) == "clutch":
                    died = True
                    break
        st = self.state()
        vigil = st.get("phase") == "clutch" and st.get("vigil", VIGIL_TICKS) < VIGIL_TICKS
        self.record(Pg, "death and vigil", died and st.get("phase") == "clutch", "phase=%s vigil=%s cause=%s (vigil open: %s)" % (
            st.get("phase"), st.get("vigil"), st.get("cause"), vigil), ev + st["_lines"][:1], t0)
        if st.get("phase") != "clutch":
            raise Fail("he did not die")
        info["cause"] = st.get("cause")

        t0 = time.monotonic()
        if st.get("vigil", 0) < VIGIL_TICKS:
            self.warp(31)
        st = self.state()
        eggs = st.get("eggs", 0) if st.get("phase") == "clutch" else 1
        clutch = self.req("CLUTCH")
        self.record(Pg, "clutch", st.get("phase") in ("clutch", "egg") and eggs >= 1, "phase=%s eggs=%s cursor=%s" % (
            st.get("phase"), eggs, st.get("cursor")), st["_lines"][:1] + clutch.plus[:4] + [clutch.ok or clutch.err or ""], t0)
        info["eggs"] = eggs

        chosen = self._pick(Pg, gen, st)
        info["chosen"] = chosen

        # the next generation hatches
        t0 = time.monotonic()
        r = self.debug("DEBUG hatch", r"\[hw\] debug hatch: (.*?) ?occupant=(\w+) genome=([0-9a-f]+)", 120)
        st = self.state()
        ok = r.group(2) == "creature" and st.get("phase") == "creature" and st.get("gen") == (gen or 0) + 1
        self.record(Pg, "next generation hatches", ok, "gen %s -> %s, genome %s" % (gen, st.get("gen"), r.group(3)),
                    [r.group(0)] + st["_lines"][:1], t0)
        child_genome = r.group(3)

        t0 = time.monotonic()
        rows1, lplus = self.lineage()
        new = rows1[len(rows0):] if rows1[:len(rows0)] == rows0 else rows1
        death = [x for x in new if x["kind"] == "death" and x.get("gen") == gen]
        birth = [x for x in new if x["kind"] == "birth" and x.get("gen") == (gen or 0) + 1]
        ok = bool(death) and bool(birth) and (chosen is None or birth[-1].get("chosen") == chosen)
        self.record(Pg, "LINEAGE rows", ok, "new rows: %s" % ", ".join(
            "%s gen=%s%s" % (x["kind"], x.get("gen"), " chosen=%s of=%s" % (x.get("chosen"), x.get("of")) if x["kind"] == "birth" else
                             " cause=%s" % x.get("cause") if x["kind"] == "death" else "") for x in new), lplus[-4:], t0)

        t0 = time.monotonic()
        try:
            stats, splus = self.stats()
            parent = stats.get("parent", {})
            problems = stats_carried(snap or {}, parent) if snap is not None else ["no STATS before he died"]
            ok = stats.get("life", {}).get("gen") == (gen or 0) + 1 and parent.get("gen") == gen and not problems
            self.record(Pg, "STATS carries the parent", ok, "life gen=%s, parent gen=%s death=%s; %s" % (
                stats.get("life", {}).get("gen"), parent.get("gen"), parent.get("death"), "; ".join(problems[:4]) or "every counter carried"),
                        splus[:6], t0)
        except MissingVerb as e:
            self.record(Pg, "STATS carries the parent", False, str(e), t0=t0)

        if self.args.reboot_check:
            t0 = time.monotonic()
            mr = self.b.mark()
            self.b.send("DEBUG reboot")
            boot = self.wait_boot(60, since=mr)
            ok = bool(boot) and boot.group(2) == "creature" and boot.group(4) == child_genome
            self.record(Pg, "the hatchling survives a reboot", ok, boot.group(0) if boot else "no boot line in 60 s", t0=t0)
            if boot:
                info["species"] = boot.group(6)
            self.awake()
        info["genome"] = child_genome
        info["secs"] = time.monotonic() - tg
        self.gens.append(info)
        self.record(Pg, "generation done", True, "species=%s chose egg %s of %s, child genome %s, %.0f s" % (
            info.get("species"), chosen, info.get("eggs"), child_genome, info["secs"]), status="PASS")

    def _feed(self, Pg):
        t0 = time.monotonic()
        self.debug("DEBUG tidy", r"\[hw\] debug tidy")
        st = self.state()
        if st["dish"].get("pantry", 0) < 2:
            self.warp(120)
        self.stage_to(st.get("stage", "adult"))
        hunger = 0
        try:
            hunger = self.chem_set("hunger", 600)   # hungry enough to eat at once (DEBUG chem)
        except MissingVerb:
            for _ in range(15):   # a firmware without DEBUG chem: let him grow hungry
                st = self.state()
                hunger = st["drives"].get("hunger", 0)
                if hunger >= 450:
                    break
                self.warp(20)
        self.debug("DEBUG sleepiness 0", r"debug sleepiness")
        if self.state().get("asleep") == 1:
            self.debug("DEBUG knock 1", r"debug knock 1")
            time.sleep(2)
        try:
            bites0 = self.stats()[0].get("life", {}).get("bites")
        except Fail:
            bites0 = None
        st0 = self.state()
        m = self.b.mark()
        self.debug("DEBUG knock 2", r"\[hw\] debug knock 2")
        _, g = self.b.wait(r"\[diag\] gesture knock_knock=press", 4, since=m)
        time.sleep(0.8)
        st1 = self.state()
        p0, p1 = st0["dish"].get("pantry"), st1["dish"].get("pantry")
        self.record(Pg, "knock knock drops food", bool(g) and p1 == (p0 or 0) - 1, "pantry %s -> %s, hunger %s" % (
            p0, p1, st0["drives"].get("hunger")), ([g.group(0)] if g else []) + st1["_lines"][-1:], t0)
        t1 = time.monotonic()
        end, eaten, st = time.monotonic() + 45, False, st1
        while time.monotonic() < end:
            st = self.state()
            if st["dish"].get("pellets", 1) == 0:
                eaten = True
                break
            time.sleep(2)
        try:
            bites1 = self.stats()[0].get("life", {}).get("bites")
        except Fail:
            bites1 = None
        self.record(Pg, "he eats it", eaten, "pellets=%s hunger %s -> %s, bites %s -> %s, action=%s" % (
            st["dish"].get("pellets"), st0["drives"].get("hunger"), st["drives"].get("hunger"), bites0, bites1,
            st.get("action")), st["_lines"][:1], t1)

    def _rot(self, Pg):
        """#21: a pellet left 90 min rots; he refuses it until starving."""
        t0 = time.monotonic()
        self.debug("DEBUG tidy", r"\[hw\] debug tidy")
        stage = self.state().get("stage", "adult")
        self.stage_to(stage)   # full: he leaves the pellet
        if self.state()["dish"].get("pantry", 0) < 1:
            self.warp(120)
            self.stage_to(stage)
        self.debug("DEBUG knock 2", r"\[hw\] debug knock 2")
        time.sleep(1.5)
        for minutes in (16,) * 6:   # 96 min, each time fed back to full so he leaves it
            self.warp(minutes)
            self.stage_to(stage)
        st, ch = self.state(), self.chem()
        rotten = st["dish"].get("pellets") == 1 and ch["locus"].get("food_rotten", 0) >= 500
        self.record(Pg, "a pellet rots", rotten, "pellets=%s food_rotten=%s after 96 min" % (
            st["dish"].get("pellets"), ch["locus"].get("food_rotten")), st["_lines"][-1:], t0)
        if not rotten:
            return self.debug("DEBUG tidy", r"\[hw\] debug tidy")
        t1 = time.monotonic()
        samples, actions, minute = [(0, st["drives"].get("hunger", 0), 1, ch["chem"].get("toxin", 0))], set(), 0
        jumped = False
        for _ in range(60):
            step = 5 if samples[-1][1] >= 600 else 10
            self.warp(step)
            minute += step
            st, ch = self.state(), self.chem()
            if st.get("phase") != "creature":
                self.record(Pg, "rot refused unless starving", False, "he died while the rot lay there: %s" % st["_lines"][:1],
                            ["min %s hunger %s pellets %s toxin %s" % x for x in samples[-6:]], t1)
                raise Fail("he died during the rot test")
            actions.add(st.get("action"))
            samples.append((minute, st["drives"].get("hunger", 0), st["dish"].get("pellets", 0), ch["chem"].get("toxin", 0)))
            if samples[-1][2] == 0 or samples[-1][1] >= 950:
                break
            if not jumped and samples[-1][1] >= 600 and samples[-2][1] >= 600:
                # Refused twice while hungry: DEBUG chem makes him starving now instead of
                # warping there 5 min at a time. A sample at the new level marks the jump.
                jumped = True
                try:
                    level = self.chem_set("hunger", STARVING + 50)
                    samples.append((minute, level, samples[-1][2], samples[-1][3]))
                except MissingVerb:
                    pass
        refused, ate, note = rot_verdict(samples)
        table = ["min %s hunger %s pellets %s toxin %s" % s for s in samples]
        self.record(Pg, "rot refused unless starving", refused, note, table[-8:], t1)
        self.record(Pg, "rot eaten when starving", ate, note + "; actions seen: %s" % sorted(a for a in actions if a), t0=t1)
        self.debug("DEBUG tidy", r"\[hw\] debug tidy")
        self.stage_to(stage)

    def _sleep_wake(self, Pg):
        t0 = time.monotonic()
        self.awake()
        for how in ("a knock", "a pick-up"):
            t = time.monotonic()
            self.debug("DEBUG nap", r"\[hw\] debug nap: forced sleep")
            self.debug("DEBUG sleepiness 100", r"debug sleepiness")   # so he does not wake by himself
            time.sleep(3)
            asleep = self.state().get("asleep") == 1   # and still asleep 3 s on: only the hand wakes him
            seen = {"woke": False, "st": None}

            def poll(end):
                while time.monotonic() < end and not seen["woke"]:
                    time.sleep(1)
                    seen["st"] = self.state()
                    seen["woke"] = seen["st"].get("asleep") == 0

            if how == "a knock":
                self.debug("DEBUG knock 1", r"debug knock 1")
                poll(time.monotonic() + 12)
            else:
                # DEBUG pickup: lying still, lifted, carried, set down, through the raw sample and
                # the real HeldDetector (~6 s); DEBUG trace shows what it fired.
                tp = time.monotonic()
                try:
                    stims, line = self.pickup(during=poll)
                    poll(time.monotonic() + 3)
                    ok, note = pickup_verdict(stims)
                    self.record(Pg, "DEBUG pickup fires picked_up", ok, note, [line], tp)
                except MissingVerb:
                    how = "a pick-up (DEBUG cradle: no DEBUG pickup here)"
                    self.debug("DEBUG cradle 6000", r"debug cradle")
                    poll(time.monotonic() + 12)
            st = seen["st"]
            self.record(Pg, "%s wakes him" % how, asleep and seen["woke"], "asleep after nap=%s, awake after=%s, action=%s" % (
                asleep, seen["woke"], st.get("action") if st else "?"), (st or {}).get("_lines", [])[:1], t)
            self.debug("DEBUG sleepiness 0", r"debug sleepiness")
            time.sleep(4)   # poses settle

    def _pick(self, Pg, gen, st):
        """The egg pick (#23): lying, nothing; a lean (a hand's move arms the aim) and a knock, the aimed one."""
        eggs = st.get("eggs", 1) if st.get("phase") == "clutch" else 1
        if st.get("phase") != "clutch":
            self.record(Pg, "egg pick", True, "one egg: no choice to make (phase=%s)" % st.get("phase"), status="SKIP")
            return None
        if eggs <= 1:
            self.record(Pg, "egg pick", True, "one egg: no choice to make", status="SKIP")
            return None
        t0 = time.monotonic()
        polls = []
        end = time.monotonic() + self.args.lying_seconds
        while time.monotonic() < end:
            time.sleep(min(10, max(0.5, end - time.monotonic())))
            polls.append(self.state().get("phase"))
        try:
            self.warp(15)   # still inside the clutch's 30 min timeout
            polls.append(self.state().get("phase"))
        except Fail:
            pass
        lying_ok = all(p == "clutch" for p in polls)
        self.record(Pg, "lying still picks nothing", lying_ok, "%d s real + 15 pet min: phases %s" % (
            self.args.lying_seconds, sorted(set(polls))), t0=t0)
        if not lying_ok:
            return None
        t1 = time.monotonic()
        target = pick_target(gen or 0, eggs)
        self.debug("DEBUG lean %d 5000" % target, r"\[hw\] debug lean")
        time.sleep(1.2)
        self.debug("DEBUG knock 1", r"\[hw\] debug knock 1")
        picked = None
        for _ in range(16):
            time.sleep(0.7)
            s = self.state()
            if s.get("phase") != "clutch":
                picked = s
                break
        rows, _ = self.lineage()
        births = [x for x in rows if x["kind"] == "birth"]
        chosen = births[-1].get("chosen") if picked and births else None
        self.record(Pg, "lean and knock picks the aimed egg", picked is not None and chosen == target,
                    "aimed at egg %d of %d (aim %d), kept egg %s, phase now %s" % (
                        target, eggs, target, chosen, picked.get("phase") if picked else "clutch"),
                    births[-1:] and ["birth " + " ".join("%s=%s" % kv for kv in births[-1].items() if kv[0] != "kind")], t1)
        time.sleep(6)   # the lean's tail and the rearm
        return chosen

    # -- server --

    def phase_server(self):
        P = "server"
        t0 = time.monotonic()
        genome = self.genome()
        ghash = hashlib.sha256(genome).hexdigest()
        hello = self.hello()
        st = self.state()
        doc = None
        try:
            doc = self.stats_json()
        except Fail as e:
            self.say("  (STATS json: %s)" % e)
        ok, mac, note = state_board(st, self.args.mac)
        self.record(P, "STATE names the board", ok, note, st["_lines"][:1], t0)
        body = {"mac": mac, "genome": genome.hex(), "stage": st.get("stage", "adult"),
                "generation": st.get("gen"), "name": hello.get("name", ""), "fw": hello.get("fw"), "wait": True}
        status, pack = self.http("POST", SITE + "/api/pet-lines", body, timeout=180)
        polls = 0
        while status == 202 and time.monotonic() - t0 < self.args.server_timeout:
            time.sleep(15)
            polls += 1
            status, pack = self.http("GET", SITE + "/api/pet-lines?mac=%s&genome_hash=%s" % (
                urllib.parse.quote(mac), ghash), timeout=60)
        names = situations(self.args.catalog)
        if status != 200 or not isinstance(pack, dict):
            self.record(P, "pet-lines pack", False, "HTTP %s after %d polls: %s" % (status, polls, str(pack)[:200]), t0=t0)
        else:
            missing, fallback = pack_coverage(pack, names)
            ok = bool(names) and not missing
            self.record(P, "pet-lines pack", ok, "version %s model %s, %d situations, %d with lines, missing %s, fallback %s%s" % (
                pack.get("version"), pack.get("model"), len(names), len(pack.get("lines") or {}), missing[:6] or "none",
                fallback[:6] or "none", "" if names else " (no catalog.json to check against)"),
                        [str(pack.get("bible", ""))[:160]], t0)
        token = self.env.get("admin_token", "")
        if not token:
            return self.record(P, "admin record", False, "no admin_token in %s" % self.args.env, status="SKIP")
        t1 = time.monotonic()
        hexmac = re.sub(r"[^0-9a-f]", "", mac.lower())
        status, badge = self.http("GET", SITE + "/api/admin/badges/" + hexmac, token=token, timeout=30)
        creature = None
        if status == 200 and isinstance(badge, dict):
            creature = next((c for c in badge.get("creatures", []) if c.get("genome_hash") == ghash), None)
        # The record is this pet as the badge says he is: STATE's species and gen.
        species = st.get("species") if st.get("species") not in (None, "none") else None
        same = bool(creature) and (species is None or creature.get("species") == species) and \
            creature.get("generation") == st.get("gen")
        self.record(P, "admin: creature record", same, "HTTP %s; %s" % (status, (
            "creature id=%s species=%s gen=%s stage=%s; STATE species=%s gen=%s" % (
                creature.get("id"), creature.get("species"), creature.get("generation"), creature.get("stage"), species,
                st.get("gen"))) if creature else "no creature with genome %s for %s" % (ghash[:12], hexmac)), t0=t1)
        if not creature:
            return
        t2 = time.monotonic()
        status, detail = self.http("GET", SITE + "/api/admin/creatures/%s" % creature["id"], token=token, timeout=30)
        stats = detail.get("stats", []) if isinstance(detail, dict) else []
        uploaded = ""
        if not stats and self.args.upload_stats and doc:
            parents = {k: doc[k] for k in ("parent", "grandparent") if k in doc}
            s2, r2 = self.http("POST", SITE + "/api/stats", {"mac": mac, "genome_hash": ghash, "stats_version": doc.get("v", 1),
                                                              "stats": doc.get("life"), "parents": parents or None,
                                                              "analytics": doc.get("analytics"), "fw": doc.get("fw")}, timeout=30)
            uploaded = "; uploaded the badge's STATS json here (HTTP %s %s)" % (s2, str(r2)[:80])
            status, detail = self.http("GET", SITE + "/api/admin/creatures/%s" % creature["id"], token=token, timeout=30)
            stats = detail.get("stats", []) if isinstance(detail, dict) else []
        self.record(P, "admin: stats", bool(stats), "HTTP %s; %d stats row(s), %d pack(s)%s" % (
            status, len(stats), len(detail.get("packs", [])) if isinstance(detail, dict) else 0, uploaded), t0=t2)
        status, page = self.http("GET", SITE + "/admin", timeout=30)
        self.record(P, "admin page", status == 200, "GET /admin HTTP %s" % status)

    # -- the whole run --

    def run(self, phases):
        try:
            self.preflight()
        except Fail as e:
            self.record("pre", "board answers", False, str(e))
            return
        for name in phases:
            self.say("== phase %s" % name)
            try:
                getattr(self, "phase_" + name)()
            except Fail as e:
                self.record(name, "phase", False, "aborted: %s" % e)
        try:
            self.debug("DEBUG power idle default", r"\[power\]")
        except Fail:
            pass

    def summary(self) -> str:
        rows = [self.report.table()]
        if self.gens:
            rows.append("\ngenerations:")
            for i, gi in enumerate(self.gens):
                rows.append("  %d. gen=%s species=%s genome(before)=%s cause=%s eggs=%s chosen=%s child=%s %.0f s" % (
                    i + 1, gi.get("gen"), gi.get("species"), gi.get("genome_sha", "")[:12], gi.get("cause"), gi.get("eggs"),
                    gi.get("chosen"), gi.get("genome"), gi.get("secs", 0)))
        if self.missing:
            rows.append("\nverbs this firmware did not know: %s" % ", ".join(sorted(self.missing)))
        if self.b.crashes:
            rows.append("\nCRASH LINES:\n  " + "\n  ".join(self.b.crashes[:10]))
        return "\n".join(rows)


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="the badge's port; found by --serial when omitted")
    ap.add_argument("--serial", required=True, help="the badge's USB serial (5B91046680): nothing runs on another")
    ap.add_argument("--phase", default="all", help="wifi, offline, lifecycle, server, or all (comma separated)")
    ap.add_argument("--generations", type=int, default=3)
    ap.add_argument("--log", help="the whole conversation, masked")
    ap.add_argument("--env", default=str(DEFAULT_ENV), help="ssid=, password=, admin_token= (never printed)")
    ap.add_argument("--test-manifest", help="a manifest URL with a newer signed image (wifi phase)")
    ap.add_argument("--download-timeout", type=int, default=600)
    ap.add_argument("--offline-checks", type=int, default=2,
                    help="dark checks to watch (each after the 1st made due by DEBUG wifi due; else 15 min, 30...)")
    ap.add_argument("--no-offline-due", dest="offline_due", action="store_false",
                    help="wait out the backoff between dark checks instead of DEBUG wifi due")
    ap.add_argument("--idle-seconds", type=int, default=60)
    ap.add_argument("--lying-seconds", type=int, default=60)
    ap.add_argument("--die-fallback-warps", type=int, default=8)
    ap.add_argument("--no-reboot-check", dest="reboot_check", action="store_false")
    ap.add_argument("--mac", default=DEFAULT_MAC, help="the MAC the server knows this badge by")
    ap.add_argument("--server-timeout", type=int, default=600)
    ap.add_argument("--no-upload-stats", dest="upload_stats", action="store_false")
    ap.add_argument("--catalog", default=str(ROOT / "web/src/catalog.json"))
    args = ap.parse_args(argv)
    order = ["wifi", "offline", "lifecycle", "server"]
    want = order if args.phase == "all" else [p.strip() for p in args.phase.split(",") if p.strip()]
    bad = [p for p in want if p not in order]
    if bad:
        ap.error("unknown phase %s" % bad)
    args.phases = [p for p in order if p in want]
    return args


def main(argv=None):
    args = parse_args(argv)
    env = read_env(args.env)
    redact = Redactor({k: env.get(k, "") for k in SECRET_KEYS})
    out = Out(args.log, redact)
    out.say("acceptance #55: phases %s on USB serial %s" % (",".join(args.phases), args.serial))
    device = args.port or find_port_by_serial(args.serial)
    if not device:
        out.say("REFUSING: no port has USB serial %s" % args.serial)
        return 2
    have = serial_of(device)
    if have != args.serial:
        out.say("REFUSING: %s has USB serial %r, not %s" % (device, have, args.serial))
        return 2
    board = Board(out, args.serial, device)
    run = Run(board, out, args, env)
    try:
        run.run(args.phases)
    except KeyboardInterrupt:
        out.say("interrupted")
    finally:
        out.say("\n" + run.summary())
        board.close()
    return 1 if run.report.failed() else 0


if __name__ == "__main__":
    sys.exit(main())
