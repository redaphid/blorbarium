"""Two simulators linked by a fake radio, filmed side by side.

    tools/duel_video.sh [duel ...]       (builds the sim, then runs this in WSL survivor)
    python3 tools/duel_video.py --out DIR [--twice] [duel ...]

A duel is tools/duels/<name>.txt: one script for two sims. A line goes to
both unless it starts `A:` or `B:`. `#>` lines are for this tool, times on
the script's clock in ms:

    #> title Ping                      the film's name on screen
    #> length 12000                    how long to record
    #> side A mossby 02:b1:0b:00:00:01 duel=scrap:3:spare:hug
    #> side B tinker 3c:84:27:aa:10:5e   species, MAC, and how its kid duels (sim --duel)
    #> weather loss=30,dup=10,reorder=20,late=600,seed=4
    #> caption 0 Two dishes | one fake radio    " | " breaks the line

The sims are joined by a socketpair (sim --air-fd, sim/serial_air.h), each on
its fixed clock in lockstep, so a duel film is the same frames every run;
--twice records it twice and fails if it is not. Every frame that crosses is
drawn flying between the discs: lost ones burst halfway, late ones fly
slower, doubles fly twice. Under each disc is what its radio and its kid did;
once a `!duel` opens (D8's session, sim/duel_bot.h), the disc is the duel
screen, and under it are the kid's moves and each turn as it reveals; at the
end the line under the title shows the outcome hash each side's book holds.

Writes <duel>-wide.mp4 (1920x1080), <duel>-square.mp4 (1080x1080) and
check/<duel>-*.png (the middle of each caption, and a contact sheet) to --out.
"""
import argparse
import glob
import hashlib
import math
import os
import socket
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "reel"))
import make_reel as mr  # noqa: E402
import sim_video as sv  # noqa: E402

DUELS = os.path.join(HERE, "duels")
FPS = sv.FPS
FLY_MS = 500             # the radio takes 3 ms; a frame flies this long so an eye can follow it
DUP_LAG_MS = 160         # a double flies this far behind its twin
BURST_MS = 400
TICKER_MS = 4000
RED = (255, 90, 80)
GREY = (110, 116, 124)
DIM_INK = (180, 184, 190)


@dataclass
class Side:
    species: str = "grungo"
    mac: str = ""
    duel: str = ""
    lines: list = field(default_factory=list)


@dataclass
class Duel:
    name: str
    title: str = ""
    length: int = 10000
    weather: str = ""
    captions: list = field(default_factory=list)
    sides: dict = field(default_factory=lambda: {"A": Side(), "B": Side()})

    def caption_at(self, ms):
        text = ""
        for at, t in self.captions:
            if at <= ms: text = t
        return text


def load(path):
    duel = Duel(os.path.splitext(os.path.basename(path))[0])
    duel.title = duel.name
    with open(path) as f:
        for raw in f:
            line = raw.strip()
            if line.startswith("#>"):
                key, _, rest = line[2:].strip().partition(" ")
                if key == "title": duel.title = rest
                elif key == "length": duel.length = int(rest)
                elif key == "weather": duel.weather = rest
                elif key == "caption":
                    at, _, text = rest.partition(" ")
                    duel.captions.append((int(at), text))
                elif key == "side":
                    who, species, mac, *opts = rest.split()
                    side = duel.sides[who]
                    side.species, side.mac = species, mac
                    for o in opts:
                        k, _, v = o.partition("=")
                        if k != "duel": raise SystemExit("%s: side options are duel=..., not %s" % (path, o))
                        side.duel = v
                else: raise SystemExit("%s: unknown #> %s" % (path, key))
            elif not line or line.startswith("#"):
                continue
            elif line[:2] in ("A:", "B:"):
                duel.sides[line[0]].lines.append(line[2:].strip())
            else:
                for side in duel.sides.values(): side.lines.append(line)
    duel.captions.sort()
    if duel.sides["A"].mac == duel.sides["B"].mac: raise SystemExit("%s: the two sides need their own MACs" % path)
    return duel


@dataclass
class Event:
    ms: int
    kind: str          # sent, received, lost, deaf, kid, duel
    seq: int = 0
    peer: str = ""
    text: str = ""


def read_log(path):
    events = []
    with open(path) as f:
        for line in f:
            w = line.rstrip("\n").split(" ", 6)
            if w[0] == "air" and len(w) >= 6:
                events.append(Event(int(w[1]), w[2], int(w[3]), w[4], w[6] if len(w) > 6 else ""))
            elif w[0] in ("kid", "duel") and len(w) >= 3:
                events.append(Event(int(w[1]), w[0], text=line.split(" ", 2)[2].strip()))
    return events


def record(duel, tmp):
    """Both sims, joined, to `tmp`: {side: (bmps, events)}."""
    a, b = socket.socketpair()
    running = {}
    for key, sock in (("A", a), ("B", b)):
        side = duel.sides[key]
        out = os.path.join(tmp, key)
        os.makedirs(out)
        with open(out + ".txt", "w") as f: f.write("\n".join(side.lines) + "\n")
        args = [sv.PROGRAM, "--headless", "--script", out + ".txt", "--record", out, "--fps", str(FPS),
                "--for", str(duel.length), "--species", side.species, "--air-fd", str(sock.fileno())]
        if duel.weather: args += ["--air-weather", duel.weather]
        if side.duel: args += ["--duel", side.duel]
        env = dict(os.environ, BLORB_SIM_MAC=side.mac)
        log = open(out + ".log", "w")
        running[key] = (subprocess.Popen(args, pass_fds=[sock.fileno()], stdin=subprocess.DEVNULL,
                                         stdout=subprocess.DEVNULL, stderr=log, env=env), log, out)
    a.close()
    b.close()
    shot = {}
    for key, (proc, log, out) in running.items():
        code = proc.wait()
        log.close()
        if code != 0:
            with open(out + ".log") as f: tail = f.read()[-800:]
            raise SystemExit("%s side %s: the simulator exited %d: %s" % (duel.name, key, code, tail))
        shot[key] = (sorted(glob.glob(os.path.join(out, "*.bmp"))), read_log(out + ".log"))
    return shot


def mac_of(text):
    return text.replace(":", "").lower()


@dataclass
class Flight:
    sender: str        # "A" or "B"
    start: int
    end: int
    fate: str          # received, lost, deaf
    unicast: bool
    text: str


def flights(duel, shot):
    """Every frame's trip, from the sender's `sent` to each of the receiver's outcomes."""
    out = []
    for sender, receiver in (("A", "B"), ("B", "A")):
        mine = mac_of(duel.sides[sender].mac)
        fates = {}
        for e in shot[receiver][1]:
            if e.kind in ("received", "lost", "deaf") and e.peer == mine: fates.setdefault(e.seq, []).append(e)
        for e in shot[sender][1]:
            if e.kind != "sent": continue
            twins = 0
            for f in fates.get(e.seq, []):
                lag = DUP_LAG_MS * twins if f.kind == "received" else 0
                twins += f.kind == "received"
                out.append(Flight(sender, e.ms + lag, e.ms + lag + FLY_MS + (f.ms - e.ms), f.kind,
                                  e.peer != "ffffffffffff", e.text))
    return out


def turn_words(text):
    """`turn 3 pounce guard 0 1 - - super`, from my side: what a kid would say happened."""
    _, n, me, them, hit_me, hit_them, fate_me, fate_them, sup = text.split()
    words = "turn %s: %s vs %s" % (n, me, them)
    if fate_me != "-": words += ", my special %s" % fate_me
    elif fate_them != "-": words += ", theirs %s" % fate_them
    if int(hit_them) > int(hit_me): return words + (", SUPER hit!" if sup == "super" else ", hit!"), mr.TEAL
    if int(hit_me) > int(hit_them): return words + ", ouch", RED
    return words + ", bonk", DIM_INK


def ticker(events, ms, dueling):
    """What a side's kid, radio and duel did in the last few seconds, oldest first: (age ms, text, colour).
    In a duel the radio talks five times a second, so its frames only fly."""
    lines = []
    for e in events:
        if e.ms > ms or e.ms < ms - TICKER_MS: continue
        age = ms - e.ms
        if e.kind == "kid": lines.append((age, "kid: " + e.text, mr.AMBER))
        elif e.kind == "duel":
            word = e.text.split()[0]
            if word == "turn": lines.append((age,) + turn_words(e.text))
            elif word == "downed": lines.append((age, "DOWN", RED))
            elif word == "fall": lines.append((age, "fell", RED))
            elif word == "egg": lines.append((age, "a friend egg!", mr.PINK))
            elif word == "met": lines.append((age, "over: %s in %s turns" % (e.text.split()[3], e.text.split()[2]), mr.INK))
        elif dueling: continue
        elif e.kind == "sent": lines.append((age, "sends  " + e.text, mr.TEAL))
        elif e.kind == "received": lines.append((age, "hears  " + e.text, mr.INK))
        elif e.kind == "lost": lines.append((age, "lost in the air: " + e.text, RED))
        elif e.kind == "deaf": lines.append((age, "radio off: missed " + e.text, GREY))
    return lines[-3:] if dueling else lines[-4:]


def standing(events, ms):
    """The duel as it stands at `ms` on one side: (phase, state words, outcome hash), or Nones."""
    phase = state = met = None
    for e in events:
        if e.ms > ms: break
        if e.kind != "duel": continue
        word, _, rest = e.text.partition(" ")
        if word == "phase": phase = rest
        elif word == "state": state = rest.split()
        elif word == "met": met = rest.split()[0]
    return phase, state, met


class Stage:
    def __init__(self, w, h):
        self.w, self.h = w, h
        wide = w > h
        k = h / 1080
        self.disc = 500 if wide else 420
        gap = 300 if wide else 160
        x0 = (w - 2 * self.disc - gap) // 2
        self.boxes = {"A": (x0, 215), "B": (x0 + self.disc + gap, 215)}
        self.f_title, self.f_name = mr.font(int(56 * k)), mr.font(int(40 * k))
        self.f_small, self.f_tick = mr.font(int(24 * k), bold=False), mr.font(int(27 * k), bold=False)
        self.f_cap = mr.font(int((52 if wide else 46) * k))
        self.caption_y = h - (80 if wide else 110)

    def edge(self, side, toward):
        x, y = self.boxes[side]
        r = self.disc / 2
        return (x + r + (r + 14) * (1 if toward == "B" else -1), y + r)

    def render(self, duel, panels, events, flying, ms):
        img = Image.new("RGB", (self.w, self.h), mr.BG)
        d = ImageDraw.Draw(img)
        d.text((self.w // 2, 64), duel.title, font=self.fit(d, duel.title, self.f_title), fill=mr.TEAL, anchor="mm")
        air = [e for k in "AB" for e in events[k] if e.ms <= ms and e.kind in ("sent", "lost")]
        sent, lost = sum(e.kind == "sent" for e in air), sum(e.kind == "lost" for e in air)
        stands = {k: standing(events[k], ms) for k in "AB"}
        hashes = [stands[k][2] for k in "AB"]
        air_words, colour = weather_words(duel.weather), mr.DIM
        if all(hashes):   # the proof the two devices agree: the same battle in both books
            same = hashes[0] == hashes[1]
            air_words = "both books: outcome %s %s %s" % (hashes[0], "=" if same else "!=", hashes[1])
            colour = mr.TEAL if same else RED
        line = "%s  |  %d frames, %d lost" % (air_words, sent, lost)
        d.text((self.w // 2, 118), line, font=self.fit(d, line, self.f_small), fill=colour, anchor="mm")
        for key, (x, y) in self.boxes.items():
            side = duel.sides[key]
            big, mask = mr.round_panel(panels[key], self.disc)
            d.ellipse((x - 8, y - 8, x + self.disc + 7, y + self.disc + 7), fill=(44, 48, 56))
            img.paste(big, (x, y), mask)
            cx = x + self.disc // 2
            d.text((cx, y - 50), side.species.upper(), font=self.f_name, fill=mr.INK, anchor="mm")
            phase, state, _ = stands[key]
            if phase is None:
                d.text((cx, y - 18), side.mac.lower(), font=self.f_small, fill=mr.DIM, anchor="mm")
            else:
                d.text((cx, y - 18), *self.status(phase, state), font=self.f_small, anchor="mm")
            ty = y + self.disc + 34
            for age, text, colour in ticker(events[key], ms, phase is not None):
                fade = max(0.5, 1 - age / TICKER_MS)
                d.text((cx, ty), text, font=self.fit(d, text, self.f_tick, self.w // 2 - 30), fill=tuple(int(c * fade + b * (1 - fade)) for c, b in zip(colour, mr.BG)),
                       anchor="mm")
                ty += 34
        for f in flying:
            self.draw_flight(d, f, ms)
        caption = duel.caption_at(ms)
        if caption:
            d.multiline_text((self.w // 2, self.caption_y), caption.replace(" | ", "\n"), font=self.f_cap, fill=mr.INK,
                             anchor="mm", align="center", spacing=10)
        return img

    def fit(self, d, text, f, width=None):
        """`f`, or a smaller size of it, so `text` fits `width` (the frame, less a margin)."""
        width = width or self.w - 60
        while d.textlength(text, font=f) > width and f.size > 12: f = f.font_variant(size=f.size - 2)
        return f

    @staticmethod
    def status(phase, state):
        """The line over a disc in a duel: a special winding up, else the phase."""
        mine, theirs = (int(state[9]), int(state[10])) if state else (0, 0)
        if phase == "mercy" and mine: return "MERCY: holding BOOT, the skull %d%% full" % (mine * 100 // 255), RED
        if phase == "mercy" and theirs: return "MERCY: the skull fills, %d%%" % (theirs * 100 // 255), RED
        if phase == "staking" and mine: return "STAKING: holding BOOT for mortal, %d%%" % (mine * 100 // 255), mr.PINK
        if state and state[6] != "-": return "winding up a %s special" % state[6], mr.PINK
        if state and state[7] != "-": return "the friend is winding up a %s special!" % state[7], mr.AMBER
        return phase.upper(), mr.DIM

    def draw_flight(self, d, f, ms):
        other = "B" if f.sender == "A" else "A"
        (x0, y0), (x1, y1) = self.edge(f.sender, other), self.edge(other, f.sender)
        lift = -1 if f.sender == "A" else 1       # A's frames arc over the gap, B's under it
        if f.fate == "lost":
            mid = f.start + (f.end - f.start) // 2
            if ms < mid: p = (ms - f.start) / max(1, f.end - f.start)
            else:
                k = (ms - mid) / BURST_MS
                if k > 1: return
                x, y = (x0 + x1) / 2, (y0 + y1) / 2 + lift * 0.4 * self.disc
                r = 10 + 26 * k
                c = tuple(int(v * (1 - k) + b * k) for v, b in zip(RED, mr.BG))
                d.line((x - r, y - r, x + r, y + r), fill=c, width=5)
                d.line((x - r, y + r, x + r, y - r), fill=c, width=5)
                return
        else:
            p = (ms - f.start) / max(1, f.end - f.start)
        if not 0 <= p <= 1: return
        x = x0 + (x1 - x0) * p
        y = y0 + (y1 - y0) * p + lift * 0.4 * self.disc * math.sin(math.pi * p)
        colour = mr.TEAL if not f.unicast else mr.AMBER
        if f.fate == "deaf" and p > 0.8: colour = GREY
        d.ellipse((x - 11, y - 11, x + 11, y + 11), fill=colour)


def weather_words(spec):
    if not spec: return "the air: clear"
    w = dict(kv.split("=") for kv in spec.split(","))
    parts = []
    if "loss" in w: parts.append("%s%% lost" % w["loss"])
    if "dup" in w: parts.append("%s%% doubled" % w["dup"])
    if "reorder" in w: parts.append("%s%% late (up to %s ms)" % (w["reorder"], w.get("late", "400")))
    return "the air: " + (", ".join(parts) if parts else "clear")


STAGES = {"wide": Stage(1920, 1080), "square": Stage(1080, 1080)}


def digest(shot):
    h = hashlib.sha256()
    for key in ("A", "B"):
        for bmp in shot[key][0]:
            with open(bmp, "rb") as f: h.update(f.read())
        for e in shot[key][1]: h.update(repr(e).encode())
    return h.hexdigest()


def film(duel, out, twice):
    check = os.path.join(out, "check")
    os.makedirs(check, exist_ok=True)
    for old in glob.glob(os.path.join(check, duel.name + "-*.png")): os.remove(old)
    with tempfile.TemporaryDirectory() as tmp:
        shot = record(duel, os.path.join(tmp, "1"))
        if twice:
            again = digest(record(duel, os.path.join(tmp, "2")))
            if again != digest(shot): raise SystemExit("%s: two runs filmed different frames" % duel.name)
            print("%s: two runs, the same frames and the same radio" % duel.name)
        flying = flights(duel, shot)
        events = {k: shot[k][1] for k in ("A", "B")}
        n = min(len(shot["A"][0]), len(shot["B"][0]))
        sinks = {k: mr.Sink(os.path.join(out, "%s-%s.mp4" % (duel.name, k)), s.w, s.h) for k, s in STAGES.items()}
        spans = {}
        for i in range(n): spans.setdefault(duel.caption_at(i * 1000 // FPS), []).append(i)
        looks = {idx[len(idx) // 2]: j for j, idx in enumerate(spans.values())}
        sheet = []
        for i in range(n):
            ms = i * 1000 // FPS
            panels = {k: Image.open(shot[k][0][i]).convert("RGB") for k in ("A", "B")}
            live = [f for f in flying if f.start <= ms <= f.end + BURST_MS]
            for key, stage in STAGES.items():
                img = stage.render(duel, panels, events, live, ms)
                sinks[key].add(mr.faded(img, i, n))
                if i in looks: img.save(os.path.join(check, "%s-%d-%s.png" % (duel.name, looks[i], key)))
                if key == "wide" and i % max(1, n // 12) == 0 and len(sheet) < 12: sheet.append((ms, img))
        for sink in sinks.values():
            sink.close()
            print(sink.path)
        contact(sheet, os.path.join(check, duel.name + "-sheet.png"))
        sent = sum(e.kind == "sent" for k in "AB" for e in events[k])
        heard = sum(e.kind == "received" for k in "AB" for e in events[k])
        lost = sum(e.kind == "lost" for k in "AB" for e in events[k])
        print("%s: %.1f s, %d frames sent, %d heard, %d lost" % (duel.name, n / FPS, sent, heard, lost))


def contact(picks, path, cols=4):
    tw, th = 480, 270
    rows = -(-len(picks) // cols)
    sheet = Image.new("RGB", (cols * tw, rows * th), mr.BG)
    d = ImageDraw.Draw(sheet)
    for i, (ms, img) in enumerate(picks):
        sheet.paste(img.resize((tw, th), Image.LANCZOS), ((i % cols) * tw, (i // cols) * th))
        d.text(((i % cols) * tw + 8, (i // cols) * th + 6), "%.1f s" % (ms / 1000), fill=mr.TEAL)
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--twice", action="store_true", help="record twice and fail unless the frames match")
    ap.add_argument("duels", nargs="*")
    args = ap.parse_args()
    paths = sorted(glob.glob(os.path.join(DUELS, "*.txt")))
    duels = [load(p) for p in paths if not args.duels or os.path.splitext(os.path.basename(p))[0] in args.duels]
    if not duels: raise SystemExit("no duels (have: %s)" % " ".join(os.path.basename(p) for p in paths))
    os.makedirs(args.out, exist_ok=True)
    for duel in duels: film(duel, args.out, args.twice)


if __name__ == "__main__":
    sys.exit(main())
