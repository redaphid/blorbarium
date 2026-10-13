"""The sizzle reel: what blorbarium does, cut from the simulator's own frames.

    tools/reel/make_reel.sh            (builds the sim, then runs this in WSL survivor)
    python3 tools/reel/make_reel.py --out DIR --assets DIR [--only BEAT ...]

REEL below is the cut, in order. A Sim beat plays tools/reel/sizzle/<feed>.txt
through tools/sim_video.py's Clip: its `#> caption` lines are the captions and
its `#> reel` stretches are the parts shown. Every Sim frame is the firmware
drawing, so a caption must say only what those frames show. A Still is a real
board's framebuffer or a phone screenshot and says so on screen; the Roster is
art that is not on any device yet and says that too.

Writes reel-square.mp4 (1080x1080) and reel-wide.mp4 (1920x1080) to --out, and
check/<n>-<beat>-<k>.png: one square frame from the middle of every caption, to
look at before anyone else does.
"""
import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import sim_video as sv  # noqa: E402

FPS = sv.FPS
FADE = 5                      # frames of fade at each end of a beat
BG = (12, 14, 18)
INK = (240, 240, 232)
DIM = (150, 156, 164)
TEAL = (80, 240, 220)
AMBER = (255, 184, 48)
PINK = (255, 120, 190)

SIM = ("SIMULATOR", TEAL)
BOARD = ("ON THE REAL BOARD", AMBER)
PHONE = ("ON THE OWNER'S PHONE", AMBER)
SOON = ("COMING SOON - NOT ON THE DEVICES YET", PINK)


@dataclass
class Card:
    title: str
    sub: str
    seconds: float


@dataclass
class Sim:
    feed: str
    macs: tuple = (None,)       # more than one: a grid, one board per MAC
    sealed: bool = False
    ota: tuple = ()             # (start, ask, receive, ready) ms: the update screen over the clip


@dataclass
class Still:
    path: str                   # under --assets
    caption: str
    label: tuple
    seconds: float
    crop: tuple = ()            # (l, t, r, b) in the image's own pixels
    disc: bool = True           # a 240x240 framebuffer: drawn as the round glass


@dataclass
class Roster:
    caption: str
    seconds: float


REEL = [
    Card("blorbarium", "a tiny swamp creature | living in a petri dish", 3),
    Still("shots/02-adult.png", "Meet Grungo", BOARD, 2.6),
    Sim("hatch"),
    Sim("grow"),
    Sim("eat"),
    Sim("think"),
    Sim("say"),
    Sim("shake"),
    Sim("foresee"),
    Sim("sleep"),
    Sim("age"),
    Sim("death"),
    Sim("clutch"),
    Sim("genomes", macs=("02:b1:0b:00:00:01", "3c:84:27:aa:10:5e", "dc:da:0c:21:7f:03", "48:27:e2:9b:44:c8")),
    Sim("sealed", sealed=True),
    Still("shots/09-keepsake-sealed.png", "The same amber sleep, | on the real board", BOARD, 2.6),
    Sim("ota", ota=(1000, 3000, 2600, 1400)),
    Still("phone.png", "\"Updated to 0.4.1. | Grungo is fine.\"", PHONE, 3.2, crop=(0, 1720, 1224, 2520), disc=False),
    Roster("Coming soon: Grungo and | eight new friends to duel", 4.5),
    Card("blorbarium", "\"GEN 0. STILL NO HAT.\" | - Grungo", 3.5),
]


@dataclass
class Shot:
    """One frame's content, before a layout places it."""
    discs: list = field(default_factory=list)   # 240x240 panels, drawn round
    picture: Image.Image = None                  # drawn square-cornered, fitted
    sprites: list = field(default_factory=list)  # (name, RGBA image) for the roster
    caption: str = ""
    label: tuple = None
    badge: str = ""
    title: str = ""                              # a card: title and sub, no caption
    sub: str = ""


def font(size, bold=True):
    for path in ("/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        if os.path.exists(path):
            f = ImageFont.truetype(path, size)
            try: f.set_variation_by_name("Bold" if bold else "Regular")
            except Exception: pass
            return f
    return ImageFont.load_default(size)


_masks = {}


def round_panel(panel, size):
    """The 240 px panel at `size`, pixels kept square: nearest-neighbour up to the
    next whole multiple, then a smooth step down if size is not one."""
    k = -(-size // sv.PANEL)
    big = panel.resize((sv.PANEL * k, sv.PANEL * k), Image.NEAREST)
    if big.size[0] != size: big = big.resize((size, size), Image.LANCZOS)
    if size not in _masks:
        m = Image.new("L", (size * 4, size * 4), 0)
        ImageDraw.Draw(m).ellipse((0, 0, size * 4 - 1, size * 4 - 1), fill=255)
        _masks[size] = m.resize((size, size), Image.LANCZOS)
    return big, _masks[size]


class Layout:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.wide = w > h
        s = h / 1080
        self.f_cap, self.f_label, self.f_badge = font(int((60 if self.wide else 50) * s)), font(int(26 * s)), font(int(26 * s))
        self.f_title, self.f_sub, self.f_name = font(int(130 * s)), font(int(46 * s), bold=False), font(int(30 * s))

    def disc_boxes(self, n):
        if n == 1:
            return [(180, 180, 720)] if self.wide else [(180, 120, 720)]
        if self.wide:
            return [(48 + i * 468, 190, 420) for i in range(4)]
        return [(120 + (i % 2) * 440, 100 + (i // 2) * 420, 400) for i in range(4)]

    def caption_anchor(self, shot):
        if self.wide and len(shot.discs) <= 1 and not shot.sprites:
            return (1400, 560)
        if self.wide:
            return (self.w // 2, 860)
        return (self.w // 2, 990 if len(shot.discs) != 4 and not shot.sprites else 1000)

    def pill(self, d, xy, text, colour, font_):
        x, y = xy
        tw = d.textlength(text, font=font_)
        pad, hh = 18, font_.size * 0.9
        d.rounded_rectangle((x - tw / 2 - pad, y - hh, x + tw / 2 + pad, y + hh), radius=hh, outline=colour, width=2, fill=BG)
        d.text((x, y), text, font=font_, fill=colour, anchor="mm")

    def render(self, shot):
        img = Image.new("RGB", (self.w, self.h), BG)
        d = ImageDraw.Draw(img)
        if shot.title:
            d.text((self.w // 2, self.h // 2 - 50), shot.title, font=self.f_title, fill=TEAL, anchor="mm")
            d.multiline_text((self.w // 2, self.h // 2 + 90), shot.sub.replace(" | ", "\n"), font=self.f_sub,
                             fill=DIM, anchor="ma", align="center", spacing=14)
            return img
        for panel, (x, y, size) in zip(shot.discs, self.disc_boxes(len(shot.discs))):
            big, mask = round_panel(panel, size)
            d.ellipse((x - 8, y - 8, x + size + 7, y + size + 7), fill=(44, 48, 56))
            img.paste(big, (x, y), mask)
        if shot.picture is not None:
            box = (100, 200, 1000, 880) if self.wide else (60, 230, 1020, 860)
            bw, bh = box[2] - box[0], box[3] - box[1]
            pic = shot.picture
            k = min(bw / pic.width, bh / pic.height)
            pic = pic.resize((int(pic.width * k), int(pic.height * k)), Image.LANCZOS)
            px, py = box[0] + (bw - pic.width) // 2, box[1] + (bh - pic.height) // 2
            d.rounded_rectangle((px - 6, py - 6, px + pic.width + 5, py + pic.height + 5), radius=18, fill=(44, 48, 56))
            img.paste(pic, (px, py))
        if shot.sprites:
            cols, size = (9, 190) if self.wide else (3, 220)
            gap_x = (self.w - cols * size) // (cols + 1)
            top = 330 if self.wide else 110
            for i, (name, sprite) in enumerate(shot.sprites):
                cx = gap_x + (i % cols) * (size + gap_x)
                cy = top + (i // cols) * (size + 50)
                small = sprite.resize((size, size), Image.LANCZOS)
                img.paste(small, (cx, cy), small)
                d.text((cx + size // 2, cy + size + 4), name, font=self.f_name, fill=INK, anchor="ma")
        if shot.label:
            text, colour = shot.label
            pos = (1400, 330) if self.wide and len(shot.discs) <= 1 and not shot.sprites else (self.w // 2, 50)
            self.pill(d, pos, text, colour, self.f_label)
        if shot.badge:
            pos = (1400, 790) if self.wide and len(shot.discs) == 1 else (self.w - 190, 50) if not self.wide else (self.w - 220, 50)
            self.pill(d, pos, shot.badge, TEAL, self.f_badge)
        if shot.caption:
            d.multiline_text(self.caption_anchor(shot), shot.caption.replace(" | ", "\n"), font=self.f_cap, fill=INK,
                             anchor="mm", align="center", spacing=12)
        return img


LAYOUTS = {"square": Layout(1080, 1080), "wide": Layout(1920, 1080)}


def build_overlay(tmp):
    exe = os.path.join(tmp, "ota_overlay")
    lib = os.path.join(os.path.dirname(os.path.dirname(HERE)), "lib")
    srcs = [os.path.join(HERE, "ota_overlay.cpp"), os.path.join(lib, "ota/src/screen.cpp")] + \
           [os.path.join(lib, "paint/src", f) for f in ("draw.cpp", "marquee.cpp", "text.cpp", "freesans9pt7b.cpp")]
    subprocess.run(["g++", "-std=c++17", "-O2", "-o", exe, "-I" + os.path.join(lib, "ota/include"),
                    "-I" + os.path.join(lib, "paint/include"), "-I" + os.path.join(lib, "blorb/include")] + srcs, check=True)
    return exe


def record_sim(beat, tmp, overlay):
    """The beat's clip, recorded once per MAC: a list of BMP lists, one per board."""
    clip = sv.Clip(os.path.join(HERE, "sizzle", beat.feed + ".txt"))
    runs = []
    for i, mac in enumerate(beat.macs):
        bmps = sv.record(clip, tmp, mac=mac, sealed=beat.sealed, tag="-%d" % i)
        if beat.ota:
            src = os.path.dirname(bmps[0])
            dst = src + "-ota"
            os.makedirs(dst)
            subprocess.run([overlay, src, dst, str(FPS)] + [str(v) for v in beat.ota], check=True)
            bmps = [os.path.join(dst, os.path.basename(b)) for b in bmps]
        runs.append(bmps)
    return clip, runs


def sim_shots(beat, clip, runs):
    stretches = clip.reel or [(clip.start, clip.length)]
    n = min(len(r) for r in runs)
    for i in range(n):
        ms = i * 1000 // FPS
        if not any(a <= ms < b for a, b in stretches): continue
        yield ms, Shot(discs=[Image.open(r[i]).convert("RGB") for r in runs], caption=clip.caption_at(ms),
                       label=SIM, badge=clip.badge_at(ms).replace("fast-forward  ", "fast-forward "))


def still_shots(beat, assets):
    im = Image.open(os.path.join(assets, beat.path)).convert("RGB")
    if beat.crop: im = im.crop(beat.crop)
    if beat.disc and im.size != (sv.PANEL, sv.PANEL): im = im.resize((sv.PANEL, sv.PANEL), Image.NEAREST)
    shot = Shot(discs=[im], caption=beat.caption, label=beat.label) if beat.disc else \
        Shot(picture=im, caption=beat.caption, label=beat.label)
    for i in range(int(beat.seconds * FPS)): yield i * 1000 // FPS, shot


def roster_shots(beat, assets):
    import json
    with open(os.path.join(assets, "roster", "roster.json")) as f:
        creatures = json.load(f)["creatures"]
    sprites = [(c["name"], Image.open(os.path.join(assets, "roster", c["slug"] + ".png")).convert("RGBA")) for c in creatures]
    shot = Shot(sprites=sprites, caption=beat.caption, label=SOON)
    for i in range(int(beat.seconds * FPS)): yield i * 1000 // FPS, shot


def card_shots(beat):
    shot = Shot(title=beat.title, sub=beat.sub)
    for i in range(int(beat.seconds * FPS)): yield i * 1000 // FPS, shot


class Sink:
    def __init__(self, path, w, h):
        self.path = path
        self.proc = subprocess.Popen(
            [sv.ffmpeg(), "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (w, h),
             "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-tune", "animation",
             "-pix_fmt", "yuv420p", "-movflags", "+faststart", path], stdin=subprocess.PIPE)

    def add(self, img): self.proc.stdin.write(img.tobytes())

    def close(self):
        self.proc.stdin.close()
        if self.proc.wait() != 0: raise SystemExit("ffmpeg failed on " + self.path)


def panel_sheet(shots, path, cols=8, rows=2):
    """Sixteen evenly spaced panels of a beat at the panel's own size, stamped
    with the clip's ms: what moved, for a check run."""
    k = cols * rows
    sheet = Image.new("RGB", (cols * sv.PANEL, rows * (sv.PANEL + 16)), BG)
    d = ImageDraw.Draw(sheet)
    for j in range(k):
        ms, shot = shots[round(j * (len(shots) - 1) / (k - 1))]
        x, y = (j % cols) * sv.PANEL, (j // cols) * (sv.PANEL + 16)
        sheet.paste(shot.discs[0], (x, y + 16))
        d.text((x + 4, y + 2), "%d ms" % ms, fill=TEAL)
    sheet.save(path)


def add_music(path, seconds):
    """tools/reel/chiptune.py under the video, the picture copied as is."""
    wav, mixed = path + ".wav", path + ".mixed.mp4"
    subprocess.run([sys.executable, os.path.join(HERE, "chiptune.py"), wav, "%.2f" % seconds], check=True)
    subprocess.run([sv.ffmpeg(), "-y", "-loglevel", "error", "-i", path, "-i", wav, "-c:v", "copy", "-c:a", "aac",
                    "-b:a", "128k", "-shortest", "-movflags", "+faststart", mixed], check=True)
    os.replace(mixed, path)
    os.remove(wav)


def faded(img, i, n):
    k = min(i + 1, n - i, FADE + 1) / (FADE + 1)
    return img if k >= 1 else Image.blend(Image.new("RGB", img.size, BG), img, k)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--assets", required=True)
    ap.add_argument("--silent", action="store_true", help="no chiptune")
    ap.add_argument("--only", nargs="*", help="beat names (feed or still file stem): a check run, no video")
    args = ap.parse_args()
    name = lambda b: b.feed if isinstance(b, Sim) else os.path.splitext(os.path.basename(b.path))[0] if isinstance(b, Still) else type(b).__name__.lower()
    reel = [b for b in REEL if not args.only or name(b) in args.only]
    check = os.path.join(args.out, "check")
    shutil.rmtree(check, ignore_errors=True)   # a frame left from an older cut would be checked as this one
    os.makedirs(check)
    sinks = {} if args.only else {k: Sink(os.path.join(args.out, "reel-%s.mp4" % k), l.w, l.h) for k, l in LAYOUTS.items()}
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        overlay = build_overlay(tmp)
        with concurrent.futures.ThreadPoolExecutor() as pool:
            recorded = {id(b): pool.submit(record_sim, b, tmp, overlay) for b in reel if isinstance(b, Sim)}
        for n, beat in enumerate(reel):
            if isinstance(beat, Sim): shots = list(sim_shots(beat, *recorded[id(beat)].result()))
            elif isinstance(beat, Still): shots = list(still_shots(beat, args.assets))
            elif isinstance(beat, Roster): shots = list(roster_shots(beat, args.assets))
            else: shots = list(card_shots(beat))
            spans = {}
            for i, (_, s) in enumerate(shots): spans.setdefault(s.caption or s.title, []).append(i)
            looks = {idx[len(idx) // 2]: k for k, idx in enumerate(spans.values())}
            for i, (ms, shot) in enumerate(shots):
                for key, sink in sinks.items(): sink.add(faded(LAYOUTS[key].render(shot), i, len(shots)))
                if i in looks:
                    LAYOUTS["square"].render(shot).save(os.path.join(check, "%02d-%s-%d.png" % (n, name(beat), looks[i])))
            if args.only and shots and shots[0][1].discs:
                panel_sheet(shots, os.path.join(check, "%02d-%s-sheet.png" % (n, name(beat))))
            print("%5.1f s  %-10s %5.1f s" % (total / FPS, name(beat), len(shots) / FPS), flush=True)
            total += len(shots)
    for sink in sinks.values():
        sink.close()
        if not args.silent: add_music(sink.path, total / FPS)
        print(sink.path)
    print("total %.1f s" % (total / FPS))


if __name__ == "__main__":
    sys.exit(main())
