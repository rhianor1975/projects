"""Modelled monsters, 32x32, each with a 2-frame idle and a lunge frame.

A body is drawn once per family palette, so a Vine Rat and an Ashen Rat are
the same rat in different colours -- the way the C game's generated variants
reuse a curated glyph. Families follow the five depth tiers.
"""
import math

from shade import Canvas, overlay, ramp

FAMILIES = {
    # body, belly/accent, eye glow
    "jungle":    ((104, 150, 72), (214, 192, 132), (255, 84, 64)),
    "vermin":    ((132, 104, 80), (220, 190, 160), (255, 72, 72)),
    "clockwork": ((196, 142, 64), (120, 208, 232), (120, 240, 255)),
    "ruins":     ((88, 160, 196), (232, 240, 200), (255, 250, 160)),
    "outrider":  ((204, 186, 156), (176, 64, 48), (255, 120, 64)),
    "abyssal":   ((132, 60, 168), (232, 96, 72), (255, 220, 96)),
    "spirit":    ((140, 184, 248), (232, 244, 255), (255, 255, 255)),
    "flame":     ((240, 128, 40), (255, 236, 140), (255, 255, 255)),
    "boss":      ((168, 36, 56), (232, 196, 88), (255, 240, 120)),
}


def _mats(family):
    body, acc, glow = FAMILIES[family]
    return ramp(body), ramp(acc), glow


def _eyes(img, pts, glow):
    pal = {"g": glow, "w": (255, 255, 255), "d": tuple(int(v * 0.55) for v in glow)}
    for (x, y) in pts:
        overlay(img, int(x), int(y), ["wg", "gd"], pal)


def monster(kind, family, frame=0):
    """frame 0/1 idle bob, 2 lunge."""
    c = Canvas(32, 32)
    body, acc, glow = _mats(family)
    bob = [0.0, -0.8, 0.0][frame]
    lunge = 3.0 if frame == 2 else 0.0
    eyes = []
    if kind == "rat":
        c.limb(24, 23, 30.5, 28, 1.3, 0.5, acc)                       # tail
        c.ellipsoid(19, 22 + bob, 9, 6.5, body)
        c.ellipsoid(17, 25.5 + bob, 6, 3, acc)
        for fx in (13, 22):
            c.ellipsoid(fx - lunge * 0.5, 28.5, 2.2, 1.4, acc)
        c.ellipsoid(10 - lunge, 19 + bob, 6, 5, body)
        c.ellipsoid(5 - lunge, 21 + bob, 2.8, 2.2, acc)
        c.ellipsoid(11 - lunge, 13.8 + bob, 2.4, 3.0, body)
        c.ellipsoid(11 - lunge, 14.2 + bob, 1.3, 1.8, acc)
        eyes = [(7 - lunge, 17 + bob)]
    elif kind == "serpent":
        pts = [(25, 28), (19, 29), (12, 27.5), (9, 23), (13, 19.5), (19, 17.5), (20, 12.5)]
        for i in range(len(pts) - 1):
            r0 = 4.0 - i * 0.35
            c.limb(pts[i][0], pts[i][1] + bob * (i / 6), pts[i + 1][0], pts[i + 1][1] + bob * (i / 6),
                   r0, r0 - 0.35, body, gloss=0.5)
        c.ellipsoid(17 - lunge, 9 + bob, 5.6, 4.4, body, gloss=0.6)
        c.ellipsoid(16 - lunge, 11.2 + bob, 3.6, 2.0, acc)
        c.limb(11.5 - lunge, 11 + bob, 8 - lunge, 12.5 + bob, 0.6, 0.4, ramp((232, 64, 80)))
        eyes = [(14 - lunge, 7 + bob), (19 - lunge, 7 + bob)]
    elif kind == "tribal":
        c.limb(13, 21 + bob, 12, 29, 2.2, 1.9, body)
        c.limb(19, 21 + bob, 20, 29, 2.2, 1.9, body)
        c.poly([(11, 13 + bob), (21, 13 + bob), (20, 22 + bob), (12, 22 + bob)], body, round_=2.4)
        c.poly([(11.5, 19 + bob), (20.5, 19 + bob), (21.5, 24 + bob), (10.5, 24 + bob)], acc, round_=1.4)
        c.limb(21, 14 + bob, 25 - lunge * 0.3, 20 + bob - lunge, 1.9, 1.7, body)
        c.limb(25.5 - lunge * 0.3, 4 + bob - lunge, 25.5 - lunge * 0.3, 29 + bob - lunge, 0.8, 0.8, ramp((150, 100, 60)))
        c.limb(25.5 - lunge * 0.3, 1.5 - lunge, 25.5 - lunge * 0.3, 5 - lunge, 1.8, 0.2, ramp((210, 214, 230)), gloss=1.0)
        c.limb(11, 14 + bob, 7.5, 21 + bob, 1.9, 1.7, body)
        c.ellipsoid(16, 9 + bob, 5.4, 5.2, body)
        c.poly([(10.5, 7.5 + bob), (21.5, 7.5 + bob), (20.5, 11.5 + bob), (11.5, 11.5 + bob)], ramp((220, 200, 150)), round_=1.4)
        for fx, col in ((12, (232, 72, 64)), (16, (248, 200, 72)), (20, (72, 168, 232))):
            c.limb(fx, 5 + bob, fx + (fx - 16) * 0.4, -0.5 + bob, 1.4, 0.4, ramp(col))
        eyes = [(13, 8 + bob), (17, 8 + bob)]
    elif kind == "wisp":
        for i in range(5):
            a = i * 1.26 + frame * 0.5
            c.limb(16, 20 + bob, 16 + math.cos(a) * 5, 9 + bob + math.sin(a) * 2 - 6, 4.5, 0.6, body, flat=False)
        c.ellipsoid(16, 18 + bob, 8.5 + (1 if frame == 2 else 0), 8, body, gloss=0.6)
        c.ellipsoid(16, 19 + bob, 5, 4.6, acc, gloss=0.9)
        c.limb(16, 25 + bob, 16 + (frame - 1) * 2, 31, 3.5, 0.5, body)
        eyes = [(12, 16 + bob), (18, 16 + bob)]
    elif kind == "automaton":
        metal = body
        c.limb(13, 22 + bob, 12.5, 29, 2.4, 2.2, metal)
        c.limb(19, 22 + bob, 19.5, 29, 2.4, 2.2, metal)
        c.ellipsoid(12.5, 29.5, 3, 1.6, metal)
        c.ellipsoid(19.5, 29.5, 3, 1.6, metal)
        c.poly([(9, 12 + bob), (23, 12 + bob), (21.5, 23 + bob), (10.5, 23 + bob)], metal, round_=2.2, gloss=0.6)
        c.ellipsoid(16, 17 + bob, 3.4, 3.4, acc, gloss=1.6)
        c.ellipsoid(8, 13.5 + bob, 3.2, 3.0, metal, gloss=0.8)
        c.ellipsoid(24, 13.5 + bob, 3.2, 3.0, metal, gloss=0.8)
        c.limb(7.5, 15 + bob, 6 - lunge, 22 + bob, 2.0, 2.4, metal)
        c.limb(24.5, 15 + bob, 26 + lunge, 22 + bob - lunge, 2.0, 2.4, metal)
        c.poly([(11, 3 + bob), (21, 3 + bob), (21, 11 + bob), (11, 11 + bob)], metal, round_=2.0, gloss=0.7)
        c.limb(16, 3 + bob, 16, -0.5 + bob, 0.6, 0.6, metal)
        c.ellipsoid(16, 0.8 + bob, 1.4, 1.4, acc, gloss=1.4)
        eyes = [(12.5, 6 + bob), (17.5, 6 + bob)]
    elif kind == "wraith":
        c.poly([(8, 12 + bob), (24, 12 + bob), (27, 30), (22, 27), (19, 31), (16, 27.5), (13, 31), (10, 27), (5, 30)],
               body, round_=3.0)
        c.limb(9, 14 + bob, 3 - lunge, 22 + bob - lunge, 2.2, 1.0, body)
        c.limb(23, 14 + bob, 29 + lunge, 22 + bob - lunge, 2.2, 1.0, body)
        c.ellipsoid(16, 9 + bob, 7.4, 7.0, body, gloss=0.4)
        c.ellipsoid(16, 10.5 + bob, 4.6, 4.4, ramp((40, 32, 72)))
        eyes = [(13, 9 + bob), (17, 9 + bob)]
    elif kind == "golem":
        c.limb(11, 22, 10, 29, 3.6, 3.2, body)
        c.limb(21, 22, 22, 29, 3.6, 3.2, body)
        c.ellipsoid(16, 17 + bob, 11, 8.5, body)
        c.ellipsoid(16, 18 + bob, 4, 4, acc, gloss=1.2)
        c.ellipsoid(4.5 - lunge, 21 + bob, 4.2, 5.5, body)
        c.ellipsoid(27.5 + lunge, 21 + bob, 4.2, 5.5, body)
        c.ellipsoid(16, 7 + bob, 5.5, 4.5, body)
        eyes = [(13, 6 + bob), (17, 6 + bob)]
    elif kind == "hound":
        c.limb(25, 18 + bob, 30, 13 + bob, 1.5, 0.6, body)
        c.ellipsoid(18, 19 + bob, 9, 5.5, body)
        for fx in (11, 14, 21, 24):
            c.limb(fx - lunge * 0.5, 22 + bob, fx - lunge * 0.7, 29.5, 1.5, 1.3, body)
        c.ellipsoid(17, 22.5 + bob, 6, 2.2, acc)
        c.ellipsoid(8 - lunge, 15 + bob, 5, 4.6, body)
        c.ellipsoid(3.8 - lunge, 17 + bob, 2.6, 2.0, acc)
        c.limb(9 - lunge, 11 + bob, 11 - lunge, 6.5 + bob, 1.8, 0.4, body)
        eyes = [(6 - lunge, 13.5 + bob)]
    elif kind == "horror":
        for i in range(6):
            a = math.pi * (0.15 + i * 0.14)
            c.limb(16, 20, 16 + math.cos(a) * 15 * (1 if i % 2 else -1), 22 + math.sin(a) * 9 + (frame - 1),
                   2.4, 0.6, acc)
        c.ellipsoid(16, 16 + bob, 12, 11, body, gloss=0.5)
        c.ellipsoid(16, 21 + bob, 7, 3.4, ramp((60, 16, 32)))
        eyes = [(10, 11 + bob), (15, 9 + bob), (20, 11 + bob)]
    img = c.render()
    _eyes(img, eyes, glow)
    if kind == "serpent" or kind == "tribal":
        pass
    return img


BOSS_KINDS = ("warden",)


def warden(frame=0):
    """The Warden of the Deep Well, 48x48: the last thing in the game."""
    c = Canvas(48, 48)
    body, acc, glow = _mats("boss")
    dark = ramp((48, 28, 64))
    bob = [0.0, -1.0, 0.0][frame]
    c.poly([(10, 18 + bob), (38, 18 + bob), (44, 46), (4, 46)], dark, round_=4)
    c.limb(9, 20 + bob, 1, 34 + bob, 4, 2.6, body)
    c.limb(39, 20 + bob, 47, 34 + bob, 4, 2.6, body)
    c.ellipsoid(24, 24 + bob, 10, 9, body, gloss=0.6)
    c.ellipsoid(24, 25 + bob, 4.5, 4.5, acc, gloss=1.6)
    c.ellipsoid(9, 18 + bob, 6, 4.5, acc, gloss=0.9)
    c.ellipsoid(39, 18 + bob, 6, 4.5, acc, gloss=0.9)
    c.ellipsoid(24, 11 + bob, 7, 7, body)
    for (x1, y1) in ((14, -1), (34, -1), (24, -3)):
        c.limb(24 + (x1 - 24) * 0.3, 7 + bob, x1, y1 + bob + 2, 2.0, 0.4, acc, gloss=1.0)
    c.limb(3, 6, 3, 46, 1.2, 1.2, ramp((140, 140, 160)))
    c.limb(3, 2, 12, 9, 1.0, 3.0, ramp((210, 214, 230)), gloss=1.2)
    img = c.render()
    _eyes(img, [(20, 10 + bob), (26, 10 + bob)], glow)
    return img
