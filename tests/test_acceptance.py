"""scripts/test/acceptance.py without a badge: its parsing of the board's lines and
replies, its verdicts, and its masking. A pretend board and a pretend server play
whole wifi and server phases; the fake .env's values must never reach the
terminal or the log, even when the board or the server echoes them back.

    python3 -m unittest tests/test_acceptance.py
"""
import base64
import importlib.util
import io
import json
import os
import queue
import re
import sys
import tempfile
import time
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("acceptance", ROOT / "scripts/test/acceptance.py")
acc = importlib.util.module_from_spec(spec)
sys.modules["acceptance"] = acc
spec.loader.exec_module(acc)

SSID = "Grandmas Wifi 5G"
PASSWORD = "hunter2-sekrit!"
TOKEN = "tok_9f8e7d6c5b4a39281706"
SECRETS = (SSID, PASSWORD, TOKEN)
TEST_URL = "https://blorb.hypnodroid.com/fw-test/lcd128/manifest.json"
GENOME = bytes(range(40)) * 3
MAC = "3c:0f:02:6f:2a:ec"


def chunks(rid, data):
    out = []
    for at in range(0, len(data), 120):
        out.append("#%s + %d %s" % (rid, at, base64.b64encode(data[at:at + 120]).decode()))
    out.append("#%s OK %d %08x" % (rid, len(data), zlib.crc32(data) & 0xFFFFFFFF))
    return out


class FakeBadge:
    """A port that answers like the acceptance firmware (wifi-ota-2 + life-stats)."""

    def __init__(self):
        self.q = queue.Queue()
        self.buf = b""
        self.fw = "0.7.13"
        self.networks = []
        self.sent = []
        self.has_due, self.next_check, self.absent = True, 900, 1
        self.air_tries, self.air_open = 0, False
        self.chems = {}
        self.extra_stims = []
        self.state_tail = " species=puffkin mac=%s" % MAC
        self.emit("[hw] blorbarium abc123 fw=%s fmt=1 lcd128" % self.fw,
                  "[hw] boot=Resumed occupant=creature gen=1 genome=deadbeef age=900 species=puffkin")

    # the port
    def read(self, n):
        try:
            return self.q.get(timeout=0.05)
        except queue.Empty:
            return b""

    def write(self, b):
        self.buf += b
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            line = raw.decode().strip("\r")
            if line:
                self.sent.append(line)
                self.answer(line)

    def close(self):
        pass

    def emit(self, *lines):
        for l in lines:
            self.q.put((l + "\n").encode())

    # the firmware
    def answer(self, line):
        m = re.match(r"#(\w+) (\w+)(?: (.*))?$", line)
        if m:
            return self.protocol(m.group(1), m.group(2), m.group(3) or "")
        if not line.startswith("DEBUG "):
            return
        d = line[6:]
        nets = " ".join('"%s"' % n for n in self.networks)
        if d.startswith("wifi set"):
            ssid = re.match(r'wifi set "([^"]*)"', d).group(1)
            self.networks = [ssid]
            self.emit('[wifi] network "%s" (with a password, saved; 1 remembered, newest first); DEBUG ota check tries it now' % ssid)
        elif d == "wifi":
            self.emit("[wifi] window=none phase=off networks=[%s] auto=1 hold=not_idle next_check_in=0s absent_streak=0 "
                      "server_streak=0 vbat=4100 mV (pin 1366 mV)" % nets,
                      '[wifi] station=off improv=1/0 consent=none trial=0 confirm_pending=0 ota=idle owner=none windows=0 '
                      'scans=0 last="" heap=90000 minheap=80000')
        elif d == "ota check":
            self.emit("[wifi] ota check asked (https://blorb.hypnodroid.com/fw/lcd128/manifest.json)",
                      "[wifi] window forced opens: dark=0 networks=1 heap=90000",
                      "[wifi] forced: off -> scanning (0 ms)",
                      '[wifi] joining "%s" (7 networks seen)' % self.networks[0],
                      '[wifi] joined "%s" rssi=-48 ch=6 ip=192.168.1.20 heap=60000' % self.networks[0],
                      "[wifi] leaky debug line: psk=%s" % PASSWORD,   # a firmware bug must not leak it either
                      "[wifi] GET manifest: 260 bytes, open 900 ms, task stack free 3800",
                      "[wifi] manifest lcd128 0.7.12 size=2407760 fmt=1 auto=1: up_to_date (running %s fmt=1)" % self.fw,
                      "[wifi] forced: up_to_date, nothing to install",
                      "[wifi] window forced closed after 4100 ms (heap=90000 minheap=52000)")
        elif d.startswith("ota check "):
            self.emit("[wifi] ota check asked (%s)" % d[10:],
                      "[wifi] window forced opens: dark=0 networks=1 heap=90000",
                      '[wifi] joined "%s" rssi=-48 ch=6 ip=192.168.1.20 heap=60000' % self.networks[0],
                      "[wifi] manifest lcd128 0.7.14 size=2714272 fmt=1 auto=1: install (running %s fmt=1)" % self.fw,
                      "[wifi] downloading 2714272 bytes (signature ok, pet saved, slot open)",
                      "[wifi] image at 1000000 bytes heap=50000 minheap=40000",
                      "[wifi] forced: downloaded and verified in 31 s; restarting into it once the badge is dark and alone",
                      "[wifi] window forced closed after 33000 ms (heap=90000 minheap=40000)")
        elif d == "power low":
            self.emit("[power] now power=low why=asked idle_ms=3600000 left_ms=0 cpu=80 frames=10 changes=1 held=0 "
                      "input=none sleep=light slept_ms=0")
            if self.fw == "0.7.13":
                self.fw = "0.7.14"
                self.emit("[hw] ota: restarting into the update, save ok",
                          "[hw] blorbarium abc124 fw=0.7.14 fmt=1 lcd128",
                          "[hw] boot=Resumed occupant=creature gen=1 genome=deadbeef age=901 species=puffkin",
                          "[hw] ota trial: on trial, panel=1 resumed=1 handover=WIFI",
                          '[wifi] networks 1 ["%s"], automatic checks on, heap 100000 -> 98000' % self.networks[0],
                          "[wifi] on trial after an update over Wi-Fi: confirming over Wi-Fi, screen dark",
                          "[wifi] confirm: trial confirmed over Wi-Fi (ok; the site has 0.7.12)",
                          "[hw] ota trial: kept")
        elif d.startswith("power"):
            low = self.fw == "0.7.14" and d == "power"
            self.emit("[power] now power=%s why=asked idle_ms=3600000 left_ms=0 cpu=%d frames=10 changes=1 held=0 "
                      "input=none sleep=off slept_ms=0" % ("low" if low else "full", 80 if low else 240))
        elif d.startswith("home"):
            self.emit("[hw] home rot=2 (USB-C up, nvs; stored up; built up)")
        elif d == "wifi due":   # test-verbs-wifi; an older firmware answers "want: wifi [...]"
            if not self.has_due:
                return self.emit("[wifi] want: wifi [status|set <ssid> [password]|forget [ssid]|on|off]")
            self.next_check = 0
            self.emit("[wifi] due: the next automatic check is due now (it opens once dark 60 s and alone)",
                      "[wifi] window=none phase=off networks=[] auto=1 hold=not_idle next_check_in=0s absent_streak=%d "
                      "server_streak=0 vbat=4100 mV (pin 1366 mV)" % self.absent,
                      '[wifi] station=off improv=1/0 consent=none trial=0 confirm_pending=0 ota=idle owner=none windows=0 '
                      'scans=0 last="" heap=90000 minheap=80000')
        elif d.startswith("air"):   # wifi-ota-2 aa4c8c1
            what = d[4:] or "status"
            if what == "open":
                self.air_tries += 1
                why = "wifi_up" if self.air_tries == 1 else "ok"   # a window still closing, the first time
                self.air_open = why == "ok"
                return self.emit("[hw] debug air open: %s heap=61000 minheap=52000 big=31000" % why)
            if what == "close":
                self.air_open = False
            self.emit("[hw] debug air %s: open=%d heap=98000 minheap=52000 big=45000" % (what, int(self.air_open)))
        elif d.startswith("chem"):   # test-verbs
            parts = d.split()
            name, permille = (parts + ["", ""])[1:3]
            if name not in ("hunger", "sleepiness", "toxin", "injury", "life"):
                return self.emit("[hw] debug chem %s %s: not a drive or chemical (hunger, sleepiness, injury, toxin, ...)" % (name, permille))
            if not permille.isdigit() or int(permille) > 1000:
                return self.emit("[hw] debug chem %s %s: want <permille 0..1000>" % (name, permille))
            self.chems[name] = int(permille)
            self.emit("[hw] debug chem %s=%s" % (name, permille))
        elif d.startswith("trace"):
            self.emit("[hw] debug trace %s s" % (d.split() + ["0"])[1])
        elif d == "pickup":
            self.emit("[hw] debug pickup: still 2000 ms, then lifted toward home rot=2 and carried, set down, over in 5500 ms",
                      "[imu] 1000 0 0 1000 0 2 0 0 0 0 pk 0 0 1000 aim -1 busy 0 ring 0",
                      "[stim] 3100 picked_up", "[stim] %d put_down" % 6900)
            for s in self.extra_stims:
                self.emit("[stim] 3200 %s" % s)
        else:
            self.emit("[hw] debug: unknown verb '%s' (try: ...)" % d.split()[0])

    def protocol(self, rid, verb, args):
        if verb == "HELLO":
            self.emit("#%s OK fw=%s fmt=1 lineage=00d15400000b gen=1 name=Blorb phase=creature feats=0x00" % (rid, self.fw))
        elif verb == "GENOME":
            self.emit(*chunks(rid, GENOME))
        elif verb == "STATE":
            self.emit("#%s + phase=creature gen=1 stage=adult age=900 action=explore face=happy asleep=0%s" % (rid, self.state_tail),
                      "#%s + drives hunger=120 sleepiness=40 boredom=0" % rid,
                      "#%s + dish pantry=3 pellets=0 night=0 time=unknown dark=0 still=0" % rid, "#%s OK" % rid)
        elif verb == "STATS" and args == "json":
            doc = {"schema": "blorb.life-stats", "v": 1, "fw": self.fw, "gen": 1, "species": "puffkin",
                   "life": {"presses": 3, "bites": 2, "death": None}, "analytics": {"powered_s": 100}}
            self.emit(*chunks(rid, json.dumps(doc).encode()))
        else:
            self.emit("#%s ERR 404 UNKNOWN_VERB" % rid)


class FakeServer:
    """The live site's routes the server phase uses; it echoes the token back to prove the mask."""

    def __init__(self):
        self.stats = []
        self.calls = []

    def __call__(self, method, url, body=None, token=None, timeout=60):
        self.calls.append((method, url, token))
        path = url.split("blorb.hypnodroid.com", 1)[-1]
        if url == acc.LIVE_MANIFEST:
            return 200, {"version": "0.7.12"}
        if url == TEST_URL:
            return 200, {"version": "0.7.14"}
        if path == "/api/pet-lines" and method == "POST":
            assert body["genome"] == GENOME.hex()
            return 200, {"version": 1, "model": "m", "bible": "He trusts nobody who says %s" % TOKEN,
                         "lines": {"peckish": ["SNACK"], "starving": ["DISH"]}, "fallback": []}
        if path.startswith("/api/admin/") and token != TOKEN:
            return 401, {"error": "admin token"}
        if path.startswith("/api/admin/badges/"):
            import hashlib
            return 200, {"creatures": [{"id": 7, "genome_hash": hashlib.sha256(GENOME).hexdigest(), "species": "puffkin",
                                        "generation": 1, "stage": "adult"}]}
        if path == "/api/admin/creatures/7":
            return 200, {"stats": list(self.stats), "packs": [{}]}
        if path == "/api/stats":
            self.stats.append(body)
            return 200, {"ok": True}
        if path == "/admin":
            return 200, "<html>"
        return 404, {"error": "no such endpoint"}


def run_phases(phases, extra=(), badge=None):
    tmp = tempfile.TemporaryDirectory()
    env = Path(tmp.name, ".env")
    env.write_text('ssid="%s"\npassword=%s\nadmin_token=%s\n' % (SSID, PASSWORD, TOKEN))
    catalog = Path(tmp.name, "catalog.json")
    catalog.write_text(json.dumps({"situations": [{"name": "peckish"}, {"name": "starving"}]}))
    log = Path(tmp.name, "acceptance.log")
    args = acc.parse_args(["--serial", "5B91046680", "--phase", phases, "--env", str(env), "--log", str(log),
                           "--catalog", str(catalog), "--mac", MAC, *extra])
    secrets = acc.read_env(env)
    stream = io.StringIO()
    out = acc.Out(str(log), acc.Redactor({k: secrets.get(k, "") for k in acc.SECRET_KEYS}), stream=stream)
    badge = badge or FakeBadge()
    board = acc.Board(out, "5B91046680", "/dev/fake", opener=lambda dev: badge, finder=lambda s: "/dev/fake")
    server = FakeServer()
    run = acc.Run(board, out, args, secrets, http=server)
    try:
        run.run(args.phases)
        out.say(run.summary())
    finally:
        board.close()
    out.f.close()
    return run, stream.getvalue(), log.read_text(), badge, server, tmp


class Masking(unittest.TestCase):
    def test_every_form_of_a_secret_is_masked(self):
        r = acc.Redactor({"ssid": SSID, "password": PASSWORD, "admin_token": TOKEN})
        for line in ('[wifi] joined "%s" rssi=-40' % SSID, "psk=%s;" % PASSWORD, json.dumps({"p": PASSWORD + '"'}),
                     "url?t=%s" % __import__("urllib.parse").parse.quote(TOKEN + "/x", safe=""),
                     "Bearer %s" % TOKEN):
            masked = r(line)
            for s in SECRETS:
                self.assertNotIn(s, masked)
        self.assertEqual(r('networks=["%s"]' % SSID), 'networks=["<ssid>"]')

    def test_a_secret_cut_off_at_the_line_cap_is_masked(self):
        r = acc.Redactor({"ssid": SSID})
        masked = r('[wifi] window=check networks=["%s' % SSID[:9])
        self.assertNotIn(SSID[:9], masked)
        self.assertIn("<ssid>~", masked)

    def test_read_env_takes_quotes_and_comments(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d, ".env")
            p.write_text("# the test network\nSSID='%s'\npassword = %s\n\nadmin_token=%s\n" % (SSID, PASSWORD, TOKEN))
            self.assertEqual(acc.read_env(p), {"ssid": SSID, "password": PASSWORD, "admin_token": TOKEN})
            self.assertEqual(acc.read_env(Path(d, "missing")), {})


class Parsing(unittest.TestCase):
    def test_state(self):
        st = acc.parse_state(["phase=creature gen=2 stage=adult age=300 action=juggle face=happy asleep=0",
                              "drives hunger=650 sleepiness=10", "body life=900 injury=0 glow=0 dreaming=0",
                              "dish pantry=2 pellets=1 night=0 time=unknown dark=1 still=50"])
        self.assertEqual((st["phase"], st["gen"], st["action"]), ("creature", 2, "juggle"))
        self.assertEqual(st["drives"]["hunger"], 650)
        self.assertEqual(st["dish"], {"pantry": 2, "pellets": 1, "night": 0, "time": "unknown", "dark": 1, "still": 50})
        cl = acc.parse_state(["phase=clutch gen=3 eggs=3 cursor=0 vigil=18601 cause=old_age"])
        self.assertEqual((cl["eggs"], cl["vigil"], cl["cause"]), (3, 18601, "old_age"))

    def test_chem_lineage_stats(self):
        ch = acc.parse_chem(["chem=21 name=toxin level=400", "locus=12 name=food_rotten level=1000"])
        self.assertEqual(ch, {"chem": {"toxin": 400}, "locus": {"food_rotten": 1000}})
        rows = acc.parse_lineage(["founding id=00d15400000b species=puffkin", "death gen=1 cause=old_age age=9 feats=0x00 name=Blorb",
                                  "birth gen=2 chosen=1 of=3 ops=4"])
        self.assertEqual([r["kind"] for r in rows], ["founding", "death", "birth"])
        self.assertEqual((rows[2]["chosen"], rows[2]["of"]), (1, 3))
        stats = acc.parse_stats(["life lineage=00d15400000b gen=2 seq=4", "life presses=1 bites=0 age_s=30 death=none",
                                 "parent gen=1", "parent presses=9 bites=4 age_s=900 death=old_age",
                                 "analytics powered_s=100 buckets=1 open=1"])
        self.assertEqual(stats["life"]["gen"], 2)
        self.assertEqual(stats["parent"], {"gen": 1, "presses": 9, "bites": 4, "age_s": 900, "death": "old_age"})

    def test_chunks_are_checked(self):
        lines = chunks("a", GENOME)
        plus = [l[len("#a + "):] for l in lines[:-1]]
        ok = lines[-1][len("#a OK "):]
        self.assertEqual(acc.decode_chunks(plus, ok), GENOME)
        with self.assertRaises(acc.Fail):
            acc.decode_chunks(plus, "%d 00000000" % len(GENOME))
        with self.assertRaises(acc.Fail):
            acc.decode_chunks(plus[:-1], ok)

    def test_fps_latency_wifi_manifest(self):
        f = acc.parse_fps("[fps] fps=48.6 frames=486 over_ms=10001 draw_ms=11.9 push_ms=1.6 worst_ms=33 rows=21 cpu=240")
        self.assertEqual((f["fps"], f["worst_ms"]), (48.6, 33))
        lat = acc.parse_latency(["[lat] knock n=4 words=4 silent=0 noack=0 seen=0.4/1.1/1.3 detect=-",
                                 "[lat] tilt n=2 words=2 silent=0 noack=0 ack=20.0/35.5/36.0", "[lat] done"])
        self.assertEqual(lat["knock"]["seen"], (0.4, 1.1, 1.3))
        self.assertNotIn("detect", lat["knock"])
        self.assertEqual(lat["tilt"]["ack"][1], 35.5)
        w = acc.parse_wifi_status(['[wifi] window=none phase=off networks=["a b" "c"] auto=1 hold=not_idle '
                                   'next_check_in=900s absent_streak=1', "[wifi] station=off scans=2 last=\"x\""])
        self.assertEqual(w["networks"], ["a b", "c"])
        self.assertEqual((w["absent_streak"], w["scans"]), (1, 2))
        self.assertEqual(w["next_check_in"], 900)
        m = acc.parse_manifest_line("[wifi] manifest lcd128 0.7.12 size=2407760 fmt=1 auto=1: up_to_date (running 0.7.13 fmt=1)")
        self.assertEqual((m["version"], m["verdict"], m["running"]), ("0.7.12", "up_to_date", "0.7.13"))

    def test_live_verdicts(self):
        self.assertTrue(acc.live_verdict(0, "up_to_date", {"running": "0.7.13", "version": "0.7.12"})[0])
        self.assertTrue(acc.live_verdict(1, "install refused: BAD_SIGNATURE", None)[0])
        self.assertFalse(acc.live_verdict(2, "downloading 5 bytes", None)[0])
        self.assertFalse(acc.live_verdict(3, "could not join", None)[0])


class Verdicts(unittest.TestCase):
    def test_rot(self):
        refused, ate, _ = acc.rot_verdict([(0, 100, 1, 0), (10, 400, 1, 0), (20, 700, 1, 0), (25, 820, 0, 380)])
        self.assertTrue(refused and ate)
        refused, ate, note = acc.rot_verdict([(0, 100, 1, 0), (10, 450, 0, 380)])
        self.assertFalse(refused)
        self.assertIn("eaten at 10 min", note)
        refused, ate, _ = acc.rot_verdict([(0, 100, 1, 0), (10, 500, 1, 300)])
        self.assertFalse(refused)
        self.assertEqual(acc.rot_verdict([(0, 100, 1, 0), (10, 700, 1, 0)])[:2], (True, False))

    def test_idle_night_pick(self):
        self.assertTrue(acc.idle_verdict(["rest", "juggle", "explore", "rest"], {"juggle", "explore"})[0])
        self.assertFalse(acc.idle_verdict(["rest"] * 30, {"juggle"})[0])
        self.assertFalse(acc.night_verdict({"night": 1, "time": "unknown"})[0])
        self.assertTrue(acc.night_verdict({"night": 1, "time": "known"})[0])
        self.assertEqual([acc.pick_target(g, 3) for g in range(4)], [1, 2, 1, 2])
        self.assertEqual(acc.pick_target(5, 2), 1)
        self.assertEqual(acc.pick_target(5, 1), 0)
        self.assertTrue(acc.idle_names())   # the repo's idles.def

    def test_stats_and_pack_and_perf(self):
        self.assertEqual(acc.stats_carried({"gen": 1, "presses": 3, "age_s": 50, "death": "none"},
                                           {"gen": 1, "presses": 3, "age_s": 51, "death": "old_age"}), [])
        self.assertTrue(acc.stats_carried({"presses": 3}, {"presses": 2, "death": "old_age"}))
        self.assertTrue(acc.stats_carried({"presses": 3}, {"presses": 3, "death": "none"}))
        self.assertEqual(acc.pack_coverage({"lines": {"a": ["X"], "b": []}, "fallback": ["b"]}, ["a", "b"]), (["b"], ["b"]))
        self.assertTrue(acc.perf_close({"fps": 48.0, "worst_ms": 30}, {"fps": 46.0, "worst_ms": 45})[0])
        self.assertFalse(acc.perf_close({"fps": 48.0, "worst_ms": 30}, {"fps": 30.0, "worst_ms": 45})[0])
        self.assertTrue(acc.latency_close({"knock": {"ack": (40, 60, 70)}}, {"knock": {"ack": (41, 80, 90)}})[0])
        self.assertFalse(acc.latency_close({"knock": {"ack": (40, 60, 70)}}, {"knock": {"ack": (41, 200, 300)}})[0])


class PretendRuns(unittest.TestCase):
    def assertNoSecrets(self, *texts):
        for t in texts:
            for s in SECRETS:
                self.assertNotIn(s, t)

    def test_wifi_phase_masks_and_passes(self):
        run, stdout, log, badge, server, tmp = run_phases("wifi", ["--test-manifest", TEST_URL])
        self.assertNoSecrets(stdout, log)
        self.assertIn('DEBUG wifi set "<ssid>" <password>', log)
        self.assertIn("psk=<password>", log)              # the leaky line is there, masked
        self.assertTrue(any(SSID in l and PASSWORD in l for l in badge.sent))   # and the badge did get them
        status = {s["name"]: s["status"] for s in run.report.steps}
        for step in ("provision", "live manifest refused", "test manifest offered", "download and verify",
                     "restart while dark", "new version boots on trial", "trial confirmed", "same pet", "same home",
                     "fight radio fits after a window"):
            self.assertEqual(status.get(step), "PASS", "%s: %s\n%s" % (step, status.get(step), stdout))
        self.assertIn("PASS", stdout)
        tmp.cleanup()

    def test_server_phase_masks_the_token(self):
        run, stdout, log, badge, server, tmp = run_phases("server")
        self.assertNoSecrets(stdout, log)
        self.assertIn("<admin_token>", stdout)            # the server's echo, masked
        status = {s["name"]: s["status"] for s in run.report.steps}
        for step in ("STATE names the board", "pet-lines pack", "admin: creature record", "admin: stats", "admin page"):
            self.assertEqual(status.get(step), "PASS", "%s\n%s" % (step, stdout))
        self.assertEqual(len(server.stats), 1)
        self.assertEqual(server.stats[0]["mac"], MAC)
        self.assertTrue(all(t in (None, TOKEN) for _, _, t in server.calls))
        tmp.cleanup()

    def test_the_server_record_must_be_the_pet_state_names(self):
        badge = FakeBadge()
        badge.state_tail = " species=gilly mac=%s" % MAC   # the record says puffkin
        run, stdout, log, badge, server, tmp = run_phases("server", badge=badge)
        status = {s["name"]: s["status"] for s in run.report.steps}
        self.assertEqual(status.get("admin: creature record"), "FAIL", stdout)
        tmp.cleanup()
        badge = FakeBadge()
        badge.state_tail = ""   # a firmware before test-verbs: --mac, and the step says why
        run, stdout, log, badge, server, tmp = run_phases("server", badge=badge)
        status = {s["name"]: s["status"] for s in run.report.steps}
        self.assertEqual((status.get("STATE names the board"), status.get("admin: creature record")), ("FAIL", "PASS"))
        tmp.cleanup()

    def test_a_missing_verb_is_named(self):
        run, stdout, log, badge, server, tmp = run_phases("lifecycle", ["--generations", "1", "--idle-seconds", "0"])
        self.assertIn("verbs this firmware did not know", stdout)
        self.assertTrue(run.report.failed())
        self.assertNoSecrets(stdout, log)
        tmp.cleanup()


class NewVerbs(unittest.TestCase):
    """The verbs test-verbs, test-verbs-wifi and wifi-ota-2 add, as their replies read."""

    def setUp(self):
        self.stream = io.StringIO()
        self.out = acc.Out(None, acc.Redactor({}), stream=self.stream)
        self.badge = FakeBadge()
        self.board = acc.Board(self.out, "5B91046680", "/dev/fake", opener=lambda dev: self.badge,
                               finder=lambda s: "/dev/fake")
        args = acc.parse_args(["--serial", "5B91046680", "--env", "/nonexistent", "--mac", MAC])
        self.run = acc.Run(self.board, self.out, args, {}, http=FakeServer())

    def tearDown(self):
        self.board.close()

    def test_state_names_the_board(self):
        st = self.run.state()
        self.assertEqual((st["species"], st["mac"]), ("puffkin", MAC))
        self.assertEqual(acc.state_board(st, MAC)[:2], (True, MAC))
        self.assertFalse(acc.state_board(st, "3c:0f:02:00:00:01")[0])   # another board's MAC
        old = acc.parse_state(["phase=creature gen=1 stage=adult age=9 action=rest face=happy asleep=0"])
        ok, mac, note = acc.state_board(old, MAC)
        self.assertEqual((ok, mac), (False, MAC))   # --mac as the fallback
        self.assertIn("lacks test-verbs", note)
        self.assertFalse(acc.state_board(acc.parse_state(["phase=egg gen=0 progress=0 species=none mac=none"]), MAC)[0])

    def test_chem_sets_and_says_why_not(self):
        self.assertEqual(self.run.chem_set("hunger", 600), 600)
        self.assertEqual(self.badge.chems["hunger"], 600)
        with self.assertRaises(acc.Fail) as e:
            self.run.chem_set("nonsense", 5)
        self.assertIn("not a drive or chemical", str(e.exception))
        with self.assertRaises(acc.Fail):
            self.run.chem_set("hunger", 1001)

    def test_pickup_traces_picked_up_and_nothing_else(self):
        polls = []
        stims, line = self.run.pickup(during=lambda end: polls.append(end - time.monotonic()))
        self.assertEqual(stims, ["picked_up", "put_down"])
        self.assertIn("over in 5500 ms", line)
        self.assertTrue(polls and 5.0 < polls[0] <= 5.5)   # told when it ends
        self.assertIn("DEBUG trace 0", self.badge.sent)    # the trace is turned off after
        self.assertTrue(acc.pickup_verdict(stims)[0])
        self.badge.extra_stims = ["cradle"]
        stims, _ = self.run.pickup()
        ok, note = acc.pickup_verdict(stims)
        self.assertFalse(ok)
        self.assertIn("cradle", note)
        self.assertFalse(acc.pickup_verdict(["put_down"])[0])
        self.assertEqual(acc.parse_stims(["[stim] 10 knock", "[imu] 1 2 3", "[stim] 20 picked_up"]), ["knock", "picked_up"])

    def test_wifi_due_and_a_firmware_without_it(self):
        st, lines = self.run.wifi_due()
        self.assertEqual((st["next_check_in"], st["absent_streak"]), (0, 1))
        self.assertTrue(any("[wifi] due:" in l for l in lines))
        self.badge.has_due = False
        self.assertIsNone(self.run.wifi_due())
        self.assertIn("DEBUG wifi due", self.run.missing)

    def test_air_fits_after_a_window_still_closing(self):
        self.assertTrue(self.run.air_fits("wifi", "fight radio fits"))
        self.assertEqual(self.badge.air_tries, 2)   # wifi_up once, then ok
        self.assertFalse(self.badge.air_open)       # and closed again
        self.assertIn("open: ok after 2 tries", self.run.report.steps[-1]["note"])


class Identity(unittest.TestCase):
    def test_another_badge_is_refused(self):
        from unittest import mock
        with mock.patch.object(acc, "find_port_by_serial", return_value="/dev/ttyACM9"), \
                mock.patch.object(acc, "serial_of", return_value="5C94094999"), \
                mock.patch.object(acc, "Board", side_effect=AssertionError("opened the wrong badge")):
            buf = io.StringIO()
            with mock.patch("sys.stdout", buf):
                rc = acc.main(["--port", "/dev/ttyACM9", "--serial", "5B91046680", "--env", "/nonexistent"])
        self.assertEqual(rc, 2)
        self.assertIn("REFUSING", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
