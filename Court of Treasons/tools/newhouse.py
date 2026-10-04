#!/usr/bin/env python3
"""Write the four new Houses' decks into POOL.tsv.

    python3 tools/newhouse.py [--dry-run]

Each House champions a subsystem that was built and idle -- measured
over 200 games before they existed: lords held per House 0.08,
Promises 1.69 a game, Grievance 0.80 of a maximum ten.  A deck is how
a House actually reaches its subsystem, so the weighting below is the
point rather than decoration: the Lion's deck is mostly Promises, the
Ox's mostly Bonds, and so on.  A House with an even spread of cards
would play like the other four whatever its rules said.

Every effect here is written in vocabulary gen-cards.py already reads.
That is a hard rule and not a convenience: a new House that needed new
opcodes would be a new subsystem wearing a House's name, and the whole
point of these four is to use the systems that already exist.
"""
import collections
import csv
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(os.path.dirname(HERE))

# (effect, type, category, cost-resource, weight-of-appearance)
#
# Costs are left at a nominal 1 and repriced by tools/price.py, which
# assigns by quantile across the whole set -- setting them by hand here
# would just be overwritten, and worse, would skew the quantiles.
COMMON = [
    ('+%d %s Levy this turn',              'Play on turn', 'Core',   None, 8),
    ('+%d %s Standing permanently',        'Play on turn', 'Growth', None, 3),
    ('Target House loses %d %s Standing',  'Play on turn', 'Core',   None, 4),
    ('Take %d %s Levy from target House',  'Play on turn', 'Core',   None, 3),
    ('Target House gains %d Unrest',       'Play on turn', 'Chaos',  None, 4),
    ('Clear %d of your Unrest',            'Play on turn', 'Core',   None, 3),
    ('Every House including you loses %d %s Levy', 'Play on turn', 'Chaos', None, 2),
]

HOUSES = {
 'Leoward': dict(prefix='LIO', res='C', words=[
    'Pride', 'Oath', 'Word', 'Banner', 'Mane', 'Crown', 'Roar', 'Standard',
    'Vow', 'Honour', 'Charge', 'Sunlit', 'Golden', 'Sworn', 'Loyal'],
   own=[  # the Word: Promises
    ('Term Peace; consideration up to %d Levy',        'Promise', 'Promise', 10),
    ('Term Vote; consideration up to %d Levy',         'Promise', 'Promise', 6),
    ('Term Restraint; consideration up to %d Levy',    'Promise', 'Promise', 6),
    ('Term Tribute; consideration up to %d Levy',      'Promise', 'Promise', 5),
    ('Term Forbearance; consideration up to %d Levy',  'Promise', 'Promise', 4),
    ('+%d Capital Levy per Promise in your Word Kept pile this turn', 'Play on turn', 'Reputation', 6),
    ('gain %d vote at the next Council',               'Play on turn', 'Council', 5),
    ('Cancel a Promise before it comes due; it is discarded, not broken', 'Play on turn', 'Promise', 4),
   ]),
 'Stonegarth': dict(prefix='OXX', res='G', words=[
    'Yoke', 'Furrow', 'Harvest', 'Stone', 'Patient', 'Deep', 'Root', 'Plough',
    'Burden', 'Slow', 'Broad', 'Iron Collar', 'Long', 'Steady', 'Field'],
   own=[  # the Yoke: Bonds and lords
    ('Advance Servitude by %d',                        'Lever', 'Bond', 12),
    ('Advance Servitude by %d against target House',   'Lever', 'Bond', 8),
    ('Set one of your lords\' Revolution to 0',        'Play on turn', 'Bond', 6),
    ('Every lord the other House holds gains %d hidden Revolution', 'Play on turn', 'Bond', 5),
    ('gain %d vote at the next Council',               'Play on turn', 'Council', 5),
    ('+%d Gold Levy this turn',                        'Play on turn', 'Core', 5),
   ]),
 'Wulfren': dict(prefix='WLF', res='M', words=[
    'Pack', 'Hunger', 'Grudge', 'Winter', 'Fang', 'Lean', 'Howl', 'Scar',
    'Long Memory', 'Hunt', 'Throat', 'Cold', 'Snow', 'Circling', 'Old Wound'],
   own=[  # the Grudge: Grievance, banked AND spent
    # Measured at 500 games with the first version of this deck, the
    # Wolf banked Grievance beautifully -- 3.15 at game end against a
    # field of 0.8 -- and had the LOWEST Standing in the game at 3.06
    # and a 29.4% win rate.  It was hoarding a resource with nothing to
    # buy.  design.xml says the drain is meant to be card costs: "Many
    # War and Political cards name Grievance in their cost.  That is
    # the intended main drain."  So half this deck is now priced in
    # Grievance, which is the only House for which that is cheap.
    # Second pass.  The Grievance sink worked -- end-game Grievance
    # fell 3.15 to 2.00, which is the Wolf spending it rather than
    # hoarding -- but it was still the floor at 31.7% and still had
    # the lowest Standing in the game at 3.22.  So the sink is pointed
    # harder at Standing, which is the thing it cannot otherwise get:
    # Grievance is the one currency the Wolf is rich in, and permanent
    # Standing is the one purchase nothing else in its deck makes.
    # A second pass pushed the Growth weight from 7 to 12 and the Wolf
    # got WORSE, 31.7% to 27.0%.  Grievance caps at 10 and accrues
    # slowly even doubled, so a deck weighted toward expensive
    # Grievance-priced cards is a deck of cards it cannot afford --
    # dead weight, not power.  Reverted, and recorded because the
    # obvious direction was the wrong one.
    ('gain %d Grievance',                              'Play on turn', 'Chaos', 6),
    ('Target House gains %d Unrest',                   'Play on turn', 'Chaos', 4),
    ('Target House loses %d Standing of your choosing','Play on turn', 'Core', 5, 'Grv'),
    ('+%d %s Standing permanently',                    'Play on turn', 'Growth', 7, 'Grv'),
    ('Take %d %s Levy from target House',              'Play on turn', 'Core', 5, 'Grv'),
    ('gain %d Favour with the Court',                  'Play on turn', 'Reputation', 5, 'Grv'),
    ('+%d Military Levy in one combat, this combat only', 'Reaction', 'Combat', 5),
    ('Reduce an attacker\'s committed Military Levy by %d', 'Reaction', 'Combat', 4),
   ]),
 'Everhold': dict(prefix='BOA', res='M', words=[
    'Tusk', 'Bristle', 'Charge', 'Thicket', 'Mire', 'Rooting', 'Wild', 'Gore',
    'Stubborn', 'Mud', 'Bracken', 'Unbroken', 'Furious', 'Low', 'Blood'],
   own=[  # the Beast: Unrest, not falling, and a way to the Court
    # The Boar ended 500 games on 0.68 Favour against a field of 2.14
    # to 6.16 and won 34.5%.  It is not weak, it is disqualified: it
    # cannot buy Favour, and Favour of the Court decides about a third
    # of all games.  The weakness stays -- it has nothing the Court
    # wants to SELL to -- but it can now EARN the Court's regard the
    # way the design's own rule does, by being the House still standing
    # when the realm is in uproar.
    ('gain %d Favour with the Court',                  'Play on turn', 'Reputation', 7),
    ('Every House gains %d Unrest',                    'Play on turn', 'Chaos', 7),
    ('Target House gains %d Unrest',                   'Play on turn', 'Chaos', 6),
    ('+%d Military Levy when defending, not when attacking', 'Reaction', 'Combat', 6),
    ('+%d Military Levy in one combat, this combat only', 'Reaction', 'Combat', 5),
    ('Attacker loses %d Military',                     'Reaction', 'Combat', 5),
    ('+%d Military Standing permanently',              'Play on turn', 'Growth', 6),
    ('Clear %d of your Unrest',                        'Play on turn', 'Core', 4),
   ]),
}

NOUNS = ['Host', 'Gate', 'Hall', 'Road', 'Watch', 'Season', 'Bargain', 'Debt',
         'Court', 'Feast', 'March', 'Vigil', 'Tithe', 'Standard', 'Reckoning',
         'Cause', 'Claim', 'Answer', 'Silence', 'Summons', 'Tally', 'Keeping']
RES3 = ['Military', 'Capital', 'Gold']


def build(rng, house, spec, n):
    """One deck.  Names are drawn without repeats inside a House."""
    # An entry is (effect, type, category, weight) or the same with a
    # cost resource on the end -- 'Grv' for the Wolf's Grievance sinks,
    # which is the only House for which that price is cheap.
    pool = [(x[0], x[1], x[2], x[3], x[4] if len(x) > 4 else None)
            for x in spec['own']]
    pool += [(e, t, c, w, None) for e, t, c, _r, w in COMMON]
    weights = [x[3] for x in pool]
    used, rows, i = set(), [], 0
    while len(rows) < n:
        e, t, c, _w, cres = rng.choices(pool, weights=weights)[0]
        holes = e.count('%d')
        res = rng.choice(RES3)
        if house == 'Wulfren' and '%s' in e and rng.random() < 0.5:
            res = 'Military'
        vals = [rng.choice([1, 1, 2, 2, 3]) for _ in range(holes)]
        try:
            eff = e % tuple(vals) if '%s' not in e else \
                  (e % (vals[0], res) if holes == 1 else e % tuple(vals))
        except TypeError:
            continue
        name = 'The %s %s' % (rng.choice(spec['words']), rng.choice(NOUNS))
        if name in used:
            continue
        used.add(name)
        i += 1
        rows.append([house, '%s%03d' % (spec['prefix'], i), name, t, c,
                     '1Grv' if cres == 'Grv' else '1' + spec['res'],
                     '', eff])
    return rows


def main():
    dry = '--dry-run' in sys.argv
    rng = random.Random(20260920)      # seeded: a rerun gives the same deck
    made = []
    for house, spec in HOUSES.items():
        made += build(rng, house, spec, 100)
    if dry:
        by = collections.Counter(r[0] for r in made)
        print('  would write %d cards: %s' % (len(made), dict(by)))
        for r in made[:6]:
            print('    %-5s %-8s %-26s %-13s %s'
                  % (r[0], r[1], r[2][:26], r[3], r[7][:40]))
        return
    rows = list(csv.reader(open('POOL.tsv'), delimiter='\t'))
    hdr, body = rows[0], [r for r in rows[1:] if r[0] not in HOUSES]
    with open('POOL.tsv', 'w', newline='') as fh:
        csv.writer(fh, delimiter='\t', lineterminator='\n').writerows(
            [hdr] + body + made)
    print('  %d cards written into POOL.tsv' % len(made))


main()
