#!/usr/bin/env python3
"""Contact sheets and side-by-side videos from an e2e learning run.

    python3 tools/e2e_sheets.py <E2E_OUT dir> <sheets dir> <videos dir>

For each <name>.json that tools/e2e_learning.sh wrote, with its frames under
frames/<name>/{control,treatment}/: a PNG contact sheet (control above,
treatment below, eight matched moments of the clip) and an MP4 (H.264,
yuv420p; control left, treatment right, captioned with the measured effect).
The frames are deleted once both are written. ffmpeg is $FFMPEG, else on
PATH, else imageio-ffmpeg's bundled binary when that package is installed;
with none of them the video is an animated GIF.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

SIDE = 240
SCALE = 2


def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def clock(ms):
    day, rest = divmod(ms // 1000, 86400)
    return f"day {day} {rest // 3600:02d}:{rest % 3600 // 60:02d}:{rest % 60:02d}"


def effect_line(m):
    return (f"{m['name']}: treatment {m['meanT']:.3g} vs control {m['meanC']:.3g}, "
            f"T-C {m['diff']:+.3g} [95% CI {m['lo']:.3g}, {m['hi']:.3g}], "
            f"{100 * m['towards']:.0f}% of {m['n']} seeds as predicted: {m['verdict']}")


def wrap(text, width):
    words, lines, line = text.split(), [], ""
    for w in words:
        if len(line) + len(w) + 1 > width:
            lines.append(line)
            line = w
        else:
            line = f"{line} {w}".strip()
    return lines + [line]


def frames(arm_dir):
    paths = sorted(arm_dir.glob("*.ppm"))
    captions = (arm_dir / "captions.txt").read_text().splitlines() if (arm_dir / "captions.txt").exists() else []
    return paths, captions


def sheet(meta, ctl, trt, out):
    (cp, cc), (tp, tc) = ctl, trt
    n = min(len(cp), len(tp))
    picks = [round(i * (n - 1) / 7) for i in range(8)]
    left, head, label = 120, 120, 40
    img = Image.new("RGB", (left + 8 * SIDE, head + 2 * (SIDE + label)), "white")
    d = ImageDraw.Draw(img)
    d.text((10, 8), f"{meta['name']}: {meta['stimulus']}", fill="black", font=font(22))
    d.text((10, 40), effect_line(meta["metrics"][0]), fill="black", font=font(16))
    d.text((10, 64), f"seed {meta['shownSeed']}, matched moments from {clock(meta['frames']['control']['at'])}, "
                     f"a frame every {meta['frames']['everyMs'] / 1000:g} s", fill="gray", font=font(16))
    every = meta["frames"]["everyMs"]
    for row, (paths, caps, name) in enumerate(((cp, cc, "control"), (tp, tc, "treatment"))):
        y = head + row * (SIDE + label)
        d.text((10, y + SIDE // 2), name, fill="black", font=font(20))
        for col, i in enumerate(picks):
            img.paste(Image.open(paths[i]), (left + col * SIDE, y))
            cap = f"+{i * every / 1000:.1f}s {caps[i] if i < len(caps) else ''}"
            d.text((left + col * SIDE + 6, y + SIDE + 6), cap, fill="black", font=font(15))
    img.save(out)


def ffmpeg():
    if os.environ.get("FFMPEG"):
        return os.environ["FFMPEG"]
    if shutil.which("ffmpeg"):
        return "ffmpeg"
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        return None


def video(meta, ctl, trt, out):
    (cp, cc), (tp, tc) = ctl, trt
    n = min(len(cp), len(tp))
    w, head, foot = 2 * SIDE * SCALE, 150, 56
    every = meta["frames"]["everyMs"]
    speed = every / 80
    lines = wrap(f"{meta['name']}: {meta['stimulus']}", 70)[:2] + wrap(effect_line(meta["metrics"][0]), 78)[:3]
    tmp = Path(tempfile.mkdtemp(prefix=f"e2e-{meta['name']}-"))
    for i in range(n):
        img = Image.new("RGB", (w, head + SIDE * SCALE + foot), "black")
        d = ImageDraw.Draw(img)
        for k, line in enumerate(lines):
            d.text((12, 8 + 26 * k), line, fill="white", font=font(20 if k < 2 else 17))
        for col, (paths, caps, name) in enumerate(((cp, cc, "CONTROL"), (tp, tc, "TREATMENT"))):
            x = col * SIDE * SCALE
            img.paste(Image.open(paths[i]).resize((SIDE * SCALE, SIDE * SCALE), Image.NEAREST), (x, head))
            d.text((x + 12, head + SIDE * SCALE + 6), f"{name}  {caps[i] if i < len(caps) else ''}",
                   fill="white", font=font(20))
        d.text((12, head + SIDE * SCALE + 32),
               f"{clock(meta['frames']['control']['at'] + i * every)}  seed {meta['shownSeed']}"
               + (f"  ({speed:g}x speed)" if speed > 1.01 else ""), fill="gray", font=font(16))
        img.save(tmp / f"{i:05d}.png")
    exe = ffmpeg()
    if not exe:
        frames0 = [Image.open(tmp / f"{i:05d}.png") for i in range(n)]
        gif = out.with_suffix(".gif")
        frames0[0].save(gif, save_all=True, append_images=frames0[1:], duration=80, loop=0)
        shutil.rmtree(tmp)
        return gif
    subprocess.run([exe, "-y", "-loglevel", "error", "-framerate", "12.5", "-i", "%05d.png", "-c:v", "libx264",
                    "-pix_fmt", "yuv420p", "-crf", "20", "-movflags", "+faststart", f"{meta['name']}.mp4"],
                   cwd=tmp, check=True)
    shutil.move(str(tmp / f"{meta['name']}.mp4"), out)
    shutil.rmtree(tmp)
    return out


def main():
    src, sheets, videos = (Path(a) for a in sys.argv[1:4])
    sheets.mkdir(parents=True, exist_ok=True)
    videos.mkdir(parents=True, exist_ok=True)
    for path in sorted(src.glob("*.json")):
        meta = json.loads(path.read_text())
        base = src / "frames" / meta["name"]
        if not (base / "control").exists() or not (base / "treatment").exists():
            print(f"{meta['name']}: no frames")
            continue
        ctl, trt = frames(base / "control"), frames(base / "treatment")
        sheet(meta, ctl, trt, sheets / f"{meta['name']}.png")
        made = video(meta, ctl, trt, videos / f"{meta['name']}.mp4")
        shutil.rmtree(base)
        print(f"{meta['name']}: {sheets / (meta['name'] + '.png')} {made}")


if __name__ == "__main__":
    main()
