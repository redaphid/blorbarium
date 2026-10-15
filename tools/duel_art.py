"""The battle art (BATTLES.md "Art units" ART1 to ART3), drawn in code: scars, trophy
tokens and duel glyphs. tools/sprite_pack.py --duel converts them into
pets/duel/duel_art.h.

Everything is a Sketch: shapes in device-pixel units, rendered CELL times larger and
brought to device scale by the converter's own downscaler, so the output is the same
bytes every run and no image model is involved. At 24 px a drawn shape reads where a
generated one blurs.

Two rules hold for every piece:
- Legible without colour. Each glyph is one bold silhouette with an ink outline, and
  no two glyphs that can share the rim differ only by colour.
- No gore. A scar is a healed crease, the skull is round and goofy, the bandage is a
  sticking plaster.

Scars carry no colour of their own. Every scar pixel is tagged skin, and its grey g
means "multiply the body pixel underneath by g / 128": below 128 a crease, above it a
raised edge. So a scar is always the pet's own skin, whatever its genes made it.
"""
import math

import sprite_pack as sp
from sprite_pack import INVARIANT, REGION, Sketch

INK = (24, 20, 16)
LIGHT = (236, 232, 220)   # a free-standing mark (a ray, a motion line) on the dark screen
CREASE, DEEP, RIDGE = (40, 40, 40), (28, 28, 28), (190, 190, 190)   # scar shades, as multipliers x128


def arc(cx, cy, rx, ry, a0, a1, n=24):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def blob(k, shapes, colour, ink=1.0, region=INVARIANT):
    """Fill the union of shapes, then ink its rim: one silhouette with one outline."""
    m = None
    for kind, *args in shapes:
        s = k.mask(kind, *args) if kind != "line" else k.mask("line", args[0], width=args[1])
        m = s if m is None else m | s
    k.fill(m, colour, region)
    if ink:
        k.outline(m, ink)
    return m


def stroke(k, pts, width, colour, region=INVARIANT, ink=0.0):
    """A line, optionally on a wider ink line so it carries its own outline."""
    if ink:
        k.fill(k.mask("line", pts, width=width + 2 * ink), INK, INVARIANT)
    k.fill(k.mask("line", pts, width=width), colour, region)


def glyph(draw, size=24):
    k = Sketch(size, size)
    draw(k)
    return k.sprite((size // 2, size // 2))


# ---- ART3: stances ------------------------------------------------------------------

def pounce(k):
    """A paw: one pad and four toes."""
    blob(k, [("ellipse", 5.5, 11, 18.5, 21.5)], (240, 140, 50))
    for x0, y0 in ((2.5, 6.5), (7, 2.5), (12.5, 2.5), (17, 6.5)):
        blob(k, [("ellipse", x0, y0, x0 + 4.8, y0 + 5.6)], (240, 140, 50))


def guard(k):
    """A leaf held up as a shield: a heater shield with a midrib and veins."""
    blob(k, [("polygon", [(3.5, 3.5), (20.5, 3.5), (20.5, 11), (12, 21.5), (3.5, 11)])], (80, 140, 230))
    stroke(k, [(12, 5.5), (12, 18)], 1.2, INK)
    for y in (8.5, 12.5):
        stroke(k, [(12, y + 2), (7, y)], 0.9, INK)
        stroke(k, [(12, y + 2), (17, y)], 0.9, INK)


def trick(k):
    """A swirl: two turns of a spiral, thick, with its own outline."""
    pts = [(12 + (1 + 8.5 * t / 720) * math.cos(math.radians(t)), 12 + (1 + 8.5 * t / 720) * math.sin(math.radians(t)))
           for t in range(0, 721, 15)]
    stroke(k, pts, 2.4, (160, 90, 210), ink=0.9)


# ---- ART3: powers (PowerGlyph order) ----------------------------------------------------

def eye(k):
    """Foresee: an open eye with rays above it."""
    lid = arc(12, 17, 10, 9, 200, 340) + arc(12, 7, 10, 9, 20, 160)
    blob(k, [("polygon", lid)], (240, 240, 230))
    k.fill(k.mask("ellipse", 8, 8, 16, 16), (60, 220, 200), INVARIANT)
    k.fill(k.mask("ellipse", 10.2, 10.2, 13.8, 13.8), INK, INVARIANT)
    k.fill(k.mask("ellipse", 10.6, 9.4, 12.2, 11), (255, 255, 255), INVARIANT)
    for x0, x1 in ((6, 4), (12, 12), (18, 20)):
        stroke(k, [(x0, 6), (x1, 1.5)], 1.2, LIGHT)


def shell(k):
    """Shell Up: a ribbed shell dome, glowing (a spiral would read as Trick in grey)."""
    blob(k, [("polygon", arc(12, 20, 10, 15, 180, 360))], (232, 150, 60))
    for a in (210, 240, 270, 300, 330):
        x, y = 12 + 10 * math.cos(math.radians(a)), 20 + 15 * math.sin(math.radians(a))
        stroke(k, [(12, 20), (x, y)], 1.0, INK)
    for a, b in (((2, 7), (0.5, 5)), ((22, 7), (23.5, 5)), ((12, 2.5), (12, 0.5))):
        stroke(k, [a, b], 1.1, LIGHT)


def spore(k):
    """Spore Puff: a flat-bottomed puff cloud, spores drifting up off it."""
    blob(k, [("ellipse", 2, 10, 11, 19), ("ellipse", 7, 5.5, 17, 15.5), ("ellipse", 13, 9, 22, 18),
             ("polygon", [(4, 14), (20, 14), (20, 19.5), (4, 19.5)])], (250, 150, 205))
    for x, y in ((6, 3), (12, 1.5), (18, 3)):   # spores drifting up, drawn as glints
        stroke(k, [(x - 1.3, y), (x + 1.3, y)], 0.9, LIGHT)
        stroke(k, [(x, y - 1.3), (x, y + 1.3)], 0.9, LIGHT)


def leaf(k):
    """Mend: a pointed leaf leaning right, a white plus on it: it grows back."""
    c, s = math.cos(math.radians(30)), math.sin(math.radians(30))
    rot = lambda x, y: (12 + (x - 12) * c - (y - 12) * s, 12 + (x - 12) * s + (y - 12) * c)
    side = lambda sign: [rot(12 + sign * 7.5 * math.sin(math.pi * i / 20), 1.5 + 21 * i / 20) for i in range(21)]
    blob(k, [("polygon", side(1) + side(-1)[::-1])], (110, 205, 110))
    stroke(k, [rot(12, 3), rot(12, 21)], 1.0, INK)
    stroke(k, [(11, 13.5), (17, 13.5)], 1.6, (255, 255, 255))
    stroke(k, [(14, 10.5), (14, 16.5)], 1.6, (255, 255, 255))


def lamp(k):
    """Lantern Flash: a lantern with a handle and rays."""
    k.fill(k.mask("line", arc(12, 6, 3.5, 3.5, 180, 360), width=1.4), INK, INVARIANT)
    blob(k, [("polygon", [(7, 6.5), (17, 6.5), (18.5, 18), (5.5, 18)])], (255, 220, 90))
    blob(k, [("polygon", [(5, 18), (19, 18), (19, 21), (5, 21)])], (150, 100, 60))
    stroke(k, [(12, 9), (12, 15)], 1.2, (255, 250, 220))
    for a, b in (((3.5, 10), (0.5, 8.5)), ((20.5, 10), (23.5, 8.5)), ((3.5, 15), (0.5, 16.5)), ((20.5, 15), (23.5, 16.5))):
        stroke(k, [a, b], 1.1, LIGHT)


def wave(k):
    """Splash Dash: three bold water lines."""
    for y in (6, 12, 18):
        pts = [(x / 2, y + 1.6 * math.sin(math.pi * x / 9)) for x in range(4, 43)]
        stroke(k, pts, 2.6, (70, 150, 230), ink=0.9)


def spark(k):
    """Static Spark: a lightning bolt."""
    blob(k, [("polygon", [(14, 1.5), (5, 13.5), (11, 13.5), (8.5, 22.5), (19, 9.5), (13, 9.5), (16.5, 1.5)])], (255, 220, 40))


def feather_shape(k, colour, bars, s=1.0):
    """A leaning feather with a quill and barbs; s scales it about the centre."""
    S = lambda pts: [(12 + (x - 12) * s, 12 + (y - 12) * s) for x, y in pts]
    pts = arc(12, 12, 6.5, 11, 270, 450) + arc(12, 12, 6.5, 11, 90, 270)
    blob(k, [("polygon", S([(x + 0.18 * (12 - y), y) for x, y in pts]))], colour)
    stroke(k, S([(14.5, 2.5), (10.5, 22)]), 1.0, INK)
    for i in range(bars):
        y = 6 + i * 4
        stroke(k, S([(13.2 - 0.2 * (y - 12), y), (8, y + 2.2)]), 0.8, INK)
        stroke(k, S([(13.2 - 0.2 * (y - 12), y), (17, y + 1.6)]), 0.8, INK)


def feather(k):
    """Hush: an owl with her eyes shut, keeping quiet (the lone feather is Spar's)."""
    blob(k, [("ellipse", 2.5, 4, 19.5, 22.5), ("polygon", [(3, 8), (2.5, 1), (8, 5)]), ("polygon", [(19, 8), (19.5, 1), (14, 5)])],
         (150, 175, 220))
    for cx in (7, 15):
        disc_ = k.mask("ellipse", cx - 4.2, 6, cx + 4.2, 14.4)
        k.fill(disc_, (235, 240, 250), INVARIANT)
        k.outline(disc_, 0.8)
        stroke(k, arc(cx, 9.6, 2.4, 2.2, 25, 155, 10), 1.1, INK)
    blob(k, [("polygon", [(9.3, 13), (12.7, 13), (11, 18)])], (240, 160, 60), ink=0.7)
    for y in (9, 13):
        stroke(k, [(20.5, y), (23.5, y - 1)], 1.0, LIGHT)


def gear_shape(k, colour, teeth=8, hole=True):
    pts = []
    for i in range(teeth * 4):
        a = 2 * math.pi * i / (teeth * 4)
        r = 10.5 if (i % 4) in (1, 2) else 7.5
        pts.append((12 + r * math.cos(a + math.pi / (teeth * 4)), 12 + r * math.sin(a + math.pi / (teeth * 4))))
    m = blob(k, [("polygon", pts)], colour)
    if hole:
        h = k.mask("ellipse", 9, 9, 15, 15)
        k.fill(h, INK, INVARIANT)
    return m


def gear(k):
    """Gadget: a cog with a question mark: which gadget is anyone's guess."""
    gear_shape(k, (175, 180, 190), hole=False)
    k.fill(k.mask("ellipse", 6.5, 6.5, 17.5, 17.5), (235, 235, 240), INVARIANT)
    stroke(k, arc(12, 10, 2.6, 2.6, 190, 420, 16) + [(12, 13.5)], 1.4, INK)
    k.fill(k.mask("ellipse", 11.2, 15, 12.8, 16.6), INK, INVARIANT)


POWERS = [("eye", eye), ("shell", shell), ("spore", spore), ("leaf", leaf), ("lamp", lamp),
          ("wave", wave), ("spark", spark), ("feather", feather), ("gear", gear)]
STANCES = [("pounce", pounce), ("guard", guard), ("trick", trick)]


# ---- ART3: tiers, the skull's fill, marks ------------------------------------------

def spar(k):
    """Spar: a feather, play."""
    feather_shape(k, (245, 245, 240), 3)


def plaster(k, size, colour=(235, 200, 160)):
    """A sticking plaster on the diagonal, its pad and air holes."""
    s = size / 24
    rot = lambda pts: [(12 * s + (x - 12) * s * 0.707 - (y - 12) * s * 0.707, 12 * s + (x - 12) * s * 0.707 + (y - 12) * s * 0.707) for x, y in pts]
    blob(k, [("polygon", rot([(1.5, 8.5), (22.5, 8.5), (22.5, 15.5), (1.5, 15.5)]))], colour)
    blob(k, [("polygon", rot([(8.5, 9.5), (15.5, 9.5), (15.5, 14.5), (8.5, 14.5)]))], (250, 235, 215), ink=0.7)
    for x in (4.5, 19.5):   # the adhesive ends' texture: short lines, never paired dots
        a, b = rot([(x, 10), (x, 14)])
        k.fill(k.mask("line", [a, b], width=0.7 * s), INK, INVARIANT)


def scrap(k):
    """Scrap: a plaster: wounds and scars, but he gets up."""
    plaster(k, 24)


def skull(k, filled):
    """Mortal: a round, goofy skull. Empty is its outline round a dark hollow; full is
    bone white. The two share every pixel's place, so the fill can wipe from one to
    the other row by row as the thumb holds."""
    head = [("ellipse", 3, 1.5, 21, 18), ("polygon", [(7, 14), (17, 14), (16, 22), (8, 22)])]
    blob(k, head, (240, 236, 222) if filled else (54, 50, 46))
    for x0 in (6, 13):
        k.fill(k.mask("ellipse", x0, 7.5, x0 + 5, 13), INK, INVARIANT)
    k.fill(k.mask("polygon", [(12, 13.5), (10.8, 16), (13.2, 16)]), INK, INVARIANT)
    for x in (10, 12, 14):
        stroke(k, [(x, 18.5), (x, 21.5)], 0.8, INK)


def debt(k):
    """A life owed: a heart with a string tied round it in a bow."""
    heart = k.mask("ellipse", 2, 3, 13, 14) | k.mask("ellipse", 11, 3, 22, 14) | k.mask("polygon", [(2.6, 10.5), (21.4, 10.5), (12, 21.5)])
    k.fill(heart, (235, 95, 120), INVARIANT)
    k.outline(heart, 1.0)
    stroke(k, [(2.5, 12.5), (21.5, 9.5)], 1.6, (250, 235, 200), ink=0.6)
    blob(k, [("ellipse", 9, 7.5, 12.5, 11.5), ("ellipse", 12.5, 6.5, 16, 10.5)], (250, 235, 200), ink=0.7)


def vendetta(k):
    """A line that remembers: one big glaring eye under a heavy slanted brow."""
    lid = arc(12, 20, 11, 9, 200, 340) + arc(12, 9, 11, 9, 20, 160)
    blob(k, [("polygon", lid)], (240, 240, 230))
    k.fill(k.mask("ellipse", 8.5, 10, 15.5, 17), (200, 50, 40), INVARIANT)
    k.fill(k.mask("ellipse", 10.5, 12, 13.5, 15), INK, INVARIANT)
    blob(k, [("polygon", [(0.5, 3), (23.5, 8), (23.5, 11.5), (0.5, 7)])], (90, 60, 50))


def wounded(k):
    """Wounded: the small plaster he wears."""
    plaster(k, 16)


# ---- elements (Element order: splash, sprout, spark), so kids see who beats whom ----

def e_splash(k):
    """Splash: a water drop."""
    blob(k, [("polygon", [(12, 1.5)] + arc(12, 15, 7, 7, 200, -20, 24))], (70, 150, 230))
    k.fill(k.mask("ellipse", 8.5, 13, 11, 17), (220, 240, 255), INVARIANT)


def e_sprout(k):
    """Sprout: a seedling, a stem with two round leaves."""
    stroke(k, [(12, 22), (12, 9)], 1.6, (90, 170, 80), ink=0.8)
    blob(k, [("ellipse", 2.5, 4, 12, 11.5)], (120, 205, 100))
    blob(k, [("ellipse", 12, 2, 21.5, 9.5)], (120, 205, 100))
    blob(k, [("polygon", [(5, 22.5), (19, 22.5), (17, 19.5), (7, 19.5)])], (150, 100, 60))


def e_spark(k):
    """Spark: a four-point sparkle (the bolt is Static Spark's)."""
    pts = []
    for i in range(8):
        a = math.radians(i * 45 - 90)
        r = 11 if i % 2 == 0 else 3.2
        pts.append((12 + r * math.cos(a), 12 + r * math.sin(a)))
    blob(k, [("polygon", pts)], (255, 215, 60))


ELEMENTS = [("splash", e_splash), ("sprout", e_sprout), ("spark", e_spark)]


TIERS = [("spar", spar), ("scrap", scrap), ("mortal", lambda k: skull(k, True))]


# ---- ART2: trophy tokens (species id order) ----------------------------------------

def token(k):
    """The token every trophy sits on, so a trophy never reads as a power glyph."""
    k.fill(k.mask("ellipse", 0.5, 0.5, 23.5, 23.5), (64, 58, 52), INVARIANT)
    k.fill(k.mask("ellipse", 2, 2, 22, 22), (92, 84, 74), INVARIANT)
    k.outline(k.mask("ellipse", 0.5, 0.5, 23.5, 23.5), 1.0)


def t_hood(k):
    token(k)
    blob(k, [("polygon", arc(12, 15, 7.5, 10, 180, 360) + [(19.5, 19), (4.5, 19)])], (120, 82, 52))
    k.fill(k.mask("ellipse", 8, 10, 16, 19), (40, 30, 24), INVARIANT)


def t_shell(k):
    token(k)
    blob(k, [("polygon", arc(12, 18, 8.5, 12, 180, 360))], (232, 137, 43))
    for a in (215, 245, 270, 295, 325):
        x, y = 12 + 8.5 * math.cos(math.radians(a)), 18 + 12 * math.sin(math.radians(a))
        stroke(k, [(12, 18), (x, y)], 0.8, INK)


def t_spore(k):
    """A bumpy coral puffball, his own shape (no spotted cap)."""
    token(k)
    bumps = [("ellipse", 12 + 6.5 * math.cos(a) - 2.4, 12.5 + 6.5 * math.sin(a) - 2.4,
              12 + 6.5 * math.cos(a) + 2.4, 12.5 + 6.5 * math.sin(a) + 2.4) for a in [2 * math.pi * i / 10 for i in range(10)]]
    blob(k, [("ellipse", 5, 5.5, 19, 19.5)] + bumps, (232, 128, 106))
    k.fill(k.mask("ellipse", 8.5, 9, 15.5, 16), (246, 238, 208), INVARIANT)


def t_gill(k):
    token(k)
    stroke(k, [(8, 19.5), (15.5, 4.5)], 1.6, (194, 54, 122), ink=0.6)
    for i in range(4):
        x, y = 9.2 + i * 1.8, 16.8 - i * 3.6
        blob(k, [("ellipse", x - 4.6, y - 1.6, x, y + 1.4)], (240, 120, 180), ink=0.6)
        blob(k, [("ellipse", x, y - 2.6, x + 4.6, y + 0.4)], (240, 120, 180), ink=0.6)


def t_scale(k):
    token(k)
    pts = arc(12, 10, 6.5, 6.5, 180, 360) + [(18.5, 10), (12, 20.5), (5.5, 10)]
    blob(k, [("polygon", pts)], (179, 157, 230))
    k.fill(k.mask("polygon", [(7, 12), (17, 12), (14.5, 16), (9.5, 16)]), (242, 220, 124), INVARIANT)


def t_fin(k):
    token(k)
    pts = [(12, 19.5)] + arc(12, 19.5, 9, 13, 215, 325)
    blob(k, [("polygon", pts)], (74, 144, 217))
    for a in (235, 255, 270, 285, 305):
        x, y = 12 + 8 * math.cos(math.radians(a)), 19.5 + 12 * math.sin(math.radians(a))
        stroke(k, [(12, 19.5), (x, y)], 0.7, INK)


def t_lamp(k):
    token(k)
    blob(k, [("ellipse", 6.5, 7, 17.5, 19)], (250, 230, 90))
    k.fill(k.mask("ellipse", 9, 9.5, 15, 15.5), (255, 252, 220), INVARIANT)
    for a in range(0, 360, 45):
        x0, y0 = 12 + 7.2 * math.cos(math.radians(a)), 13 + 7.8 * math.sin(math.radians(a))
        x1, y1 = 12 + 9.4 * math.cos(math.radians(a)), 13 + 9.6 * math.sin(math.radians(a))
        stroke(k, [(x0, y0), (x1, y1)], 0.8, (255, 240, 150))


def t_feather(k):
    token(k)
    feather_shape(k, (242, 239, 230), 3, s=0.78)


def t_gear(k):
    token(k)
    pts = []
    for i in range(32):
        a = 2 * math.pi * i / 32
        r = (10.5 if (i % 4) in (1, 2) else 7.5) * 0.78   # a smaller cog, inside the token
        pts.append((12 + r * math.cos(a + math.pi / 32), 12 + r * math.sin(a + math.pi / 32)))
    blob(k, [("polygon", pts)], (184, 115, 51))
    k.fill(k.mask("ellipse", 9.6, 9.6, 14.4, 14.4), (64, 58, 52), INVARIANT)


TROPHIES = [("hood", t_hood), ("shell", t_shell), ("spore", t_spore), ("gill", t_gill), ("scale", t_scale),
            ("fin", t_fin), ("lamp", t_lamp), ("feather", t_feather), ("gear", t_gear)]


# ---- ART1: scars (Scar::Kind order), small, medium, large ---------------------------

SCAR_SIZES = (12, 18, 26)   # on a body about 168 px tall


def scar(kind, size):
    k = Sketch(size, size)
    u = size / 17                         # drawn on a 17 px design, scaled
    P = lambda pts: [(x * u, y * u) for x, y in pts]
    skin = REGION["skin"]
    w = max(1.2, 1.7 * u)

    def crease(pts, width=w):
        """A dark crease inside a light raised ridge: it reads on dark skin and pale alike."""
        k.fill(k.mask("line", P(pts), width=width + max(1.4, 1.6 * u)), RIDGE, skin)
        k.fill(k.mask("line", P(pts), width=width), CREASE, skin)

    if kind == "claw":
        for dx in (-4, 0, 4):
            crease([(4 + dx, 3), (12 + dx, 14)])
    elif kind == "scorch":
        # a singe streak, long and jagged, so even at its smallest it is never a dot
        spine = [(2, 13), (5, 10.5), (6.5, 12), (9, 8), (10.5, 9.5), (14.5, 4)]
        crease(spine, max(1.6, 2.4 * u))
        for (x, y), (dx, dy) in zip(spine[1:-1], ((-1, -2.5), (1.5, 2), (-1.5, -2.5), (1.8, 1.8))):
            crease([(x, y), (x + dx, y + dy)], max(1.0, 1.1 * u))
    elif kind == "notch":
        crease(arc(8.5, 4, 6, 7, 20, 160, 16))
        crease([(5, 9), (8.5, 13), (12, 9)])
    elif kind == "stitches":
        crease([(2.5, 11.5), (14.5, 5.5)])
        for i in range(4):
            x, y = 4.5 + i * 3.2, 10.5 - i * 1.6
            crease([(x - 1.2, y - 2.6), (x + 1.2, y + 2.6)], max(1.0, 1.2 * u))
    elif kind == "bite":
        # an arch of tooth nicks, opening downward so it never reads as a grin
        crease(arc(8.5, 13.5, 7, 9, 205, 335, 16))
        for a in (220, 245, 270, 295, 320):
            c, s_ = math.cos(math.radians(a)), math.sin(math.radians(a))
            crease([(8.5 + 7 * c, 13.5 + 9 * s_), (8.5 + 4.6 * c, 13.5 + 6.4 * s_)], max(1.0, 1.2 * u))
    return k.sprite((size // 2, size // 2))


SCARS = ["claw", "scorch", "notch", "stitches", "bite"]

# Where the owner's sheet shows each kind (SCARS order) on each pet, as fractions of its
# body bbox: head, shoulders, sides, wings, fins and limbs, never the front of the chest
# or belly, where a small round mark can read as a piercing (the owner vetoed ring and
# ripple scars for that).
SHEET_SPOTS = {
    "grungo": [(0.10, 0.60), (0.90, 0.62), (0.76, 0.12), (0.82, 0.88), (0.18, 0.88)],
    "mossby": [(0.15, 0.45), (0.88, 0.45), (0.50, 0.12), (0.25, 0.88), (0.75, 0.88)],
    "puffkin": [(0.13, 0.35), (0.87, 0.32), (0.50, 0.07), (0.07, 0.60), (0.93, 0.60)],
    "gilly": [(0.42, 0.24), (0.83, 0.68), (0.20, 0.42), (0.17, 0.68), (0.68, 0.90)],
    "mothwick": [(0.15, 0.12), (0.85, 0.15), (0.38, 0.29), (0.15, 0.55), (0.85, 0.55)],
    "mudge": [(0.45, 0.14), (0.90, 0.53), (0.24, 0.65), (0.10, 0.55), (0.77, 0.66)],
    "fizz": [(0.42, 0.17), (0.88, 0.52), (0.12, 0.50), (0.24, 0.55), (0.76, 0.55)],
    "hushwing": [(0.38, 0.08), (0.92, 0.55), (0.72, 0.20), (0.08, 0.60), (0.90, 0.75)],
    "tinker": [(0.15, 0.58), (0.85, 0.60), (0.12, 0.33), (0.30, 0.90), (0.72, 0.90)],
}


def frames():
    """Every battle frame by header name, in a fixed order."""
    out = {}
    for kind in SCARS:
        for label, size in zip("SML", SCAR_SIZES):
            out[f"Scar{kind.capitalize()}{label}"] = scar(kind, size)
    for name, draw in TROPHIES:
        out[f"Trophy{name.capitalize()}"] = glyph(draw)
    for name, draw in STANCES:
        out[f"Stance{name.capitalize()}"] = glyph(draw)
    for name, draw in POWERS:
        out[f"Power{name.capitalize()}"] = glyph(draw)
    for name, draw in TIERS:
        out[f"Tier{name.capitalize()}"] = glyph(draw)
    for name, draw in ELEMENTS:
        out[f"Element{name.capitalize()}"] = glyph(draw)
    out["SkullEmpty"] = glyph(lambda k: skull(k, False))
    out["Debt"] = glyph(debt)
    out["Vendetta"] = glyph(vendetta)
    out["Wounded"] = glyph(wounded, 16)
    return out
