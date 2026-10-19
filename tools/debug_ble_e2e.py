# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial", "bleak>=0.22"]
# ///
"""Every debug-link verb, end to end, on one real badge over Bluetooth.

    uv run tools/debug_ble_e2e.py /dev/ttyACM1 --log debug_ble.log
    uv run tools/debug_ble_e2e.py /dev/ttyACM1 --keep-pet     # skips WIPE (the pet is not reset)

DESTRUCTIVE by default: it WIPEs the pet (founding a fresh egg of the badge's
own species), hatches and grows him, kills him into a clutch, picks an egg
and hatches the next generation, then WIPEs again so the badge is left with
a fresh egg. Only on a test badge.

Every command goes over the air (docs/debug-ble.md, docs/ble.md), from this
machine's Bluetooth to the badge, as the website's debug panel sends them.
The cable is used for two things only: to read the badge's own log (which
proves what the reply says: a reboot, a wipe, a power change, a fight), and
for the unlock's press, since `DEBUG press` on the cable is a hand on the
dish and no phone may press the button for itself.

The port is opened once and held: on Linux an open resets the board.
"""
import argparse
import asyncio
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from power_e2e import CRASH, DEBUG, NUS_RX, NUS_TX, Board, Fail  # noqa: E402

ALL_VERBS = ["HELP", "STATUS", "UNLOCK", "LOCK", "THINK", "FORTUNE", "LOOK", "FIRE", "WALL", "SCALE", "FREEHATCH",
             "HATCH", "STAGE", "WARP", "CHEM", "DIE", "SPECIES", "WIPE", "POWER", "DUEL", "NAP", "PRESS", "DOUBLE",
             "HANG", "CRADLE", "LEAN", "NET", "REBOOT"]
HARM = ["HATCH", "STAGE adult", "WARP 5", "CHEM hunger 900", "DIE", "SPECIES gilly", "WIPE", "PRESS", "DOUBLE",
        "HANG", "CRADLE", "LEAN 1", "NET clear", "REBOOT"]


class Session:
    """One Bluetooth connection: the protocol on RX/TX and the debug characteristic."""

    def __init__(self, run, name):
        self.run, self.name = run, name
        self.events, self.lines = [], {}
        self.buf = {"d": b"", "t": b""}
        self.ids = iter(range(1, 1 << 20))
        self.c = None

    def _on(self, key):
        def cb(_, data):
            self.buf[key] += bytes(data)
            *whole, self.buf[key] = self.buf[key].split(b"\n")
            for raw in whole:
                s = raw.decode("utf-8", "replace")
                self.run.b.note("ble %s << %s" % (key, s))
                if s.startswith("! "):
                    self.events.append(s)
                elif m := re.match(r"#(\w+) ", s):
                    if re.match(r"#\w+ (OK|ERR)", s):
                        self.lines[(key, m.group(1))] = s
                    else:
                        self.lines.setdefault((key, m.group(1) + "+"), []).append(s)
        return cb

    async def connect(self):
        from bleak import BleakClient, BleakScanner
        for attempt in range(3):
            dev = await BleakScanner.find_device_by_name(self.name, timeout=20)
            if not dev:
                continue
            try:
                self.c = BleakClient(dev, timeout=20)
                await self.c.connect()
                await self.c.start_notify(DEBUG, self._on("d"))
                await self.c.start_notify(NUS_TX, self._on("t"))
                await self.rx("HELLO", r"OK fw=")   # a page speaks at once, or it is dropped as a squatter
                return
            except Exception as e:  # noqa: BLE001
                self.run.b.note("ble connect attempt %d: %s" % (attempt, e))
                await asyncio.sleep(2)
        raise Fail("could not connect to %s" % self.name)

    async def close(self):
        if self.c:
            try:
                await self.c.disconnect()
            except Exception:  # noqa: BLE001
                pass
            self.c = None

    async def _send(self, uuid, key, line, want, timeout):
        i = "%x" % next(self.ids)
        self.run.b.note("ble %s >> #%s %s" % (key, i, line))
        await self.c.write_gatt_char(uuid, ("#%s %s\n" % (i, line)).encode(), response=True)
        end = time.monotonic() + timeout
        while (key, i) not in self.lines and time.monotonic() < end:
            await asyncio.sleep(0.05)
        got = self.lines.get((key, i), "")
        self.run.check(bool(re.search(want, got)), "%s %s -> %s" % ("RX" if key == "t" else "DBG", line,
                                                                    got or "(no reply)"))
        return got, self.lines.get((key, i + "+"), [])

    async def dbg(self, line, want=r" OK", timeout=5.0):
        return (await self._send(DEBUG, "d", line, want, timeout))[0]

    async def dbg_all(self, line, want=r" OK", timeout=5.0):
        return await self._send(DEBUG, "d", line, want, timeout)

    async def rx(self, line, want=r" OK", timeout=5.0):
        return (await self._send(NUS_RX, "t", line, want, timeout))[0]

    async def event(self, text, timeout, n=0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if any(text in e for e in self.events[n:]):
                return True
            await asyncio.sleep(0.1)
        return False


class Run:
    def __init__(self, board, args):
        self.b, self.args = board, args
        self.passed = self.failed = 0
        self.failures = []

    def say(self, s):
        print(s, flush=True)
        self.b.note(s)

    def check(self, ok, what):
        self.say(("  ok   " if ok else "  FAIL ") + what)
        if ok:
            self.passed += 1
        else:
            self.failed += 1
            self.failures.append(what)
            raise Fail(what)

    def soft(self, ok, what):
        try:
            self.check(ok, what)
        except Fail:
            pass

    async def serial(self, pattern, timeout, since):
        return await asyncio.to_thread(self.b.wait, pattern, timeout, since)

    async def unlock(self, s):
        await s.dbg("UNLOCK", r"OK unlock=(asking|open)")
        await asyncio.sleep(1.7)   # a press in the first 1.5 s is no yes
        n = len(s.events)
        self.b.send("DEBUG press 150")
        self.check(await s.event("unlock=open", 5, n), "unlocked by a press on the dish (the cable's)")

    async def status(self, s):
        got = await s.dbg("STATUS", r"OK unlock=")
        return dict(kv.split("=", 1) for kv in got.split(" OK ", 1)[1].split() if "=" in kv)

    async def gen(self, s):
        got = await s.rx("HELLO", r"OK fw=")
        return int(re.search(r"gen=(\d+)", got).group(1))

    async def reconnect_after_restart(self, s, since, what):
        self.b.expected_boots += 1
        await s.close()
        up = await self.serial(r"\[hw\] boot=(\w+) occupant=(\w+) gen=(\d+) genome=(\w+) age=(\d+) species=(\w+)", 20,
                               since)
        self.check(bool(up), "%s: the badge restarted (%s)" % (what, up and up.group(0)))
        await self.serial(r"\[hw\] ble adv on", 10, since)
        await asyncio.sleep(1)
        await s.connect()
        return up

    # ---- the run ---------------------------------------------------------------------------

    async def main(self):
        self.say("waiting for the boot (opening the port reset it)")
        r = self.b.wait(r"\[hw\] ble name=(\S+)", 15, since=0)
        if not r:
            raise Fail("no boot banner")
        name = r.group(1)
        species = re.search(r"species=(\w+)", "".join(l for _, l in self.b.lines if "boot=" in l)).group(1)
        self.b.expected_boots = self.b.boots
        self.b.wait(r"\[hw\] ble adv on", 5, since=0)
        s = Session(self, name)
        self.say("== connect to %s (%s)" % (name, species))
        await s.connect()

        self.say("== look: HELP and STATUS, needing nothing")
        got, plus = await s.dbg_all("HELP", r"OK %d$" % len(ALL_VERBS))
        listed = [l.split()[2] for l in plus]
        self.check(listed == ALL_VERBS, "HELP lists every verb in order (%d)" % len(listed))
        st = await self.status(s)
        self.check(st.get("unlock") == "locked", "a new connection starts locked")

        self.say("== the gate: every harm verb refused while locked")
        for v in HARM:
            await s.dbg(v, r"ERR 423 LOCKED")
        await self.unlock(s)
        await s.dbg("LOCK", r"OK unlock=locked")
        await s.dbg("HATCH", r"ERR 423 LOCKED")
        await self.unlock(s)

        if not self.args.keep_pet:
            self.say("== WIPE: the pet reset (a fresh %s egg)" % species)
            m = self.b.mark()
            await s.dbg("WIPE %s" % species, r"OK wiping found=%s" % species)
            self.check(bool(await self.serial(r"debug wipe: erasing the pet", 5, m)), "the log says the erase began")
            up = await self.reconnect_after_restart(s, m, "WIPE")
            self.check(up.group(1) == "Fresh" and up.group(2) == "egg" and up.group(6) == species,
                       "boot=Fresh occupant=egg species=%s" % species)
            self.check(await self.gen(s) == 0, "generation 0")
            await self.unlock(s)

        self.say("== write: FREEHATCH, WALL, SCALE, FIRE (no press needed)")
        await s.dbg("FREEHATCH on", r"OK freehatch=on")
        now = int(time.time())
        await s.dbg("WALL %d -420" % now, r"OK time=known wall=%d tz=-420" % now)
        st = await self.status(s)
        self.check(st.get("time") == "known", "STATUS time=known")
        await s.dbg("WALL clear", r"OK time=unknown")
        await s.dbg("WALL 12", r"ERR 416 OUT_OF_RANGE")
        await s.dbg("SCALE 0", r"ERR 416 OUT_OF_RANGE")

        self.say("== harm: HATCH, then STAGE through every stage, WARP, CHEM")
        st = await self.status(s)
        if st.get("phase") == "egg":
            await s.dbg("HATCH", r"OK phase=creature stage=baby", timeout=60)
        else:
            await s.dbg("HATCH", r"ERR 409 NOT_EGG")
        await s.dbg("HATCH", r"ERR 409 NOT_EGG")
        for stage in ("child", "adult"):
            st = await self.status(s)
            if st.get("stage") in ("adult", "elder") and stage == "child":
                continue
            await s.dbg("STAGE %s" % stage, r"OK phase=creature stage=%s" % stage, timeout=120)
        await s.dbg("STAGE child", r"ERR 409 PAST_STAGE")
        a0 = int((await self.status(s))["age"])
        await s.dbg("WARP 5", r"OK phase=creature", timeout=60)
        a1 = int((await self.status(s))["age"])
        self.check(a1 - a0 >= 3000, "WARP 5 lived five minutes (age %d -> %d ticks)" % (a0, a1))
        await s.dbg("WARP 0", r"ERR 416 OUT_OF_RANGE")
        await s.dbg("CHEM hunger 900 fear 0", r"OK 2")
        chem = await s.rx("CHEM", r"OK")
        await s.dbg("CHEM hunger 0", r"OK 1")
        await s.dbg("CHEM nonsense 5", r"ERR 416 OUT_OF_RANGE")
        await s.dbg("SCALE 20", r"OK scale=20")
        a2 = int((await self.status(s))["age"])
        await asyncio.sleep(5)
        a3 = int((await self.status(s))["age"])
        self.soft(a3 - a2 >= 400, "SCALE 20 ran him faster (%d ticks in 5 s; real time is 50)" % (a3 - a2))
        await s.dbg("SCALE 1", r"OK scale=1")

        self.say("== show: THINK, FORTUNE, LOOK; FIRE")
        await s.dbg("THINK crunch", r"OK think=crunch")
        await s.dbg("FORTUNE", r"OK fortune=p_")
        await s.dbg("LOOK gilly", r"OK look=gilly")
        self.check((await self.status(s)).get("look") == "gilly", "STATUS look=gilly")
        await s.dbg("LOOK off", r"OK look=own")
        await s.dbg("LOOK nobody", r"ERR 416 OUT_OF_RANGE")
        await s.dbg("FIRE button", r"OK fire=button")
        await s.dbg("FIRE knock", r"OK fire=knock")

        self.say("== SPECIES in place, and back")
        await s.dbg("SPECIES gilly", r"OK species=gilly|ERR 404 NO_SPECIES_GENE")
        if (await self.status(s)).get("species") == "gilly":
            await s.dbg("SPECIES %s" % species, r"OK species=%s" % species)
            self.check((await self.status(s)).get("species") == species, "his own species again")

        self.say("== the device: POWER, NAP, DUEL, PRESS, DOUBLE, HANG, CRADLE, LEAN, NET")
        m = self.b.mark()
        await s.dbg("POWER low", r"OK power=low why=asked .*cpu=80")
        self.check(bool(await self.serial(r"\[power\] power=low why=asked", 3, m)), "the badge's log: low")
        m = self.b.mark()
        await s.dbg("PRESS 200", r"OK press=200")
        self.check(bool(await self.serial(r"\[power\] power=full why=button", 3, m)), "BLE PRESS woke it")
        await s.dbg("POWER", r"OK power=full .*cpu=240")
        await s.dbg("NAP", r"OK nap=1")
        await asyncio.sleep(3)
        await s.dbg("DUEL", r"OK fight=off active=0 phase=closed")
        # Fight mode opens only when asked now: a double press. Rested first.
        await s.dbg("CHEM sleepiness 0", r"OK 1")
        m = self.b.mark()
        await s.dbg("DOUBLE", r"OK double=1")
        r = await self.serial(r"\[diag\] fight double=1 .*result=(\w+)", 5, m)
        self.check(bool(r) and r.group(1) == "entered", "BLE DOUBLE opened fight mode (%s)" % (r and r.group(0)))
        await asyncio.sleep(1)
        await s.dbg("DUEL", r"OK fight=searching active=1 phase=lobby")
        await asyncio.sleep(10)
        await s.dbg("DUEL", r"OK fight=searching active=1", timeout=5)   # no one else: it keeps searching
        m = self.b.mark()
        await s.dbg("DOUBLE", r"OK double=1")
        self.check(bool(await self.serial(r"phase lobby -> closed", 5, m)), "a second DOUBLE leaves fight mode")
        await asyncio.sleep(3)
        await s.dbg("DUEL", r"OK fight=off active=0")
        await asyncio.sleep(3)
        await s.dbg("DUEL", r"OK fight=off active=0", timeout=5)   # and it stays off: no constant fight mode
        await s.dbg("HANG 1500 1", r"OK hang=1500 wobble=1")
        await asyncio.sleep(5)
        if "fight=off" not in await s.dbg("DUEL", r"OK fight="):
            m = self.b.mark()
            await s.dbg("DOUBLE", r"OK")   # the hang opened it: leave
            await self.serial(r"-> closed", 5, m)
        await s.dbg("CRADLE 3000", r"OK cradle=3000")
        await s.dbg("LEAN 2 1000", r"OK lean=2 ms=1000")
        await s.dbg("NET", r"OK rx_drop=0 ")
        await s.dbg("NET rx 40", r"OK rx_drop=40 ")
        await s.dbg("NET clear", r"OK rx_drop=0 ")
        await asyncio.sleep(4)

        self.say("== laying eggs: DIE into a clutch, pick an egg, the next generation")
        g0 = await self.gen(s)
        await s.dbg("DIE", r"OK phase=clutch eggs=\d+ vigil=\d+", timeout=60)
        clutch = await s.rx("CLUTCH", r"OK")
        st = await self.status(s)
        self.check(st.get("phase") == "clutch", "STATUS phase=clutch")
        await s.rx("TWIST pick 0", r"OK")
        for _ in range(40):
            if (await self.status(s)).get("phase") == "egg":
                break
            await asyncio.sleep(0.5)
        self.check((await self.status(s)).get("phase") == "egg", "the picked egg is in the dish")
        await s.dbg("HATCH", r"OK phase=creature stage=baby", timeout=60)
        g1 = await self.gen(s)
        self.check(g1 == g0 + 1, "the next generation hatched (gen %d -> %d)" % (g0, g1))
        await s.rx("LINEAGE", r"OK")
        await s.dbg("DIE remains", r"OK phase=clutch eggs=\d+ vigil=\d{1,2}$", timeout=60)
        await s.dbg("NAP", r"ERR 409 NO_CREATURE")

        self.say("== REBOOT: the clutch resumes")
        h0 = await s.rx("HASH", r"OK")
        m = self.b.mark()
        await s.dbg("REBOOT", r"OK rebooting quiet=0")
        up = await self.reconnect_after_restart(s, m, "REBOOT")
        self.check(up.group(1) == "Resumed" and up.group(2) == "clutch", "boot=Resumed occupant=clutch")
        st = await self.status(s)
        self.check(st.get("unlock") == "locked", "a reconnect starts locked again")
        self.check(st.get("phase") == "clutch", "still the clutch after the reboot")

        if not self.args.keep_pet:
            self.say("== WIPE again: leave the badge a fresh %s egg" % species)
            await self.unlock(s)
            m = self.b.mark()
            await s.dbg("WIPE %s" % species, r"OK wiping found=%s" % species)
            up = await self.reconnect_after_restart(s, m, "WIPE")
            self.check(up.group(1) == "Fresh" and up.group(2) == "egg", "boot=Fresh occupant=egg")
        await s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--keep-pet", action="store_true", help="no WIPE: the pet is kept (and still killed into a clutch)")
    ap.add_argument("--log", default="debug_ble_e2e.log")
    args = ap.parse_args()
    with open(args.log, "w") as log:
        b = Board(args.port, log)
        run = Run(b, args)
        ok = True
        try:
            asyncio.run(run.main())
        except Fail as e:
            ok = False
            run.say("STOPPED: %s" % e)
        crashed = [l for _, l in b.lines if CRASH.search(l)]
        if crashed or b.boots != b.expected_boots:
            ok = False
            run.say("CRASH or unasked boot: %s (boots %d, expected %d)" % (crashed[:2], b.boots, b.expected_boots))
    print("\n%d checks passed, %d failed%s" % (run.passed, run.failed, ": " + "; ".join(run.failures) if run.failures else ""))
    print("log: %s" % args.log)
    return 0 if ok and not run.failed else 1


if __name__ == "__main__":
    sys.exit(main())
