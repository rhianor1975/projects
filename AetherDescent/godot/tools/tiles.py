"""32x32 terrain tiles, the size the 32x48 heroes stand on.

Every tile is built from per-biome ramps of five or six shades and shaded as
a surface rather than filled as a pattern: flagstones are Voronoi cells with
a bevel, wall faces are courses of block with lit and shadowed edges, water
is layered. Noise is periodic, so a tile tiles with itself and its kin.
Shaded props (thickets, torches, fountains) use the sprite modeller.
"""
import math
import random

import numpy as np
from PIL import Image

from shade import Canvas, ramp

T = 32
K = T / 16          # model units are 16 to a tile; props are drawn at K px a unit

BIOMES = {
    #           stone base       growth           wall face        wall top
    "jungle":     ((112, 116, 100), (84, 156, 64),  (118, 112, 96),  (44, 52, 44)),
    "industrial": ((124, 108, 92),  (212, 150, 56), (150, 96, 64),   (54, 44, 40)),
    "ruins":      ((136, 146, 162), (90, 176, 204), (162, 170, 188), (46, 52, 68)),
    "wastes":     ((176, 146, 116), (204, 84, 60),  (190, 130, 100), (72, 48, 44)),
    "abyss":      ((88, 74, 116),   (176, 92, 224), (110, 88, 148),  (30, 24, 48)),
}


def _noise(seed, cells=4, octaves=3):
    """Periodic value noise over one tile, 0..1."""
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


def _to_img(idx, rmp):
    img = Image.new("RGBA", (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            px[x, y] = rmp[int(np.clip(idx[y, x], 0, len(rmp) - 1))] + (255,)
    return img


def _voronoi(pts):
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
    return d2 - d1, owner


def floor(biome, seed=0, growth=0.0):
    """Irregular flagstones, each bevelled and tinted, with grit and wear."""
    stone, grow, _, _ = BIOMES[biome]
    rmp = ramp(stone, n=7, lo=0.42, hi=1.14)
    rng = random.Random(seed * 7919 + sum(map(ord, biome)))
    # jittered grid of seeds, so the slabs are even-sized rather than random blobs
    pts = []
    for gy in range(3):
        for gx in range(3):
            pts.append(((gx + 0.5 + rng.uniform(-0.3, 0.3)) * T / 3, (gy + 0.5 + rng.uniform(-0.3, 0.3)) * T / 3))
    tint = [rng.uniform(-0.6, 0.6) for _ in pts]
    edge, owner = _voronoi(pts)
    n = _noise(seed + 11, 4, 4)
    shade = 3.4 + np.array(tint)[owner] + (n - 0.5) * 1.8
    gy, gx = np.gradient(edge)
    shade += np.clip(-(gx + gy) * 2.0, -1.4, 1.4) * (edge < 3.5)   # bevel: lit top-left
    shade = np.where(edge < 1.0, 0.4, shade)                        # mortar
    shade = np.where((edge >= 1.0) & (edge < 1.8), shade - 0.9, shade)
    img = _to_img(np.round(shade), rmp)
    px = img.load()
    for _ in range(6):                                               # pits and chips
        x, y = rng.randrange(T), rng.randrange(T)
        if edge[y, x] > 3:
            px[x, y] = rmp[1] + (255,)
            if x + 1 < T:
                px[x + 1, y] = rmp[5] + (255,)
    if growth > 0:
        g = _noise(seed + 101, 3, 4)
        gr = ramp(grow, n=6, lo=0.42, hi=1.18)
        thresh = 1.0 - growth
        for y in range(T):
            for x in range(T):
                v = g[y, x]
                if v > thresh or (v > thresh - 0.05 and (x + y) % 2 == 0):
                    k = int(np.clip((v - thresh) * 16 + 1 + (0.8 if edge[y, x] < 1.8 else 0), 0, 5))
                    px[x, y] = gr[k] + (255,)
    return img


def grass(biome, seed=0):
    """Ground gone to growth: a dense floor of tufts."""
    _, grow, _, _ = BIOMES[biome]
    gr = ramp(grow, n=7, lo=0.38, hi=1.18)
    n = _noise(seed + 5, 4, 4)
    img = _to_img(np.round(1.6 + n * 2.6), gr)
    px = img.load()
    rng = random.Random(seed)
    for _ in range(22):                                              # blade tufts
        x, y = rng.randrange(1, T - 1), rng.randrange(4, T)
        for k, (dx, dy) in enumerate(((0, 0), (-1, -1), (1, -1), (0, -2), (-1, -3), (1, -3), (0, -4))):
            if 0 <= x + dx < T and 0 <= y + dy < T:
                px[x + dx, y + dy] = gr[min(6, 3 + k // 2)] + (255,)
        if y + 1 < T:
            px[x, y + 1] = gr[0] + (255,)
    return img


def flowers(biome, seed=0):
    img = grass(biome, seed)
    px = img.load()
    rng = random.Random(seed + 3)
    for _ in range(6):
        x, y = rng.randrange(3, T - 3), rng.randrange(3, T - 3)
        petal = ramp(rng.choice(((240, 108, 160), (250, 224, 96), (236, 236, 255), (150, 120, 248))), n=4)
        for dx, dy, k in ((0, -2, 3), (-1, -1, 3), (1, -1, 2), (-2, 0, 2), (2, 0, 1), (-1, 1, 1), (1, 1, 1), (0, 2, 0)):
            px[x + dx, y + dy] = petal[k] + (255,)
        for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1), (0, 0)):
            px[x + dx, y + dy] = petal[2] + (255,)
        px[x, y] = (255, 214, 72, 255)
    return img


def wall_face(biome, seed=0, growth=0.0):
    """Four courses of ashlar with a lit lip on top and a shadowed foot."""
    _, grow, face, top = BIOMES[biome]
    rmp = ramp(face, n=7, lo=0.38, hi=1.14)
    n = _noise(seed + 3, 4, 4)
    rng = random.Random(seed + 77)
    idx = np.zeros((T, T))
    courses = [(3, 10), (10, 17), (17, 24), (24, 31)]
    for r, (y0, y1) in enumerate(courses):
        off = rng.randrange(0, 16)
        joints = sorted({off % T, (off + rng.choice((12, 14, 16, 18))) % T})
        for y in range(y0, y1):
            for x in range(T):
                rel_y = y - y0
                base = 3.6 + (n[y, x] - 0.5) * 1.6
                if rel_y == 0:
                    base = 5.6                      # lit top edge of the course
                elif rel_y == 1:
                    base = 4.6
                elif rel_y == y1 - y0 - 1:
                    base = 0.8                      # shadowed underside
                if x in joints:
                    base = 0.6
                elif (x - 1) % T in joints:
                    base += 1.2                     # lit left edge after a joint
                elif (x + 1) % T in joints:
                    base -= 1.0
                idx[y, x] = base
    top_r = ramp(top, n=4, lo=0.6, hi=1.4)
    img = _to_img(np.round(idx), rmp)
    px = img.load()
    for x in range(T):
        px[x, 0] = top_r[3] + (255,)                # the rim where top meets face
        px[x, 1] = top_r[2] + (255,)
        px[x, 2] = rmp[6] + (255,)
        px[x, T - 1] = rmp[0] + (255,)
    for _ in range(3):                              # cracks
        x, y = rng.randrange(3, T - 3), rng.randrange(5, T - 6)
        for k in range(rng.randrange(3, 7)):
            if 0 <= x < T and 3 <= y < T - 1:
                px[x, y] = rmp[0] + (255,)
                if x + 1 < T:
                    px[x + 1, y] = rmp[5] + (255,)
            x += rng.choice((-1, 0, 1))
            y += 1
    if growth > 0:
        gr = ramp(grow, n=6, lo=0.42, hi=1.18)
        for x in range(T):                          # moss and vines off the lip
            if rng.random() < growth:
                L = rng.randrange(3, 18)
                for y in range(3, 3 + L):
                    px[x, y] = gr[max(0, 4 - (y - 3) // 4 - (x % 2))] + (255,)
                px[x, 3 + L - 1] = gr[0] + (255,)
                if rng.random() < 0.3:
                    px[x, 3 + L] = gr[5] + (255,)   # a leaf at the end of the vine
    return img


def wall_top(biome, seed=0):
    _, _, _, top = BIOMES[biome]
    rmp = ramp(top, n=5, lo=0.55, hi=1.35)
    n = _noise(seed + 9, 4, 3)
    return _to_img(np.round(0.8 + n * 1.9), rmp)


def water(frame=0, seed=0):
    deep = ramp((40, 92, 184), n=7, lo=0.42, hi=1.28)
    n = _noise(seed + 40, 2, 3)
    img = Image.new("RGBA", (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            w = math.sin(x * 0.45 + math.sin(y * 0.4) * 1.5 + frame * math.pi / 2) * 0.5 + 0.5
            v = 1.6 + n[y, x] * 1.8 + (1.4 if w > 0.9 else (0.6 if w > 0.75 else 0))
            px[x, y] = deep[int(np.clip(round(v), 0, 6))] + (255,)
    rng = random.Random(seed * 3 + frame)
    for _ in range(3):                              # glints
        x, y = rng.randrange(2, T - 2), rng.randrange(2, T - 2)
        px[x, y] = (240, 252, 255, 255)
        px[x - 1, y] = deep[6] + (255,)
        px[x + 1, y] = deep[6] + (255,)
    return img


def lava(frame=0, seed=0):
    hot = [(96, 16, 16), (160, 28, 16), (216, 72, 24), (244, 136, 40), (255, 200, 80), (255, 240, 170), (255, 255, 230)]
    n = _noise(seed + 70, 3, 4)
    img = Image.new("RGBA", (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            v = n[(y + frame * 3) % T, x] * 6.5 + math.sin(x * 0.35 + frame) * 0.5
            px[x, y] = hot[int(np.clip(round(v), 0, 6))] + (255,)
    return img


def thicket(biome, seed=0):
    """A clump of broad-leaved undergrowth that hides what is behind it."""
    _, grow, _, _ = BIOMES[biome]
    base = grass(biome, seed)
    c = Canvas(T, T, ss=3, ambient=0.35, cel=True, scale=K)
    leaves = ramp(grow)
    rng = random.Random(seed + 13)
    for _ in range(11):
        x, y = rng.uniform(2.5, 13.5), rng.uniform(3, 13.5)
        c.ellipsoid(x, y, rng.uniform(2.4, 3.6), rng.uniform(2.0, 3.0), leaves, gloss=0.4)
    base.alpha_composite(c.render())
    return base


def stairs_down(biome):
    img = floor(biome, 3)
    _, _, face, _ = BIOMES[biome]
    rmp = ramp(face, n=7, lo=0.22, hi=1.12)
    px = img.load()
    for i in range(5):
        y0 = 5 + i * 5
        for y in range(y0, y0 + 5):
            for x in range(4 + i * 2, T - 4 - i * 2):
                k = 6 - i if y == y0 else max(0, 4 - i - (y - y0))
                px[x, y] = rmp[k] + (255,)
        for x in range(4 + i * 2, T - 4 - i * 2):
            px[x, y0 + 4] = rmp[0] + (255,)
    for y in range(5, 30):                           # the stair well's side walls
        k = (y - 5) // 5
        px[3 + k * 2, y] = rmp[0] + (255,)
        px[T - 4 - k * 2, y] = rmp[0] + (255,)
    return img


def floor_town(seed=0):
    """Town plaza cobbles: warm and rounded, more inviting than dungeon stone."""
    rmp = ramp((176, 160, 140), n=7, lo=0.38, hi=1.14)
    rng = random.Random(seed + 500)
    img = Image.new("RGBA", (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            px[x, y] = rmp[0] + (255,)
    R = 3.3
    for cy in range(3, T + 7, 7):
        for cx in range(((cy // 7) % 2) * 3, T + 7, 7):
            tint = rng.uniform(-0.7, 0.7)
            for y in range(cy - 4, cy + 4):
                for x in range(cx - 4, cx + 4):
                    dx, dy = x - cx + 0.5, y - cy + 0.5
                    d2 = dx * dx + dy * dy
                    if d2 > R * R:
                        continue
                    v = 3.6 + tint - (dx + dy) * 0.42 - (1.0 if d2 > (R - 1) ** 2 else 0)
                    px[x % T, y % T] = rmp[int(np.clip(round(v), 1, 6))] + (255,)
    return img


def roof(seed=0, colour=(196, 84, 60)):
    rmp = ramp(colour, n=7, lo=0.40, hi=1.14)
    img = Image.new("RGBA", (T, T))
    px = img.load()
    for y in range(T):
        for x in range(T):
            row = y // 6
            xx = (x + (4 if row % 2 else 0)) % 8
            yy = y % 6
            k = [5, 4, 4, 3, 2, 0][yy]
            if xx == 0:
                k = max(0, k - 3)
            elif xx == 1:
                k = min(6, k + 1)
            px[x, y] = rmp[k] + (255,)
    return img


def house_wall(seed=0, window=True):
    plaster = ramp((236, 220, 188), n=6, lo=0.6, hi=1.06)
    beam = ramp((122, 78, 50), n=5)
    n = _noise(seed + 61, 4, 3)
    img = _to_img(np.round(2.4 + n * 2.0), plaster)
    px = img.load()
    for x in range(T):
        for y, k in ((0, 4), (1, 3), (2, 1), (T - 3, 3), (T - 2, 1), (T - 1, 0)):
            px[x, y] = beam[k] + (255,)
    for y in range(T):
        for x, k in ((0, 3), (1, 1), (T - 2, 1), (T - 1, 0)):
            px[x, y] = beam[k] + (255,)
    if window:
        glass_lit, glass_mid = (255, 236, 168), (248, 196, 96)
        for y in range(8, 21):
            for x in range(8, 24):
                edge = x in (8, 23) or y in (8, 20)
                if edge:
                    c = beam[1]
                elif x in (15, 16) or y == 14:
                    c = beam[2]
                else:
                    c = glass_lit if (x - 8) + (y - 8) < 9 else glass_mid
                px[x, y] = c + (255,)
        for x in range(6, 26):                       # the sill
            px[x, 21] = beam[4] + (255,)
            px[x, 22] = beam[1] + (255,)
    return img


def door(sign=(248, 208, 72)):
    img = house_wall(window=False)
    px = img.load()
    wood = ramp((150, 92, 52), n=6)
    for y in range(8, T):
        for x in range(8, 24):
            k = 3
            if x in (8, 23) or y == 8:
                k = 0
            elif x in (9, 16):
                k = 5
            elif x in (15, 22):
                k = 1
            elif y in (14, 24):
                k = 1
            px[x, y] = wood[k] + (255,)
    for (x, y) in ((20, 19), (21, 19), (20, 20)):
        px[x, y] = (255, 220, 96, 255)
    s = ramp(sign, n=5)
    for y in range(0, 6):
        for x in range(5, T - 5):
            px[x, y] = s[4 - min(4, y)] + (255,)
    return img


def fountain():
    img = floor_town(7)
    c = Canvas(T, T, ss=3, scale=K, cel=True)
    stone = ramp((200, 196, 210))
    c.ellipsoid(8, 9, 7.6, 6.4, stone)
    c.ellipsoid(8, 9, 5.8, 4.6, ramp((56, 120, 220)), gloss=1.2)
    c.ellipsoid(8, 7, 1.8, 3.4, stone)
    c.ellipsoid(8, 3.5, 1.4, 1.6, ramp((180, 230, 255)), gloss=1.5)
    img.alpha_composite(c.render())
    return img


def torch():
    """A wall bracket with a flame, composited onto a wall face."""
    c = Canvas(T, T, ss=3, scale=K)
    c.limb(8, 13, 8, 8, 1.0, 1.2, ramp((120, 84, 60)))
    c.ellipsoid(8, 7.5, 2.2, 1.0, ramp((90, 90, 100)), gloss=0.8)
    c.limb(8, 7, 8, 2, 2.4, 0.4, ramp((255, 150, 40), n=5, lo=0.7, hi=1.3), flat=True)
    c.ellipsoid(8, 5.2, 1.1, 1.6, [(255, 240, 180)] * 3, flat=True)
    return c.render(outline=False)
