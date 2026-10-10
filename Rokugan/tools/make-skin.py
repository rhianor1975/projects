#!/usr/bin/env python3
"""Paint the client's surfaces: frames, textures, card backs, coins, mon.

    tools/make-skin.py        -> client/assets/*.png

Everything here is drawn by this script -- no card art, nothing of AEG's
-- so the output is committed and a clone has the look without running
anything.  The painted portraits and backgrounds come from the card art
at runtime instead, which stays local.

The kanji are rendered here, into images, so the client needs no CJK
font: a machine without one drew every 運 and 天照 as an empty box.
"""
import math
import os
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "client", "assets")
CJK = ["/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
       "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
       "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",
       "/System/Library/Fonts/Hiragino Sans GB.ttc",
       "C:/Windows/Fonts/msgothic.ttc"]
rng = np.random.default_rng(1997)
random.seed(1997)


def font(paths, size):
    for p in paths:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    raise SystemExit("no font found among %s" % paths)


def save(img, name):
    img.save(os.path.join(OUT, name + ".png"), optimize=True)


def noise(w, h, scale, octaves=4):
    """Tileable value noise, 0..1."""
    out = np.zeros((h, w))
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = max(2, int(scale * 2 ** o))
        grid = rng.random((n, n))
        ys = np.linspace(0, n, h, endpoint=False)
        xs = np.linspace(0, n, w, endpoint=False)
        y0 = ys.astype(int) % n
        x0 = xs.astype(int) % n
        fy = (ys - ys.astype(int))[:, None]
        fx = (xs - xs.astype(int))[None, :]
        fy = fy * fy * (3 - 2 * fy)
        fx = fx * fx * (3 - 2 * fx)
        a = grid[y0][:, x0]
        b = grid[y0][:, (x0 + 1) % n]
        c = grid[(y0 + 1) % n][:, x0]
        d = grid[(y0 + 1) % n][:, (x0 + 1) % n]
        out += amp * ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy)
        tot += amp
        amp *= 0.5
    return out / tot


def colorize(n, dark, light):
    dark, light = np.array(dark, float), np.array(light, float)
    rgb = dark[None, None, :] + (light - dark)[None, None, :] * n[:, :, None]
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))


# ---------------------------------------------------------------- textures
def wood():
    w, h = 512, 512
    n = noise(w, h, 3)
    y = np.arange(h)[:, None]
    x = np.arange(w)[None, :]
    warp = noise(w, h, 2, 3)
    grain = 0.5 + 0.5 * np.sin((y / h * 2 * math.pi * 9) + warp * 14.0 + np.sin(x / w * 2 * math.pi) * 1.5)
    fine = noise(w, h, 64, 2)
    planks = ((y % 128) < 2).astype(float)       # the seams between boards
    v = 0.35 * grain + 0.40 * n + 0.25 * fine - 0.5 * planks
    img = colorize(v, (52, 30, 16), (128, 82, 46))
    save(img, "wood")


def parchment():
    w, h = 256, 256
    n = 0.6 * noise(w, h, 4) + 0.4 * noise(w, h, 24, 2)
    save(colorize(n, (205, 186, 146), (243, 232, 205)), "parchment")


def lacquer_tex():
    w, h = 256, 256
    n = 0.7 * noise(w, h, 3) + 0.3 * noise(w, h, 30, 2)
    save(colorize(n, (14, 10, 8), (40, 28, 20)), "lacquer")


def dusk():
    """The Modern table when no Stronghold art is to hand."""
    w, h = 800, 500
    img = Image.new("RGB", (w, h))
    px = np.zeros((h, w, 3))
    t = np.linspace(0, 1, h)[:, None]
    top, bot = np.array([46, 54, 70]), np.array([22, 17, 14])
    px[:] = (top * (1 - t) + bot * t)[:, :, :] if False else (top[None, None, :] * (1 - t[:, :, None]) + bot[None, None, :] * t[:, :, None])
    img = Image.fromarray(px.astype(np.uint8))
    d = ImageDraw.Draw(img)
    for k, (base, col) in enumerate([(300, (34, 38, 48)), (360, (26, 26, 32)), (420, (20, 18, 20))]):
        pts = [(0, h)]
        for x in range(0, w + 40, 40):
            pts.append((x, base - 60 * math.sin(x / 130.0 + k * 2) - 30 * math.sin(x / 47.0 + k)))
        pts.append((w, h))
        d.polygon(pts, fill=col)
    # a pagoda on the far ridge
    cx, by = 640, 300
    for i in range(5):
        ww = 70 - i * 11
        y = by - i * 26
        d.polygon([(cx - ww, y), (cx + ww, y), (cx + ww * 0.6, y - 10), (cx - ww * 0.6, y - 10)], fill=(18, 16, 18))
        d.rectangle([cx - ww * 0.45, y - 26, cx + ww * 0.45, y - 10], fill=(18, 16, 18))
    for i in range(160):
        x, y = random.randrange(w), random.randrange(h)
        r = random.choice([1, 2, 2, 3])
        d.ellipse([x - r, y - r, x + r, y + r], fill=(214, 140, 160))
    img = img.filter(ImageFilter.GaussianBlur(1.2))
    save(img, "dusk")


# ------------------------------------------------------------- frames 9-patch
def gold_grad(d, box, width, light=(244, 214, 128), dark=(122, 86, 30)):
    x0, y0, x1, y1 = box
    for i in range(width):
        t = i / max(width - 1, 1)
        s = 0.5 + 0.5 * math.cos(t * math.pi * 2)
        c = tuple(int(dark[j] + (light[j] - dark[j]) * s) for j in range(3))
        d.rectangle([x0 + i, y0 + i, x1 - i, y1 - i], outline=c)


def flourish(d, cx, cy, sx, sy, col):
    """A corner ornament: a curl and a diamond, mirrored by sx, sy."""
    for k in range(3):
        r = 9 - k * 3
        d.arc([cx - r, cy - r, cx + r, cy + r], 0, 360, fill=col, width=1)
    d.polygon([(cx + sx * 14, cy), (cx + sx * 18, cy - sy * 3), (cx + sx * 22, cy), (cx + sx * 18, cy + sy * 3)], fill=col)
    d.polygon([(cx, cy + sy * 14), (cx - sx * 3, cy + sy * 18), (cx, cy + sy * 22), (cx + sx * 3, cy + sy * 18)], fill=col)


def frame_classic():
    """The first mockup's card: dark wood rim, a gilded band, a fine inner
    line.  Patch margins 14px: thick enough to read as a gilded frame,
    thin enough that a card's name and stats are not under it -- the
    first, 22px frame clipped the first letter of every title."""
    w, h = 120, 168
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, w - 1, h - 1], 6, fill=(58, 34, 16))
    gold_grad(d, (2, 2, w - 3, h - 3), 7)
    d.rectangle([9, 9, w - 10, h - 10], outline=(96, 66, 30))
    for (x, y) in [(4, 4), (w - 9, 4), (4, h - 9), (w - 9, h - 9)]:
        d.ellipse([x, y, x + 5, y + 5], fill=(255, 236, 170), outline=(110, 74, 24))
    img.paste(Image.new("RGBA", (w - 20, h - 20), (0, 0, 0, 0)), (10, 10))
    save(img, "frame_classic")


def frame_modern():
    """The second mockup's card: near-black rim, a fine double gold line."""
    w, h = 96, 134
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, w - 1, h - 1], 5, fill=(20, 14, 10))
    d.rounded_rectangle([3, 3, w - 4, h - 4], 4, outline=(196, 156, 82), width=2)
    d.rectangle([7, 7, w - 8, h - 8], outline=(120, 90, 46))
    img.paste(Image.new("RGBA", (w - 20, h - 20), (0, 0, 0, 0)), (10, 10))
    save(img, "frame_modern")


def bevel95(name, raised):
    w = h = 16
    img = Image.new("RGB", (w, h), (192, 192, 192))
    d = ImageDraw.Draw(img)
    lt, rb = ((255, 255, 255), (64, 64, 64)) if raised else ((64, 64, 64), (255, 255, 255))
    mid_lt, mid_rb = ((223, 223, 223), (128, 128, 128)) if raised else ((128, 128, 128), (223, 223, 223))
    d.line([(0, 0), (w - 1, 0)], fill=lt)
    d.line([(0, 0), (0, h - 1)], fill=lt)
    d.line([(0, h - 1), (w - 1, h - 1)], fill=rb)
    d.line([(w - 1, 0), (w - 1, h - 1)], fill=rb)
    d.line([(1, 1), (w - 2, 1)], fill=mid_lt)
    d.line([(1, 1), (1, h - 2)], fill=mid_lt)
    d.line([(1, h - 2), (w - 2, h - 2)], fill=mid_rb)
    d.line([(w - 2, 1), (w - 2, h - 2)], fill=mid_rb)
    save(img, name)


def lacquer_panel():
    """Modern panels: lacquer with a gold border and corner studs."""
    w, h = 96, 96
    tex = Image.open(os.path.join(OUT, "lacquer.png")).resize((w, h))
    img = tex.convert("RGBA")
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w - 1, h - 1], outline=(70, 50, 24), width=2)
    d.rectangle([3, 3, w - 4, h - 4], outline=(190, 148, 76), width=2)
    d.rectangle([8, 8, w - 9, h - 9], outline=(110, 82, 40))
    for (x, y) in [(6, 6), (w - 7, 6), (6, h - 7), (w - 7, h - 7)]:
        d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=(214, 172, 90), outline=(90, 60, 20))
    save(img, "panel_modern")


def gold_button(name, lit):
    w, h = 64, 40
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    fill = (74, 54, 24) if lit else (30, 24, 18)
    d.rounded_rectangle([0, 0, w - 1, h - 1], 4, fill=fill, outline=(206, 164, 86), width=2)
    d.rounded_rectangle([4, 4, w - 5, h - 5], 3, outline=(120, 90, 46))
    if lit:
        for y in range(5, h // 2):
            a = int(60 * (1 - y / (h / 2)))
            d.line([(5, y), (w - 6, y)], fill=(255, 226, 150, a))
    save(img, name)


# ---------------------------------------------------------------- card backs
def back(name, field, emblem, w=300, h=420):
    img = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, w - 1, h - 1], 14, fill=(52, 32, 14))
    gold_grad(d, (6, 6, w - 7, h - 7), 12)
    n = noise(w - 44, h - 44, 5)
    tex = colorize(n, tuple(int(c * 0.7) for c in field), field)
    img.paste(tex, (22, 22))
    d.rectangle([22, 22, w - 23, h - 23], outline=(110, 76, 30), width=2)
    d.rectangle([30, 30, w - 31, h - 31], outline=(200, 160, 80), width=1)
    for (cx, cy, sx, sy) in [(36, 36, 1, 1), (w - 37, 36, -1, 1), (36, h - 37, 1, -1), (w - 37, h - 37, -1, -1)]:
        flourish(d, cx, cy, sx, sy, (214, 172, 90))
    emblem(d, img, w / 2, h / 2)
    save(img, name)


def tomoe(d, img, cx, cy, r=78, gold=(222, 182, 96), ground=None):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=gold)
    d.ellipse([cx - r + 8, cy - r + 8, cx + r - 8, cy + r - 8], fill=ground)
    for k in range(3):
        a = k * 2 * math.pi / 3
        hx, hy = cx + math.cos(a) * r * 0.32, cy + math.sin(a) * r * 0.32
        hr = r * 0.26
        d.ellipse([hx - hr, hy - hr, hx + hr, hy + hr], fill=gold)
        pts = []
        for i in range(24):
            t = i / 23
            ang = a + t * 2.3
            rr = r * (0.32 + 0.30 * t)
            wdt = hr * (1 - t)
            pts.append((cx + math.cos(ang) * (rr + wdt), cy + math.sin(ang) * (rr + wdt)))
        for i in range(23, -1, -1):
            t = i / 23
            ang = a + t * 2.3
            rr = r * (0.32 + 0.30 * t)
            pts.append((cx + math.cos(ang) * rr * 0.86, cy + math.sin(ang) * rr * 0.86))
        d.polygon(pts, fill=gold)


def sun(d, img, cx, cy):
    gold, pale = (238, 196, 92), (250, 236, 196)
    r = 70
    for i in range(16):
        a = i * 2 * math.pi / 16
        d.polygon([(cx + math.cos(a - 0.13) * r * 0.95, cy + math.sin(a - 0.13) * r * 0.95),
                   (cx + math.cos(a) * r * 1.62, cy + math.sin(a) * r * 1.62),
                   (cx + math.cos(a + 0.13) * r * 0.95, cy + math.sin(a + 0.13) * r * 0.95)], fill=gold)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=gold, outline=(120, 80, 20), width=3)
    d.ellipse([cx - r + 10, cy - r + 10, cx + r - 10, cy + r - 10], fill=pale)
    f = font(CJK, 44)
    d.text((cx, cy - 22), "天", font=f, fill=(40, 22, 8), anchor="mm")
    d.text((cx, cy + 24), "照", font=f, fill=(40, 22, 8), anchor="mm")


# ----------------------------------------------------------- coins and mon
def coin(name, k):
    s = 128
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([2, 2, s - 3, s - 3], fill=(140, 98, 24))
    d.ellipse([8, 8, s - 9, s - 9], fill=(236, 194, 82))
    d.ellipse([20, 20, s - 21, s - 21], outline=(150, 104, 26), width=4)
    d.text((s / 2, s / 2 + 2), k, font=font(CJK, 60), fill=(64, 36, 8), anchor="mm")
    save(img, name)


MON = {"Crab": "蟹", "Crane": "鶴", "Dragon": "龍", "Lion": "獅", "Phoenix": "鳳",
       "Scorpion": "蠍", "Unicorn": "麒", "Mantis": "蟷", "Spider": "蜘", "Unaligned": "浪"}
COLOR = {"Crab": (60, 84, 108), "Crane": (92, 150, 196), "Dragon": (44, 116, 72), "Lion": (196, 152, 38),
         "Phoenix": (206, 104, 36), "Scorpion": (156, 30, 30), "Unicorn": (108, 62, 156),
         "Mantis": (30, 128, 104), "Spider": (52, 48, 52), "Unaligned": (110, 98, 82)}
RINGS = {"earth": "地", "water": "水", "fire": "火", "air": "風", "void": "空"}


def mon(clan):
    s = 128
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([2, 2, s - 3, s - 3], fill=(214, 170, 80))
    d.ellipse([9, 9, s - 10, s - 10], fill=tuple(int(c * 0.75) for c in COLOR[clan]))
    d.ellipse([15, 15, s - 16, s - 16], outline=(214, 170, 80), width=2)
    d.text((s / 2, s / 2 + 3), MON[clan], font=font(CJK, 64), fill=(250, 240, 220), anchor="mm")
    save(img, "mon_" + clan)


def ring(name, k, lit):
    s = 96
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rim = (206, 160, 80) if lit else (84, 72, 62)
    d.ellipse([2, 2, s - 3, s - 3], fill=rim)
    d.ellipse([8, 8, s - 9, s - 9], fill=(36, 22, 14) if lit else (22, 18, 16))
    d.text((s / 2, s / 2 + 2), k, font=font(CJK, 46), fill=(255, 226, 150) if lit else (110, 98, 86), anchor="mm")
    save(img, name)


def glyph(name, k, size, col, bg=None):
    f = font(CJK, size)
    box = f.getbbox(k)
    w, h = box[2] - box[0] + 8, box[3] - box[1] + 8
    img = Image.new("RGBA", (w, h), bg or (0, 0, 0, 0))
    ImageDraw.Draw(img).text((w / 2, h / 2), k, font=f, fill=col, anchor="mm")
    save(img, name)


def banner():
    """五輪の書 down a dark strip, for the Modern right column."""
    w, h = 90, 330
    img = Image.new("RGBA", (w, h), (16, 11, 8, 255))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w - 1, h - 1], outline=(150, 112, 50), width=2)
    f = font(CJK, 58)
    for i, k in enumerate("五輪の書"):
        d.text((w / 2, 44 + i * 78), k, font=f, fill=(226, 180, 92), anchor="mm")
    save(img, "banner")


def hand_mark():
    s = 40
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([1, 1, s - 2, s - 2], fill=(150, 26, 20), outline=(240, 210, 150), width=2)
    d.text((s / 2, s / 2 + 1), "手", font=font(CJK, 24), fill=(255, 245, 230), anchor="mm")
    save(img, "hand")


def main():
    os.makedirs(OUT, exist_ok=True)
    wood()
    parchment()
    lacquer_tex()
    dusk()
    frame_classic()
    frame_modern()
    bevel95("bevel_up", True)
    bevel95("bevel_down", False)
    lacquer_panel()
    gold_button("gold_button", False)
    gold_button("gold_button_lit", True)
    # the Classic backs are square, like the Classic cards
    back("back_fate", (24, 46, 104), lambda d, i, x, y: tomoe(d, i, x, y, ground=(24, 46, 104)), 340, 392)
    back("back_dynasty", (80, 46, 20), lambda d, i, x, y: tomoe(d, i, x, y, ground=(80, 46, 20)), 340, 392)
    back("back_province", (110, 74, 32), sun, 340, 392)
    back("back_modern", (70, 14, 12), lambda d, i, x, y: tomoe(d, i, x, y, r=70, gold=(196, 150, 70), ground=(70, 14, 12)))
    coin("coin_fate", "運")
    coin("coin_gold", "金")
    for c in MON:
        mon(c)
    for k, g in RINGS.items():
        ring("ring_" + k, g, True)
        ring("ring_" + k + "_off", g, False)
    banner()
    hand_mark()
    print("painted", len(os.listdir(OUT)), "files into", OUT)


if __name__ == "__main__":
    main()
