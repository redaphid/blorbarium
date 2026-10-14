#!/usr/bin/env python3
"""Fill ComfyUI's own queue with blorbarium art candidates, in one pass, so it runs with
nobody watching. Rerun it if ComfyUI restarts and loses its queue (it resubmits all).

    python tools/comfy_queue.py --se D:/Projects/_wt/sprexp-roster          # submit everything
    python tools/comfy_queue.py --se ... --dry                               # count only

Uses sprite-expressions' flows (inpaint.json, concept.json) and each species' anchor,
the same pipeline and style as the roster. Every image is saved by SaveImage under
blorbarium/<item>/<species>/<variant> in ComfyUI's output folder; roster/art-queue.json
says what each prefix is. Ordered most useful first. Stills only. tools/art_sheets.py
makes contact sheets from whatever has rendered.
"""
import argparse
import io
import json
import math
import sys
import urllib.request
import uuid
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
COMFY = "http://127.0.0.1:8188"
SPECIES = ["grungo", "mossby", "puffkin", "gilly", "mothwick", "mudge", "fizz", "hushwing", "tinker"]
NEG_SAFE = ("scary, creepy, gore, blood, wound, weapon, teeth, fangs, piercing, ring, stud, tattoo, jewelry, "
            "nipples, text, watermark")

EXPRESSIONS = {   # happy, sleepy and scared (alarmed) already exist for every species
    "sad": "(sad expression:1.4), (teary droopy eyes looking down:1.3), (small wobbly frown:1.3)",
    "angry": "(furious angry expression:1.4), (slanted angry brows:1.4), narrowed glaring eyes, (small shouting mouth:1.2)",
    "sick": "(sick unwell queasy expression:1.4), droopy tired half-closed eyes with dark bags, (green-tinged pale cheeks:1.2), wavy queasy mouth, a sweat drop",
    "hurt": "(hurt pained expression:1.4), (one eye squeezed shut wincing:1.4), clenched wobbly mouth, a little sticking plaster on the cheek",
    "fallen": "(knocked out:1.4), (cartoon X X eyes:1.5), tongue lolling out of the corner of the mouth, comically fainted",
}
POSES = {
    "windup": "(winding up to pounce:1.4), crouched low, leaning back, gathering energy, determined face, full body",
    "strike": "(lunging forward in a playful bonk:1.4), leaning hard into a charge, motion lines, full body",
    "guard": "(guarding:1.4), arms or wings raised in front to block, braced stance, focused face, full body",
    "flinch": "(flinching from a bonk:1.4), recoiling backwards, eyes squeezed shut, little stars, full body",
    "faint": "(fainted flat on its back:1.4), lying down, dizzy spiral eyes, little stars circling, full body",
    "victory": "(victory pose:1.4), cheering with arms or wings raised high, beaming proud face, sparkles, full body",
}
SCARS = {
    "claw": "three healed claw scratch scars", "scorch": "a healed singe scorch mark", "notch": "a small healed notch nick",
    "stitches": "a healed scar with cartoon stitches across it", "bite": "a healed arch of little bite nicks",
}
SPOTS = {   # head, shoulders, sides, limbs only (owner: never the chest or belly)
    "grungo": [(0.10, 0.60), (0.90, 0.62), (0.76, 0.12)], "mossby": [(0.15, 0.45), (0.88, 0.45), (0.50, 0.12)],
    "puffkin": [(0.13, 0.35), (0.87, 0.32), (0.07, 0.60)], "gilly": [(0.42, 0.24), (0.83, 0.68), (0.17, 0.68)],
    "mothwick": [(0.15, 0.12), (0.85, 0.15), (0.15, 0.55)], "mudge": [(0.45, 0.14), (0.90, 0.53), (0.10, 0.55)],
    "fizz": [(0.42, 0.17), (0.12, 0.50), (0.88, 0.52)], "hushwing": [(0.38, 0.08), (0.92, 0.55), (0.08, 0.60)],
    "tinker": [(0.15, 0.58), (0.85, 0.60), (0.12, 0.33)],
}
GRITTY = "(grittier, scrappier, more weathered look:1.3), a few patches and scuffs, muddy swamp-settler survivor, darker muted colours, like a frog settler of a sinking swamp colony"
MUDGE_ALTS = {
    "lungfish": "a chubby round lungfish creature standing upright on stubby fins, smooth slick blue-grey skin, wide frog-like fish mouth, no wings, no beak, no feathers",
    "mudpuppy": "a cute round mudpuppy salamander-fish with frilly red gills, sky blue spotted skin, sitting on its tail, no wings, no beak",
    "goby": "a round goofy mudskipper goby fish propped on two thick pectoral fins on wet mud, big bulging eyes on top of its head, scaly blue skin, a fish tail, no wings, no beak, no feathers",
    "tadfish": "a fat round blue fish creature with a big tail curled beside it, bubble eyes, tiny fin arms, on a mud puddle, no wings, no beak",
}
SPARES = {
    "newt": "a cute chubby orange fire newt creature with a spotted back and a long curled tail, standing upright",
    "beetle_crab": "a cute round hermit crab creature living in an old rusty tin can, two small claws, eye stalks",
    "mole": "a cute chubby velvet mole creature with big pink digging paws and tiny goggles, a swamp tunneler",
    "toadstool_bat": "a cute round fruit bat creature with big ears and leathery wings, hanging upside down from a toadstool",
    "slug": "a cute glowing sea-slug creature with frilly coloured fringes, plump and round, a swamp drifter",
}
FRIEND_PAIRS = [("grungo", "gilly"), ("mossby", "fizz"), ("puffkin", "mothwick"), ("mudge", "hushwing"), ("tinker", "grungo"),
                ("gilly", "mudge"), ("fizz", "tinker"), ("hushwing", "mothwick"), ("mossby", "puffkin"), ("grungo", "hushwing")]


def upload(data, name):
    b = uuid.uuid4().hex
    body = (f"--{b}\r\nContent-Disposition: form-data; name=\"image\"; filename=\"{name}\"\r\nContent-Type: image/png\r\n\r\n").encode() \
        + data + f"\r\n--{b}\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--{b}--\r\n".encode()
    req = urllib.request.Request(COMFY + "/upload/image", body, {"Content-Type": "multipart/form-data; boundary=" + b})
    return json.load(urllib.request.urlopen(req, timeout=120))["name"]


def png(im):
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return buf.getvalue()


def fill(node, v):
    if isinstance(node, dict):
        return {k: fill(x, v) for k, x in node.items()}
    if isinstance(node, list):
        return [fill(x, v) for x in node]
    if isinstance(node, str) and node.startswith("{{") and node.endswith("}}") and node.count("{{") == 1:
        return v[node[2:-2]]
    return node


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--se", required=True, help="a sprite-expressions checkout with the roster characters")
    ap.add_argument("--dry", action="store_true")
    a = ap.parse_args()
    se = Path(a.se)
    flows = {n: {k: x for k, x in json.loads((se / f"flows/{n}.json").read_text()).items() if not k.startswith("_")}
             for n in ("inpaint", "concept")}
    jobs = []   # (flow, values, prefix, about)
    up = {}

    def U(key, maker):
        if key not in up:
            up[key] = "dry.png" if a.dry else upload(png(maker()), f"blorb-q-{key}.png")
        return up[key]

    cfg = {s: json.loads((se / f"characters/{s}/character.json").read_text()) for s in SPECIES}
    anchor = {s: se / f"characters/{s}/anchor-raw.png" for s in SPECIES}
    alpha = {s: Image.open(se / f"characters/{s}/anchor.png").getchannel("A") for s in SPECIES}

    def base(s, extra=""):
        v = dict(cfg[s]["vars"])
        v.setdefault("subject", "")
        v["ref"] = U(f"{s}-ref", lambda: Image.open(anchor[s]).convert("RGB"))
        v["init"] = v["ref"]
        return v

    def add(flow, v, positive, negative, prefix, about, seeds, denoise=None):
        for i in range(seeds):
            w = dict(v)
            w.update(positive=positive, negative=negative, prefix=prefix, seed=7000 + i * 7919 % 100000)
            if denoise is not None:
                w["denoise"] = denoise
            jobs.append((flow, w, prefix, about))

    style = cfg["grungo"]["vars"]["style"]
    neg = cfg["grungo"]["vars"]["negative_base"] + ", " + NEG_SAFE
    # 1. expressions: the face re-inpainted on the anchor
    for e, pos in EXPRESSIONS.items():
        for s in SPECIES:
            v = base(s)
            v["mask"] = U(f"{s}-face", lambda s=s: Image.open(se / f"characters/{s}/masks/face.png").convert("L"))
            add("inpaint", v, f"{v['subject']}, {pos}, {v.get('style', style)}", neg, f"blorbarium/expressions/{s}/{e}",
                f"{s} face: {e}", 24, 0.85)
    # 2. duel poses: the whole body redrawn from the anchor, identity held by IP-Adapter
    full = lambda: Image.new("L", (1024, 1024), 255)
    for p, pos in POSES.items():
        for s in SPECIES:
            v = base(s)
            v.update(mask=U("full", full), ipa_weight=0.8, ipa_end_at=0.9)
            add("inpaint", v, f"{v['subject']}, {pos}, {v.get('style', style)}", neg, f"blorbarium/poses/{s}/{p}",
                f"{s} duel pose: {p}", 16, 0.78)
    # 3. eggs: one per species, and friend eggs mixing two parents
    for s in SPECIES:
        v = base(s)
        v.update(ipa_weight=0.5, ipa_end_at=0.7)
        add("concept", v, f"(a single round glossy translucent jelly egg:1.4), a tiny curled baby {v['subject']} visible inside, cute, {style}",
            neg, f"blorbarium/eggs/{s}/egg", f"{s} egg", 12)
    for x, y in FRIEND_PAIRS:
        v = base(x)
        add("concept", v, f"(a single round glossy jelly egg:1.4), inside it a tiny baby that is half {cfg[x]['vars']['subject']} and half "
            f"{cfg[y]['vars']['subject']}, mixed colours of both, cute, {style}", neg, f"blorbarium/eggs/friend/{x}-{y}",
            f"friend egg of {x} and {y}", 8)
    # 4. scars on head, shoulders and limbs; war paint on the cheeks
    for s in SPECIES:
        box = alpha[s].getbbox()

        def spots(s=s, box=box):
            m = Image.new("L", (1024, 1024), 0)
            d = ImageDraw.Draw(m)
            for u, w in SPOTS[s][:2]:
                x, y = box[0] + u * (box[2] - box[0]), box[1] + w * (box[3] - box[1])
                d.ellipse([x - 55, y - 55, x + 55, y + 55], fill=255)
            return m
        for k, pos in SCARS.items():
            v = base(s)
            v["mask"] = U(f"{s}-spots", spots)
            add("inpaint", v, f"{v['subject']}, ({pos} on the arm and head:1.3), healed, cartoon, {v.get('style', style)}", neg,
                f"blorbarium/scars/{s}/{k}", f"{s} scar: {k}, on limbs and head only", 8, 0.7)
        v = base(s)
        v["mask"] = U(f"{s}-face", lambda s=s: Image.open(se / f"characters/{s}/masks/face.png").convert("L"))
        add("inpaint", v, f"{v['subject']}, (bold swamp-mud war paint stripes on the cheeks and forehead:1.4), fierce, {v.get('style', style)}",
            neg, f"blorbarium/warpaint/{s}/warpaint", f"{s} war paint on the face", 8, 0.7)
    # 5. grittier takes (style from grungo) and Mudge that is not a bird
    for s in SPECIES[1:]:
        v = base("grungo")
        v.update(ipa_weight=0.6, ipa_end_at=0.8)
        add("concept", v, f"{cfg[s]['vars']['subject']}, {GRITTY}, {style}", neg, f"blorbarium/gritty/{s}/gritty", f"{s}, grittier", 24)
    for n, pos in MUDGE_ALTS.items():
        v = base("grungo")
        v.update(ipa_weight=0.35, ipa_end_at=0.5)
        add("concept", v, f"{pos}, {style}", neg + ", bird, wings, beak, feathers", f"blorbarium/mudge-alt/{n}", f"Mudge alternative: {n}", 24)
    # 6. spare species
    for n, pos in SPARES.items():
        v = base("grungo")
        v.update(ipa_weight=0.35, ipa_end_at=0.5)
        add("concept", v, f"{pos}, {style}", neg, f"blorbarium/spares/{n}", f"spare species: {n}", 24)

    manifest = {}
    for flow, v, prefix, about in jobs:
        manifest.setdefault(prefix, {"about": about, "flow": flow, "count": 0, "positive": v["positive"]})["count"] += 1
    (ROOT / "roster/art-queue.json").write_text(json.dumps(manifest, indent=1) + "\n", newline="\n")
    print(len(jobs), "jobs,", len(manifest), "prefixes")
    if a.dry:
        return
    for i, (flow, v, prefix, about) in enumerate(jobs):
        g = fill(flows[flow], v)
        body = json.dumps({"prompt": g, "client_id": "blorb-queue"}).encode()
        urllib.request.urlopen(urllib.request.Request(COMFY + "/prompt", body, {"Content-Type": "application/json"}), timeout=120).read()
        if i % 200 == 0:
            print("queued", i, prefix, flush=True)
    print("queued all", len(jobs))


if __name__ == "__main__":
    sys.exit(main())
