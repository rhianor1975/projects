#!/usr/bin/env python3
"""Art-direction mockups for the Godot port.

Renders screens at the game's native 640x360 -- 32px tiles, 32x48 heroes --
and writes them scaled 2x to ../mockups/, plus an animation sheet and an
animated GIF of the party walking up to a monster and fighting it. Built from
the same modelled sprites and tiles the game's assets come from: pictures of
the intended look, not screenshots of running code.

    python3 tools/mockups.py      (from AetherDescent/godot)
"""
import math
import os
import random
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import monsters as MON  # noqa: E402
import tiles as TL  # noqa: E402
from characters import FEET_Y, Pose, Spec, field_sprite, portrait  # noqa: E402
from pixelart import draw_text, draw_window, paste, shadow, text_width  # noqa: E402

W, H = 640, 360
TS = TL.T                      # 32
SCALE = 2
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
    draw_window(img, x - 5, y - 5, 74, 74, top=(40, 40, 104), bottom=(16, 16, 56))
    paste(img, face(name, expr), x, y)


def bar(img, x, y, w, frac, col, back=(24, 24, 56), h=5):
    px = img.load()
    fill = int(w * frac)
    for xx in range(-1, w + 1):
        for yy in range(-1, h + 1):
            if xx in (-1, w) or yy in (-1, h):
                px[x + xx, y + yy] = (8, 8, 24, 255)
            elif xx < fill:
                c = col
                if yy == 0:
                    c = tuple(min(255, v + 80) for v in col)
                elif yy >= h - 2:
                    c = tuple(int(v * 0.7) for v in col)
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
            X, Y = ox + tx * TS, oy + ty * TS
            if X >= W or Y >= H or X + TS <= 0 or Y + TS <= 0:
                continue
            if ch in "#t":
                if below not in "#t":
                    paste(img, t(("face", seed % 3), lambda: TL.wall_face(biome, seed % 3, 0.35 if seed % 2 else 0.0)), X, Y)
                    if ch == 't':
                        paste(img, t("torch", TL.torch), X, Y - 4)
                        torches.append((X + 16, Y + 8))
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
    """Walls cast a short shadow onto the floor below, and shores foam -- the
    two cheapest depth cues a top-down map has."""
    px = img.load()
    for ty, row in enumerate(rows):
        for tx, ch in enumerate(row):
            above = rows[ty - 1][tx] if ty > 0 else '#'
            X, Y = ox + tx * TS, oy + ty * TS
            if ch not in "#t~" and above in "#t":
                for yy, k in ((0, 0.50), (1, 0.58), (2, 0.66), (3, 0.76), (4, 0.86), (5, 0.94)):
                    for xx in range(TS):
                        if 0 <= X + xx < img.width and 0 <= Y + yy < img.height:
                            r, g, b, a = px[X + xx, Y + yy]
                            px[X + xx, Y + yy] = (int(r * k), int(g * k), min(255, int(b * k + 6)), a)
            if ch == '~' and above != '~':
                for xx in range(TS):
                    for yy, c in ((0, (224, 244, 255)), (1, (176, 216, 248)), (2, (120, 176, 232))):
                        if 0 <= X + xx < img.width and 0 <= Y + yy < img.height and ((xx + tx + yy) % 4 or yy == 0):
                            px[X + xx, Y + yy] = c + (255,)


def light(img, lights, ambient=0.50):
    """SNES colour-math style lighting: banded pools of warm light over a dim map."""
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            v = ambient
            warm = 0.0
            for (lx, ly, rad, strength) in lights:
                dx, dy = x - lx, (y - ly) * 1.15
                if abs(dx) < rad and abs(dy) < rad:
                    d = math.hypot(dx, dy)
                    if d < rad:
                        f = (1 - d / rad) * strength
                        v += f
                        warm += f
            v = min(1.12, v)
            v = round(v * 6) / 6
            warm = min(0.4, warm)
            r, g, b, a = px[x, y]
            px[x, y] = (min(255, int(r * v + 26 * warm)), min(255, int(g * v + 12 * warm)),
                        min(255, int(b * v * (1 - warm * 0.4))), a)


def fog(img, cells, ox=0, oy=0, alpha=120):
    over = Image.new("RGBA", img.size, (0, 0, 0, 0))
    opx = over.load()
    for (tx, ty) in cells:
        for y in range(TS):
            for x in range(TS):
                X, Y = ox + tx * TS + x, oy + ty * TS + y
                if 0 <= X < img.width and 0 <= Y < img.height:
                    opx[X, Y] = (6, 6, 28, alpha)
    img.alpha_composite(over)


def place(img, spr, tx, ty, ox=0, oy=0, kind="hero", dx=0, dy=0, flash=None):
    """Feet near the bottom of the tile, centred on it."""
    feet = oy + ty * TS + TS - 3
    if kind == "hero":
        top = feet - FEET_Y
        sw = 22
    else:
        top = feet - (spr.height - 3)
        sw = 26
    left = ox + tx * TS + TS // 2 - spr.width // 2
    paste(img, shadow(sw, 7), ox + tx * TS + TS // 2 - sw // 2 + dx, feet - 4 + dy)
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
    draw_text(img, x - text_width(s) * 2 // 2, y, s, col=col, outline=INK, scale=2)


# ---- screens ----------------------------------------------------------------------
def screen_title(ui=True):
    img = Image.new("RGBA", (W, H))
    px = img.load()
    top, mid, bot = (20, 14, 56), (132, 52, 112), (248, 156, 80)
    for y in range(H):
        t = y / H
        k = t / 0.6 if t < 0.6 else (t - 0.6) / 0.4
        a, b = (top, mid) if t < 0.6 else (mid, bot)
        c = tuple((int(a[i] + (b[i] - a[i]) * k) // 6) * 6 for i in range(3))
        for x in range(W):
            px[x, y] = c + (255,)
    r = random.Random(3)
    for _ in range(160):
        x, y = r.randrange(W), r.randrange(int(H * 0.5))
        px[x, y] = (255, 255, 230, 255) if r.random() < 0.3 else (200, 200, 255, 255)
    ground = H - 70
    tiers = ((380, 34), (300, 30), (222, 28), (146, 26), (74, 24))
    for i, (w, hh) in enumerate(tiers):
        x0 = W // 2 - w // 2
        y1 = ground - sum(t[1] for t in tiers[:i])
        y0 = y1 - hh
        for y in range(y0, y1):
            for x in range(x0, x0 + w):
                base = (120, 80, 120) if y <= y0 + 1 else (40, 26, 60)
                if x < x0 + 4:
                    base = (60, 40, 84)
                px[x, y] = base + (255,)
        for x in range(x0 + 10, x0 + w - 10, 18):
            for y in range(y0 + 10, y0 + 18):
                if r.random() < 0.7:
                    for dx in range(3):
                        px[x + dx, y] = (255, 196, 96, 255) if y > y0 + 12 else (255, 240, 180, 255)
    top_y = ground - sum(t[1] for t in tiers)
    for y in range(0, top_y):                         # aether pouring up out of the well
        for x in range(W // 2 - 14, W // 2 + 14):
            k = 1 - abs(x - W // 2) / 14
            if r.random() < k * 0.6:
                rr, gg, bb, aa = px[x, y]
                px[x, y] = (min(255, rr + 60), min(255, gg + 90), min(255, bb + 110), 255)
    for cx, rad in ((60, 56), (150, 34), (500, 50), (590, 36)):     # the city's green domes
        for y in range(ground - rad, ground):
            for x in range(cx - rad, cx + rad):
                if (x - cx) ** 2 + (y - ground) ** 2 <= rad * rad:
                    px[x, y] = ((40, 88, 80) if (x - cx) < -rad * 0.3 else (24, 56, 56)) + (255,)
    for y in range(ground, H):
        for x in range(W):
            px[x, y] = (14, 36, 28, 255) if (x * 7 + y * 3) % 11 else (22, 60, 40, 255)
    if not ui:
        return img
    gold = [(255, 250, 210), (255, 232, 140), (250, 196, 72), (224, 148, 40), (184, 104, 32), (152, 72, 24), (112, 48, 24)]
    title = "AETHER DESCENT"
    draw_text(img, (W - text_width(title) * 5) // 2, 44, title, col=gold, outline=(32, 16, 40), scale=5)
    sub = "One hundred floors down."
    draw_text(img, (W - text_width(sub) * 2) // 2, 98, sub, col=(232, 216, 248), scale=2)
    draw_window(img, W - 150, 196, 130, 62)
    for i, opt in enumerate(("New Game", "Continue", "Settings")):
        draw_text(img, W - 122, 206 + i * 16, opt, col=WHITE if i == 0 else GREY)
    draw_text(img, W - 138, 206, "▶", col=GOLDC)
    draw_text(img, W - text_width("v0.1  Godot 4") - 8, H - 12, "v0.1  Godot 4", col=(120, 160, 128), shadow_col=None)
    for i, n in enumerate(("kael", "mira", "rook")):
        paste(img, shadow(22, 7), 40 + i * 40, ground + 14)
        paste(img, hero(n, Pose("right", walk=i % 4)), 27 + i * 40, ground + 18 - FEET_Y)
    return img


def screen_create():
    img = Image.new("RGBA", (W, H), (8, 8, 24, 255))
    draw_window(img, 6, 6, W - 12, 28)
    draw_text(img, 18, 16, "New Character  -  Choose a class", col=WHITE)
    draw_text(img, W - 118, 16, "Arcanist  4/7", col=CYAN)
    draw_window(img, 6, 38, 184, 276)
    classes = ["Arcanist-Engineer", "Aether-Sailor", "Lamp-Witch", "Static Hexer",
               "Glyph-Scribe", "Rune-Smith", "Void Cantor", "Ember Cleric",
               "Clock-Oracle", "Storm Caller", "Brass Theurge", "Mire Augur", "Cinder Saint"]
    for i, c in enumerate(classes):
        draw_text(img, 26, 52 + i * 19, c, col=WHITE if i == 1 else GREY)
    draw_text(img, 14, 71, "▶", col=GOLDC)
    draw_window(img, 194, 38, W - 200, 276)
    framed_portrait(img, 212, 56, "vesper")
    draw_text(img, 296, 56, "Aether-Sailor", col=GOLDC, scale=2)
    for i, line in enumerate(("Void-current navigator. Eerie, unshaken;", "bends gravity with a scope.")):
        draw_text(img, 296, 80 + i * 13, line, col=WHITE)
    stats = [("HP", "42"), ("ATK", "8"), ("DEF", "5"), ("AE", "14"),
             ("Evasion", "8%"), ("Crit", "0%"), ("Ward", "2%"), ("Vision", "6")]
    for i, (k, v) in enumerate(stats):
        x = 212 + (i % 2) * 120
        y = 146 + (i // 2) * 16
        draw_text(img, x, y, k, col=CYAN)
        draw_text(img, x + 96 - text_width(v), y, v, col=WHITE)
    for i, (k, v) in enumerate((("School", "Aether-Sense"), ("Ability", "Steady Footing"),
                                ("Leads", "AETHER-SENSE 9   RESOLVE 8"))):
        draw_text(img, 212, 222 + i * 16, k, col=CYAN)
        draw_text(img, 280, 222 + i * 16, v, col=GREEN if k == "Leads" else WHITE)
    for i, f in enumerate(("down", "left", "up", "right")):     # turning to show itself off
        x, y = 470 + (i % 2) * 64, 128 + (i // 2) * 72
        paste(img, shadow(22, 7), x + 13, y + FEET_Y - 4)
        paste(img, hero("vesper", Pose(f, walk=1 if i % 2 else 0)), x, y)
    draw_window(img, 6, 318, W - 12, 36)
    draw_text(img, 18, 332, "Z: choose     X: back     Left/Right: archetype", col=GREY)
    return img


DUNGEON = [
    "####################",
    "####t######t########",
    "##..,,;;..####~~~~##",
    "##.,;**;...###~~~~~#",
    "#.....;;TT......~~~#",
    "#..,.....TT........#",
    "#.....>.......,..;;#",
    "#####..........*.;;#",
    "#####...,.......;;;#",
    "#~~~##.........TT;;#",
    "#~~~~#..,......TT;;#",
    "##########t#########",
]


def dungeon_frame(actors, party_xy, effects=(), oy=-4):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    torches = draw_map(img, DUNGEON, "jungle", oy=oy)
    for a in sorted(actors, key=lambda a: a[2]):
        spr, tx, ty, kind = a[:4]
        dx = a[4] if len(a) > 4 else 0
        dy = a[5] if len(a) > 5 else 0
        flash = a[6] if len(a) > 6 else None
        place(img, spr, tx, ty, oy=oy, kind=kind, dx=dx, dy=dy, flash=flash)
    lights = [(x, y, 110, 0.45) for (x, y) in torches]
    lights.append((party_xy[0], party_xy[1] + oy, 170, 0.55))
    light(img, lights, ambient=0.42)
    fog(img, [(x, y) for x in range(0, 4) for y in range(0, 12)] + [(x, y) for x in range(13, 20) for y in range(0, 4)],
        oy=oy, alpha=120)
    for fx in effects:
        fx(img)
    return img


def hud(img, hp=(62, 86), msg=("", "")):
    draw_window(img, 4, 4, 236, 64)
    paste(img, face("kael").crop((12, 10, 52, 50)), 12, 14)
    draw_text(img, 60, 13, "Kael", col=WHITE)
    draw_text(img, 100, 13, "Lv 7", col=GREY)
    draw_text(img, 140, 13, "Gear-Knight", col=(144, 152, 200), shadow_col=None)
    draw_text(img, 60, 30, "HP", col=CYAN)
    bar(img, 78, 31, 100, hp[0] / hp[1], (88, 216, 96))
    draw_text(img, 186, 30, "%d/%d" % hp, col=WHITE)
    draw_text(img, 60, 46, "AE", col=CYAN)
    bar(img, 78, 47, 100, 0.45, (96, 160, 248))
    draw_text(img, 186, 46, "9/20", col=WHITE)
    for i, n in enumerate(("mira", "rook")):            # the rest of the party, compact
        x = 244 + i * 92
        draw_window(img, x, 4, 88, 40)
        paste(img, face(n).crop((14, 12, 50, 48)).resize((24, 24), Image.NEAREST), x + 8, 11)
        draw_text(img, x + 38, 10, ("Mira", "Rook")[i], col=WHITE, shadow_col=None)
        bar(img, x + 38, 22, 42, [0.9, 0.55][i], (88, 216, 96), h=4)
        bar(img, x + 38, 31, 42, [0.7, 0.3][i], (96, 160, 248), h=4)
    draw_window(img, W - 158, 4, 154, 40)
    draw_text(img, W - 148, 12, "B12", col=GOLDC)
    draw_text(img, W - 120, 12, "Sunken Jungle Roots", col=WHITE)
    draw_text(img, W - 148, 27, "G", col=GOLDC)
    draw_text(img, W - 136, 27, "1,284", col=WHITE)
    draw_text(img, W - 70, 27, "Keys 1", col=GREY)
    draw_window(img, W - 92, 48, 88, 62, top=(24, 24, 64), bottom=(8, 8, 32))
    mp = img.load()
    for y in range(12):
        for x in range(20):
            ch = DUNGEON[y][x]
            if ch in "#t":
                continue
            c = (64, 112, 200) if ch == '~' else (88, 140, 80)
            if ch == '>':
                c = (255, 224, 96)
            for dy in range(3):
                for dx in range(3):
                    mp[W - 82 + x * 3 + dx, 58 + y * 3 + dy] = c + (255,)
    for dy in range(3):
        for dx in range(3):
            mp[W - 82 + 9 * 3 + dx, 58 + 7 * 3 + dy] = (255, 255, 255, 255)
    draw_window(img, 4, H - 50, W - 8, 46)
    draw_text(img, 16, H - 41, msg[0], col=WHITE)
    draw_text(img, 16, H - 26, msg[1], col=GREY)
    draw_text(img, W - 22, H - 18, "▼", col=GOLDC, shadow_col=None)
    return img


def screen_dungeon():
    oy = -4
    actors = [
        (hero("kael", Pose("right", attack=1)), 9, 7, "hero"),
        (hero("mira", Pose("right", cast=True)), 7, 8, "hero"),
        (hero("rook", Pose("up", walk=1)), 9, 9, "hero"),
        (mon("tribal", "jungle", 2), 10, 7, "mon"),
        (mon("serpent", "jungle", 1), 13, 9, "mon"),
        (mon("rat", "vermin", 2), 12, 5, "mon"),
    ]

    def bolt(img):
        px = img.load()
        x0, y0, x1, y1 = 7 * TS + 26, 8 * TS + oy - 30, 12 * TS + 12, 5 * TS + oy + 4
        for i in range(80):
            t = i / 79
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t - math.sin(t * math.pi) * 18
            if i % 2 == 0:
                for dx in range(3):
                    for dy in range(3):
                        px[int(x) + dx, int(y) + dy] = (255, 255, 200, 255) if t > 0.6 else (180, 140, 255, 255)
        for k in range(8):
            a = k * math.pi / 4
            for rr in range(3, 12):
                px[int(x1 + math.cos(a) * rr), int(y1 + math.sin(a) * rr)] = \
                    (255, 255, 255, 255) if rr < 7 else (200, 170, 255, 255)
        damage_number(img, 10 * TS + 16, 7 * TS + oy - 60, 31)
        draw_text(img, 12 * TS - 4, 5 * TS + oy - 30, "Miss", col=CYAN, outline=INK, scale=2)

    img = dungeon_frame(actors, (9 * TS + 16, 7 * TS), effects=[bolt], oy=oy)
    return hud(img, msg=("Kael strikes the Tribal Skirmisher for 31!",
                         "Mira casts Brass Arc -- the Vine Rat slips aside."))


TOWN = [
    "RRRRRR.RRRRRR.QQQQQQ",
    "WWDWWW.WWWDWW.WWDWWW",
    "....................",
    ";*................*;",
    ".........F..........",
    "....................",
    "..QQQQ........QQQQ..",
    "..WDWW....>...WWDW..",
    "....................",
    ";;..................",
    "*;..................",
    "....................",
]


def screen_town():
    img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    roof_a = TL.roof()
    roof_b = TL.roof(colour=(72, 156, 140))
    wall = TL.house_wall()
    signs = [(248, 208, 72), (200, 64, 64), (96, 160, 248), (136, 216, 120), (200, 120, 232)]
    cob = [TL.floor_town(i) for i in range(4)]
    oy = -8
    for ty, row in enumerate(TOWN):
        for tx, ch in enumerate(row):
            X, Y = tx * TS, ty * TS + oy
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
    contact_shadows(img, [''.join('#' if ch in "WDRQ" else '.' for ch in r) for r in TOWN], 0, oy)
    actors = [(hero("kael", Pose("down", walk=1)), 9, 6), (hero("mira", Pose("down", walk=3)), 8, 6),
              (hero("rook", Pose("down")), 10, 6), (hero("ivy", Pose("right", walk=2)), 3, 3),
              (hero("sable", Pose("left")), 16, 9)]
    for spr, tx, ty in sorted(actors, key=lambda a: a[2]):
        place(img, spr, tx, ty, oy=oy)
    for label, tx, ty in (("ARMORY", 2, 1), ("ARCANIST", 10, 1), ("BANK", 16, 1), ("INN", 3, 7), ("TAVERN", 16, 7)):
        w = text_width(label) + 14
        x = max(4, min(W - w - 4, tx * TS + TS // 2 - w // 2))
        y = ty * TS + oy - 18
        draw_window(img, x, y, w, 17)
        draw_text(img, x + 7, y + 5, label, col=WHITE)
    draw_window(img, 4, H - 34, W - 8, 30)
    draw_text(img, 16, H - 23, "The plaza. Brass domes gone green, and somebody still takes good coin.", col=WHITE)
    return img


def screen_shop():
    img = screen_town()
    img.alpha_composite(Image.new("RGBA", (W, H), (8, 8, 32, 150)))
    draw_window(img, 6, 6, 160, 28)
    draw_text(img, 18, 13, "Armory", col=GOLDC, scale=2)
    draw_window(img, W - 140, 6, 134, 28)
    draw_text(img, W - 128, 16, "G", col=GOLDC)
    draw_text(img, W - 114, 16, "1,284", col=WHITE)
    draw_window(img, 6, 38, 340, 210)
    items = [("Rusty Cutlass", 15, "+2"), ("Brass Rapier", 55, "+5"), ("Riveted Cleaver", 110, "+8"),
             ("Steam-Forged Sabre", 200, "+12"), ("Clockwork Estoc", 350, "+16"),
             ("Star-iron Edge", 600, "+21"), ("Ether-Etched Blade", 1000, "+27"),
             ("Weapon +3", 300, "+5"), ("Armour +2", 260, "+5"), ("HP +4", 320, "+10")]
    for i, (n, p, b) in enumerate(items):
        y = 52 + i * 19
        col = GOLDC if i == 3 else WHITE
        draw_text(img, 30, y, n, col=col)
        draw_text(img, 240 - text_width(b), y, b, col=GREEN)
        draw_text(img, 330 - text_width(str(p)), y, str(p), col=col)
    draw_text(img, 16, 52 + 3 * 19, "▶", col=GOLDC)
    draw_window(img, 350, 38, W - 356, 210)
    framed_portrait(img, 366, 54, "kael", "happy")
    draw_text(img, 448, 56, "Kael", col=WHITE, scale=2)
    draw_text(img, 448, 78, "Gear-Knight", col=GREY)
    for i, (k, a, b, better) in enumerate((("ATK", "14", "21", True), ("DEF", "11", "11", False))):
        y = 146 + i * 18
        draw_text(img, 366, y, k, col=CYAN)
        draw_text(img, 404, y, a, col=WHITE)
        draw_text(img, 426, y, "▶", col=GOLDC)
        draw_text(img, 440, y, b, col=GREEN if better else WHITE)
    draw_text(img, 366, 192, "Now: Riveted Cleaver", col=GREY)
    paste(img, shadow(22, 7), 542, 96 + FEET_Y - 4)
    paste(img, hero("kael", Pose("down", attack=0)), 529, 96)
    draw_window(img, 6, 252, W - 12, 102)
    draw_text(img, 18, 264, "Steam-Forged Sabre", col=GOLDC, scale=2)
    draw_text(img, 18, 290, "A pressure-tempered blade. Hisses when it bites.", col=WHITE)
    draw_text(img, 18, 306, "Attack +12.  Upgrades you buy here carry over to whatever you wield.", col=WHITE)
    draw_text(img, 18, 330, "Z: buy     X: leave     A/S: switch tab", col=GREY)
    return img


def sheet_animation():
    """Every frame of two heroes, the portraits' expressions, and the monsters."""
    names = ("kael", "mira")
    sw, shh = 52, 64
    rows = []
    for n in names:
        for f in ("down", "left", "right", "up"):
            frames = [Pose(f, w) for w in range(4)] + [Pose(f, attack=a) for a in range(3)] + [Pose(f, cast=True)]
            rows.append((n, frames))
    x_faces = 8 * sw + 16
    img = Image.new("RGBA", (x_faces + 4 * 68 + 8, len(rows) * shh + 190), (28, 32, 56, 255))
    for r, (n, frames) in enumerate(rows):
        for i, p in enumerate(frames):
            paste(img, shadow(22, 7), 6 + i * sw + 13, 4 + r * shh + FEET_Y - 4)
            paste(img, hero(n, p), 6 + i * sw, 4 + r * shh)
    for i, t in enumerate(("walk 1", "walk 2", "walk 3", "walk 4", "wind-up", "strike", "follow", "cast")):
        draw_text(img, 8 + i * sw, len(rows) * shh + 6, t, col=GREY, shadow_col=None)
    for i, n in enumerate(("kael", "mira", "rook", "vesper")):
        for j, e in enumerate(("neutral", "happy", "angry", "hurt")):
            paste(img, face(n, e), x_faces + i * 68, 6 + j * 68)
    for j, e in enumerate(("neutral", "happy", "angry", "hurt")):
        draw_text(img, x_faces + 4 * 68 - 48, 6 + j * 68 + 66 - 10, "", col=GREY)
    y0 = len(rows) * shh + 26
    kinds = [("rat", "vermin"), ("serpent", "jungle"), ("tribal", "jungle"), ("wisp", "flame"),
             ("automaton", "clockwork"), ("hound", "clockwork"), ("wraith", "spirit"), ("golem", "ruins"),
             ("horror", "abyssal")]
    for i, (k, fam) in enumerate(kinds):
        for fr in range(3):
            paste(img, mon(k, fam, fr), 6 + i * 52, y0 + fr * 52 if fr < 2 else y0 + 104)
    for fr in range(2):
        paste(img, MON.warden(fr), 6 + 9 * 52 + 10 + fr * 76, y0 + 30)
    return img


def gif_fight():
    """Kael walks up to a tribal skirmisher and cuts it down."""
    oy = -4
    frames = []
    kx, ky = 6, 7
    others = [(hero("mira", Pose("right")), 5, 8, "hero"), (hero("rook", Pose("right")), 5, 6, "hero")]
    foe = (9, 7)

    def snap(kpose, kdx, foe_frame=0, foe_flash=None, fx=(), foe_alive=True, foe_dx=0, k_flash=None, msg=None):
        acts = list(others)
        acts.append((hero("kael", kpose), kx, ky, "hero", kdx, 0, k_flash))
        if foe_alive:
            acts.append((mon("tribal", "jungle", foe_frame), foe[0], foe[1], "mon", foe_dx, 0, foe_flash))
        acts.append((mon("serpent", "jungle", len(frames) % 2), 13, 9, "mon"))
        img = dungeon_frame(acts, (kx * TS + 16 + kdx, ky * TS), effects=fx, oy=oy)
        frames.append(hud(img, msg=msg or ("You close on the Tribal Skirmisher.", "")))

    for step in range(8):                               # two tiles: 4 frames a tile, 8px a frame
        snap(Pose("right", walk=step % 4), (step + 1) * 8, foe_frame=(step // 2) % 2)
    kdx = 64
    hit1 = ("Kael strikes the Tribal Skirmisher for 31!", "")
    nx, ny = foe[0] * TS + 16, foe[1] * TS + oy - 64
    snap(Pose("right", attack=0), kdx, msg=hit1)
    snap(Pose("right", attack=1), kdx, foe_flash=(255, 255, 255), msg=hit1,
         fx=[lambda im: damage_number(im, nx, ny, 31)])
    for k in range(3):
        snap(Pose("right", attack=2 if k == 0 else -1), kdx, foe_flash=(255, 96, 96) if k == 0 else None, msg=hit1,
             fx=[lambda im, k=k: damage_number(im, nx, ny - 4 - k * 5, 31)])
    back = ("Kael strikes the Tribal Skirmisher for 31!", "The Tribal Skirmisher hits you for 9.")
    snap(Pose("right"), kdx, foe_frame=2, foe_dx=-10, k_flash=(255, 80, 80), msg=back,
         fx=[lambda im: damage_number(im, kx * TS + kdx + 16, ky * TS + oy - 70, 9, col=(255, 200, 200))])
    snap(Pose("right"), kdx, msg=back)
    snap(Pose("right", attack=0), kdx, msg=back)
    kill = ("A vicious opening! You strike for 28.", "The Tribal Skirmisher falls. (+11 gold)")
    snap(Pose("right", attack=1), kdx, foe_flash=(255, 255, 255), msg=kill,
         fx=[lambda im: damage_number(im, nx, ny, 28, col=GOLDC)])
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
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(path)
    print("wrote", os.path.relpath(path))
    frames = [f.convert("RGB") for f in gif_fight()]
    path = os.path.join(OUT, "7-walk-and-attack.gif")
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=110, loop=0)
    print("wrote", os.path.relpath(path))


if __name__ == "__main__":
    main()
