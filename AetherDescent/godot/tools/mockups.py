#!/usr/bin/env python3
"""Art-direction mockups for the Godot port.

Renders screens at the game's native 384x216 and writes them scaled up to
../mockups/, plus an animation sheet and an animated GIF of the party
walking and fighting. Built from the same modelled sprites and tiles the
game's assets come from -- these are pictures of the intended look, not
screenshots of running code.

    python3 tools/mockups.py      (from AetherDescent/godot)
"""
import math
import os
import random
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import monsters as MON  # noqa: E402
import tiles16 as TL  # noqa: E402
from characters import OY, Pose, Spec, field_sprite, portrait  # noqa: E402
from pixelart import draw_text, draw_window, paste, shadow, text_width  # noqa: E402

W, H = 384, 216
SCALE = 4
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mockups")

WHITE = (248, 248, 248)
GREY = (168, 176, 200)
GOLDC = (248, 216, 96)
CYAN = (128, 224, 248)
GREEN = (136, 232, 120)
INK = (24, 16, 40)

CAST = {
    "kael": Spec("blonde", "spiky", cloth="steel", trim="gold", scarf="crimson", legs="navy",
                 pauldrons=True, extras={"weapon": "sword"}),
    "mira": Spec("pink", "long", cloth="violet", trim="gold", legs="plum", hat="wizard", hat_col="violet",
                 female=True, gloves="", extras={"weapon": "staff"}),
    "rook": Spec("red", "tail", cloth="forest", trim="brass", legs="sand", hat="band", hat_col="crimson",
                 eyes=(72, 160, 88), extras={"weapon": "dagger"}),
    "sable": Spec("navy", "wild", skin="warm", cloth="brass", trim="leather", legs="black", hat="goggles",
                  eyes=(200, 120, 48), extras={"weapon": "hammer"}),
    "vesper": Spec("silver", "bob", cloth="crimson", trim="gold", legs="black", cape="navy", female=True,
                   eyes=(176, 64, 96), gloves="", extras={"weapon": "rapier"}),
    "ivy": Spec("green", "long", cloth="white", trim="teal", legs="teal", hat="hood", hat_col="teal",
                female=True, gloves="", eyes=(72, 168, 120)),
}

_cache = {}


def hero(name, pose=None):
    key = ("h", name, repr(pose))
    if key not in _cache:
        _cache[key] = field_sprite(CAST[name], pose or Pose())
    return _cache[key]


def face(name, expr="neutral"):
    key = ("p", name, expr)
    if key not in _cache:
        _cache[key] = portrait(CAST[name], expr)
    return _cache[key]


def mon(kind, fam, frame=0):
    key = ("m", kind, fam, frame)
    if key not in _cache:
        _cache[key] = MON.monster(kind, fam, frame)
    return _cache[key]


def framed_portrait(img, x, y, name, expr="neutral"):
    draw_window(img, x - 4, y - 4, 56, 56, top=(40, 40, 104), bottom=(16, 16, 56))
    paste(img, face(name, expr), x, y)


def bar(img, x, y, w, frac, col, back=(24, 24, 56)):
    px = img.load()
    fill = int(w * frac)
    for xx in range(-1, w + 1):
        for yy in range(-1, 4):
            if xx in (-1, w) or yy in (-1, 3):
                px[x + xx, y + yy] = (8, 8, 24, 255)
            elif xx < fill:
                c = col if yy == 1 else (tuple(min(255, v + 70) for v in col) if yy == 0
                                         else tuple(int(v * 0.7) for v in col))
                px[x + xx, y + yy] = c + (255,)
            else:
                px[x + xx, y + yy] = back + (255,)


# ---- map + lighting -----------------------------------------------------------
def draw_map(img, rows, biome, ox=0, oy=0):
    cache = {}

    def t(key, fn):
        if key not in cache:
            cache[key] = fn()
        return cache[key]

    torches = []
    for ty, row in enumerate(rows):
        for tx, ch in enumerate(row):
            seed = (tx * 31 + ty * 17) % 6
            below = rows[ty + 1][tx] if ty + 1 < len(rows) else '#'
            X, Y = ox + tx * 16, oy + ty * 16
            if ch in "#t":
                if below not in "#t":
                    tile = t(("face", seed % 3), lambda: TL.wall_face(biome, seed % 3, 0.35 if seed % 2 else 0.0))
                    paste(img, tile, X, Y)
                    if ch == 't':
                        paste(img, t("torch", TL.torch), X, Y - 2)
                        torches.append((X + 8, Y + 4))
                else:
                    paste(img, t(("top", seed % 2), lambda: TL.wall_top(biome, seed % 2)), X, Y)
            elif ch == '.':
                paste(img, t(("floor", seed), lambda: TL.floor(biome, seed)), X, Y)
            elif ch == ',':
                paste(img, t(("growf", seed), lambda: TL.floor(biome, seed, 0.45)), X, Y)
            elif ch == ';':
                paste(img, t(("grass", seed), lambda: TL.grass(biome, seed)), X, Y)
            elif ch == '*':
                paste(img, t(("flowers", seed), lambda: TL.flowers(biome, seed)), X, Y)
            elif ch == 'T':
                paste(img, t(("thicket", seed % 3), lambda: TL.thicket(biome, seed % 3)), X, Y)
            elif ch == '~':
                paste(img, t(("water", (tx + ty) % 2), lambda: TL.water((tx + ty) % 2, tx)), X, Y)
            elif ch == '>':
                paste(img, t("down", lambda: TL.stairs_down(biome)), X, Y)
            elif ch == 'L':
                paste(img, t(("lava", (tx + ty) % 2), lambda: TL.lava((tx + ty) % 2)), X, Y)
    contact_shadows(img, rows, ox, oy)
    return torches


def contact_shadows(img, rows, ox, oy):
    """Walls cast a short shadow onto the floor below them, and shores foam --
    the two cheapest depth cues a top-down map has."""
    px = img.load()
    for ty, row in enumerate(rows):
        for tx, ch in enumerate(row):
            above = rows[ty - 1][tx] if ty > 0 else '#'
            X, Y = ox + tx * 16, oy + ty * 16
            if ch not in "#t~" and above in "#t":
                for yy, k in ((0, 0.55), (1, 0.68), (2, 0.82)):
                    for xx in range(16):
                        if 0 <= X + xx < img.width and 0 <= Y + yy < img.height:
                            r, g, b, a = px[X + xx, Y + yy]
                            px[X + xx, Y + yy] = (int(r * k), int(g * k), min(255, int(b * k + 6)), a)
            if ch == '~' and above != '~':
                for xx in range(16):
                    if 0 <= X + xx < img.width and 0 <= Y < img.height:
                        px[X + xx, Y] = (216, 240, 255, 255)
                        if (xx + tx) % 3 and Y + 1 < img.height:
                            px[X + xx, Y + 1] = (150, 200, 240, 255)


def light(img, lights, ambient=0.50):
    """SNES colour-math style lighting: banded pools of warm light over a dim map."""
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            v = ambient
            warm = 0.0
            for (lx, ly, rad, strength) in lights:
                d = math.hypot(x - lx, (y - ly) * 1.15)
                if d < rad:
                    f = (1 - d / rad) * strength
                    v += f
                    warm += f
            v = min(1.12, v)
            v = round(v * 6) / 6                       # bands, not a smooth gradient
            warm = min(0.4, warm)
            r, g, b, a = px[x, y]
            px[x, y] = (min(255, int(r * v + 26 * warm)), min(255, int(g * v + 12 * warm)),
                        min(255, int(b * v * (1 - warm * 0.4))), a)


def fog(img, cells, ox=0, oy=0, alpha=170):
    over = Image.new("RGBA", img.size, (0, 0, 0, 0))
    opx = over.load()
    for (tx, ty) in cells:
        for y in range(16):
            for x in range(16):
                X, Y = ox + tx * 16 + x, oy + ty * 16 + y
                if 0 <= X < img.width and 0 <= Y < img.height:
                    opx[X, Y] = (6, 6, 28, alpha)
    img.alpha_composite(over)


def place(img, spr, tx, ty, ox=0, oy=0, kind="hero", dx=0, dy=0, flash=None):
    """Feet near the bottom of the tile, centred on it."""
    if kind == "hero":
        top = oy + ty * 16 + 15 - (OY + 31)
        sw = 14
    else:
        top = oy + ty * 16 + 15 - 30
        sw = 16
    left = ox + tx * 16 + 8 - spr.width // 2
    paste(img, shadow(sw, 5), ox + tx * 16 + 8 - sw // 2 + dx, oy + ty * 16 + 12 + dy)
    if flash:
        spr = tint(spr, flash)
    paste(img, spr, left + dx, top + dy)


def tint(spr, col):
    out = spr.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = (min(255, (r + col[0] * 2) // 3), min(255, (g + col[1] * 2) // 3),
                            min(255, (b + col[2] * 2) // 3), a)
    return out


def damage_number(img, x, y, n, col=WHITE):
    s = str(n)
    draw_text(img, x - text_width(s), y, s, col=col, outline=INK, scale=2)


# ---- screens ----------------------------------------------------------------------
def screen_title():
    img = Image.new("RGBA", (W, H))
    px = img.load()
    top, mid, bot = (20, 14, 56), (132, 52, 112), (248, 156, 80)
    for y in range(H):
        t = y / H
        k = t / 0.6 if t < 0.6 else (t - 0.6) / 0.4
        a, b = (top, mid) if t < 0.6 else (mid, bot)
        c = tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))
        c = tuple((v // 6) * 6 for v in c)          # stepped like a 15-bit gradient
        for x in range(W):
            px[x, y] = c + (255,)
    r = random.Random(3)
    for _ in range(90):
        x, y = r.randrange(W), r.randrange(int(H * 0.5))
        px[x, y] = (255, 255, 230, 255) if r.random() < 0.3 else (200, 200, 255, 255)
    tiers = ((236, 22), (186, 20), (136, 18), (88, 16), (44, 16))
    ybase = H - 44
    for i, (w, hh) in enumerate(tiers):
        x0 = W // 2 - w // 2
        y1 = ybase - sum(t[1] for t in tiers[:i])
        y0 = y1 - hh
        for y in range(y0, y1):
            for x in range(x0, x0 + w):
                base = (120, 80, 120) if y == y0 else (40, 26, 60)
                if x < x0 + 3:
                    base = (60, 40, 84)
                px[x, y] = base + (255,)
        for x in range(x0 + 6, x0 + w - 6, 12):
            for y in range(y0 + 7, y0 + 12):
                if r.random() < 0.7:
                    for dx in range(2):
                        px[x + dx, y] = (255, 196, 96, 255) if y > y0 + 8 else (255, 240, 180, 255)
    top_y = ybase - sum(t[1] for t in tiers)
    for y in range(0, top_y):                         # aether pouring up out of the well
        for x in range(W // 2 - 8, W // 2 + 8):
            k = 1 - abs(x - W // 2) / 8
            if r.random() < k * 0.6:
                rr, gg, bb, aa = px[x, y]
                px[x, y] = (min(255, rr + 60), min(255, gg + 90), min(255, bb + 110), 255)
    for cx, rad in ((36, 34), (92, 20), (300, 30), (352, 22)):     # the city's green domes
        for y in range(H - 44 - rad, H - 44):
            for x in range(cx - rad, cx + rad):
                if (x - cx) ** 2 + (y - (H - 44)) ** 2 <= rad * rad:
                    px[x, y] = ((40, 88, 80) if (x - cx) < -rad * 0.3 else (24, 56, 56)) + (255,)
    for y in range(H - 44, H):
        for x in range(W):
            px[x, y] = (14, 36, 28, 255) if (x * 7 + y * 3) % 11 else (22, 60, 40, 255)
    gold = [(255, 250, 210), (255, 232, 140), (250, 196, 72), (224, 148, 40), (184, 104, 32), (152, 72, 24), (112, 48, 24)]
    title = "AETHER DESCENT"
    draw_text(img, (W - text_width(title) * 3) // 2, 30, title, col=gold, outline=(32, 16, 40), scale=3)
    sub = "One hundred floors down."
    draw_text(img, (W - text_width(sub)) // 2, 62, sub, col=(232, 216, 248))
    draw_window(img, W - 100, 118, 88, 46)
    for i, opt in enumerate(("New Game", "Continue", "Settings")):
        draw_text(img, W - 80, 126 + i * 12, opt, col=WHITE if i == 0 else GREY)
    draw_text(img, W - 92, 126, "▶", col=GOLDC)
    draw_text(img, W - text_width("v0.1  Godot 4") - 6, H - 10, "v0.1  Godot 4", col=(120, 160, 128), shadow_col=None)
    for i, n in enumerate(("kael", "mira", "rook")):
        paste(img, shadow(14, 5), 23 + i * 24, H - 31)
        paste(img, hero(n, Pose("right", walk=i % 4)), 14 + i * 24, H - 30 - 38 + 4)
    return img


def screen_create():
    img = Image.new("RGBA", (W, H), (8, 8, 24, 255))
    draw_window(img, 4, 4, W - 8, 22)
    draw_text(img, 12, 11, "New Character  -  Choose a class", col=WHITE)
    draw_text(img, W - 92, 11, "Arcanist  4/7", col=CYAN)
    draw_window(img, 4, 28, 124, 156)
    classes = ["Arcanist-Engineer", "Aether-Sailor", "Lamp-Witch", "Static Hexer",
               "Glyph-Scribe", "Rune-Smith", "Void Cantor", "Ember Cleric",
               "Clock-Oracle", "Storm Caller", "Brass Theurge", "Mire Augur", "Cinder Saint"]
    for i, c in enumerate(classes):
        draw_text(img, 20, 36 + i * 11, c, col=WHITE if i == 1 else GREY)
    draw_text(img, 10, 47, "▶", col=GOLDC)
    draw_window(img, 132, 28, W - 136, 156)
    framed_portrait(img, 144, 38, "vesper")
    draw_text(img, 204, 38, "Aether-Sailor", col=GOLDC)
    for i, line in enumerate(("Void-current navigator.", "Eerie, unshaken; bends", "gravity with a scope.")):
        draw_text(img, 204, 50 + i * 10, line, col=WHITE)
    stats = [("HP", "42"), ("ATK", "8"), ("DEF", "5"), ("AE", "14"),
             ("Evasion", "8%"), ("Crit", "0%"), ("Ward", "2%"), ("Vision", "6")]
    for i, (k, v) in enumerate(stats):
        x = 144 + (i % 2) * 76
        y = 98 + (i // 2) * 11
        draw_text(img, x, y, k, col=CYAN)
        draw_text(img, x + 64 - text_width(v), y, v, col=WHITE)
    draw_text(img, 144, 146, "School", col=CYAN)
    draw_text(img, 196, 146, "Aether-Sense", col=WHITE)
    draw_text(img, 144, 157, "Ability", col=CYAN)
    draw_text(img, 196, 157, "Steady Footing", col=WHITE)
    draw_text(img, 144, 168, "Leads", col=CYAN)
    draw_text(img, 196, 168, "AETHER-SENSE 9  RESOLVE 8", col=GREEN)
    for i, f in enumerate(("down", "left", "up", "right")):      # turning to show itself off
        paste(img, hero("vesper", Pose(f)), 304 + (i % 2) * 34, 92 + (i // 2) * 40)
    draw_window(img, 4, 186, W - 8, 26)
    draw_text(img, 12, 195, "Z: choose   X: back   <>: archetype", col=GREY)
    return img


DUNGEON = [
    "########################",
    "#####t######t###########",
    "###..,,;;..#####~~~~####",
    "###.,;**;...####~~~~~###",
    "##.....;;TT......~~~~###",
    "##..,.....TT.........,.#",
    "##.....>.......,.....;;#",
    "######..........*...;;;#",
    "######...,.........;;;;#",
    "#~~~~##.........TT;;;;##",
    "#~~~~##..,......TT;;;;;#",
    "#~~~~~#...........;;;;;#",
    "############t###########",
    "########################",
]


def dungeon_frame(oy, actors, party_xy, effects=()):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    torches = draw_map(img, DUNGEON, "jungle", oy=oy)
    for a in sorted(actors, key=lambda a: a[2] + (a[5] if len(a) > 5 else 0) / 16):
        spr, tx, ty, kind = a[:4]
        dx = a[4] if len(a) > 4 else 0
        dy = a[5] if len(a) > 5 else 0
        flash = a[6] if len(a) > 6 else None
        place(img, spr, tx, ty, oy=oy, kind=kind, dx=dx, dy=dy, flash=flash)
    lights = [(x, y, 64, 0.45) for (x, y) in torches]
    lights.append((party_xy[0], party_xy[1], 96, 0.55))
    light(img, lights, ambient=0.42)
    fog(img, [(x, y) for x in range(0, 6) for y in range(0, 14)] + [(x, y) for x in range(15, 24) for y in range(0, 4)],
        oy=oy, alpha=120)
    for fx in effects:
        fx(img)
    return img


def hud(img, hp=(62, 86), msg=("", "")):
    draw_window(img, 2, 2, 164, 50)
    paste(img, face("kael").crop((8, 8, 40, 40)), 8, 10)
    draw_text(img, 46, 9, "Kael", col=WHITE)
    draw_text(img, 80, 9, "Lv 7", col=GREY)
    draw_text(img, 46, 21, "HP", col=CYAN)
    bar(img, 60, 23, 60, hp[0] / hp[1], (88, 216, 96))
    draw_text(img, 126, 21, "%d/%d" % hp, col=WHITE)
    draw_text(img, 46, 33, "AE", col=CYAN)
    bar(img, 60, 35, 60, 0.45, (96, 160, 248))
    draw_text(img, 126, 33, "9/20", col=WHITE)
    for i, n in enumerate(("mira", "rook")):            # the rest of the party, compact
        x = 170 + i * 52
        draw_window(img, x, 2, 50, 30)
        paste(img, face(n).crop((10, 10, 38, 38)).resize((14, 14), Image.NEAREST), x + 5, 8)
        bar(img, x + 22, 10, 22, [0.9, 0.55][i], (88, 216, 96))
        bar(img, x + 22, 18, 22, [0.7, 0.3][i], (96, 160, 248))
    draw_window(img, W - 104, 34, 102, 30)
    draw_text(img, W - 96, 40, "B12", col=GOLDC)
    draw_text(img, W - 74, 40, "Jungle Roots", col=WHITE)
    draw_text(img, W - 96, 51, "G", col=GOLDC)
    draw_text(img, W - 86, 51, "1,284", col=WHITE)
    draw_text(img, W - 50, 51, "Keys 1", col=GREY)
    draw_window(img, W - 58, 66, 56, 38, top=(24, 24, 64), bottom=(8, 8, 32))
    mp = img.load()
    for y in range(14):
        for x in range(24):
            ch = DUNGEON[y][x]
            if ch in "#t":
                continue
            c = (64, 112, 200) if ch == '~' else (88, 140, 80)
            if ch == '>':
                c = (255, 224, 96)
            mp[W - 52 + x * 2, 72 + y * 2] = c + (255,)
    mp[W - 52 + 22, 72 + 14] = (255, 255, 255, 255)
    draw_window(img, 2, H - 38, W - 4, 36)
    draw_text(img, 10, H - 32, msg[0], col=WHITE)
    draw_text(img, 10, H - 21, msg[1], col=GREY)
    draw_text(img, W - 16, H - 15, "▼", col=GOLDC, shadow_col=None)
    return img


def screen_dungeon():
    oy = -4
    actors = [
        (hero("kael", Pose("right", attack=1)), 11, 7, "hero"),
        (hero("mira", Pose("right", cast=True)), 9, 8, "hero"),
        (hero("rook", Pose("up", walk=1)), 11, 9, "hero"),
        (mon("tribal", "jungle", 2), 12, 7, "mon"),
        (mon("serpent", "jungle", 1), 15, 9, "mon"),
        (mon("rat", "vermin", 2), 14, 5, "mon"),
    ]

    def bolt(img):
        px = img.load()
        x0, y0, x1, y1 = 9 * 16 + 14, 8 * 16 + oy - 14, 14 * 16 + 6, 5 * 16 + oy + 2
        for i in range(48):
            t = i / 47
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t - math.sin(t * math.pi) * 10
            if i % 2 == 0:
                for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
                    px[int(x) + dx, int(y) + dy] = (255, 255, 200, 255) if t > 0.6 else (180, 140, 255, 255)
        for k in range(8):
            a = k * math.pi / 4
            for rr in range(2, 7):
                px[int(x1 + math.cos(a) * rr), int(y1 + math.sin(a) * rr)] = \
                    (255, 255, 255, 255) if rr < 4 else (200, 170, 255, 255)
        damage_number(img, 12 * 16 + 14, 7 * 16 + oy - 30, 31)
        draw_text(img, 14 * 16 - 2, 5 * 16 + oy - 22, "Miss", col=CYAN, outline=INK)

    img = dungeon_frame(oy, actors, (11 * 16 + 8, 7 * 16), effects=[bolt])
    return hud(img, msg=("Kael strikes the Tribal Skirmisher for 31!",
                         "Mira casts Brass Arc -- the Vine Rat slips aside."))


TOWN = [
    "RRRRRRR.RRRRRRR.QQQQQQQQ",
    "RRRRRRR.RRRRRRR.QQQQQQQQ",
    "WWDWWWW.WWWDWWW.WWWWDWWW",
    "........................",
    ";*.....................*",
    "...........F............",
    "........................",
    "..QQQQQ...........QQQQQ.",
    "..QQQQQ.....>.....QQQQQ.",
    "..WWDWW...........WWDWW.",
    "........................",
    ";;......................",
    "*;......................",
    "........................",
]


def screen_town():
    img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    roof_a = TL.roof()
    roof_b = TL.roof(colour=(72, 156, 140))
    wall = TL.house_wall()
    signs = [(248, 208, 72), (200, 64, 64), (96, 160, 248), (136, 216, 120), (200, 120, 232)]
    cob = [TL.floor_town(i) for i in range(4)]
    for ty, row in enumerate(TOWN):
        for tx, ch in enumerate(row):
            X, Y = tx * 16, ty * 16 - 8
            paste(img, cob[(tx * 7 + ty) % 4], X, Y)
            if ch == 'R':
                paste(img, roof_a, X, Y)
            elif ch == 'Q':
                paste(img, roof_b, X, Y)
            elif ch == 'W':
                paste(img, wall, X, Y)
            elif ch == 'D':
                paste(img, TL.door(signs[(tx + ty) % len(signs)]), X, Y)
            elif ch == 'F':
                paste(img, TL.fountain(), X, Y)
            elif ch == '*':
                paste(img, TL.flowers("jungle", tx), X, Y)
            elif ch == ';':
                paste(img, TL.grass("jungle", tx + ty), X, Y)
            elif ch == '>':
                paste(img, TL.stairs_down("abyss"), X, Y)
    walls = [''.join('#' if ch in "WDRQ" else '.' for ch in r) for r in TOWN]
    contact_shadows(img, walls, 0, -8)
    actors = [(hero("kael", Pose("down", walk=1)), 11, 6), (hero("mira", Pose("down", walk=3)), 10, 6),
              (hero("rook", Pose("down")), 12, 6), (hero("ivy", Pose("right", walk=2)), 4, 4),
              (hero("sable", Pose("left")), 19, 11)]
    for spr, tx, ty in sorted(actors, key=lambda a: a[2]):
        place(img, spr, tx, ty, oy=-8)
    for label, tx, ty in (("ARMORY", 2, 2), ("ARCANIST", 11, 2), ("BANK", 20, 2), ("INN", 4, 9), ("TAVERN", 20, 9)):
        w = text_width(label) + 10
        x = max(2, min(W - w - 2, tx * 16 + 8 - w // 2))
        draw_window(img, x, ty * 16 - 8 - 13, w, 14)
        draw_text(img, x + 5, ty * 16 - 8 - 10, label, col=WHITE)
    draw_window(img, 2, H - 26, W - 4, 24)
    draw_text(img, 10, H - 18, "The plaza. Brass domes gone green; somebody still takes good coin.", col=WHITE)
    return img


def screen_shop():
    img = screen_town()
    img.alpha_composite(Image.new("RGBA", (W, H), (8, 8, 32, 150)))
    draw_window(img, 4, 4, 120, 22)
    draw_text(img, 14, 11, "Armory", col=GOLDC)
    draw_window(img, W - 100, 4, 96, 22)
    draw_text(img, W - 92, 11, "G", col=GOLDC)
    draw_text(img, W - 80, 11, "1,284", col=WHITE)
    draw_window(img, 4, 28, 216, 124)
    items = [("Rusty Cutlass", 15, "+2"), ("Brass Rapier", 55, "+5"), ("Riveted Cleaver", 110, "+8"),
             ("Steam-Forged Sabre", 200, "+12"), ("Clockwork Estoc", 350, "+16"),
             ("Star-iron Edge", 600, "+21"), ("Ether-Etched Blade", 1000, "+27"),
             ("Weapon +3", 300, "+5"), ("Armour +2", 260, "+5")]
    for i, (n, p, b) in enumerate(items):
        y = 36 + i * 12
        col = GOLDC if i == 3 else WHITE
        draw_text(img, 22, y, n, col=col)
        draw_text(img, 150 - text_width(b), y, b, col=GREEN)
        draw_text(img, 210 - text_width(str(p)), y, str(p), col=col)
    draw_text(img, 10, 72, "▶", col=GOLDC)
    draw_window(img, 224, 28, W - 228, 124)
    framed_portrait(img, 236, 38, "kael", "happy")
    draw_text(img, 296, 40, "Kael", col=WHITE)
    draw_text(img, 296, 51, "Gear-Knight", col=GREY)
    for i, (k, a, b, better) in enumerate((("ATK", "14", "21", True), ("DEF", "11", "11", False))):
        y = 100 + i * 12
        draw_text(img, 236, y, k, col=CYAN)
        draw_text(img, 264, y, a, col=WHITE)
        draw_text(img, 280, y, "▶", col=GOLDC)
        draw_text(img, 290, y, b, col=GREEN if better else WHITE)
    draw_text(img, 236, 128, "Now: Riveted Cleaver", col=GREY)
    paste(img, hero("kael", Pose("down", attack=0)), 330, 58)
    draw_window(img, 4, 156, W - 8, 56)
    draw_text(img, 12, 164, "Steam-Forged Sabre", col=GOLDC)
    draw_text(img, 12, 176, "A pressure-tempered blade. Hisses when it bites.", col=WHITE)
    draw_text(img, 12, 188, "Attack +12.  Upgrades you buy here carry over.", col=WHITE)
    draw_text(img, 12, 200, "Z: buy   X: leave   A/S: switch tab", col=GREY)
    return img


def sheet_animation():
    """Every frame of two heroes, the portraits' expressions, and the monsters."""
    names = ("kael", "mira")
    cols = 8
    sw, shh = 34, 42
    rows = []
    for n in names:
        for f in ("down", "left", "right", "up"):
            frames = [Pose(f, w) for w in range(4)] + [Pose(f, attack=a) for a in range(3)] + [Pose(f, cast=True)]
            rows.append((n, frames))
    img = Image.new("RGBA", (cols * sw + 8 + 4 * 52 + 12, len(rows) * shh + 128), (28, 32, 56, 255))
    for r, (n, frames) in enumerate(rows):
        for i, p in enumerate(frames):
            paste(img, shadow(14, 5), 4 + i * sw + 9, 4 + r * shh + 35)
            paste(img, hero(n, p), 4 + i * sw, 4 + r * shh)
    for i, t in enumerate(("walk 1", "walk 2", "walk 3", "walk 4", "wind", "strike", "follow", "cast")):
        draw_text(img, 4 + i * sw, len(rows) * shh + 4, t, col=GREY, shadow_col=None)
    x0 = cols * sw + 12
    for i, n in enumerate(("kael", "mira", "rook", "vesper")):
        for j, e in enumerate(("neutral", "happy", "angry", "hurt")):
            paste(img, face(n, e), x0 + i * 52, 4 + j * 52)
    for j, e in enumerate(("neutral", "happy", "angry", "hurt")):
        draw_text(img, x0, 4 + 4 * 52 + 2 + j * 10, "", col=GREY)
    y0 = len(rows) * shh + 18
    kinds = [("rat", "vermin"), ("serpent", "jungle"), ("tribal", "jungle"), ("wisp", "flame"),
             ("automaton", "clockwork"), ("hound", "clockwork"), ("wraith", "spirit"), ("golem", "ruins"),
             ("horror", "abyssal")]
    for i, (k, fam) in enumerate(kinds):
        for fr in range(3):
            paste(img, mon(k, fam, fr), 4 + i * 34, y0 + fr * 34)
    paste(img, MON.warden(0), 4 + 9 * 34 + 8, y0 + 8)
    return img


def gif_fight():
    """Kael walks up to a tribal skirmisher and cuts it down."""
    oy = -4
    frames = []
    kx, ky = 9, 7
    others = [(hero("mira", Pose("right")), 8, 8, "hero"), (hero("rook", Pose("right")), 8, 6, "hero")]
    foe = (12, 7)

    def snap(kpose, kdx, foe_frame=0, foe_flash=None, fx=(), foe_alive=True, foe_dx=0, k_flash=None, msg=None):
        acts = list(others)
        acts.append((hero("kael", kpose), kx, ky, "hero", kdx, 0, k_flash))
        if foe_alive:
            acts.append((mon("tribal", "jungle", foe_frame), foe[0], foe[1], "mon", foe_dx, 0, foe_flash))
        acts.append((mon("serpent", "jungle", len(frames) % 2), 15, 9, "mon"))
        img = dungeon_frame(oy, acts, (kx * 16 + 8 + kdx, ky * 16), effects=fx)
        frames.append(hud(img, msg=msg or ("You close on the Tribal Skirmisher.", "")))

    for step in range(8):                              # two tiles: 4 frames a tile
        snap(Pose("right", walk=step % 4), (step + 1) * 4, foe_frame=(step // 2) % 2)
    kdx = 32
    hit1 = ("Kael strikes the Tribal Skirmisher for 31!", "")
    snap(Pose("right", attack=0), kdx, msg=hit1)
    snap(Pose("right", attack=1), kdx, foe_flash=(255, 255, 255), msg=hit1,
         fx=[lambda im: damage_number(im, foe[0] * 16 + 14, foe[1] * 16 + oy - 26, 31)])
    for k in range(3):
        snap(Pose("right", attack=2 if k == 0 else -1), kdx, foe_flash=(255, 96, 96) if k == 0 else None, msg=hit1,
             fx=[lambda im, k=k: damage_number(im, foe[0] * 16 + 14, foe[1] * 16 + oy - 30 - k * 3, 31)])
    back = ("Kael strikes the Tribal Skirmisher for 31!", "The Tribal Skirmisher hits you for 9.")
    snap(Pose("right"), kdx, foe_frame=2, foe_dx=-5, k_flash=(255, 80, 80), msg=back,
         fx=[lambda im: damage_number(im, kx * 16 + kdx + 14, ky * 16 + oy - 32, 9, col=(255, 200, 200))])
    snap(Pose("right"), kdx, msg=back)
    snap(Pose("right", attack=0), kdx, msg=back)
    kill = ("A vicious opening! You strike for 28.", "The Tribal Skirmisher falls. (+11 gold)")
    snap(Pose("right", attack=1), kdx, foe_flash=(255, 255, 255), msg=kill,
         fx=[lambda im: damage_number(im, foe[0] * 16 + 14, foe[1] * 16 + oy - 26, 28, col=GOLDC)])
    for k in range(4):
        snap(Pose("right", attack=2 if k == 0 else -1), kdx, foe_flash=(255, 255, 255),
             foe_alive=(k % 2 == 0), msg=kill)
    for k in range(4):
        snap(Pose("down" if k > 1 else "right"), kdx, foe_alive=False, msg=kill)
    return frames


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in (("1-title", screen_title), ("2-character", screen_create),
                     ("3-dungeon", screen_dungeon), ("4-town", screen_town), ("5-shop", screen_shop)):
        img = fn().convert("RGB")
        path = os.path.join(OUT, name + ".png")
        img.resize((W * SCALE, H * SCALE), Image.NEAREST).save(path)
        print("wrote", os.path.relpath(path))
    sheet = sheet_animation().convert("RGB")
    path = os.path.join(OUT, "6-animation-sheet.png")
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(path)
    print("wrote", os.path.relpath(path))
    frames = [f.convert("RGB").resize((W * 2, H * 2), Image.NEAREST) for f in gif_fight()]
    path = os.path.join(OUT, "7-walk-and-attack.gif")
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=120, loop=0)
    print("wrote", os.path.relpath(path))


if __name__ == "__main__":
    main()
