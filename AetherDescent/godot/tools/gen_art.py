#!/usr/bin/env python3
"""Build every texture the game loads, and the layout file that indexes them.

    python3 tools/gen_art.py        (from AetherDescent/godot; takes a few minutes)

Writes assets/*.png and scripts/data/art_layout.gd. The art is generated, not
drawn, so changing a colour or a pose here and re-running is the whole edit.
"""
import os
import sys
import time

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import monsters as MON  # noqa: E402
import tiles as TL  # noqa: E402
from characters import CELL_H, CELL_W, FEET_Y, PORTRAIT, Pose, Spec, field_sprite, portrait  # noqa: E402
from font5x7 import GLYPHS  # noqa: E402
from shade import Canvas, ramp  # noqa: E402

GODOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(GODOT, "assets")
LAYOUT = os.path.join(GODOT, "scripts", "data", "art_layout.gd")
T = TL.T
BIOMES = ["jungle", "industrial", "ruins", "wastes", "abyss"]

# ---- dungeon tiles: one row per biome, one column per kind -------------------
TILE_KINDS = [
    "floor0", "floor1", "floor2", "floor3", "grown0", "grown1", "grass0", "grass1", "flowers",
    "thicket0", "thicket1", "face0", "face1", "face2", "top0", "top1", "water0", "water1",
    "lava0", "lava1", "stairs_down", "stairs_up", "bridge", "miasma", "portal", "locked_door",
    "sealed_door", "lever", "crystal", "bloodpool", "prism_red", "prism_blue", "prism_green",
    "snare", "ore", "rubble",
]


def _overlay_on(base, fn):
    img = base.copy()
    c = Canvas(T, T, ss=3, scale=T / 16, cel=True)
    fn(c)
    img.alpha_composite(c.render())
    return img


def _tint_tile(base, col, k):
    img = base.copy()
    px = img.load()
    for y in range(T):
        for x in range(T):
            r, g, b, a = px[x, y]
            px[x, y] = (int(r * (1 - k) + col[0] * k), int(g * (1 - k) + col[1] * k), int(b * (1 - k) + col[2] * k), a)
    return img


def dungeon_tile(biome, kind):
    if kind.startswith("floor"):
        return TL.floor(biome, int(kind[-1]))
    if kind.startswith("grown"):
        return TL.floor(biome, 10 + int(kind[-1]), 0.45)
    if kind.startswith("grass"):
        return TL.grass(biome, int(kind[-1]))
    if kind == "flowers":
        return TL.flowers(biome, 1)
    if kind.startswith("thicket"):
        return TL.thicket(biome, int(kind[-1]))
    if kind.startswith("face"):
        n = int(kind[-1])
        return TL.wall_face(biome, n, 0.35 if n == 1 else 0.0)
    if kind.startswith("top"):
        return TL.wall_top(biome, int(kind[-1]))
    if kind.startswith("water"):
        return TL.water(int(kind[-1]))
    if kind.startswith("lava"):
        return TL.lava(int(kind[-1]))
    if kind == "stairs_down":
        return TL.stairs_down(biome)
    if kind == "stairs_up":
        def f(c):
            st = ramp((200, 196, 186))
            for i in range(4):
                c.poly([(3 + i, 13 - i * 3), (13 - i, 13 - i * 3), (13 - i, 15 - i * 3), (3 + i, 15 - i * 3)], st, round_=0.6)
        return _overlay_on(TL.floor(biome, 0), f)
    if kind == "bridge":
        img = TL.water(0)
        px = img.load()
        wood = ramp((168, 112, 64), n=6)
        for y in range(T):
            for x in range(3, T - 3):
                k = 3 if (y % 8) not in (0, 7) else 1
                if x in (3, T - 4):
                    k = 0
                px[x, y] = wood[k] + (255,)
        return img
    if kind == "miasma":
        img = _tint_tile(TL.floor(biome, 2), (120, 200, 72), 0.35)

        def f(c):
            gas = ramp((150, 230, 96))
            for (x, y, r) in ((5, 6, 3.2), (11, 9, 3.6), (6, 12, 2.6)):
                c.ellipsoid(x, y, r, r * 0.7, gas, gloss=0.6)
        c = Canvas(T, T, ss=3, scale=T / 16, cel=True)
        f(c)
        gas = c.render(outline=False)
        gpx = gas.load()
        for y in range(T):
            for x in range(T):
                r, g, b, a = gpx[x, y]
                if a:
                    gpx[x, y] = (r, g, b, 150)
        img.alpha_composite(gas)
        return img
    if kind == "portal":
        def f(c):
            c.ellipsoid(8, 8.5, 6.5, 6.5, ramp((120, 72, 200)))
            c.ellipsoid(8, 8.5, 4.6, 4.6, ramp((180, 140, 255)), gloss=1.2)
            c.ellipsoid(8, 8.5, 2.4, 2.4, ramp((240, 230, 255)), gloss=1.5)
        return _overlay_on(TL.floor(biome, 1), f)
    if kind in ("locked_door", "sealed_door"):
        img = TL.wall_face(biome, 2)
        col = (168, 112, 64) if kind == "locked_door" else (120, 128, 148)

        def f(c):
            c.poly([(3, 4), (13, 4), (13, 16), (3, 16)], ramp(col), round_=1.0)
            if kind == "locked_door":
                c.ellipsoid(10.5, 10.5, 1.2, 1.2, ramp((248, 208, 72)), gloss=1.2)
            else:
                for yy in (7, 11, 14):
                    c.poly([(3, yy), (13, yy), (13, yy + 1), (3, yy + 1)], ramp((80, 84, 100)), round_=0.4)
        return _overlay_on(img, f)
    if kind == "lever":
        def f(c):
            c.ellipsoid(8, 12, 4.5, 2.2, ramp((110, 110, 124)))
            c.limb(8, 11.5, 11.5, 4, 0.8, 0.7, ramp((150, 150, 164)), gloss=0.8)
            c.ellipsoid(11.8, 3.6, 1.6, 1.6, ramp((220, 56, 56)), gloss=1.2)
        return _overlay_on(TL.floor(biome, 3), f)
    if kind == "crystal":
        def f(c):
            cr = ramp((140, 220, 255))
            c.limb(6, 15, 5, 3, 2.6, 0.8, cr, gloss=1.4)
            c.limb(10, 15, 12, 5, 2.4, 0.8, cr, gloss=1.4)
            c.limb(8, 15, 8.5, 8, 2.0, 0.6, ramp((200, 160, 255)), gloss=1.4)
        return _overlay_on(TL.floor(biome, 2), f)
    if kind == "bloodpool":
        def f(c):
            c.ellipsoid(8, 9, 6.8, 5.2, ramp((168, 24, 40)), gloss=1.0)
        return _overlay_on(TL.floor(biome, 1), f)
    if kind.startswith("prism"):
        col = {"prism_red": (240, 64, 72), "prism_blue": (72, 120, 248), "prism_green": (72, 220, 120)}[kind]

        def f(c):
            c.ellipsoid(8, 9, 6.8, 5.2, ramp(col), gloss=1.6)
        return _overlay_on(TL.floor(biome, 0), f)
    if kind == "snare":
        def f(c):
            for i in range(6):
                import math
                a = i * math.pi / 3
                c.limb(8, 9, 8 + math.cos(a) * 5, 9 + math.sin(a) * 4, 1.2, 0.3, ramp((200, 64, 120)))
            c.ellipsoid(8, 9, 2.2, 1.8, ramp((248, 200, 72)), gloss=0.8)
        return _overlay_on(TL.grass(biome, 3), f)
    if kind == "ore":
        def f(c):
            for (x, y) in ((5, 7), (10, 10), (7, 13), (12, 5)):
                c.ellipsoid(x, y, 1.6, 1.3, ramp((120, 220, 240)), gloss=1.4)
        return _overlay_on(TL.wall_face(biome, 0), f)
    if kind == "rubble":
        def f(c):
            st = ramp(TL.BIOMES[biome][2])
            for (x, y, r) in ((5, 10, 2.4), (9, 12, 2.0), (11, 7, 1.6), (6, 5, 1.2)):
                c.ellipsoid(x, y, r, r * 0.8, st)
        return _overlay_on(TL.floor(biome, 3), f)
    raise KeyError(kind)


# ---- town ------------------------------------------------------------------------
# Every building's door wears its trade's colour on the sign above it.
TOWN_DOORS = [
    ("general", (248, 208, 72)), ("armory", (200, 64, 64)), ("apothecary", (136, 216, 120)),
    ("arcanist", (176, 120, 248)), ("inn", (248, 160, 96)), ("tavern", (232, 120, 64)),
    ("junkyard", (150, 140, 120)), ("gladiator", (224, 72, 48)), ("bank", (96, 160, 248)),
    ("races", (120, 200, 96)), ("kitchen", (240, 136, 80)), ("blackmarket", (90, 64, 110)),
    ("oracle", (140, 220, 240)), ("altar", (232, 232, 240)), ("bazaar", (240, 184, 64)),
]
TOWN_KINDS = (["cobble0", "cobble1", "cobble2", "cobble3", "roof_red", "roof_teal", "roof_slate", "house_wall"]
              + ["door_" + n for n, _ in TOWN_DOORS]
              + ["fountain", "quest_board", "grass", "flowers", "temple", "planter"])


def town_tile(kind):
    if kind.startswith("cobble"):
        return TL.floor_town(int(kind[-1]))
    if kind == "roof_red":
        return TL.roof()
    if kind == "roof_teal":
        return TL.roof(colour=(72, 156, 140))
    if kind == "roof_slate":
        return TL.roof(colour=(96, 104, 136))
    if kind == "house_wall":
        return TL.house_wall()
    if kind.startswith("door_"):
        return TL.door(dict(TOWN_DOORS)[kind[5:]])
    if kind == "fountain":
        return TL.fountain()
    if kind == "quest_board":
        def f(c):
            wood = ramp((150, 96, 56))
            c.limb(4, 15, 4, 4, 0.8, 0.8, wood)
            c.limb(12, 15, 12, 4, 0.8, 0.8, wood)
            c.poly([(2.5, 3), (13.5, 3), (13.5, 11), (2.5, 11)], wood, round_=0.8)
            for (x, y) in ((4.5, 5), (8.5, 4.5), (6, 8)):
                c.poly([(x, y), (x + 3, y), (x + 3, y + 3), (x, y + 3)], ramp((240, 232, 200)), round_=0.4)
        return _overlay_on(TL.floor_town(1), f)
    if kind == "grass":
        return TL.grass("jungle", 4)
    if kind == "flowers":
        return TL.flowers("jungle", 5)
    if kind == "temple":
        return TL.stairs_down("abyss")
    if kind == "planter":
        def f(c):
            c.poly([(3, 9), (13, 9), (12, 15), (4, 15)], ramp((176, 96, 64)), round_=1.0)
            for (x, y) in ((5.5, 7.5), (8, 6), (10.5, 7.5)):
                c.ellipsoid(x, y, 2.6, 2.4, ramp((84, 156, 64)), gloss=0.4)
        return _overlay_on(TL.floor_town(2), f)
    raise KeyError(kind)


# ---- heroes ------------------------------------------------------------------------
# Fourteen looks: two per archetype. A class wears look archetype*2 + (id % 2).
LOOKS = [
    # Vanguard
    Spec("blonde", "spiky", cloth="steel", trim="gold", scarf="crimson", legs="navy", pauldrons=True,
         extras={"weapon": "sword"}),
    Spec("brown", "tail", skin="olive", cloth="rust", trim="steel", legs="leather", hat="helm",
         extras={"weapon": "axe"}),
    # Skirmisher
    Spec("red", "tail", cloth="forest", trim="brass", legs="sand", hat="band", hat_col="crimson",
         eyes=(72, 160, 88), extras={"weapon": "dagger"}),
    Spec("black", "bob", skin="warm", cloth="plum", trim="gold", legs="black", female=True, gloves="",
         eyes=(200, 80, 120), extras={"weapon": "dagger"}),
    # Marksman
    Spec("green", "spiky", cloth="forest", trim="leather", legs="sand", hat="hood", hat_col="forest",
         eyes=(72, 168, 120), extras={"weapon": "bow"}),
    Spec("orange", "long", cloth="sand", trim="crimson", legs="leather", female=True, gloves="",
         eyes=(64, 120, 200), extras={"weapon": "bow"}),
    # Arcanist
    Spec("pink", "long", cloth="violet", trim="gold", legs="plum", hat="wizard", hat_col="violet",
         female=True, gloves="", extras={"weapon": "staff"}),
    Spec("silver", "spiky", cloth="navy", trim="gold", legs="navy", hat="wizard", hat_col="navy",
         eyes=(176, 64, 96), gloves="", extras={"weapon": "staff"}),
    # Artificer
    Spec("navy", "wild", skin="warm", cloth="brass", trim="leather", legs="black", hat="goggles",
         eyes=(200, 120, 48), extras={"weapon": "hammer"}),
    Spec("teal", "bob", cloth="white", trim="brass", legs="navy", hat="goggles", female=True,
         eyes=(72, 168, 176), extras={"weapon": "hammer"}),
    # Survivor
    Spec("brown", "wild", skin="olive", cloth="leather", trim="forest", legs="sand", scarf="forest",
         eyes=(120, 88, 56), extras={"weapon": "axe"}),
    Spec("white", "long", skin="deep", cloth="teal", trim="sand", legs="leather", female=True, gloves="",
         eyes=(200, 152, 64), extras={"weapon": "sword"}),
    # Envoy
    Spec("black", "spiky", skin="olive", cloth="navy", trim="gold", legs="black", hat="tophat", hat_col="black",
         eyes=(120, 88, 200), extras={"weapon": "rapier"}),
    Spec("silver", "bob", cloth="crimson", trim="gold", legs="black", cape="navy", female=True,
         eyes=(176, 64, 96), gloves="", extras={"weapon": "rapier"}),
]
FACINGS = ["down", "left", "right", "up"]
HERO_FRAMES = ["walk0", "walk1", "walk2", "walk3", "atk0", "atk1", "atk2", "cast"]
EXPRESSIONS = ["neutral", "happy", "angry", "hurt"]


def hero_pose(facing, frame):
    if frame.startswith("walk"):
        return Pose(facing, walk=int(frame[-1]))
    if frame.startswith("atk"):
        return Pose(facing, attack=int(frame[-1]))
    return Pose(facing, cast=True)


# ---- monsters ----------------------------------------------------------------------
MON_KINDS = ["rat", "serpent", "tribal", "wisp", "automaton", "wraith", "golem", "hound", "horror"]
MON_FAMILIES = ["jungle", "vermin", "clockwork", "ruins", "outrider", "abyssal", "spirit", "flame", "boss"]

# ---- props -------------------------------------------------------------------------
PROP_KINDS = ["gold", "potion", "tonic", "guard", "ration", "recall", "key", "relic", "quest", "elixir",
              "shrine", "fountain", "merchant", "machine", "waygate", "altar", "toll", "console", "strongbox",
              "corpse"]


def prop(kind):
    c = Canvas(T, T, ss=3, scale=T / 16, cel=True)
    if kind == "gold":
        g = ramp((248, 200, 64))
        for (x, y) in ((6, 11), (10, 11), (8, 9.5), (8, 12.5), (5, 13), (11, 13)):
            c.ellipsoid(x, y, 2.2, 1.3, g, gloss=1.3)
    elif kind in ("potion", "tonic", "guard", "elixir"):
        col = {"potion": (232, 56, 72), "tonic": (72, 140, 248), "guard": (72, 200, 104), "elixir": (248, 200, 72)}[kind]
        c.ellipsoid(8, 10.5, 4.0, 4.0, ramp(col), gloss=1.5)
        c.limb(8, 7, 8, 4, 1.3, 1.3, ramp((200, 220, 240)), gloss=0.8)
        c.ellipsoid(8, 3.4, 1.6, 1.0, ramp((150, 100, 64)))
    elif kind == "ration":
        c.poly([(3, 8), (13, 8), (12.5, 14), (3.5, 14)], ramp((168, 120, 72)), round_=1.4)
        c.poly([(3, 7), (13, 7), (13, 9), (3, 9)], ramp((210, 180, 132)), round_=0.6)
    elif kind == "recall":
        c.ellipsoid(8, 9, 4.4, 4.4, ramp((96, 220, 220)), gloss=1.6)
        c.ellipsoid(8, 9, 2.0, 2.0, ramp((240, 255, 255)), gloss=1.6)
        c.limb(8, 4.4, 8, 2.0, 0.6, 0.6, ramp((248, 208, 72)))
    elif kind == "key":
        gk = ramp((248, 200, 64))
        c.ellipsoid(5, 8, 2.6, 2.6, gk, gloss=1.2)
        c.limb(7, 8, 13, 8, 0.8, 0.8, gk, gloss=1.0)
        c.limb(11.5, 8, 11.5, 10.5, 0.6, 0.6, gk)
        c.limb(13, 8, 13, 10.5, 0.6, 0.6, gk)
    elif kind in ("relic", "strongbox"):
        col = (168, 104, 56) if kind == "relic" else (112, 116, 132)
        c.poly([(2.5, 7), (13.5, 7), (13.5, 14), (2.5, 14)], ramp(col), round_=1.2)
        c.poly([(2.5, 4.5), (13.5, 4.5), (13.5, 7.5), (2.5, 7.5)], ramp(col), round_=1.0, gloss=0.4)
        c.poly([(2.5, 7), (13.5, 7), (13.5, 8), (2.5, 8)], ramp((248, 200, 64)), round_=0.4)
        c.ellipsoid(8, 9.5, 1.3, 1.3, ramp((248, 200, 64)), gloss=1.4)
    elif kind == "quest":
        c.poly([(4, 3), (12, 3), (12, 14), (4, 14)], ramp((240, 228, 196)), round_=0.8)
        c.limb(4, 3, 12, 3, 1.0, 1.0, ramp((176, 120, 72)))
        c.ellipsoid(8, 12, 1.5, 1.5, ramp((200, 40, 40)), gloss=1.0)
    elif kind == "shrine":
        st = ramp((200, 200, 220))
        c.poly([(4, 8), (12, 8), (12, 15), (4, 15)], st, round_=1.2)
        c.ellipsoid(8, 5.5, 3.2, 3.2, ramp((248, 232, 140)), gloss=1.6)
    elif kind == "fountain":
        st = ramp((184, 188, 204))
        c.ellipsoid(8, 11, 6.5, 3.5, st)
        c.ellipsoid(8, 11, 5.0, 2.4, ramp((64, 140, 230)), gloss=1.4)
        c.limb(8, 10, 8, 4, 1.0, 1.0, st)
        c.ellipsoid(8, 3.5, 1.6, 1.8, ramp((170, 220, 255)), gloss=1.5)
    elif kind == "merchant":
        c.poly([(1.5, 9), (14.5, 9), (14.5, 15), (1.5, 15)], ramp((150, 96, 56)), round_=1.0)
        c.poly([(1, 3), (15, 3), (15, 6), (1, 6)], ramp((200, 64, 64)), round_=0.8)
        c.limb(2, 6, 2, 9, 0.6, 0.6, ramp((150, 96, 56)))
        c.limb(14, 6, 14, 9, 0.6, 0.6, ramp((150, 96, 56)))
        for x in (5, 8, 11):
            c.ellipsoid(x, 8.2, 1.3, 1.6, ramp(((232, 56, 72), (72, 140, 248), (72, 200, 104))[(x - 5) // 3]), gloss=1.2)
    elif kind == "machine":
        m = ramp((176, 132, 72))
        c.poly([(3, 5), (13, 5), (13, 15), (3, 15)], m, round_=1.4, gloss=0.5)
        c.ellipsoid(8, 9, 2.6, 2.6, ramp((96, 220, 255)), gloss=1.6)
        c.limb(12, 5, 13, 1.5, 0.7, 0.5, m)
    elif kind == "waygate":
        st = ramp((150, 156, 180))
        c.limb(4, 15, 4, 4, 1.4, 1.2, st)
        c.limb(12, 15, 12, 4, 1.4, 1.2, st)
        c.limb(3, 3.5, 13, 3.5, 1.4, 1.4, st)
        c.ellipsoid(8, 9.5, 3.0, 5.0, ramp((120, 220, 255)), gloss=1.4)
    elif kind == "altar":
        c.poly([(2.5, 9), (13.5, 9), (13.5, 15), (2.5, 15)], ramp((140, 40, 56)), round_=1.0)
        c.poly([(2, 7.5), (14, 7.5), (14, 9.5), (2, 9.5)], ramp((220, 210, 220)), round_=0.6)
        c.ellipsoid(8, 6.5, 2.0, 1.4, ramp((200, 24, 40)), gloss=1.4)
    elif kind == "toll":
        c.poly([(4, 4), (12, 4), (12, 15), (4, 15)], ramp((104, 96, 120)), round_=1.4)
        c.ellipsoid(8, 8, 2.0, 2.0, ramp((248, 200, 64)), gloss=1.4)
    elif kind == "console":
        c.poly([(2.5, 7), (13.5, 7), (12.5, 15), (3.5, 15)], ramp((120, 128, 140)), round_=1.0)
        for (x, col) in ((5, (240, 72, 72)), (8, (248, 208, 72)), (11, (72, 220, 120))):
            c.ellipsoid(x, 9.5, 1.2, 1.2, ramp(col), gloss=1.4)
    elif kind == "corpse":
        c.ellipsoid(8, 12, 5, 2.4, ramp((96, 64, 64)))
        c.ellipsoid(5, 11, 2, 1.5, ramp((200, 200, 200)))
    return c.render()


def font_sheet():
    """The 5x7 font as a strip of 6x10 cells, ASCII 32..126 then the specials."""
    chars = [chr(c) for c in range(32, 127)] + ["▶", "♥", "✦", "▼", "▲"]
    cw, ch = 8, 10
    img = Image.new("RGBA", (cw * 16, ch * ((len(chars) + 15) // 16)), (0, 0, 0, 0))
    px = img.load()
    widths = []
    for i, c in enumerate(chars):
        g = GLYPHS.get(c, GLYPHS["?"])
        ox, oy = (i % 16) * cw, (i // 16) * ch
        for y, row in enumerate(g):
            for x, v in enumerate(row):
                if v == "#":
                    px[ox + x, oy + y] = (255, 255, 255, 255)
        widths.append(len(g[0]))
    return img, chars, widths


def sheet(cells, cols, cw, chh):
    rows = (len(cells) + cols - 1) // cols
    img = Image.new("RGBA", (cols * cw, rows * chh), (0, 0, 0, 0))
    for i, c in enumerate(cells):
        if c is not None:
            img.alpha_composite(c, ((i % cols) * cw, (i // cols) * chh))
    return img


def main():
    os.makedirs(ASSETS, exist_ok=True)
    only = set(sys.argv[1:])
    t0 = time.time()

    def want(name):
        return not only or name in only

    if want("tiles"):
        cells = []
        for b in BIOMES:
            cells += [dungeon_tile(b, k) for k in TILE_KINDS]
        cells += [town_tile(k) for k in TOWN_KINDS] + [None] * (len(TILE_KINDS) - len(TOWN_KINDS))
        sheet(cells, len(TILE_KINDS), T, T).save(os.path.join(ASSETS, "tiles.png"))
        torch = TL.torch()
        torch.save(os.path.join(ASSETS, "torch.png"))
        print("tiles", round(time.time() - t0, 1))
    if want("heroes"):
        cells = []
        for spec in LOOKS:
            for f in FACINGS:
                cells += [field_sprite(spec, hero_pose(f, fr)) for fr in HERO_FRAMES]
        sheet(cells, len(HERO_FRAMES), CELL_W, CELL_H).save(os.path.join(ASSETS, "heroes.png"))
        print("heroes", round(time.time() - t0, 1))
    if want("portraits"):
        cells = [portrait(spec, e) for spec in LOOKS for e in EXPRESSIONS]
        sheet(cells, len(EXPRESSIONS), PORTRAIT, PORTRAIT).save(os.path.join(ASSETS, "portraits.png"))
        print("portraits", round(time.time() - t0, 1))
    if want("monsters"):
        cells = [MON.monster(k, fam, fr) for fam in MON_FAMILIES for k in MON_KINDS for fr in range(3)]
        sheet(cells, len(MON_KINDS) * 3, MON.MON_SIZE, MON.MON_SIZE).save(os.path.join(ASSETS, "monsters.png"))
        cells = [MON.warden(fr) for fr in range(3)]
        sheet(cells, 3, MON.BOSS_SIZE, MON.BOSS_SIZE).save(os.path.join(ASSETS, "warden.png"))
        print("monsters", round(time.time() - t0, 1))
    if want("props"):
        sheet([prop(k) for k in PROP_KINDS], len(PROP_KINDS), T, T).save(os.path.join(ASSETS, "props.png"))
        print("props", round(time.time() - t0, 1))
    fimg, chars, widths = font_sheet()
    fimg.save(os.path.join(ASSETS, "font.png"))

    def arr(xs):
        return "[" + ", ".join('"%s"' % x if isinstance(x, str) else str(x) for x in xs) + "]"
    with open(LAYOUT, "w", encoding="utf-8") as f:
        f.write("# GENERATED by tools/gen_art.py -- the index into the generated textures.\n")
        f.write("class_name ArtLayout\n\n")
        f.write("const TILE := %d\n" % T)
        f.write("const TILE_KINDS := %s\n" % arr(TILE_KINDS))
        f.write("const TOWN_KINDS := %s\n" % arr(TOWN_KINDS))
        f.write("const TOWN_ROW := %d\n" % len(BIOMES))
        f.write("const HERO_CELL := Vector2i(%d, %d)\n" % (CELL_W, CELL_H))
        f.write("const HERO_FEET_Y := %d\n" % FEET_Y)
        f.write("const HERO_LOOKS := %d\n" % len(LOOKS))
        f.write("const HERO_FACINGS := %s\n" % arr(FACINGS))
        f.write("const HERO_FRAMES := %s\n" % arr(HERO_FRAMES))
        f.write("const PORTRAIT := %d\n" % PORTRAIT)
        f.write("const EXPRESSIONS := %s\n" % arr(EXPRESSIONS))
        f.write("const MON_CELL := %d\n" % MON.MON_SIZE)
        f.write("const BOSS_CELL := %d\n" % MON.BOSS_SIZE)
        f.write("const MON_KINDS := %s\n" % arr(MON_KINDS))
        f.write("const MON_FAMILIES := %s\n" % arr(MON_FAMILIES))
        f.write("const PROP_KINDS := %s\n" % arr(PROP_KINDS))
        f.write("const FONT_CELL := Vector2i(8, 10)\n")
        f.write("const FONT_CHARS := %s\n" % ("[" + ", ".join('"%s"' % (c.replace("\\", "\\\\").replace('"', '\\"')) for c in chars) + "]"))
        f.write("const FONT_WIDTHS := %s\n" % arr(widths))
    print("done in", round(time.time() - t0, 1), "s")


if __name__ == "__main__":
    main()
