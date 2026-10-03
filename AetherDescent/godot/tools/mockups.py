#!/usr/bin/env python3
"""Art-direction mockups for the Godot port.

Renders five screens at the game's native 384x216 and writes them scaled 4x
to ../mockups/. These are pictures of the intended look, built from the same
sprite and tile code the game's assets will come from -- not screenshots of
running code.

    python3 tools/mockups.py      (from AetherDescent/godot)
"""
import os
import random
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from font5x7 import GLYPHS  # noqa: E402
from pixelart import (draw_window, grid_image, mirror, palette, paste,  # noqa: E402
                      shadow)
import sprites as S  # noqa: E402
import tiles as TL  # noqa: E402

W, H = 384, 216
SCALE = 4
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mockups")

WHITE = (248, 248, 248)
GREY = (168, 176, 200)
GOLDC = (248, 216, 96)
SHADOW = (16, 16, 48)
CYAN = (128, 224, 248)
RED = (248, 112, 112)
GREEN = (136, 232, 120)


# ---- text ------------------------------------------------------------------
def text_width(s):
    w = 0
    for ch in s:
        g = GLYPHS.get(ch, GLYPHS['?'])
        w += len(g[0]) + 1
    return w - 1 if s else 0


def draw_text(img, x, y, s, col=WHITE, shadow_col=SHADOW, scale=1, outline=None):
    px = img.load()

    def dot(xx, yy, c):
        for sy in range(scale):
            for sx in range(scale):
                X, Y = xx * scale + sx + x, yy * scale + sy + y
                if 0 <= X < img.width and 0 <= Y < img.height:
                    px[X, Y] = c + (255,)

    cx = 0
    for ch in s:
        g = GLYPHS.get(ch, GLYPHS['?'])
        lit = [(gx, gy) for gy, row in enumerate(g) for gx, c in enumerate(row) if c == '#']
        if outline:
            for gx, gy in lit:
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        dot(cx + gx + dx, gy + dy, outline)
        elif shadow_col:
            for gx, gy in lit:
                dot(cx + gx + 1, gy + 1, shadow_col)
        for gx, gy in lit:
            c = col
            if isinstance(col, list):            # vertical gradient
                c = col[min(len(col) - 1, gy * len(col) // 7)]
            dot(cx + gx, gy, c)
        cx += len(g[0]) + 1
    return cx * scale


def bar(img, x, y, w, frac, col, back=(24, 24, 56)):
    px = img.load()
    for xx in range(w):
        for yy in range(3):
            c = col if xx < int(w * frac) else back
            if yy == 0 and xx < int(w * frac):
                c = tuple(min(255, v + 60) for v in col)
            px[x + xx, y + yy] = c + (255,)
    for xx in range(-1, w + 1):
        px[x + xx, y - 1] = (8, 8, 24, 255)
        px[x + xx, y + 3] = (8, 8, 24, 255)
    px[x - 1, y] = px[x - 1, y + 1] = px[x - 1, y + 2] = (8, 8, 24, 255)
    px[x + w, y] = px[x + w, y + 1] = px[x + w, y + 2] = (8, 8, 24, 255)


# ---- sprites ---------------------------------------------------------------
HEROES = {
    "kael":  ("spiky", "blonde", "steel", "crimson"),
    "mira":  ("long", "pink", "violet", "brass"),
    "rook":  ("tail", "red", "forest", "brass"),
    "sable": ("spiky", "navy", "brass", "teal"),
    "ivy":   ("long", "green", "white", "teal"),
    "vesper": ("long", "silver", "crimson", "violet"),
}


def hero(name):
    style, hair, cloth, acc = HEROES[name]
    return grid_image(S.hero_rows(style), palette(hair, cloth, acc))


def portrait(name):
    style, hair, cloth, acc = HEROES[name]
    rows = S.PORTRAIT_LONG if style == "long" else S.PORTRAIT_SPIKY
    return grid_image(mirror(rows), palette(hair, cloth, acc))


def monster(kind, fam):
    return grid_image(S.MONSTERS[kind], S.monster_palette(fam))


def framed_portrait(img, x, y, name):
    draw_window(img, x - 4, y - 4, 40, 40, top=(40, 40, 96), bottom=(16, 16, 48))
    paste(img, portrait(name), x, y)


# ---- map drawing -----------------------------------------------------------
def draw_map(img, rows, biome, ox=0, oy=0):
    """Rows of chars -> tiles. Walls pick face or top from what is below."""
    rnd = random.Random(7)
    cache = {}

    def tile(key, fn):
        if key not in cache:
            cache[key] = fn()
        return cache[key]

    for ty, row in enumerate(rows):
        for tx, ch in enumerate(row):
            seed = (tx * 31 + ty * 17) % 5
            below = rows[ty + 1][tx] if ty + 1 < len(rows) else '#'
            if ch == '#':
                if below != '#':
                    t = tile(("face", seed % 2, seed == 3), lambda: TL.wall_face(biome, seed % 2, seed == 3))
                else:
                    t = tile(("top", seed), lambda: TL.wall_top(biome, seed))
            elif ch == '.':
                t = tile(("floor", seed), lambda: TL.floor(biome, seed))
            elif ch == ',':
                t = tile(("grow", seed), lambda: TL.floor(biome, seed, True))
            elif ch == 'T':
                t = tile(("thicket", seed), lambda: TL.thicket(biome, seed))
            elif ch == '*':
                t = tile(("flowers", seed), lambda: TL.flowers(biome, seed))
            elif ch == '~':
                t = tile(("water", (tx + ty) % 2), lambda: TL.water((tx + ty) % 2))
            elif ch == '>':
                t = tile("down", lambda: TL.stairs_down(biome))
            elif ch == '<':
                t = tile("up", lambda: TL.stairs_up(biome))
            elif ch == 'L':
                t = tile(("lava", (tx + ty) % 2), lambda: TL.lava((tx + ty) % 2))
            else:
                continue
            paste(img, t, ox + tx * 16, oy + ty * 16)
    del rnd


def fog(img, cells, ox=0, oy=0, alpha=150):
    """Darken remembered-but-unseen cells, the way the game will."""
    over = Image.new("RGBA", img.size, (0, 0, 0, 0))
    opx = over.load()
    for (tx, ty) in cells:
        for y in range(16):
            for x in range(16):
                X, Y = ox + tx * 16 + x, oy + ty * 16 + y
                if 0 <= X < W and 0 <= Y < H:
                    opx[X, Y] = (8, 8, 32, alpha)
    img.alpha_composite(over)


def actor(img, spr, tx, ty, ox=0, oy=0):
    """Feet on the tile; a 16x24 hero rises 8px above it."""
    paste(img, shadow(12, 4), ox + tx * 16 + 2, oy + ty * 16 + 12)
    paste(img, spr, ox + tx * 16, oy + ty * 16 + 16 - spr.height)


# ---- screens ---------------------------------------------------------------
def screen_title():
    img = Image.new("RGBA", (W, H))
    px = img.load()
    top, mid, bot = (24, 16, 64), (152, 64, 112), (248, 160, 88)
    for y in range(H):
        t = y / H
        if t < 0.6:
            k = t / 0.6
            c = tuple(int(top[i] + (mid[i] - top[i]) * k) for i in range(3))
        else:
            k = (t - 0.6) / 0.4
            c = tuple(int(mid[i] + (bot[i] - mid[i]) * k) for i in range(3))
        for x in range(W):
            px[x, y] = c + (255,)
    r = random.Random(3)
    for _ in range(70):                               # stars
        x, y = r.randrange(W), r.randrange(int(H * 0.45))
        px[x, y] = (255, 255, 230, 255)
    # the sunken temple: stepped ziggurat cut into the ground
    sil = (20, 12, 36)
    for i, (w, hgt) in enumerate(((220, 20), (170, 18), (120, 18), (74, 16), (34, 14))):
        x0 = W // 2 - w // 2
        y0 = 216 - 40 - sum(h for _, h in ((220, 20), (170, 18), (120, 18), (74, 16), (34, 14))[:i + 1])
        for y in range(y0, y0 + hgt):
            for x in range(x0, x0 + w):
                px[x, y] = sil + (255,)
        for x in range(x0 + 4, x0 + w - 4, 10):       # lit windows
            for y in range(y0 + 6, y0 + 10):
                if r.random() < 0.6:
                    px[x, y] = (255, 200, 96, 255)
                    px[x + 1, y] = (255, 200, 96, 255)
    # the city's domes either side
    for cx, rad in ((40, 30), (88, 18), (300, 26), (350, 20)):
        for y in range(H - 40 - rad, H - 40):
            for x in range(cx - rad, cx + rad):
                if (x - cx) ** 2 + (y - (H - 40)) ** 2 <= rad * rad:
                    px[x, y] = (32, 40, 48, 255)
    for y in range(H - 40, H):                        # the jungle floor
        for x in range(W):
            px[x, y] = (16, 40, 32, 255) if (x * 7 + y * 3) % 13 else (24, 64, 40, 255)
    # canopy fronds over the edges
    for x in range(W):
        h = int(8 + 6 * abs(((x * 13) % 40) - 20) / 20)
        for y in range(H - 40 - h // 3, H - 40):
            px[x, y] = (16, 40, 32, 255)
    gold = [(255, 248, 200), (255, 224, 120), (248, 184, 64), (216, 136, 40), (176, 96, 32), (152, 72, 24), (120, 56, 24)]
    title = "AETHER DESCENT"
    tw = text_width(title) * 3
    draw_text(img, (W - tw) // 2, 34, title, col=gold, outline=(32, 16, 40), scale=3)
    sub = "One hundred floors down."
    draw_text(img, (W - text_width(sub)) // 2, 66, sub, col=(232, 216, 248))
    draw_window(img, W - 100, 118, 88, 46)
    for i, opt in enumerate(("New Game", "Continue", "Settings")):
        y = 126 + i * 12
        draw_text(img, W - 80, y, opt, col=WHITE if i == 0 else GREY)
    draw_text(img, W - 92, 126, "▶", col=GOLDC)
    draw_text(img, W - text_width("v0.1  Godot 4") - 6, H - 10, "v0.1  Godot 4", col=(136, 168, 136), shadow_col=None)
    # party silhouettes walking toward the temple
    for i, n in enumerate(("kael", "mira", "rook")):
        actor(img, hero(n), 2 + i, 11, ox=8, oy=-10)
    return img


def screen_create():
    img = Image.new("RGBA", (W, H), (8, 8, 24, 255))
    draw_map(img, ["########################"] * 14, "abyss")
    fog(img, [(x, y) for x in range(24) for y in range(14)], alpha=170)
    draw_window(img, 4, 4, W - 8, 22)
    draw_text(img, 12, 11, "New Character  -  Choose a class", col=WHITE)
    draw_text(img, W - 92, 11, "Arcanist  4/7", col=CYAN)
    # class list
    draw_window(img, 4, 28, 132, 156)
    classes = ["Arcanist-Engineer", "Aether-Sailor", "Lamp-Witch", "Static Hexer",
               "Glyph-Scribe", "Rune-Smith", "Void Cantor", "Ember Cleric",
               "Clock-Oracle", "Storm Caller", "Brass Theurge", "Mire Augur", "Cinder Saint"]
    for i, c in enumerate(classes):
        y = 36 + i * 11
        draw_text(img, 20, y, c, col=WHITE if i == 1 else GREY)
    draw_text(img, 10, 36 + 11, "▶", col=GOLDC)
    # detail
    draw_window(img, 140, 28, W - 144, 156)
    framed_portrait(img, 152, 40, "vesper")
    draw_text(img, 196, 40, "Aether-Sailor", col=GOLDC)
    draw_text(img, 196, 52, "Void-current navigator.", col=WHITE)
    draw_text(img, 196, 62, "Eerie, unshaken, bends", col=WHITE)
    draw_text(img, 196, 72, "gravity with a scope.", col=WHITE)
    stats = [("HP", "42"), ("ATK", "8"), ("DEF", "5"), ("AE", "14"),
             ("Evasion", "8%"), ("Crit", "0%"), ("Ward", "2%"), ("Vision", "6")]
    for i, (k, v) in enumerate(stats):
        col, row = i % 2, i // 2
        x = 156 + col * 104
        y = 92 + row * 11
        draw_text(img, x, y, k, col=CYAN)
        draw_text(img, x + 70 - text_width(v), y, v, col=WHITE)
    draw_text(img, 156, 140, "School", col=CYAN)
    draw_text(img, 210, 140, "Aether-Sense", col=WHITE)
    draw_text(img, 156, 151, "Ability", col=CYAN)
    draw_text(img, 210, 151, "Steady Footing", col=WHITE)
    draw_text(img, 156, 162, "Leads", col=CYAN)
    draw_text(img, 210, 162, "AETHER-SENSE 9  RESOLVE 8", col=GREEN)
    # the party preview
    draw_window(img, 4, 186, W - 8, 26)
    draw_text(img, 12, 195, "Z: choose   X: back   <>: archetype", col=GREY)
    paste(img, hero("vesper"), W - 30, 187)
    return img


DUNGEON = [
    "########################",
    "########################",
    "###....,,..#####~~~~####",
    "###.,......#####~~~~~###",
    "##.......TT.......~~~###",
    "##..,.....TT.........,.#",
    "##.....>.......,.......#",
    "######..........*......#",
    "######...,.............#",
    "#~~~~##.........TT.....##",
    "#~~~~##..,......TT.....#",
    "#~~~~~#................#",
    "########################",
    "########################",
]


def screen_dungeon():
    img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    rows = [r[:24].ljust(24, '#') for r in DUNGEON]
    draw_map(img, rows, "jungle", oy=-4)
    # remembered-but-not-visible on the far left
    fog(img, [(x, y) for x in range(0, 7) for y in range(0, 14)] +
        [(x, y) for x in range(15, 24) for y in range(0, 4)], oy=-4)
    oy = -4
    # party
    actor(img, hero("kael"), 11, 7, oy=oy)
    actor(img, hero("mira"), 10, 8, oy=oy)
    actor(img, hero("rook"), 12, 9, oy=oy)
    # monsters
    actor(img, monster("tribal", "jungle"), 16, 6, oy=oy)
    actor(img, monster("serpent", "jungle"), 17, 8, oy=oy)
    actor(img, monster("rat", "vermin"), 14, 5, oy=oy)
    # a Conduit bolt in flight from Mira to the tribal
    px = img.load()
    for i in range(0, 64):
        x = 10 * 16 + 10 + i * 1.6
        y = 8 * 16 + oy + 2 - i * 0.55
        if i % 3 == 0:
            for dx, dy in ((0, 0), (1, 0), (0, 1)):
                X, Y = int(x) + dx, int(y) + dy
                px[X, Y] = (255, 255, 160, 255) if i > 40 else (255, 200, 64, 255)
    bx, by = 16 * 16 + 6, 6 * 16 + oy + 6
    for dx in range(-4, 5):
        for dy in range(-4, 5):
            if abs(dx) + abs(dy) <= 4 and (dx == 0 or dy == 0 or abs(dx) == abs(dy)):
                px[bx + dx, by + dy] = (255, 255, 200, 255) if abs(dx) + abs(dy) < 3 else (255, 176, 64, 255)
    draw_text(img, 16 * 16 + 1, 6 * 16 + oy - 22, "24", col=WHITE, outline=(24, 16, 40), scale=1)
    draw_text(img, 14 * 16 + 4, 5 * 16 + oy - 16, "Miss", col=CYAN, outline=(24, 16, 40))
    # HUD: party strip
    draw_window(img, 2, 2, 150, 44)
    paste(img, portrait("kael").crop((4, 2, 28, 26)), 8, 8)
    draw_text(img, 36, 8, "Kael", col=WHITE)
    draw_text(img, 66, 8, "Lv 7", col=GREY)
    draw_text(img, 36, 19, "HP", col=CYAN)
    bar(img, 50, 21, 60, 0.72, (88, 216, 96))
    draw_text(img, 114, 19, "62/86", col=WHITE)
    draw_text(img, 36, 30, "AE", col=CYAN)
    bar(img, 50, 32, 60, 0.45, (96, 160, 248))
    draw_text(img, 114, 30, "9/20", col=WHITE)
    # depth / gold
    draw_window(img, W - 104, 2, 102, 30)
    draw_text(img, W - 96, 8, "B12", col=GOLDC)
    draw_text(img, W - 74, 8, "Jungle Roots", col=WHITE)
    draw_text(img, W - 96, 19, "G", col=GOLDC)
    draw_text(img, W - 86, 19, "1,284", col=WHITE)
    draw_text(img, W - 50, 19, "Keys 1", col=GREY)
    # minimap
    draw_window(img, W - 58, 34, 56, 40, top=(24, 24, 64), bottom=(8, 8, 32))
    mp = img.load()
    for y in range(14):
        for x in range(24):
            ch = rows[y][x]
            if ch == '#':
                continue
            c = (64, 112, 200) if ch == '~' else (96, 136, 88)
            if ch == '>':
                c = (255, 224, 96)
            mp[W - 52 + x * 2, 40 + y * 2] = c + (255,)
    mp[W - 52 + 22, 40 + 14] = (255, 255, 255, 255)
    # message window
    draw_window(img, 2, H - 38, W - 4, 36)
    draw_text(img, 10, H - 32, "Mira casts Brass Arc! The Tribal Skirmisher takes 24.", col=WHITE)
    draw_text(img, 10, H - 21, "The Vine Rat lunges -- Kael sidesteps it.", col=GREY)
    draw_text(img, W - 16, H - 15, "▼", col=GOLDC, shadow_col=None)
    return img


TOWN = [
    "RRRRRRR.RRRRRRR.RRRRRRRR",
    "RRRRRRR.RRRRRRR.RRRRRRRR",
    "WWDWWWW.WWWDWWW.WWWWDWWW",
    "........................",
    "..*....................*",
    "...........F............",
    "........................",
    "..RRRRR...........RRRRR.",
    "..RRRRR.....T.....RRRRR.",
    "..WWDWW...........WWDWW.",
    "........................",
    "........................",
    "........................",
    "........................",
]


def screen_town():
    img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    roof_a = TL.roof()
    roof_b = TL.roof(col=((48, 112, 104), (72, 152, 136), (128, 200, 176)))   # verdigris domes
    wall = TL.house_wall()
    signs = [(248, 208, 72), (200, 64, 64), (96, 160, 248), (136, 216, 120), (200, 120, 232)]
    for ty, row in enumerate(TOWN):
        for tx, ch in enumerate(row):
            X, Y = tx * 16, ty * 16 - 8
            paste(img, TL.cobble((tx * 7 + ty) % 4), X, Y)
            if ch == 'R':
                paste(img, roof_b if tx > 15 or (ty > 5 and tx < 8) else roof_a, X, Y)
            elif ch == 'W':
                paste(img, wall, X, Y)
            elif ch == 'D':
                paste(img, TL.door(signs[(tx + ty) % len(signs)]), X, Y)
            elif ch == 'F':
                paste(img, TL.fountain(), X, Y)
            elif ch == '*':
                paste(img, TL.flowers("jungle", tx), X, Y)
            elif ch == 'T':      # the temple mouth: stairs into the ground
                paste(img, TL.stairs_down("abyss"), X, Y)
    for i, (n, x, y) in enumerate((("kael", 11, 7), ("mira", 10, 7), ("rook", 12, 7))):
        actor(img, hero(n), x, y, oy=-8)
    # townsfolk
    actor(img, hero("ivy"), 4, 4, oy=-8)
    actor(img, hero("sable"), 19, 11, oy=-8)
    # labels hovering over the doors
    for label, tx, ty in (("ARMORY", 2, 2), ("ARCANIST", 11, 2), ("BANK", 20, 2),
                          ("INN", 4, 9), ("TAVERN", 20, 9)):
        w = text_width(label) + 10
        x = tx * 16 + 8 - w // 2
        draw_window(img, max(2, min(W - w - 2, x)), ty * 16 - 8 - 12, w, 14)
        draw_text(img, max(2, min(W - w - 2, x)) + 5, ty * 16 - 8 - 9, label, col=WHITE)
    draw_window(img, 2, H - 26, W - 4, 24)
    draw_text(img, 10, H - 18, "The plaza. Brass domes gone green; somebody still takes good coin.", col=WHITE)
    return img


def screen_shop():
    img = screen_town()
    over = Image.new("RGBA", (W, H), (8, 8, 32, 150))
    img.alpha_composite(over)
    draw_window(img, 4, 4, 120, 22)
    draw_text(img, 14, 11, "Armory", col=GOLDC)
    draw_window(img, W - 100, 4, 96, 22)
    draw_text(img, W - 92, 11, "G", col=GOLDC)
    draw_text(img, W - 80, 11, "1,284", col=WHITE)
    draw_window(img, 4, 28, 228, 124)
    items = [("Rusty Cutlass", 15, "+2"), ("Brass Rapier", 55, "+5"), ("Riveted Cleaver", 110, "+8"),
             ("Steam-Forged Sabre", 200, "+12"), ("Clockwork Estoc", 350, "+16"),
             ("Star-iron Edge", 600, "+21"), ("Ether-Etched Blade", 1000, "+27"),
             ("Weapon +3", 300, "+5"), ("Armour +2", 260, "+5")]
    for i, (n, p, b) in enumerate(items):
        y = 36 + i * 12
        col = WHITE if p <= 1284 else (120, 120, 152)
        if i == 3:
            col = GOLDC
        draw_text(img, 22, y, n, col=col)
        draw_text(img, 160 - text_width(b), y, b, col=GREEN if p <= 1284 else (96, 120, 96))
        ps = "%d" % p
        draw_text(img, 222 - text_width(ps), y, ps, col=col)
    draw_text(img, 10, 36 + 3 * 12, "▶", col=GOLDC)
    # equip comparison
    draw_window(img, 236, 28, W - 240, 124)
    framed_portrait(img, 248, 40, "kael")
    draw_text(img, 290, 40, "Kael", col=WHITE)
    draw_text(img, 290, 51, "Gear-Knight", col=GREY)
    draw_text(img, 248, 84, "ATK", col=CYAN)
    draw_text(img, 278, 84, "14", col=WHITE)
    draw_text(img, 296, 84, "▶", col=GOLDC)
    draw_text(img, 306, 84, "21", col=GREEN)
    draw_text(img, 248, 96, "DEF", col=CYAN)
    draw_text(img, 278, 96, "11", col=WHITE)
    draw_text(img, 296, 96, "▶", col=GOLDC)
    draw_text(img, 306, 96, "11", col=WHITE)
    draw_text(img, 248, 112, "Now:", col=GREY)
    draw_text(img, 248, 123, "Riveted Cleaver", col=WHITE)
    paste(img, hero("kael"), 352, 84)
    draw_window(img, 4, 156, W - 8, 56)
    draw_text(img, 12, 164, "Steam-Forged Sabre", col=GOLDC)
    draw_text(img, 12, 176, "A pressure-tempered blade. Hisses when it bites.", col=WHITE)
    draw_text(img, 12, 188, "Attack +12.  Upgrades you buy here carry over.", col=WHITE)
    draw_text(img, 12, 200, "Z: buy   X: leave   A/S: switch tab", col=GREY)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in (("1-title", screen_title), ("2-character", screen_create),
                     ("3-dungeon", screen_dungeon), ("4-town", screen_town),
                     ("5-shop", screen_shop)):
        img = fn().convert("RGB")
        path = os.path.join(OUT, name + ".png")
        img.resize((W * SCALE, H * SCALE), Image.NEAREST).save(path)
        print("wrote", os.path.relpath(path))


if __name__ == "__main__":
    main()
