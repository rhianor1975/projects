#!/usr/bin/env python3
"""The Classic art set: the flat look of the first mockups, animated.

Every sheet has exactly the layout of the 16-bit set at half the size --
16px tiles, 24x30 hero cells, 32px portraits, 24px monster cells -- so the
game draws it at 2x with the same indices and nothing else changes.

Heroes, portraits and monsters are hand-drawn character grids (the
first-mockup sprites, plus the back and side views, walk, attack and cast
frames they never had). Tiles reuse the first-mockup tile code where it
exists; the kinds it never drew are the 16-bit tiles flattened.

    python3 tools/classic/gen_classic.py      (from AetherDescent/godot)
"""
import math
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(1, os.path.dirname(HERE))
import classic_tiles as CT  # noqa: E402
from classic_pixelart import HAIR, CLOTH, grid_image, mirror, palette  # noqa: E402
import classic_sprites as CS  # noqa: E402

GODOT = os.path.dirname(os.path.dirname(HERE))
HD = os.path.join(GODOT, "assets")
OUT = os.path.join(GODOT, "assets", "classic")
T = 16
CELL_W, CELL_H = 24, 30
SPR_X, SPR_Y = 4, 5          # where the 16x24 body sits in its cell: boots on row 28, as the 16-bit feet

# ---- heroes ---------------------------------------------------------------------
# The fourteen looks of the 16-bit set, in the same order, translated to the
# flat palettes: (hair style, hair, cloth, accent, weapon).
LOOKS = [
    ("spiky", "blonde", "steel", "crimson", "sword"), ("tail", "brown", "rust", "steel", "axe"),
    ("tail", "red", "forest", "brass", "dagger"), ("long", "black", "violet", "brass", "dagger"),
    ("spiky", "green", "forest", "brass", "bow"), ("long", "brown", "brass", "crimson", "bow"),
    ("long", "pink", "violet", "brass", "staff"), ("spiky", "silver", "teal", "brass", "staff"),
    ("spiky", "navy", "brass", "teal", "hammer"), ("long", "green", "white", "brass", "hammer"),
    ("tail", "brown", "rust", "forest", "axe"), ("long", "silver", "teal", "brass", "sword"),
    ("spiky", "black", "teal", "brass", "rapier"), ("long", "silver", "crimson", "violet", "rapier"),
]
FACINGS = ["down", "left", "right", "up"]
FRAMES = ["walk0", "walk1", "walk2", "walk3", "atk0", "atk1", "atk2", "cast"]

LEGS_STAND = ["..kCCcCkkCcCCk..", "..kbbBk..kBbbk..", "..kkkk....kkkk.."]
LEGS_A = ["..kCCcCkkBbbk...", "..kbbBk..kkkk...", "..kkkk.........."]
LEGS_B = ["...kbbBkkCcCCk..", "...kkkk..kBbbk..", "..........kkkk.."]

SIDE_HEAD = {
    "spiky": [
        "......k..k......",
        ".....khkkhk.k...",
        "....khhhhhhkhk..",
        "...khhhlhhhhhhk.",
        "..khhhlhhhhhhhk.",
        "..khhhhhhhhhhhhk",
        ".khhhhhhhhhhhhk.",
        ".kHhhHhhhhhhhhhk",
    ],
    "long": [
        "......kkkk......",
        "....kkhhhhkk....",
        "...khhhhhhhhk...",
        "..khhhlhhhhhhk..",
        "..khhlhhhhhhhk..",
        ".khhhhhhhhhhhhk.",
        ".khhhhhhhhhhhhk.",
        ".kHhhHhhhhhhhhk.",
    ],
    "tail": [
        ".......kk.......",
        ".....kkhhkk.....",
        "....khhhhhhk.kk.",
        "...khhlhhhhhkhhk",
        "..khhlhhhhhhhhk.",
        "..khhhhhhhhhhhk.",
        ".khhhhhhhhhhhhk.",
        ".kHhhHhhhhhhhhk.",
    ],
}
SIDE_FACE = [
    ".kssHssHhhhhhhk.",
    "ksssssssHhhhhhk.",
    "kskksssshhhhhk..",
    "ksewsssssHhhhk..",
    ".kEessssShhhk...",
    ".ksrsssssSkk....",
    "..kSssSSSk......",
    "...kkssk........",
]
SIDE_BODY = [
    "....kcccLck.....",
    "...kccccCcck....",
    "..kscccaCcck....",
    "...kaaaaaak.....",
    "....kCcccCk.....",
]
SIDE_LEGS = {
    "stand": ["....kCCCCk......", "...kbbbbBk......", "...kkkkkkk......"],
    "a": ["...kCCk.kCk.....", "..kbbBk.kbbk....", "..kkkk..kkkk...."],
    "b": ["....kCkkCCk.....", "...kbbkbbBk.....", "...kkkkkkkk....."],
}


def _front(style, frame):
    rows = CS.hero_rows(style)
    if frame in ("walk1", "walk3"):
        legs = LEGS_A if frame == "walk1" else LEGS_B
        rows = rows[:21] + legs
    return rows


def _back(style, frame):
    top = CS.HERO_HAIR[style]
    hair = ["khhhhhhh", "khhhhHhh", "khHhhhhh", "khhhhhhh", ".khhHhhh", ".khhhhhh", "..khhhhh", "...kkSSS"]
    body = ["..kcccCc", ".kscCcCc", ".kscCccc", ".kskCaaa", "..kCcccc", "..kCCcCk", "..kbbBk.", "..kkkk.."]
    if style == "long":
        hair = ["khhhhhhh", "khhhhHhh", "khHhhhhh", "khhhhhhh", "khhhHhhh", "khhhhhhh", "khhhhhhh", "khhhhhhh"]
        body = ["khhhhhhh", ".khhHhhh", ".kshhhhh", ".kskhhhh", "..kChhhh", "..kCCcCk", "..kbbBk.", "..kkkk.."]
    rows = mirror(top + hair + body)
    if style == "tail":
        rows[8] = rows[8][:7] + "hh" + rows[8][9:]
        for r in range(9, 17):
            rows[r] = rows[r][:7] + "hH" + rows[r][9:]
    if frame in ("walk1", "walk3"):
        rows = rows[:21] + (LEGS_A if frame == "walk1" else LEGS_B)
    return rows


def _side(style, frame):
    head = list(SIDE_HEAD[style])
    face = list(SIDE_FACE)
    body = list(SIDE_BODY)
    if style == "long":
        for i in range(len(face)):
            face[i] = face[i][:11] + "hhhk"[: 16 - 11] if i < 7 else face[i]
            face[i] = (face[i] + "................")[:16]
        body[0] = "....kcccLchhk..."
        body[1] = "...kccccCchhk..."
        body[2] = "..kscccaCchk...."
    legs = SIDE_LEGS["a" if frame == "walk1" else "b" if frame == "walk3" else "stand"]
    return head + face + body + legs


def _overlay_weapon(img, facing, frame, weapon):
    """The weapon and the swing, drawn over the hero in its 24x30 cell."""
    d = ImageDraw.Draw(img)
    steel, edge, hilt = (220, 228, 248), (96, 104, 136), (200, 160, 64)
    wood, orb = (150, 96, 56), (120, 220, 255)
    ink = (24, 16, 40)
    # hand positions in cell coordinates and blade direction per frame
    if facing in ("down", "up"):
        poses = {"atk0": ((19, 12), -60), "atk1": ((14, 21), 110), "atk2": ((6, 24), 160)}
    elif facing == "left":
        poses = {"atk0": ((14, 11), -120), "atk1": ((5, 19), 180), "atk2": ((6, 24), 140)}
    else:
        poses = {"atk0": ((9, 11), -60), "atk1": ((18, 19), 0), "atk2": ((17, 24), 40)}
    if frame not in poses:
        return
    (hx, hy), ang = poses[frame]
    a = math.radians(ang)
    dx, dy = math.cos(a), math.sin(a)
    L = {"sword": 9, "dagger": 5, "rapier": 10, "axe": 8, "hammer": 8, "staff": 11, "bow": 6}[weapon]
    ex, ey = hx + dx * L, hy + dy * L
    if weapon == "bow":
        for k in range(-5, 6):
            px = hx + dx * 2 - dy * k * 0.9 + dx * (-abs(k) * 0.3)
            py = hy + dy * 2 + dx * k * 0.9 + dy * (-abs(k) * 0.3)
            d.point((round(px), round(py)), fill=wood + (255,))
        d.line([(round(hx - dy * 4.5), round(hy + dx * 4.5)), (round(hx + dy * 4.5), round(hy - dx * 4.5))], fill=(230, 230, 230, 255))
    else:
        col = wood if weapon in ("staff", "hammer", "axe") else steel
        d.line([(round(hx), round(hy)), (round(ex), round(ey))], fill=ink + (255,), width=3)
        d.line([(round(hx), round(hy)), (round(ex), round(ey))], fill=col + (255,), width=1)
        if weapon == "staff":
            d.ellipse([ex - 2, ey - 2, ex + 2, ey + 2], fill=orb + (255,), outline=ink + (255,))
        elif weapon == "hammer":
            d.rectangle([ex - 2, ey - 2, ex + 2, ey + 2], fill=hilt + (255,), outline=ink + (255,))
        elif weapon == "axe":
            d.ellipse([ex - 2, ey - 2, ex + 2, ey + 2], fill=steel + (255,), outline=ink + (255,))
        else:
            d.point((round(hx - dy), round(hy + dx)), fill=hilt + (255,))
            d.point((round(hx + dy), round(hy - dx)), fill=hilt + (255,))
    if frame == "atk1":
        # the swoosh
        cx, cy, r = (12, 16, 10)
        a0, a1 = (-40, 130) if facing in ("down", "up") else ((110, 250) if facing == "left" else (-70, 70))
        for k in range(24):
            aa = math.radians(a0 + (a1 - a0) * k / 23)
            x, y = round(cx + math.cos(aa) * r), round(cy + math.sin(aa) * r)
            if 0 <= x < CELL_W and 0 <= y < CELL_H:
                img.putpixel((x, y), (255, 255, 255, 255) if k % 5 else (180, 220, 255, 255))


def _overlay_cast(img):
    for (x, y, c) in ((3, 6, (255, 240, 140)), (20, 5, (255, 255, 255)), (2, 12, (140, 220, 255)),
                      (21, 11, (255, 240, 140)), (12, 1, (255, 255, 255))):
        for ddx, ddy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
            if 0 <= x + ddx < CELL_W and 0 <= y + ddy < CELL_H:
                img.putpixel((x + ddx, y + ddy), c + (255,) if (ddx, ddy) == (0, 0) else tuple(int(v * 0.8) for v in c) + (255,))


def hero_cell(look, facing, frame):
    style, hair, cloth, acc, weapon = LOOKS[look]
    pal = palette(hair, cloth, acc)
    base = "walk0" if frame.startswith("atk") or frame == "cast" else frame
    if facing == "down":
        rows = _front(style, base)
    elif facing == "up":
        rows = _back(style, base)
    else:
        rows = _side(style, base)
    spr = grid_image(rows, pal)
    if facing == "right":
        spr = spr.transpose(Image.FLIP_LEFT_RIGHT)
    cell = Image.new("RGBA", (CELL_W, CELL_H), (0, 0, 0, 0))
    bob = -1 if base in ("walk1", "walk3") else 0
    lunge = {"atk1": 2, "atk2": 1}.get(frame, 0)
    ox = {"left": -lunge, "right": lunge}.get(facing, 0)
    oy = lunge if facing == "down" else (-lunge if facing == "up" else 0)
    if facing == "up" and frame.startswith("atk"):
        _overlay_weapon(cell, facing, frame, weapon)        # behind the back
    cell.alpha_composite(spr, (SPR_X + ox, SPR_Y + bob + oy))
    if frame == "cast":
        _overlay_cast(cell)
    elif frame.startswith("atk") and facing != "up":
        _overlay_weapon(cell, facing, frame, weapon)
    return cell


# ---- portraits -----------------------------------------------------------------
def portrait_cell(look, expr):
    style = LOOKS[look][0]
    rows = list(CS.PORTRAIT_LONG if style == "long" else CS.PORTRAIT_SPIKY)
    pal = palette(LOOKS[look][1], LOOKS[look][2], LOOKS[look][3])
    if expr in ("happy", "hurt"):
        # closed eyes: an arc for happy, a flat line for hurt
        shut = ["......", "......", ".kkkk." if expr == "happy" else "......",
                "k....k" if expr == "happy" else "kkkkkk", "......", "......", "......"]
        for i in range(7):
            r = rows[15 + i]
            rows[15 + i] = r[:8] + "".join(("s" if c == "." else c) for c in shut[i]) + r[14:]
    if expr == "angry":
        for i, br in ((13, "kk.."),):
            r = rows[i]
            rows[i] = r[:9] + "Hkk" + r[12:]
    img = grid_image(mirror(rows), pal)
    if expr == "hurt":
        for x in range(13, 19):
            img.putpixel((x, 26), (120, 40, 60, 255))
    return img


# ---- monsters ------------------------------------------------------------------
EXTRA_MONSTERS = {
    "golem": [
        "................",
        "......kkkk......",
        ".....k2222k.....",
        ".....k2e2ek.....",
        "..kkk.k22k.kkk..",
        ".k222kk22kk222k.",
        ".k232k3443k232k.",
        ".k222k3443k222k.",
        "..kkkk2332kkkk..",
        "....k222222k....",
        "....k221122k....",
        "....k22kk22k....",
        "...kk22kk22kk...",
        "...k1111k1111k..",
        "...kkkkk.kkkkk..",
        "................",
    ],
    "hound": [
        "................",
        "................",
        "..k.............",
        ".k2k............",
        "k2e2k.......k...",
        "k22222kkkkkk2k..",
        ".k3k2222222222k.",
        "..kk2222222222k.",
        "....k23332222k..",
        "....k2kk2k2kk2k.",
        "....k2k.k2k.k2k.",
        "....k1k.k1k.k1k.",
        "....kk..kk..kk..",
        "................",
        "................",
        "................",
    ],
    "horror": [
        "................",
        ".....kkkkkk.....",
        "...kk222222kk...",
        "..k2e22e22e22k..",
        ".k22222222222k..",
        ".k23222222232k..",
        "k2222kkkkk2222k.",
        "k222k11111k222k.",
        "k222k1kkk1k222k.",
        ".k22kkkkkkk22k..",
        ".k4k.k4k4k.k4k..",
        "k4k..k4.4k..k4k.",
        "kk...kk.kk...kk.",
        "................",
        "................",
        "................",
    ],
}
MON_KINDS = ["rat", "serpent", "tribal", "wisp", "automaton", "wraith", "golem", "hound", "horror"]
MON_FAMILIES = ["jungle", "vermin", "clockwork", "ruins", "outrider", "abyssal", "spirit", "flame", "boss"]
FAMILY = dict(CS.FAMILY)
FAMILY["boss"] = {"1": (112, 16, 40), "2": (192, 40, 64), "3": (248, 112, 120), "4": (248, 208, 72)}
MON_CELL = 24


def monster_cell(kind, family, frame):
    grid = CS.MONSTERS.get(kind) or EXTRA_MONSTERS[kind]
    grid = [(r + "................")[:16] for r in grid]
    pal = {"k": (24, 16, 40), "e": (255, 64, 64), "w": (255, 255, 255)}
    pal.update(FAMILY[family])
    spr = grid_image(grid, pal)
    cell = Image.new("RGBA", (MON_CELL, MON_CELL), (0, 0, 0, 0))
    dx = -2 if frame == 2 else 0
    dy = -1 if frame == 1 else 0
    cell.alpha_composite(spr, (4 + dx, 7 + dy))
    return cell


# ---- flattening the 16-bit art for the kinds Classic never drew ---------------------
def flatten(img, size, colours=6, outline=False):
    """Halve and posterise: the 16-bit art pressed into a few flat colours."""
    small = img.resize((size, size) if isinstance(size, int) else size, Image.BOX)
    alpha = small.getchannel("A")
    q = small.convert("RGB").quantize(colors=colours, method=Image.Quantize.MEDIANCUT).convert("RGB")
    out = q.convert("RGBA")
    px = out.load()
    apx = alpha.load()
    for y in range(out.height):
        for x in range(out.width):
            if apx[x, y] < 110:
                px[x, y] = (0, 0, 0, 0)
    if outline:
        src = out.copy()
        sp = src.load()
        for y in range(out.height):
            for x in range(out.width):
                if sp[x, y][3] == 0:
                    for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        xx, yy = x + ddx, y + ddy
                        if 0 <= xx < out.width and 0 <= yy < out.height and sp[xx, yy][3]:
                            px[x, y] = (24, 16, 40, 255)
                            break
    return out


def _hd_cell(sheet, col, row, cw, ch):
    return sheet.crop((col * cw, row * ch, col * cw + cw, row * ch + ch))


def tiles_sheet():
    sys.path.insert(0, os.path.dirname(HERE))
    import gen_art as GA
    hd = Image.open(os.path.join(HD, "tiles.png")).convert("RGBA")
    kinds = GA.TILE_KINDS
    out = Image.new("RGBA", (len(kinds) * T, 6 * T), (0, 0, 0, 0))
    for r, b in enumerate(GA.BIOMES):
        for c, k in enumerate(kinds):
            img = None
            n = k[-1]
            if k.startswith("floor"):
                img = CT.floor(b, int(n))
            elif k.startswith("grown"):
                img = CT.floor(b, 5 + int(n), True)
            elif k.startswith("face"):
                img = CT.wall_face(b, int(n) % 2, int(n) == 1)
            elif k.startswith("top"):
                img = CT.wall_top(b, int(n))
            elif k.startswith("water"):
                img = CT.water(int(n))
            elif k.startswith("lava"):
                img = CT.lava(int(n))
            elif k.startswith("thicket"):
                img = CT.thicket(b, int(n))
            elif k == "flowers":
                img = CT.flowers(b, 1)
            elif k == "stairs_down":
                img = CT.stairs_down(b)
            elif k == "stairs_up":
                img = CT.stairs_up(b)
            elif k.startswith("grass"):
                img = CT.floor(b, 8 + int(n), True)
            if img is None:
                img = flatten(_hd_cell(hd, c, r, 32, 32), T, 7)
            out.alpha_composite(img, (c * T, r * T))
    signs = dict(GA.TOWN_DOORS)
    for c, k in enumerate(GA.TOWN_KINDS):
        img = None
        if k.startswith("cobble"):
            img = CT.cobble(int(k[-1]))
        elif k == "roof_red":
            img = CT.roof()
        elif k == "roof_teal":
            img = CT.roof(col=((48, 112, 104), (72, 152, 136), (128, 200, 176)))
        elif k == "roof_slate":
            img = CT.roof(col=((64, 68, 96), (96, 104, 136), (144, 152, 184)))
        elif k == "house_wall":
            img = CT.house_wall()
        elif k.startswith("door_"):
            img = CT.door(signs[k[5:]])
        elif k == "fountain":
            img = CT.fountain()
        elif k == "flowers":
            img = CT.flowers("jungle", 3)
        elif k == "grass":
            img = CT.floor("jungle", 9, True)
        elif k == "temple":
            img = CT.stairs_down("abyss")
        if img is None:
            img = flatten(_hd_cell(hd, c, 5, 32, 32), T, 7)
        out.alpha_composite(img, (c * T, 5 * T))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    # heroes: same rows/cols as the 16-bit sheet
    cells = [hero_cell(l, f, fr) for l in range(len(LOOKS)) for f in FACINGS for fr in FRAMES]
    sheet = Image.new("RGBA", (len(FRAMES) * CELL_W, len(cells) // len(FRAMES) * CELL_H), (0, 0, 0, 0))
    for i, c in enumerate(cells):
        sheet.alpha_composite(c, ((i % len(FRAMES)) * CELL_W, (i // len(FRAMES)) * CELL_H))
    sheet.save(os.path.join(OUT, "heroes.png"))
    exprs = ["neutral", "happy", "angry", "hurt"]
    ps = Image.new("RGBA", (4 * 32, len(LOOKS) * 32), (0, 0, 0, 0))
    for l in range(len(LOOKS)):
        for j, e in enumerate(exprs):
            ps.alpha_composite(portrait_cell(l, e), (j * 32, l * 32))
    ps.save(os.path.join(OUT, "portraits.png"))
    ms = Image.new("RGBA", (len(MON_KINDS) * 3 * MON_CELL, len(MON_FAMILIES) * MON_CELL), (0, 0, 0, 0))
    for r, fam in enumerate(MON_FAMILIES):
        for k, kind in enumerate(MON_KINDS):
            for fr in range(3):
                ms.alpha_composite(monster_cell(kind, fam, fr), ((k * 3 + fr) * MON_CELL, r * MON_CELL))
    ms.save(os.path.join(OUT, "monsters.png"))
    wd = Image.open(os.path.join(HD, "warden.png")).convert("RGBA")
    ws = Image.new("RGBA", (3 * 36, 36), (0, 0, 0, 0))
    for fr in range(3):
        ws.alpha_composite(flatten(_hd_cell(wd, fr, 0, 72, 72), 36, 8, True), (fr * 36, 0))
    ws.save(os.path.join(OUT, "warden.png"))
    pr = Image.open(os.path.join(HD, "props.png")).convert("RGBA")
    n = pr.width // 32
    pp = Image.new("RGBA", (n * T, T), (0, 0, 0, 0))
    for i in range(n):
        pp.alpha_composite(flatten(_hd_cell(pr, i, 0, 32, 32), T, 6, True), (i * T, 0))
    pp.save(os.path.join(OUT, "props.png"))
    tiles_sheet().save(os.path.join(OUT, "tiles.png"))
    torch = Image.open(os.path.join(HD, "torch.png")).convert("RGBA")
    flatten(torch, T, 5).save(os.path.join(OUT, "torch.png"))
    print("classic set written to", os.path.relpath(OUT, GODOT))


if __name__ == "__main__":
    main()
