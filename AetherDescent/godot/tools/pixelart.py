"""Shared pixel-art helpers: palettes, sprite rendering from character grids,
and the SNES-style window frame. Used by gen_art.py and mockups.py."""
from PIL import Image

# ---- palettes --------------------------------------------------------------
OUTLINE = (24, 16, 40)

SKIN = {"s": (252, 208, 168), "S": (220, 156, 124), "r": (240, 128, 128)}
EYES = {"e": (40, 48, 104), "w": (255, 255, 255), "E": (88, 120, 200)}

HAIR = {
    "blonde": ((248, 224, 104), (208, 152, 48), (255, 248, 192)),
    "red":    ((224, 72, 64), (152, 32, 48), (255, 152, 128)),
    "navy":   ((72, 80, 152), (36, 36, 88), (136, 152, 216)),
    "silver": ((216, 216, 240), (144, 144, 192), (255, 255, 255)),
    "green":  ((96, 192, 112), (40, 120, 72), (176, 232, 168)),
    "brown":  ((168, 104, 56), (104, 56, 32), (216, 160, 104)),
    "pink":   ((248, 144, 192), (192, 72, 128), (255, 208, 232)),
    "black":  ((64, 56, 80), (32, 24, 40), (112, 104, 144)),
}
CLOTH = {
    "steel":  ((176, 184, 208), (104, 112, 144), (232, 236, 248)),
    "teal":   ((48, 160, 160), (24, 96, 104), (128, 216, 208)),
    "forest": ((72, 136, 72), (40, 80, 48), (144, 200, 112)),
    "violet": ((128, 80, 192), (72, 40, 120), (184, 152, 240)),
    "brass":  ((200, 144, 64), (128, 80, 32), (248, 208, 120)),
    "rust":   ((176, 88, 56), (112, 48, 40), (232, 152, 104)),
    "crimson": ((192, 40, 64), (112, 16, 40), (248, 112, 120)),
    "white":  ((232, 232, 240), (168, 168, 192), (255, 255, 255)),
}
LEATHER = {"b": (112, 72, 48), "B": (64, 40, 32)}
GOLD = {"g": (248, 208, 72), "G": (184, 128, 32)}


def palette(hair="brown", cloth="teal", accent="crimson", extra=None):
    """Map grid characters to colours for a hero/portrait sprite.

    k outline   s/S skin   r blush   e/w/E eye
    h/H/l hair (mid/dark/light)   c/C/L cloth   a/A accent   b/B leather
    g/G gold    m/M metal
    """
    hm, hd, hl = HAIR[hair]
    cm, cd, cl = CLOTH[cloth]
    am, ad, _ = CLOTH[accent]
    p = {"k": OUTLINE, "h": hm, "H": hd, "l": hl, "c": cm, "C": cd, "L": cl,
         "a": am, "A": ad, "m": (200, 208, 224), "M": (120, 128, 152)}
    p.update(SKIN)
    p.update(EYES)
    p.update(LEATHER)
    p.update(GOLD)
    if extra:
        p.update(extra)
    return p


def grid_image(rows, pal):
    """Render a list of equal-length strings; '.' is transparent."""
    w = len(rows[0])
    for i, r in enumerate(rows):
        if len(r) != w:
            raise ValueError("row %d is %d wide, expected %d: %r" % (i, len(r), w, r))
    img = Image.new("RGBA", (w, len(rows)), (0, 0, 0, 0))
    px = img.load()
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch in ". ":
                continue
            if ch not in pal:
                raise KeyError("no colour for %r (row %d)" % (ch, y))
            c = pal[ch]
            px[x, y] = c + (255,) if len(c) == 3 else c
    return img


def mirror(rows):
    """Build symmetric rows from their left halves."""
    return [r + r[::-1] for r in rows]


# ---- the window frame ------------------------------------------------------
WIN_TOP = (64, 80, 200)
WIN_BOT = (16, 16, 96)
WIN_EDGE_LIGHT = (232, 232, 248)
WIN_EDGE_MID = (168, 168, 200)
WIN_EDGE_DARK = (40, 40, 72)


def draw_window(img, x, y, w, h, top=WIN_TOP, bottom=WIN_BOT):
    """The classic blue gradient box with a bevelled silver frame."""
    px = img.load()
    for yy in range(h):
        t = yy / max(1, h - 1)
        col = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
        for xx in range(w):
            px[x + xx, y + yy] = col
    # frame: dark outer, light bevel, mid inner
    for xx in range(w):
        for (yy, c) in ((0, WIN_EDGE_DARK), (1, WIN_EDGE_LIGHT), (2, WIN_EDGE_MID),
                        (h - 1, WIN_EDGE_DARK), (h - 2, WIN_EDGE_MID), (h - 3, WIN_EDGE_DARK)):
            if 0 < xx < w - 1 or yy in (0, h - 1):
                px[x + xx, y + yy] = c + (255,)
    for yy in range(h):
        for (xx, c) in ((0, WIN_EDGE_DARK), (1, WIN_EDGE_LIGHT), (2, WIN_EDGE_MID),
                        (w - 1, WIN_EDGE_DARK), (w - 2, WIN_EDGE_MID), (w - 3, WIN_EDGE_DARK)):
            if 0 < yy < h - 1 or xx in (0, w - 1):
                px[x + xx, y + yy] = c + (255,)
    # round the corners off
    for (cx, cy) in ((x, y), (x + w - 1, y), (x, y + h - 1), (x + w - 1, y + h - 1)):
        px[cx, cy] = (0, 0, 0, 0)


def paste(dst, src, x, y):
    dst.alpha_composite(src, (int(x), int(y)))


def shadow(w=12, h=4):
    """The little ellipse every field sprite stands on."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for yy in range(h):
        for xx in range(w):
            dx = (xx + 0.5 - w / 2) / (w / 2)
            dy = (yy + 0.5 - h / 2) / (h / 2)
            if dx * dx + dy * dy <= 1.0:
                px[xx, yy] = (0, 0, 0, 90)
    return img
