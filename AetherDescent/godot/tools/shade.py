"""A tiny shading renderer for SNES-style sprites.

SNES sprite art reads as SNES rather than NES because of three things: many
shades per material (five or six, hue-shifted -- cool shadows, warm lights),
coloured "selective" outlines instead of black, and soft contour shadows
where one part overlaps another. Drawing that by hand in character grids is
slow and inconsistent, so sprites here are *modelled* instead: each body part
is a primitive with a surface normal (an ellipsoid, a tapered limb, a
cushioned polygon), lit from the top left, rendered at 4x and resolved down
to the final pixel grid against a per-material colour ramp.

Hand-painted detail that should not be modelled -- eyes, mouths, buckles --
goes on top as small explicit-colour grids.
"""
import colorsys

import numpy as np
from PIL import Image

LIGHT = np.array([-0.5, -0.6, 1.0])
LIGHT = LIGHT / np.linalg.norm(LIGHT)

BAYER2 = np.array([[0.0, 0.5], [0.75, 0.25]]) - 0.375


class Ramp(list):
    """A colour ramp that remembers the colour it was built from."""
    base = None


def ramp(base, n=6, hue_shift=0.06, lo=0.30, hi=1.18, sat_lo=1.15, sat_hi=0.70):
    """A hue-shifted colour ramp from deep shadow to highlight.

    Shadows drift toward blue/purple and gain saturation, lights drift toward
    yellow and lose it -- the way SNES artists painted, and the single biggest
    reason a sprite stops looking flat."""
    r, g, b = [c / 255.0 for c in base]
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    out = []
    for i in range(n):
        t = i / (n - 1)
        hh = (h + hue_shift * (0.5 - t) * (1 if h < 0.5 or h > 0.85 else -1)) % 1.0
        # nudge toward blue in shadow and yellow in light regardless of hue
        vv = min(1.0, v * (lo + (hi - lo) * t))
        ss = min(1.0, max(0.0, s * (sat_lo + (sat_hi - sat_lo) * t)))
        rr, gg, bb = colorsys.hsv_to_rgb(hh, ss, vv)
        # cool the darks, warm the lights
        rr = rr * (0.92 + 0.12 * t)
        bb = bb * (1.12 - 0.20 * t)
        out.append(tuple(int(max(0, min(255, round(c * 255)))) for c in (rr, gg, bb)))
    out = Ramp(out)
    out.base = tuple(base)
    return out


class Canvas:
    def __init__(self, w, h, ss=4, ambient=0.30, ox=0.0, oy=0.0, cel=False):
        self.w, self.h, self.ss = w, h, ss
        self.cel = cel
        self.groups = []
        self.ox, self.oy = ox, oy
        H, W = h * ss, w * ss
        self.part = np.full((H, W), -1, dtype=np.int32)
        self.inten = np.zeros((H, W))
        ys, xs = np.mgrid[0:H, 0:W]
        self.X = (xs + 0.5) / ss - ox
        self.Y = (ys + 0.5) / ss - oy
        self.ramps = []        # per part
        self.flat = []         # per part: no outline/contour logic
        self.ambient = ambient

    # -- primitives -----------------------------------------------------------
    def _put(self, mask, nx, ny, nz, rmp, gloss=0.0, flat=False, shade_mul=1.0, bias=0.0, group=None):
        n = np.stack([nx, ny, nz], axis=-1)
        n = n / np.maximum(1e-6, np.linalg.norm(n, axis=-1, keepdims=True))
        d = np.clip(n @ LIGHT, 0, 1)
        i = self.ambient + (1 - self.ambient) * d * shade_mul + bias
        if gloss:
            # a tight specular band: the anime "angel ring" on hair, the glint on metal
            refl = 2 * (n @ LIGHT)[..., None] * n - LIGHT
            spec = np.clip(refl[..., 2], 0, 1) ** 18
            i = i + gloss * spec
        pid = len(self.ramps)
        self.ramps.append(rmp)
        self.flat.append(flat)
        self.groups.append(group if group is not None else id(rmp))
        self.part[mask] = pid
        self.inten[mask] = np.clip(i[mask], 0, 1.3)
        return pid

    def ellipsoid(self, cx, cy, rx, ry, rmp, bulge=1.0, **kw):
        dx = (self.X - cx) / rx
        dy = (self.Y - cy) / ry
        r2 = dx * dx + dy * dy
        mask = r2 <= 1.0
        nz = np.sqrt(np.clip(1 - r2, 0, 1))
        return self._put(mask, dx * bulge, dy * bulge, nz, rmp, **kw)

    def limb(self, x0, y0, x1, y1, r0, r1, rmp, **kw):
        """A tapered capsule -- arms, legs, hair locks, tails."""
        px, py = self.X - x0, self.Y - y0
        dx, dy = x1 - x0, y1 - y0
        L2 = dx * dx + dy * dy or 1e-6
        t = np.clip((px * dx + py * dy) / L2, 0, 1)
        cx, cy = x0 + t * dx, y0 + t * dy
        ox, oy = self.X - cx, self.Y - cy
        r = r0 + (r1 - r0) * t
        dist = np.sqrt(ox * ox + oy * oy)
        mask = dist <= r
        nx, ny = ox / np.maximum(r, 1e-6), oy / np.maximum(r, 1e-6)
        nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
        return self._put(mask, nx, ny, nz, rmp, **kw)

    def poly(self, pts, rmp, round_=2.0, **kw):
        """A cushioned polygon: flat in the middle, rolling off at the edges --
        cloth, plates, walls of a sprite that are not round."""
        X, Y = self.X, self.Y
        inside = np.zeros(X.shape, dtype=bool)
        dist = np.full(X.shape, 1e9)
        n = len(pts)
        for k in range(n):
            (ax, ay), (bx, by) = pts[k], pts[(k + 1) % n]
            cond = ((ay > Y) != (by > Y)) & (X < (bx - ax) * (Y - ay) / ((by - ay) or 1e-9) + ax)
            inside ^= cond
            ex, ey = bx - ax, by - ay
            L2 = ex * ex + ey * ey or 1e-9
            t = np.clip(((X - ax) * ex + (Y - ay) * ey) / L2, 0, 1)
            qx, qy = ax + t * ex - X, ay + t * ey - Y
            dist = np.minimum(dist, np.sqrt(qx * qx + qy * qy))
        s = np.clip(dist / round_, 0, 1)
        hgt = np.sqrt(np.clip(1 - (1 - s) ** 2, 0, 1))
        gy, gx = np.gradient(hgt * round_ / self.ss)
        return self._put(inside, -gx * self.ss, -gy * self.ss, np.ones_like(X) * 0.6, rmp, **kw)

    # -- resolve --------------------------------------------------------------
    def render(self, outline=True, contour=True, dither=False):
        ss, w, h = self.ss, self.w, self.h
        part = self.part.reshape(h, ss, w, ss).transpose(0, 2, 1, 3).reshape(h, w, ss * ss)
        inten = self.inten.reshape(h, ss, w, ss).transpose(0, 2, 1, 3).reshape(h, w, ss * ss)
        fpart = np.full((h, w), -1, dtype=np.int32)
        fidx = np.zeros((h, w), dtype=np.int32)
        for y in range(h):
            for x in range(w):
                ps = part[y, x]
                cov = np.count_nonzero(ps >= 0)
                if cov < ss * ss * 0.45:
                    continue
                vals, counts = np.unique(ps[ps >= 0], return_counts=True)
                p = int(vals[np.argmax(counts)])
                # the frontmost part wins a tie-ish split, so edges stay crisp
                if counts.max() < ss * ss * 0.6 and vals.max() != p:
                    front = int(vals.max())
                    if counts[list(vals).index(front)] >= ss * ss * 0.35:
                        p = front
                iv = inten[y, x][ps == p].mean()
                n = len(self.ramps[p])
                if self.cel:
                    fpart[y, x] = p
                    fidx[y, x] = 0 if iv < CEL_T[0] else 1 if iv < CEL_T[1] else 2 if iv < CEL_T[2] else 3
                    continue
                f = iv * (n - 1)
                if dither:
                    f += BAYER2[y % 2, x % 2] * 0.9
                fpart[y, x] = p
                fidx[y, x] = int(np.clip(round(f), 0, n - 1))
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        px = img.load()
        for y in range(h):
            for x in range(w):
                p = fpart[y, x]
                if p < 0:
                    continue
                rmp = self.ramps[p]
                idx = fidx[y, x]
                if self.cel:
                    rmp = cel_tones(rmp)
                    edge = behind = False
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        xx, yy = x + dx, y + dy
                        q = fpart[yy, xx] if 0 <= xx < w and 0 <= yy < h else -1
                        if q < 0:
                            edge = True
                        elif q > p and self.groups[q] != self.groups[p] and not self.flat[q]:
                            behind = True
                    if not self.flat[p] and ((outline and edge) or (contour and behind)):
                        px[x, y] = ink(self.ramps[p]) + (255,)
                    else:
                        px[x, y] = rmp[idx] + (255,)
                    continue
                if not self.flat[p]:
                    edge = False
                    behind = False
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        xx, yy = x + dx, y + dy
                        q = fpart[yy, xx] if 0 <= xx < w and 0 <= yy < h else -1
                        if q < 0:
                            edge = True
                        elif q > p and self.ramps[q] is not rmp and not self.flat[q]:
                            behind = True
                    if outline and edge:
                        # selective outline: the material's own darkest tone,
                        # pushed darker on the shadow side (right and below)
                        dark_side = (x + 1 >= w or fpart[y, x + 1] < 0) or (y + 1 >= h or fpart[y + 1, x] < 0)
                        c = rmp[0]
                        if dark_side:
                            c = tuple(int(v * 0.62) for v in c)
                        px[x, y] = c + (255,)
                        continue
                    if contour and behind:
                        idx = max(0, min(idx, 2) - 1)
                px[x, y] = rmp[idx] + (255,)
        return img


# Cel shading: a base tone owns most of a surface, with one shadow, one light
# and a pinch of highlight -- the clean clusters of a 16-bit sprite.
CEL_T = (0.46, 0.80, 0.96)


_CEL_CACHE = {}


def cel_tones(rmp):
    """Shadow, base, light, highlight -- built around the material's own colour,
    so the base tone *is* the colour asked for, saturated and bright."""
    base = getattr(rmp, "base", None)
    if base is None:
        n = len(rmp)
        return [rmp[max(0, int(n * 0.22))], rmp[int(n * 0.55)], rmp[min(n - 1, int(n * 0.82))], rmp[-1]]
    if base in _CEL_CACHE:
        return _CEL_CACHE[base]
    r, g, b = [c / 255.0 for c in base]
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    s = min(1.0, s * 1.12)

    def mk(hh, ss, vv):
        rr, gg, bb = colorsys.hsv_to_rgb(hh % 1.0, min(1, max(0, ss)), min(1, max(0, vv)))
        return (int(rr * 255), int(gg * 255), int(bb * 255))
    # shadows lean toward violet, lights toward warm yellow
    toward_violet = 0.035 if (h < 0.16 or h > 0.75) else -0.035
    out = [mk(h - toward_violet, s * 1.18 + 0.08, v * 0.66),
           mk(h, s, v),
           mk(h + toward_violet * 0.6, s * 0.82, v * 1.13 + 0.04),
           mk(h + toward_violet, s * 0.45, v * 1.25 + 0.12)]
    _CEL_CACHE[base] = out
    return out


def ink(rmp):
    """The outline: near-black, tinted by the material it surrounds."""
    d = rmp[0]
    return tuple(int(d[i] * 0.30 + (18, 12, 30)[i] * 0.70) for i in range(3))


def overlay(img, x, y, rows, pal):
    """Paint explicit-colour detail (eyes, mouths) on a rendered sprite."""
    px = img.load()
    for yy, r in enumerate(rows):
        for xx, ch in enumerate(r):
            if ch in ". ":
                continue
            c = pal[ch]
            X, Y = x + xx, y + yy
            if 0 <= X < img.width and 0 <= Y < img.height:
                px[X, Y] = (c + (255,)) if len(c) == 3 else c
