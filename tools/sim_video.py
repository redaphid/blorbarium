"""Clips of the dish as video: each tools/reel/<clip>.txt played on the simulator.

    python3 tools/sim_video.py --out DIR [clip ...]     (tools/sim_video.sh builds the sim first)

A clip is a simulator script, played as is, so everything in it goes through
the sim's own seams: `!` lines are the hand on the IMU and BOOT, `DEBUG warp`
and `DEBUG time` are the clock. Lines starting `#>` are for this tool and the
sim skips them as comments; every time is on the script's clock in ms:

    #> title Eating              the clip's name on screen
    #> length 12000              how long to record
    #> from 600                  the first moment shown (the setup before it is cut)
    #> caption 600 BOOT drops | a pellet     shown from 600 until the next; " | " breaks the line
    #> reel 1000 7000            a stretch that goes into reel.mp4 (repeatable)

`@<ms> REPEAT <n> <line> ; <line> ...` is shorthand this tool expands into n
copies of the lines at that moment, so a fed day reads as one line:
`@1000 REPEAT 12 !button ; DEBUG warp 72000` is BOOT every two pet hours.

A warp is shown as a fast-forward badge for a moment, since the frames jump.
Frames are 2x nearest-neighbour, cut to the round panel, H.264 yuv420p.
"""
import argparse
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROGRAM = os.path.join(ROOT, ".pio", "build", "sim", "program")
CLIPS = os.path.join(ROOT, "tools", "reel")

FPS = 25                  # the firmware draws every 40 ms
TICK_MS = 100             # blorb::kTickMs
PANEL = 240
SCALE = 2
W, H = 540, 680           # phone-portrait-ish, even for yuv420p
DISC_X, DISC_Y = (W - PANEL * SCALE) // 2, 84
BADGE_MS = 1500
BURST_MS = 400
BG = (14, 16, 20)
BEZEL = (52, 56, 64)
INK = (232, 232, 226)
DIM = (150, 156, 164)
ACCENT = (96, 214, 196)


class Clip:
    def __init__(self, path):
        self.name = os.path.splitext(os.path.basename(path))[0]
        self.path = path
        self.title = self.name
        self.length = 10000
        self.start = 0
        self.captions = []   # (at_ms, text)
        self.warps = []      # (at_ms, ticks)
        self.reel = []       # (from_ms, to_ms) stretches
        self.script = []     # the lines the sim plays, REPEATs expanded
        with open(path) as f:
            for raw in f:
                line = raw.strip()
                if line.startswith("@") and " REPEAT " in line:
                    at, _, rest = line.partition(" ")
                    n, _, body = rest[len("REPEAT "):].partition(" ")
                    parts = [p.strip() for p in body.split(" ; ")]
                    self.script += ["%s %s" % (at, p) for _ in range(int(n)) for p in parts]
                else:
                    self.script.append(line)
        for line in self.script:
            if line.startswith("#>"):
                key, _, rest = line[2:].strip().partition(" ")
                if key == "title": self.title = rest
                elif key == "length": self.length = int(rest)
                elif key == "from": self.start = int(rest)
                elif key == "caption":
                    at, _, text = rest.partition(" ")
                    self.captions.append((int(at), text))
                elif key == "reel":
                    a, b = rest.split()
                    self.reel.append((int(a), int(b)))
                else: raise SystemExit("%s: unknown #> %s" % (path, key))
            elif line.startswith("@") and " DEBUG warp " in line:
                at, _, rest = line[1:].partition(" ")
                self.warps.append((int(at), int(rest.split()[2])))
        self.captions.sort()

    def caption_at(self, ms):
        text = ""
        for at, t in self.captions:
            if at <= ms: text = t
        return text

    def badge_at(self, ms):
        """The fast-forward shown at `ms`: the latest burst of warps (those within
        BURST_MS of each other), for BADGE_MS after it."""
        shown = [at for at, _ in self.warps if self.start <= at <= ms]
        if not shown or ms >= max(shown) + BADGE_MS: return ""
        last = max(shown)
        ticks = sum(n for at, n in self.warps if last - BURST_MS < at <= last)
        return "fast-forward  +" + span(ticks * TICK_MS)


def span(ms):
    s = ms / 1000
    if s < 90: return "%d s" % s
    if s < 5400: return "%d min" % round(s / 60)
    if s < 2 * 86400: return "%.1f h" % (s / 3600) if s < 36000 else "%d h" % round(s / 3600)
    return "%.1f days" % (s / 86400)


def font(size):
    for path in sorted(glob.glob("/usr/share/fonts/**/*.ttf", recursive=True)):
        if "Sans-Regular" in path or "DejaVuSans.ttf" in path:
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size)


TITLE_FONT, CAPTION_FONT, BADGE_FONT = font(28), font(21), font(18)

DISC_MASK = Image.new("L", (PANEL * SCALE, PANEL * SCALE), 0)
ImageDraw.Draw(DISC_MASK).ellipse((0, 0, PANEL * SCALE - 1, PANEL * SCALE - 1), fill=255)


def compose(panel, clip, ms):
    """One video frame: the panel as the round glass shows it, the title, the caption."""
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    big = panel.resize((PANEL * SCALE, PANEL * SCALE), Image.NEAREST)
    r = PANEL * SCALE // 2
    d.ellipse((DISC_X - 6, DISC_Y - 6, DISC_X + 2 * r + 5, DISC_Y + 2 * r + 5), fill=BEZEL)
    d.ellipse((DISC_X - 1, DISC_Y - 1, DISC_X + 2 * r, DISC_Y + 2 * r), fill=(0, 0, 0))
    img.paste(big, (DISC_X, DISC_Y), DISC_MASK)
    d.text((W // 2, 44), clip.title, font=TITLE_FONT, fill=INK, anchor="mm")
    badge = clip.badge_at(ms)
    if badge:
        d.rounded_rectangle((W // 2 - 110, DISC_Y + 2 * r + 14, W // 2 + 110, DISC_Y + 2 * r + 42), 12, fill=(30, 60, 58))
        d.text((W // 2, DISC_Y + 2 * r + 28), badge, font=BADGE_FONT, fill=ACCENT, anchor="mm")
    caption = clip.caption_at(ms)
    if caption:
        d.multiline_text((W // 2, H - 40), caption.replace(" | ", "\n"), font=CAPTION_FONT, fill=DIM, anchor="mm", align="center")
    return img


def record(clip, tmp):
    """Every frame of the clip as BMPs in tmp/<clip>/, in order."""
    out = os.path.join(tmp, clip.name)
    os.makedirs(out)
    script = os.path.join(tmp, clip.name + ".txt")
    with open(script, "w") as f: f.write("\n".join(clip.script) + "\n")
    r = subprocess.run([PROGRAM, "--headless", "--script", script, "--record", out,
                        "--fps", str(FPS), "--for", str(clip.length)],
                       stdin=subprocess.DEVNULL, capture_output=True, text=True,
                       env={k: v for k, v in os.environ.items() if k != "BLORB_SIM_MAC"})
    if r.returncode != 0:
        raise SystemExit("%s: the simulator exited %d: %s" % (clip.name, r.returncode, r.stderr[-800:]))
    return sorted(glob.glob(os.path.join(out, "*.bmp")))


def ffmpeg():
    """FFMPEG=<binary> (imageio-ffmpeg's static build serves), else whatever PATH has."""
    found = os.environ.get("FFMPEG") or shutil.which("ffmpeg") or shutil.which("ffmpeg.exe")
    if found: return found
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        return None


def host_path(exe, path):
    """ffmpeg.exe through WSL interop wants a Windows path."""
    if exe.endswith(".exe"):
        return subprocess.run(["wslpath", "-w", path], capture_output=True, text=True, check=True).stdout.strip()
    return path


class Sink:
    """Frames in, a video file out: H.264 through ffmpeg, else an animated WebP."""

    def __init__(self, path_stem):
        self.exe = ffmpeg()
        self.frames = []
        if self.exe:
            self.path = path_stem + ".mp4"
            self.proc = subprocess.Popen(
                [self.exe, "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                 "-s", "%dx%d" % (W, H), "-r", str(FPS), "-i", "-",
                 "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-tune", "animation",
                 "-pix_fmt", "yuv420p", "-profile:v", "high", "-level", "4.0",
                 "-movflags", "+faststart", host_path(self.exe, self.path)],
                stdin=subprocess.PIPE)
        else:
            self.path = path_stem + ".webp"

    def add(self, img):
        if self.exe: self.proc.stdin.write(img.tobytes())
        else: self.frames.append(img)

    def close(self):
        if self.exe:
            self.proc.stdin.close()
            if self.proc.wait() != 0: raise SystemExit("ffmpeg failed on " + self.path)
        else:
            self.frames[0].save(self.path, save_all=True, append_images=self.frames[1:],
                                duration=1000 // FPS, loop=0, quality=90)
        return self.path


SHEET_COLS, SHEET_ROWS = 4, 3


def sheet_picks(n):
    """Which of n frames the contact sheet shows: twelve, evenly spaced."""
    k = SHEET_COLS * SHEET_ROWS
    return {round(i * (n - 1) / (k - 1)) for i in range(k)}


def contact_sheet(picks, path, cols=SHEET_COLS, rows=SHEET_ROWS):
    """The picked frames on one image, for a person (or an agent) to look at without a player."""
    tw, th = W // 2, H // 2
    sheet = Image.new("RGB", (cols * tw, rows * th), BG)
    for i, (ms, img) in enumerate(picks):
        cell = img.resize((tw, th), Image.LANCZOS)
        ImageDraw.Draw(cell).text((8, 6), "%.1f s" % (ms / 1000), font=BADGE_FONT, fill=ACCENT)
        sheet.paste(cell, ((i % cols) * tw, (i // cols) * th))
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("clips", nargs="*")
    args = ap.parse_args()
    paths = sorted(glob.glob(os.path.join(CLIPS, "*.txt")))
    clips = [Clip(p) for p in paths if not args.clips or os.path.splitext(os.path.basename(p))[0] in args.clips]
    if not clips: raise SystemExit("no clips (have: %s)" % " ".join(os.path.basename(p) for p in paths))
    os.makedirs(os.path.join(args.out, "sheets"), exist_ok=True)
    print("encoder:", ffmpeg() or "none, animated WebP")
    reel = Sink(os.path.join(args.out, "reel")) if not args.clips else None
    reel_ms = 0
    with tempfile.TemporaryDirectory() as tmp:
        with concurrent.futures.ThreadPoolExecutor() as pool:
            shot = {c.name: pool.submit(record, c, tmp) for c in clips}
        for clip in clips:
            sink = Sink(os.path.join(args.out, clip.name))
            first = -(-clip.start * FPS // 1000)
            bmps = shot[clip.name].result()[first:]
            wanted, picks = sheet_picks(len(bmps)), []
            for i, bmp in enumerate(bmps):
                ms = (first + i) * 1000 // FPS
                img = compose(Image.open(bmp).convert("RGB"), clip, ms)
                sink.add(img)
                if i in wanted: picks.append((ms, img))
                if reel and any(a <= ms < b for a, b in clip.reel):
                    reel.add(img)
                    reel_ms += 1000 // FPS
            print("%-12s %5.1f s  %s" % (clip.name, len(bmps) / FPS, sink.close()))
            contact_sheet(picks, os.path.join(args.out, "sheets", clip.name + ".png"))
    if reel: print("%-12s %5.1f s  %s" % ("reel", reel_ms / 1000, reel.close()))


if __name__ == "__main__":
    sys.exit(main())
