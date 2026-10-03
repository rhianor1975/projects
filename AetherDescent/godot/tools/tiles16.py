"""16-bit terrain tiles.

Every tile is built from per-biome ramps of five or six shades, shaded as a
surface rather than filled as a pattern: flagstones are Voronoi cells with a
bevel, wall faces are blocks with lit and shadowed edges, water is layered.
Noise is periodic, so every tile tiles with itself and with its neighbours.
"""
import math
import random

import numpy as np
from PIL import Image

from shade import ramp

T = 16

BIOMES = {
    #           stone base       growth           wall face        wall top
    "jungle":     ((112, 116, 100), (84, 156, 64),  (118, 112, 96),  (44, 52, 44)),
    "industrial": ((124, 108, 92),  (212, 150, 56), (150, 96, 64),   (54, 44, 40)),
    "ruins":      ((136, 146, 162), (90, 176, 204), (162, 170, 188), (46, 52, 68)),
    "wastes":     ((176, 146, 116), (204, 84, 60),  (190, 130, 100), (72, 48, 44)),
    "abyss":      ((88, 74, 116),   (176, 92, 224), (110, 88, 148),  (30, 24, 48)),
}


def _noise(seed, cells=4, octaves=3):
    """Periodic value noise on a 16x16 tile, 0..1."""
    rng = np.random.default_rng(seed)
    out = np.zeros((T, T))
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = cells * (2 ** o)
        g = rng.random((n, n))
        ys, xs = np.mgrid[0:T, 0:T] / T * n
        x0, y0 = np.floor(xs).astype(int), np.floor(ys).astype(int)
        fx, fy = xs - x0, ys - y0
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        x1, y1 = (x0 + 1) % n, (y0 + 1) % n
        x0, y0 = x0 % n, y0 % n
        v = (g[y0, x0] * (1 - fx) * (1 - fy) + g[y0, x1] * fx * (1 - fy) +
             g[y1, x0] * (1 - fx) * fy + g[y1, x1] * fx * fy)
        out += v * amp
        tot += amp
        amp *= 0.5
    return out / tot


def _to_img(idx, rmp, alpha=None):
    img = Image.new("RGBA", (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            c = rmp[int(np.clip(idx[y, x], 0, len(rmp) - 1))]
            a = 255 if alpha is None else int(alpha[y, x])
            px[x, y] = c + (a,)
    return img


def floor(biome, seed=0, growth=0.0):
    """Irregular flagstones: periodic Voronoi cells, each bevelled and tinted."""
    stone, grow, _, _ = BIOMES[biome]
    rmp = ramp(stone, n=6, lo=0.45, hi=1.12)
    rng = random.Random(seed * 7919 + hash(biome) % 1000)
    pts = [(rng.uniform(0, T), rng.uniform(0, T)) for _ in range(5)]
    tint = [rng.uniform(-0.45, 0.45) for _ in pts]
    ys, xs = np.mgrid[0:T, 0:T] + 0.5
    d1 = np.full((T, T), 1e9)
    d2 = np.full((T, T), 1e9)
    owner = np.zeros((T, T), dtype=int)
    for i, (px_, py_) in enumerate(pts):
        for ox in (-T, 0, T):
            for oy in (-T, 0, T):
                d = np.hypot(xs - px_ - ox, ys - py_ - oy)
                closer = d < d1
                d2 = np.where(closer, d1, np.minimum(d2, d))
                owner = np.where(closer, i, owner)
                d1 = np.where(closer, d, d1)
    edge = d2 - d1                                   # distance to the mortar line
    n = _noise(seed + 11, 4, 3)
    shade = 3.0 + np.array(tint)[owner] + (n - 0.5) * 1.4
    # bevel: lit on the upper-left side of each stone, shadowed lower-right
    gy, gx = np.gradient(edge)
    shade += np.clip(-(gx + gy) * 1.6, -1.2, 1.2) * (edge < 2.5)
    shade = np.where(edge < 0.9, 0.6, shade)        # mortar
    shade = np.where((edge >= 0.9) & (edge < 1.6), shade - 0.6, shade)
    img = _to_img(np.round(shade), rmp)
    if growth > 0:
        g = _noise(seed + 101, 3, 3)
        gr = ramp(grow, n=5, lo=0.45, hi=1.15)
        px = img.load()
        thresh = 1.0 - growth
        for y in range(T):
            for x in range(T):
                v = g[y, x]
                if v > thresh or (v > thresh - 0.06 and (x + y) % 2 == 0):
                    k = int(np.clip((v - thresh) * 14 + 1 + (0.6 if edge[y, x] < 1.2 else 0), 0, 4))
                    px[x, y] = gr[k] + (255,)
    return img


def grass(biome, seed=0):
    """Ground gone to growth: a dense floor of tufts."""
    _, grow, _, _ = BIOMES[biome]
    gr = ramp(grow, n=6, lo=0.40, hi=1.15)
    n = _noise(seed + 5, 4, 3)
    idx = 1.6 + n * 2.2
    img = _to_img(np.round(idx), gr)
    px = img.load()
    rng = random.Random(seed)
    for _ in range(7):                                # blade tufts
        x, y = rng.randrange(1, 15), rng.randrange(3, 16)
        for k, (dx, dy) in enumerate(((0, 0), (-1, -1), (1, -1), (0, -2), (-1, -2))):
            if 0 <= x + dx < T and 0 <= y + dy < T:
                px[x + dx, y + dy] = gr[min(5, 3 + k // 2)] + (255,)
        if y + 1 < T:
            px[x, y + 1] = gr[0] + (255,)
    return img


def flowers(biome, seed=0):
    img = grass(biome, seed)
    px = img.load()
    rng = random.Random(seed + 3)
    for _ in range(3):
        x, y = rng.randrange(2, 14), rng.randrange(2, 14)
        petal = ramp(rng.choice(((240, 108, 160), (250, 224, 96), (236, 236, 255), (150, 120, 248))), n=4)
        for dx, dy, k in ((0, -1, 3), (-1, 0, 2), (1, 0, 2), (0, 1, 1)):
            px[x + dx, y + dy] = petal[k] + (255,)
        px[x, y] = (255, 214, 72, 255)
    return img


def wall_face(biome, seed=0, growth=0.0):
    """Ashlar blocks with a lit top lip and a shadowed foot."""
    _, grow, face, top = BIOMES[biome]
    rmp = ramp(face, n=6, lo=0.40, hi=1.12)
    n = _noise(seed + 3, 4, 3)
    rng = random.Random(seed + 77)
    idx = np.zeros((T, T))
    rows = [(2, 8), (8, 15)]                           # two courses of block
    for r, (y0, y1) in enumerate(rows):
        off = (rng.randrange(0, 8) + r * 5) % 16
        joints = sorted({off % 16, (off + rng.choice((6, 7, 8, 9))) % 16})
        for y in range(y0, y1):
            for x in range(T):
                xx = x
                rel_y = y - y0
                base = 3.0 + (n[y, x] - 0.5) * 1.3
                if rel_y == 0:
                    base = 4.6                          # lit top edge of the course
                elif rel_y == y1 - y0 - 1:
                    base = 1.0                          # shadowed underside
                if xx in joints:
                    base = 0.7
                elif (xx - 1) % 16 in joints:
                    base += 0.9                         # lit left edge after a joint
                elif (xx + 1) % 16 in joints:
                    base -= 0.8
                idx[y, x] = base
    top_r = ramp(top, n=4, lo=0.6, hi=1.3)
    img = _to_img(np.round(idx), rmp)
    px = img.load()
    for x in range(T):
        px[x, 0] = top_r[3] + (255,)                     # the rim where top meets face
        px[x, 1] = rmp[5] + (255,)
        px[x, 15] = rmp[0] + (255,)
    for _ in range(2):                                    # cracks
        x, y = rng.randrange(2, 14), rng.randrange(4, 12)
        for k in range(rng.randrange(2, 5)):
            if 0 <= x < T and 2 <= y < 15:
                px[x, y] = rmp[0] + (255,)
            x += rng.choice((-1, 0, 1)); y += 1
    if growth > 0:
        gr = ramp(grow, n=5, lo=0.45, hi=1.15)
        for x in range(T):                                # moss and vines off the lip
            if rng.random() < growth:
                L = rng.randrange(2, 10)
                for y in range(2, 2 + L):
                    px[x, y] = gr[max(0, 3 - (y - 2) // 3 - (x % 2))] + (255,)
                px[x, 2 + L - 1] = gr[0] + (255,)
    return img


def wall_top(biome, seed=0):
    _, _, _, top = BIOMES[biome]
    rmp = ramp(top, n=5, lo=0.55, hi=1.35)
    n = _noise(seed + 9, 4, 2)
    return _to_img(np.round(0.8 + n * 1.8), rmp)


def water(frame=0, seed=0):
    deep = ramp((40, 92, 184), n=6, lo=0.45, hi=1.25)
    n = _noise(seed + 40, 2, 2)
    img = Image.new("RGBA", (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            w = math.sin((x * 0.8 + y * 0.35) + frame * math.pi / 2) * 0.5 + 0.5
            v = 1.4 + n[y, x] * 1.4 + (1.0 if w > 0.86 else 0)
            px[x, y] = deep[int(np.clip(round(v), 0, 5))] + (255,)
    rng = random.Random(seed * 3 + frame)
    for _ in range(2):                                   # glints
        x, y = rng.randrange(1, 15), rng.randrange(1, 15)
        px[x, y] = (232, 248, 255, 255)
        px[x - 1, y] = deep[5] + (255,)
    return img


def lava(frame=0, seed=0):
    hot = [(96, 16, 16), (168, 32, 16), (224, 80, 24), (248, 152, 40), (255, 220, 96), (255, 250, 200)]
    n = _noise(seed + 70, 3, 3)
    img = Image.new("RGBA", (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            v = n[(y + frame * 2) % T, x] * 5.5 + math.sin(x * 0.7 + frame) * 0.4
            px[x, y] = hot[int(np.clip(round(v), 0, 5))] + (255,)
    return img


def thicket(biome, seed=0):
    """A clump of broad-leaved undergrowth that hides what is behind it."""
    from shade import Canvas
    _, grow, _, _ = BIOMES[biome]
    base = grass(biome, seed)
    c = Canvas(T, T, ss=4, ambient=0.35)
    leaves = ramp(grow, n=6, lo=0.30, hi=1.2)
    rng = random.Random(seed + 13)
    for _ in range(9):
        x, y = rng.uniform(2, 14), rng.uniform(3, 13)
        c.ellipsoid(x, y, rng.uniform(2.6, 3.8), rng.uniform(2.2, 3.2), leaves, gloss=0.4)
    leaf = c.render(contour=True)
    base.alpha_composite(leaf)
    return base


def stairs_down(biome):
    img = floor(biome, 3)
    stone, _, face, top = BIOMES[biome]
    rmp = ramp(face, n=6, lo=0.25, hi=1.1)
    px = img.load()
    for i in range(4):
        y0 = 3 + i * 3
        for y in range(y0, y0 + 3):
            for x in range(2 + i, 14 - i):
                k = 4 - i if y == y0 else max(0, 3 - i - (y - y0))
                px[x, y] = rmp[k] + (255,)
        for x in range(2 + i, 14 - i):
            px[x, y0 + 2] = rmp[0] + (255,)
    for y in range(3, 15):
        px[1 + (y - 3) // 3, y] = rmp[0] + (255,)
        px[14 - (y - 3) // 3, y] = rmp[0] + (255,)
    return img


def cobble(seed=0):
    """Town plaza cobbles: warm and rounded, more inviting than dungeon stone."""
    return floor_town(seed)


def floor_town(seed=0):
    rmp = ramp((176, 160, 140), n=6, lo=0.40, hi=1.12)
    rng = random.Random(seed + 500)
    img = Image.new("RGBA", (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            px[x, y] = rmp[0] + (255,)
    for cy in range(2, T + 4, 4):
        for cx in range(((cy // 4) % 2) * 2, T + 4, 4):
            tint = rng.uniform(-0.6, 0.6)
            for y in range(cy - 2, cy + 2):
                for x in range(cx - 2, cx + 2):
                    dx, dy = x - cx + 0.5, y - cy + 0.5
                    if dx * dx + dy * dy > 4.2:
                        continue
                    v = 3 + tint - (dx + dy) * 0.55
                    px[x % T, y % T] = rmp[int(np.clip(round(v), 1, 5))] + (255,)
    return img


def roof(seed=0, colour=(196, 84, 60)):
    rmp = ramp(colour, n=6, lo=0.40, hi=1.12)
    img = Image.new("RGBA", (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            row = y // 4
            xx = (x + (3 if row % 2 else 0)) % 6
            k = 3
            if y % 4 == 3:
                k = 0
            elif y % 4 == 2:
                k = 2
            elif y % 4 == 0:
                k = 4
            if xx == 0:
                k = max(0, k - 2)
            elif xx == 1:
                k = min(5, k + 1)
            px[x, y] = rmp[k] + (255,)
    return img


def house_wall(seed=0, window=True):
    plaster = ramp((236, 220, 188), n=5, lo=0.6, hi=1.05)
    beam = ramp((122, 78, 50), n=5)
    n = _noise(seed + 61, 4, 2)
    img = _to_img(np.round(2.2 + n * 1.6), plaster)
    px = img.load()
    for x in range(T):
        px[x, 0] = beam[3] + (255,); px[x, 1] = beam[1] + (255,)
        px[x, 14] = beam[2] + (255,); px[x, 15] = beam[0] + (255,)
    for y in range(T):
        px[0, y] = beam[2] + (255,); px[15, y] = beam[0] + (255,)
    if window:
        glass = [(48, 40, 72), (248, 196, 96), (255, 232, 160)]
        for y in range(4, 11):
            for x in range(4, 12):
                edge = x in (4, 11) or y in (4, 10)
                c = beam[1] if edge else (glass[2] if (x < 8 and y < 7) else glass[1])
                if x == 8 or y == 7:
                    c = beam[1]
                px[x, y] = c + (255,)
        for x in range(3, 13):
            px[x, 11] = beam[3] + (255,)
    return img


def door(sign=(248, 208, 72)):
    img = house_wall(window=False)
    px = img.load()
    wood = ramp((150, 92, 52), n=5)
    for y in range(4, 16):
        for x in range(4, 12):
            k = 2
            if x in (4, 11) or y == 4:
                k = 0
            elif x in (5, 8):
                k = 3
            elif x == 7:
                k = 1
            px[x, y] = wood[k] + (255,)
    px[10, 10] = (255, 220, 96, 255)
    s = ramp(sign, n=4)
    for y in range(0, 3):
        for x in range(2, 14):
            px[x, y] = s[3 - y] + (255,)
    return img


def fountain():
    from shade import Canvas
    img = floor_town(7)
    c = Canvas(T, T, ss=4)
    stone = ramp((200, 196, 210), n=6, lo=0.4, hi=1.1)
    c.ellipsoid(8, 9, 7.6, 6.4, stone)
    c.ellipsoid(8, 9, 5.8, 4.6, ramp((56, 120, 220), n=6, lo=0.5, hi=1.3), gloss=1.2)
    c.ellipsoid(8, 7, 1.8, 3.4, stone)
    c.ellipsoid(8, 3.5, 1.4, 1.6, ramp((180, 230, 255)), gloss=1.5)
    img.alpha_composite(c.render())
    return img


def torch():
    """A wall bracket with a flame -- composited onto a wall face."""
    from shade import Canvas
    c = Canvas(T, T, ss=4)
    c.limb(8, 13, 8, 8, 1.0, 1.2, ramp((120, 84, 60)))
    c.ellipsoid(8, 7.5, 2.2, 1.0, ramp((90, 90, 100)), gloss=0.8)
    c.limb(8, 7, 8, 2, 2.4, 0.4, ramp((255, 150, 40), n=5, lo=0.7, hi=1.3), gloss=0.0, flat=True)
    c.ellipsoid(8, 5.2, 1.1, 1.6, [(255, 240, 180)] * 3, flat=True)
    return c.render(outline=False)
