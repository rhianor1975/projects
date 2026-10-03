"""Modelled heroes: 24x32 field sprites and 48x48 anime portraits.

A hero is a Spec -- hair style and colours, clothes, headgear -- and both the
field sprite and the portrait are built from the same Spec, so the face in
the menu is the face walking around the dungeon.
"""
from dataclasses import dataclass, field

from shade import Canvas, overlay, ramp

SKIN_TONES = {
    "fair": (250, 206, 170),
    "warm": (232, 178, 132),
    "olive": (206, 156, 112),
    "deep": (150, 98, 70),
}

HAIR_COLOURS = {
    "blonde": (246, 210, 96), "red": (214, 66, 52), "navy": (66, 82, 168),
    "silver": (206, 214, 236), "green": (78, 176, 104), "brown": (150, 92, 52),
    "pink": (240, 128, 176), "black": (58, 52, 78), "violet": (148, 92, 208),
    "teal": (56, 168, 176), "white": (238, 236, 244), "orange": (240, 140, 48),
}

CLOTH_COLOURS = {
    "steel": (164, 176, 204), "teal": (44, 150, 156), "forest": (64, 132, 72),
    "violet": (124, 76, 188), "brass": (204, 150, 64), "rust": (176, 84, 52),
    "crimson": (188, 44, 64), "white": (230, 230, 238), "navy": (52, 64, 132),
    "black": (52, 48, 64), "sand": (210, 180, 132), "plum": (136, 52, 112),
    "leather": (128, 84, 52), "gold": (232, 188, 72),
}


FW, FH = 32, 40      # field sprite canvas
OX, OY = 4, 7        # where the 24x32 body sits inside it (room for hats, weapons)


# Skin is hand-picked rather than generated: a generated ramp greys out in the
# mid-tones, and a grey face is the fastest way to make a sprite look dead.
SKIN_RAMPS = {
    "fair":  [(150, 84, 92), (204, 122, 110), (236, 164, 132), (252, 200, 164), (255, 228, 200)],
    "warm":  [(132, 70, 72), (190, 108, 88), (222, 148, 108), (242, 184, 138), (252, 214, 176)],
    "olive": [(108, 60, 60), (160, 96, 76), (196, 134, 96), (222, 168, 124), (240, 200, 158)],
    "deep":  [(70, 36, 44), (108, 62, 56), (142, 90, 70), (176, 120, 90), (206, 156, 120)],
}


_SKIN_CACHE = {}


def SKIN(spec):
    if spec.skin not in _SKIN_CACHE:
        from shade import Ramp
        r = Ramp(SKIN_RAMPS[spec.skin])
        r.base = SKIN_RAMPS[spec.skin][3]
        _SKIN_CACHE[spec.skin] = r
    return _SKIN_CACHE[spec.skin]


@dataclass
class Spec:
    hair: str = "brown"
    hair_style: str = "spiky"     # spiky, long, tail, bob, wild
    skin: str = "fair"
    cloth: str = "teal"
    trim: str = "brass"
    legs: str = "navy"
    gloves: str = "leather"
    scarf: str = ""
    eyes: tuple = (64, 112, 216)
    cape: str = ""                # colour name, or "" for none
    pauldrons: bool = False
    hat: str = ""                 # "", "wizard", "goggles", "helm", "band", "tophat", "hood"
    hat_col: str = "violet"
    female: bool = False
    extras: dict = field(default_factory=dict)


def _r(name, table=CLOTH_COLOURS, **kw):
    return ramp(table[name], **kw)


def _eye_pal(spec):
    er = ramp(spec.eyes, n=4)
    return {"k": (36, 24, 48), "K": (72, 48, 64), "E": er[0], "e": er[1], "i": er[2],
            "w": (255, 255, 255), "W": (220, 228, 248), "m": (168, 72, 80), "M": (120, 48, 64),
            "b": (240, 150, 140), "n": (214, 140, 112), "t": (250, 250, 255)}


# ---- field sprite -----------------------------------------------------------
# A field sprite is the Spec in a Pose. Facing is down/up/left (right is left
# mirrored, as on the SNES); `walk` is the 4-step cycle; `attack` is the
# 3-frame swing (wind-up, strike, follow-through) or -1 for none.

WEAPON_FOR_ARCHETYPE = ["sword", "dagger", "bow", "staff", "hammer", "axe", "rapier"]


@dataclass
class Pose:
    facing: str = "down"
    walk: int = 0
    attack: int = -1
    cast: bool = False
    hurt: bool = False


def field_sprite(spec, pose=None, frame=None):
    if pose is None:
        pose = Pose(walk=frame or 0)
    if pose.facing == "right":
        from PIL import ImageOps
        p2 = Pose(facing="left", walk=pose.walk, attack=pose.attack, cast=pose.cast, hurt=pose.hurt)
        return ImageOps.mirror(field_sprite(spec, p2))
    c = Canvas(FW, FH, ox=OX, oy=OY, cel=True)
    m = _Mats(spec)
    if pose.facing == "left":
        _side(c, spec, pose, m)
    else:
        _front_back(c, spec, pose, m, back=(pose.facing == "up"))
    img = c.render()
    if pose.facing == "down" and not pose.hurt:
        _front_face(img, spec, pose)
    elif pose.facing == "left":
        _side_face(img, spec, pose)
    if pose.attack == 1:
        _slash(img, pose)
    return img


class _Mats:
    def __init__(self, spec):
        self.skin = SKIN(spec)
        self.hair = ramp(HAIR_COLOURS[spec.hair])
        self.cloth = _r(spec.cloth)
        self.sleeve = _r(spec.cloth)        # its own ramp, so the arm is inked off the body
        self.trim = _r(spec.trim)
        self.legs = _r(spec.legs)
        self.boots = _r("leather")
        self.gloves = _r(spec.gloves) if spec.gloves else self.skin
        self.scarf = _r(spec.scarf) if spec.scarf else None
        self.cape = _r(spec.cape) if spec.cape else None
        self.steel = ramp((196, 204, 224), lo=0.35, hi=1.15)
        self.wood = ramp((150, 96, 56))


def _bob(pose):
    if pose.attack >= 0:
        return [0.4, 0.8, 0.6][pose.attack]
    return [0.0, -0.7, 0.0, -0.7][pose.walk % 4]


def _weapon(c, spec, m, hx, hy, ang_deg, length=None):
    """A weapon in the hand at (hx, hy), pointing along ang_deg (0 = right,
    90 = down). Drawn as modelled parts so it shades with everything else."""
    import math
    kind = spec.extras.get("weapon", "sword")
    a = math.radians(ang_deg)
    dx, dy = math.cos(a), math.sin(a)
    if kind in ("sword", "dagger", "rapier", "axe"):
        L = length or {"sword": 11, "dagger": 6, "rapier": 12, "axe": 9}[kind]
        w = {"sword": 1.4, "dagger": 1.2, "rapier": 0.8, "axe": 0.9}[kind]
        c.limb(hx - dx * 1.5, hy - dy * 1.5, hx + dx * 1.2, hy + dy * 1.2, 0.8, 0.8, m.wood)
        c.limb(hx + dx * 1.2 - dy * 2.0, hy + dy * 1.2 + dx * 2.0,
               hx + dx * 1.2 + dy * 2.0, hy + dy * 1.2 - dx * 2.0, 0.7, 0.7, _r("gold"), gloss=0.8)
        shaft = m.wood if kind == "axe" else m.steel
        c.limb(hx + dx * 1.4, hy + dy * 1.4, hx + dx * L, hy + dy * L, w, 0.35, shaft, gloss=1.2)
        if kind == "axe":
            c.ellipsoid(hx + dx * (L - 1.5) - dy * 1.6, hy + dy * (L - 1.5) + dx * 1.6, 2.4, 2.4, m.steel, gloss=1.0)
    elif kind == "staff":
        L = length or 14
        c.limb(hx - dx * 4, hy - dy * 4, hx + dx * L, hy + dy * L, 0.8, 0.7, m.wood)
        c.ellipsoid(hx + dx * (L + 1), hy + dy * (L + 1), 2.0, 2.0, ramp((120, 200, 255)), gloss=1.5, flat=False)
    elif kind == "hammer":
        L = length or 9
        c.limb(hx - dx * 1.5, hy - dy * 1.5, hx + dx * L, hy + dy * L, 0.7, 0.7, m.wood)
        c.limb(hx + dx * L - dy * 2.6, hy + dy * L + dx * 2.6,
               hx + dx * L + dy * 2.6, hy + dy * L - dx * 2.6, 2.0, 2.0, _r("brass"), gloss=0.9)
    elif kind == "bow":
        # held vertical across the hand, string toward the body
        for k in range(-4, 5):
            t0, t1 = k / 4.5, (k + 1) / 4.5
            bx0 = hx + dx * (1.6 - 1.4 * t0 * t0) - dy * 5.5 * t0
            by0 = hy + dy * (1.6 - 1.4 * t0 * t0) + dx * 5.5 * t0
            bx1 = hx + dx * (1.6 - 1.4 * t1 * t1) - dy * 5.5 * t1
            by1 = hy + dy * (1.6 - 1.4 * t1 * t1) + dx * 5.5 * t1
            if k < 4:
                c.limb(bx0, by0, bx1, by1, 0.75, 0.75, m.wood)


def _front_back(c, spec, pose, m, back):
    b = _bob(pose)
    w = pose.walk % 4
    liftL = 1.3 if (w == 1 and pose.attack < 0) else 0.0
    liftR = 1.3 if (w == 3 and pose.attack < 0) else 0.0
    hair_long = spec.hair_style in ("long", "bob")
    drop = 21 if spec.hair_style == "long" else 15.5

    # hand positions: hanging, swinging, casting or attacking
    lh = [5.6, 21.4 + b]
    rh = [18.4, 21.4 + b]
    if pose.attack < 0 and not pose.cast:
        lh[1] += -0.8 if w == 3 else (0.4 if w == 1 else 0)
        rh[1] += -0.8 if w == 1 else (0.4 if w == 3 else 0)
    if pose.cast:
        lh, rh = [6.0, 9.5 + b], [18.0, 9.5 + b]
    weapon_ang = None
    if pose.attack >= 0:
        if back:
            rh, weapon_ang = [[18.5, 8.0], [15.0, 6.0], [13.5, 10.0]][pose.attack], [-70, -95, -110][pose.attack]
        else:
            rh, weapon_ang = [[19.5, 9.0], [15.0, 21.0], [8.5, 23.5]][pose.attack], [-60, 100, 160][pose.attack]

    if not back:
        if hair_long:
            _hair_drape(c, m.hair, b, drop, behind=True)
        c.ellipsoid(12, 8.4 + b, 7.6, 7.2, m.hair)
        if m.cape:
            c.poly([(7.0, 16 + b), (17.0, 16 + b), (19.2, 28.5), (4.8, 28.5)], m.cape, round_=2.0)
        if back is False and weapon_ang is not None and pose.attack == 0:
            _weapon(c, spec, m, rh[0], rh[1], weapon_ang)   # raised behind the head

    # legs and boots
    c.limb(10.0, 22.5 + b, 9.8, 28.6 - liftL, 1.9, 1.7, m.legs)
    c.limb(14.0, 22.5 + b, 14.2, 28.6 - liftR, 1.9, 1.7, m.legs)
    c.limb(9.8, 26.8 - liftL, 9.7, 29.0 - liftL, 2.2, 2.4, m.boots)
    c.limb(14.2, 26.8 - liftR, 14.3, 29.0 - liftR, 2.2, 2.4, m.boots)
    c.ellipsoid(9.5, 30.0 - liftL, 2.8, 1.5, m.boots)
    c.ellipsoid(14.5, 30.0 - liftR, 2.8, 1.5, m.boots)

    # torso: a tapered coat with a skirt and a belt
    c.poly([(6.8, 15.6 + b), (17.2, 15.6 + b), (16.0, 22.6 + b), (8.0, 22.6 + b)], m.cloth, round_=2.0)
    c.poly([(8.4, 21.2 + b), (15.6, 21.2 + b), (16.8, 25.2 + b), (7.2, 25.2 + b)], m.cloth, round_=1.5)
    c.poly([(8.4, 20.6 + b), (15.6, 20.6 + b), (15.6, 21.9 + b), (8.4, 21.9 + b)], m.trim, round_=0.6, bias=0.05)
    if not back:
        c.poly([(11.5, 17.0 + b), (12.5, 17.0 + b), (12.5, 20.4 + b), (11.5, 20.4 + b)], m.trim, round_=0.5)
    if m.scarf and not back:
        c.ellipsoid(12, 16.2 + b, 4.6, 2.0, m.scarf)
        c.limb(13.0, 16.6 + b, 14.2, 20.5 + b, 1.3, 1.0, m.scarf)
    if spec.pauldrons:
        c.ellipsoid(7.6, 16.6 + b, 2.4, 1.9, m.steel, gloss=0.9)
        c.ellipsoid(16.4, 16.6 + b, 2.4, 1.9, m.steel, gloss=0.9)

    # arms
    c.limb(6.8, 16.4 + b, lh[0], lh[1] - 1.0, 1.4, 1.3, m.sleeve)
    c.limb(17.2, 16.4 + b, rh[0], rh[1] - 1.0, 1.4, 1.3, m.sleeve)
    c.ellipsoid(lh[0], lh[1], 1.6, 1.6, m.gloves)
    c.ellipsoid(rh[0], rh[1], 1.6, 1.6, m.gloves)

    # head
    c.limb(12, 14.0 + b, 12, 16.2 + b, 1.4, 1.6, m.skin)
    c.ellipsoid(12, 9.2 + b, 6.6, 6.3, m.skin, bulge=0.35)
    if back:
        c.ellipsoid(12, 8.6 + b, 7.4, 7.0, m.hair, gloss=0.5)
        if hair_long:
            _hair_drape(c, m.hair, b, drop, behind=False)
        elif spec.hair_style in ("spiky", "wild"):
            for (x0, y0, x1, y1, r) in ((9, 6, 3.4, 1.5, 2.8), (15, 6, 20.6, 1.5, 2.8),
                                        (12, 5, 12.0, -2.0, 3.0), (8, 9, 4.0, 13.5, 2.6),
                                        (16, 9, 20.0, 13.5, 2.6), (10, 10, 8.5, 15.0, 2.4),
                                        (14, 10, 15.5, 15.0, 2.4)):
                c.limb(x0, y0 + b, x1, y1 + b, r, 0.7, m.hair, gloss=0.7)
        elif spec.hair_style == "tail":
            c.limb(12, 6 + b, 12.5, 17.5 + b, 2.6, 1.0, m.hair, gloss=0.7)
        if m.cape:
            c.poly([(7.0, 16 + b), (17.0, 16 + b), (19.2, 28.5), (4.8, 28.5)], m.cape, round_=2.0)
    else:
        _bangs(c, spec, m.hair, b)
    if spec.hat:
        _field_hat(c, spec, b, back)
    if weapon_ang is not None and not (pose.attack == 0 and not back):
        _weapon(c, spec, m, rh[0], rh[1], weapon_ang)


def _hair_drape(c, hair, b, drop, behind):
    """Long hair as strands, not a slab: tapered locks fanning down the back."""
    locks = ((7.0, 5.0, 2.8), (9.5, 7.8, 2.8), (12.0, 12.0, 2.9), (14.5, 16.2, 2.8), (17.0, 19.0, 2.8))
    for (x0, x1, r) in locks:
        c.limb(x0, 6.5 + b, x1 + (0.0 if behind else 0.0), drop + (0.6 if abs(x0 - 12) < 3 else 0), r, 1.2,
               hair, gloss=0.6)


def _bangs(c, spec, hair, b):
    st = spec.hair_style
    if st == "spiky":
        for (x0, y0, x1, y1, r) in ((9, 5.0, 3.2, 1.0, 2.8), (15, 5.0, 20.8, 1.0, 2.8),
                                    (12, 4.5, 11.5, -2.2, 3.0), (7, 7.0, 3.6, 12.0, 2.3),
                                    (17, 7.0, 20.4, 12.0, 2.3),
                                    (10.5, 4.0, 7.2, 9.2, 2.6), (12, 4.0, 12.0, 8.8, 2.2),
                                    (13.5, 4.0, 16.8, 9.2, 2.6)):
            c.limb(x0, y0 + b, x1, y1 + b, r, 0.7, hair, gloss=0.8)
    elif st in ("long", "bob"):
        for (x0, y0, x1, y1, r) in ((12, 3.0, 7.6, 8.8, 2.6), (12, 3.0, 10.8, 9.0, 2.2),
                                    (12, 3.0, 13.2, 9.0, 2.2), (12, 3.0, 16.4, 8.8, 2.6)):
            c.limb(x0, y0 + b, x1, y1 + b, r, 0.6, hair, gloss=0.9)
        dropf = 19.5 if st == "long" else 14
        c.limb(6.0, 7 + b, 5.4, dropf, 2.2, 1.2, hair, gloss=0.5)
        c.limb(18.0, 7 + b, 18.6, dropf, 2.2, 1.2, hair, gloss=0.5)
    elif st == "tail":
        c.limb(12, 2.5 + b, 12.6, 0.0 + b, 2.2, 1.6, hair, gloss=0.6)
        for (x0, y0, x1, y1, r) in ((12, 3.5, 7.4, 8.8, 2.4), (12, 3.5, 11.6, 8.6, 1.9),
                                    (12, 3.5, 16.6, 8.8, 2.4)):
            c.limb(x0, y0 + b, x1, y1 + b, r, 0.6, hair, gloss=0.8)
        c.limb(6.2, 6.5 + b, 6.0, 12.5 + b, 1.8, 0.8, hair)
        c.limb(17.8, 6.5 + b, 18.0, 12.5 + b, 1.8, 0.8, hair)
    elif st == "wild":
        import math
        for i in range(7):
            ang = -2.75 + i * 0.9
            c.limb(12, 6.5 + b, 12 + math.cos(ang) * 10.5, 7.5 + b + math.sin(ang) * 8.5, 2.8, 0.6, hair, gloss=0.7)
        for (x1, y1) in ((7.6, 9.0), (12, 8.8), (16.4, 9.0)):
            c.limb(12, 4.5 + b, x1, y1 + b, 2.0, 0.6, hair, gloss=0.7)


def _field_hat(c, spec, b=0.0, back=False):
    col = _r(spec.hat_col)
    if spec.hat == "wizard":
        c.ellipsoid(12, 4.6 + b, 9.4, 2.0, col)
        c.limb(12, 4.0 + b, 15.5, -5.0 + b, 4.6, 0.8, col, gloss=0.3)
        c.poly([(7.8, 3.4 + b), (16.2, 3.4 + b), (15.9, 4.8 + b), (8.1, 4.8 + b)], _r("gold"), round_=0.6)
    elif spec.hat == "helm":
        c.ellipsoid(12, 6.2 + b, 7.4, 5.4, ramp((196, 204, 224), lo=0.35, hi=1.15), gloss=1.0)
        c.poly([(5.0, 6.6 + b), (19.0, 6.6 + b), (19.0, 8.0 + b), (5.0, 8.0 + b)], _r("gold"), round_=0.6)
        c.limb(12, 1.0 + b, 12, -3.0 + b, 1.2, 0.6, _r("crimson"))
    elif spec.hat == "goggles":
        c.poly([(5.4, 4.8 + b), (18.6, 4.8 + b), (18.6, 6.2 + b), (5.4, 6.2 + b)], _r("leather"), round_=0.6)
        if not back:
            for gx in (9.2, 14.8):
                c.ellipsoid(gx, 5.2 + b, 2.3, 1.9, _r("brass"), gloss=0.8)
                c.ellipsoid(gx, 5.2 + b, 1.3, 1.1, ramp((120, 220, 255)), gloss=1.2)
    elif spec.hat == "band":
        c.poly([(5.6, 4.8 + b), (18.4, 4.8 + b), (18.4, 6.2 + b), (5.6, 6.2 + b)], col, round_=0.6)
        c.limb(18.4, 5.6 + b, 21.5, 9.6 + b, 0.9, 0.5, col)
    elif spec.hat == "tophat":
        c.ellipsoid(12, 3.6 + b, 8.6, 1.8, col)
        c.poly([(8.2, -4.0 + b), (15.8, -4.0 + b), (15.6, 3.6 + b), (8.4, 3.6 + b)], col, round_=1.4)
        c.poly([(8.3, 1.6 + b), (15.7, 1.6 + b), (15.7, 2.8 + b), (8.3, 2.8 + b)], _r("crimson"), round_=0.5)
    elif spec.hat == "hood":
        # a hood is a cowl over the hair: it frames the face rather than hiding it
        c.limb(12, 4.5 + b, 12, 2.0 + b, 6.4, 5.0, col)
        c.limb(5.6, 7 + b, 6.4, 14 + b, 2.0, 1.4, col)
        c.limb(18.4, 7 + b, 17.6, 14 + b, 2.0, 1.4, col)


def _side(c, spec, pose, m):
    """Facing left. Far limbs first, then body, head, near limbs."""
    b = _bob(pose)
    w = pose.walk % 4
    stride = [0.0, 2.6, 0.0, -2.6][w] if pose.attack < 0 else [1.0, 2.4, 2.0][pose.attack]
    hair_long = spec.hair_style in ("long", "bob")
    drop = 21 if spec.hair_style == "long" else 15.5
    near_hand = [12.0 + stride * 0.8, 21.6 + b]
    far_hand = [12.0 - stride * 0.8, 21.2 + b]
    weapon_ang = None
    if pose.cast:
        near_hand, far_hand = [6.5, 11.0 + b], [8.0, 10.0 + b]
    if pose.attack >= 0:
        near_hand, weapon_ang = [[15.5, 9.0], [5.0, 17.5], [6.5, 23.0]][pose.attack], [-120, 180, 140][pose.attack]

    # back hair and far limbs
    if hair_long:
        for (x0, x1, r) in ((14.5, 17.5, 3.0), (16.5, 19.0, 2.6), (12.5, 15.0, 2.6)):
            c.limb(x0, 7 + b, x1, drop, r, 1.3, m.hair, gloss=0.5)
    if m.cape:
        c.poly([(11.0, 16 + b), (16.0, 16 + b), (20.0, 28.0), (13.0, 28.5)], m.cape, round_=2.0)
    c.limb(12.0, 22.5 + b, 12.0 + stride, 28.6, 1.8, 1.6, m.legs, bias=-0.12)
    c.limb(12.0 + stride, 26.8, 12.0 + stride, 29.0, 2.2, 2.3, m.boots, bias=-0.12)
    c.ellipsoid(11.2 + stride, 30.0, 2.9, 1.5, m.boots, bias=-0.12)
    c.limb(12.0, 16.8 + b, far_hand[0], far_hand[1] - 1, 1.4, 1.3, m.cloth, bias=-0.15)
    c.ellipsoid(far_hand[0], far_hand[1], 1.5, 1.5, m.gloves, bias=-0.15)

    # body
    c.poly([(9.4, 15.8 + b), (14.6, 15.8 + b), (14.4, 22.6 + b), (9.6, 22.6 + b)], m.cloth, round_=2.0)
    c.poly([(9.4, 21.2 + b), (14.6, 21.2 + b), (15.6, 25.2 + b), (8.6, 25.2 + b)], m.cloth, round_=1.5)
    c.poly([(9.4, 20.6 + b), (14.6, 20.6 + b), (14.6, 21.9 + b), (9.4, 21.9 + b)], m.trim, round_=0.6)
    c.limb(12.0, 22.5 + b, 12.0 - stride, 28.6, 1.9, 1.7, m.legs)
    c.limb(12.0 - stride, 26.8, 12.0 - stride, 29.0, 2.2, 2.3, m.boots)
    c.ellipsoid(11.0 - stride, 30.0, 2.9, 1.5, m.boots)

    # head, turned left
    c.limb(12.4, 14.0 + b, 12.4, 16.2 + b, 1.4, 1.6, m.skin)
    c.ellipsoid(13.6, 8.6 + b, 6.6, 6.8, m.hair)
    c.ellipsoid(11.0, 9.4 + b, 5.6, 6.0, m.skin, bulge=0.4)
    c.ellipsoid(5.8, 11.0 + b, 0.9, 0.9, m.skin)            # the nose's little bump
    _side_bangs(c, spec, m.hair, b)
    if spec.hat:
        _field_hat(c, spec, b, False)
    if spec.pauldrons:
        c.ellipsoid(12.2, 16.6 + b, 2.6, 2.0, m.steel, gloss=0.9)

    # near arm, and the weapon in it
    c.limb(12.2, 16.8 + b, near_hand[0], near_hand[1] - 1, 1.5, 1.4, m.sleeve)
    c.ellipsoid(near_hand[0], near_hand[1], 1.6, 1.6, m.gloves)
    if weapon_ang is not None:
        _weapon(c, spec, m, near_hand[0], near_hand[1], weapon_ang)


def _side_bangs(c, spec, hair, b):
    st = spec.hair_style
    if st == "spiky":
        for (x0, y0, x1, y1, r) in ((14, 5, 22.5, 2.0, 3.0), (13, 4.5, 14.0, -2.5, 3.0),
                                    (15, 7, 21.5, 11.5, 2.6), (11, 4.5, 4.0, 3.5, 2.8),
                                    (10, 4.8, 5.0, 7.6, 2.4), (16, 9, 18.5, 14.5, 2.4)):
            c.limb(x0, y0 + b, x1, y1 + b, r, 0.7, hair, gloss=0.8)
    elif st in ("long", "bob"):
        c.limb(12, 3.0 + b, 6.0, 8.6 + b, 2.6, 0.7, hair, gloss=0.9)
        c.limb(12, 3.0 + b, 9.0, 9.4 + b, 2.2, 0.7, hair, gloss=0.9)
        c.limb(13.5, 5 + b, 14.0, (19.0 if st == "long" else 14.0), 2.6, 1.4, hair, gloss=0.5)
    elif st == "tail":
        c.limb(15.5, 4.5 + b, 21.0, 9.0 + b, 2.0, 1.0, hair, gloss=0.6)
        c.limb(12, 3.5 + b, 6.0, 8.0 + b, 2.4, 0.7, hair, gloss=0.8)
        c.limb(12, 3.5 + b, 9.2, 9.0 + b, 2.0, 0.7, hair, gloss=0.8)
    elif st == "wild":
        import math
        for i in range(6):
            ang = -2.9 + i * 0.75
            c.limb(13, 6.5 + b, 13 + math.cos(ang) * 10, 7.5 + b + math.sin(ang) * 8, 2.8, 0.6, hair, gloss=0.7)


def _front_face(img, spec, pose):
    ep = _eye_pal(spec)
    b = int(round(_bob(pose)))
    y = OY + 9 + b
    if pose.attack == 1:          # eyes narrowed in effort
        overlay(img, OX + 7, y + 1, ["kkk", ".ie"], ep)
        overlay(img, OX + 14, y + 1, ["kkk", "ei."], ep)
    else:
        overlay(img, OX + 7, y, ["kkk", "wEk", "Eei", ".i."], ep)
        overlay(img, OX + 14, y, ["kkk", "kEw", "ieE", ".i."], ep)
    overlay(img, OX + 6, y + 4, ["b"], ep)
    overlay(img, OX + 17, y + 4, ["b"], ep)
    overlay(img, OX + 11, y + 5, ["mm"] if pose.attack != 1 else ["MM"], ep)


def _side_face(img, spec, pose):
    ep = _eye_pal(spec)
    b = int(round(_bob(pose)))
    y = OY + 9 + b
    overlay(img, OX + 6, y, ["kk", "wE", "Ei"] if pose.attack != 1 else ["kk", "ie"], ep)
    overlay(img, OX + 9, y + 4, ["b"], ep)
    overlay(img, OX + 6, y + 5, ["m"], ep)


def _slash(img, pose):
    """The white arc of a swing on the strike frame -- the FF6 'swoosh'."""
    import math
    px = img.load()
    if pose.facing in ("down", "up"):
        cx, cy, r, a0, a1 = OX + 12, OY + 14, 13, -40, 130
    else:
        cx, cy, r, a0, a1 = OX + 11, OY + 15, 12, 110, 250
    for k in range(40):
        a = math.radians(a0 + (a1 - a0) * k / 39)
        for dr, col in ((0, (255, 255, 255, 255)), (-1, (200, 232, 255, 220)), (1, (160, 200, 255, 140))):
            x = int(round(cx + math.cos(a) * (r + dr)))
            y = int(round(cy + math.sin(a) * (r + dr)))
            if 0 <= x < img.width and 0 <= y < img.height and (k % 7 or dr == 0):
                if px[x, y][3] == 0 or dr == 0:
                    px[x, y] = col


# ---- portraits ----------------------------------------------------------------
# 48x48, head and shoulders, for menus and dialogue. Modelled like the field
# sprite and finished by hand where a face needs it: the eyes are painted.

_PORTRAIT_EYE_L = [
    "kkkkkkk.",
    "kKwwEEEk",
    ".kwEEEEk",
    ".kEEeeEk",
    ".kEeiieK",
    "..keiiK.",
    "...KKK..",
]
_PORTRAIT_EYE_SHUT = [
    "........",
    "........",
    "kkkkkkk.",
    ".KKKKKk.",
    "........",
]


def portrait(spec, expression="neutral"):
    import math
    c = Canvas(48, 48)
    m = _Mats(spec)
    hair = m.hair
    long_ = spec.hair_style in ("long", "bob")
    # back hair
    c.ellipsoid(24, 19, 17.5, 16.5, hair, gloss=0.4)
    if long_:
        drop = 52 if spec.hair_style == "long" else 36
        for (x0, x1, r) in ((9, 6, 5.5), (13, 10, 5.0), (35, 38, 5.0), (39, 42, 5.5)):
            c.limb(x0, 16, x1, drop, r, 2.5, hair, gloss=0.5)
    if m.cape:
        c.poly([(2, 48), (46, 48), (42, 36), (6, 36)], m.cape, round_=3)
    # shoulders and collar
    c.poly([(5, 49), (43, 49), (39, 38), (9, 38)], m.cloth, round_=4)
    if spec.pauldrons:
        c.ellipsoid(9, 42, 6, 5, m.steel, gloss=1.0)
        c.ellipsoid(39, 42, 6, 5, m.steel, gloss=1.0)
    c.limb(24, 30, 24, 41, 4.2, 4.6, m.skin)
    c.poly([(17, 37), (31, 37), (28, 44), (24, 46), (20, 44)], m.trim, round_=2)
    c.poly([(19.5, 37), (28.5, 37), (24, 42.5)], m.skin, round_=2)
    # ears and face
    c.ellipsoid(12.4, 25, 2.4, 3.6, m.skin)
    c.ellipsoid(35.6, 25, 2.4, 3.6, m.skin)
    c.poly([(12.5, 13), (35.5, 13), (36.2, 23), (34, 31), (29, 36.5), (24, 38.6),
            (19, 36.5), (14, 31), (11.8, 23)], m.skin, round_=5.5)
    # front hair
    st = spec.hair_style
    if st == "spiky":
        for (x0, y0, x1, y1, r) in ((18, 9, 4, 2, 6.0), (30, 9, 44, 2, 6.0), (24, 8, 22, -4, 6.0),
                                    (14, 13, 6, 28, 4.8), (34, 13, 42, 28, 4.8),
                                    (20, 7, 14, 22, 4.8), (24, 7, 23, 21, 4.4), (28, 7, 33, 22, 4.8),
                                    (17, 8, 11, 17, 4.0), (31, 8, 37, 17, 4.0)):
            c.limb(x0, y0, x1, y1, r, 0.9, hair, gloss=0.9)
    elif long_:
        for (x0, y0, x1, y1, r) in ((24, 4, 14, 20, 5.2), (24, 4, 20, 21.5, 4.6), (24, 4, 27, 21.5, 4.6),
                                    (24, 4, 34, 20, 5.2)):
            c.limb(x0, y0, x1, y1, r, 0.9, hair, gloss=1.0)
        c.limb(13, 14, 11, 38, 4.2, 2.0, hair, gloss=0.6)
        c.limb(35, 14, 37, 38, 4.2, 2.0, hair, gloss=0.6)
    elif st == "tail":
        c.limb(24, 4, 31, 1.5, 4.5, 3.0, hair, gloss=0.6)
        for (x0, y0, x1, y1, r) in ((24, 5, 14, 20, 5.0), (24, 5, 22, 20, 4.0), (24, 5, 33, 20, 5.0)):
            c.limb(x0, y0, x1, y1, r, 0.9, hair, gloss=0.9)
        c.limb(13, 14, 12, 30, 3.8, 1.2, hair)
        c.limb(35, 14, 36, 30, 3.8, 1.2, hair)
    elif st == "wild":
        for i in range(9):
            ang = -2.9 + i * 0.6
            if math.sin(ang) > 0.35 and abs(math.cos(ang)) < 0.6:
                continue
            c.limb(24, 14, 24 + math.cos(ang) * 22, 15 + math.sin(ang) * 17, 6.0, 1.0, hair, gloss=0.8)
        for x1 in (14, 24, 34):
            c.limb(24, 8, x1, 19, 4.0, 0.8, hair, gloss=0.8)
    if spec.hat:
        _portrait_hat(c, spec)
    img = c.render()

    ep = _eye_pal(spec)
    ep["H"] = hair[1]
    shut = expression in ("happy", "hurt")
    eye = _PORTRAIT_EYE_SHUT if shut else _PORTRAIT_EYE_L
    if expression == "happy":
        eye = ["........", "..kkkk..", ".k....k.", "k......k"]
    overlay(img, 14, 22, eye, ep)
    overlay(img, 26, 22, [r[::-1] for r in eye], ep)
    brow = {"neutral": ["..HHHH", "HH...."], "angry": ["HH....", "..HHHH"],
            "happy": [".HHHH.", "H....H"], "hurt": ["....HH", "HHHH.."]}.get(expression, ["..HHHH", "HH...."])
    overlay(img, 15, 19, brow, ep)
    overlay(img, 27, 19, [r[::-1] for r in brow], ep)
    overlay(img, 24, 29, ["n", "n"], ep)
    mouth = {"neutral": [".MMM."], "happy": ["M...M", ".MMM.", "..m.."], "angry": ["MMMMM"],
             "hurt": [".MMM.", "M...M"]}.get(expression, [".MMM."])
    overlay(img, 22, 33, mouth, ep)
    overlay(img, 14, 29, ["bbb"], ep)
    overlay(img, 31, 29, ["bbb"], ep)
    return img


def _portrait_hat(c, spec):
    col = _r(spec.hat_col)
    if spec.hat == "wizard":
        c.ellipsoid(24, 10, 21, 4.0, col)
        c.limb(24, 9, 33, 0.5, 9.5, 1.5, col, gloss=0.3)
        c.poly([(13, 7), (35, 7), (35, 10.5), (13, 10.5)], _r("gold"), round_=1.2)
    elif spec.hat == "helm":
        st = ramp((196, 204, 224), lo=0.35, hi=1.15)
        c.ellipsoid(24, 14, 17, 12, st, gloss=1.2)
        c.poly([(7, 15), (41, 15), (41, 19), (7, 19)], _r("gold"), round_=1.0)
        c.limb(24, 3, 24, 0.5, 2.6, 1.4, _r("crimson"))
    elif spec.hat == "goggles":
        c.poly([(9, 10), (39, 10), (39, 13.5), (9, 13.5)], _r("leather"), round_=1.0)
        for gx in (18, 30):
            c.ellipsoid(gx, 11, 5.2, 4.2, _r("brass"), gloss=1.0)
            c.ellipsoid(gx, 11, 3.2, 2.6, ramp((120, 220, 255)), gloss=1.4)
    elif spec.hat == "band":
        c.poly([(9, 11), (39, 11), (39, 14.5), (9, 14.5)], col, round_=1.2)
        c.limb(39, 13, 46, 24, 2.0, 1.2, col)
    elif spec.hat == "tophat":
        c.ellipsoid(24, 8, 19, 3.6, col)
        c.poly([(15.5, 0), (32.5, 0), (32.6, 8), (15.4, 8)], col, round_=3)
        c.poly([(15.3, 3), (32.7, 3), (32.7, 6), (15.3, 6)], _r("crimson"), round_=1)
    elif spec.hat == "hood":
        c.ellipsoid(24, 18, 19, 17, col)
