#!/usr/bin/env python3
"""Draw the round as it stands, with what two players kills marked on it.

    python3 tools/mkflow.py > FLOW.svg

A diagram rather than a document because the question is which rules
survive, and a rule that survives is one you can point at.  Everything
struck through is dead under the two-player first rule; everything in
plain type stands.  The counts are from TWOPLAYER.md.
"""
W, H = 1500, 1080
INK, DIM, DEAD = '#241a12', '#6b5a3e', '#8f3c2c'
PARCH, BRASS, LINE = '#d2bc8e', '#8a6a25', '#a08d6a'
GROUND = '#1a1610'

out = []
def t(x, y, s, size=20, fill=INK, w='normal', anchor='start', strike=False):
    dec = ' text-decoration="line-through"' if strike else ''
    out.append('<text x="%d" y="%d" font-family="Spectral,Georgia,serif" font-size="%d" '
               'fill="%s" font-weight="%s" text-anchor="%s"%s>%s</text>'
               % (x, y, size, fill, w, anchor, dec, s))
def box(x, y, w, h, fill=PARCH, stroke=BRASS, sw=3, r=10):
    out.append('<rect x="%d" y="%d" width="%d" height="%d" rx="%d" fill="%s" '
               'stroke="%s" stroke-width="%d"/>' % (x, y, w, h, r, fill, stroke, sw))
def arrow(x, y1, y2, colour=BRASS):
    out.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" '
               'stroke-width="4"/>' % (x, y1, x, y2 - 12, colour))
    out.append('<polygon points="%d,%d %d,%d %d,%d" fill="%s"/>'
               % (x, y2, x - 8, y2 - 14, x + 8, y2 - 14, colour))

out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
           'viewBox="0 0 %d %d">' % (W, H, W, H))
out.append('<rect width="%d" height="%d" fill="%s"/>' % (W, H, GROUND))

t(40, 56, 'COURT OF TREASONS', 30, '#e6d5ab', 'bold')
t(40, 88, 'One round, as design.xml has it.  Struck through is what a two-player '
          'first rule kills.', 20, '#a89madeup')
out[-1] = out[-1].replace('#a89madeup', '#b8a884')

# ---------------------------------------------------------------- phases
PHASES = [
    ('1  ACCESSION', [('Recount Total Power; the higher is Lord Paramount', 0)],
     'At two Houses this is "whoever is ahead".'),
    ('2  TAX', [('Lord Paramount takes 1 Standing from the other House', 0),
                ('That House gains 1 Grievance', 0)],
     'First Paramount already wins 66% of two-player games.'),
    ('3  TURNS', [('Each House in seat order -- see below', 0)], ''),
    ('4  RECKONING', [('Promises due are kept or broken, revealed together', 0)],
     'Five of seven terms die with votes and vassals.'),
    ('5  CONSEQUENCE', [('Instigators resolve', 1), ('Revolts at Revolution 5', 1),
                        ('Unrest 5 resolves', 0), ('Ambitions checked', 0),
                        ('Throne challenges answered', 0)], ''),
    ('6  WORLD', [('Even rounds: turn one World card', 0),
                  ('The Court turns one card', 0)],
     'The Court is no longer a two-player special case.'),
]
X, y = 40, 120
for title, items, note in PHASES:
    h = 44 + 30 * len(items) + (26 if note else 0)
    box(X, y, 700, h)
    t(X + 22, y + 32, title, 22, INK, 'bold')
    yy = y + 62
    for text, dead in items:
        t(X + 40, yy, ('×  ' if dead else '·  ') + text, 19,
          DEAD if dead else INK, 'normal', 'start', bool(dead))
        yy += 30
    if note:
        t(X + 40, yy + 4, note, 17, DIM, 'normal')
    if title.startswith('6') is False:
        arrow(X + 350, y + h, y + h + 26)
    y += h + 26

# ------------------------------------------------------------- the turn
TX = 790
box(TX, 120, 670, 470, '#1f1a12', BRASS, 3)
t(TX + 24, 158, 'A TURN', 24, '#e6d5ab', 'bold')
t(TX + 24, 186, 'Phase 3, for one House', 18, '#b8a884')
TURN = [('Levy set from Standing, less Unrest', 0, 'automatic'),
        ('Draw -- choose a deck and pay its access', 0, 'once'),
        ('Play cards, paying Levy', 0, 'unlimited'),
        ('Build Bond -- advance Servitude with a Lever', 1, 'once'),
        ('Send Instigator', 1, 'once'),
        ('Propose a Promise', 0, 'unlimited, free'),
        ('Declare -- Open War, or a Throne challenge', 0, 'once')]
yy = 232
for text, dead, lim in TURN:
    c = DEAD if dead else '#e8dab8'
    out.append('<text x="%d" y="%d" font-family="Spectral,Georgia,serif" font-size="19" '
               'fill="%s"%s>%s%s</text>'
               % (TX + 44, yy, c,
                  ' text-decoration="line-through"' if dead else '',
                  '×  ' if dead else '·  ', text))
    t(TX + 640, yy, lim, 16, '#8a7a5c' if not dead else DEAD, 'normal', 'end')
    yy += 38
t(TX + 24, 556, 'Two of seven turn actions die with vassals.', 17, '#8a7a5c')

# --------------------------------------------------------------- winning
box(TX, 616, 670, 300, '#1f1a12', BRASS, 3)
t(TX + 24, 654, 'HOW IT ENDS', 24, '#e6d5ab', 'bold')
WIN = [('Claim by Force -- 3 Ambitions, then beat the field', 0),
       ('The Purchased Throne -- 12 Gold and 8 Capital Standing', 0),
       ('Favour of the Court -- reach +5', 0),
       ('Acclamation -- be voted king', 1),
       ('The Last House -- the other House is dead', 0),
       ('The Final King -- round 30, highest Total Power', 0)]
yy = 692
for text, dead in WIN:
    out.append('<text x="%d" y="%d" font-family="Spectral,Georgia,serif" font-size="19" '
               'fill="%s"%s>%s%s</text>'
               % (TX + 44, yy, DEAD if dead else '#e8dab8',
                  ' text-decoration="line-through"' if dead else '',
                  '×  ' if dead else '·  ', text))
    yy += 34
t(TX + 24, 900, 'Five roads become four, and the Court now carries two of them.',
  17, '#8a7a5c')

# ----------------------------------------------------------------- tally
box(TX, 942, 670, 108, '#2a1c16', DEAD, 3)
t(TX + 24, 978, 'WHAT IT COSTS', 20, '#e8c4b0', 'bold')
t(TX + 24, 1008, '51 of 124 seed cards  ·  3 whole decks  ·  90 manifest cards',
  19, '#e8c4b0')
t(TX + 24, 1034, 'Lever, Instigator and Council lose their purpose entirely.',
  17, '#c9a08c')

out.append('</svg>')
print('\n'.join(out))
