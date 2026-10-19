# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""End-to-end soak of fight mode on two real badges over real ESP-NOW.

    uv run tools/duel_soak.py /dev/ttyACM0 /dev/ttyACM1 --log soak.log
    uv run tools/duel_soak.py A B --only clean,hang_wake --rounds 3

No BOOT press is ever sent unless a scenario says so (the cases cover the
button): fight mode opens by itself on an awake pet, the hang (`DEBUG hang`,
which feeds an upside-down IMU reading through the real orientation settle
and HangDetector) is the only gesture, and `DEBUG net` makes the room worse
between the session and the radio (src/hw/net_faults.h).

Each scenario reboots both badges first (RTS), which also clears the
after-fight rest, then checks its own rules. Every scenario also checks the
invariants: a battle both badges finished has ONE outcome hash, no badge
met a battle the other did not, and no badge crashed (a boot banner it was
not asked for).

Ports are opened once and held: on Linux an open pulses DTR/RTS and resets the board.
"""
import argparse
import random
import re
import sys
import threading
import time

import serial

PHASES = ["closed", "lobby", "greeting", "staking", "choosing", "waiting", "revealing", "fainting", "mercy",
          "fallen", "friends"]


class Board:
    def __init__(self, tag, port, t0, log):
        self.tag, self.name, self.t0, self.log = tag, port, t0, log
        self.p = serial.Serial()
        self.p.port, self.p.baudrate, self.p.timeout = port, 115200, 0.1
        self.p.dtr = self.p.rts = False
        self.p.open()
        self.lines = []           # (t, line), the whole run
        self.cv = threading.Condition()
        self.expected_boots = 0
        self.boots = 0
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
                self.log.write("%8.2f %s %s\n" % (t, self.tag, line))
                with self.cv:
                    self.lines.append((t, line))
                    self.cv.notify_all()

    def note(self, s):
        self.log.write("%8.2f %s ## %s\n" % (time.monotonic() - self.t0, self.tag, s))
        self.log.flush()

    def mark(self):
        with self.cv:
            return len(self.lines)

    def send(self, line):
        self.log.write("%8.2f %s >> %s\n" % (time.monotonic() - self.t0, self.tag, line))
        self.p.write(line.encode() + b"\n")

    def reset(self):
        self.expected_boots += 1
        self.note("reset (RTS)")
        self.p.rts = True
        time.sleep(0.1)
        self.p.rts = False

    def since(self, mark):
        with self.cv:
            return [l for _, l in self.lines[mark:]]

    def wait(self, pattern, timeout, since=None, fail=None):
        """The first line matching pattern after `since`, or None. A `fail` match aborts early (returns None)."""
        rx = re.compile(pattern)
        fx = re.compile(fail) if fail else None
        i = self.mark() if since is None else since
        end = time.monotonic() + timeout
        with self.cv:
            while True:
                while i < len(self.lines):
                    line = self.lines[i][1]
                    i += 1
                    if fx and fx.search(line):
                        return None
                    m = rx.search(line)
                    if m:
                        return m
                left = end - time.monotonic()
                if left <= 0:
                    return None
                self.cv.wait(left)


class Fail(Exception):
    pass


class Soak:
    def __init__(self, a, b, log):
        self.A, self.B, self.log = a, b, log
        self.both = (a, b)
        self.results = []

    def say(self, s):
        print(s, flush=True)
        self.log.write("######## %s\n" % s)
        self.log.flush()

    # ---- helpers --------------------------------------------------------------------

    def reboot(self, boards=None):
        """Reboots isolated (`DEBUG reboot quiet`): each badge comes up with its radio blacked
        out from the first tick, so nobody meets anyone until the scenario has set its
        conditions and calls go(). It also clears the after-fight rest."""
        boards = boards or self.both
        marks = {b: b.mark() for b in boards}
        for b in boards:
            b.expected_boots += 1
            b.send("DEBUG reboot quiet")
        for b in boards:
            if not b.wait(r"\[net\] quiet boot", 20, since=marks[b]):
                raise Fail("%s did not come back from a quiet reboot" % b.tag)
            if not b.wait(r"\[hw\] ble adv on", 10, since=marks[b]):
                raise Fail("%s did not finish booting" % b.tag)
        # A pet on three losses in a row wants a cuddle before he fights again (a
        # readiness rule, not a fault): every scenario starts with both cuddled,
        # through the real held/cradle detectors.
        for b in boards:
            b.send("DEBUG cradle 5000")
        time.sleep(6.0)
        for b in boards:
            if any("-> greeting" in l or "-> staking" in l for l in b.since(marks[b])):
                raise Fail("%s paired through a quiet boot" % b.tag)

    def go(self, boards=None):
        """Lifts the isolation; returns the marks from that moment."""
        boards = boards or self.both
        marks = {b: b.mark() for b in self.both}
        for b in boards:
            b.send("DEBUG net blackout 0")   # lifts the quiet boot's deafness; keeps the scenario's faults
        return marks

    def ctx(self, b):
        m = b.mark()
        b.send("DEBUG duel")
        r = b.wait(r"\[duel\] now active=(\d) phase=(\w+) peers=(\d+) ctx=(\d+)", 5, since=m)
        if not r:
            raise Fail("%s does not answer DEBUG duel" % b.tag)
        return int(r.group(1)), r.group(2), int(r.group(3)), int(r.group(4))

    def ensure_creatures(self):
        for b in self.both:
            m = b.mark()
            b.send("DEBUG duel")
            r = b.wait(r"ctx=(\d+)", 5, since=m)
            ctx = int(r.group(1)) if r else -1
            b.note("ctx %d" % ctx)
            if ctx in (14, 15) or ctx < 0 or ctx >= 16:
                continue
            m = b.mark()
            b.send("#1 ADOPT")   # an egg waits for its owner's phone; the cable speaks the same protocol
            if not b.wait(r"#1 (OK|ERR)", 5, since=m):
                raise Fail("%s did not answer ADOPT" % b.tag)
            m = b.mark()
            b.send("DEBUG stage adult")
            if not b.wait(r"debug stage adult: (ok|he is already)", 120, since=m):
                raise Fail("%s would not grow to an adult" % b.tag)

    def wake_with_hang(self, b, timeout=30):
        """The product's own way into fight mode with no button: a hang (it wakes a sleeper too).
        Fight mode opens only when asked (0.7.11), so this asks until he is in it."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            active, phase, _, ctx = self.ctx(b)
            if active:
                return True
            b.send("DEBUG sleepiness 0")   # these scenarios test the controls, not his bedtime (see too_sleepy)
            b.send("DEBUG hang 2500")
            time.sleep(6)
        return False

    def nap_all(self, boards=None, tries=3):
        """Asleep for real (ctx 14, fight mode closed as he dozed), under the 0.9 wake gate."""
        boards = boards or self.both
        for b in boards:
            for _ in range(tries):
                b.send("DEBUG nap")
                b.send("DEBUG sleepiness 60")
                time.sleep(4)
                active, phase, _, ctx = self.ctx(b)
                if ctx == 14 and not active:
                    break
            else:
                raise Fail("%s would not nap (ctx=%d active=%d phase=%s)" % (b.tag, ctx, active, phase))

    def battle(self, timeout=420, during=None, marks=None):
        """Waits for both badges to finish one battle. Returns {tag: hash}. `during` runs in a thread meanwhile."""
        marks = marks or {b: b.mark() for b in self.both}
        stop = threading.Event()
        th = None
        if during:
            th = threading.Thread(target=during, args=(stop,), daemon=True)
            th.start()
        out = {}
        try:
            for b in self.both:
                left = max(1, timeout - 0)
                r = b.wait(r"\[duel\] met side=(\w) ending=([\w ]+?) turns=(\d+) outcome_hash=([0-9a-f]+)", left,
                           since=marks[b])
                if r:
                    out[b.tag] = (r.group(4), r.group(2), int(r.group(3)), r.group(1))
        finally:
            stop.set()
            if th:
                th.join(10)
        return out

    def check_battle(self, met, label):
        if len(met) != 2:
            raise Fail("%s: only %s met (%s)" % (label, sorted(met), met))
        ha, hb = met["A"][0], met["B"][0]
        if ha != hb:
            raise Fail("%s: outcome hashes differ A=%s B=%s" % (label, ha, hb))
        if met["A"][3] == met["B"][3]:
            raise Fail("%s: both badges think they are side %s" % (label, met["A"][3]))
        return "hash=%s %s turns=%d" % (ha, met["A"][1], met["A"][2])

    def crashes(self, marks):
        bad = []
        for b in self.both:
            for l in b.since(marks[b]):
                if re.search(r"Guru Meditation|abort\(\)|Backtrace:|panic|Brownout|stack overflow|WDT|rst:0x(7|8|9|f|10) ", l):
                    bad.append("%s: %s" % (b.tag, l))
        unasked = [b.tag for b in self.both if b.boots > b.expected_boots]
        if unasked:
            bad.append("unexpected reboot on %s" % unasked)
            for b in self.both:
                b.expected_boots = b.boots
        return bad

    def phase_log(self, b, mark):
        return [l for l in b.since(mark) if "[duel] phase" in l]

    # ---- scenarios -----------------------------------------------------------------

    def s_clean(self):
        """Both awake, nobody touches anything: they find each other, stake, fight and agree."""
        self.reboot()
        for b in self.both:
            if not self.wake_with_hang(b):
                raise Fail("%s will not wake" % b.tag)
        t = time.monotonic()
        marks = self.go()
        joined = [b.wait(r"phase \w+ -> greeting|phase lobby -> staking|-> staking", 90, since=marks[b]) for b in self.both]
        if not all(joined):
            raise Fail("no join within 90 s (A=%s B=%s)" % tuple(bool(j) for j in joined))
        tj = time.monotonic() - t
        met = self.battle(marks=marks)
        return "join %.1fs, %s" % (tj, self.check_battle(met, "clean"))

    def s_hang_wake(self):
        """Both asleep: no fight mode. A hang on each (no button) wakes him and the fight happens."""
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
        time.sleep(2)
        for b in self.both:
            b.send("DEBUG nap")
            b.send("DEBUG sleepiness 60")   # asleep, but under the 0.9 wake gate
        time.sleep(4)
        for b in self.both:
            active, phase, _, ctx = self.ctx(b)
            if active and phase not in ("lobby", "closed"):
                raise Fail("%s already in a battle before the nap took (%s)" % (b.tag, phase))
            if ctx != 14:
                raise Fail("%s is not asleep after DEBUG nap (ctx=%d phase=%s)" % (b.tag, ctx, phase))
        # Asleep for a while: nobody must open fight mode by himself.
        marks = self.go()
        time.sleep(10)
        for b in self.both:
            if any("-> staking" in l or "-> greeting" in l for l in b.since(marks[b])):
                raise Fail("%s entered a battle while asleep" % b.tag)
        marks = {b: b.mark() for b in self.both}
        for b in self.both:
            b.send("DEBUG hang 2500")
        woke = []
        for b in self.both:
            r = b.wait(r"fight double=0 hang=1 ctx=\w+ gate=(\S+) result=(\w+)", 12, since=marks[b])
            if not r:
                raise Fail("%s: the hang did not register as a fight ask" % b.tag)
            woke.append("%s:%s/%s" % (b.tag, r.group(1), r.group(2)))
            if r.group(2) == "refused":
                raise Fail("%s: hang refused (%s)" % (b.tag, r.group(1)))
        met = self.battle(marks=marks)
        return "%s, %s" % (" ".join(woke), self.check_battle(met, "hang_wake"))

    def s_swing_through(self):
        """Upside down for 400 ms (a toss, a swing on the way somewhere): never a fight ask."""
        self.reboot()
        b = self.A
        self.wake_with_hang(b)
        time.sleep(8)   # let any wake hang drain
        m = b.mark()
        for _ in range(5):
            b.send("DEBUG hang 400")
            time.sleep(4)
        bad = [l for l in b.since(m) if "hang=1" in l]
        if bad:
            raise Fail("a 400 ms flip asked for a fight: %s" % bad[0])
        return "5 flips of 400 ms, no ask"

    def s_wobbly_hang(self):
        """A small, shaky hand: tremor and jolts while upside down still land the hang."""
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
        time.sleep(2)
        self.nap_all()   # asleep, but under the 0.9 wake gate
        marks = self.go()
        for b in self.both:
            b.send("DEBUG hang 3500 1")
        for b in self.both:
            if not b.wait(r"fight double=0 hang=1 .*result=(entered|waiting)", 14, since=marks[b]):
                raise Fail("%s: a wobbly hang did not wake him into fight mode" % b.tag)
        met = self.battle(marks=marks)
        return self.check_battle(met, "wobbly_hang")

    def s_hang_in_lobby(self):
        """A hang in the lobby leaves it and fight mode stays off; a second hang reopens it; the friend is still found."""
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
        self.B.send("DEBUG nap")   # B out of the air, so A sits in the lobby alone
        self.B.send("DEBUG sleepiness 60")
        time.sleep(5)
        m = self.A.mark()
        active, phase, _, ctx = self.ctx(self.A)
        if phase != "lobby":
            raise Fail("A not in the lobby (phase=%s ctx=%d)" % (phase, ctx))
        self.A.send("DEBUG hang 2500")
        if not self.A.wait(r"phase lobby -> closed", 10, since=m):
            raise Fail("a hang in the lobby did not leave it")
        time.sleep(4)
        active, phase, _, _ = self.ctx(self.A)
        if active:
            raise Fail("fight mode reopened by itself after the lobby hang (phase=%s): it must stay off" % phase)
        if not self.wake_with_hang(self.A):
            raise Fail("a second hang did not reopen fight mode")
        marks = self.go()
        self.B.send("DEBUG hang 2500")
        met = self.battle(marks=marks)
        return "left, stayed off, a hang reopened it, then " + self.check_battle(met, "hang_in_lobby")

    def _hang_storm(self, boards, every=(3, 7)):
        def run(stop):
            while not stop.is_set():
                b = random.choice(boards)
                b.send("DEBUG hang %d %d" % (random.choice([1200, 2000, 3000]), random.randint(0, 1)))
                stop.wait(random.uniform(*every))
        return run

    def s_hangs_in_battle(self):
        """Kids flipping the badges all through the battle: the duel takes the hang, nothing breaks."""
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
        marks = self.go()
        for b in self.both:
            if not b.wait(r"-> staking", 90, since=marks[b]):
                raise Fail("%s never reached staking" % b.tag)
        met = self.battle(marks=marks, during=self._hang_storm(list(self.both)))
        res = self.check_battle(met, "hangs_in_battle")
        for b in self.both:
            ph = self.phase_log(b, marks[b])
            back = [l for l in ph if "-> lobby" in l and "friends -> lobby" not in l]
            if back:
                raise Fail("%s dropped out of the battle on a hang: %s" % (b.tag, back[0]))
        return res

    def s_noise_in_battle(self):
        """Presses, double presses, holds and leans mid-battle (a case that does expose BOOT): still one outcome."""
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
        marks = self.go()

        def run(stop):
            while not stop.is_set():
                b = random.choice(self.both)
                b.send(random.choice(["DEBUG press 120", "DEBUG press 1500", "DEBUG double",
                                      "DEBUG lean %d 2500" % random.randint(0, 3), "DEBUG hang 2000"]))
                stop.wait(random.uniform(1.5, 4))
        met = self.battle(marks=marks, during=run)
        return self.check_battle(met, "noise_in_battle")

    def s_hang_on_friends(self):
        """After a battle, a hang on the friends screen goes back to searching."""
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
        marks = self.go()
        met = self.battle(marks=marks)
        res = self.check_battle(met, "hang_on_friends")
        if not self.A.wait(r"-> friends", 30, since=marks[self.A]):
            raise Fail("A never showed the friends screen")
        m = self.A.mark()
        self.A.send("DEBUG hang 2500")
        if not self.A.wait(r"phase friends -> (lobby|closed)", 12, since=m):
            raise Fail("a hang on the friends screen did not leave it")
        return res + ", hang left friends"

    def _lossy(self, rx=0, tx=0, dup=0, delay=0, jitter=0, label=""):
        self.reboot()
        for b in self.both:
            b.send("DEBUG net rx %d" % rx)
            b.send("DEBUG net tx %d" % tx)
            b.send("DEBUG net dup %d" % dup)
            b.send("DEBUG net delay %d %d" % (delay, jitter))
        for b in self.both:
            self.wake_with_hang(b)
        marks = self.go()
        met = self.battle(marks=marks, timeout=600)
        counts = []
        for b in self.both:
            m = b.mark()
            b.send("DEBUG net")
            r = b.wait(r"\[net\] cfg .*", 5, since=m)
            counts.append(r.group(0).split("| ")[1] if r else "?")
            b.send("DEBUG net clear")
        if len(met) == 1:
            raise Fail("%s: one-sided meeting %s" % (label, met))
        if not met:
            return None, counts
        return self.check_battle(met, label), counts

    def s_loss_20(self):
        """20% of frames lost each way."""
        r, c = self._lossy(rx=10, tx=10, label="loss_20")
        if not r:
            raise Fail("no battle at 20%% loss; %s" % c)
        return r + " | " + " || ".join(c)

    def s_loss_50(self):
        """About half the frames lost each way."""
        r, c = self._lossy(rx=30, tx=30, label="loss_50")
        if not r:
            raise Fail("no battle at ~50%% loss; %s" % c)
        return r + " | " + " || ".join(c)

    def s_loss_80(self):
        """A terrible room (~80% lost): a battle may not happen, but never a split or one-sided one."""
        r, c = self._lossy(rx=55, tx=55, label="loss_80")
        return (r or "no battle (allowed), no split") + " | " + " || ".join(c)

    def s_dup_reorder(self):
        """Doubled frames, 150-650 ms late and reordered."""
        r, c = self._lossy(dup=40, delay=150, jitter=500, label="dup_reorder")
        if not r:
            raise Fail("no battle with dup/reorder; %s" % c)
        return r + " | " + " || ".join(c)

    def _midbattle(self, label, act, expect_met=True, wait_phase=r"-> choosing"):
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
        marks = self.go()
        for b in self.both:
            if not b.wait(wait_phase, 120, since=marks[b]):
                raise Fail("%s: %s never reached %s" % (label, b.tag, wait_phase))
        time.sleep(random.uniform(2, 12))
        act()
        met = self.battle(marks=marks, timeout=600)
        if expect_met:
            return self.check_battle(met, label)
        if len(met) == 1:
            raise Fail("%s: one-sided meeting %s" % (label, met))
        if len(met) == 2:
            return self.check_battle(met, label)
        return None

    def s_blackout_short(self):
        """Out of range for 6 s mid-battle (both sides deaf and mute): the battle resumes and agrees."""
        def act():
            for b in self.both:
                b.send("DEBUG net blackout 6000")
        return self._midbattle("blackout_short", act)

    def s_blackout_one_way(self):
        """A one-way link for 10 s: A hears nothing, B still hears A."""
        def act():
            self.A.send("DEBUG net rx 100")
            time.sleep(10)
            self.A.send("DEBUG net rx 0")
        return self._midbattle("one_way", act)

    def s_blackout_long(self):
        """Out of range 30 s mid-battle (past the 20 s wander-off): both end cleanly, then find each other again."""
        marks0 = {}

        def act():
            for b in self.both:
                marks0[b] = b.mark()
                b.send("DEBUG net blackout 30000")
        r = self._midbattle("blackout_long", act, expect_met=False)
        ended = [b.tag for b in self.both if b.wait(r"-> (lobby|closed)", 5, since=marks0[b])]
        if r:
            return "finished anyway before the timeout: " + r
        if len(ended) != 2:
            raise Fail("not both badges ended the battle (%s)" % ended)
        # And they find each other again (after the after-fight rest, if any).
        marks = {b: b.mark() for b in self.both}
        met = self.battle(marks=marks, timeout=480)
        return "both wandered off cleanly; rematch " + self.check_battle(met, "blackout_long_rematch")

    def s_reset_midbattle(self):
        """A badge loses power mid-battle: the other ends cleanly (a wander-off draw at most, never a
        win or a wound); after the reboot they fight again."""
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
        marks = self.go()
        for b in self.both:
            if not b.wait(r"-> choosing", 120, since=marks[b]):
                raise Fail("%s never reached choosing" % b.tag)
        time.sleep(random.uniform(2, 12))
        mb = self.B.mark()
        self.A.reset()
        left = self.B.wait(r"phase (\w+) -> (lobby|closed) end=(\d+)", 40, since=mb)
        if not left:
            raise Fail("B never left the battle after A lost power")
        met = self.B.wait(r"\[duel\] met side=(\w) ending=([\w ]+?) turns=(\d+)", 3, since=mb)
        if met and met.group(2) != "draw":
            raise Fail("B recorded a %s against a badge that lost power" % met.group(2))
        if self.A.wait(r"\[duel\] met ", 1, since=mb):
            raise Fail("A recorded a meeting across its own reset")
        self.A.wait(r"\[hw\] ble adv on", 15, since=mb)
        marks = {b: b.mark() for b in self.both}
        self.wake_with_hang(self.A)
        if not self.wake_with_hang(self.B):
            raise Fail("B would not go back into fight mode")
        met2 = self.battle(marks=marks, timeout=480)
        return "B left cleanly (end=%s%s); rematch %s" % (left.group(3), ", a draw" if met else "",
                                                           self.check_battle(met2, "reset_rematch"))

    def _ghosts(self, n, mode, side, label, rx=0):
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
            b.send("DEBUG net ghosts %d %d %s" % (n, mode, side))
            if rx:
                b.send("DEBUG net rx %d" % rx)
                b.send("DEBUG net tx %d" % rx)
        t = time.monotonic()
        marks = self.go()
        joined = [b.wait(r"-> staking", 120, since=marks[b]) for b in self.both]
        tj = time.monotonic() - t
        counts = []
        for b in self.both:
            m = b.mark()
            b.send("DEBUG net")
            r = b.wait(r"ghost_beacons=(\d+)", 5, since=m)
            counts.append("%s ghost_beacons=%s" % (b.tag, r.group(1) if r else "?"))
        if not all(joined):
            raise Fail("%s: A and B never paired in 120 s with %d ghosts (%s)" % (label, n, ", ".join(counts)))
        met = self.battle(marks=marks, timeout=600)
        for b in self.both:
            b.send("DEBUG net clear")
        return "paired in %.1fs past %d ghosts; %s | %s" % (tj, n, self.check_battle(met, label), ", ".join(counts))

    def s_ghosts_deaf(self):
        """Three badges in the room everyone hears but who hear nobody, all with lower MACs."""
        return self._ghosts(3, 0, "low", "ghosts_deaf")

    def s_ghosts_phantom(self):
        """Three phantoms who claim me but whose greetings never arrive, all with lower MACs."""
        return self._ghosts(3, 1, "low", "ghosts_phantom")

    def s_ghosts_paired(self):
        """Six badges busy with each other (claims on each other), lower MACs: never a partner."""
        return self._ghosts(6, 2, "low", "ghosts_paired")

    def s_ghosts_storm(self):
        """Six deaf ghosts with higher MACs, and a third of every frame lost each way."""
        return self._ghosts(6, 0, "high", "ghosts_storm", rx=20)

    def s_too_sleepy(self):
        """Documents the wake gate: asleep and sleepiness >= 0.9, a hang is refused and he sleeps on."""
        self.reboot()
        self.wake_with_hang(self.A)
        self.nap_all([self.A])
        self.A.send("DEBUG sleepiness 100")
        time.sleep(1)
        m = self.A.mark()
        self.A.send("DEBUG hang 2500")
        r = self.A.wait(r"fight double=0 hang=1 ctx=asleep gate=(\S+) result=(\w+)", 10, since=m)
        if not r:
            raise Fail("the hang was not heard at all")
        if r.group(2) != "refused":
            raise Fail("a too-sleepy pet was woken (%s/%s): the gate changed" % (r.group(1), r.group(2)))
        self.A.send("DEBUG sleepiness 0")
        return "refused as designed: gate=%s" % r.group(1)

    def s_legacy_double(self):
        """A case with BOOT exposed: the double press still wakes and opens fight mode."""
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
        time.sleep(2)
        self.nap_all()   # asleep, but under the 0.9 wake gate
        marks = self.go()
        for b in self.both:
            b.send("DEBUG double")
        for b in self.both:
            if not b.wait(r"fight double=1 hang=0 .*result=(entered|waiting)", 10, since=marks[b]):
                raise Fail("%s: the double press did not open fight mode" % b.tag)
        return self.check_battle(self.battle(marks=marks), "legacy_double")

    def s_back_to_back(self):
        """Two battles in a row with no reboot: rest, then they find each other again by themselves."""
        self.reboot()
        for b in self.both:
            self.wake_with_hang(b)
        marks = self.go()
        r1 = self.check_battle(self.battle(marks=marks), "b2b_1")
        marks = {b: b.mark() for b in self.both}
        r2 = self.check_battle(self.battle(marks=marks, timeout=600), "b2b_2")
        if r1.split()[0] == r2.split()[0]:
            raise Fail("the second battle replayed the first (%s)" % r1)
        return "%s ; then %s" % (r1, r2)


SCENARIOS = ["clean", "hang_wake", "swing_through", "wobbly_hang", "hang_in_lobby", "hangs_in_battle",
             "noise_in_battle", "hang_on_friends", "loss_20", "loss_50", "loss_80", "dup_reorder",
             "blackout_short", "blackout_one_way", "blackout_long", "reset_midbattle", "too_sleepy", "legacy_double", "ghosts_deaf", "ghosts_phantom", "ghosts_paired", "ghosts_storm",
             "back_to_back"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ports", nargs=2)
    ap.add_argument("--log", default="soak.log")
    ap.add_argument("--only", default="")
    ap.add_argument("--rounds", type=int, default=1)
    ap.add_argument("--shuffle", action="store_true")
    ap.add_argument("--seed", type=int, default=None)
    args = ap.parse_args()
    seed = args.seed if args.seed is not None else int(time.time())
    random.seed(seed)

    log = open(args.log, "a", buffering=1)
    t0 = time.monotonic()
    A, B = Board("A", args.ports[0], t0, log), Board("B", args.ports[1], t0, log)
    s = Soak(A, B, log)
    s.say("soak seed=%d ports A=%s B=%s" % (seed, *args.ports))
    names = [n for n in (args.only.split(",") if args.only else SCENARIOS) if n]
    for b in s.both:   # an open on Linux may have reset it: let that boot finish before talking
        if b.wait(r"\[hw\] ble adv on", 4, since=0):
            b.expected_boots = b.boots
    time.sleep(0.5)
    for b in s.both:
        b.expected_boots = b.boots
    s.reboot()
    s.ensure_creatures()
    passed = failed = 0
    for rnd in range(args.rounds):
        order = list(names)
        if args.shuffle:
            random.shuffle(order)
        for n in order:
            marks = {b: b.mark() for b in s.both}
            t = time.monotonic()
            try:
                res = getattr(s, "s_" + n)()
                bad = s.crashes(marks)
                if bad:
                    raise Fail("crash: %s" % bad[:3])
                passed += 1
                s.say("PASS r%d %-16s %5.0fs  %s" % (rnd, n, time.monotonic() - t, res))
            except Fail as e:
                failed += 1
                s.crashes(marks)
                s.say("FAIL r%d %-16s %5.0fs  %s" % (rnd, n, time.monotonic() - t, e))
    s.say("TOTAL passed=%d failed=%d" % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
