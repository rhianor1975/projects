#!/usr/bin/env python3
"""A still of the table, at 1920x1080, from the real card art.

    python3 tools/mktable.py > /dev/null   # writes TABLE.png

Not a UI and not a mockup of one: a picture of everything one seat is
entitled to see at one moment, laid out, so that the shape of the screen
can be argued about before a toolkit is chosen.  The screen tells you
what the engine has to expose; deciding it after the toolkit is how you
end up with a UI that can only show what the toolkit made easy.

Everything drawn here comes from a real View: your own hand and lords in
full, the other House's hand as backs, its lords face up but with their
Revolution counts absent, because that is exactly what court_view gives
a seat.
"""
import os
import subprocess
import sys

W, H = 1920, 1080
RIGHT = 1450                     # the table ends here, the panel begins

# The vertical budget, written down rather than discovered.  The first
# attempt placed each band by eye and the hand ended up drawn through the
# status bar; a screen this dense needs its rows declared once.
LORD_W = 96                     # a lord card is 96 wide, so 134 tall
Y_THEIR_BAR   = 0,   112
Y_THEIR_LORDS = 138, 180        # 134 of card, then 44 for the two counts
Y_TABLE       = 330, 226
Y_YOUR_LORDS  = 580, 180
Y_YOUR_BAR    = 772, 96
Y_HAND        = 876, 200
GROUND, PANEL = '#14110d', '#1c1812'
INK, DIM, PALE = '#e8dab8', '#9b8a6a', '#f4ead2'
import os
_FD = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'fonts')
SERIF = os.path.join(_FD, 'Spectral-Regular.ttf')
SERIFB = os.path.join(_FD, 'Spectral-Bold.ttf')
IM = 'magick'
cmd = []


def run(args):
    subprocess.run([IM] + args, check=True)


def text(x, y, s, size=22, fill=INK, bold=False, anchor='start'):
    g = {'start': 'northwest', 'middle': 'north', 'end': 'northeast'}[anchor]
    xx = x if anchor == 'start' else (W - x if anchor == 'end' else x)
    cmd.extend(['-font', SERIFB if bold else SERIF, '-pointsize', str(size),
                '-fill', fill, '-gravity', g,
                '-annotate', '+%d+%d' % (xx, y), s])


def box(x, y, w, h, fill, stroke=None, sw=2):
    cmd.extend(['-fill', fill, '-stroke', stroke or 'none',
                '-strokewidth', str(sw), '-gravity', 'northwest',
                '-draw', 'roundrectangle %d,%d %d,%d 6,6'
                % (x, y, x + w, y + h)])
    cmd.extend(['-stroke', 'none'])


def bar(x, y, w, n, of, colour, label, value):
    """A resource: a label, a number, and a bar you can read at a glance."""
    text(x, y - 2, label, 15, '#8a7a5c', True)
    text(x + w - 4, y - 4, value, 26, PALE, True, 'start')
    box(x, y + 26, w, 10, '#2a241b')
    if of:
        box(x, y + 26, int(w * min(n, of) / of), 10, colour)


CACHE = '/tmp/court-faces'


def face(cid):
    """A composed card, not the raw art.  The first version composited
    art/<id>.webp straight onto the table, which is the painting without
    its frame, name, cost or rules text -- a hand of seven pictures."""
    os.makedirs(CACHE, exist_ok=True)
    out = '%s/%s.png' % (CACHE, cid)
    if not os.path.exists(out) and os.path.exists('art/%s.webp' % cid):
        subprocess.run(['tools/card.sh', cid, out],
                       stdout=subprocess.DEVNULL, check=False)
    return out


def card(path, x, y, w):
    if not os.path.exists(path):
        return False
    cmd.extend(['(', path, '-resize', '%dx' % w, ')',
                '-gravity', 'northwest', '-geometry', '+%d+%d' % (x, y),
                '-composite'])
    return True


HAND = ['RAV001', 'RAV006', 'RAV011', 'WAR003', 'POL007', 'INT004', 'PRM002']
THEIR_LORDS = ['VIP009', 'GOL005']          # stand-ins: lords are still rendering
YOUR_LORDS = ['ALD002', 'INT006', 'LEV001']
UNBOUND = ['LEV006', 'LEV003']

cmd = ['-size', '%dx%d' % (W, H), 'xc:' + GROUND]

# ---- the other House -------------------------------------------------
box(0, 0, RIGHT, Y_THEIR_BAR[1], '#20160f')
if os.path.exists('assets/house-vipren.png'):
    cmd.extend(['(', 'assets/house-vipren.png', '-resize', '64x64',
                '-channel', 'RGB', '-fill', '#7fd6a8', '-colorize', '100%',
                '+channel', ')', '-gravity', 'northwest',
                '-geometry', '+24+26', '-composite'])
text(96, 20, 'VIPREN', 28, '#9fe0bb', True)
text(96, 54, 'What is Whispered is Owned', 16, '#6f9a82')
text(96, 80, 'LORD PARAMOUNT', 15, '#d8b070')

for i, (lab, val, col, n) in enumerate(
        [('MILITARY', '2', '#8f3c2c', 2), ('CAPITAL', '6', '#3f5a74', 6),
         ('GOLD', '3', '#9a7b28', 3)]):
    bar(400 + i * 138, 26, 112, n, 12, col, lab, val)
text(400, 84, 'unrest   M1  C0  G2', 17, '#b06a5a')
text(830, 26, 'GRIEVANCE', 14, '#8a7a5c', True)
text(830, 46, '4', 28, PALE, True)
text(940, 26, 'WORD KEPT', 14, '#8a7a5c', True)
text(940, 46, '3', 28, PALE, True)
text(1062, 26, 'BROKEN', 14, '#8a7a5c', True)
text(1062, 46, '1', 28, '#c98a72', True)

text(1180, 22, 'THEIR HAND', 14, '#8a7a5c', True)
for i in range(5):
    card('art/backs/vipren.webp', 1180 + i * 40, 42, 66)

# ---- their lords -----------------------------------------------------
text(24, Y_THEIR_LORDS[0] - 22, 'THEIR LORDS', 16, '#8a7a5c', True)
for i, cid in enumerate(THEIR_LORDS):
    x = 24 + i * (LORD_W + 12)
    card(face(cid), x, Y_THEIR_LORDS[0], LORD_W)
    # Servitude is theirs and visible once revealed; Revolution is not
    # shown at all, because the View does not carry it for this seat.
    yy = Y_THEIR_LORDS[0] + 140
    box(x, yy, LORD_W, 18, '#2a241b')
    box(x, yy, int(LORD_W * 4 / 5), 18, '#6a5a3a')
    text(x + 4, yy, 'SERVITUDE 4', 12, PALE, True)
    text(x + 4, yy + 21, 'REVOLUTION  ?', 12, '#7a6a52', True)
text(24 + len(THEIR_LORDS) * (LORD_W + 12) + 16, Y_THEIR_LORDS[0] + 40,
     'the Revolution in a lord you do not hold\nis not yours to see', 16, '#6d5f48')

# ---- the table between -----------------------------------------------
box(0, Y_TABLE[0] - 10, RIGHT, 2, '#3a3126')
text(24, Y_TABLE[0], 'UNBOUND', 16, '#8a7a5c', True)
for i, cid in enumerate(UNBOUND):
    card(face(cid), 24 + i * 108, Y_TABLE[0] + 24, 100)

text(266, Y_TABLE[0], 'PROMISES IN PLAY', 16, '#8a7a5c', True)
PROM = [('Two Rounds of Quiet', 'Peace', 'due end of next round', '3 Gold paid'),
        ('Paid in Advance', 'Forbearance', 'due at their next challenge', '4 Gold paid')]
for i, (name, term, due, cons) in enumerate(PROM):
    y = Y_TABLE[0] + 24 + i * 92
    box(266, y, 520, 80, '#221c14', '#5a4a30')
    text(282, y + 8, name, 22, PALE, True)
    text(282, y + 38, '%s  \u00b7  %s' % (term, due), 16, '#9b8a6a')
    text(282, y + 58, cons, 15, '#c9a86a')
    text(660, y + 12, 'YOU \u2192 THEM', 15, '#8a7a5c', True)

# ---- the piles, which are public and permanent and the only thing trust
# ---- is made of, so they are on the table and not in a status line -----
def pile(x, y, label, n, back, tint):
    text(x, y, label, 15, '#8a7a5c', True)
    for k in range(min(n, 4)):
        cmd.extend(['(', back, '-resize', '76x', '-channel', 'RGB',
                    '-fill', tint, '-colorize', '35%', '+channel', ')',
                    '-gravity', 'northwest',
                    '-geometry', '+%d+%d' % (x + k * 9, y + 24 + k * 5),
                    '-composite'])
    text(x + 96, y + 52, str(n), 34, PALE, True)

pile(840, Y_TABLE[0], 'YOUR WORD KEPT', 2, 'art/backs/ravenmark.webp', '#e8dab8')
pile(1010, Y_TABLE[0], 'THEIR WORD KEPT', 3, 'art/backs/vipren.webp', '#e8dab8')
pile(1190, Y_TABLE[0], 'THEIR BROKEN WORD', 1, 'art/backs/vipren.webp', '#8f3c2c')
text(840, Y_TABLE[0] + 146, 'the piles are public and permanent, and the only '
     'thing trust is made of', 16, '#6d5f48')

# ---- your lords -------------------------------------------------------
text(24, Y_YOUR_LORDS[0] - 20, 'YOUR LORDS', 16, '#8a7a5c', True)
for i, cid in enumerate(YOUR_LORDS):
    x = 24 + i * (LORD_W + 12)
    card(face(cid), x, Y_YOUR_LORDS[0], LORD_W)
    yy = Y_YOUR_LORDS[0] + 140
    box(x, yy, LORD_W, 18, '#2a241b')
    box(x, yy, int(LORD_W * (5 - i) / 5), 18, '#6a5a3a')
    text(x + 4, yy, 'SERVITUDE %d' % (5 - i), 12, PALE, True)
    box(x, yy + 21, LORD_W, 18, '#2a241b')
    box(x, yy + 21, int(LORD_W * (i + 1) / 5), 18, '#7d3b4a')
    text(x + 4, yy + 21, 'REVOLUTION %d' % (i + 1), 12, PALE, True)
text(24 + len(YOUR_LORDS) * (LORD_W + 12) + 16, Y_YOUR_LORDS[0] + 30,
     'yours, so you see both counts.  the third has\nturned: at the next Council it will not raise\nits hand',
     16, '#a8795f')

# ---- your bar ---------------------------------------------------------
box(0, Y_YOUR_BAR[0], RIGHT, Y_YOUR_BAR[1], '#20160f')
if os.path.exists('assets/house-ravenmark.png'):
    cmd.extend(['(', 'assets/house-ravenmark.png', '-resize', '58x58',
                '-channel', 'RGB', '-fill', '#e0a898', '-colorize', '100%',
                '+channel', ')', '-gravity', 'northwest',
                '-geometry', '+24+%d' % (Y_YOUR_BAR[0] + 20), '-composite'])
text(94, Y_YOUR_BAR[0] + 14, 'RAVENMARK', 26, '#e8b4a0', True)
text(94, Y_YOUR_BAR[0] + 48, 'By Sword and Storm', 16, '#9a7266')
for i, (lab, val, col, n, lv) in enumerate(
        [('MILITARY', '6', '#8f3c2c', 6, '5'), ('CAPITAL', '3', '#3f5a74', 3, '3'),
         ('GOLD', '4', '#9a7b28', 4, '2')]):
    x = 400 + i * 138
    bar(x, Y_YOUR_BAR[0] + 16, 112, n, 12, col, lab, val)
    text(x, Y_YOUR_BAR[0] + 58, 'levy %s' % lv, 16, '#b8a884')
text(830, Y_YOUR_BAR[0] + 16, 'GRIEVANCE', 14, '#8a7a5c', True)
text(830, Y_YOUR_BAR[0] + 36, '2', 28, PALE, True)
text(940, Y_YOUR_BAR[0] + 16, 'WORD KEPT', 14, '#8a7a5c', True)
text(940, Y_YOUR_BAR[0] + 36, '2', 28, PALE, True)
text(1062, Y_YOUR_BAR[0] + 16, 'BROKEN', 14, '#8a7a5c', True)
text(1062, Y_YOUR_BAR[0] + 36, '0', 28, PALE, True)
text(1170, Y_YOUR_BAR[0] + 16, 'UNREST', 14, '#8a7a5c', True)
text(1170, Y_YOUR_BAR[0] + 36, 'M0  C2  G0', 22, '#c98a72', True)

# ---- the right panel: the Court, and where you may draw ---------------
box(RIGHT, 0, W - RIGHT, H, PANEL)
box(RIGHT, 0, 3, H, '#3a3126')
text(RIGHT + 30, 28, 'ROUND 9', 26, PALE, True)
text(RIGHT + 30, 62, 'your turn \u00b7 play', 18, '#9b8a6a')

text(RIGHT + 30, 116, 'THE COURT', 22, '#d8c49a', True)
text(RIGHT + 30, 146, 'crowns at +8', 16, '#8a7a5c')
for row, (who, fav, col) in enumerate((('YOU', 5, '#8f5c3c'), ('THEM', 2, '#3c7a5c'))):
    y = 182 + row * 76
    text(RIGHT + 30, y, who, 16, '#8a7a5c', True)
    for i in range(9):
        on = i <= fav
        box(RIGHT + 30 + i * 44, y + 24, 36, 30,
            col if on else '#241f18', '#4a3f2e', 1)
        if i == 8:
            text(RIGHT + 38 + i * 44, y + 30, '\u2605', 18,
                 PALE if on else '#5a4e3a', True)
    text(RIGHT + 400, y + 26, str(fav), 26, PALE, True)

text(RIGHT + 30, 350, 'DRAW  \u2014  one a turn', 20, '#d8c49a', True)
DECKS = [('YOUR HOUSE', 'free', '#7d3b2e'), ('WAR', '1 Military', '#8f3c2c'),
         ('POLITICAL', '1 Capital', '#3f5a74'), ('INTRIGUE', '1 Gold', '#9a7b28'),
         ('WORLD', 'free \u00b7 resolves at once', '#8b7362'),
         ('AMBITION', '2 Capital', '#6f4a68')]
for i, (name, cost, col) in enumerate(DECKS):
    y = 388 + i * 62
    box(RIGHT + 30, y, 410, 52, '#241f18', col, 2)
    box(RIGHT + 30, y, 8, 52, col)
    text(RIGHT + 54, y + 8, name, 20, PALE, True)
    text(RIGHT + 54, y + 32, cost, 15, '#9b8a6a')
text(RIGHT + 30, 764, 'Throne closed \u2014 not Throneworthy', 15, '#6d5f48')

text(RIGHT + 30, 810, 'THEIR AMBITIONS', 16, '#8a7a5c', True)
for i in range(6):
    box(RIGHT + 30 + i * 62, 836, 52, 72, '#241f18', '#4a3f2e', 1)
    text(RIGHT + 50 + i * 62, 860, '?', 26, '#5a4e3a', True)
text(RIGHT + 30, 920, 'six dealt, none revealed', 15, '#6d5f48')

# ---- your hand --------------------------------------------------------
for i, cid in enumerate(HAND):
    card(face(cid), 20 + i * 146, Y_HAND[0], 140)

run(cmd + ['TABLE.png'])
print('TABLE.png', file=sys.stderr)
