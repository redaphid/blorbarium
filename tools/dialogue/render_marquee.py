"""Render dialogue lines on the round screen with the engine's own marquee.

Builds marquee_main.cpp in WSL against a copy of lib/paint/src/draw.cpp whose
marquee string is the dialogue line (the engine source is untouched), renders
the line's head and tail as two frames, and tiles every line into one PNG.

    python tools/dialogue/render_marquee.py OUT.png [N_LINES]
"""
import random
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BUILD = HERE / ".cache" / "marquee"
DISTRO = "survivor"
MARQUEE_DECL = re.compile(r'constexpr char kTimeUnknownMarquee\[\] = "[^"]*";')
# draw.cpp: the band's widest chord is x 22..218 and a glyph advances 12 px at 3 px a tick.
HEAD_TICK = 60
TAIL_TICK = HEAD_TICK + 16 * 12 // 3


def wsl(path: Path) -> str:
    p = path.resolve().as_posix()
    return f"/mnt/{p[0].lower()}{p[2:]}"


def lines_from_def(n, seed=7):
    text = (REPO / "lib/blorb/include/blorb/defs/dialogue.def").read_text(encoding="utf-8")
    rows = re.findall(r'BLORB_LINE\((\w+), (\w+), (\w+), "(.*)"\)', text)
    return [r[3] for r in random.Random(seed).sample(rows, n)]


def gxx(*args):
    subprocess.run(["wsl", "-d", DISTRO, "--exec", "g++", "-std=c++17", "-O1",
                    f"-I{wsl(REPO / 'lib/blorb/include')}", f"-I{wsl(REPO / 'lib/paint/include')}", *args],
                   check=True)


def build_shared():
    """Everything but draw.cpp, compiled once into one relocatable object."""
    BUILD.mkdir(parents=True, exist_ok=True)
    srcs = [HERE / "marquee_main.cpp", REPO / "lib/paint/src/placeholder_pack.cpp",
            *sorted((REPO / "lib/blorb/src").glob("*.cpp"))]
    objs = []
    for s in srcs:
        o = BUILD / f"{s.stem}.o"
        gxx("-c", wsl(s), "-o", wsl(o))
        objs.append(wsl(o))
    return objs


def render(line: str, idx: int, shared):
    src = (REPO / "lib/paint/src/draw.cpp").read_text(encoding="utf-8")
    patched, n = MARQUEE_DECL.subn(f'constexpr char kTimeUnknownMarquee[] = "{line}";', src)
    assert n == 1, "draw.cpp no longer declares kTimeUnknownMarquee as expected"
    draw = BUILD / f"draw_{idx}.cpp"
    draw.write_text(patched, encoding="utf-8", newline="\n")
    exe = BUILD / f"marquee_{idx}"
    gxx(wsl(draw), *shared, "-o", wsl(exe))
    frames = []
    for tick in (HEAD_TICK, TAIL_TICK):
        ppm = subprocess.run(["wsl", "-d", DISTRO, "--exec", wsl(exe), str(tick)], check=True,
                             capture_output=True).stdout
        frames.append(Image.frombytes("RGB", (240, 240), ppm[ppm.index(b"255\n") + 4:]))
    return frames


def main():
    out = Path(sys.argv[1])
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    lines = lines_from_def(n)
    shared = build_shared()
    sheet = Image.new("RGB", (2 * 240 + 30, n * 250 + 10), (24, 24, 24))
    for i, line in enumerate(lines):
        for j, frame in enumerate(render(line, i, shared)):
            sheet.paste(frame, (10 + j * 250, 10 + i * 250))
        print(f"rendered: {line}", flush=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
