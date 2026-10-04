#!/usr/bin/env python3
"""The conversion pass: 1,035 parent cards -> this game's pool.

    python3 convert-parent.py            write CONVERTED.tsv and report
    python3 convert-parent.py --report   report only

<conversion> in design.xml describes four passes over the pool in
treasons-c and says the third is the only one needing judgement.  Having
run them, that is not quite right, and the differences are printed rather
than smoothed over -- a conversion that quietly disagrees with the design
it is converting for is how 1,000 cards go wrong at once.
"""
import argparse
import collections
import csv
import os
import re
import sys
import xml.etree.ElementTree as ET

PARENT = os.environ.get('PARENT_DESIGN', '../treasons-c/design.xml')

# Parent deck -> this game's deck.  The Gauntlet becomes Ambition: "the
# goals were already cards in the parent design; only the track needed
# removing."  Promise, Lever and Court have no parent and are not here.
DECK_MAP = {
    'Ravenmark': 'Ravenmark', 'Vipren': 'Vipren', 'Goldwyn': 'Goldwyn',
    'Aldemar': 'Aldemar', 'War': 'War', 'Political': 'Political',
    'Intrigue': 'Intrigue', 'World Event': 'World', 'Combat': 'Combat',
    'Ghost': 'Ghost', 'Cataclysm': 'Cataclysm', 'Council': 'Council',
    'Resurrection': 'Resurrection', 'Instigator': 'Instigator',
    'Gauntlet': 'Ambition', 'Throne': 'Throne',
}

# Pass 5, which <conversion> does not mention.  The parent's type and
# category vocabularies are not this game's.  Its "type" is mostly the
# name of the deck the card is in -- Political, Intrigue, Ghost -- and it
# carries 36 categories against the 8 in <card_schema>.  Neither maps
# itself, and a card whose type is "Political" is not a card the engine
# can do anything with.
TYPE_MAP = {
    'Play on turn': 'Play on turn',
    'Play anytime': 'Instant',
    'Reaction': 'Reaction',
    # Combat-deck subtypes.  All of them are played inside a combat, which
    # in this game is a Reaction: "Each may then play one Combat card."
    'Attack': 'Reaction', 'Defense': 'Reaction', 'Trick': 'Reaction',
    'Betrayal': 'Reaction', 'Terrain': 'Reaction', 'Combat': 'Reaction',
    # Types that are really deck names.  The deck decides: a World or
    # Council or Cataclysm card resolves as it is drawn, a Gauntlet card
    # is an Ambition, and the rest are ordinary plays.
    'World Event': 'Resolve on draw', 'Council Event': 'Resolve on draw',
    'Cataclysm': 'Resolve on draw', 'Gauntlet': 'Ambition',
    'Political': 'Play on turn', 'Intrigue': 'Play on turn',
    'War': 'Play on turn', 'Throne': 'Play on turn',
    'Ghost': 'Play on turn', 'Resurrection': 'Play on turn',
    'Instigator': 'Play on turn',
}

# 36 parent categories onto the 8 this game has.  Where a parent category
# splits across two, it goes to the one the effect is about rather than
# the one it is named after: Assassination is Core because it is a thing
# you do, not a kind of combat.
CATEGORY_MAP = {
    'Core': 'Core', 'Minor': 'Core', 'Major': 'Core', 'Unique': 'Core',
    'Reaction': 'Core', 'Command': 'Core', 'Assassination': 'Core',
    'Shadow': 'Core', 'Secrets': 'Core', 'Manipulation': 'Core',
    'Intrigue': 'Core',
    'Economy': 'Growth', 'Gold': 'Growth',
    'Combat': 'Combat', 'Attack': 'Combat', 'Defense': 'Combat',
    'Battles': 'Combat', 'Armies': 'Combat', 'Conquest': 'Combat',
    'Trick': 'Combat', 'Terrain': 'Combat',
    'Revolution': 'Bond', 'Alliances': 'Bond', 'Betrayal': 'Bond',
    'Council': 'Council', 'Votes': 'Council', 'Politics': 'Council',
    'Legitimacy': 'Council',
    'WorldEvent': 'Chaos', 'Cataclysm': 'Chaos',
    'Cataclysm Trigger': 'Chaos',
    # The Advanced- categories are the parent's gauntlet goals, which
    # become Ambitions, and an Ambition's category says what kind of goal
    # it is.
    'Advanced-3Vassals': 'Bond',
    'Advanced-LordParamount': 'Core',
    'Advanced-SurvivedRevolt': 'Bond',
    'Advanced-InnerRing': 'Core',
    'Advanced-EliminatedPlayer': 'Combat',
}

MOVEMENT = re.compile(r'\b(move|moves|moving|movement|spaces?|steps?)\b', re.I)
CASTLE   = re.compile(r'\b(castles?|sieges?|besiege|walls)\b', re.I)
GAUNTLET = re.compile(r'\b(gauntlet|inner ring|outer ring)\b', re.I)

# Pass 2.  "Effects name Levy or Standing explicitly.  '+2 Military' alone
# is ambiguous and is not acceptable in this design; the parent game's
# classifier could not tell them apart and it cost a fortnight."
PERMANENT = re.compile(r'\bpermanent(ly)?\b', re.I)
TRANSIENT = re.compile(r'\b(this round|this turn|this combat|when defending|'
                       r'when attacking|until end of)\b', re.I)
STAT = re.compile(r'([+-]\s*\d+)\s+(Political Capital|Military|Capital|Gold)'
                  r'\b(?!\s+(?:Levy|Standing))')

READS = [
    (re.compile(r'\bbroken (word|promise|oath)', re.I), 'broken_words'),
    (re.compile(r'\bword kept|kept a promise|kept promise', re.I), 'word_kept'),
    (re.compile(r'\bgrievance\b', re.I), 'grievance'),
    (re.compile(r'\bunrest\b', re.I), 'unrest'),
    (re.compile(r'\bthroneworthy\b', re.I), 'throneworthy'),
    (re.compile(r'\bfavour\b', re.I), 'favour'),
]


def disambiguate(text):
    """Return (text, how) with every bare stat named Levy or Standing."""
    if not STAT.search(text):
        return text, 'no stat'
    permanent = bool(PERMANENT.search(text))
    transient = bool(TRANSIENT.search(text))
    how = ('permanent' if permanent else
           'transient' if transient else 'defaulted to Levy')

    def sub(m):
        amt, stat = m.group(1).replace(' ', ''), m.group(2)
        if stat == 'Political Capital':
            stat = 'Capital'
        if permanent:
            return '%s %s Standing permanently' % (amt, stat)
        return '%s %s Levy' % (amt, stat)

    out = STAT.sub(sub, text)
    # "+2 Military Levy this round" is the parent's wording; this game
    # sets Levy per turn and loses it at end of turn, so a round is a turn.
    out = re.sub(r'\bthis round\b', 'this turn', out)
    out = re.sub(r'\bPolitical Capital\b', 'Capital', out)
    # a Standing clause that then says "permanently" says it twice
    out = re.sub(r'Standing permanently(.*?)\bpermanently\b', r'Standing permanently\1', out)
    if permanent and not TRANSIENT.search(out) and 'Standing permanently' not in out:
        pass
    return out, how


def price(text, deck):
    """A first pass at a cost, generated from the size of the effect.

    "A first pass can be generated from the size of the effect and then
    tuned by soak, but it is the pass that decides whether the game has a
    curve."  So this is deliberately crude and deliberately reported: it
    is a starting point for the soak, not an answer.
    """
    n = sum(int(m) for m in re.findall(r'[+-](\d+)', text)) or 1
    permanent = bool(PERMANENT.search(text))
    res = ('M' if re.search(r'\bMilitary\b', text) else
           'C' if re.search(r'\bCapital\b|\bvote', text, re.I) else 'G')
    if permanent:
        return '%d%s' % (max(3, n + 2), res)          # land-like, expensive
    if re.search(r'\b(every|all|each) (other )?House\b', text, re.I):
        return '%d%s 1Grv' % (max(2, n + 1), res)     # table-wide costs more
    return '%d%s' % (max(1, n), res)


def reads_of(text):
    return ' '.join(tag for rx, tag in READS if rx.search(text))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--report', action='store_true')
    a = ap.parse_args()

    if not os.path.exists(PARENT):
        print('parent design not found at %s' % PARENT, file=sys.stderr)
        return 1
    root = ET.parse(PARENT).getroot()

    # Names already written by hand in this game's seed.  The seed is
    # itself a hand conversion of the first cards of each parent deck --
    # Conscription is parent RAV010 and this game's RAV002 -- so matching
    # on name is what stops the pool carrying two of everything.
    seen = collections.defaultdict(set)
    if os.path.exists('CARDS.tsv'):
        for r in csv.DictReader(open('CARDS.tsv', encoding='utf-8'),
                                delimiter='\t'):
            seen[r['deck']].add(r['name'].lower())
    already = sum(len(v) for v in seen.values())

    cut = collections.Counter()
    cuts = []
    out = []
    how = collections.Counter()
    unmapped_type = collections.Counter()
    unmapped_cat = collections.Counter()
    dupes = 0

    for deck in root.find('cards'):
        if deck.tag != 'deck':
            continue
        target = DECK_MAP.get(deck.get('name'))
        if target is None:
            continue
        for c in deck:
            if c.tag != 'card':
                continue
            eff = c.get('effect') or ''
            name = c.get('name') or ''
            why = ('movement' if MOVEMENT.search(eff) else
                   'castles and sieges' if CASTLE.search(eff) else
                   'the gauntlet' if GAUNTLET.search(eff) else None)
            if why:
                cut[why] += 1
                cuts.append((target, c.get('id'), name, why, eff))
                continue
            if name.lower() in seen[target]:
                dupes += 1
                continue
            text, h = disambiguate(eff)
            how[h] += 1
            ptype = c.get('type') or 'Play on turn'
            pcat = c.get('category') or 'Core'
            ctype = TYPE_MAP.get(ptype)
            ccat = CATEGORY_MAP.get(pcat)
            if ctype is None:
                unmapped_type[ptype] += 1
                ctype = 'Play on turn'
            if ccat is None:
                unmapped_cat[pcat] += 1
                ccat = 'Core'
            # An Ambition is checked, never played, and the deck decides:
            # everything in the Ambition deck is one whatever its parent
            # type said.
            if target == 'Ambition':
                ctype = 'Ambition'
            out.append((target, c.get('id'), name, ctype, ccat,
                        '0' if ctype == 'Ambition' else price(text, target),
                        reads_of(text), text))

    print('\n  PASS 1 -- drop the spatial\n', file=sys.stderr)
    for k, v in cut.most_common():
        print('    %-22s %4d' % (k, v), file=sys.stderr)
    print('    %-22s %4d  (%.1f%% of 1,035)'
          % ('total cut', sum(cut.values()), 100 * sum(cut.values()) / 1035),
          file=sys.stderr)
    print('\n    <conversion> says "19 cards name movement or spaces ... under'
          '\n    2% of the pool and the whole price of removing the board".'
          '\n    Movement alone is close to that.  But <ancestry> also removes'
          '\n    castles and sieges, and <ambitions> removes the gauntlet, and'
          '\n    neither is counted in the 19.  The real price is the total'
          '\n    above, which is over three times what the design states.',
          file=sys.stderr)

    print('\n  PASS 2 -- disambiguate the stats\n', file=sys.stderr)
    for k, v in how.most_common():
        print('    %-22s %4d' % (k, v), file=sys.stderr)
    print('\n    "defaulted to Levy" is the count that needs a human: the'
          '\n    effect names a stat and says nothing about duration, so the'
          '\n    safe reading is the one that does not compound.',
          file=sys.stderr)

    print('\n  PASS 3 -- price them\n', file=sys.stderr)
    print('    %4d cards priced from the size of the effect' % len(out),
          file=sys.stderr)
    print('    Crude on purpose.  The design calls this "a first pass ...'
          '\n    then tuned by soak" and "the pass that decides whether the'
          '\n    game has a curve", so it is a starting point to measure,'
          '\n    not an answer to trust.', file=sys.stderr)

    print('\n  PASS 4 -- fill the reads\n', file=sys.stderr)
    rc = collections.Counter()
    for r in out:
        for t in r[6].split():
            rc[t] += 1
    for k, v in rc.most_common():
        print('    %-22s %4d' % (k, v), file=sys.stderr)

    print('\n  PASS 5 -- the vocabularies, which <conversion> does not mention\n',
          file=sys.stderr)
    print('    The parent\'s "type" is mostly the name of its deck, and it'
          '\n    carries 36 categories against the 8 in <card_schema>.',
          file=sys.stderr)
    tc = collections.Counter(r[3] for r in out)
    cc = collections.Counter(r[4] for r in out)
    print('\n    types:      ' + ', '.join('%s %d' % kv for kv in tc.most_common()),
          file=sys.stderr)
    print('    categories: ' + ', '.join('%s %d' % kv for kv in cc.most_common()),
          file=sys.stderr)
    if unmapped_type or unmapped_cat:
        print('\n    UNMAPPED (defaulted, and each one needs a decision):',
              file=sys.stderr)
        for k, v in list(unmapped_type.items()) + list(unmapped_cat.items()):
            print('      %-26s %4d' % (k, v), file=sys.stderr)

    print('\n  RESULT\n', file=sys.stderr)
    print('    %4d converted' % len(out), file=sys.stderr)
    print('    %4d cut as spatial' % sum(cut.values()), file=sys.stderr)
    print('    %4d skipped: already written by hand in the seed' % dupes,
          file=sys.stderr)
    print('    %4d cards in the seed already' % already, file=sys.stderr)
    print('    %4d total, against a manifest of 1,135'
          % (len(out) + already), file=sys.stderr)

    if a.report:
        return 0
    hdr = ('deck', 'id', 'name', 'type', 'category', 'cost', 'reads', 'effect')
    with open('CONVERTED.tsv', 'w', encoding='utf-8') as fh:
        fh.write('\t'.join(hdr) + '\n')
        for r in out:
            fh.write('\t'.join(str(x).replace('\t', ' ') for x in r) + '\n')
    with open('CUT.tsv', 'w', encoding='utf-8') as fh:
        fh.write('deck\tid\tname\twhy\teffect\n')
        for r in cuts:
            fh.write('\t'.join(str(x).replace('\t', ' ') for x in r) + '\n')
    print('\n    CONVERTED.tsv  %d rows\n    CUT.tsv        %d rows\n'
          % (len(out), len(cuts)), file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
