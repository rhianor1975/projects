"""16x16 terrain tiles, built procedurally from per-biome colour ramps.

SNES dungeons are drawn in three-quarter view: a wall with open floor below it
shows its *face* (brickwork you can see), and wall with wall below shows only
its dark top. The renderer picks between WALL_TOP and WALL_FACE per tile.
"""
import random
from PIL import Image

T = 16

# floor dark/mid/light, wall top dark/mid, wall face dark/mid/light, growth dark/mid/light
BIOMES = {
    "jungle": {
        "floor": [(70, 74, 64), (98, 104, 88), (128, 134, 112)],
        "top": [(28, 36, 32), (44, 56, 48)],
        "face": [(56, 60, 56), (88, 92, 84), (120, 124, 112)],
        "grow": [(40, 96, 48), (72, 144, 64), (136, 200, 88)],
    },
    "industrial": {
        "floor": [(72, 64, 56), (104, 92, 80), (140, 124, 104)],
        "top": [(32, 28, 28), (52, 44, 40)],
        "face": [(96, 64, 48), (136, 92, 64), (176, 128, 88)],
        "grow": [(144, 96, 32), (208, 152, 56), (248, 208, 112)],
    },
    "ruins": {
        "floor": [(84, 92, 104), (120, 128, 140), (164, 172, 184)],
        "top": [(32, 36, 48), (52, 60, 76)],
        "face": [(104, 112, 128), (144, 152, 168), (192, 200, 212)],
        "grow": [(56, 120, 152), (96, 176, 200), (184, 232, 240)],
    },
    "wastes": {
        "floor": [(120, 96, 80), (160, 132, 108), (200, 172, 140)],
        "top": [(48, 32, 32), (72, 48, 44)],
        "face": [(136, 88, 72), (176, 120, 96), (216, 164, 128)],
        "grow": [(152, 48, 40), (200, 80, 56), (240, 136, 96)],
    },
    "abyss": {
        "floor": [(48, 40, 64), (72, 60, 96), (100, 84, 132)],
        "top": [(16, 12, 28), (32, 24, 48)],
        "face": [(64, 48, 88), (92, 72, 124), (128, 104, 168)],
        "grow": [(112, 48, 160), (168, 88, 216), (224, 160, 248)],
    },
}


def _img():
    return Image.new("RGBA", (T, T), (0, 0, 0, 0))


def _put(px, x, y, c):
    if 0 <= x < T and 0 <= y < T:
        px[x, y] = c + (255,) if len(c) == 3 else c


def floor(b, seed=0, overgrown=False):
    """Flagstones: a 2x2 grid of slabs with bevelled edges and grit."""
    r = random.Random(seed)
    pal = BIOMES[b]
    d, m, l = pal["floor"]
    img = _img(); px = img.load()
    off = r.choice((0, 4, 8))
    for y in range(T):
        for x in range(T):
            xx = (x + (off if y >= 8 else 0)) % T
            c = m
            if y % 8 == 0 or xx % 8 == 0:
                c = d
            elif y % 8 == 1 or xx % 8 == 1:
                c = l
            elif r.random() < 0.08:
                c = d
            _put(px, x, y, c)
    if overgrown:
        gd, gm, gl = pal["grow"]
        for _ in range(3):
            cx, cy = r.randrange(T), r.randrange(T)
            for _ in range(14):
                x = cx + r.randint(-3, 3); y = cy + r.randint(-2, 2)
                _put(px, x, y, r.choice((gd, gm, gm, gl)))
    return img


def wall_top(b, seed=0):
    r = random.Random(seed)
    d, m = BIOMES[b]["top"]
    img = _img(); px = img.load()
    for y in range(T):
        for x in range(T):
            _put(px, x, y, m if r.random() < 0.18 else d)
    return img


def wall_face(b, seed=0, overgrown=False):
    """Brick courses with a lit top lip, the side of the wall you look at."""
    r = random.Random(seed)
    pal = BIOMES[b]
    d, m, l = pal["face"]
    img = _img(); px = img.load()
    for y in range(T):
        for x in range(T):
            row = y // 4
            xx = (x + (4 if row % 2 else 0)) % 8
            c = m
            if y % 4 == 3 or xx == 7:
                c = d
            elif y % 4 == 0:
                c = l
            _put(px, x, y, c)
    for x in range(T):        # the lip where the top meets the face
        _put(px, x, 0, BIOMES[b]["top"][1])
        _put(px, x, 1, l)
    for x in range(T):        # contact shadow at the foot
        _put(px, x, T - 1, d)
    if overgrown:
        gd, gm, gl = pal["grow"]
        for x in range(T):     # vines hanging off the lip
            if r.random() < 0.55:
                for y in range(2, 2 + r.randint(1, 7)):
                    _put(px, x, y, gm if (x + y) % 3 else gd)
                _put(px, x, 2, gl)
    return img


def water(frame=0):
    img = _img(); px = img.load()
    deep, mid, light = (32, 72, 160), (48, 112, 200), (152, 208, 248)
    for y in range(T):
        for x in range(T):
            _put(px, x, y, deep if (y // 4) % 2 else mid)
    for k in range(3):
        y = 3 + k * 5
        x0 = (k * 5 + frame * 3) % T
        for i in range(4):
            _put(px, (x0 + i) % T, y, light)
    return img


def lava(frame=0):
    img = _img(); px = img.load()
    for y in range(T):
        for x in range(T):
            v = ((x * 3 + y * 5 + frame * 4) // 4) % 4
            _put(px, x, y, [(176, 40, 16), (224, 88, 24), (248, 160, 40), (224, 88, 24)][v])
    return img


def thicket(b, seed=0):
    """Round leafy bushes, blocking sight -- a jungle staple."""
    r = random.Random(seed)
    gd, gm, gl = BIOMES[b]["grow"]
    img = floor(b, seed); px = img.load()
    for cx, cy, rad in ((5, 6, 5), (11, 5, 5), (8, 10, 6)):
        for y in range(T):
            for x in range(T):
                dd = (x - cx) ** 2 + (y - cy) ** 2
                if dd <= rad * rad:
                    c = gm
                    if dd >= (rad - 1) ** 2:
                        c = (20, 40, 24)
                    elif (x - cx) + (y - cy) < -rad // 2:
                        c = gl
                    elif r.random() < 0.15:
                        c = gd
                    _put(px, x, y, c)
    return img


def flowers(b, seed=0):
    r = random.Random(seed)
    img = floor(b, seed, overgrown=True); px = img.load()
    for _ in range(4):
        x, y = r.randrange(2, 14), r.randrange(2, 14)
        c = r.choice(((248, 120, 160), (255, 232, 96), (240, 240, 255)))
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
            _put(px, x + dx, y + dy, c)
        _put(px, x, y, (255, 200, 64))
    return img


def stairs_down(b):
    img = floor(b, 3); px = img.load()
    d = BIOMES[b]["top"][0]
    steps = [BIOMES[b]["floor"][2], BIOMES[b]["floor"][1], BIOMES[b]["floor"][0], d]
    for i, c in enumerate(steps):
        for y in range(3 + i * 3, 6 + i * 3):
            for x in range(2 + i, 14 - i):
                _put(px, x, y, c)
        for x in range(2 + i, 14 - i):
            _put(px, x, 3 + i * 3, (24, 16, 40))
    return img


def stairs_up(b):
    img = floor(b, 4); px = img.load()
    steps = [BIOMES[b]["face"][0], BIOMES[b]["face"][1], BIOMES[b]["face"][2], (240, 240, 232)]
    for i, c in enumerate(steps):
        for y in range(12 - i * 3, 15 - i * 3):
            for x in range(2, 14):
                _put(px, x, y, c)
        for x in range(2, 14):
            _put(px, x, 14 - i * 3, (24, 16, 40))
    return img


def cobble(seed=0):
    """Town plaza: rounded cobbles."""
    r = random.Random(seed)
    img = _img(); px = img.load()
    base = [(120, 112, 104), (152, 144, 128), (184, 176, 160)]
    for y in range(T):
        for x in range(T):
            _put(px, x, y, (72, 64, 64))
    for cy in range(2, T, 5):
        for cx in range((cy // 5) % 2 * 2 + 2, T, 5):
            for y in range(cy - 2, cy + 2):
                for x in range(cx - 2, cx + 2):
                    c = base[1]
                    if y == cy - 2 or x == cx - 2:
                        c = base[2]
                    elif y == cy + 1 or x == cx + 1:
                        c = base[0]
                    _put(px, x, y, c)
    return img


def roof(seed=0, col=((152, 56, 48), (200, 88, 64), (232, 136, 96))):
    """Terracotta/brass-green roof tiles, seen from above."""
    d, m, l = col
    img = _img(); px = img.load()
    for y in range(T):
        for x in range(T):
            xx = (x + (2 if (y // 4) % 2 else 0)) % 4
            c = m
            if y % 4 == 3:
                c = d
            elif xx == 0:
                c = l
            _put(px, x, y, c)
    return img


def house_wall(seed=0):
    img = _img(); px = img.load()
    plaster, shade, beam = (232, 216, 184), (200, 180, 152), (112, 72, 48)
    for y in range(T):
        for x in range(T):
            c = plaster if (x + y * 3) % 11 else shade
            if x in (0, 15) or y in (0, 15):
                c = beam
            _put(px, x, y, c)
    # a window
    for y in range(4, 10):
        for x in range(5, 11):
            _put(px, x, y, (24, 16, 40) if x in (5, 10) or y in (4, 9) else (248, 216, 120))
    for x in range(5, 11):
        _put(px, x, 7, (24, 16, 40))
    return img


def door(sign_rgb=(248, 208, 72)):
    img = house_wall(); px = img.load()
    for y in range(4, 16):
        for x in range(4, 12):
            c = (128, 80, 48)
            if x in (4, 11) or y == 4:
                c = (64, 40, 24)
            elif x == 8:
                c = (96, 56, 32)
            _put(px, x, y, c)
    _put(px, 10, 10, (248, 208, 72))
    for y in range(0, 3):          # hanging sign colour strip
        for x in range(3, 13):
            _put(px, x, y, sign_rgb)
    return img


def fountain():
    img = cobble(5); px = img.load()
    for y in range(T):
        for x in range(T):
            dd = (x - 7.5) ** 2 + (y - 8) ** 2
            if dd <= 49:
                _put(px, x, y, (160, 160, 176) if dd > 30 else (64, 128, 216))
            if dd <= 6:
                _put(px, x, y, (200, 232, 255))
    return img
