"""Hand-drawn sprite grids: chibi field heroes (16x24), anime portraits
(32x32, drawn as left halves and mirrored), monsters (16x16) and tiles."""
from pixelart import mirror

# ---- field heroes: 16x24, FF6-proportioned (big head, short body) ----------
# Left halves, mirrored. Hair silhouettes differ per style; the body is shared.

_FACE = [
    "kHhssHss",   # 8  bangs over the brow
    "kHssssss",   # 9
    "khskksss",   # 10 lashes
    "khsewsss",   # 11 iris + highlight
    "kHsEesss",   # 12
    ".kSrssss",   # 13 blush
    "..kSsssS",   # 14 mouth
    "...kkkss",   # 15 neck
]

_BODY = [
    "..kcccLc",   # 16 collar
    ".kscacCc",   # 17 hands at sides, sash
    ".kscaacc",   # 18
    ".kskCaaa",   # 19 belt
    "..kCcccc",   # 20
    "..kCCcCk",   # 21 legs
    "..kbbBk.",   # 22 boots
    "..kkkk..",   # 23
]

HERO_HAIR = {
    # short spiky hair -- the vanguard / skirmisher look
    "spiky": [
        "....k..k",
        "...khkkh",
        "..khhhhh",
        ".khhhlhh",
        "kkhhlhhh",
        "khhhhhhh",
        "khhhhhhh",
        "khHhhHhh",
    ],
    # long hair falling past the shoulders -- caster / envoy
    "long": [
        ".....kkk",
        "...kkhhh",
        "..khhhhh",
        ".khhhlhh",
        ".khhlhhh",
        "khhhhhhh",
        "khhhhhhh",
        "khHhhHhh",
    ],
    # tied-back ponytail look: a tuft above, close at the sides
    "tail": [
        "......kk",
        "....kkhh",
        "...khhhh",
        "..khhlhh",
        ".khhlhhh",
        ".khhhhhh",
        "khhhhhhh",
        "khHhhHhh",
    ],
}


def hero_rows(style):
    top = HERO_HAIR[style]
    face = list(_FACE)
    body = list(_BODY)
    if style == "long":
        # hair framing the face and spilling over the shoulders
        face = ["kHhssHss", "kHhsssss", "khhkksss", "khhewsss",
                "kHhEesss", "khkrssss", "khhSsssS", "khhkkkss"]
        body = ["khhcccLc", ".khcacCc", ".kscaacc", ".kskCaaa",
                "..kCcccc", "..kCCcCk", "..kbbBk.", "..kkkk.."]
    return mirror(top + face + body)


# ---- portraits: 32x32 anime faces, left halves -----------------------------
PORTRAIT_SPIKY = [
    "........k.....k.",
    ".......khk...khk",
    "...k..khhhk.khhh",
    "..khk.khhhhkhhhh",
    "..khhkhhhlhhhhlh",
    "...khhhhllhhhllh",
    "..kkhhhhhhhhhhhh",
    ".khhhhhhhhhhhhhh",
    "khhhhhhhhhhhhhhh",
    ".khhhhhhHhhhhhhh",
    "khhhhhhHhhhhHhhh",
    "khhhhhHhhhhHhhHh",
    ".khhhHhshhhHshHh",
    ".khhHhssHHHHssHs",
    "..khHsssssssssss",
    ".khhHssskkkkkkss",
    "khhhHssskwwEEkss",
    "khhhHssskwEEEkss",
    ".khhHssskEeeEkss",
    ".khhHssskEeeEkss",
    "..khHsssskEEksss",
    "..khHssssskksssS",
    "..khhSrrrsssssss",
    "...khSssssssssss",
    "...khkSssssskSss",
    "....kkSssssssskk",
    ".....kkSssssssss",
    "......kkSsssssss",
    "........kkSSssss",
    "..........kkSSSS",
    "......kkcckkSSSS",
    "...kkccccLcckkkk",
]

PORTRAIT_LONG = [
    "..........kkkkkk",
    "........kkhhhhhh",
    "......kkhhhhhhhh",
    ".....khhhhhllhhh",
    "....khhhhhlllhhh",
    "...khhhhhhhhhhhh",
    "...khhhhhhhhhhhh",
    "..khhhhhhhhhhhhh",
    "..khhhhhhhHhhhhh",
    ".khhhhhhhHhhhHhh",
    ".khhhhhhHhhhHhhh",
    ".khhhhhHhssHhHss",
    "khhhhhHsssssHsss",
    "khhhhHssHHHHssss",
    "khhhhHssssssssss",
    "khhhhHsskkkkkkss",
    "khhhhHsskwwEEkss",
    "khhhhHsskwEEEkss",
    "khhhhHsskEeeEkss",
    "khhhhHsskEeeEkss",
    "khhhhHssskEEksss",
    "khhhhHsssskksssS",
    "khhhhhSrrrssssss",
    "khhhhhHSssssssss",
    "khhhhhhkSsssskss",
    "khhhhhhkkSsssskk",
    "kHhhhhhhkkSsssss",
    "kHhhhhhhhkkSssss",
    ".kHhhhhhhhhkkSSs",
    ".kHhhhhhhhhhkkSS",
    "..kHhhhkkcckkSSS",
    "..kkkkkcccLccckk",
]

# ---- monsters: 16x16 --------------------------------------------------------
# k outline, 1 dark, 2 mid, 3 light, 4 accent, e eye, w white
MONSTERS = {
    "rat": [
        "................",
        "................",
        "................",
        "......kk...kk...",
        ".....k23k.k23k..",
        ".....k22kk22k...",
        "....kk22222kk...",
        "...k2222222222k.",
        "..k2e22222222w2k",
        "..k3222222222222k"[:16],
        "...k33222222212k",
        "....kk33322212k.",
        "....k1kk1kkk1k..",
        "...4kkkkkkkkk...",
        "..44............",
        "................",
    ],
    "serpent": [
        "................",
        "........kkkk....",
        ".......k2222k...",
        "......k2e22e2k..",
        "......k2222222k.",
        ".......k3k4k3k..",
        "........kk.4k...",
        "....kkkk22k.....",
        "...k22221k......",
        "..k2kkkk1k......",
        "..k2k..k12kkkk..",
        "..k22kkk222222k.",
        "...k332222kk21k.",
        "....kk3333kk12k.",
        "......kkkkkk1k..",
        "............kk..",
    ],
    "tribal": [
        ".....4..4.......",
        "....k4kk4k......",
        "...k222222k.....",
        "...k2e22e2k.4...",
        "...k222222k.k...",
        "....k1111k..k...",
        "...k223322k.k...",
        "..k2k2332k2kk...",
        "..k2k3443k2k....",
        "..k2k2332kk.....",
        "..kk.1221k......",
        "....k1kk1k......",
        "....k2kk2k......",
        "....k1kk1k......",
        "...kk1kk1kk.....",
        "................",
    ],
    "wisp": [
        "................",
        ".......k........",
        "......k3k...k...",
        "...k..k33k.k3k..",
        "..k3k.k333k33k..",
        "..k33k33333333k.",
        "...k3333w333w3k.",
        "...k3332w332w3k.",
        "..k33333333333k.",
        "..k32333333323k.",
        "...k322222223k..",
        "....k2222222k...",
        ".....kk222kk....",
        ".......k1k......",
        "........k.......",
        "................",
    ],
    "automaton": [
        "......kkkk......",
        ".....k4444k.....",
        "....kkkkkkkk....",
        "....k2e22e2k....",
        "....k222222k....",
        "...kkk1111kkk...",
        "..k22k3333k22k..",
        "..k21k2442k12k..",
        "..k21k2442k12k..",
        "..kk.k3333k.kk..",
        "..k3.kkkkkk.3k..",
        "..kk.k2kk2k.kk..",
        ".....k2kk2k.....",
        ".....k1kk1k.....",
        "....kk1kk1kk....",
        "................",
    ],
    "wraith": [
        "......kkkk......",
        ".....k3333k.....",
        "....k333333k....",
        "....k3kk3kk3....",
        "....k3we3ew3k...",
        "....k333333k....",
        "...k23333332k...",
        "..k2k333333k2k..",
        "..k2k223322k2k..",
        "...kk222222kk...",
        "....k222222k....",
        "....k122221k....",
        "...k1212121k....",
        "...k1k1k1k1k....",
        "....k.k.k.k.....",
        "................",
    ],
}

# Monster colour ramps per family: 1 dark, 2 mid, 3 light, 4 accent
FAMILY = {
    "vermin":    {"1": (72, 64, 48), "2": (136, 112, 80), "3": (200, 176, 136), "4": (232, 96, 112)},
    "jungle":    {"1": (40, 96, 48), "2": (88, 160, 72), "3": (176, 216, 104), "4": (232, 72, 64)},
    "clockwork": {"1": (112, 72, 32), "2": (184, 136, 64), "3": (240, 200, 112), "4": (96, 224, 255)},
    "ruins":     {"1": (40, 96, 128), "2": (88, 168, 200), "3": (184, 232, 248), "4": (248, 248, 160)},
    "outrider":  {"1": (120, 104, 88), "2": (192, 176, 152), "3": (240, 232, 216), "4": (200, 64, 48)},
    "abyssal":   {"1": (64, 24, 88), "2": (136, 56, 160), "3": (208, 128, 232), "4": (255, 96, 64)},
    "spirit":    {"1": (72, 104, 200), "2": (128, 176, 248), "3": (216, 240, 255), "4": (255, 255, 255)},
    "flame":     {"1": (176, 56, 24), "2": (240, 136, 40), "3": (255, 232, 120), "4": (255, 255, 255)},
}


def monster_palette(family):
    p = {"k": (24, 16, 40), "e": (255, 64, 64), "w": (255, 255, 255)}
    p.update(FAMILY[family])
    return p
