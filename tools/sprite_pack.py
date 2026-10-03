#!/usr/bin/env python3
"""Grungo's sprite pack: the picked 1024 px frames in pets/grungo/art/ become
pets/grungo/grungo_pack.h, one region-tagged palette plus run-length frames.

    python3 tools/sprite_pack.py                  # write the header, print its sha256
    python3 tools/sprite_pack.py --preview DIR    # also decode the header into preview PNGs

Needs Pillow and numpy. Deterministic: no random numbers, stable sorts only, so two
runs write the same bytes.

Every frame, drawn or picked, is a Sprite: device-scale RGB cells, a region per cell
(regions.def ids, INVARIANT for the outline and fixed props, CLEAR for transparent),
an origin and eye anchors. Picked art and hi-res drawings both reach device scale
through one downscaler (CELL source pixels per device pixel), and one quantiser
turns every Sprite into palette indices.
"""
import argparse
import hashlib
import json
import math
import re
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "pets/grungo/art"
OUT = ROOT / "pets/grungo/grungo_pack.h"
DEFS = ROOT / "lib/blorb/include/blorb/defs"
COMMAND = "python3 tools/sprite_pack.py"

CELL = 5                      # the 840 px body bbox is 168 px on device, so he fills the round panel
GRID = (227, 80)              # the body bbox's top-left in the art; cells align to it
BODY_CELLS = (-(-569 // CELL), 840 // CELL)   # the body bbox, 569 x 840 art pixels
INVARIANT, CLEAR = 255, 254

ART_NAME = {"croak": "meow", "asleep": "sleep"}   # engine face -> picked frame
EYEBALLS = [(408, 207), (612, 207)]               # the eye mask's ellipse centres (art-plan section 5)
EYEBALL_R = 47
IRIS_TO_EYEBALL = 47 / 32
MOVED_EYES = ("happy", "meow", "annoyed", "alarmed")
MOUTH_Y = 255                 # a saturated red below this is the open mouth, not an iris
BELLY_SEED, BELLY_AXES = (487, 622), (175, 140)

# Palette slots per region; the total plus index 0 must stay within 256.
BUDGET = {"skin": 60, "belly": 20, "cloak": 44, "eye": 32, "mouth": 24, "glow": 24, "shell": 14, INVARIANT: 37}

# How far palette genes may move each region (Tint units: hue in 1/256 turn, sat and
# val 128 = x1), so he stays grungo. Skin spans at least the engine's forced-change
# reach (mutate.cpp: hue -24 to +40, val 96 to 160), so a sibling's step is never swallowed.
BANDS = {
    "skin": (-24, 40, 96, 176, 96, 160),    # ochre olive to blue-green, frog greens only
    "belly": (-8, 8, 104, 152, 112, 144),
    "cloak": (-16, 36, 80, 200, 80, 176),   # rust through brown to moss
    "eye": (-8, 30, 96, 176, 96, 192),      # red-brown to amber and gold
    "mouth": (-6, 6, 104, 152, 112, 144),
    "glow": (-20, 20, 112, 160, 112, 160),  # around teal, never dim enough to hide
    "shell": (-24, 40, 96, 176, 96, 160),   # the egg follows the skin's range
}


def read_def(name, macro):
    rows = re.findall(rf"^{macro}\((\d+),\s*(\w+)", (DEFS / name).read_text(), re.M)
    return [(int(i), n) for i, n in rows]


REGION = {n: i for i, n in read_def("regions.def", "BLORB_REGION")}
EXPRESSIONS = read_def("expressions.def", "BLORB_EXPR")
REGION_ORDER = [INVARIANT] + [REGION[n] for n in sorted(REGION, key=REGION.get)]


@dataclass
class Sprite:
    rgb: np.ndarray            # (h, w, 3) uint8
    region: np.ndarray         # (h, w) uint8: a region id, INVARIANT or CLEAR
    origin: tuple
    eyes: list = field(default_factory=list)   # [(x, y, r)] in this sprite's pixels

    @property
    def size(self):
        return self.region.shape[1], self.region.shape[0]


# ---- colour ------------------------------------------------------------------------

def to_hsv(rgb):
    c = rgb.astype(np.float64) / 255
    r, g, b = c[..., 0], c[..., 1], c[..., 2]
    v = c.max(-1)
    d = v - c.min(-1)
    s = np.where(v > 0, d / np.where(v > 0, v, 1), 0)
    dd = np.where(d > 0, d, 1)
    h = np.select([d == 0, v == r, v == g], [0, ((g - b) / dd) % 6, (b - r) / dd + 2], (r - g) / dd + 4) * 60
    return h, s, v


def from_hsv(h, s, v):
    h6 = (h % 360) / 60
    i = np.floor(h6).astype(int) % 6
    f = h6 - np.floor(h6)
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    r = np.choose(i, [v, q, p, p, t, v])
    g = np.choose(i, [t, v, v, q, p, p])
    b = np.choose(i, [p, p, t, v, v, q])
    return np.clip(np.round(np.stack([r, g, b], -1) * 255), 0, 255).astype(np.uint8)


# ---- labelling the picked art --------------------------------------------------------

def disc(shape, cx, cy, rx, ry=None):
    ry = rx if ry is None else ry
    yy, xx = np.mgrid[0:shape[0], 0:shape[1]]
    return ((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 <= 1


def iris_mask(rgb, eyes):
    r, g, b = (rgb[..., i].astype(int) for i in range(3))
    near = np.zeros(rgb.shape[:2], bool)
    for e in eyes:
        near |= disc(near.shape, e["x"], e["y"], 1.3 * e["r"])
    return near & (r > g + 40) & (g < b + 15) & (g < 110)


def fix_iris(frames, eyes):
    """Shift each frame's iris to neutral's red-brown in hue, saturation and value, so
    the eye entries recolour alike in every frame. Lidded and teal frames have too
    little iris to measure and are left alone."""
    def stats(name):
        m = iris_mask(frames[name], eyes[name]["eyes"])
        return m, [np.median(c[m]) for c in to_hsv(frames[name])] if m.sum() >= 100 else None
    _, (h0, s0, v0) = stats("neutral")
    fixed = {}
    for name, rgb in frames.items():
        m, measured = stats(name)
        if measured is None:
            continue
        h, s, v = measured
        if abs(s - s0) < 0.08 and abs(v - v0) < 0.06:
            continue
        hh, ss, vv = to_hsv(rgb)
        out = rgb.copy()
        out[m] = from_hsv(hh[m] + h0 - h, np.clip(ss[m] * s0 / s, 0, 1), np.clip(vv[m] * v0 / v, 0, 1))
        frames[name] = out
        fixed[name] = (round(float(s), 2), round(float(v), 2))
    return fixed


def grow_into(lab, todo):
    """Unlabelled pixels take their nearest labelled neighbour's region, by repeated
    four-way dilation in a fixed neighbour order."""
    lab = lab.copy()
    while todo.any():
        moved = False
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            src = np.roll(lab, (dy, dx), (0, 1))
            ok = np.roll(~todo, (dy, dx), (0, 1)) & (src != CLEAR) & todo
            lab[ok] = src[ok]
            todo &= ~ok
            moved |= ok.any()
        if not moved:
            break
    return lab


def erode(m, n):
    for _ in range(n):
        m = m & np.roll(m, 1, 0) & np.roll(m, -1, 0) & np.roll(m, 1, 1) & np.roll(m, -1, 1)
    return m


def flood(seed, allowed):
    reach = np.zeros_like(allowed)
    reach[seed[1], seed[0]] = True
    while True:
        nxt = reach.copy()
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            nxt |= np.roll(reach, (dy, dx), (0, 1))
        nxt &= allowed
        if (nxt == reach).all():
            return reach
        reach = nxt


def label(rgb, alpha, eye_zone, sclera_zone, mouth_zone):
    """regions.py's rules, first match wins: glow, eye, mouth, outline, cloak, skin
    (belly where it is the belly's enclosed green), then the rest by nearest neighbour.
    The keyed-white fringe on the silhouette counts as clear."""
    r, g, b = (rgb[..., i].astype(int) for i in range(3))
    h, s, v = to_hsv(rgb)
    y = np.mgrid[0:rgb.shape[0], 0:rgb.shape[1]][0]
    green = (h >= 55) & (h <= 150) & (s > 0.15)
    pink = ((h >= 320) | (h <= 10)) & (s > 0.2) & (v > 0.55)
    red = ((h >= 340) | (h <= 15)) & (s > 0.5) & (v > 0.25) & (y > MOUTH_Y)
    cloak = (h >= 10) & (h <= 50) & (v < 0.5)
    rules = [
        ((g > 150) & (b > 120) & (r < 0.7 * g), REGION["glow"]),
        ((eye_zone & ~(green & (v > 0.35)) & ~(cloak & (h >= 18) & (v > 0.12)))
         | (sclera_zone & (s < 0.25) & (v > 0.45)), REGION["eye"]),
        (mouth_zone & (pink | red), REGION["mouth"]),
        (v < 0.16, INVARIANT),
        (cloak, REGION["cloak"]),
        (green | ((v > 0.6) & (s < 0.35)), REGION["skin"]),
    ]
    lab = np.select([c for c, _ in rules], [k for _, k in rules], CLEAR).astype(np.uint8)
    opaque = alpha > 0
    opaque &= ~(opaque & ~erode(opaque, 4) & (s < 0.2) & (v > 0.75))
    lab = grow_into(np.where(opaque, lab, CLEAR).astype(np.uint8), opaque & (lab == CLEAR))
    lab[~opaque] = CLEAR
    skin = lab == REGION["skin"]
    belly = flood(BELLY_SEED, skin & disc(skin.shape, *BELLY_SEED, *BELLY_AXES))
    lab[belly] = REGION["belly"]
    return lab


def eye_zones(name, eyes, eye_mask):
    shape = eye_mask.shape
    if name not in MOVED_EYES:
        return eye_mask, np.zeros(shape, bool)
    zone, sclera = np.zeros(shape, bool), np.zeros(shape, bool)
    for e in eyes:
        zone |= disc(shape, e["x"], e["y"], e["r"] * IRIS_TO_EYEBALL)
        sclera |= disc(shape, e["x"], e["y"], 1.9 * e["r"])
    return zone, sclera


# ---- device scale ------------------------------------------------------------------

WEIGHT = {INVARIANT: 2.0, REGION["eye"]: 1.5, REGION["glow"]: 2.0, REGION["mouth"]: 1.5}


def downscale(rgb, lab, x0, y0, cols, rows):
    """Each CELL x CELL block becomes one pixel: opaque when at least half its pixels
    are, labelled by a vote that favours the outline and the small features (so thin
    ink stays continuous and the eyes and mouth survive), coloured by the mean of the
    winning region's pixels (so no colour bleeds across a region edge)."""
    H, W = lab.shape
    pad_l, pad_t = max(0, -x0), max(0, -y0)
    pad_r, pad_b = max(0, x0 + cols * CELL - W), max(0, y0 + rows * CELL - H)
    lab = np.pad(lab, ((pad_t, pad_b), (pad_l, pad_r)), constant_values=CLEAR)
    rgb = np.pad(rgb, ((pad_t, pad_b), (pad_l, pad_r), (0, 0)))
    x0, y0 = x0 + pad_l, y0 + pad_t
    lb = lab[y0:y0 + rows * CELL, x0:x0 + cols * CELL].reshape(rows, CELL, cols, CELL).transpose(0, 2, 1, 3).reshape(rows, cols, -1)
    cb = rgb[y0:y0 + rows * CELL, x0:x0 + cols * CELL].reshape(rows, CELL, cols, CELL, 3).transpose(0, 2, 1, 3, 4).reshape(rows, cols, -1, 3)
    labels = [INVARIANT] + sorted(REGION.values())
    votes = np.stack([(lb == k).sum(-1) * WEIGHT.get(k, 1.0) for k in labels], -1)
    win = np.array(labels, np.uint8)[votes.argmax(-1)]
    pick = lb == win[..., None]
    colour = (cb * pick[..., None]).sum(2) / np.maximum(pick.sum(2), 1)[..., None]
    opaque = (lb != CLEAR).sum(-1) * 2 >= CELL * CELL
    return np.round(colour).astype(np.uint8), np.where(opaque, win, CLEAR).astype(np.uint8)


def to_cell(x, y, grid):
    return int((x - grid[0]) // CELL), int((y - grid[1]) // CELL)


# ---- drawing (egg, items, bog) at source scale ---------------------------------------

class Sketch:
    """A hi-res drawing in colour and region at once, drawn in device-pixel units and
    rendered CELL times larger, so it reaches device scale through the same
    downscaler as the picked art."""

    def __init__(self, cols, rows):
        self.cols, self.rows = cols, rows
        self.rgb = np.zeros((rows * CELL, cols * CELL, 3), np.uint8)
        self.lab = np.full((rows * CELL, cols * CELL), CLEAR, np.uint8)

    def mask(self, kind, *args, width=1.0):
        im = Image.new("L", (self.cols * CELL, self.rows * CELL))
        d = ImageDraw.Draw(im)
        s = lambda pts: [(x * CELL, y * CELL) for x, y in pts]
        if kind == "ellipse":
            x0, y0, x1, y1 = args
            d.ellipse([x0 * CELL, y0 * CELL, x1 * CELL - 1, y1 * CELL - 1], fill=255)
        elif kind == "polygon":
            d.polygon(s(args[0]), fill=255)
        elif kind == "line":
            d.line(s(args[0]), fill=255, width=round(width * CELL), joint="curve")
        return np.asarray(im) > 127

    def fill(self, m, colour, region):
        self.rgb[m] = colour
        self.lab[m] = region

    def outline(self, m, width=1.0, colour=(24, 20, 16)):
        """Ink the rim of shape m, `width` device pixels thick."""
        self.fill(m & ~erode(m, round(width * CELL)), colour, INVARIANT)

    def sprite(self, origin, eyes=()):
        rgb, lab = downscale(self.rgb, self.lab, 0, 0, self.cols, self.rows)
        return Sprite(rgb, lab, origin, list(eyes))


EGG_ROWS = 150                          # the egg fills the panel with room for its rock inside the round mask
EGG_STEP = EGG_ROWS / 72                 # EGG_CRACKS were drawn on a 72 px egg
EGG_FROGLET = (510, 692), (82, 106)     # the froglet in the hollow, in the art's pixels
EGG_CRACKS = [[(14, 18), (18, 14), (22, 18), (26, 14)], [(26, 14), (30, 19), (34, 15), (38, 19)],
              [(30, 19), (31, 24)]]     # device pixels, across the jelly's dome


def label_egg(rgb, alpha, eyes, froglet):
    """The egg's own rules, first match wins: the froglet's teal eyes and their shine
    are glow, ink is the outline, the brown hollow is cloak, the froglet's green is
    skin (so a clutch shows each child's skin genes), and the jelly is shell."""
    r, g, b = (rgb[..., i].astype(int) for i in range(3))
    h, s, v = to_hsv(rgb)
    eye_zone = np.zeros(alpha.shape, bool)
    for x, y, er in eyes:
        eye_zone |= disc(alpha.shape, x, y, 1.6 * er)
    green = (h >= 55) & (h <= 150) & (s > 0.15)
    brown = ((h <= 50) | (h >= 330)) & (s > 0.25)
    (fx, fy), (frx, fry) = froglet
    frog = flood((round(fx), round(fy + 0.4 * fry)), green & disc(alpha.shape, fx, fy, frx, fry))
    hollow = disc(alpha.shape, fx, fy, 1.35 * frx, 1.35 * fry)
    rules = [
        (eye_zone & (((g > 150) & (b > 120) & (r < 0.7 * g)) | ((s < 0.25) & (v > 0.8))), REGION["glow"]),
        (hollow & brown & (v >= 0.06), REGION["cloak"]),   # the hollow is dark: cloak genes must still show there
        ((v < 0.16) | brown, INVARIANT),                    # brown outside the hollow is the ink ring's soft edge
        (frog, REGION["skin"]),
    ]
    lab = np.select([c for c, _ in rules], [k for _, k in rules], REGION["shell"]).astype(np.uint8)
    lab[alpha == 0] = CLEAR
    return lab


def egg_art():
    """The picked egg (sprite-expressions raw/egg, `egg` in eyes.json) at device scale,
    EGG_ROWS tall. It is scaled first so the shared downscaler's CELL lands on that height."""
    eyes = json.loads((ART / "eyes.json").read_text())["egg"]["eyes"]
    im = Image.open(ART / "frames/egg.png").convert("RGBA")
    k = EGG_ROWS * CELL / (im.getbbox()[3] - im.getbbox()[1])
    im = im.convert("RGBa").resize((round(im.width * k), round(im.height * k)), Image.LANCZOS).convert("RGBA")
    a = np.asarray(im)
    rgb, alpha = a[..., :3].copy(), a[..., 3]
    (fx, fy), (frx, fry) = EGG_FROGLET
    lab = label_egg(rgb, alpha, [(e["x"] * k, e["y"] * k, e["r"] * k) for e in eyes], ((fx * k, fy * k), (frx * k, fry * k)))
    ys, xs = np.nonzero(alpha)
    cols = -(-(int(xs.max()) + 1 - int(xs.min())) // CELL)
    gx = (int(xs.min()) + int(xs.max()) + 1) // 2 - cols * CELL // 2
    gy = int(ys.max()) + 1 - EGG_ROWS * CELL
    rgb, lab = downscale(rgb, lab, gx, gy, cols, EGG_ROWS)
    anchors = [(int((e["x"] * k - gx) // CELL), int((e["y"] * k - gy) // CELL), max(1, round(e["r"] * k / CELL))) for e in eyes]
    return Sprite(rgb, lab, (cols // 2, EGG_ROWS - 1), anchors)


def egg(art, stage):
    """The egg with `stage` of its cracks inked across the dome: whole, cracking, hatching."""
    s = Sprite(art.rgb.copy(), art.region.copy(), art.origin, list(art.eyes))
    w, h = s.size
    jelly = s.region == REGION["shell"]
    for line in EGG_CRACKS[:stage]:
        for dy, colour, region in ((1, (196, 222, 150), REGION["shell"]), (0, (24, 20, 16), INVARIANT)):
            im = Image.new("L", (w, h))
            ImageDraw.Draw(im).line([(x * EGG_STEP, (y + dy) * EGG_STEP) for x, y in line], fill=255, width=round(EGG_STEP))
            m = (np.asarray(im) > 0) & jelly
            s.rgb[m], s.region[m] = colour, region
    return s


def fly(body, stripe, wing, eye):
    k = Sketch(15, 13)
    wings = k.mask("ellipse", 0.5, 0.5, 7.5, 6.5) | k.mask("ellipse", 7.5, 0.5, 14.5, 6.5)
    k.fill(wings, wing, INVARIANT)
    k.outline(wings, 0.7)
    legs = k.mask("line", [(5, 10), (3.5, 12.6)], width=0.8) | k.mask("line", [(10, 10), (11.5, 12.6)], width=0.8)
    k.fill(legs, (24, 20, 16), INVARIANT)
    trunk = k.mask("ellipse", 2.5, 3.5, 12.5, 12)
    k.fill(trunk, body, INVARIANT)
    for y in (8.2, 10.0):
        k.fill(trunk & k.mask("line", [(2, y), (13, y)], width=0.8), stripe, INVARIANT)
    for x in (3.6, 7.6):
        e = k.mask("ellipse", x, 3.2, x + 3.8, 7.0)
        k.fill(e, eye, INVARIANT)
        k.fill(k.mask("ellipse", x + 0.8, 3.8, x + 2.0, 5.0), (240, 200, 190), INVARIANT)
    k.outline(trunk, 0.8)
    return k.sprite((7, 12))


def bubble():
    k = Sketch(12, 12)
    ball = k.mask("ellipse", 0.5, 0.5, 11.5, 11.5)
    k.fill(ball, (96, 200, 196), INVARIANT)
    k.fill(ball & ~k.mask("ellipse", -0.5, -0.5, 10, 10), (56, 140, 146), INVARIANT)
    k.fill(ball & ~k.mask("ellipse", 2, -1, 13, 10) & k.mask("ellipse", 0, 4, 9, 12), (236, 170, 214), INVARIANT)
    k.fill(k.mask("ellipse", 2.8, 2.4, 5.8, 5.2), (240, 255, 252), INVARIANT)
    k.outline(ball, 0.8)
    return k.sprite((6, 11))


def remains(sleep_rgb, sleep_lab, feet):
    """The asleep frame settled into the bog to his chest: a pool behind him, a soft
    waterline across him, a few spore glints. Cosy, like a warm bath."""
    grid = (GRID[0] - 49, GRID[1])              # 49 art pixels of pool beyond the body each side
    cols, rows = -(-(569 + 98) // CELL), -(-574 // CELL)   # down to the pool, 574 art pixels
    H, W = rows * CELL, cols * CELL
    k = Sketch(cols, rows)
    yy, xx = np.mgrid[0:H, 0:W]
    pool = disc((H, W), 511 - grid[0], 606 - grid[1], 330, 46)
    k.fill(pool, (50, 64, 42), INVARIANT)
    k.fill(pool & ~disc((H, W), 511 - grid[0], 600 - grid[1], 300, 34), (70, 92, 58), INVARIANT)
    k.outline(pool, 1.0)
    src = (slice(grid[1], grid[1] + H), slice(grid[0], grid[0] + W))
    body_rgb, body_lab = sleep_rgb[src], sleep_lab[src]
    water = 600 - grid[1] + 5 * np.sin((xx + grid[0]) * 2 * math.pi / 130)
    above = (body_lab != CLEAR) & (yy <= water)
    k.fill(above, body_rgb[above], 0)
    k.lab[above] = body_lab[above]
    near = (np.abs(yy - water) <= 5) & (np.abs(xx + grid[0] - 511) <= 300)
    k.fill(near & (pool | above), (112, 146, 104), INVARIANT)
    for gx, gy in ((300, 628), (690, 622), (430, 640)):
        k.fill(disc((H, W), gx - grid[0], gy - grid[1], 6), (110, 214, 190), INVARIANT)
    return k.sprite(to_cell(*feet, grid))


# ---- palette -------------------------------------------------------------------------

def median_cut(colours, weights, k):
    boxes = [np.arange(len(colours))]
    while len(boxes) < k:
        def sse(b):
            c, w = colours[b].astype(float), weights[b]
            return float((w[:, None] * (c - np.average(c, axis=0, weights=w)) ** 2).sum()) if len(b) > 1 else -1.0
        scores = [sse(b) for b in boxes]
        i = int(np.argmax(scores))
        if scores[i] <= 0:
            break
        b = boxes[i]
        c = colours[b]
        ch = int(np.argmax(c.max(0).astype(int) - c.min(0)))
        order = b[np.lexsort((c[:, (ch + 2) % 3], c[:, (ch + 1) % 3], c[:, ch]))]
        cw = np.cumsum(weights[order])
        cut = int(np.clip(np.searchsorted(cw, cw[-1] / 2) + 1, 1, len(order) - 1))
        boxes[i:i + 1] = [order[:cut], order[cut:]]
    centres = np.array([np.average(colours[b], axis=0, weights=weights[b]) for b in boxes])
    for _ in range(6):
        near = nearest(colours, centres)
        for j in range(len(centres)):
            m = near == j
            if m.any():
                centres[j] = np.average(colours[m], axis=0, weights=weights[m])
    return np.unique(np.round(centres).astype(np.uint8), axis=0)


def nearest(colours, centres):
    d = ((colours[:, None, :].astype(int) - centres[None, :, :].astype(int)) ** 2).sum(-1)
    return d.argmin(1)


def build_palette(sprites):
    """One palette for every sprite: each region quantised on its own within BUDGET,
    entries ordered by region then brightness. Index 0 is clear."""
    entries = [(0, 0, 0, INVARIANT)]
    by_region = {}
    for reg in REGION_ORDER:
        cells = np.concatenate([s.rgb[s.region == reg] for s in sprites])
        if not len(cells):
            continue
        colours, counts = np.unique(cells, axis=0, return_counts=True)
        name = next((n for n, i in REGION.items() if i == reg), INVARIANT)
        chosen = colours if len(colours) <= BUDGET[name] else median_cut(colours, counts.astype(float), BUDGET[name])
        chosen = sorted((tuple(map(int, c)) for c in chosen), key=lambda c: (299 * c[0] + 587 * c[1] + 114 * c[2], c))
        by_region[reg] = (len(entries), np.array(chosen, np.uint8))
        entries += [(int(r), int(g), int(b), reg) for r, g, b in chosen]
    assert len(entries) <= 256, f"palette has {len(entries)} entries"
    return entries, by_region


def index(sprite, by_region):
    idx = np.zeros(sprite.region.shape, np.uint8)
    for reg, (base, chosen) in by_region.items():
        m = sprite.region == reg
        if m.any():
            idx[m] = base + nearest(sprite.rgb[m], chosen)
    return idx


def rle(idx):
    out = bytearray()
    for row in idx:
        x = 0
        while x < len(row):
            n = 1
            while x + n < len(row) and row[x + n] == row[x] and n < 255:
                n += 1
            out += bytes((n, int(row[x])))
            x += n
    return bytes(out)


def unrle(data, w, h):
    rows, i = [], 0
    for _ in range(h):
        row = []
        while len(row) < w:
            row += [data[i + 1]] * data[i]
            i += 2
        assert len(row) == w, "a run crosses the row end"
        rows.append(row)
    assert i == len(data), "trailing bytes after the last row"
    return np.array(rows, np.uint8)


# ---- the pack ------------------------------------------------------------------------

def load_art():
    eyes = json.loads((ART / "eyes.json").read_text())
    frames, alpha = {}, None
    for name in sorted({ART_NAME.get(n, n) for _, n in EXPRESSIONS}):
        a = np.asarray(Image.open(ART / "frames" / f"{name}.png").convert("RGBA"))
        frames[name] = a[..., :3].copy()
        alpha = a[..., 3] if alpha is None else alpha
        assert (a[..., 3] == alpha).all(), f"{name} is not registered on the anchor's alpha"
    eye_mask = np.asarray(Image.open(ART / "masks/eyes.png").convert("L")) > 127
    mouth = np.asarray(Image.open(ART / "masks/mouth.png").convert("L")) > 127
    ys, xs = np.nonzero(mouth)
    mouth_zone = disc(mouth.shape, (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2,
                      (xs.max() - xs.min()) / 2 + 14, (ys.max() - ys.min()) / 2 + 14)
    return frames, alpha, eyes, eye_mask, mouth_zone


def build():
    frames, alpha, eyes, eye_mask, mouth_zone = load_art()
    fixed = fix_iris(frames, eyes)
    labels = {n: label(rgb, alpha, *eye_zones(n, eyes[n]["eyes"], eye_mask), mouth_zone) for n, rgb in frames.items()}
    ys, xs = np.nonzero(alpha)
    bottom = xs[ys == ys.max()]
    feet = ((int(bottom.min()) + int(bottom.max())) // 2, int(ys.max()))

    full = {}
    for n in frames:
        rgb, lab = downscale(frames[n], labels[n], *GRID, *BODY_CELLS)
        full[n] = Sprite(rgb, lab, to_cell(*feet, GRID))
    body = full["neutral"]
    body.eyes = [(*to_cell(x, y, GRID), round(EYEBALL_R / CELL)) for x, y in EYEBALLS]

    changed = np.zeros(body.region.shape, bool)
    for s in full.values():
        changed |= (s.region != body.region) | (s.rgb != body.rgb).any(-1)
    cy, cx = np.nonzero(changed)
    x0, y0 = int(cx.min()) - 1, int(cy.min()) - 1
    x1, y1 = int(cx.max()) + 2, int(cy.max()) + 2
    faces = {}
    for _, expr in EXPRESSIONS:
        s = full[ART_NAME.get(expr, expr)]
        art = ART_NAME.get(expr, expr)
        own = [(*to_cell(e["x"], e["y"], GRID), round(e["r"] * IRIS_TO_EYEBALL / CELL)) for e in eyes[art]["eyes"]]
        faces[expr] = Sprite(s.rgb[y0:y1, x0:x1], s.region[y0:y1, x0:x1], (body.origin[0] - x0, body.origin[1] - y0),
                             [(x - x0, y - y0, r) for x, y, r in own] if art in MOVED_EYES else [])
    patch_box = (x0, y0, x1, y1)

    frames_out = {"Body": body}
    frames_out.update({f"Face_{e}": faces[e] for _, e in EXPRESSIONS})
    whole = egg_art()
    frames_out.update({f"Egg{i}": egg(whole, cracks) for i, cracks in enumerate((0, 1, 3))})
    frames_out["Remains"] = remains(frames["sleep"], labels["sleep"], feet)
    frames_out["ItemPellet"] = fly((46, 48, 56), (84, 86, 98), (206, 226, 236), (170, 40, 32))
    frames_out["ItemRotten"] = fly((112, 126, 96), (90, 102, 78), (172, 184, 164), (120, 128, 92))
    frames_out["ItemMarble"] = bubble()

    entries, by_region = build_palette(list(frames_out.values()))
    idx = {n: index(s, by_region) for n, s in frames_out.items()}
    check_anchors(frames_out, idx, entries, body, faces)
    return dict(entries=entries, frames=frames_out, idx=idx, patch_box=patch_box, fixed=fixed)


OPEN_EYED = ("neutral", "happy", "alarmed", "annoyed", "croak", "blep", "foresee")


def check_anchors(frames, idx, entries, body, faces):
    """Every anchor sits on an eye or glow pixel of its own frame, and the body's
    anchors still do through every open-eyed patch that does not override them."""
    seeing = {REGION["eye"], REGION["glow"]}

    def on_eye(name, x, y):
        w, h = frames[name].size
        assert 0 <= x < w and 0 <= y < h, f"{name} anchor ({x},{y}) is outside the frame"
        assert entries[idx[name][y, x]][3] in seeing, f"{name} anchor ({x},{y}) is not on an eye or glow pixel"

    for name, s in frames.items():
        for x, y, _ in s.eyes:
            on_eye(name, x, y)
    for expr in OPEN_EYED:
        if not faces[expr].eyes:
            ox, oy = body.origin[0] - faces[expr].origin[0], body.origin[1] - faces[expr].origin[1]
            for x, y, _ in body.eyes:
                on_eye(f"Face_{expr}", x - ox, y - oy)


# ---- the header ----------------------------------------------------------------------

def emit(p):
    entries, frames, idx = p["entries"], p["frames"], p["idx"]
    blobs = {n: rle(idx[n]) for n in frames}
    for n, s in frames.items():
        assert (unrle(blobs[n], *s.size) == idx[n]).all(), f"{n} does not round-trip"
    flash = sum(map(len, blobs.values())) + 4 * len(entries)
    region_names = {i: n for n, i in REGION.items()}
    L = [
        f"// GENERATED by `{COMMAND}` from pets/grungo/art/. Do not edit; rerun it.",
        f"// {len(entries)} palette entries, {len(frames)} frames, {flash} bytes of flash, no RAM.",
        "#pragma once",
        "#include <cstdint>",
        '#include "paint/sprite_pack.h"',
        "",
        "namespace grungo_pack {",
        "",
        "inline constexpr paint::PaletteEntry kPalette[] = {",
    ]
    for i, (r, g, b, reg) in enumerate(entries):
        L.append(f"    {{{r}, {g}, {b}, {reg}}},  // {i} {region_names.get(reg, 'invariant')}")
    L += ["};", f"inline constexpr uint16_t kPaletteCount = {len(entries)};", "",
          "// By RegionId (regions.def): hueMin, hueMax, satMin, satMax, valMin, valMax.",
          "inline constexpr paint::RegionBand kBands[] = {"]
    for i, name in sorted(region_names.items()):
        L.append("    {" + ", ".join(map(str, BANDS[name])) + f"}},  // {name}")
    L += ["};", ""]
    for n, s in frames.items():
        data = blobs[n]
        L.append(f"inline constexpr uint8_t k{n}Rle[] = {{")
        L += ["    " + ", ".join(str(b) for b in data[i:i + 24]) + "," for i in range(0, len(data), 24)]
        L.append("};")
    L.append("")
    for n, s in frames.items():
        eyes = list(s.eyes) + [(0, 0, 0)] * (2 - len(s.eyes))
        e = ", ".join(f"{{{x}, {y}, {r}}}" for x, y, r in eyes)
        L.append(f"inline constexpr paint::FrameRef k{n}{{k{n}Rle, {s.size[0]}, {s.size[1]}, {s.origin[0]}, {s.origin[1]}, "
                 f"{{{e}}}, {len(s.eyes)}}};")
    L += ["", "// By ExprId (expressions.def).", "inline constexpr paint::FrameRef kFaces[] = {"]
    L += [f"    kFace_{e}," for _, e in EXPRESSIONS]
    L += ["};", "inline constexpr paint::FrameRef kItems[] = {kItemPellet, kItemRotten, kItemMarble};", "",
          "// One front-facing body for every pose and stage: motion and the hatchling's",
          "// scale are the renderer's. Mottling (mark layer 0) is procedural, so no mark art.",
          "class Pack final : public paint::SpritePack {",
          " public:",
          "  const paint::PaletteEntry* palette(uint16_t& count) const override { count = kPaletteCount; return kPalette; }",
          "  paint::RegionBand band(blorb::RegionId r) const override {",
          "    return r.v < sizeof(kBands) / sizeof(kBands[0]) ? kBands[r.v] : paint::RegionBand{0, 0, 128, 128, 128, 128};",
          "  }",
          "  paint::FrameRef body(blorb::PoseId, blorb::Stage, uint16_t) const override { return kBody; }",
          "  paint::FrameRef face(blorb::ExprId e, blorb::Stage) const override {",
          "    return e.v < sizeof(kFaces) / sizeof(kFaces[0]) ? kFaces[e.v] : kFaces[0];",
          "  }",
          "  paint::FrameRef mark(uint8_t, uint8_t) const override { return paint::FrameRef{}; }",
          "  paint::FrameRef egg(blorb::Fx progress) const override {",
          "    if (progress < blorb::Fx::ratio(6, 10)) return kEgg0;",
          "    return progress < blorb::Fx::ratio(85, 100) ? kEgg1 : kEgg2;",
          "  }",
          "  paint::FrameRef remains() const override { return kRemains; }",
          "  paint::FrameRef item(blorb::Appearance::Item::What w) const override {",
          "    uint8_t i = uint8_t(w);",
          "    return i < sizeof(kItems) / sizeof(kItems[0]) ? kItems[i] : kItems[0];",
          "  }",
          "};",
          "",
          "}  // namespace grungo_pack",
          "",
          "inline const paint::SpritePack& grungoPack() {",
          "  static const grungo_pack::Pack pack;",
          "  return pack;",
          "}",
          ""]
    return "\n".join(L)


# ---- previews (decoded back from the written header) ---------------------------------

def parse_header(text):
    pal = [tuple(map(int, m)) for m in re.findall(r"\{(\d+), (\d+), (\d+), (\d+)\},  // \d+", text)]
    blobs = {n: bytes(int(v) for v in re.findall(r"\d+", body))
             for n, body in re.findall(r"inline constexpr uint8_t k(\w+)Rle\[\] = \{(.*?)\};", text, re.S)}
    frames = {}
    for m in re.finditer(r"paint::FrameRef k(\w+)\{k\w+Rle, (\d+), (\d+), (-?\d+), (-?\d+), \{(.*?)\}, (\d+)\};", text):
        n, w, h, ox, oy, eyes, count = m.groups()
        e = [tuple(map(int, t)) for t in re.findall(r"\{(-?\d+), (-?\d+), (\d+)\}", eyes)][:int(count)]
        frames[n] = dict(idx=unrle(blobs[n], int(w), int(h)), origin=(int(ox), int(oy)), eyes=e)
    bands = [tuple(map(int, t)) for t in re.findall(r"\{(-?\d+), (-?\d+), (\d+), (\d+), (\d+), (\d+)\},  // \w+", text)]
    return pal, frames, bands


def tinted(pal, bands, shifts):
    """An approximation of a genetic recolour: per region, hue offset in 1/256 turn and
    sat and val multipliers (128 = x1), clamped to that region's band."""
    out = []
    for r, g, b, reg in pal:
        if reg in shifts:
            hmin, hmax, smin, smax, vmin, vmax = bands[reg]
            dh, ms, mv = shifts[reg]
            dh, ms, mv = min(max(dh, hmin), hmax), min(max(ms, smin), smax), min(max(mv, vmin), vmax)
            h, s, v = to_hsv(np.array([[r, g, b]], np.uint8))
            r, g, b = from_hsv(h + dh * 360 / 256, np.clip(s * ms / 128, 0, 1), np.clip(v * mv / 128, 0, 1))[0]
        out.append((int(r), int(g), int(b), reg))
    return out


def preview(text, outdir):
    pal, frames, bands = parse_header(text)
    outdir.mkdir(parents=True, exist_ok=True)
    BG = (34, 38, 40)

    def paint(canvas, f, at, pal, anchors=False):
        img = canvas.load()
        ox, oy = at[0] - f["origin"][0], at[1] - f["origin"][1]
        h, w = f["idx"].shape
        for y in range(h):
            for x in range(w):
                i = f["idx"][y, x]
                if i and 0 <= ox + x < canvas.width and 0 <= oy + y < canvas.height:
                    img[ox + x, oy + y] = pal[i][:3]
        if anchors:
            d = ImageDraw.Draw(canvas)
            for x, y, r in f["eyes"]:
                d.ellipse([ox + x - r, oy + y - r, ox + x + r, oy + y + r], outline=(255, 0, 255))
                img[ox + x, oy + y] = (255, 0, 255)

    def save(canvas, name):
        canvas.resize((canvas.width * 3, canvas.height * 3), Image.NEAREST).save(outdir / name)
        print("preview", outdir / name)

    body = frames["Body"]
    bw, bh = body["idx"].shape[1], body["idx"].shape[0]
    faces = [e for _, e in EXPRESSIONS]
    for name, p, anchors in (("faces.png", pal, False), ("faces-anchors.png", pal, True),
                             ("recolour.png", tinted(pal, bands, {REGION["cloak"]: (30, 150, 120), REGION["skin"]: (-14, 140, 128)}), False)):
        c = Image.new("RGB", ((bw + 4) * 5, (bh + 4) * 2), BG)
        for i, e in enumerate(faces):
            at = ((i % 5) * (bw + 4) + 2 + body["origin"][0], (i // 5) * (bh + 4) + 2 + body["origin"][1])
            paint(c, body, at, p)
            f = frames[f"Face_{e}"]
            paint(c, f, at, p, anchors)
            if anchors and not f["eyes"]:
                paint(c, {**body, "idx": np.zeros((1, 1), np.uint8)}, at, p, True)
        save(c, name)
    c = Image.new("RGB", (240, 100), BG)
    for i, n in enumerate(("Egg0", "Egg1", "Egg2")):
        paint(c, frames[n], (40 + i * 70, 90), pal, False)
    paint(c, frames["ItemPellet"], (220, 40), pal)
    paint(c, frames["ItemRotten"], (220, 60), pal)
    paint(c, frames["ItemMarble"], (220, 85), pal)
    save(c, "egg-items.png")
    c = Image.new("RGB", (240, 100), BG)
    for i, n in enumerate(("Egg0", "Egg1", "Egg2")):
        paint(c, frames[n], (40 + i * 70, 90), pal, True)
    save(c, "egg-anchors.png")
    r = frames["Remains"]
    c = Image.new("RGB", (r["idx"].shape[1] + 8, r["idx"].shape[0] + 8), BG)
    paint(c, r, (4 + r["origin"][0], 4 + r["origin"][1]), pal)
    save(c, "remains.png")
    # the region map, one flat colour per region, to review the labelling
    flat = {INVARIANT: (16, 16, 16), REGION["skin"]: (90, 170, 70), REGION["belly"]: (220, 230, 120),
            REGION["cloak"]: (150, 90, 45), REGION["eye"]: (230, 40, 60), REGION["mouth"]: (250, 150, 200),
            REGION["glow"]: (60, 240, 220), REGION["shell"]: (200, 200, 90)}
    rp = [(*flat[reg], reg) for *_, reg in pal]
    c = Image.new("RGB", ((bw + 4) * 5, (bh + 4) * 2), BG)
    for i, e in enumerate(faces):
        at = ((i % 5) * (bw + 4) + 2 + body["origin"][0], (i // 5) * (bh + 4) + 2 + body["origin"][1])
        paint(c, body, at, rp)
        paint(c, frames[f"Face_{e}"], at, rp)
    save(c, "regions.png")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preview", type=Path, help="decode the written header into preview PNGs here")
    a = ap.parse_args()
    p = build()
    text = emit(p)
    OUT.write_text(text, newline="\n")
    print("iris shifted to neutral's (from sat, val):", p["fixed"])
    print("face patch box (body px):", p["patch_box"], "palette:", len(p["entries"]))
    print("sha256", hashlib.sha256(text.encode()).hexdigest(), OUT.relative_to(ROOT))
    if a.preview:
        preview(OUT.read_text(), a.preview)


if __name__ == "__main__":
    main()
