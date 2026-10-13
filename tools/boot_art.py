"""ART4: the BOOT hint glyphs (docs/design/BATTLES.md "Amendment", sketch boot.h).

The rim glyph beside the button says what a press does now. Drawn in code on a 16 px
grid, like the rest of the battle art (tools/duel_art.py), and converted with it by
tools/sprite_pack.py --duel. Each glyph has a greyed twin for "not now"; the hold ring
comes in HOLD_STEPS + 1 fill steps; a double press is two short bars, not two dots
(nothing in the battle art may read as a stud).
"""
import math

import numpy as np

import sprite_pack as sp
from duel_art import INK, LIGHT, arc, blob, glyph, stroke
from sprite_pack import INVARIANT, Sketch


def pellet(k):
    """Feed: a bowl heaped with food (a lone pellet at 16 px is a dot)."""
    blob(k, [("ellipse", 3.5, 3.5, 12.5, 10)], (190, 130, 70), ink=0.8)
    for x, y in ((6, 5.5), (9, 4.8), (10.5, 7)):
        stroke(k, [(x - 1, y), (x + 1, y)], 0.7, INK)
    blob(k, [("polygon", [(1, 8), (15, 8), (12.5, 14), (3.5, 14)])], (80, 150, 220), ink=0.8)


def moon(k):
    """Tuck in: a crescent moon."""
    m = k.mask("ellipse", 2, 1.5, 14, 13.5) & ~k.mask("ellipse", 6, 0, 17, 11)
    k.fill(m, (250, 225, 120), INVARIANT)
    k.outline(m, 0.8)


def download(k):
    """Yes, update: an arrow down into a tray."""
    blob(k, [("polygon", [(6, 1), (10, 1), (10, 6), (13, 6), (8, 11), (3, 6), (6, 6)])], (90, 200, 120), ink=0.8)
    blob(k, [("polygon", [(1, 10), (3, 10), (3, 13), (13, 13), (13, 10), (15, 10), (15, 15), (1, 15)])], (210, 210, 215), ink=0.7)


def lock(k):
    """Nothing moves: a padlock."""
    shackle = arc(8, 7, 3.6, 4.5, 180, 360, 12)
    k.fill(k.mask("line", shackle, width=1.8), INK, INVARIANT)
    k.fill(k.mask("line", shackle, width=0.9), (200, 200, 210), INVARIANT)
    blob(k, [("polygon", [(2.5, 7), (13.5, 7), (13.5, 15), (2.5, 15)])], (235, 190, 70), ink=0.8)
    k.fill(k.mask("line", [(8, 9.5), (8, 12.5)], width=1.2), INK, INVARIANT)


def friend(k):
    """Pick a friend: two people, heads and shoulders, one behind the other."""
    for dx, c in ((4, (90, 170, 160)), (-3, (130, 215, 200))):
        blob(k, [("ellipse", 5 + dx, 1.5, 11 + dx, 7.5), ("polygon", arc(8 + dx, 16, 6, 7, 180, 360, 12))], c, ink=0.8)


def door(k):
    """Leave: a door, ajar."""
    blob(k, [("polygon", [(3, 1), (13, 1), (13, 15), (3, 15)])], (60, 50, 45), ink=0.8)
    blob(k, [("polygon", [(3, 1), (10, 2.5), (10, 16), (3, 15)])], (170, 110, 60), ink=0.8)
    stroke(k, [(8.2, 8), (8.2, 10)], 0.9, INK)


def next_arrow(k):
    """Next egg: an arrow to the right."""
    blob(k, [("polygon", [(1, 5.5), (8, 5.5), (8, 1.5), (15, 8), (8, 14.5), (8, 10.5), (1, 10.5)])], (240, 240, 235))


def check(k):
    """Keep this one: a check mark."""
    stroke(k, [(1.5, 8.5), (6, 13), (14.5, 3)], 2.6, (110, 220, 110), ink=0.8)


def flower(k):
    """Lay a flower: a flower on its stem."""
    stroke(k, [(8, 9), (8, 15.5)], 1.2, (90, 170, 80), ink=0.6)
    blob(k, [("ellipse", 9, 11, 14, 14)], (110, 200, 100), ink=0.6)
    petals = []
    for i in range(5):
        a = 2 * math.pi * i / 5 - math.pi / 2
        cx, cy = 8 + 3.4 * math.cos(a), 6 + 3.4 * math.sin(a)
        petals.append(("ellipse", cx - 2.4, cy - 2.4, cx + 2.4, cy + 2.4))
    blob(k, petals, (240, 140, 180))
    k.fill(k.mask("ellipse", 6.4, 4.4, 9.6, 7.6), (250, 220, 90), INVARIANT)


def peek(k):
    """Peek: an egg asking who is inside."""
    blob(k, [("ellipse", 2.5, 0.5, 13.5, 15.5)], (235, 230, 210))
    stroke(k, arc(8, 6, 2.6, 2.6, 190, 420, 16) + [(8, 9.6)], 1.5, INK)
    k.fill(k.mask("ellipse", 7, 11.2, 9, 13.2), INK, INVARIANT)


def open_hand(k):
    """Spare: an open hand, held up, palm forward."""
    shapes = [("polygon", [(3.5, 8), (12, 8), (11.5, 15), (4.5, 15)])]
    for x0, top in ((3.6, 3), (5.9, 1.5), (8.2, 1.5), (10.4, 3)):
        shapes.append(("polygon", [(x0, 9), (x0, top + 1), (x0 + 1.9, top + 1), (x0 + 1.9, 9)]))
        shapes.append(("ellipse", x0, top, x0 + 1.9, top + 2))
    shapes.append(("polygon", [(12, 12), (15, 7.5), (15.5, 9), (12.5, 14)]))
    blob(k, shapes, (240, 200, 160), ink=0.8)


def plead(k):
    """Plead: two palms pressed together, fingers up."""
    for sign in (-1, 1):
        x = lambda u: 8 + sign * u
        palm = [(x(0), 1.5), (x(2.6), 2.5), (x(4.4), 8), (x(5), 15), (x(0), 15)]
        blob(k, [("polygon", palm)], (240, 200, 160), ink=0.8)
        for y in (5, 7.5):
            stroke(k, [(x(0.6), y), (x(2.6), y + 0.6)], 0.6, INK)
    stroke(k, [(2, 4), (0.5, 3)], 0.8, LIGHT)
    stroke(k, [(14, 4), (15.5, 3)], 0.8, LIGHT)


def candle(k):
    """A pet fell: a candle, lit."""
    blob(k, [("polygon", [(4.5, 8), (11.5, 8), (11.5, 15), (4.5, 15)])], (240, 235, 220), ink=0.8)
    blob(k, [("polygon", [(8, 0.5), (11, 4.5), (8, 7.5), (5, 4.5)])], (255, 180, 60), ink=0.7)
    k.fill(k.mask("polygon", [(8, 3), (9.4, 5), (8, 6.5), (6.6, 5)]), (255, 240, 170), INVARIANT)


def rematch(k):
    """Ask a rematch: two arrows chasing each other round."""
    for a0, a1 in ((215, 320), (35, 140)):
        pts = arc(8, 8, 5.4, 5.4, a0, a1, 12)
        stroke(k, pts, 1.8, (240, 240, 235), ink=0.7)
        a = math.radians(a1)
        tx, ty = -math.sin(a), math.cos(a)            # the arc's direction at its end
        x1, y1 = 8 + 5.4 * math.cos(a), 8 + 5.4 * math.sin(a)
        nx, ny = math.cos(a), math.sin(a)
        blob(k, [("polygon", [(x1 + 3.4 * tx, y1 + 3.4 * ty), (x1 + 3 * nx, y1 + 3 * ny), (x1 - 3 * nx, y1 - 3 * ny)])],
             (240, 240, 235), ink=0.7)


def heart(k):
    """Hug: a heart."""
    m = k.mask("ellipse", 1, 2, 8.6, 9.6) | k.mask("ellipse", 7.4, 2, 15, 9.6) | k.mask("polygon", [(1.5, 7.5), (14.5, 7.5), (8, 15)])
    k.fill(m, (235, 95, 120), INVARIANT)
    k.outline(m, 0.8)


def countdown(k):
    """Winding up: an hourglass, sand running (D11 draws the turns left beside it)."""
    blob(k, [("polygon", [(3, 1.5), (13, 1.5), (8.8, 8), (13, 14.5), (3, 14.5), (7.2, 8)])], (230, 230, 240), ink=0.8)
    k.fill(k.mask("polygon", [(5, 3), (11, 3), (8, 7)]), (240, 200, 90), INVARIANT)
    k.fill(k.mask("polygon", [(8, 11), (11.5, 13.5), (4.5, 13.5)]), (240, 200, 90), INVARIANT)


# In BootGlyph order. None: a slot ART4 does not draw. None itself is nothing; Swords is
# unused by kBoot and not drawn (no weapons on the toy); Tier, Skull and the specials
# are ART3's tier and power glyphs, which D11 picks.
NAMES = ["None", "Pellet", "Moon", "Swords", "Peek", "Flower", "Arrow", "Check", "Lock", "Download", "Friend", "Door",
         "Tier", "Skull", "SpecialQuick", "SpecialHeavy", "OpenHand", "Plead", "Candle", "Rematch", "Heart", "Countdown"]
DRAW = [None, pellet, moon, None, peek, flower, next_arrow, check, lock, download, friend, door,
        None, None, None, None, open_hand, plead, candle, rematch, heart, countdown]
HOLD_STEPS = 8


def greyed(s):
    """Not now: the same glyph in flat greys, its ink kept, so its shape still says what."""
    out = sp.Sprite(s.rgb.copy(), s.region.copy(), s.origin, list(s.eyes))
    lum = (299 * s.rgb[..., 0].astype(int) + 587 * s.rgb[..., 1].astype(int) + 114 * s.rgb[..., 2].astype(int)) // 1000
    m = (s.region != sp.CLEAR) & (lum >= 60)
    g = np.clip(70 + lum * 0.25, 0, 255).astype(np.uint8)
    out.rgb[m] = np.repeat(g[m][:, None], 3, 1)
    return out


def hold_ring(step):
    """The ring round the glyph that fills clockwise from the top while BOOT is held: a
    dark track, lit for step / HOLD_STEPS of the way round. It sits on the rim, round a
    glyph, never on a pet."""
    k = Sketch(22, 22)
    track = k.mask("ellipse", 0.5, 0.5, 21.5, 21.5) & ~k.mask("ellipse", 2.3, 2.3, 19.7, 19.7)
    k.fill(track, (60, 60, 64), INVARIANT)
    if step:
        wedge = k.mask("polygon", [(11, 11)] + arc(11, 11, 14, 14, -90, -90 + 360 * step / HOLD_STEPS, max(2, 4 * step)))
        k.fill(track & wedge, (250, 250, 245), INVARIANT)
    return k.sprite((11, 11))


def double_mark():
    """A double press also does something: two short bars under the glyph."""
    k = Sketch(12, 4)
    for x in (1, 7):
        blob(k, [("polygon", [(x, 0.8), (x + 4, 0.8), (x + 4, 3.2), (x, 3.2)])], (240, 240, 235), ink=0.6)
    return k.sprite((6, 2))


def frames():
    out = {}
    for name, draw in zip(NAMES, DRAW):
        if draw:
            s = glyph(draw, 16)
            out[f"Boot{name}"] = s
            out[f"Boot{name}Grey"] = greyed(s)
    for i in range(HOLD_STEPS + 1):
        out[f"HoldRing{i}"] = hold_ring(i)
    out["DoublePress"] = double_mark()
    return out
