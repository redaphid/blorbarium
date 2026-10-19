# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial", "bleak>=0.22"]
# ///
"""End-to-end proof of the no-button controls on one real badge (lib/blorb/include/blorb/controls.h).

    uv run tools/gesture_e2e.py /dev/ttyACM0 --log gesture.log
    uv run tools/gesture_e2e.py /dev/ttyACM0 --only knock_knock_feeds,side_hold --no-ble

The kids' cases cover BOOT, so each word the button says has a twin made with
the body: a knock knock is a press, a turn onto a side held is a hold, the
hang is a double press. Every gesture here goes through the real IMU path on
the board: `DEBUG knock` puts the tap engine's report on the next raw sample,
`DEBUG side` and `DEBUG hang` replace the raw accelerometer sample, so the
orientation settle (orient.h), the detectors (senses.cpp) and the Dish's
routing (boot.h) decide, as with a kid's hands. BOOT is never pressed except
to unlock the debug link.

Each scenario checks, from the board's own log and the protocol's STATE (the
cable speaks it too): what the gesture did (a pellet out of the pantry, the
tuck-in, fight mode), what it must not have done, and no crash.

A badge with an egg is hatched first (`DEBUG hatch`); its pet is fed and
tucked in by the run. The port is opened once and held: on Linux an open
resets the board.
"""
import argparse
import asyncio
import re
import sys
import time

from power_e2e import CRASH, DEBUG, NUS_RX, Board, Fail


class Run:
    SCENARIOS = ["knock_knock_feeds", "one_knock_no_pellet", "side_hold", "short_side_nothing",
                 "hang_fight_and_lobby_feed", "waking_knock_feeds_nothing", "ble"]

    def __init__(self, board):
        self.b = board
        self.results = []
        self.ble_name = None

    def say(self, s):
        print(s, flush=True)
        self.b.note(s)

    def check(self, ok, what):
        self.say(("  ok   " if ok else "  FAIL ") + what)
        if not ok:
            raise Fail(what)

    # ---- what the board says ------------------------------------------------------------

    def state(self):
        """pantry, pellets and the phase, from the protocol's STATE over the cable."""
        m = self.b.mark()
        self.b.send("#e STATE")
        if not self.b.wait(r"^#e OK", 3, since=m):
            raise Fail("no STATE reply")
        out = {}
        for _, line in self.b.lines[m:]:
            if g := re.search(r"#e \+ dish pantry=(\d+) pellets=(\d+)", line):
                out["pantry"], out["pellets"] = int(g.group(1)), int(g.group(2))
            if g := re.search(r"#e \+ phase=(\w+)", line):
                out["phase"] = g.group(1)
        return out

    def fight(self):
        r = self.b.ask("DEBUG duel", r"\[duel\] now active=(\d) phase=(\w+)")
        return r.group(1) == "1", r.group(2)

    def quiet(self, s):
        """Nothing for s seconds: the poses' upright tail and the detectors' rearm."""
        time.sleep(s)

    # ---- setup --------------------------------------------------------------------------

    def boot(self):
        self.say("waiting for the boot (opening the port reset it)")
        r = self.b.wait(r"\[hw\] ble name=(\S+)", 15, since=0)
        if not r:
            self.b.expected_boots += 1
            self.b.p.rts = True
            time.sleep(0.1)
            self.b.p.rts = False
            r = self.b.wait(r"\[hw\] ble name=(\S+)", 15, since=0)
        if not r:
            raise Fail("no boot banner")
        self.ble_name = r.group(1)
        self.b.expected_boots = self.b.boots
        self.b.wait(r"\[hw\] ble adv on", 5, since=0)
        self.b.ask("DEBUG power wake", r"\[power\] now power=full")
        self.b.ask("DEBUG power idle 3600", r"idle_ms=3600000")   # no low power mid-scenario (one scenario asks for it)
        st = self.state()
        if st.get("phase") == "egg":
            self.say("  an egg: hatching him (DEBUG hatch)")
            self.b.ask("DEBUG hatch", r"\[hw\] debug hatch: ok", timeout=90)
        self.rested()

    def rested(self):
        """Awake, rested and out of fight mode, with food in the pantry."""
        if self.fight()[0]:
            self.b.expected_boots += 1
            m = self.b.mark()
            self.b.send("DEBUG reboot")
            self.check(bool(self.b.wait(r"\[hw\] ble adv on", 15, since=m)), "rebooted out of fight mode")
            self.b.ask("DEBUG power idle 3600", r"idle_ms=3600000")
        self.b.ask("DEBUG power wake", r"\[power\] now power=full")
        self.b.ask("DEBUG sleepiness 0", r"debug sleepiness 0%")
        st = self.state()
        if st.get("pantry", 0) < 2:
            raise Fail("the pantry is nearly empty (%s): it refills on the pet clock; run again later" % st.get("pantry"))

    # ---- scenarios ----------------------------------------------------------------------

    def sc_knock_knock_feeds(self):
        """A knock knock is a press: a pellet out of the pantry, and the log names the gesture."""
        st0 = self.state()
        m = self.b.mark()
        self.b.send("DEBUG knock 2")
        g = self.b.wait(r"\[diag\] gesture knock_knock=press", 3, since=m)
        self.check(bool(g), "the log says gesture knock_knock=press")
        time.sleep(0.5)
        st1 = self.state()
        self.check(st1["pantry"] == st0["pantry"] - 1, "a pellet left the pantry (%d -> %d)" % (st0["pantry"], st1["pantry"]))
        self.quiet(1)

    def sc_one_knock_no_pellet(self):
        """One knock is a bumped desk: he feels it, and nothing drops."""
        st0 = self.state()
        m = self.b.mark()
        self.b.send("DEBUG knock 1")
        self.b.wait(r"\[hw\] debug knock 1", 2, since=m)
        g = self.b.wait(r"\[diag\] (gesture|boot press)", 2, since=m)
        self.check(not g, "no press (%s)" % (g and g.group(0)))
        st1 = self.state()
        self.check(st1["pantry"] == st0["pantry"], "the pantry is untouched (%d -> %d)" % (st0["pantry"], st1["pantry"]))

    def sc_side_hold(self):
        """Turned onto a side and held: the hold (tuck-in) once it lasts 1.2 s, nothing dropped, either side."""
        for which in ("r", "l"):
            st0 = self.state()
            m = self.b.mark()
            self.b.send("DEBUG side 3500 %s" % which)
            t0 = time.monotonic()
            g = self.b.wait(r"\[diag\] gesture side=hold ms=1200", 5, since=m)
            took = time.monotonic() - t0
            self.check(bool(g), "side %s: the log says gesture side=hold (%.1f s after the turn)" % (which, took))
            self.check(1.5 <= took <= 3.2, "side %s: after the settle and the hold, not sooner (%.1f s)" % (which, took))
            again = self.b.wait(r"\[diag\] gesture side=hold", 2.5)
            self.check(not again, "side %s: one hold per turn" % which)
            st1 = self.state()
            self.check(st1["pantry"] == st0["pantry"], "side %s: nothing dropped" % which)
            self.quiet(2.5)   # the pose's upright tail: armed again

    def sc_short_side_nothing(self):
        """A turn let go before the hold is nothing at all: no press, no hold."""
        st0 = self.state()
        m = self.b.mark()
        self.b.send("DEBUG side 1000")
        g = self.b.wait(r"\[diag\] (gesture|boot)", 4.5, since=m)
        self.check(not g, "nothing (%s)" % (g and g.group(0)))
        st1 = self.state()
        self.check(st1["pantry"] == st0["pantry"], "nothing dropped")

    def sc_hang_fight_and_lobby_feed(self):
        """The hang opens fight mode; there a knock knock still feeds him; the hang again leaves."""
        self.rested()
        m = self.b.mark()
        self.b.send("DEBUG hang 2500")
        g = self.b.wait(r"\[diag\] fight double=0 hang=1 ctx=\w+ gate=\S+ result=(\w+)", 5, since=m)
        self.check(bool(g) and g.group(1) == "entered", "a hang entered fight mode (%s)" % (g and g.group(0)))
        self.quiet(3.5)
        active, phase = self.fight()
        self.check(active and phase == "lobby", "fight mode is searching (active=%d phase=%s)" % (active, phase))
        st0 = self.state()
        self.b.send("DEBUG knock 2")
        time.sleep(1.5)   # a press in the lobby waits out the double's window
        st1 = self.state()
        self.check(st1["pantry"] == st0["pantry"] - 1, "in the lobby a knock knock feeds him (%d -> %d)" % (st0["pantry"], st1["pantry"]))
        self.b.send("DEBUG hang 2500")
        self.quiet(6.5)   # the hang, its 3 s upright tail, and the real sample again
        active, phase = self.fight()
        self.check(not active, "a hang again left fight mode (active=%d phase=%s)" % (active, phase))

    def sc_waking_knock_feeds_nothing(self):
        """Dark, a knock knock wakes the badge and only lights it: nobody fed him unseen."""
        st0 = self.state()
        self.b.ask("DEBUG power low", r"\[power\] now power=low")
        time.sleep(4)   # the cable's 3 s hold lapses: the chip may light-sleep now
        p = self.b.ask("DEBUG power", r"\[power\] now power=(\w+)")
        self.check(p.group(1) == "low", "dark before the knock (power=%s)" % p.group(1))
        time.sleep(3.5)
        m = self.b.mark()
        self.b.send("DEBUG knock 2")
        w = self.b.wait(r"\[power\] power=full why=(\w+)", 3, since=m)
        self.check(bool(w) and w.group(1) == "tap", "a knock knock woke it (why=%s)" % (w and w.group(1)))
        g = self.b.wait(r"\[diag\] gesture", 1.5, since=m)
        self.check(not g, "the waking knock knock was no press")
        st1 = self.state()
        self.check(st1["pantry"] == st0["pantry"], "nothing dropped (%d -> %d)" % (st0["pantry"], st1["pantry"]))

    # ---- over Bluetooth: the debug link's SIDE and KNOCK ------------------------------------

    def sc_ble(self):
        asyncio.run(self._ble())

    async def _ble(self):
        from bleak import BleakClient, BleakScanner

        self.say("  scanning for %s" % self.ble_name)
        dev = await BleakScanner.find_device_by_name(self.ble_name, timeout=20)
        if not dev:
            raise Fail("no %s advertising" % self.ble_name)
        lines, events, buf = {}, [], {"d": b""}

        def on(_, data):
            buf["d"] += bytes(data)
            *whole, buf["d"] = buf["d"].split(b"\n")
            for raw in whole:
                s = raw.decode("utf-8", "replace")
                self.b.note("ble << %s" % s)
                if s.startswith("! "):
                    events.append(s)
                elif m := re.match(r"#(\w+) (OK|ERR)", s):
                    lines[m.group(1)] = s

        async with BleakClient(dev, timeout=20) as c:
            await c.start_notify(DEBUG, on)
            await c.write_gatt_char(NUS_RX, b"HELLO\n", response=True)
            ids = iter(range(1, 1 << 20))

            async def dbg(line, want=r"OK", timeout=4.0):
                i = "%x" % next(ids)
                self.b.note("ble dbg >> #%s %s" % (i, line))
                await c.write_gatt_char(DEBUG, ("#%s %s\n" % (i, line)).encode(), response=True)
                end = time.monotonic() + timeout
                while i not in lines and time.monotonic() < end:
                    await asyncio.sleep(0.05)
                got = lines.get(i, "")
                self.check(bool(re.search(want, got)), "BLE %s -> %s" % (line, got or "(no reply)"))
                return got

            await dbg("KNOCK", r"ERR 423 LOCKED")
            await dbg("UNLOCK", r"OK unlock=asking")
            await asyncio.sleep(1.7)
            self.b.send("DEBUG press 150")   # the cable is a hand on the dish
            end = time.monotonic() + 4
            while not any("unlock=open" in e for e in events) and time.monotonic() < end:
                await asyncio.sleep(0.1)
            self.check(any("unlock=open" in e for e in events), "unlocked by a press on the dish")
            await asyncio.sleep(1.0)
            st0 = await asyncio.to_thread(self.state)
            m = self.b.mark()
            await dbg("KNOCK 2", r"OK knock=2")
            g = await asyncio.to_thread(self.b.wait, r"\[diag\] gesture knock_knock=press", 3, m)
            self.check(bool(g), "BLE KNOCK 2 is a press on the board")
            await asyncio.sleep(0.5)
            st1 = await asyncio.to_thread(self.state)
            self.check(st1["pantry"] == st0["pantry"] - 1, "and a pellet left the pantry (%d -> %d)" % (st0["pantry"], st1["pantry"]))
            await dbg("KNOCK 3", r"ERR 400 BAD_ARGS")
            m = self.b.mark()
            await dbg("SIDE 3000 l", r"OK side=3000 which=l")
            g = await asyncio.to_thread(self.b.wait, r"\[diag\] gesture side=hold", 5, m)
            self.check(bool(g), "BLE SIDE is a hold on the board")
            await dbg("SIDE 3000 up", r"ERR 400 BAD_ARGS")
            await dbg("LOCK", r"OK")

    # ---- the run --------------------------------------------------------------------------

    def run(self, names):
        self.boot()
        for name in names:
            self.say("== %s" % name)
            m = self.b.mark()
            try:
                getattr(self, "sc_" + name)()
                ok = True
            except Fail as e:
                ok = False
                self.say("  scenario failed: %s" % e)
                try:
                    self.b.ask("DEBUG power wake", r"\[power\] now power=full")
                except Fail:
                    pass
            crashed = [l for _, l in self.b.lines[m:] if CRASH.search(l)]
            if crashed or self.b.boots != self.b.expected_boots:
                ok = False
                self.say("  CRASH or unasked boot: %s" % (crashed[:2] or "boot banner"))
                self.b.expected_boots = self.b.boots
            self.results.append((name, ok))
        try:
            self.b.ask("DEBUG power idle default", r"idle_ms=600000")
        except Fail:
            pass
        print()
        for name, ok in self.results:
            print("%-28s %s" % (name, "PASS" if ok else "FAIL"))
        return all(ok for _, ok in self.results)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--only", help="comma-separated: " + ",".join(Run.SCENARIOS))
    ap.add_argument("--no-ble", action="store_true")
    ap.add_argument("--log", default="gesture_e2e.log")
    args = ap.parse_args()
    names = args.only.split(",") if args.only else list(Run.SCENARIOS)
    if args.no_ble:
        names = [n for n in names if n != "ble"]
    for n in names:
        if n not in Run.SCENARIOS:
            ap.error("no scenario %s" % n)
    with open(args.log, "w") as log:
        ok = Run(Board(args.port, log)).run(names)
    print("log: %s" % args.log)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
