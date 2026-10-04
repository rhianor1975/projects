#!/usr/bin/env python3
"""Price every card from what it does, then let the soak correct it.

    python3 tools/price.py            rewrite the costs in CARDS.tsv
    python3 tools/price.py --dry-run  print what would change

Why derived rather than hand-set.  The pool's costs came from the
four-player board game and were carried across untouched while the
effects were rewritten: 165 of the 221 cards added tonight cost exactly
1, whether they give +1 Gold Levy or put two hidden Revolution on every
Bond in play.  That is not "too cheap", it is no curve at all -- and it
is measurable in the soak as a 5.48x win spread (whoever draws the
strong 1-cost cards wins) and half the Levy going unspent (nothing
costs enough to drain a turn).

What this does NOT change is which resource a card costs.  That is
House character -- Ravenmark pays in Military, Goldwyn in Gold -- and
it is already right in the data.  Only the magnitude moves.

The weights below are a first pass and say so.  They are a hypothesis
to be corrected by measurement, which is the only way anything in this
game has been settled.
"""
import csv, importlib.util, io, contextlib, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
os.chdir(ROOT)

# Kept before gen-cards.py is handed a fake argv.  Overwriting it first
# made --dry-run invisible, so the dry run wrote.
ARGV = list(sys.argv)

spec = importlib.util.spec_from_file_location('gc', 'gen-cards.py')
gc = importlib.util.module_from_spec(spec)
sys.argv = ['gen-cards.py']
buf = io.StringIO()
try:
    with contextlib.redirect_stdout(buf):
        spec.loader.exec_module(gc)
except SystemExit:
    pass

# What an opcode is worth, before its magnitude.
#
# The grouping is by what the effect DOES to the game rather than by
# which tracker it touches: a permanent gain outranks a one-turn gain,
# anything that reaches into the other House outranks anything that
# only helps you, and anything that moves a road to victory outranks
# both.
WEIGHT = {
    # permanent, compounding
    'OP_STANDING': 3.0, 'OP_STANDING_LOSS': 3.0,
    # roads to winning
    'OP_THRONE_LEVY': 2.5, 'OP_THRONE_BAR': 3.0, 'OP_FAVOUR': 3.0,
    'OP_AMBITION_DONE': 3.5,
    # the Council, which elects the title the tax hangs off
    'OP_VOTE_GAIN': 1.6, 'OP_VOTE_FREE': 1.6, 'OP_VOTE_COMMAND': 2.0,
    'OP_LORD_DIRECT': 2.0, 'OP_COUNCIL': 1.4,
    # lords: slow, hidden, and how the Council is won
    'OP_SERVITUDE': 1.5, 'OP_REVOLUTION_ALL': 1.8, 'OP_REVOLUTION_ZERO': 1.5,
    'OP_REVOLUTION_FREEZE': 1.3, 'OP_LORD_FREE': 2.0, 'OP_REVOLT_CANCEL': 1.5,
    'OP_LORD_TRAIT': 1.2, 'OP_PARDON': 1.5,
    # reaching into the other House
    'OP_LEVY_STEAL': 1.8, 'OP_UNREST_TARGET': 1.6, 'OP_DECLARE_WAR': 1.5,
    'OP_LEVY_ALL_LOSE': 1.4, 'OP_PEEK_HAND': 1.0, 'OP_EXPOSE': 1.4,
    # your own turn
    'OP_LEVY': 1.0, 'OP_LEVY_ALL_GAIN': 0.8, 'OP_UNREST_CLEAR': 1.1,
    'OP_COMBAT_LEVY': 1.0, 'OP_COMBAT_LEVY_DEF': 0.9, 'OP_EXTRA_DRAW': 1.6,
    'OP_LEVY_KEEP': 1.4,
    # promises cost nothing to offer and nothing to refuse
    'OP_PROPOSE': 0.6, 'OP_PROPOSE_BONUS': 1.2, 'OP_PROPOSE_PAID': 0.8,
    # things you do to yourself
    'OP_GRIEVANCE_SELF': 0.4, 'OP_UNREST_SELF': 0.3,

    # ---- the pool conversion, 2026-09-20 --------------------------
    # 496 card-effects were falling through to DEFAULT_WEIGHT, and a
    # third of the whole set landed on exactly 1.2 as a result.  That
    # is what collapsed the curve: with 336 cards sharing one weight,
    # the 10% and 35% quantile cuts fell on the same value and the
    # cost-1 band came out EMPTY, with 43% of the game free.
    # A default is a placeholder, and at a third of the deck it stops
    # being one and becomes the answer.
    'OP_RESTORE': 3.5, 'OP_EXTINGUISH': 3.0, 'OP_SURVIVE': 2.6,
    'OP_WIN_GOLD': 4.0, 'OP_CLAIM_THRONE': 2.8, 'OP_MAKE_PARAMOUNT': 2.4,
    'OP_REVOLUTION_THEIRS': 1.8, 'OP_SERVITUDE_ALL': 1.6,
    'OP_SPY_ANY': 1.1, 'OP_NO_SPY': 0.8, 'OP_RUMOUR': 1.2,
    'OP_TIE_BREAK': 1.7, 'OP_VOTE_DOUBLE': 1.8, 'OP_VOTE_NULLIFY': 1.6,
    'OP_VOTE_BUY': 1.5, 'OP_COUNCIL': 1.4,
    'OP_NO_COMBAT': 1.5, 'OP_NO_ASSASSIN': 0.9,
    'OP_COMBAT_LEVY_IF': 1.0, 'OP_COMBAT_ATT_LOSS': 1.5,
    'OP_COMBAT_REDUCE': 1.4, 'OP_NO_SPOIL': 1.1, 'OP_SHIELD': 1.2,
    'OP_DRAW': 1.5, 'OP_DRAW_ALL': 0.7, 'OP_DRAW_DECK': 1.3,
    'OP_DISCARD_TARGET': 1.4, 'OP_STEAL_CARD': 1.6, 'OP_GIVE_CARD': 0.6,
    'OP_CANCEL_NEXT': 1.7, 'OP_SKIP_ACTION': 1.8,
    'OP_STANDING_ALL_LOSS': 1.5, 'OP_RANDOM_LOSS': 0.9,
    'OP_GRIEVANCE_SCALE': 0.8, 'OP_LP_GAIN': 0.9, 'OP_LP_PAY': 1.2,
    'OP_TAX_DOUBLE': 1.3, 'OP_LEVY_GIVE': 0.5, 'OP_NO_PROMISE': 1.0,
    'OP_PROMISE_CANCEL': 1.3, 'OP_PROMISE_COERCE': 1.5,
    'OP_ASSASSINATE': 2.0, 'OP_UNREST_ALL': 1.0, 'OP_LOWEST_GAIN': 0.9,
    'OP_REGENCY': 1.4, 'OP_NO_TAX': 1.0, 'OP_CALL_DEBT': 1.3,
    'OP_INSTIGATOR': 1.2, 'OP_INSTIGATOR_REDIRECT': 1.0,
    'OP_LORD_FREE': 1.8, 'OP_MANUMIT': 1.2, 'OP_BOND_RIDER': 1.0,
    'OP_EXPOSE': 1.3, 'OP_PEEK_HAND': 1.0, 'OP_SELL_SECRET': 1.2,
}
DEFAULT_WEIGHT = 1.2


def weigh(effect):
    """What an effect is worth, before it is turned into a price."""
    hits = gc.classify(effect or '')
    if not hits:
        return None
    total = 0.0
    for h in hits:
        op = h[0] if isinstance(h, (list, tuple)) else str(h)
        mag = 0
        if isinstance(h, (list, tuple)):
            for v in h[1:]:
                if isinstance(v, int):
                    mag = max(mag, abs(v))
        w = WEIGHT.get(op, DEFAULT_WEIGHT)
        # The first point of an effect is most of its value; the rest
        # scale sublinearly, or a +5 card costs five times a +1 and
        # nobody ever plays it.
        total += w * (1.0 + 0.45 * max(0, min(mag, 5) - 1))
    # Two effects on one card is worth more than one, not twice one.
    if len(hits) > 1:
        total *= 0.85
    return total


# What proportion of the cards should sit at each price.
#
# By quantile, not by dividing the weight through and rounding.  The
# weights DO separate -- 0.40 at the bottom to 7.00 at the top -- and
# dividing by a constant then rounding put three quarters of them back
# on 1, which is the exact flatness this exists to remove.  A share per
# price cannot do that: the curve is guaranteed, and tuning a weight
# moves a card along it rather than collapsing it.
GRIEVANCE_MAX = 10   # must match src/court.h

BANDS = [(0.10, 0), (0.35, 1), (0.65, 2), (0.87, 3), (0.97, 4), (1.01, 5)]


def bands_from(weights):
    """Turn the target shares into weight thresholds for this card set.

    Thresholds are forced strictly upward.  A quantile cut can land on
    the same weight as the one before it whenever many cards share a
    value, and price() takes the FIRST band whose cut is at or above
    the weight -- so a repeated threshold does not merely blur a
    boundary, it deletes a price.  With 336 cards on one weight the
    cost-1 band vanished entirely and 43% of the set came out free.

    Forcing the cut up means a tied group lands wholly in one band
    rather than being split, so the shares come out approximate.  That
    is the right trade: a band that is too wide is a blunt curve, and a
    band that is empty is no curve at all.
    """
    ws = sorted(weights)
    out, floor = [], None
    for share, cost in BANDS:
        i = min(len(ws) - 1, int(len(ws) * share))
        cut = ws[i]
        if floor is not None and cut <= floor:
            higher = [w for w in ws if w > floor]
            if not higher:
                continue          # nothing left above: the band cannot exist
            cut = higher[0]
        floor = cut
        out.append((cut, cost))
    return out


def price(w, thresholds):
    for cut, cost in thresholds:
        if w <= cut:
            return cost
    return thresholds[-1][1]


def main():
    dry = '--dry-run' in ARGV
    rows = list(csv.reader(open('CARDS.tsv'), delimiter='\t'))
    hdr, body = rows[0], rows[1:]
    ci, ei, ti, di = (hdr.index('cost'), hdr.index('effect'),
                      hdr.index('type'), hdr.index('deck'))
    RES = {'Ravenmark': 'M', 'Vipren': 'C', 'Goldwyn': 'G', 'Aldemar': 'C',
           'War': 'M', 'Political': 'C', 'Intrigue': 'G', 'World': 'C',
           'Court': 'C', 'Throne': 'M', 'Combat': 'M', 'Council': 'C',
           'Instigator': 'G', 'Ghost': 'C', 'Ambition': 'C'}
    weights = {}
    for r in body:
        if r[ti] != 'Ambition':
            w = weigh(r[ei])
            if w is not None:
                weights[id(r)] = w
    thresholds = bands_from(list(weights.values()))
    changed = 0
    for r in body:
        if r[ti] == 'Ambition' or id(r) not in weights:
            continue                     # a goal is not bought
        p = price(weights[id(r)], thresholds)
        old = r[ci]
        # Keep which resource it costs; move only how much.  A card with
        # no cost at all takes its deck's natural one.
        extra = ' '.join(x for x in old.split() if x.endswith('Grv'))
        letters = re.findall(r'\d+([MCG])', old)
        # A card paid for ENTIRELY in Grievance stays that way.  Adding a
        # Levy cost on top would quietly convert the design's "intended
        # main drain" for Grievance into an ordinary card with a rider,
        # and it is the only sink a House that banks Grievance has.
        if extra and not letters:
            new = '%dGrv' % max(1, min(GRIEVANCE_MAX, p + 1))
        else:
            res = letters[0] if letters else RES.get(r[di], 'C')
            new = ('%d%s' % (p, res)) if p > 0 else ''
            if extra:
                new = (new + ' ' + extra).strip()
        if new != old:
            changed += 1
            if dry and changed <= 12:
                print('  %-8s %-28s %-8s -> %-8s  %s'
                      % (r[hdr.index('id')], r[hdr.index('name')][:28],
                         old or '(free)', new or '(free)', r[ei][:40]))
        r[ci] = new
    if dry:
        print('  %d of %d costs would change' % (changed, len(body)))
        return
    with open('CARDS.tsv', 'w', newline='') as fh:
        csv.writer(fh, delimiter='\t', lineterminator='\n').writerows([hdr] + body)
    print('  %d of %d costs rewritten' % (changed, len(body)))


main()
