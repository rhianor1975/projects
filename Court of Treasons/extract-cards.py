#!/usr/bin/env python3
"""Pull the design out of design.xml into CARDS.tsv and HOUSES.tsv.

    python3 extract-cards.py

Adapted from the sibling project's extractor, which the design predicted
would need three changes and needed exactly those three: the board block
is gone, because there is no board; cost and reads are carried through,
because they are new; and the deck-count check now closes, because unlike
the parent design the arithmetic here is right.

The only judgement this makes is to say plainly what the design does not
contain.  It contains a seed set, not the full manifest, and it says so
itself -- so the shortfall below is a statement of where the project is,
not a defect report.
"""
import collections
import os
import sys
import xml.etree.ElementTree as ET

root = ET.parse('design.xml').getroot()


def tsv(path, header, rows):
    """Write a table, refusing to shrink one that has outgrown the XML.

    This guard exists because the pipeline changed under it.  design.xml
    is still the authority for the RULES, but the card table is no
    longer generated from it: the pool conversion wrote 1,037 cards into
    CARDS.tsv while the XML's <cards> section still holds the 157 the
    design calls a seed.  The Makefile rule

        CARDS.tsv HOUSES.tsv: design.xml extract-cards.py

    therefore means that editing the authority -- to record a decision,
    which is exactly what it is for -- makes it newer than CARDS.tsv and
    the next `make` silently rebuilds the live set from a sixth of it.

    Nothing would have failed.  The build would have succeeded, the
    tests would have passed, and 880 cards would have been gone.

    So a write that would shed rows stops and says so.  Reconciling the
    two properly -- folding every card back into design.xml so this is
    generated again -- is real work and is queued separately; until then
    this makes the trap loud instead of silent.
    """
    if os.path.exists(path) and '--force' not in sys.argv:
        with open(path, encoding='utf-8') as fh:
            have = max(0, sum(1 for _ in fh) - 1)
        if have > len(rows):
            print('REFUSING to write %s: it holds %d rows and design.xml\n'
                  'would give it %d.  The card table has outgrown the XML;\n'
                  'see the comment in extract-cards.py.  --force overrides.'
                  % (path, have, len(rows)), file=sys.stderr)
            sys.exit(1)
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write('\t'.join(header) + '\n')
        for r in rows:
            fh.write('\t'.join(str(c).replace('\t', ' ').replace('\n', ' ')
                               for c in r) + '\n')
    print('  %-12s %4d rows' % (path, len(rows)), file=sys.stderr)


cards, decks, order = [], collections.Counter(), []
for deck in root.find('cards'):
    if deck.tag != 'deck':
        continue                      # the <note> that opens the section
    name = deck.get('name')
    if name not in order:
        order.append(name)
    for c in deck:
        if c.tag != 'card':
            continue                  # per-deck <note>s
        decks[name] += 1
        cards.append((name, c.get('id') or '', c.get('name') or '',
                      c.get('type') or '', c.get('category') or '',
                      c.get('cost') or '0', c.get('reads') or '',
                      c.get('effect') or ''))
tsv('CARDS.tsv',
    ('deck', 'id', 'name', 'type', 'category', 'cost', 'reads', 'effect'),
    cards)

# The Promise terms.  A Promise card is a form and these are what goes on
# it: what is undertaken, and when it comes due.  Both are in the design
# and neither should be retyped onto a card face by hand.
terms = []
for t in root.find('promises'):
    if t.tag != 'term':
        continue
    terms.append((t.get('name'), t.get('due'), (t.text or '').strip()))
# The Sworn Alliance is Peace and Aid together and breaks as one; the
# design names it in a card rather than in the term list, so it is added
# here and marked as derived.
terms.append(('Alliance', 'Both conditions, and breaking either breaks both',
              'I will keep the peace, and come when called.'))
terms.append(('Any', 'As the term names',
              'The term is named when this is sworn.'))
tsv('TERMS.tsv', ('term', 'due', 'undertaking'), terms)

houses = []
for h in root.find('houses'):
    def bits(k):
        e = h.find(k)
        if e is None:
            return ''
        return ' | '.join((c.text or '').strip()
                          for c in e if (c.text or '').strip())
    houses.append((h.get('name'), h.get('title'), h.get('words'),
                   bits('strengths'), bits('weaknesses'),
                   bits('signature_cards')))
tsv('HOUSES.tsv',
    ('house', 'title', 'words', 'strengths', 'weaknesses', 'signature'),
    houses)

# ------------------------------------------------------------------------
# What the design says it holds, against what it actually carries.
#
# The parent extractor printed this because the parent's total was wrong.
# Here the total is right and the decks are short, which is a different
# thing and reads differently: the design calls its card section a seed,
# so every line below is work not yet done rather than a slip to correct.
declared = {}
for comp in root.find('components'):
    if comp.get('cards'):
        declared[comp.get('name').replace(' Event', '')
                 .replace(' Action', '').replace(' Deck', '')] = \
            int(comp.get('cards')) * int(comp.get('quantity') or 1)
declared['House'] = 100        # per House, and there are four of them
HOUSE_DECKS = ('Ravenmark', 'Vipren', 'Goldwyn', 'Aldemar')

# Several pool deck NAMES are one deck in the manifest and one DeckId in
# the engine.  Counting them apart made this report wrong rather than
# merely incomplete: War read 93 of 150 while Combat's 43 sat unclaimed
# beside it, Intrigue 123 of 220 while Instigator held 29, and The Dead
# read "nothing written" with Ghost and Resurrection's 63 cards directly
# above.  The shortfall came out at 348 when it was 162.
#
# The mapping is the same one gen-cards.py uses; it is written out here
# rather than imported because the two tools answer different questions
# and neither should be able to break the other.
ROLLS_INTO = {'Combat': 'War', 'Council': 'Political', 'Promise': 'Political',
              'Lever': 'Intrigue', 'Minor Lords': 'Intrigue',
              'Instigator': 'Intrigue', 'Cataclysm': 'World',
              'Ghost': 'The Dead', 'Resurrection': 'The Dead'}
for _sub, _parent in ROLLS_INTO.items():
    if decks.get(_sub):
        decks[_parent] = decks.get(_parent, 0) + decks.pop(_sub)
        if _sub in order:
            order.remove(_sub)
        if _parent not in order:
            order.append(_parent)

# A deck in the manifest with nothing written is the case that matters, so
# it gets a line rather than being left out of the loop.  Combat and
# Instigator are both named by the rules -- "each may then play one Combat
# card", "Ravenmark cannot send Instigators" -- and neither has a seed
# card, which is exactly the kind of thing that is cheap to see here and
# expensive to discover from an empty deck at runtime.
listed = list(order) + [n for n in declared
                        if n not in order and n != 'House']
print('\n  deck            written   manifest    short', file=sys.stderr)
have = short = want_total = 0
for name in listed:
    n = decks[name]
    d = declared.get('House' if name in HOUSE_DECKS else name, 0)
    have += n
    want_total += d
    short += max(0, d - n)
    print('  %-15s %5d %10s %8s%s'
          % (name, n, d or '?', (d - n) if d else '?',
             '   <- nothing written' if d and not n else ''),
          file=sys.stderr)

want = int((root.findtext('decks/total_cards') or '0')
           .split()[0].replace(',', '') or 0)
print('\n  %d of %d written; %d still to write.' % (have, want, short),
      file=sys.stderr)
comp = sum(int(c.get('cards')) * int(c.get('quantity') or 1)
           for c in root.find('components') if c.get('cards'))
comp += 400                                    # the four House decks
if comp == want:
    print('  The manifest closes: %d components, <total_cards> %d.'
          % (comp, want), file=sys.stderr)
else:
    print('  components sum to %d but <total_cards> says %d -- an'
          ' arithmetic slip' % (comp, want), file=sys.stderr)
cutel = root.find('decks/cut')
cut = int((cutel.get('cards') if cutel is not None else 0) or 0)
if cut:
    # The shortfall is no longer a conversion backlog.  The pool is
    # spent: everything in it that could be read has been, and the rest
    # is formally cut.  What is left has to be WRITTEN, which is a
    # different kind of work and should not be reported as if a script
    # could still do it.
    print('  The pool is spent: %d of its cards are cut and will not be'
          '\n  written -- see <decks><cut> for what each of them wanted.'
          '\n  The %d above are therefore new cards to write, not a'
          '\n  conversion still to run.' % (cut, short), file=sys.stderr)
else:
    print('  The <cards> section calls itself a seed, so this is the size'
          ' of\n  the remaining conversion pass, not a fault in the design.',
          file=sys.stderr)
