# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial", "bleak>=0.22"]
# ///
"""End-to-end proof of low power on one real badge, over its cable and Bluetooth.

    uv run tools/power_e2e.py /dev/ttyACM0 --log power.log
    uv run tools/power_e2e.py /dev/ttyACM0 --only idle,button --no-ble
    uv run tools/power_e2e.py /dev/ttyACM0 --only ten_minutes      # the real default, 10.5 min
    uv run tools/power_e2e.py /dev/ttyACM0 --only hand             # you pick the badge up

Nothing here touches the pet's save except what a scenario says (a nap, a
press the pet feels). The cable's DEBUG lines are never user input, so the
script can ask `DEBUG power` as often as it likes without waking the badge.
Moves go through the real IMU path: `DEBUG hang` and `DEBUG cradle` replace
the raw accelerometer sample the sense task reads, so the same smoothing
and thresholds a hand meets decide (src/hw/power.h).

Each scenario checks, from the board's own log:
  - asleep: `[power] power=low`, cpu=80, and the frame counter stops;
  - the pet lives on while it is dark (his age in ticks keeps rising);
  - awake: `[power] power=full why=<cause>`, cpu=240, and frames run again;
  - no crash (a boot banner nobody asked for, a panic, a watchdog).

The Bluetooth scenarios use the debug characteristic (docs/debug-ble.md):
POWER, the protocol's own lines, the unlock (answered by the cable's DEBUG
press, which is a hand on the dish), PRESS, HANG, DUEL, NAP, NET, REBOOT.

The port is opened once and held: on Linux an open resets the board.
"""
import argparse
import asyncio
import re
import sys
import threading
import time

import serial

NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
DEBUG = "6e400005-b5a3-f393-e0a9-e50e24dcca9e"

IDLE_S = 6          # the short timeout the scenarios set (POWER idle)
CRASH = re.compile(r"Guru Meditation|abort\(\)|Backtrace:|panic|Brownout|stack overflow|WDT|rst:0x(7|8|9|f|10) ")


class Fail(Exception):
    pass


class Board:
    def __init__(self, port, log):
        self.t0, self.log = time.monotonic(), log
        self.p = serial.Serial()
        self.p.port, self.p.baudrate, self.p.timeout = port, 115200, 0.1
        self.p.dtr = self.p.rts = False
        self.p.open()
        self.lines = []
        self.cv = threading.Condition()
        self.boots = self.expected_boots = 0
        self.crashes = []
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        buf = b""
        while True:
            try:
                buf += self.p.read(4096)
            except serial.SerialException as e:
                self.note("serial error %s" % e)
                time.sleep(0.5)
                continue
            *whole, buf = buf.split(b"\n")
            for raw in whole:
                line = raw.decode("utf-8", "replace").rstrip("\r")
                t = time.monotonic() - self.t0
                if "] blorbarium " in line and " fw=" in line:
                    self.boots += 1
                if CRASH.search(line):
                    self.crashes.append(line)
                self.log.write("%8.2f    %s\n" % (t, line))
                self.log.flush()
                with self.cv:
                    self.lines.append((t, line))
                    self.cv.notify_all()

    def note(self, s):
        self.log.write("%8.2f ## %s\n" % (time.monotonic() - self.t0, s))
        self.log.flush()

    def mark(self):
        with self.cv:
            return len(self.lines)

    def send(self, line):
        self.log.write("%8.2f >> %s\n" % (time.monotonic() - self.t0, line))
        self.p.write(line.encode() + b"\n")

    def wait(self, pattern, timeout, since=None):
        rx = re.compile(pattern)
        i = self.mark() if since is None else since
        end = time.monotonic() + timeout
        with self.cv:
            while True:
                while i < len(self.lines):
                    t, line = self.lines[i]
                    i += 1
                    m = rx.search(line)
                    if m:
                        return m
                left = end - time.monotonic()
                if left <= 0:
                    return None
                self.cv.wait(left)

    def ask(self, line, pattern, timeout=3):
        m = self.mark()
        self.send(line)
        r = self.wait(pattern, timeout, since=m)
        if not r:
            raise Fail("no %r after %r" % (pattern, line))
        return r

    def power(self):
        """The badge's power state now, as a dict, from `DEBUG power`."""
        r = self.ask("DEBUG power", r"\[power\] (now|refused \w+) (power=.*)")
        return dict(kv.split("=", 1) for kv in r.group(2).split())

    def age(self):
        """His age in ticks, from the next 10 s status line."""
        r = self.wait(r"\[hw\] up=\d+ .* age=(\d+) .* power=(\w+) cpu=(\d+) frames=(\d+)", 12)
        if not r:
            raise Fail("no status line in 12 s")
        return int(r.group(1)), r.group(2), int(r.group(3)), int(r.group(4))


class Run:
    def __init__(self, board, args):
        self.b, self.args = board, args
        self.results = []
        self.ble_name = None

    def say(self, s):
        print(s, flush=True)
        self.b.note(s)

    def check(self, ok, what):
        self.say(("  ok   " if ok else "  FAIL ") + what)
        if not ok:
            raise Fail(what)

    # ---- helpers ------------------------------------------------------------------------

    def boot(self):
        self.say("waiting for the boot (opening the port reset it)")
        r = self.b.wait(r"\[hw\] ble name=(\S+)", 15, since=0)
        if not r:   # the open did not reset it: reset it on purpose
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

    def awake_with_idle(self, seconds):
        """Awake, with a short idle timeout, so each scenario starts from the same place."""
        self.b.ask("DEBUG power wake", r"\[power\] now power=full")
        self.b.ask("DEBUG power idle %d" % seconds, r"\[power\] now power=full .*idle_ms=%d" % (seconds * 1000))

    def wait_low(self, why, timeout):
        m = self.b.wait(r"\[power\] power=low why=(\w+)", timeout)
        self.check(bool(m) and m.group(1) == why, "went low, why=%s (got %s)" % (why, m and m.group(1)))
        return m

    def wait_full(self, whys, timeout=3):
        m = self.b.wait(r"\[power\] power=full why=(\w+)", timeout)
        self.check(bool(m) and m.group(1) in whys, "woke, why in %s (got %s)" % (whys, m and m.group(1)))
        return m

    def dark_and_living(self):
        """Asleep: cpu 80, frames frozen, and his ticks still run."""
        p1 = self.b.power()
        self.check(p1["power"] == "low" and p1["cpu"] == "80", "low at 80 MHz (%s, cpu=%s)" % (p1["power"], p1["cpu"]))
        a1, _, _, f1 = self.b.age()
        a2, pw, cpu, f2 = self.b.age()
        self.check(pw == "low" and f2 == f1, "no frame drawn while dark (frames %d -> %d)" % (f1, f2))
        self.check(a2 - a1 >= 80, "the pet lived on while dark (age %d -> %d ticks)" % (a1, a2))

    def lit(self):
        p1 = self.b.power()
        time.sleep(1.5)
        p2 = self.b.power()
        self.check(p2["power"] == "full" and p2["cpu"] == "240", "full at 240 MHz (cpu=%s)" % p2["cpu"])
        self.check(int(p2["frames"]) > int(p1["frames"]), "frames run again (%s -> %s)" % (p1["frames"], p2["frames"]))

    # ---- scenarios over the cable ---------------------------------------------------------

    def sc_idle(self):
        """Still for the timeout: it goes low by itself, stays dark and the pet lives on."""
        self.awake_with_idle(IDLE_S)
        t = time.monotonic()
        self.wait_low("idle", IDLE_S + 4)
        took = time.monotonic() - t
        self.check(IDLE_S - 1 <= took <= IDLE_S + 3, "after %.1f s of nothing (timeout %d s)" % (took, IDLE_S))
        self.dark_and_living()
        self.b.ask("DEBUG power wake", r"\[power\] now power=full")

    def sc_button(self):
        """A press wakes it, and only lights it: the pet does not feel that press."""
        self.awake_with_idle(IDLE_S)
        self.wait_low("idle", IDLE_S + 4)
        m = self.b.mark()
        self.b.send("DEBUG press 200")
        self.wait_full({"button"})
        fed = self.b.wait(r"\[diag\] (boot|press)", 2, since=m)
        self.check(not fed, "the waking press did nothing else (%s)" % (fed and fed.group(0)))
        self.lit()
        self.b.ask("DEBUG power idle 20", r"idle_ms=20000")
        time.sleep(4)
        m = self.b.mark()
        self.b.ask("DEBUG press 200", r"debug press 200")   # awake, a press is input and restarts the timeout
        time.sleep(0.5)
        p = self.b.power()
        self.check(int(p["left_ms"]) > 18000, "a press restarts the timeout (left %s of 20000 ms)" % p["left_ms"])
        fed = self.b.wait(r"\[diag\] boot press", 2, since=m)
        self.check(bool(fed), "awake, the pet feels a press (%s)" % (fed and fed.group(0)))

    def sc_drift(self):
        """Turned over (the raw IMU sample upside down): the move wakes it."""
        self.awake_with_idle(IDLE_S)
        self.wait_low("idle", IDLE_S + 4)
        self.b.send("DEBUG hang 1500")
        self.wait_full({"drift", "jolt"})
        self.lit()

    def sc_cradle(self):
        """Picked up and held still in a hand (tipped back about 27 degrees): it wakes."""
        self.awake_with_idle(IDLE_S)
        self.wait_low("idle", IDLE_S + 4)
        self.b.send("DEBUG cradle 3000")
        self.wait_full({"drift", "jolt"})

    def sc_still_stays_dark(self):
        """Left where it is, nothing wakes it: 25 s of the real sensor's noise and drift."""
        self.awake_with_idle(IDLE_S)
        self.wait_low("idle", IDLE_S + 4)
        m = self.b.wait(r"\[power\] power=full", 25)
        self.check(not m, "no wake in 25 s untouched%s" % (" (woke: %s)" % m.group(0) if m else ""))

    def sc_asked(self):
        """`DEBUG power low` sleeps it now; a move still wakes it."""
        self.awake_with_idle(600)
        self.b.ask("DEBUG power low", r"\[power\] now power=low why=asked")
        self.dark_and_living()
        self.b.send("DEBUG hang 1500")
        self.wait_full({"drift", "jolt"})

    def fight(self):
        r = self.b.ask("DEBUG duel", r"\[duel\] now active=(\d)")
        return r.group(1) == "1"

    def sc_hold(self):
        """Fight mode holds it awake past the timeout; leaving fight mode lets it sleep."""
        self.awake_with_idle(IDLE_S)
        if not self.fight():
            # Rested, so a double press may wake him for a fight (a sleepy pet refuses one).
            self.b.ask("DEBUG sleepiness 0", r"debug sleepiness 0%")
            m = self.b.mark()
            self.b.send("DEBUG double")
            r = self.b.wait(r"\[diag\] fight .*result=(\w+)", 4, since=m)
            if not self.fight():
                raise Fail("fight mode would not open (%s)" % (r and r.group(0)))
        low = self.b.wait(r"\[power\] power=low", IDLE_S + 6)
        self.check(not low, "no sleep while fight mode is on (6 s past the timeout)")
        p = self.b.power()
        self.check(p["held"] == "1", "power says held=1")
        r = self.b.ask("DEBUG power low", r"\[power\] (now|refused \w+)")
        self.check(r.group(1).startswith("refused"), "asked to sleep while held: refused")
        # Out of fight mode the sure way: a reboot (fight mode is RAM only).
        self.b.expected_boots += 1
        m = self.b.mark()
        self.b.send("DEBUG reboot")
        self.check(bool(self.b.wait(r"\[hw\] ble adv on", 15, since=m)), "rebooted out of fight mode")
        self.check(not self.fight(), "fight mode is off")
        self.b.ask("DEBUG power idle %d" % IDLE_S, r"idle_ms=%d" % (IDLE_S * 1000))
        self.wait_low("idle", IDLE_S + 4)
        self.b.ask("DEBUG power wake", r"\[power\] now power=full")

    def sc_ten_minutes(self):
        """The real default: untouched for 10 minutes, it goes low, not a moment before."""
        self.b.ask("DEBUG power wake", r"\[power\] now power=full")
        self.b.ask("DEBUG power idle default", r"idle_ms=600000")
        t = time.monotonic()
        early = self.b.wait(r"\[power\] power=low", 590)
        self.check(not early, "still awake at 9 min 50 s")
        self.wait_low("idle", 20)
        took = time.monotonic() - t
        self.check(595 <= took <= 610, "low after %.0f s" % took)
        self.dark_and_living()
        self.b.send("DEBUG press 200")
        self.wait_full({"button"})

    def sc_hand(self):
        """A person: with the badge asleep, pick it up when asked."""
        self.awake_with_idle(IDLE_S)
        self.wait_low("idle", IDLE_S + 4)
        print("\n  >>> The badge is dark. Pick it up (do not press anything) within 60 s.\n", flush=True)
        self.wait_full({"drift", "jolt", "tap"}, timeout=60)
        print("  >>> Now put it down and leave it.\n", flush=True)
        time.sleep(3)
        self.awake_with_idle(IDLE_S)
        self.wait_low("idle", IDLE_S + 10)

    # ---- over Bluetooth -------------------------------------------------------------------

    def sc_ble(self):
        asyncio.run(self._ble())

    async def _ble(self):
        from bleak import BleakClient, BleakScanner

        self.say("  scanning for %s" % self.ble_name)
        dev = await BleakScanner.find_device_by_name(self.ble_name, timeout=20)
        if not dev:
            raise Fail("no %s advertising" % self.ble_name)
        events, lines = [], {}
        buf = {"d": b"", "t": b""}

        def on(key):
            def cb(_, data):
                buf[key] += bytes(data)
                *whole, buf[key] = buf[key].split(b"\n")
                for raw in whole:
                    s = raw.decode("utf-8", "replace")
                    self.b.note("ble %s << %s" % (key, s))
                    if s.startswith("! "):
                        events.append(s)
                    elif m := re.match(r"#(\w+) (OK|ERR)", s):
                        lines[m.group(1)] = s
            return cb

        # A sleeping badge wakes when a phone connects.
        self.b.ask("DEBUG power idle %d" % IDLE_S, r"idle_ms=%d" % (IDLE_S * 1000))
        self.wait_low("idle", IDLE_S + 4)
        m_conn = self.b.mark()
        async with BleakClient(dev, timeout=20) as c:
            woke = await asyncio.to_thread(self.b.wait, r"\[power\] power=full why=(\w+)", 5, m_conn)
            self.check(bool(woke) and woke.group(1) == "phone", "a phone connecting wakes it (why=%s)" % (woke and woke.group(1)))
            await c.start_notify(DEBUG, on("d"))
            await c.start_notify(NUS_TX, on("t"))
            ids = iter(range(1, 1 << 20))

            async def rx(line):
                self.b.note("ble rx >> %s" % line)
                await c.write_gatt_char(NUS_RX, (line + "\n").encode(), response=True)

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

            async def serial_wait(pattern, timeout, since=None):
                return await asyncio.to_thread(self.b.wait, pattern, timeout, since)

            await rx("HELLO")   # a page speaks the protocol at once, or it is dropped as a squatter
            await dbg("POWER wake", r"OK power=full")
            await dbg("POWER idle %d" % IDLE_S, r"OK power=full .*idle_ms=%d" % (IDLE_S * 1000))

            # Asked to sleep over the air: the debug link's own lines do not wake it.
            m = self.b.mark()
            await dbg("POWER low", r"OK power=low why=asked .*cpu=80")
            self.check(bool(self.b.wait(r"\[power\] power=low why=asked", 2, since=m)), "the cable log agrees: low")
            await asyncio.sleep(2)
            await dbg("POWER", r"OK power=low")
            await dbg("STATUS", r"OK unlock=")
            await rx("STATE")   # a protocol line: input, but the page that asked for low power must not undo it
            await asyncio.sleep(0.5)
            await dbg("POWER", r"OK power=low why=asked")
            n, m_wake = len(events), self.b.mark()
            await dbg("POWER wake", r"OK power=full why=asked .*cpu=240")

            # Idle timeout while connected, with the event on the debug link.
            low = await serial_wait(r"\[power\] power=low why=idle", IDLE_S + 4, m_wake)
            self.check(bool(low), "connected but idle: low by itself")
            await asyncio.sleep(0.5)
            self.check(any("power=low why=idle" in e for e in events[n:]), "! DEBUG power=low event on the debug link")
            # A protocol line from the phone wakes an idle badge.
            m = self.b.mark()
            await rx("STATE")
            self.check(bool(await serial_wait(r"\[power\] power=full why=phone", 3, m)), "a phone's line wakes it (why=phone)")

            # The device's verbs: the harmful ones need the unlock, a press on the dish.
            await dbg("PRESS", r"ERR 423 LOCKED")
            await dbg("UNLOCK", r"OK unlock=asking")
            await asyncio.sleep(1.7)
            self.b.send("DEBUG press 150")   # the cable is a hand on the dish
            end = time.monotonic() + 4
            while not any("unlock=open" in e for e in events) and time.monotonic() < end:
                await asyncio.sleep(0.1)
            self.check(any("unlock=open" in e for e in events), "unlocked by a press on the dish")

            # PRESS wakes it over the air, and HANG moves it.
            await dbg("POWER low", r"OK power=low")
            m = self.b.mark()
            await dbg("PRESS 200", r"OK press=200")
            self.check(bool(await serial_wait(r"\[power\] power=full why=button", 3, m)), "BLE PRESS wakes it")
            await dbg("POWER low", r"OK power=low")
            m = self.b.mark()
            await dbg("HANG 1500", r"OK hang=1500")
            self.check(bool(await serial_wait(r"\[power\] power=full why=(drift|jolt)", 3, m)), "BLE HANG wakes it")
            await dbg("CRADLE 1000", r"OK cradle=1000")
            await dbg("LEAN 1 500", r"OK lean=1 ms=500")
            await dbg("DUEL", r"OK fight=\w+ active=\d phase=\w+")
            await dbg("NET", r"OK rx_drop=0 ")
            await dbg("NET rx 30", r"OK rx_drop=30 ")
            await dbg("NET clear", r"OK rx_drop=0 ")
            await dbg("NET bogus", r"ERR 400 BAD_ARGS")
            await dbg("NAP", r"OK nap=1|ERR 409 NO_CREATURE")
            await dbg("POWER idle default", r"OK power=full .*idle_ms=600000")
            # Last: a reboot over the air, which saves first.
            boots, m_boot = self.b.boots, self.b.mark()
            await dbg("REBOOT", r"OK rebooting quiet=0")
            try:
                await c.disconnect()
            except Exception:
                pass
        self.b.expected_boots += 1
        up = self.b.wait(r"\[hw\] boot=Resumed", 15, since=m_boot)
        self.check(bool(up) and self.b.boots == boots + 1, "BLE REBOOT restarted it, and the pet resumed")
        self.b.wait(r"\[hw\] ble adv on", 5)

    SCENARIOS = ["idle", "button", "drift", "cradle", "still_stays_dark", "asked", "hold", "ble"]
    EXTRA = ["ten_minutes", "hand"]

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
            crashed = [l for _, l in self.b.lines[m:] if CRASH.search(l)]
            if crashed or self.b.boots != self.b.expected_boots:
                ok = False
                self.say("  CRASH or unasked boot: %s" % (crashed[:2] or "boot banner"))
                self.b.expected_boots = self.b.boots
            self.results.append((name, ok))
        print()
        for name, ok in self.results:
            print("%-18s %s" % (name, "PASS" if ok else "FAIL"))
        return all(ok for _, ok in self.results)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--only", help="comma-separated: " + ",".join(Run.SCENARIOS + Run.EXTRA))
    ap.add_argument("--no-ble", action="store_true")
    ap.add_argument("--log", default="power_e2e.log")
    args = ap.parse_args()
    names = args.only.split(",") if args.only else list(Run.SCENARIOS)
    if args.no_ble:
        names = [n for n in names if n != "ble"]
    for n in names:
        if n not in Run.SCENARIOS + Run.EXTRA:
            ap.error("no scenario %s" % n)
    with open(args.log, "w") as log:
        ok = Run(Board(args.port, log), args).run(names)
    print("log: %s" % args.log)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
