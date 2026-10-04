#!/usr/bin/env python3
"""Work out one accent colour per deck, far enough apart to tell apart.

    python3 tools/mkpalette.py            print the palette
    python3 tools/mkpalette.py --swatch   also write /tmp/palette.png

Every deck is its own pile on the table, so a deck has to be identifiable
from its back and its edge.  The palette this replaces had ten pairs
closer than the threshold and two -- Goldwyn against Throne, Aldemar
against Court -- that were the same colour to within a rounding error.

The hard part is that they also have to look like one game.  A muted
ochre-and-verdigris register means low chroma, and low chroma is exactly
what makes hues hard to separate, so lightness does most of the work here
and hue does the rest.  Search is a plain local one: it only has to beat
a person guessing, and it does.
"""
import argparse
import colorsys
import itertools
import math
import random
import os
_FD = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'fonts')

DECKS = [
    # the Houses first: these are the ones a player learns
    'Ravenmark', 'Vipren', 'Goldwyn', 'Aldemar',
    'Leoward', 'Stonegarth', 'Wulfren', 'Everhold',
    # the three shared decks the board used to choose for you
    'War', 'Political', 'Intrigue',
    # everything else, including the five the manifest declares and the
    # card section has not written yet
    'Promise', 'Lever', 'Ambition', 'Council', 'World', 'Court', 'Throne',
    'Combat', 'Ghost', 'Cataclysm', 'Resurrection', 'Instigator',
]

# Houses are pinned: their colours are already on rendered cards and in
# the art, and a House's colour is the one thing a player learns first.
PINNED = {
    'Ravenmark': '#7d3b2e',
    'Vipren':    '#2f5c46',
    'Goldwyn':   '#8a6a25',
    'Aldemar':   '#6a6350',
    # The four added 2026-09-20.  Pinned like the others and picked to
    # sit clear of them in Lab space as well as by name: the first four
    # occupy red, green, gold and grey, so these take amber, ochre,
    # slate and a dark blood.  A player learns a House by its colour
    # before its rules, and eight is enough that two that read alike
    # would be a real confusion rather than an aesthetic one.
    'Leoward':      '#9c6b1f',
    'Stonegarth':        '#5d5326',
    'Wulfren':      '#3f4a57',
    'Everhold':      '#5c2f33',
}


def lab(h):
    r, g, b = (int(h[i:i + 2], 16) / 255 for i in (1, 3, 5))
    def f(c): return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = f(r), f(g), f(b)
    x = r * .4124 + g * .3576 + b * .1805
    y = r * .2126 + g * .7152 + b * .0722
    z = r * .0193 + g * .1192 + b * .9505
    x /= .95047; z /= 1.08883
    def g2(t): return t ** (1 / 3) if t > .008856 else 7.787 * t + 16 / 116
    x, y, z = g2(x), g2(y), g2(z)
    return (116 * y - 16, 500 * (x - y), 200 * (y - z))


def hexc(h, s, v):
    r, g, b = colorsys.hsv_to_rgb(h % 1.0, s, v)
    return '#%02x%02x%02x' % (int(r * 255), int(g * 255), int(b * 255))


def dist(a, b):
    return math.dist(lab(a), lab(b))


def score(pal):
    return min(dist(a, b) for a, b in itertools.combinations(pal.values(), 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--swatch', action='store_true')
    a = ap.parse_args()

    rng = random.Random(20260919)
    free = [d for d in DECKS if d not in PINNED]
    # A muted register: saturation and value both stay in a narrow band so
    # nothing here can turn into a primary.
    best, best_s = None, -1
    for _ in range(4000):
        pal = dict(PINNED)
        for i, d in enumerate(free):
            pal[d] = hexc(rng.random(),
                          rng.uniform(0.28, 0.66),
                          rng.uniform(0.26, 0.62))
        s = score(pal)
        if s > best_s:
            best, best_s = pal, s
    # local search
    for _ in range(30000):
        d = rng.choice(free)
        keep = best[d]
        cand = hexc(rng.random(), rng.uniform(0.28, 0.66),
                    rng.uniform(0.26, 0.62))
        best[d] = cand
        s = score(best)
        if s > best_s:
            best_s = s
        else:
            best[d] = keep

    print('  %d decks, closest pair %.1f (was 2.3)\n' % (len(DECKS), best_s))
    for d in DECKS:
        c = best[d]
        L = lab(c)[0]
        # the dark card ground is the accent taken down to near-black
        r, g, b = (int(c[i:i + 2], 16) for i in (1, 3, 5))
        dark = '#%02x%02x%02x' % (int(r * .28), int(g * .28), int(b * .28))
        print("  %-13s ACCENT='%s'; DARK='%s'   L=%.0f%s"
              % (d, c, dark, L, '  (pinned)' if d in PINNED else ''))

    if a.swatch:
        import subprocess
        args = ['magick']
        for d in DECKS:
            args += ['(', '-size', '150x90', 'xc:' + best[d],
                     '-gravity', 'south', '-pointsize', '13',
                     '-fill', '#f0e6d0',
                     '-font', os.path.join(_FD, 'Spectral-Regular.ttf'),
                     '-annotate', '+0+6', d, ')']
        args += ['-background', '#12100e', '+append', '/tmp/palrow.png']
        subprocess.run(args, check=True)
        subprocess.run(['magick', '/tmp/palrow.png', '-crop', '1425x90+0+0',
                        '+repage', '/tmp/pal1.png'], check=True)
        subprocess.run(['magick', '/tmp/palrow.png', '-crop', '1425x90+1425+0',
                        '+repage', '/tmp/pal2.png'], check=True)
        subprocess.run(['magick', '/tmp/pal1.png', '/tmp/pal2.png',
                        '-background', '#12100e', '-append',
                        '/tmp/palette.png'], check=True)
        print('\n  /tmp/palette.png')


if __name__ == '__main__':
    main()
