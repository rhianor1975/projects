"""Shared helpers for the art tools: the SNES window frame, compositing,
drop shadows and the bitmap-font text renderer."""
from PIL import Image

from font5x7 import GLYPHS

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


# ---- text ------------------------------------------------------------------
WHITE = (248, 248, 248)
TEXT_SHADOW = (16, 16, 48)


def text_width(s):
    return sum(len(GLYPHS.get(ch, GLYPHS['?'])[0]) + 1 for ch in s) - (1 if s else 0)


def draw_text(img, x, y, s, col=WHITE, shadow_col=TEXT_SHADOW, scale=1, outline=None):
    """Proportional 5x7 text. `col` may be a list for a vertical gradient."""
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
            c = col[min(len(col) - 1, gy * len(col) // 7)] if isinstance(col, list) else col
            dot(cx + gx, gy, c)
        cx += len(g[0]) + 1
    return cx * scale
