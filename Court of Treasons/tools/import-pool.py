#!/usr/bin/env python3
"""Move every pool card the classifier can now read into the live set.

    python3 tools/import-pool.py            import
    python3 tools/import-pool.py --dry-run  say what would come in

POOL.tsv holds the whole manifest; CARDS.tsv holds what is playable.  A
card moves across when gen-cards.py can read its effect and not before,
because a card whose effect compiles to nothing is an inert play, and
the inert-play measure would then report it as a design fault rather
than as the missing pattern it is.

Two rules about WHERE a card lands, both learned the hard way:

  - gen-cards.py slices DECK_FIRST/DECK_SIZE out of contiguous runs, so
    a card must be inserted at the end of its own deck's block and never
    appended to the file.  The generator asserts this, which is the only
    reason the mistake was cheap.
  - The dead-player decks are held back.  Ghost, Resurrection and
    Cataclysm are four-player content and whether a duel has dead
    players at all is a design question nobody has answered.
"""
import collections
import contextlib
import csv
import importlib.util
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(os.path.dirname(HERE))

# gen-cards.py reads sys.argv, so this one is kept before handing it a
# fake.  Overwriting it first made --dry-run invisible and the first
# "dry" run imported for real.
ARGV = list(sys.argv)

spec = importlib.util.spec_from_file_location('gc', 'gen-cards.py')
gc = importlib.util.module_from_spec(spec)
sys.argv = ['gen-cards.py']
with contextlib.redirect_stdout(io.StringIO()):
    try:
        spec.loader.exec_module(gc)
    except SystemExit:
        pass

# Ghost and Resurrection are still held: whether a duel has a fallen
# House that can return is being settled separately.  Cataclysm is not
# -- design.xml puts it in the standard game.
HELD = set()   # Ghost and Resurrection released 2026-09-20

# Pool deck names that share an engine DeckId with another name.  Taken
# from the DECK map in gen-cards.py, which is where the truth is.
SIBLING = {'Resurrection': 'Ghost', 'Combat': 'War', 'Council': 'Political',
           'Promise': 'Political', 'Lever': 'Intrigue',
           'Minor Lords': 'Intrigue', 'Instigator': 'Intrigue'}
# Opcodes that only mean anything while a combat is being resolved.
COMBAT_ONLY = {'OP_COMBAT_LEVY', 'OP_COMBAT_LEVY_DEF', 'OP_COMBAT_REDUCE',
               'OP_COMBAT_ATT_LOSS'}
# Concepts that need a third House to mean anything.  A duel has no ally
# to betray and nobody to come back as a ghost.
# Concepts that still need a third House or a settled ruling.  Alliance
# words are NOT here any more: an alliance is a Promise in this design
# and the ones that map are classified like anything else -- what is
# left out is what the classifier still cannot read, which is the same
# gate every other card passes through.
# "Another player" needed a third House at a table of four; in a duel it
# is the other House and nothing else, so it is no longer held -- that
# alone was keeping six perfectly ordinary Gold transfers out.  What
# stays held genuinely needs a third party present.
MULTI = re.compile(r'\b(other players|each other player|'
                   r'all other players|two other|third)\b', re.I)


def main():
    dry = '--dry-run' in ARGV
    rows = list(csv.reader(open('CARDS.tsv'), delimiter='\t'))
    hdr, body = rows[0], rows[1:]
    live = {r[1] for r in body}
    # The pool repeats a card within a deck on purpose: these are extra
    # COPIES, with their own ids, and the live set already holds plenty
    # -- Vipren has The Long Night three times.  Dropping them because
    # the name matched was wrong and cost nine cards.  Only an id
    # already present is a real duplicate, and that is checked below.
    add = collections.defaultdict(list)
    three = []
    for r in csv.DictReader(open('POOL.tsv'), delimiter='\t'):
        eff = (r['effect'] or '').strip()
        if r['id'] in live or r['deck'] in HELD:
            continue
        if MULTI.search(eff):
            continue
        hits = gc.classify(eff)
        if not hits:
            continue
        # A Card holds two effects.  gen-cards.py refuses a third
        # rather than dropping it quietly -- "A card compiled to
        # OP_NONE is an inert play, and the inert-play measure would
        # then report it as a design fault" -- so a three-clause card
        # is held back here instead of breaking the build later.
        if len(hits) > 2:
            three.append((r['id'], r['name'], eff))
            continue
        # An Ambition is a goal, not a play, and src/tests.c refuses a
        # card table where one has a playable effect.  The pool files
        # several holdings from the parent game into the Ambition deck
        # with ordinary effects on them -- The Treasury, "+3 Gold Levy"
        # -- and they are not Ambitions whatever the column says.
        amb_deck = r['deck'] == 'Ambition' or r['type'] == 'Ambition'
        amb_eff = all((h[0] if isinstance(h, (list, tuple)) else h)
                      .startswith('OP_AMB_') for h in hits)
        if amb_deck != amb_eff:
            continue
        # A card whose whole effect happens inside a combat is a
        # Reaction, whatever the pool's type column says.  The pool
        # files the same card both ways -- The Boiling Oil is a
        # Reaction in the Combat deck and "Play on turn" in the War
        # deck -- and the "Play on turn" copies fire on your own turn,
        # when there is no attacker to hurt and no fight to reinforce,
        # so they resolve to nothing and land in the inert count.
        typ = r['type']
        # A proposal is an action, not an effect: OP_PROPOSE returns 0,
        # so a card offering a Promise must be typed Promise or it plays
        # as an ordinary card and does nothing.
        if any((h[0] if isinstance(h, (list, tuple)) else h) == 'OP_PROPOSE'
               for h in hits):
            typ = 'Promise'
        elif typ != 'Reaction' and all(
                (h[0] if isinstance(h, (list, tuple)) else h) in COMBAT_ONLY
                for h in hits):
            typ = 'Reaction'
        add[r['deck']].append([r['deck'], r['id'], r['name'], typ,
                               r['category'], r['cost'], r['reads'], eff])
    total = sum(len(v) for v in add.values())
    if three:
        print('  %d held back with three clauses (a Card holds two):'
              % len(three), file=sys.stderr)
        for cid, nm, eff in three:
            print('    %-8s %-22s %s' % (cid, nm[:22], eff[:52]),
                  file=sys.stderr)
    if dry:
        for d in sorted(add):
            print('  %-12s +%d' % (d, len(add[d])))
        print('  %d cards would come in' % total)
        return
    out = []
    for i, row in enumerate(body):
        out.append(row)
        nxt = body[i + 1][0] if i + 1 < len(body) else None
        if nxt != row[0] and row[0] in add:
            out.extend(add.pop(row[0]))
    # A deck with nothing live yet has no block to append to, which is a
    # real case: Cataclysm opened with no cards in the seed set at all.
    # Its rows go at the end of the file, where they form their own
    # contiguous run -- which is all DECK_FIRST/DECK_SIZE require.  They
    # are named on the way in rather than appearing silently.
    # Several pool deck NAMES share one engine DeckId -- Ghost and
    # Resurrection are both D_DEAD, Combat is D_WAR, Council is
    # D_POLITICAL -- and DECK_FIRST slices one contiguous run per
    # DeckId, not per name.  So a name with no block of its own goes
    # beside a sibling that shares its DeckId, and only a genuinely new
    # DeckId starts a run at the end.  Appending Resurrection to the
    # file while Ghost sat elsewhere split D_DEAD in two, which
    # gen-cards.py refused to build -- correctly.
    for d in sorted(add):
        sib = SIBLING.get(d)
        if sib:
            at = max(i for i, r in enumerate(out) if r[0] == sib) + 1
            print('  %s: %d cards, placed with %s (same deck)'
                  % (d, len(add[d]), sib), file=sys.stderr)
            out[at:at] = add[d]
        else:
            print('  %s: new deck, %d cards appended as its own block'
                  % (d, len(add[d])), file=sys.stderr)
            out.extend(add[d])
    with open('CARDS.tsv', 'w', newline='') as fh:
        csv.writer(fh, delimiter='\t', lineterminator='\n').writerows([hdr] + out)
    print('  %d -> %d cards (+%d)' % (len(body), len(out), len(out) - len(body)))


main()
