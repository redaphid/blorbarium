# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial", "bleak>=0.22"]
# ///
"""0.7.35's hardware checks on one badge on the cable (moth), end to end.

    uv run scripts/test/moth_0735.py /dev/ttyACM0 --log .jobs/moth.log [--tip-wait 900]

(a) the boot says the Improv UUID is in the scan response, and this
    machine's scan hears it there;
(b) LOG and WIFI over Bluetooth, with no SSID or password anywhere;
(c) DEBUG wifi status over Bluetooth;
(d) DEBUG ota check over Bluetooth: Bluetooth pauses, the badge joins its
    network #1, Bluetooth comes back, and WIFI says how it went;
(e) the website's provision, as web/public/improv.js and index.html run it:
    HELLO, STATE's mac, the site's baseline, open Improv (the badge asks
    "Join Wi-Fi? Tip me on my side!"), a hand's yes, the network (this
    laptop's active one) sent, the drop, /api/badge/online until it checked
    in, then Bluetooth back and WIFI. Prints "NEED TIP" when it is asking;
    asks again every 30 s for --tip-wait seconds, then says yes with the
    cable's `DEBUG side` (and says the hand's tip was not seen).

Nothing secret is printed or logged: every SSID heard nearby, this laptop's
network and password and the repo's .env values are masked, and every text
captured is checked for them (only a count is printed).
The port is opened once and held: on Linux an open resets the board.
"""
import argparse
import asyncio
import json
import os
import re
import subprocess
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from power_e2e import NUS_RX, Board, Fail  # noqa: E402
from debug_ble_e2e import Run, Session  # noqa: E402

IMPROV = "00467768-6228-2272-4663-277478268000"
STATE_C = "00467768-6228-2272-4663-277478268001"
ERROR_C = "00467768-6228-2272-4663-277478268002"
RPC_C = "00467768-6228-2272-4663-277478268003"
RESULT_C = "00467768-6228-2272-4663-277478268004"
SITE = "https://blorb.hypnodroid.com"


def nm(*a):
    return subprocess.run(["nmcli", *a], capture_output=True, text=True, timeout=20, stdin=subprocess.DEVNULL).stdout


def laptop_network():
    active = nm("-t", "-f", "NAME,TYPE", "connection", "show", "--active")
    con = next((l.rsplit(":", 1)[0] for l in active.splitlines() if l.endswith(":802-11-wireless")), None)
    if not con:
        return None
    get = lambda f, *a: nm(*a, "-g", f, "connection", "show", con).strip()
    ssid = get("802-11-wireless.ssid")
    # A badge hears only 2.4 GHz: the flash station's twin when the laptop is on 5 GHz.
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    from flash_station import parse_scan, twin_24ghz
    twin = None
    for _ in range(4):   # the laptop's list can miss a 2.4 GHz network until a fresh scan
        twin = twin_24ghz(ssid, parse_scan(nm("-t", "-f", "ssid,freq,security", "dev", "wifi", "list")))
        if twin:
            break
        nm("dev", "wifi", "rescan")
        time.sleep(5)
    return (twin, get("802-11-wireless-security.psk", "-s")) if twin else None


def secrets(net):
    out = set()
    for line in nm("-t", "-f", "SSID", "dev", "wifi", "list").splitlines():
        s = line.replace("\\:", ":").strip()
        if len(s) >= 5:
            out.add(s)
    if net:
        out.update(x for x in net if x and len(x) >= 3)
    for env in [os.path.join(ROOT, ".env"), "/home/redaphid/Projects/blorbarium/.env"]:
        if os.path.exists(env):
            for line in open(env):
                if "=" in line and not line.lstrip().startswith("#"):
                    v = line.split("=", 1)[1].strip().strip("\"'")
                    if len(v) >= 4:
                        out.add(v)
    return sorted(out, key=len, reverse=True)


class Masked:
    """The log file, every secret blanked; counts what had to be."""

    def __init__(self, f, hidden):
        self.f, self.hidden, self.hits = f, hidden, []

    def mask(self, s):
        for h in self.hidden:
            s = s.replace(h, "<masked>")
        return s

    def write(self, s):
        m = self.mask(s)
        if m != s:
            self.hits.append(len(self.hits))
        self.f.write(m)

    def flush(self):
        self.f.flush()


def online(mac, since):
    req = urllib.request.Request("%s/api/badge/online?mac=%s&since=%d" % (SITE, mac, since),
                                 headers={"user-agent": "blorb", "cache-control": "no-store"})
    with urllib.request.urlopen(req, timeout=10) as r:
        d = json.loads(r.read())
        return d, r.headers.get("date")


def packet(cmd, data):
    out = bytes([cmd, len(data)]) + data
    return out + bytes([sum(out) & 0xff])


class Checks(Run):
    def __init__(self, board, args, masked, net):
        super().__init__(board, args)
        self.masked, self.net = masked, net
        self.items = {}

    def say(self, s):
        super().say(self.masked.mask(s))

    def item(self, key, ok, what):
        self.items[key] = ("PASS" if ok else "FAIL", what)
        self.say("[%s] %s: %s" % (key, "PASS" if ok else "FAIL", what))

    async def wifi_lines(self, s):
        got, plus = await s.dbg_all("WIFI", r" OK", timeout=8)
        return [l.split(" + ", 1)[-1] for l in plus]

    async def restore(self):
        """What the flash station gives a badge, by the cable: its home, the
        kids' network, then this laptop's (the newest, so #1). A full-flash
        image (merged.hex at 0x0) leaves NVS empty."""
        b = self.b
        b.wait(r"\[hw\] ble adv on", 10, since=0)
        b.wait(r"no such line", 3)   # let it finish booting
        mark = b.mark()
        b.send("DEBUG home %s" % self.args.home)
        h = await self.serial(r"\[hw\] home rot=.*", 6, mark)
        self.say("  restore: %s" % (h.group(0) if h else "home: no answer"))
        mark = b.mark()
        b.send("DEBUG wifi forget")
        f = await self.serial(r"\[wifi\] forgot .*", 6, mark)
        self.say("  restore: %s" % (f.group(0) if f else "forget: no answer"))
        env = {}
        for line in open("/home/redaphid/Projects/blorbarium/.env"):
            if "=" in line and not line.lstrip().startswith("#"):
                k, v = line.split("=", 1)
                env[k.strip()] = v.strip().strip("\"'")
        for i, (ssid, pw) in enumerate([(env["ssid"], env["password"]), self.net]):
            for verb, want in (("set", r"\[wifi\] network .*saved.*"), ("verify", r"\[wifi\] verify.*")):
                mark = b.mark()
                b.note('>> DEBUG wifi %s "<ssid%d>" <password%d>' % (verb, i, i))
                b.p.write(b"\r" * 8)
                time.sleep(0.06)
                b.p.write(('DEBUG wifi %s "%s" %s\n' % (verb, ssid, pw)).encode())
                r = await self.serial(want, 6, mark)
                self.say("  restore #%d %s: %s" % (i, verb, r.group(0) if r else "(no answer)"))
                if not r or (verb == "verify" and "password MATCHES" not in r.group(0)):
                    raise Fail("restore: wifi %s <ssid%d> did not take" % (verb, i))

    async def back(self, since, what, timeout=150):
        r = await self.serial(r"\[hw\] ble back after the Wi-Fi join", timeout, since)
        self.say("  %s: ble back %s" % (what, "seen" if r else "NOT seen"))
        await asyncio.sleep(2)
        return bool(r)

    async def main(self):
        b, a = self.b, self.args
        self.say("waiting for the boot (opening the port reset it)")
        r = b.wait(r"\[hw\] ble name=(\S+)", 20, since=0)
        if not r:
            raise Fail("no boot banner")
        name = r.group(1)
        if a.restore:
            await self.restore()
        self.say("== (a) the scan response")
        up = b.wait(r"\[wifi\] improv service up: .*", 20, since=0)
        self.say("  boot: %s" % (up.group(0) if up else "(no improv line)"))
        uuid_yes = bool(up and "uuid yes" in up.group(0))
        b.wait(r"\[hw\] ble adv on", 10, since=0)
        from bleak import BleakScanner
        heard = None
        for _ in range(3):
            try:
                found = await asyncio.wait_for(BleakScanner.discover(timeout=10, return_adv=True), 25)
            except asyncio.TimeoutError:
                self.say("  scan: hung; again")
                continue
            for dev, adv in found.values():
                if adv.local_name == name or dev.name == name:
                    heard = adv
            if heard:
                break
        in_scan = bool(heard and IMPROV in [u.lower() for u in heard.service_uuids])
        self.say("  scan: %s, uuids %s" % ("heard" if heard else "NOT heard", heard and heard.service_uuids))
        self.item("a", uuid_yes and in_scan, "boot says uuid %s; the laptop's scan %s the Improv UUID" % (
            "yes" if uuid_yes else "NOT yes", "hears" if in_scan else "does NOT hear"))

        self.say("== (b) LOG and WIFI over Bluetooth")
        s = Session(self, name)
        await s.connect()
        got, log = await s.dbg_all("LOG 40", r" OK lines=\d+", timeout=8)
        wifi = await self.wifi_lines(s)
        for l in wifi:
            self.say("  WIFI: " + l)
        leaks = len(self.masked.hits)
        self.item("b", len(log) > 0 and len(wifi) > 0 and leaks == 0,
                  "LOG %d lines, WIFI %d lines, secrets seen anywhere so far: %d" % (len(log), len(wifi), leaks))

        self.say("== (c) DEBUG wifi status over Bluetooth")
        got, plus = await s.dbg_all("DEBUG wifi status", r" OK reach=look lines=[1-9]", timeout=8)
        for l in plus:
            self.say("  status: " + l.split(" + ", 1)[-1])
        self.item("c", bool(re.search(r"OK reach=look lines=[1-9]", got)), got)

        self.say("== (d) DEBUG ota check over Bluetooth")
        mark = b.mark()
        got = await s.dbg("DEBUG ota check", r" OK reach=write", timeout=8)
        pause = await self.serial(r"\[hw\] ble pauses for a Wi-Fi join", 10, mark)
        joined = await self.serial(r"\[wifi\] .*joined in \d+ ms rssi=-?\d+ ch=\d+", 60, mark)
        picked = b.wait(r"joining network #(\d+)", 0.1, mark)
        await s.close()
        back = await self.back(mark, "ota check")
        closed = b.wait(r"window forced closed.*", 0.1, mark) or b.wait(r"window \w+ closed after \d+ ms", 0.1, mark)
        self.say("  join: %s" % (joined.group(0) if joined else "(none)"))
        self.say("  window: %s" % (closed.group(0) if closed else "(still open?)"))
        await s.connect()
        wifi = await self.wifi_lines(s)
        for l in wifi:
            self.say("  WIFI: " + l)
        n1 = next((l for l in wifi if re.search(r"network #1:", l)), "")
        self.item("d", bool(pause and joined and back and picked and picked.group(1) == "1" and "ssid 19 B" in n1),
                  "paused=%s joined #%s (%s) back=%s; #1 is %s" % (
                      bool(pause), picked and picked.group(1), joined and re.search(r"ch=\d+", joined.group(0)).group(0),
                      back, re.search(r"ssid \d+ B", n1).group(0) if n1 else "?"))

        if a.skip_e:
            await s.close()
            return
        self.say("== (e) the website's provision")
        if not self.net or len(self.net[0]) != 19:
            self.item("e", False, "this laptop is not on the 19-byte network")
            return
        got, lines = await s._send(NUS_RX, "t", "STATE", r" OK", 5)
        mac = None
        for l in lines:
            m = re.search(r"\bmac=([0-9a-fA-F:]+)", l)
            if m:
                mac = m.group(1).replace(":", "").lower()
        self.say("  STATE mac=%s" % mac)
        started = time.time() * 1000
        base, date = online(mac, 0)
        offset = 0
        if date:
            from email.utils import parsedate_to_datetime
            offset = parsedate_to_datetime(date).timestamp() * 1000 - time.time() * 1000
        since = int(started + offset - 1000)
        self.say("  baseline: joined=%s at=%s" % (base.get("joined"), base.get("at")))

        c = s.c
        state = {"v": 0, "err": 0, "result": None}
        await c.start_notify(STATE_C, lambda _, d: state.__setitem__("v", d[0]))
        await c.start_notify(ERROR_C, lambda _, d: state.__setitem__("err", d[0]))
        await c.start_notify(RESULT_C, lambda _, d: state.__setitem__("result", bytes(d)))
        asked_at = b.mark()
        state["v"] = (await c.read_gatt_char(STATE_C))[0]
        self.say("  improv state %d" % state["v"])
        ask = await self.serial(r"improv: a phone opened the service: asking", 5, asked_at)
        self.say("NEED TIP: moth is asking \"Join Wi-Fi? Tip me on my side!\" (%s)" % (bool(ask)))
        end = time.monotonic() + a.tip_wait
        by_hand = False
        while state["v"] != 2 and time.monotonic() < end:
            t = time.monotonic() + 31
            while state["v"] != 2 and time.monotonic() < t:
                await asyncio.sleep(0.2)
            if state["v"] != 2:
                await asyncio.sleep(1.5)   # let the lapse land, then open it again as a page would
                state["v"] = (await c.read_gatt_char(STATE_C))[0]
                self.say("  still asking (re-opened, state %d)" % state["v"])
        if state["v"] == 2:
            by_hand = True
            yes = b.wait(r"improv: yes.*", 1, asked_at)
            self.say("  YES by hand: %s" % (yes.group(0) if yes else "(state 2)"))
        else:
            self.say("  no hand in %d s: the cable's DEBUG side says yes instead" % a.tip_wait)
            await c.read_gatt_char(STATE_C)
            await asyncio.sleep(2)
            b.send("DEBUG side 2500 r")
            t = time.monotonic() + 10
            while state["v"] != 2 and time.monotonic() < t:
                await asyncio.sleep(0.2)
        if state["v"] != 2:
            self.item("e", False, "never authorized")
            return
        mark = b.mark()
        ssid, pw = self.net
        data = bytes([len(ssid.encode())]) + ssid.encode() + bytes([len(pw.encode())]) + pw.encode()
        t0 = time.monotonic()
        try:
            await c.write_gatt_char(RPC_C, packet(1, data), response=True)
            wrote = "answered"
        except Exception as e:  # noqa: BLE001
            wrote = "cut off (%s)" % type(e).__name__
        t = time.monotonic() + 30
        while c.is_connected and time.monotonic() < t:
            await asyncio.sleep(0.1)
        drop_s = time.monotonic() - t0
        self.say("  write %s; state %d; dropped=%s after %.1f s" % (wrote, state["v"], not c.is_connected, drop_s))
        s.c = None
        polls, joined_site = 0, None
        t = time.monotonic() + 120
        while time.monotonic() < t:
            polls += 1
            try:
                d, _ = online(mac, since)
                if d.get("joined") and (base.get("at") is None or d.get("at") > base.get("at")):
                    joined_site = d
                    break
            except Exception as e:  # noqa: BLE001
                self.say("  online ask failed: %s" % e)
            await asyncio.sleep(3)
        self.say("  site: %s after %d asks (%.0f s)" % (joined_site, polls, time.monotonic() - t0))
        join = await self.serial(r"\[wifi\] .*joined in \d+ ms rssi=-?\d+ ch=\d+", 1, mark)
        pw_line = b.wait(r"network #\d+: its (new|old) password (joined|failed).*", 0.1, mark)
        back = await self.back(mark, "provision", timeout=120)
        await s.connect()
        wifi = await self.wifi_lines(s)
        for l in wifi:
            self.say("  WIFI: " + l)
        await s.close()
        n1 = next((l for l in wifi if re.search(r"network #1:", l)), "")
        self.item("e", bool(joined_site and back and not c.is_connected and "ssid 19 B" in n1),
                  "yes %s; dropped in %.1f s; site checked in=%s; join: %s; %s; ble back=%s" % (
                      "by hand" if by_hand else "by the cable's DEBUG side (no hand)", drop_s, bool(joined_site),
                      join and re.search(r"ch=\d+", join.group(0)).group(0), pw_line and pw_line.group(0), back))


async def amain(args):
    net = laptop_network()
    hidden = secrets(net)
    log = Masked(open(args.log, "w"), hidden)
    b = Board(args.port, log)
    run = Checks(b, args, log, net)
    try:
        await run.main()
    except Fail as e:
        run.say("STOPPED: %s" % e)
    leaks = len(log.hits)
    run.say("secrets masked in the log: %d" % leaks)
    run.say("crashes: %d" % len(b.crashes))
    for k in "abcde":
        if k in run.items:
            run.say("RESULT %s %s %s" % (k, *run.items[k]))
    return 0 if all(v[0] == "PASS" for v in run.items.values()) and not leaks else 1


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("port")
    p.add_argument("--log", required=True)
    p.add_argument("--tip-wait", type=int, default=900)
    p.add_argument("--skip-e", action="store_true")
    p.add_argument("--restore", action="store_true", help="give it its home and networks first, by the cable")
    p.add_argument("--home", default="down", choices=["up", "down"])
    sys.exit(asyncio.run(amain(p.parse_args())))
