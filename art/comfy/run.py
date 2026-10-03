#!/usr/bin/env python3
"""Queue grungo_adjust_api.json on ComfyUI, and move a result into the sprite pack.

    uv run --with pillow --with numpy python art/comfy/run.py install
    uv run --with pillow --with numpy python art/comfy/run.py gen --frame neutral --mask mouth \\
        --prompt "(a slightly wider gentle closed-mouth frog smile:1.3)" --denoise 0.6 --seed 1001 --count 4
    uv run --with pillow --with numpy python art/comfy/run.py adopt outputs/comfy/neutral_mouth_s1003.png --as neutral

install uploads the opaque frames, the mask presets and the body alpha to ComfyUI's input
folder (the editor workflow needs them too). gen writes outputs/comfy/<frame>_<mask>_s<seed>.png
and a contact sheet, and checks that nothing outside the grown mask changed. adopt copies
an output into pets/grungo/art/frames/. ComfyUI is $COMFY, default http://127.0.0.1:8188.
"""
import argparse
import io
import json
import os
import time
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from build import NEGATIVE, STYLE, SUBJECT

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
ART = ROOT / "pets/grungo/art"
OUT = ROOT / "outputs/comfy"
COMFY = os.environ.get("COMFY", "http://127.0.0.1:8188")
FACE_ELLIPSE = (345, 150, 685, 372)     # sprite-expressions character.json masks.face, with the eyes
FACE_BOX = (337, 142, 694, 385)         # where every registered frame differs from neutral
UNREGISTERED = {"egg"}                  # blank-canvas art: its own alpha, not the anchor's

# run.py knob -> (node title, input). The node titles are build.py's.
KNOBS = {
    "frame": ("Input frame", "image"),
    "ref_a": ("Reference A", "image"),
    "ref_b": ("Reference B", "image"),
    "mask": ("Mask preset", "image"),
    "positive": ("Positive prompt", "text"),
    "negative": ("Negative prompt", "text"),
    "ipa_weight": ("IP-Adapter (identity)", "weight"),
    "ipa_weight_type": ("IP-Adapter (identity)", "weight_type"),
    "ipa_end_at": ("IP-Adapter (identity)", "end_at"),
    "grow": ("Grow mask", "expand"),
    "seed": ("Sampler", "seed"),
    "steps": ("Sampler", "steps"),
    "cfg": ("Sampler", "cfg"),
    "sampler": ("Sampler", "sampler_name"),
    "scheduler": ("Sampler", "scheduler"),
    "denoise": ("Sampler", "denoise"),
}


def registered_frames():
    return sorted(p.stem for p in (ART / "frames").glob("*.png") if p.stem not in UNREGISTERED)


def anchor_alpha():
    return Image.open(ART / "frames/neutral.png").getchannel("A")


def presets():
    """Mask presets in anchor pixels, white = regenerate."""
    size = anchor_alpha().size
    eyes = Image.open(ART / "masks/eyes.png").convert("L")
    face = eyes.copy()
    ImageDraw.Draw(face).ellipse(FACE_ELLIPSE, fill=255)
    box = Image.new("L", size, 0)
    ImageDraw.Draw(box).rectangle((FACE_BOX[0] + 8, FACE_BOX[1] + 8, FACE_BOX[2] - 9, FACE_BOX[3] - 9), fill=255)
    return {
        "face": face,
        "facebox": box,
        "eyes": eyes,
        "mouth": Image.open(ART / "masks/mouth.png").convert("L"),
        "body": anchor_alpha(),
        "canvas": Image.new("L", size, 255),
        "none": Image.new("L", size, 0),
    }


def opaque(path):
    """A frame flattened on white with a solid alpha: what forge.py feeds the sampler,
    and paintable in ComfyUI's mask editor."""
    im = Image.open(path).convert("RGBA")
    flat = Image.new("RGBA", im.size, (255, 255, 255, 255))
    flat.alpha_composite(im)
    return flat


def upload(im, name):
    """ComfyUI 0.37 decodes inputs through PyAV, which reads a greyscale PNG's 255 as 254,
    so a mask goes up as RGB."""
    buf = io.BytesIO()
    (im.convert("RGB") if im.mode == "L" else im).save(buf, "PNG")
    boundary = uuid.uuid4().hex
    body = b"".join([
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"image\"; filename=\"{name}\"\r\n"
        "Content-Type: image/png\r\n\r\n".encode(), buf.getvalue(),
        f"\r\n--{boundary}\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--{boundary}--\r\n".encode()])
    req = urllib.request.Request(COMFY + "/upload/image", body, {"Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.load(r)["name"]


def api(path, data=None):
    req = urllib.request.Request(COMFY + path, json.dumps(data).encode() if data else None,
                                 {"Content-Type": "application/json"} if data else {})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.load(r)


def install(_):
    for name in registered_frames() + sorted(UNREGISTERED):
        print("uploaded", upload(opaque(ART / "frames" / f"{name}.png"), f"grungo_{name}.png"))
    for name, im in presets().items():
        print("uploaded", upload(im, f"grungo_mask_{name}.png"))
    print("uploaded", upload(anchor_alpha(), "grungo_alpha.png"))


def frame_input(frame):
    """(uploaded name, local RGBA) for a frame name or a PNG path."""
    path = Path(frame) if frame.endswith(".png") else ART / "frames" / f"{frame}.png"
    name = f"grungo_{path.stem}.png" if path.parent == ART / "frames" else f"grungo_in_{path.stem}.png"
    return upload(opaque(path), name), Image.open(path).convert("RGBA")


def queue(graph, values):
    by_title = {n["_meta"]["title"]: n for n in graph.values()}
    for knob, value in values.items():
        title, field = KNOBS[knob]
        by_title[title]["inputs"][field] = value
    return api("/prompt", {"prompt": graph, "client_id": uuid.uuid4().hex})["prompt_id"]


def wait(prompt_id):
    while True:
        hist = api(f"/history/{prompt_id}").get(prompt_id)
        if hist and hist.get("status", {}).get("completed"):
            return hist["outputs"]
        if hist and hist.get("status", {}).get("status_str") == "error":
            raise SystemExit(f"{prompt_id} failed: {json.dumps(hist['status'].get('messages'))[:2000]}")
        time.sleep(2)


def fetch(img):
    q = urllib.parse.urlencode({"filename": img["filename"], "subfolder": img["subfolder"], "type": img["type"]})
    with urllib.request.urlopen(f"{COMFY}/view?{q}", timeout=120) as r:
        return Image.open(io.BytesIO(r.read())).convert("RGBA")


def check(before, after, mask, grow):
    """Changed pixels, and how many fall outside the mask grown by `grow` (must be 0), and
    whether the alpha is the input's. A square dilation contains GrowMask's tapered one."""
    a, b = np.asarray(before), np.asarray(after)
    changed = (a != b).any(-1)
    zone = np.asarray(mask.filter(ImageFilter.MaxFilter(2 * grow + 1))) > 127 if grow else np.asarray(mask) > 127
    return int(changed.sum()), int((changed & ~zone).sum()), bool((a[..., 3] == b[..., 3]).all())


def sheet(before, outs, mask, path):
    ys, xs = np.nonzero(np.asarray(mask) > 127)
    box = (0, 0) + before.size if not len(xs) else (
        max(int(xs.min()) - 60, 0), max(int(ys.min()) - 60, 0),
        min(int(xs.max()) + 60, before.width), min(int(ys.max()) + 60, before.height))
    tiles = [before.crop(box)] + [o.crop(box) for _, o in outs]
    w, h = tiles[0].size
    out = Image.new("RGBA", (w * len(tiles), h + 24), (40, 40, 40, 255))
    draw = ImageDraw.Draw(out)
    for i, (tile, label) in enumerate(zip(tiles, ["input"] + [f"seed {s}" for s, _ in outs])):
        out.alpha_composite(tile, (i * w, 24))
        draw.text((i * w + 6, 6), label, fill=(230, 230, 230, 255))
    out.save(path)


def gen(a):
    graph = json.loads((HERE / "grungo_adjust_api.json").read_text())
    ids = {n["_meta"]["title"]: k for k, n in graph.items()}
    frame_name, before = frame_input(a.frame)
    refs = [frame_input(r)[0] for r in a.ref]
    mask = presets()[a.mask]
    values = {
        "frame": frame_name, "ref_a": refs[0], "ref_b": refs[-1], "mask": f"grungo_mask_{a.mask}.png",
        "positive": ", ".join(p for p in (a.subject, a.prompt, a.style) if p),
        "negative": ", ".join(p for p in (NEGATIVE, a.negative) if p),
    }
    values.update({k: getattr(a, k) for k in ("ipa_weight", "ipa_weight_type", "ipa_end_at", "grow", "steps",
                                               "cfg", "sampler", "scheduler", "denoise") if getattr(a, k) is not None})
    grow = values.get("grow", graph[ids["Grow mask"]]["inputs"]["expand"])
    OUT.mkdir(parents=True, exist_ok=True)
    stem = Path(a.frame).stem
    jobs = [(a.seed + i, queue(json.loads(json.dumps(graph)), {**values, "seed": a.seed + i})) for i in range(a.count)]
    print("positive:", values["positive"])
    outs = []
    for seed, pid in jobs:
        outputs = wait(pid)
        img = fetch(outputs[ids["Adjusted frame"]]["images"][0])
        path = OUT / f"{stem}_{a.mask}_s{seed}.png"
        img.save(path)
        if a.raw:
            raw = outputs[ids["Raw decode (new silhouettes only)"]]["images"][0]
            fetch(raw).save(OUT / f"{stem}_{a.mask}_s{seed}_raw.png")
        changed, outside, alpha_same = check(before, img, mask, grow)
        print(f"{path.relative_to(ROOT)}  changed {changed} px, outside mask+grow {outside}, alpha unchanged {alpha_same}")
        outs.append((seed, img))
    sheet(before, outs, mask, OUT / f"{stem}_{a.mask}_sheet.png")
    print("sheet", (OUT / f"{stem}_{a.mask}_sheet.png").relative_to(ROOT))


def adopt(a):
    new = Image.open(a.png).convert("RGBA")
    dest = ART / "frames" / f"{a.as_}.png"
    base = np.asarray(Image.open(dest).convert("RGBA"))
    edit = np.asarray(new)
    if a.as_ not in UNREGISTERED:
        assert (edit[..., 3] == np.asarray(anchor_alpha())).all(), "alpha differs from the anchor's: sprite_pack.py would refuse it"
    delta = (edit != base).any(-1)
    new.save(dest)
    print(f"{dest.relative_to(ROOT)}: {int(delta.sum())} px changed")
    if not a.everywhere:
        return
    for name in registered_frames():
        if name == a.as_:
            continue
        path = ART / "frames" / f"{name}.png"
        frame = np.asarray(Image.open(path).convert("RGBA")).copy()
        same = (frame == base).all(-1)
        frame[delta & same] = edit[delta & same]
        Image.fromarray(frame).save(path)
        print(f"{path.relative_to(ROOT)}: took {int((delta & same).sum())} px, kept its own {int((delta & ~same).sum())}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("install", help="upload frames, mask presets and the body alpha to ComfyUI").set_defaults(fn=install)
    g = sub.add_parser("gen", help="queue the workflow --count times")
    g.set_defaults(fn=gen)
    g.add_argument("--frame", default="neutral", help="a frame in pets/grungo/art/frames, or a PNG path")
    g.add_argument("--prompt", required=True, help="the change, between the subject and the style")
    g.add_argument("--negative", default="", help="added to the base negative")
    g.add_argument("--mask", default="face", choices=sorted(presets()))
    g.add_argument("--denoise", type=float)
    g.add_argument("--seed", type=int, default=1001)
    g.add_argument("--count", type=int, default=4)
    g.add_argument("--ref", nargs="+", default=["neutral"], help="one or two reference frames (names or paths)")
    g.add_argument("--subject", default=SUBJECT, help='"" for blank-canvas art')
    g.add_argument("--style", default=STYLE)
    g.add_argument("--ipa-weight", type=float)
    g.add_argument("--ipa-weight-type")
    g.add_argument("--ipa-end-at", type=float)
    g.add_argument("--grow", type=int)
    g.add_argument("--steps", type=int)
    g.add_argument("--cfg", type=float)
    g.add_argument("--sampler")
    g.add_argument("--scheduler")
    g.add_argument("--raw", action="store_true", help="also save the raw decode (new silhouettes)")
    d = sub.add_parser("adopt", help="copy an output into pets/grungo/art/frames")
    d.set_defaults(fn=adopt)
    d.add_argument("png", type=Path)
    d.add_argument("--as", dest="as_", required=True, help="the frame it replaces, e.g. neutral")
    d.add_argument("--everywhere", action="store_true",
                   help="a body edit: also apply it to every registered frame wherever that frame matches the old one")
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
